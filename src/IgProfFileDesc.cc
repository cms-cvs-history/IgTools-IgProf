//<<<<<< INCLUDES                                                       >>>>>>

#include "Ig_Tools/IgProf/src/IgProfFileDesc.h"
#include "Ig_Tools/IgProf/src/IgProf.h"
#include "Ig_Tools/IgHook/interface/IgHook.h"
#include "Ig_Tools/IgHook/interface/IgHookTrace.h"
#include "Ig_Tools/IgHook/interface/IgHookLiveMap.h"
#include <cstdlib>
#include <cstdio>
#include <sys/socket.h>

//<<<<<< PRIVATE DEFINES                                                >>>>>>
//<<<<<< PRIVATE CONSTANTS                                              >>>>>>
//<<<<<< PRIVATE TYPES                                                  >>>>>>
//<<<<<< PRIVATE VARIABLE DEFINITIONS                                   >>>>>>
//<<<<<< PUBLIC VARIABLE DEFINITIONS                                    >>>>>>
//<<<<<< CLASS STRUCTURE INITIALIZATION                                 >>>>>>
//<<<<<< PRIVATE FUNCTION DEFINITIONS                                   >>>>>>

// Traps for this profiling module
IGPROF_DUAL_HOOK (3, int, doopen, _main, _libc,
		  (const char *fn, int flags, int mode), (fn, flags, mode),
		  "open", 0, "libc.so.6")
IGPROF_DUAL_HOOK (3, int, doopen64, _main, _libc,
		  (const char *fn, int flags, int mode), (fn, flags, mode),
		  "__open64", 0, "libc.so.6")
IGPROF_DUAL_HOOK (1, int, doclose, _main, _libc,
		  (int fd), (fd),
		  "close", 0, "libc.so.6")
IGPROF_DUAL_HOOK (1, int, dodup, _main, _libc,
		  (int fd), (fd),
		  "dup", 0, "libc.so.6")
IGPROF_DUAL_HOOK (2, int, dodup2, _main, _libc,
		  (int fd, int newfd), (fd, newfd),
		  "dup2", 0, "libc.so.6")
IGPROF_DUAL_HOOK (3, int, dosocket, _main, _libc,
		  (int domain, int type, int proto), (domain, type, proto),
		  "socket", 0, "libc.so.6")
IGPROF_DUAL_HOOK (3, int, doaccept, _main, _libc,
		  (int fd, sockaddr *addr, socklen_t *len), (fd, addr, len),
		  "accept", 0, "libc.so.6")

// Data for this profiling module
static IgHookTrace::Counter	s_ct_used	= { "FD_USED" };
static IgHookTrace::Counter	s_ct_live	= { "FD_LIVE" };
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
    int		drop = 4; // one for stacktrace, one for me, two for hook
    IgHookTrace	*node = IgProf::root ();
    void	*addresses [128];
    int		depth = IgHookTrace::stacktrace (addresses, 128);

    // Walk the tree
    for (int i = depth-2; i >= drop; --i)
	node = node->child (IgHookTrace::tosymbol (addresses [i]));

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
	if (info == s_live->end ())
	    // Unknown to us, probably allocated before we started.
	    // IGPROF_ASSERT (info != s_live->end ());
	    return;

	IgHookTrace	*node = info->second.first;

	IGPROF_ASSERT (node->counter (&s_ct_live)->value () >= 1);
	node->counter (&s_ct_live)->untick ();

	s_live->remove (info);
    }
}

//<<<<<< PUBLIC FUNCTION DEFINITIONS                                    >>>>>>
//<<<<<< MEMBER FUNCTION DEFINITIONS                                    >>>>>>

/** Initialise file descriptor profiling.  Traps various system
    calls to keep track of usage, and if requested, leaks.  */
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
	while (*options == ' ' || *options == ',')
	    ++options;

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

        IgHook::hook (doopen_hook_main.raw);
        IgHook::hook (doopen64_hook_main.raw);
        IgHook::hook (doclose_hook_main.raw);
        IgHook::hook (dodup_hook_main.raw);
        IgHook::hook (dodup2_hook_main.raw);
        IgHook::hook (dosocket_hook_main.raw);
        IgHook::hook (doaccept_hook_main.raw);
#if __linux
        if (doopen_hook_main.raw.chain)   IgHook::hook (doopen_hook_libc.raw);
        if (doopen64_hook_main.raw.chain) IgHook::hook (doopen64_hook_libc.raw);
        if (doclose_hook_main.raw.chain)  IgHook::hook (doclose_hook_libc.raw);
        if (dodup_hook_main.raw.chain)    IgHook::hook (dodup_hook_libc.raw);
        if (dodup2_hook_main.raw.chain)   IgHook::hook (dodup2_hook_libc.raw);
        if (dosocket_hook_main.raw.chain) IgHook::hook (dosocket_hook_libc.raw);
        if (doaccept_hook_main.raw.chain) IgHook::hook (doaccept_hook_libc.raw);
