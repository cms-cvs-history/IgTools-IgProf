//<<<<<< INCLUDES                                                       >>>>>>

#include "Ig_Tools/IgProf/src/IgProfMem.h"
#include "Ig_Tools/IgProf/src/IgProf.h"
#include "Ig_Tools/IgHook/interface/IgHookTrace.h"
#include "Ig_Tools/IgHook/interface/IgHookLiveMap.h"
#include <cstdlib>

//<<<<<< PRIVATE DEFINES                                                >>>>>>
//<<<<<< PRIVATE CONSTANTS                                              >>>>>>
//<<<<<< PRIVATE TYPES                                                  >>>>>>
//<<<<<< PRIVATE VARIABLE DEFINITIONS                                   >>>>>>
//<<<<<< PUBLIC VARIABLE DEFINITIONS                                    >>>>>>
//<<<<<< CLASS STRUCTURE INITIALIZATION                                 >>>>>>
//<<<<<< PRIVATE FUNCTION DEFINITIONS                                   >>>>>>

// Traps for this profiler module
IGPROF_DUAL_HOOK (1, void *, domalloc, _main, _libc,
		  (size_t n), (n),
  		  "malloc", 0, "libc.so.6")
IGPROF_DUAL_HOOK (2, void *, docalloc, _main, _libc,
		  (size_t n, size_t m), (n, m),
		  "calloc", 0, "libc.so.6")
IGPROF_DUAL_HOOK (2, void *, dorealloc, _main, _libc,
		  (void *ptr, size_t n), (ptr, n),
		  "realloc", 0, "libc.so.6")
IGPROF_DUAL_HOOK (3, int, dopmemalign, _main, _libc,
		  (void **ptr, size_t alignment, size_t size),
		  (ptr, alignment, size),
		  "posix_memalign", 0, "libc.so.6")
IGPROF_DUAL_HOOK (2, void *, domemalign, _main, _libc,
		  (size_t alignment, size_t size), (alignment, size),
		  "memalign", 0, "libc.so.6")
IGPROF_DUAL_HOOK (1, void *, dovalloc, _main, _libc,
		  (size_t size), (size),
		  "valloc", 0, "libc.so.6")
IGPROF_DUAL_HOOK (1, void, dofree, _main, _libc,
		  (void *ptr), (ptr),
		  "free", 0, "libc.so.6")

// Data for this profiler module
static IgHookTrace::Counter	s_ct_total	= { "MEM_TOTAL" };
static IgHookTrace::Counter	s_ct_largest	= { "MEM_MAX" };
static IgHookTrace::Counter	s_ct_live	= { "MEM_LIVE" };
static IgHookTrace::Counter	s_ct_maxlive	= { "MEM_LIVE_MAX" };
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
    IGPROF_TRACE (("[mem add %p %lu\n", ptr, size));

    int		drop = 4; // one for stacktrace, one for me, two for hook
    IgHookTrace	*node = IgProf::root ();
    void	*addresses [128];
    int		depth = IgHookTrace::stacktrace (addresses, 128);

    // Walk the tree
    for (int i = depth-2; i >= drop; --i)
	node = node->child (IgHookTrace::tosymbol (addresses [i]));

    // Increment counters for this node
    if (s_count_total)   node->counter (&s_ct_total)->add (size);
    if (s_count_largest) node->counter (&s_ct_largest)->max (size);
    if (s_count_live)
    {
	IgHookTrace::CounterValue *ctr = node->counter (&s_ct_live);
	ctr->add (size);
	node->counter (&s_ct_maxlive)->max (ctr->value ());
    }
    if (s_count_leaks)
    {
	IGPROF_ASSERT (s_live->find ((unsigned long) ptr) == s_live->end ());
	s_live->insert ((unsigned long) ptr, node, size);
    }

    IGPROF_TRACE (("]\n"));
}

/** Remove knowledge about allocation.  If we are tracking leaks,
    removes the memory allocation from the live map and subtracts
    from the live memory counters.  */
static void
remove (void *ptr)
{
    IGPROF_TRACE (("[mem rm %p\n", ptr));

    if (s_count_leaks)
    {
	IgHookLiveMap::Iterator	info = s_live->find ((unsigned long) ptr);
	if (info == s_live->end ())
	    // Unknown to us, probably allocated before we started.
	    // This happens for instance with GCC 3.4+ as libstdc++
	    // allocates memory and is invoked before we start.
	    // IGPROF_ASSERT (info != s_live->end ());
	    return;

	IgHookTrace	*node = info->second.first;
	size_t		size = info->second.second;

	IGPROF_ASSERT (node->counter (&s_ct_live)->value () >= size);
	node->counter (&s_ct_live)->sub (size);

	s_live->remove (info);
    }

    IGPROF_TRACE (("]\n", ptr));
}

//<<<<<< PUBLIC FUNCTION DEFINITIONS                                    >>>>>>
//<<<<<< MEMBER FUNCTION DEFINITIONS                                    >>>>>>

/** Initialise memory profiling.  Traps various system calls to keep track
    of memory usage, and if requested, leaks.  */
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
	while (*options == ' ' || *options == ',')
	    ++options;

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

        IgHook::hook (domalloc_hook_main.raw);
        IgHook::hook (docalloc_hook_main.raw);
        IgHook::hook (dorealloc_hook_main.raw);
        IgHook::hook (dopmemalign_hook_main.raw);
        IgHook::hook (domemalign_hook_main.raw);
        IgHook::hook (dovalloc_hook_main.raw);
        IgHook::hook (dofree_hook_main.raw);
