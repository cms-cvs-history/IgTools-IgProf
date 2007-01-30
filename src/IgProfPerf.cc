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

struct IgProfPerfCache
{
    void *addr;
    IgHookTrace *node;
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
	        "pthread_sigmask", 0, 0)

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
static const int		N_STACK		= 256;
static IgHookTrace::Counter	s_ct_ticks	= { "PERF_TICKS" };
static int			s_enabled	= 0;
static bool			s_initialized	= false;
static int			s_signal	= SIGPROF;
static int			s_itimer	= ITIMER_PROF;
static pthread_t		s_mainthread	= 0;
static IgProfPerfCache		*s_cache	= 0;
static pthread_key_t		s_tcache;

/** Setup per-thread address lookup cache. */
static void
setupThreadCache (void)
{
    IgProfPerfCache *cache = new IgProfPerfCache [N_STACK];
    memset (cache, 0, N_STACK * sizeof (IgProfPerfCache));

    if (! IgProf::isMultiThreaded ())
	s_cache = cache;
    else
	pthread_setspecific (s_tcache, cache);
}

/** Return per-thread cache.  */
static IgProfPerfCache *
threadCache (void)
{
    if (! IgProf::isMultiThreaded ())
	return s_cache;
    else
	return (IgProfPerfCache *) pthread_getspecific (s_tcache);
}

/** Performance profiler signal handler, SIGPROF or SIGALRM depending
    on the current profiler mode.  Record a tick for the current program
    location.  Assumes the signal handler is registered for the correct
    thread.  Skip ticks when this profiler is not enabled.  */
static void
profileSignalHandler (void)
{
    if (s_enabled <= 0)
	return;

    // Increment counter for the final leaf of this call tree.
    void		*addresses [N_STACK];
    IgProfPerfCache	*nodecache = threadCache ();
    IgHookTrace		*node = IgProf::threadRoot ();
    int			depth = IgHookTrace::stacktrace (addresses, N_STACK);
    const int		droptop = 3; // stack trace, me, signal frame
    const int		dropbottom = 2; // system + start or thread wrapper

    // Walk the tree
    for (int i = depth-dropbottom, j = 0, valid = 1; node && i >= droptop; --i, ++j)
    {
	if (valid && nodecache[j].addr == addresses[i])
	{
	    node = nodecache[j].node;
	}
	else
	{
	    node = node->child (addresses[i]);
	    nodecache[j].addr = addresses[i];
	    nodecache[j].node = node;
	    valid = 0;
        }
    }

    // Increment counters for this node
    node->counter (&s_ct_ticks)->tick ();
}

/** Enable profiling timer.  You should have called
    #enableSignalHandler() before calling this function.
    This needs to be executed in every thread to be profiled. */
static void
enableTimer (void)
{
    itimerval interval = { { 0, 5000 }, { 0, 5000 } };
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
    void *(*start_routine) (void*);
    void *start_arg;

    // Hide the init and exit sequences from the memory profiler.  If
    // the memory and performance profilers are enabled concurrently,
    // the actions below will otherwise become visible.
    {
	IgProfLock lock (s_enabled);

	// Enable separate profile tree for this thread.
        IgProf::initThread ();
	setupThreadCache ();

        // Make sure we've called stack trace code at least once in
	// this thread before the profile signal hits.  Linux's
	// backtrace() seems to want to call pthread_once() which
	// can be bad news inside the signal handler.
	void *dummy = 0; IgHookTrace::stacktrace (&dummy, 1);

        // Enable profiling in this thread.
        sigset_t profset;
        sigemptyset (&profset);
        sigaddset (&profset, s_signal);
        pthread_sigmask (SIG_UNBLOCK, &profset, 0);
        enableSignalHandler ();
        enableTimer ();

        // Get arguments.
        IgProfPerfWrappedArg *wrapped = (IgProfPerfWrappedArg *) arg;
        start_routine = wrapped->start_routine;
        start_arg = wrapped->arg;
        delete wrapped;

	// Report the thread.
        IgProf::debug ("Perf: captured thread %lu for profiling (%p, %p)\n",
		       (unsigned long) pthread_self (),
		       (void *) start_routine,
		       start_arg);
    }

    // Start the user thread.
    void *ret = (*start_routine) (start_arg);

    {
	// Harvest thread profile result.
	IgProfLock lock (s_enabled);
	delete [] threadCache ();
        IgProf::debug ("Perf: exiting thread %lu from profiling (%p, %p)\n",
		       (unsigned long) pthread_self (),
		       (void *) start_routine,
		       start_arg);
        IgProf::exitThread ();
    }

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
	{
	    s_mainthread = pthread_self();
	    pthread_key_create (&s_tcache, 0);
	}

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
	setupThreadCache ();

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
    // Pass the actual arguments to our wrapper in a temporary memory
    // structure.  We need to hide the creation from memory profiler
    // in case it's running concurrently with this profiler.
    IgProfPerfWrappedArg *wrapped;
    {
	IgProfLock lock (s_enabled);
	wrapped = new IgProfPerfWrappedArg;
        wrapped->start_routine = start_routine;
        wrapped->arg = arg;
    }
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
