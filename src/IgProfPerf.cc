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
IGPROF_LIBHOOK (3, int, dopthread_sigmask, _main,
	        (int how, sigset_t *newmask, sigset_t *oldmask),
		(how, newmask, oldmask),
	        "pthread_sigmask", 0, 0) // "/lib/tls/libpthread.so.0")

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
static int			s_signal	= SIGPROF;
static int			s_itimer	= ITIMER_PROF;
static pthread_t		s_mainthread	= 0;

/** Record a tick.  Increments counters in the tree for ticks.  */
static void 
add (void)
{
    IgHookTrace	*node = IgProf::threadRoot ();
    void	*addresses [256];
    int		depth = IgHookTrace::stacktrace (addresses, 256);
    // one for stacktrace, one for me, one for signal frame, one for profileSignalHandler(), one for
    // pthreads signal handler wrapper (on linux only?).
    int		droptop = 2 + (IgProf::isMultiThreaded () ? 1 : 0);
    int		dropbottom = (s_mainthread && pthread_self() == s_mainthread ? 3 : 4);

    // Walk the tree
    for (int i = depth-dropbottom; node && i >= droptop; --i)
	node = node->child (IgHookTrace::tosymbol (addresses [i]));

    // Increment counters for this node
    node->counter (&s_ct_ticks)->tick ();
}

/** Performance profiler signal handler, SIGPROF or SIGALRM depending
    on current profiler mode.  Record a tick for the current program
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

    add ();
}

/** Enable profiling timer.  You should have called
    #enableSignalHandler() before calling this function.
    This needs to be executed in every thread to be profiled. */
static void
enableTimer (void)
{
    itimerval interval = { { 0, 1000 }, { 0, 1000 } };
    setitimer (s_itimer, &interval, 0);
}

/** Enable profiling signal handler.  */
static void
enableSignalHandler (void)
{ signal (s_signal, (sighandler_t) &profileSignalHandler); }

/** A wrapper for starting user threads to enable profiling.  */
static void *
threadWrapper (void *arg)
{
    IgProf::initThread ();

    // Enable profiling in this thread.
    sigset_t profset;
    sigemptyset (&profset);
    sigaddset (&profset, s_signal);
    pthread_sigmask (SIG_UNBLOCK, &profset, 0);
    enableSignalHandler ();
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

    void *ret = (*start_routine) (start_arg);

    IgProf::debug ("Perf: exiting thread %lu from profiling (%p, %p)\n",
		   (unsigned long) pthread_self (),
		   (void *) start_routine,
		   start_arg);
    IgProf::exitThread ();
    return ret;
}

//<<<<<< PUBLIC FUNCTION DEFINITIONS                                    >>>>>>
//<<<<<< MEMBER FUNCTION DEFINITIONS                                    >>>>>>

/** Possibly start performance profiler.  */
void
IgProfPerf::initialize (void)
{
    if (s_initialized) return;
    s_initialized = true;

    if (! IgProf::initialize ()) return;
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
	    while (*options)
	    {
		if (! strncmp (options, ":real", 5))
		{
		    IgProf::debug ("Perf: measuring real time\n");
		    s_signal = SIGALRM;
		    s_itimer = ITIMER_REAL;
		    options += 5;
		}
		else if (! strncmp (options, ":user", 5))
		{
		    IgProf::debug ("Perf: profiling user time\n");
		    s_signal = SIGVTALRM;
		    s_itimer = ITIMER_VIRTUAL;
		    options += 5;
		}
		else if (! strncmp (options, ":process", 7))
		{
		    IgProf::debug ("Perf: profiling process time\n");
		    s_signal = SIGPROF;
		    s_itimer = ITIMER_PROF;
		    options += 7;
		}
		else
		    break;
	    }
	}
	else
	    options++;

	while (*options && *options != ',' && *options != ' ')
	    options++;
    }

    if (enable)
    {
	// Enable profiler.  On old GNU/Linux systems, also start a
	// signal handling worker thread that mirrors the profiling
	// signal to all real user-threads.
	IgProf::debug ("Performance profiler enabled\n");
	if (IgProf::isMultiThreaded())
	    s_mainthread = pthread_self();

	IgHook::hook (dopthread_create_hook_main.raw);
#if __linux
	IgHook::hook (dopthread_create_hook_pthread20.raw);
	IgHook::hook (dopthread_create_hook_pthread21.raw);
#endif
	IgHook::hook (dopthread_sigmask_hook_main.raw);
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

// Trap fiddling with thread signal masks
static int
dopthread_sigmask (IgHook::SafeData<igprof_dopthread_sigmask_t> &hook,
		   int how, sigset_t *newmask,  sigset_t *oldmask)
{
    if (newmask && (how == SIG_BLOCK || how == SIG_SETMASK))
    {
	sigdelset (newmask, SIGALRM);
	sigdelset (newmask, SIGVTALRM);
	sigdelset (newmask, SIGPROF);
    }
    return hook.chain (how, newmask, oldmask);
}

//////////////////////////////////////////////////////////////////////
static bool autoboot = (IgProfPerf::initialize (), true);
