//<<<<<< INCLUDES                                                       >>>>>>

#include "Ig_Tools/IgProf/src/IgProfPerf.h"
#include "Ig_Tools/IgProf/src/IgProf.h"
#include "Ig_Tools/IgHook/interface/IgHook.h"
#include "Ig_Tools/IgHook/interface/IgHookTrace.h"
#include "Ig_Tools/IgHook/interface/IgHookLiveMap.h"
#include <cstdlib>
#include <signal.h>
#include <pthread.h>
#include <sys/time.h>
#include <unistd.h>
#include <dlfcn.h>

//<<<<<< PRIVATE DEFINES                                                >>>>>>
//<<<<<< PRIVATE CONSTANTS                                              >>>>>>
//<<<<<< PRIVATE TYPES                                                  >>>>>>

// Used to capture real user start arguments in our custom thread wrapper
struct IgProfPerfWrappedArg
{
    void * (*start_routine) (void *);
    void *arg;
};

#ifdef __APPLE__
typedef sig_t sighandler_t;
#endif

//<<<<<< PRIVATE VARIABLE DEFINITIONS                                   >>>>>>
//<<<<<< PUBLIC VARIABLE DEFINITIONS                                    >>>>>>
//<<<<<< CLASS STRUCTURE INITIALIZATION                                 >>>>>>
//<<<<<< PRIVATE FUNCTION DEFINITIONS                                   >>>>>>

// Traps for this profiler module
IGPROF_LIBHOOK (4, int, dopthread_create, _main,
	        (pthread_t *thread, const pthread_attr_t *attr,
		 void * (*start_routine)(void *), void *arg),
		(thread, attr, start_routine, arg),
	        "pthread_create", 0, 0)

IGPROF_LIBHOOK (4, int, dopthread_create, _pthread20,
	        (pthread_t *thread, const pthread_attr_t *attr,
		 void * (*start_routine)(void *), void *arg),
		(thread, attr, start_routine, arg),
	        "pthread_create", "GLIBC_2.0", 0)

IGPROF_LIBHOOK (4, int, dopthread_create, _pthread21,
	        (pthread_t *thread, const pthread_attr_t *attr,
		 void * (*start_routine)(void *), void *arg),
		(thread, attr, start_routine, arg),
	        "pthread_create", "GLIBC_2.1", 0)

// Data for this profiler module
static IgHookTrace::Counter	s_ct_ticks	= { "PERF_TICKS" };
static int			s_enabled	= 0;
static bool			s_initialized	= false;
#if __linux
static pthread_mutex_t		s_lock		= PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t		s_sigstart	= PTHREAD_MUTEX_INITIALIZER;
static int			s_nthreads	= 0;
static pthread_t		s_threads [1024];
static pthread_t		s_profthread;
#endif

/** Record a tick.  Increments counters in the tree for ticks.  */
static void 
add (void)
{
    int		drop = 2; // one for stacktrace, one for me
    IgHookTrace	*node = IgProf::threadRoot ();
    void	*addresses [128];
    int		depth = IgHookTrace::stacktrace (addresses, 128);

    // Walk the tree
    for (int i = depth-2; node && i >= drop; --i)
	node = node->child (IgHookTrace::tosymbol (addresses [i]));

    // Increment counters for this node
    node->counter (&s_ct_ticks)->tick ();
}

/** SIGPROF signal handler.  Record a tick for the current program
    location.  For POSIX-conforming systems assumes the signal handler
    is registered for the correct thread.  For non-conforming old
    GNU/Linux systems assumes there is a separate thread listening for
    the SIGPROF signals, and broadcasts the signal to the other
    threads.  Skips ticks when the profiler is not enabled.  */
static void
profileSignalHandler (void)
{
    if (s_enabled <= 0)
	return;

#if __linux
    if (IgProf::isMultiThreaded () && pthread_self () == s_profthread)
    {
	pthread_mutex_lock (&s_lock);
	for (int i = 0; i < s_nthreads; ++i)
	    pthread_kill (s_threads [i], SIGPROF);
	pthread_mutex_unlock (&s_lock);
    }
    else
#endif
	add ();
}

/** Enable profiling timer.  You should have called
    #enableSignalHandler() before calling this function.
    This needs to be executed in every thread to be profiled. */
static void
enableTimer (void)
{
    itimerval interval = { { 0, 1000 }, { 0, 1000 } };
    setitimer (ITIMER_PROF, &interval, 0);
}

/** Enable SIGPROF signal handler.  */
static void
enableSignalHandler (void)
{ signal (SIGPROF, (sighandler_t) &profileSignalHandler); }

#if __linux
/** Old GNU/Linux system hack.  Make sure the signal handling thread
    signal mask is restored after fork().  */
static void
profileSignalThreadFork (void)
{
    enableSignalHandler ();
    enableTimer ();
}

/** Old GNU/Linux system hack.  Worker thread for receiving SIGPROF
    signals on behalf of all the other threads.  */
