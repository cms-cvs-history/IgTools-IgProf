//<<<<<< INCLUDES                                                       >>>>>>

#include "Ig_Tools/IgProf/src/IgProfFileDesc.h"
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

static IgHookTrace::Counter	s_ct_used	= { "FD_USED" };
static IgHookTrace::Counter	s_ct_cumused	= { "FD_CUM_USED" };
static IgHookTrace::Counter	s_ct_live	= { "FD_LIVE" };
static IgHookTrace::Counter	s_ct_cumlive	= { "FD_CUM_LIVE" };
static bool			s_count_used	= 0;
static bool			s_count_live	= 0;
static bool			s_count_leaks	= 0;
static IgHookLiveMap		*s_live		= 0;
static int			s_enabled	= 0;
static bool			s_initialized	= false;

/** Record file descriptor.  Increments counters in the tree. */
static void 
add (int fd)
{
    int		drop = 3; // one for stacktrace, one for me, one for hook
    IgHookTrace	*node = IgProf::root ();
    void	*addresses [128];
    int		depth = IgHookTrace::stacktrace (addresses, 128);

    // Increment cumulative counters for higher in the tree
    for (int i = depth-2; i >= drop; --i)
    {
	if (s_count_used) node->counter (&s_ct_cumused)->tick ();
	if (s_count_live) node->counter (&s_ct_cumlive)->tick ();
	node = node->child (IgHookTrace::tosymbol (addresses [i]));
    }

    // Increment counters for this node
    if (s_count_used)  node->counter (&s_ct_used)->tick ();
    if (s_count_live)  node->counter (&s_ct_live)->tick ();
    if (s_count_leaks) s_live->insert (fd, node);
}

/** Remove knowledge about the file descriptor.  If we are tracking
    leaks, removes the descriptor from the live map and subtracts
    from the live descriptor counters.  */
static void
remove (int fd)
{
    if (s_count_leaks)
    {
	IgHookLiveMap::Iterator	info = s_live->find (fd);
	assert (info != s_live->end ());

	IgHookTrace::Counter	*id = &s_ct_live;
	IgHookTrace		*node = info->second.first;

	while (node)
	{
	    IgHookTrace::CounterValue *val = node->counter (id);
	    assert (val->value () > 0);
	    val->untick ();
	    node = node->parent ();
	    id = &s_ct_cumlive;
	}

	s_live->remove (info);
    }
}

//<<<<<< PUBLIC FUNCTION DEFINITIONS                                    >>>>>>
//<<<<<< MEMBER FUNCTION DEFINITIONS                                    >>>>>>

void
IgProfFileDesc::initialize (void)
{
    if (s_initialized) return;
    s_initialized = true;

    IgProf::initialize ();
    IgProf::debug ("File descriptor profiler loaded\n");

    const char	*options = IgProf::options ();
    bool	enable = false;
    bool	opts = false;

    while (options && *options)
    {
	if (! strncmp (options, "fd", 2))
	{
	    enable = true;
	    options += 2;
	    while (*options)
	    {
	        if (! strncmp (options, ":used", 5))
	        {
		    IgProf::debug ("FD: enabling usage counting\n");
		    s_count_used = 1;
		    options += 5;
		    opts = true;
	        }
		else if (! strncmp (options, ":live", 5))
	        {
		    IgProf::debug ("FD: enabling live counting\n");
		    s_count_live = 1;
		    options += 5;
		    opts = true;
	        }
		else if (! strncmp (options, ":leaks", 6))
		{
		    IgProf::debug ("FD: enabling leak table\n");
		    s_count_leaks = 1;
		    options += 6;
		    opts = true;
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

    if (enable && !opts)
    {
	IgProf::debug ("FD: defaulting to total descriptor counting\n");
	s_count_used = 1;
    }

    if (enable)
    {
        if (s_count_leaks)
	    s_live = IgProf::liveMap ("File Descriptor Leaks");

        // IgHook::hook (igmalloc_hook.raw);
        IgProf::debug ("File descriptor profiler enabled\n");
        IgProf::onactivate (&IgProfFileDesc::enable);
        IgProf::ondeactivate (&IgProfFileDesc::disable);
	IgProfFileDesc::enable ();
    }
}

void
IgProfFileDesc::enable (void)
{ s_enabled++; }

void
IgProfFileDesc::disable (void)
{ s_enabled--; }

static bool autoboot = (IgProfFileDesc::initialize (), true);
