//<<<<<< INCLUDES                                                       >>>>>>

#include "Ig_Tools/IgProf/src/IgProfMem.h"
#include "Ig_Tools/IgProf/src/IgProf.h"
#include "Ig_Tools/IgProf/src/IgProfPool.h"
#include "Ig_Tools/IgHook/interface/IgHook.h"
#include "Ig_Tools/IgHook/interface/IgHookTrace.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <pthread.h>

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
static IgProfTrace::CounterDef	s_ct_total	= { "MEM_TOTAL",    IgProfTrace::TICK, -1 };
static IgProfTrace::CounterDef	s_ct_largest	= { "MEM_MAX",      IgProfTrace::MAX, -1 };
static IgProfTrace::CounterDef	s_ct_live	= { "MEM_LIVE",     IgProfTrace::TICK_PEAK, -1 };
static bool			s_count_total	= 0;
static bool			s_count_largest	= 0;
static bool			s_count_live	= 0;
static bool			s_initialized	= false;
static int			s_moduleid	= -1;

/** Record an allocation at @a ptr of @a size bytes.  Increments counters
    in the tree for the allocations as per current configuration and adds
    the pointer to current live memory map if we are tracking leaks.  */
static void  __attribute__((noinline))
add (void *ptr, size_t size)
{
    void		*addresses [IgProfTrace::MAX_DEPTH];
    int			depth = IgHookTrace::stacktrace (addresses, IgProfTrace::MAX_DEPTH);
    IgProfPool		*pool = IgProf::pool (s_moduleid);
    IgProfTrace::Record	entries [3];
    int			nentries = 0;

    if (! pool)
	return;

    if (s_count_total)
    {
	entries[nentries].type = IgProfTrace::COUNT;
	entries[nentries].def = &s_ct_total;
	entries[nentries].amount = size;
	entries[nentries].ticks = 1;
	nentries++;
    }

    if (s_count_largest)
    {
	entries[nentries].type = IgProfTrace::COUNT;
	entries[nentries].def = &s_ct_largest;
	entries[nentries].amount = size;
	entries[nentries].ticks = 1;
	nentries++;
    }

    if (s_count_live)
    {
	entries[nentries].type = IgProfTrace::COUNT | IgProfTrace::ACQUIRE;
	entries[nentries].def = &s_ct_live;
	entries[nentries].amount = size;
	entries[nentries].ticks = 1;
	entries[nentries].resource = (IgProfTrace::Address) ptr;
	nentries++;
    }

    // Drop two bottom frames, four top ones (stacktrace, me, two for hook).
    pool->push (addresses+4, depth-5, entries, nentries);
}

/** Remove knowledge about allocation.  If we are tracking leaks,
    removes the memory allocation from the live map and subtracts
    from the live memory counters.  */
static void
remove (void *ptr)
{
    if (s_count_live && ptr)
    {
        IgProfPool *pool = IgProf::pool (s_moduleid);
	if (! pool) return;

	IgProfTrace::Record entry
	    = { IgProfTrace::RELEASE, &s_ct_live, 0, 0, (IgProfTrace::Address) ptr };
        pool->push (0, 0, &entry, 1);
    }
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
		    s_count_total = 1;
		    options += 6;
		    opts = true;
	        }
		else if (! strncmp (options, ":largest", 8))
	        {
		    s_count_largest = 1;
		    options += 8;
		    opts = true;
	        }
		else if (! strncmp (options, ":live", 5))
		{
		    s_count_live = 1;
		    options += 5;
		    opts = true;
		}
		else if (! strncmp (options, ":all", 4))
		{
		    s_count_total = 1;
		    s_count_largest = 1;
		    s_count_live = 1;
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

    if (! enable)
	return;

    if (! IgProf::initialize (&s_moduleid, 0, false))
	return;

    IgProf::disable ();
    if (! opts)
    {
	IgProf::debug ("Memory: defaulting to total memory counting\n");
	s_count_total = 1;
    }
    else
    {
	if (s_count_total)
	    IgProf::debug ("Memory: enabling total counting\n");
	if (s_count_largest)
	    IgProf::debug ("Memory: enabling max counting\n");
	if (s_count_live)
	    IgProf::debug ("Memory: enabling live counting\n");
    }

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
    if (domemalign_hook_main.raw.chain)  IgHook::hook (domemalign_hook_libc.raw);
    if (dovalloc_hook_main.raw.chain)    IgHook::hook (dovalloc_hook_libc.raw);
    if (dofree_hook_main.raw.chain)      IgHook::hook (dofree_hook_libc.raw);
#endif
    IgProf::debug ("Memory profiler enabled\n");
    IgProf::enable ();
}

//////////////////////////////////////////////////////////////////////
// Traps for this profiler module.  Track memory allocation routines.
static void *
domalloc (IgHook::SafeData<igprof_domalloc_t> &hook, size_t n)
{
    bool enabled = IgProf::disable ();
    void *result = (*hook.chain) (n);

    if (enabled && result)
	add (result, n);

    IgProf::enable ();
    return result;
}

static void *
docalloc (IgHook::SafeData<igprof_docalloc_t> &hook, size_t n, size_t m)
{
    bool enabled = IgProf::disable ();
    void *result = (*hook.chain) (n, m);

    if (enabled && result)
	add (result, n * m);

    IgProf::enable ();
    return result;
}

static void *
dorealloc (IgHook::SafeData<igprof_dorealloc_t> &hook, void *ptr, size_t n)
{
    bool enabled = IgProf::disable ();
    void *result = (*hook.chain) (ptr, n);

    if (result)
    {
	if (ptr) remove (ptr);
	if (enabled && result) add (result, n);
    }

    IgProf::enable ();
    return result;
}

static void *
domemalign (IgHook::SafeData<igprof_domemalign_t> &hook, size_t alignment, size_t size)
{
    bool enabled = IgProf::disable ();
    void *result = (*hook.chain) (alignment, size);

    if (enabled && result)
	add (result, size);

    IgProf::enable ();
    return result;
}

static void *
dovalloc (IgHook::SafeData<igprof_dovalloc_t> &hook, size_t size)
{
    bool enabled = IgProf::disable ();
    void *result = (*hook.chain) (size);

    if (enabled && result)
	add (result, size);

    IgProf::enable ();
    return result;
}

static int
dopmemalign (IgHook::SafeData<igprof_dopmemalign_t> &hook,
	     void **ptr, size_t alignment, size_t size)
{
    bool enabled = IgProf::disable ();
    int result = (*hook.chain) (ptr, alignment, size);

    if (enabled && ptr && *ptr)
	add (*ptr, size);

    IgProf::enable ();
    return result;
}

static void
dofree (IgHook::SafeData<igprof_dofree_t> &hook, void *ptr)
{
    IgProf::disable ();
    (*hook.chain) (ptr);
    remove (ptr);
    IgProf::enable ();
}

//////////////////////////////////////////////////////////////////////
static bool autoboot = (IgProfMem::initialize (), true);
