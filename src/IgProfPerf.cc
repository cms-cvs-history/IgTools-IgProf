//<<<<<< INCLUDES                                                       >>>>>>

#include "Ig_Tools/IgProf/src/IgProfPerf.h"
#include "Ig_Tools/IgProf/src/IgProf.h"
#include "Ig_Tools/IgHook/interface/IgHook.h"
#include "Ig_Tools/IgHook/interface/IgHookTrace.h"
#include "Ig_Tools/IgHook/interface/IgHookLiveMap.h"
#include <cassert>
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
static IgHookTrace::Counter	s_ct_cumticks	= { "PERF_CUM_TICKS" };
static int			s_enabled	= 0;
static bool			s_initialized	= false;
#if __linux
static pthread_mutex_t		s_lock		= PTHREAD_MUTEX_INITIALIZER;
static int			s_nthreads	= 0;
static pthread_t		s_threads [1024];
static pthread_t		s_profthread;
#endif
static itimerval		s_interval;

/** Record a tick.  Increments counters in the tree for ticks.  */
static void 
add (void)
{
    int		drop = 2; // one for stacktrace, one for me
    IgHookTrace	*node = IgProf::root ();
    void	*addresses [128];
    int		depth = IgHookTrace::stacktrace (addresses, 128);

    // Increment cumulative counters for higher in the tree
    for (int i = depth-2; i >= drop; --i)
    {
	node->counter (&s_ct_cumticks)->tick ();
	node = node->child (IgHookTrace::tosymbol (addresses [i]));
    }

    // Increment counters for this node
    node->counter (&s_ct_ticks)->tick ();
}

/** Check if the program we are running on was linked against pthreads.  */
static bool
isMultiThreaded (void)
{ return igpthread_create_hook.typed.chain != 0; }

/** SIGPROF signal handler.  Record a tick for the current program
    location.  For POSIX-conforming systems assumes the signal handler
    is registered for the correct thread.  For non-conforming old
    GNU/Linux systems assumes there is a separate thread listening for
    the SIGPROF signals, and broadcasts the signal to the other
    threads.  Skips ticks when the profiler is not enabled.  */
static void
profileSignalHandler (void)
{
    if (isMultiThreaded ())
    {
	IgProfLock lock (s_enabled);

#if __linux
	if (igpthread_create_hook.typed.chain && pthread_self () == s_profthread)
	{
	    pthread_mutex_lock (&s_lock);
	    static int sigs = 0;
	    if (sigs++ % 1000 == 0)
		IgProf::debug ("%d profiling signals received", sigs);

	    for (int i = 0; i < s_nthreads; ++i)
		pthread_kill (s_threads [i], SIGPROF);

	    pthread_mutex_unlock (&s_lock);
	}
	else if (lock.enabled ())
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
    IgProf::debug ("Perf: timer started\n");
    s_interval.it_interval.tv_sec = 0;
    s_interval.it_interval.tv_usec = 1000;
    s_interval.it_value.tv_sec = 0;
    s_interval.it_value.tv_usec = 1000;
    setitimer (ITIMER_PROF, &s_interval, 0);
}

/** Enable SIGPROF signal handler in the current thread.  */
static void
enableSignalHandler (void)
{
    IgProf::debug ("Perf: signal handler enabled in thread %lu\n",
		   (unsigned long) pthread_self ());
    signal (SIGPROF, (sighandler_t) &profileSignalHandler);
}

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
    if (isMultiThreaded ())
	(*igpthread_create_hook.typed.chain)
	    (&s_profthread, 0, &profileSignalThread, 0);
}

/** Old GNU/Linux system hack.  Record this thread to the table of
    threads we need to pass SIGPROF for.  */
static void
registerThisThread (void)
{
    IgProf::debug ("Perf: registering thread %lu\n", (unsigned long) pthread_self ());
    pthread_mutex_lock (&s_lock);
    assert (s_nthreads < 1024);
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
#endif

    // Enable profiling in this thread.
    enableTimer ();

    sigset_t profset;
    sigemptyset (&profset);
    sigaddset (&profset, SIGPROF);
    pthread_sigmask (SIG_UNBLOCK, &profset, 0);

    IgProfPerfWrappedArg *wrapped = (IgProfPerfWrappedArg *) arg;
    IgProf::debug ("Perf: captured thread %lu for profiling (%p, %p)\n",
		   (unsigned long) pthread_self (),
		   (void *) wrapped->start_routine,
		   wrapped->arg);
    return (*wrapped->start_routine) (wrapped->arg);
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
	if (! strncmp (options, "perf", 4))
	{
	    enable = true;
	    options += 4;
	}

	while (*options && *options != ',' && *options != ' ')
	    options++;
    }

    if (enable)
    {
	// Enable profiler.  On old GNU/Linux systems, also start a
	// signal handling worker thread that mirrors the SIGPROF
	// signal to all real user-threads.
        IgHook::hook (igpthread_create_hook.raw);
	IgProf::debug ("Performance profiler enabled\n");
	IgProf::onactivate (&IgProfPerf::enable);
	IgProf::ondeactivate (&IgProfPerf::disable);
	IgProfPerf::enable ();
#if __linux
	registerThisThread ();
	enableSignalThread ();
#endif
	enableSignalHandler ();
	enableTimer ();
    }
}

/** Enable performance profiler.  */
void
IgProfPerf::enable (void)
{ s_enabled++; }

/** Disable performance profiler.  */
void
IgProfPerf::disable (void)
{ s_enabled--; }

/** Override for user thread creation to make sure SIGPROF signal is
    also sent and handled in the user thread.  */
static int
igpthread_create (pthread_t *thread,
		  pthread_attr_t *attr,
		  void * (*start_routine) (void *),
		  void *arg)
{
    IgProfLock lock (s_enabled);
    IgProfPerfWrappedArg wrapped = { start_routine, arg };
    return (*igpthread_create_hook.typed.chain)
	(thread, attr, &threadWrapper, &wrapped);
}

// Auto start this profiling module
static bool autoboot = (IgProfPerf::initialize (), true);
