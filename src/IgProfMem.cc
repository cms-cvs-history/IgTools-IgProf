//<<<<<< INCLUDES                                                       >>>>>>

#include "Ig_Tools/IgProf/src/IgProfMem.h"
#include "Ig_Tools/IgProf/src/IgProf.h"
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

static void *igmalloc (size_t n);
static void *igrealloc (void *ptr, size_t n);
static void *igcalloc (size_t n, size_t m);
static int  igpmemalign (void **ptr, size_t align, size_t size);
static void *igmemalign (size_t align, size_t size);
static void *igvalloc (size_t size);
static void igfree (void *ptr);

IGPROF_HOOK (void * (size_t), malloc, igmalloc);
IGPROF_HOOK (void * (size_t, size_t), calloc, igcalloc);
IGPROF_HOOK (void * (void *, size_t), realloc, igrealloc);
IGPROF_HOOK (int (void **, size_t, size_t), posix_memalign, igpmemalign);
IGPROF_HOOK (void * (size_t, size_t), memalign, igmemalign);
IGPROF_HOOK (void * (size_t), valloc, igvalloc);
IGPROF_HOOK (void (void *), free, igfree);

static IgHookTrace::Counter	s_ct_total	= { "MEM_TOTAL" };
static IgHookTrace::Counter	s_ct_largest	= { "MEM_MAX" };
static IgHookTrace::Counter	s_ct_live	= { "MEM_LIVE" };
static bool			s_count_total	= 0;
static bool			s_count_largest	= 0;
static bool			s_count_live	= 0;
static bool			s_count_leaks	= 0;
static IgHookLiveMap		*s_live		= 0;
static int			s_enabled	= 0;
static bool			s_initialized	= false;

/** Record an allocation at @a ptr of @a size bytes.  Increments counters
    in the tree for the allocations as per current configuration and adds
    the pointer to current live memory map if we are tracking leaks.  */
static void 
add (void *ptr, size_t size)
{
    int		drop = 3; // one for stacktrace, one for me, one for hook
    IgHookTrace	*node = IgProf::root ();
    void	*addresses [128];
    int		depth = IgHookTrace::stacktrace (addresses, 128);

    // Walk the tree
    for (int i = depth-2; i >= drop; --i)
	node = node->child (IgHookTrace::tosymbol (addresses [i]));

    // Increment counters for this node
    if (s_count_total)   node->counter (&s_ct_total)->add (size);
    if (s_count_largest) node->counter (&s_ct_largest)->max (size);
    if (s_count_live)    node->counter (&s_ct_live)->max (size);
    if (s_count_leaks)   s_live->insert ((unsigned long) ptr, node, size);
}

/** Remove knowledge about allocation.  If we are tracking leaks,
    removes the memory allocation from the live map and subtracts
    from the live memory counters.  */
static void
remove (void *ptr)
{
    if (s_count_leaks)
    {
	IgHookLiveMap::Iterator	info = s_live->find ((unsigned long) ptr);
	assert (info != s_live->end ());

	IgHookTrace	*node = info->second.first;
	size_t		size = info->second.second;

	assert (node->counter (&s_ct_live)->value () >= size);
	node->counter (&s_ct_live)->sub (size);

	s_live->remove (info);
    }
}

//<<<<<< PUBLIC FUNCTION DEFINITIONS                                    >>>>>>
//<<<<<< MEMBER FUNCTION DEFINITIONS                                    >>>>>>