#endif
        IgProf::debug ("File descriptor profiler enabled\n");
        IgProf::onactivate (&IgProfFileDesc::enable);
        IgProf::ondeactivate (&IgProfFileDesc::disable);
	IgProfFileDesc::enable ();
    }
}

/** Enable this profiling module.  Only call within #IgProfLock.
    Normally called automatically through activation by #IgProfLock.
    Allows recursive enable/disable.  */
void
IgProfFileDesc::enable (void)
{ s_enabled++; }

/** Disable this profiling module.  Only call within #IgProfLock.
    Normally called automatically through activation by #IgProfLock.
    Allows recursive enable/disable.  */
void
IgProfFileDesc::disable (void)
{ s_enabled--; }

//////////////////////////////////////////////////////////////////////
// Trapped system calls.  Track live file descriptor usage.
static int
doopen (IgHook::SafeData<igprof_doopen_t> &hook, const char *fn, int flags, int mode)
{
    IGPROF_TRACE (("(%d igopen %s %d %d\n", s_enabled, fn, flags, mode));
    IgProfLock lock (s_enabled);
    int result = (*hook.chain) (fn, flags, mode);

    if (lock.enabled () > 0 && result != -1)
	add (result);

    IGPROF_TRACE ((" -> %d)\n", result));
    return result;
}

static int
doopen64 (IgHook::SafeData<igprof_doopen64_t> &hook, const char *fn, int flags, int mode)
{
    IGPROF_TRACE (("(%d igopen64 %s %d %d\n", s_enabled, fn, flags, mode));
    IgProfLock lock (s_enabled);
    int result = (*hook.chain) (fn, flags, mode);

    if (lock.enabled () > 0 && result != -1)
	add (result);

    IGPROF_TRACE ((" -> %d)\n", result));
    return result;
}

static int
doclose (IgHook::SafeData<igprof_doclose_t> &hook, int fd)
{
    IGPROF_TRACE (("(%d igclose %d\n", s_enabled, fd));
    IgProfLock lock (s_enabled);
    int result = (*hook.chain) (fd);

    if (lock.enabled () > 0 && result != -1)
	remove (fd);

    IGPROF_TRACE ((" -> %d)\n", result));
    return result;
}


static int
dodup (IgHook::SafeData<igprof_dodup_t> &hook, int fd)
{
    IGPROF_TRACE (("(%d igdup %d\n", s_enabled, fd));
    IgProfLock lock (s_enabled);
    int result = (*hook.chain) (fd);

    if (lock.enabled () > 0 && result != -1)
	add (result);

    IGPROF_TRACE ((" -> %d)\n", result));
    return result;
}

static int
dodup2 (IgHook::SafeData<igprof_dodup2_t> &hook, int fd, int newfd)
{
    IGPROF_TRACE (("(%d igdup2 %d %d\n", s_enabled, fd, newfd));
    IgProfLock lock (s_enabled);
    int result = (*hook.chain) (fd, newfd);

    if (lock.enabled () > 0 && result != -1)
    {
	remove (newfd);
	add (newfd);
    }

    IGPROF_TRACE ((" -> %d)\n", result));
    return result;
}

static int
dosocket (IgHook::SafeData<igprof_dosocket_t> &hook, int domain, int type, int proto)
{
    IGPROF_TRACE (("(%d igsocket %d %d %d\n", s_enabled, domain, type, proto));
    IgProfLock lock (s_enabled);
    int result = (*hook.chain) (domain, type, proto);

    if (lock.enabled () > 0 && result != -1)
	add (result);

    IGPROF_TRACE ((" -> %d)\n", result));
    return result;
}

static int
doaccept (IgHook::SafeData<igprof_doaccept_t> &hook,
	  int fd, struct sockaddr *addr, socklen_t *len)
{
    IGPROF_TRACE (("(%d igaccept %d %p %p\n", s_enabled,
		   fd, (void *) addr, (void *) len));
    IgProfLock lock (s_enabled);
    int result = (*hook.chain) (fd, addr, len);

    if (lock.enabled () > 0 && result != -1)
	add (result);

    IGPROF_TRACE ((" -> %d)\n", result));
    return result;
}

//////////////////////////////////////////////////////////////////////
static bool autoboot = (IgProfFileDesc::initialize (), true);
