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

static void *igmalloc (size_t n);
static void *igrealloc (void *ptr, size_t n);
static void *igcalloc (size_t n, size_t m);
static int  igpmemalign (void **ptr, size_t align, size_t size);
static void *igmemalign (size_t align, size_t size);
static void *igvalloc (size_t size);
static void igfree (void *ptr);

#if __linux
static void *igcmalloc (size_t n);
static void *igcrealloc (void *ptr, size_t n);
static void *igccalloc (size_t n, size_t m);
static int  igcpmemalign (void **ptr, size_t align, size_t size);
static void *igcmemalign (size_t align, size_t size);
static void *igcvalloc (size_t size);
static void igcfree (void *ptr);
#endif

IGPROF_HOOK (void * (size_t),			malloc,		igmalloc);
IGPROF_HOOK (void * (size_t, size_t),		calloc,		igcalloc);
IGPROF_HOOK (void * (void *, size_t),		realloc,	igrealloc);
IGPROF_HOOK (int (void **, size_t, size_t),	posix_memalign,	igpmemalign);
IGPROF_HOOK (void * (size_t, size_t),		memalign,	igmemalign);
IGPROF_HOOK (void * (size_t),			valloc,		igvalloc);
IGPROF_HOOK (void (void *),			free,		igfree);

#if __linux
IGPROF_LIBHOOK ("libc.so.6", void * (size_t),			malloc,		igcmalloc);
IGPROF_LIBHOOK ("libc.so.6", void * (size_t, size_t),		calloc,		igccalloc);
IGPROF_LIBHOOK ("libc.so.6", void * (void *, size_t),		realloc,	igcrealloc);
IGPROF_LIBHOOK ("libc.so.6", int (void **, size_t, size_t),	posix_memalign,	igcpmemalign);
IGPROF_LIBHOOK ("libc.so.6", void * (size_t, size_t),		memalign,	igcmemalign);
IGPROF_LIBHOOK ("libc.so.6", void * (size_t),			valloc,		igcvalloc);
IGPROF_LIBHOOK ("libc.so.6", void (void *),			free,		igcfree);
#endif

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
    if (s_count_live)    node->counter (&s_ct_live)->max (size);
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

        IgHook::hook (igmalloc_hook.raw);
        IgHook::hook (igcalloc_hook.raw);
        IgHook::hook (igrealloc_hook.raw);
        IgHook::hook (igpmemalign_hook.raw);
        IgHook::hook (igmemalign_hook.raw);
        IgHook::hook (igvalloc_hook.raw);
        IgHook::hook (igfree_hook.raw);
#if __linux
        IgHook::hook (igcmalloc_hook.raw);
        IgHook::hook (igccalloc_hook.raw);
        IgHook::hook (igcrealloc_hook.raw);
        IgHook::hook (igcpmemalign_hook.raw);
        IgHook::hook (igcmemalign_hook.raw);
        IgHook::hook (igcvalloc_hook.raw);
        IgHook::hook (igcfree_hook.raw);
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

void
IgProfMem::enable (void)
{ s_enabled++; }

void
IgProfMem::disable (void)
{ s_enabled--; }

static void *
domalloc (IgHook::SafeData<void *(size_t)> &hook, size_t n)
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
docalloc (IgHook::SafeData<void *(size_t, size_t)> &hook,
	   size_t n, size_t m)
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
dorealloc (IgHook::SafeData<void *(void *, size_t)> &hook,
	   void *ptr, size_t n)
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
domemalign (IgHook::SafeData<void *(size_t, size_t)> &hook,
	    size_t alignment, size_t size)
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
dovalloc (IgHook::SafeData<void *(size_t)> &hook, size_t size)
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
dopmemalign (IgHook::SafeData<int(void **, size_t, size_t)> &hook,
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
dofree (IgHook::SafeData<void (void *)> &hook, void *ptr)
{
    IGPROF_TRACE (("(%d igfree %p\n", s_enabled, ptr));
    IgProfLock lock (s_enabled);
    (*hook.chain) (ptr);

    if (lock.enabled () > 0)
	remove (ptr);

    IGPROF_TRACE ((")\n"));
}

static void *igmalloc (size_t n) { return domalloc (igmalloc_hook.typed, n); }
static void *igrealloc (void *ptr, size_t n) { return dorealloc (igrealloc_hook.typed, ptr, n); }
static void *igcalloc (size_t n, size_t m) { return docalloc (igcalloc_hook.typed, n, m); }
static int  igpmemalign (void **ptr, size_t align, size_t size) { return dopmemalign (igpmemalign_hook.typed, ptr, align, size); }
static void *igmemalign (size_t align, size_t size) { return domemalign (igmemalign_hook.typed, align, size); }
static void *igvalloc (size_t size) { return dovalloc (igvalloc_hook.typed, size); }
static void igfree (void *ptr) { return dofree (igfree_hook.typed, ptr); }

#if __linux
static void *igcmalloc (size_t n) { return domalloc (igcmalloc_hook.typed, n); }
static void *igcrealloc (void *ptr, size_t n) { return dorealloc (igcrealloc_hook.typed, ptr, n); }
static void *igccalloc (size_t n, size_t m) { return docalloc (igccalloc_hook.typed, n, m); }
static int  igcpmemalign (void **ptr, size_t align, size_t size) { return dopmemalign (igcpmemalign_hook.typed, ptr, align, size); }
static void *igcmemalign (size_t align, size_t size) { return domemalign (igcmemalign_hook.typed, align, size); }
static void *igcvalloc (size_t size) { return dovalloc (igcvalloc_hook.typed, size); }
static void igcfree (void *ptr) { return dofree (igcfree_hook.typed, ptr); }
#endif

static bool autoboot = (IgProfMem::initialize (), true);