void
IgProfMem::initialize (void)
{
    if (s_initialized) return;
    s_initialized = true;

    IgProf::initialize ();
    IgProf::debug ("Memory profiler loaded\n");

    const char	*options = IgProf::options ();
    bool	enable = false;
    bool	opts = false;

    while (options && *options)
    {
	if (! strncmp (options, "mem", 3))
	{
	    enable = true;
	    options += 3;
	    while (*options)
	    {
		if (! strncmp (options, ":total", 6))
	        {
		    IgProf::debug ("Memory: enabling total counting\n");
		    s_count_total = 1;
		    options += 6;
		    opts = true;
	        }
		else if (! strncmp (options, ":largest", 8))
	        {
		    IgProf::debug ("Memory: enabling max counting\n");
		    s_count_largest = 1;
		    options += 8;
		    opts = true;
	        }
		else if (! strncmp (options, ":live", 5))
		{
		    IgProf::debug ("Memory: enabling live counting\n");
		    s_count_live = 1;
		    options += 5;
		    opts = true;
		}
		else if (! strncmp (options, ":leaks", 6))
		{
		    IgProf::debug ("Memory: enabling leak table and live counting\n");
		    s_count_live = 1;
		    s_count_leaks = 1;
		    options += 6;
		    opts = true;
		}
		else if (! strncmp (options, ":all", 4))
		{
		    IgProf::debug ("Memory: enabling everything\n");
		    s_count_total = 1;
		    s_count_largest = 1;
		    s_count_live = 1;
		    s_count_leaks = 1;
		    options += 4;
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
	IgProf::debug ("Memory: defaulting to total memory counting\n");
	s_count_total = 1;
    }

    if (enable)
    {
        if (s_count_leaks)
	    s_live = IgProf::liveMap ("Memory Leaks");

        IgHook::hook (igmalloc_hook.raw);
        IgHook::hook (igcalloc_hook.raw);
        IgHook::hook (igrealloc_hook.raw);
        IgHook::hook (igpmemalign_hook.raw);
        IgHook::hook (igmemalign_hook.raw);
        IgHook::hook (igvalloc_hook.raw);
        IgHook::hook (igfree_hook.raw);
        IgProf::debug ("Memory profiler enabled\n");
	IgProf::onactivate (&IgProfMem::enable);
	IgProf::ondeactivate (&IgProfMem::disable);
	IgProfMem::enable ();
    }
}

void
IgProfMem::enable (void)
{ s_enabled++; }

void
IgProfMem::disable (void)
{ s_enabled--; }

void *igmalloc (size_t n)
{
    IgProfLock lock (s_enabled);
    void *result = (*igmalloc_hook.typed.chain) (n);

    if (lock.enabled () > 0 && result)
	add (result, n);

    return result;
}

void *igcalloc (size_t n, size_t m)
{
    IgProfLock lock (s_enabled);
    void *result = (*igcalloc_hook.typed.chain) (n, m);

    if (lock.enabled () > 0 && result)
	add (result, n * m);

    return result;
}

void *igrealloc (void *ptr, size_t n)
{
    IgProfLock lock (s_enabled);
    void *result = (*igrealloc_hook.typed.chain) (ptr, n);

    if (lock.enabled () > 0 && result)
    {
	remove (ptr);
	add (result, n);
    }

    return result;
}

void *igmemalign (size_t alignment, size_t size)
{
    IgProfLock lock (s_enabled);
    void *result = (*igmemalign_hook.typed.chain) (alignment, size);

    if (lock.enabled () > 0 && result)
	add (result, size);

    return result;
}

void *igvalloc (size_t size)
{
    IgProfLock lock (s_enabled);
    void *result = (*igvalloc_hook.typed.chain) (size);

    if (lock.enabled () > 0 && result)
	add (result, size);

    return result;
}

int igpmemalign (void **ptr, size_t alignment, size_t size)
{
    IgProfLock lock (s_enabled);
    int result = (*igpmemalign_hook.typed.chain) (ptr, alignment, size);

    if (lock.enabled () > 0 && ptr && *ptr)
	add (*ptr, size);

    return result;
}

void igfree (void *ptr)
{
    IgProfLock lock (s_enabled);
    (*igfree_hook.typed.chain) (ptr);

    if (lock.enabled () > 0)
	remove (ptr);
}

static bool autoboot = (IgProfMem::initialize (), true);