#if __linux
        if (domalloc_hook_main.raw.chain)    IgHook::hook (domalloc_hook_libc.raw);
        if (docalloc_hook_main.raw.chain)    IgHook::hook (docalloc_hook_libc.raw);
        if (dorealloc_hook_main.raw.chain)   IgHook::hook (dorealloc_hook_libc.raw);
        if (dopmemalign_hook_main.raw.chain) IgHook::hook (dopmemalign_hook_libc.raw);
        if (domemalign_hook_main.raw.chain)  IgHook::hook (domemalign_hook_libc.raw);
        if (dovalloc_hook_main.raw.chain)    IgHook::hook (dovalloc_hook_libc.raw);
        if (dofree_hook_main.raw.chain)      IgHook::hook (dofree_hook_libc.raw);
#endif
        IgProf::debug ("Memory profiler enabled\n");
	// This may allocate, so force our flag to remain false
	// and then set it to a known value.
	s_enabled = -10;
	IgProf::onactivate (&IgProfMem::enable);
	IgProf::ondeactivate (&IgProfMem::disable);
	s_enabled = 1;
    }
}

/** Enable this profiling module.  Only call within #IgProfLock.
    Normally called automatically through activation by #IgProfLock.
    Allows recursive enable/disable.  */
void
IgProfMem::enable (void)
{ s_enabled++; }

/** Disable this profiling module.  Only call within #IgProfLock.
    Normally called automatically through activation by #IgProfLock.
    Allows recursive enable/disable.  */
void
IgProfMem::disable (void)
{ s_enabled--; }

//////////////////////////////////////////////////////////////////////
// Traps for this profiler module.  Track memory allocation routines.
static void *
domalloc (IgHook::SafeData<igprof_domalloc_t> &hook, size_t n)
{
    IGPROF_TRACE (("(%d igmalloc %lu\n", s_enabled, n));
    IgProfLock lock (s_enabled);
    void *result = (*hook.chain) (n);

    if (lock.enabled () > 0 && result)
	add (result, n);

    IGPROF_TRACE ((" -> %p)\n", result));
    return result;
}

static void *
docalloc (IgHook::SafeData<igprof_docalloc_t> &hook, size_t n, size_t m)
{
    IGPROF_TRACE (("(%d igcalloc %lu %lu\n", s_enabled, n, m));
    IgProfLock lock (s_enabled);
    void *result = (*hook.chain) (n, m);

    if (lock.enabled () > 0 && result)
	add (result, n * m);

    IGPROF_TRACE ((" -> %p)\n", result));
    return result;
}

static void *
dorealloc (IgHook::SafeData<igprof_dorealloc_t> &hook, void *ptr, size_t n)
{
    IGPROF_TRACE (("(%d igrealloc %p %lu\n", s_enabled, ptr, n));
    IgProfLock lock (s_enabled);
    void *result = (*hook.chain) (ptr, n);

    if (lock.enabled () > 0)
    {
	if (ptr) remove (ptr);
	if (result) add (result, n);
    }

    IGPROF_TRACE ((" -> %p)\n", result));
    return result;
}

static void *
domemalign (IgHook::SafeData<igprof_domemalign_t> &hook, size_t alignment, size_t size)
{
    IGPROF_TRACE (("(%d igmemalign %lu %lu\n", s_enabled, alignment, size));
    IgProfLock lock (s_enabled);
    void *result = (*hook.chain) (alignment, size);

    if (lock.enabled () > 0 && result)
	add (result, size);

    IGPROF_TRACE ((" -> %p)\n", result));
    return result;
}

static void *
dovalloc (IgHook::SafeData<igprof_dovalloc_t> &hook, size_t size)
{
    IGPROF_TRACE (("(%d igvalloc %lu\n", s_enabled, size));
    IgProfLock lock (s_enabled);
    void *result = (*hook.chain) (size);

    if (lock.enabled () > 0 && result)
	add (result, size);

    IGPROF_TRACE ((" -> %p)\n", result));
    return result;
}

static int
dopmemalign (IgHook::SafeData<igprof_dopmemalign_t> &hook,
	     void **ptr, size_t alignment, size_t size)
{
    IGPROF_TRACE (("(%d igpmemalign %p %lu %lu\n", s_enabled, ptr, alignment, size));
    IgProfLock lock (s_enabled);
    int result = (*hook.chain) (ptr, alignment, size);

    if (lock.enabled () > 0 && ptr && *ptr)
	add (*ptr, size);

    IGPROF_TRACE ((" -> %p %d)\n", *ptr, result));
    return result;
}

static void
dofree (IgHook::SafeData<igprof_dofree_t> &hook, void *ptr)
{
    IGPROF_TRACE (("(%d igfree %p\n", s_enabled, ptr));
    IgProfLock lock (s_enabled);
    (*hook.chain) (ptr);

    if (lock.enabled () > 0)
	remove (ptr);

    IGPROF_TRACE ((")\n"));
}

//////////////////////////////////////////////////////////////////////
static bool autoboot = (IgProfMem::initialize (), true);
