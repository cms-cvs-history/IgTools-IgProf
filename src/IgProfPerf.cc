//<<<<<< INCLUDES                                                       >>>>>>

#include "Ig_Tools/IgProf/src/IgProfPerf.h"
#include "Ig_Tools/IgProf/src/IgProf.h"
#include "Ig_Tools/IgHook/interface/IgHook.h"
#include "Ig_Tools/IgHook/interface/IgHookTrace.h"
#include "Ig_Tools/IgHook/interface/IgHookLiveMap.h"
#include <cassert>
#include <cstdlib>

//<<<<<< PRIVATE DEFINES                                                >>>>>>
//<<<<<< PRIVATE CONSTANTS                                              >>>>>>
//<<<<<< PRIVATE TYPES                                                  >>>>>>
//<<<<<< PRIVATE VARIABLE DEFINITIONS                                   >>>>>>
//<<<<<< PUBLIC VARIABLE DEFINITIONS                                    >>>>>>
//<<<<<< CLASS STRUCTURE INITIALIZATION                                 >>>>>>
//<<<<<< PRIVATE FUNCTION DEFINITIONS                                   >>>>>>

static IgHookTrace::Counter	s_ct_ticks	= { "PERF_TICKS" };
static IgHookTrace::Counter	s_ct_cumticks	= { "PERF_CUM_TICKS" };
static int			s_enabled	= 0;
static bool			s_initialized	= false;

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

//<<<<<< PUBLIC FUNCTION DEFINITIONS                                    >>>>>>
//<<<<<< MEMBER FUNCTION DEFINITIONS                                    >>>>>>

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
        // IgHook::hook (igmalloc_hook.raw);
        IgProf::debug ("Performance profiler enabled\n");
	IgProf::onexit (&IgProfPerf::disable);
        IgProfPerf::enable ();
    }
}

void
IgProfPerf::enable (void)
{ s_enabled++; }

void
IgProfPerf::disable (void)
{ s_enabled--; }

static bool autoboot = (IgProfPerf::initialize (), true);
