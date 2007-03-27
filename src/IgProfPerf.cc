//<<<<<< INCLUDES                                                       >>>>>>

#include "Ig_Tools/IgProf/src/IgProfPerf.h"
#include "Ig_Tools/IgProf/src/IgProf.h"
#include "Ig_Tools/IgProf/src/IgProfPool.h"
#include "Ig_Tools/IgHook/interface/IgHook.h"
#include "Ig_Tools/IgHook/interface/IgHookTrace.h"
#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>

//<<<<<< PRIVATE DEFINES                                                >>>>>>
//<<<<<< PRIVATE CONSTANTS                                              >>>>>>
//<<<<<< PRIVATE TYPES                                                  >>>>>>

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

// Data for this profiler module
static IgProfTrace::CounterDef	s_ct_ticks	= { "PERF_TICKS", IgProfTrace::TICK, -1 };
static bool			s_initialized	= false;
static int			s_signal	= SIGPROF;
static int			s_itimer	= ITIMER_PROF;
static int			s_moduleid	= -1;

/** Performance profiler signal handler, SIGPROF or SIGALRM depending
    on the current profiler mode.  Record a tick for the current program
    location.  Assumes the signal handler is registered for the correct
    thread.  Skip ticks when this profiler is not enabled.  */
static void
profileSignalHandler (int /* nsig */, siginfo_t * /* info */, void * /* ctx */)
{
    if (! IgProf::enabled ())
	return;

    void		*addresses [IgProfTrace::MAX_DEPTH];
    int			depth = IgHookTrace::stacktrace (addresses, IgProfTrace::MAX_DEPTH);
    IgProfTrace::Record	entry = { IgProfTrace::COUNT, &s_ct_ticks, 1, 1, 0 };
    IgProfPool		*pool = IgProf::pool (s_moduleid);

    // Drop two bottom frames, three top ones (stacktrace, me, signal frame).
    if (pool) pool->push (addresses+3, depth-4, &entry, 1);
}

/** Enable profiling timer.  You should have called
    #enableSignalHandler() before calling this function.
    This needs to be executed in every thread to be profiled. */
static void
enableTimer (void)
{
    itimerval interval = { { 0, 10000 }, { 0, 10000 } };
    setitimer (s_itimer, &interval, 0);
}

/** Enable profiling signal handler.  */
static void
enableSignalHandler (void)
{
    sigset_t profset;
    sigemptyset (&profset);
    sigaddset (&profset, s_signal);
    if (IgProf::isMultiThreaded ())
        pthread_sigmask (SIG_UNBLOCK, &profset, 0);
    else
        sigprocmask (SIG_UNBLOCK, &profset, 0);

    struct sigaction sa;
    sigemptyset (&sa.sa_mask);
    sa.sa_handler = (sighandler_t) &profileSignalHandler;
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    sigaction (s_signal, &sa, 0);
}

//<<<<<< PUBLIC FUNCTION DEFINITIONS                                    >>>>>>
//<<<<<< MEMBER FUNCTION DEFINITIONS                                    >>>>>>

/** Possibly start performance profiler.  */
void
IgProfPerf::initialize (void)
{
    if (s_initialized) return;
    s_initialized = true;

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
		    s_signal = SIGALRM;
		    s_itimer = ITIMER_REAL;
		    options += 5;
		}
		else if (! strncmp (options, ":user", 5))
		{
		    s_signal = SIGVTALRM;
		    s_itimer = ITIMER_VIRTUAL;
		    options += 5;
		}
		else if (! strncmp (options, ":process", 7))
		{
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

    if (! enable)
	return;

    if (! IgProf::initialize (&s_moduleid, &IgProfPerf::threadInit, true))
	return;

    IgProf::disable ();
    if (s_itimer == ITIMER_REAL)
	IgProf::debug ("Perf: measuring real time\n");
    else if (s_itimer == ITIMER_VIRTUAL)
	IgProf::debug ("Perf: profiling user time\n");
    else if (s_itimer == ITIMER_PROF)
	IgProf::debug ("Perf: profiling process time\n");

    // Enable profiler.  On old GNU/Linux systems, also start a
    // signal handling worker thread that mirrors the profiling
    // signal to all real user-threads.
    IgHook::hook (dopthread_sigmask_hook_main.raw);
    IgProf::debug ("Performance profiler enabled\n");

    enableSignalHandler ();
    enableTimer ();
    IgProf::enable ();
}

/** Thread setup function.  */
void
IgProfPerf::threadInit (void)
{
   // Enable profiling in this thread.
   enableSignalHandler ();
   enableTimer ();
}

//////////////////////////////////////////////////////////////////////
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