static void *
profileSignalThread (void *)
{
    IgProf::initThread ();
    IgProf::debug ("Perf: starting signal handler thread %lu\n",
		   (unsigned long) pthread_self ());
    pthread_atfork (0, 0, &profileSignalThreadFork);
    enableSignalHandler ();
    enableTimer ();
    pthread_mutex_unlock (&s_sigstart);
    
    while (true)
	sleep (1);

    return 0;
}

/** Old GNU/Linux system hack.  If the process is multi-threaded
    (linked against pthreads), start a a worker thread to listen for
    the SIGPROF signals.  */
static void
enableSignalThread (void)
{
    if (IgProf::isMultiThreaded ())
    {
	// Start the thread and wait until it's running.
	pthread_mutex_lock (&s_sigstart);
	pthread_create (&s_profthread, 0, &profileSignalThread, 0);
	pthread_mutex_lock (&s_sigstart);
    }
}

/** Old GNU/Linux system hack.  Record this thread to the table of
    threads we need to pass SIGPROF for.  */
static void
registerThisThread (void)
{
    IgProf::debug ("Perf: registering thread %lu\n", (unsigned long) pthread_self ());
    pthread_mutex_lock (&s_lock);
    IGPROF_ASSERT (s_nthreads < 1024);
    s_threads [s_nthreads++] = pthread_self ();
    pthread_mutex_unlock (&s_lock);
}
#endif

/** A wrapper for starting user threads to enable profiling.  */
static void *
threadWrapper (void *arg)
{
    IgProf::initThread ();

#if __linux
    // Old GNU/Linux system hack: make sure signal worker sends the
    // signal to this thread too.
    registerThisThread ();
    enableSignalHandler ();
#endif

    sigset_t profset;
    sigemptyset (&profset);
    sigaddset (&profset, SIGPROF);
    pthread_sigmask (SIG_UNBLOCK, &profset, 0);

    // Enable profiling in this thread.
    enableTimer ();

    // Get arguments
    IgProfPerfWrappedArg *wrapped = (IgProfPerfWrappedArg *) arg;
    void *(*start_routine) (void*) = wrapped->start_routine;
    void *start_arg = wrapped->arg;
    delete wrapped;

    // Start the user thread
    IgProf::debug ("Perf: captured thread %lu for profiling (%p, %p)\n",
		   (unsigned long) pthread_self (),
		   (void *) start_routine,
		   start_arg);
    return (*start_routine) (start_arg);
}

//<<<<<< PUBLIC FUNCTION DEFINITIONS                                    >>>>>>
//<<<<<< MEMBER FUNCTION DEFINITIONS                                    >>>>>>

/** Possibly start performance profiler.  */
void
IgProfPerf::initialize (void)
{
    if (s_initialized) return;
    s_initialized = true;

    IgProf::initialize ();
    IgProf::debug ("Performance profiler loaded\n");

    const char	*options = IgProf::options ();
    bool	enable = false;

    while (options && *options)
    {
	while (*options == ' ' || *options == ',')
	    ++options;

	if (! strncmp (options, "perf", 4))
	{
	    enable = true;
	    options += 4;
	}
	else
	    options++;

	while (*options && *options != ',' && *options != ' ')
	    options++;
    }

    if (enable)
    {
	// Enable profiler.  On old GNU/Linux systems, also start a
	// signal handling worker thread that mirrors the SIGPROF
	// signal to all real user-threads.
	IgProf::debug ("Performance profiler enabled\n");
#if __linux
	registerThisThread ();
	enableSignalThread ();
#endif
	IgHook::hook (dopthread_create_hook_main.raw);
#if __linux
	IgHook::hook (dopthread_create_hook_pthread20.raw);
	IgHook::hook (dopthread_create_hook_pthread21.raw);
#endif
	IgProf::onactivate (&IgProfPerf::enable);
	IgProf::ondeactivate (&IgProfPerf::disable);

	IgProf::root ();
	IgProf::threadRoot ();

	enableSignalHandler ();
	enableTimer ();

	IgProfPerf::enable ();
    }
}

/** Enable this profiling module.  Only call within #IgProfLock.
    Normally called automatically through activation by #IgProfLock.
    Allows recursive enable/disable.  */
void
IgProfPerf::enable (void)
{ s_enabled++; }

/** Disable this profiling module.  Only call within #IgProfLock.
    Normally called automatically through activation by #IgProfLock.
    Allows recursive enable/disable.  */
void
IgProfPerf::disable (void)
{ s_enabled--; }

//////////////////////////////////////////////////////////////////////
// Trap thread creation to enable profiling timers and signal handling.
static int
dopthread_create (IgHook::SafeData<igprof_dopthread_create_t> &hook,
		  pthread_t *thread,
		  const pthread_attr_t *attr,
		  void * (*start_routine) (void *),
		  void *arg)
{
    IgProfPerfWrappedArg *wrapped = new IgProfPerfWrappedArg;
    wrapped->start_routine = start_routine;
    wrapped->arg = arg;
    int ret = hook.chain (thread, attr, &threadWrapper, wrapped);
    return ret;
}

//////////////////////////////////////////////////////////////////////
static bool autoboot = (IgProfPerf::initialize (), true);
