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

static int igpthread_create (pthread_t *thread, pthread_attr_t *attr,
			     void * (*start_routine) (void *), void *arg);
IGPROF_HOOK (int (pthread_t *thread, pthread_attr_t *attr,
		  void * (*start_routine) (void *), void *arg),
	     pthread_create, igpthread_create);

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
    IgHookTrace	*node = IgProf::root ();
    void	*addresses [128];
    int		depth = IgHookTrace::stacktrace (addresses, 128);

    // Walk the tree
    for (int i = depth-2; i >= drop; --i)
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
    if (IgProf::isMultiThreaded ())
    {
	IgProfLock lock (s_enabled);

#if __linux
	if (lock.enabled () > 0 && pthread_self () == s_profthread)
	{
	    pthread_mutex_lock (&s_lock);
	    static int sigs = 0;
	    if (sigs++ % 1000 == 0)
		IgProf::debug ("%d profiling signals received\n", sigs);

	    for (int i = 0; i < s_nthreads; ++i)
		pthread_kill (s_threads [i], SIGPROF);
	    pthread_mutex_unlock (&s_lock);
	}
	else if (lock.enabled () > 0)
#endif
	    add ();
    }
    else if (s_enabled > 0)
	add ();
}

/** Enable profiling timer.  You should have called
    #enableSignalHandler() before calling this function.  */
static void
enableTimer (void)
{
    itimerval interval = { { 0, 1000 }, { 0, 1000 } };
    setitimer (ITIMER_PROF, &interval, 0);
}

/** Disable profiling timer.  */
static void
disableTimer (void)
{
    itimerval interval = { { 0, 0 }, { 0, 0 } };
    setitimer (ITIMER_PROF, &interval, 0);
}

/** Enable SIGPROF signal handler.  */
static void
enableSignalHandler (void)
{ signal (SIGPROF, (sighandler_t) &profileSignalHandler); }

/** Disable SIGPROF signal handler.  */
static void
disableSignalHandler (void)
{ signal (SIGPROF, SIG_IGN); }

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

/** A wrapper for starting user threads to enable SIGPROF signal handling.  */
static void *
threadWrapper (void *arg)
{
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

    IgProfPerfWrappedArg *wrapped = (IgProfPerfWrappedArg *) arg;
    void *(*start_routine) (void*) = wrapped->start_routine;
    void *start_arg = wrapped->arg;
    delete wrapped;
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
        IgHook::hook (igpthread_create_hook.raw);

	IgProf::onactivate (&IgProfPerf::enable);
	IgProf::ondeactivate (&IgProfPerf::disable);

	enableSignalHandler ();
	enableTimer ();

	IgProfPerf::enable ();
    }
}

/** Enable performance profiler.  */
void
IgProfPerf::enable (void)
{ s_enabled++; enableSignalHandler (); enableTimer (); }

/** Disable performance profiler.  */
void
IgProfPerf::disable (void)
{ disableTimer (); disableSignalHandler (); s_enabled--; }

/** Override for user thread creation to make sure SIGPROF signal is
    also sent and handled in the user thread.  */
static int
igpthread_create (pthread_t *thread,
		  pthread_attr_t *attr,
		  void * (*start_routine) (void *),
		  void *arg)
{
    IgProfPerfWrappedArg *wrapped = new IgProfPerfWrappedArg;
    wrapped->start_routine = start_routine;
    wrapped->arg = arg;
    return (*igpthread_create_hook.typed.chain)
	(thread, attr, &threadWrapper, wrapped);
}

// Auto start this profiling module
static bool autoboot = (IgProfPerf::initialize (), true);
