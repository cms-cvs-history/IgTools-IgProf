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

static int igopen (const char *fn, int flags, int mode);
static int igopen64 (const char *fn, int flags, int mode);
static int igclose (int fd);
static int igdup (int fd);
static int igdup2 (int fd, int newfd);
static int igsocket (int domain, int type, int proto);
static int igaccept (int fd, struct sockaddr *addr, socklen_t *len);

IGPROF_HOOK (int (const char *, int, int),	open,		igopen);
IGPROF_HOOK (int (const char *, int, int),	__open64,	igopen64);
IGPROF_HOOK (int (int),				close,		igclose);
IGPROF_HOOK (int (int),				dup,		igdup);
IGPROF_HOOK (int (int, int),			dup2,		igdup2);
IGPROF_HOOK (int (int, int, int),		socket,		igsocket);
IGPROF_HOOK (int (int, sockaddr *,socklen_t *),	accept,		igaccept);

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
    int		drop = 3; // one for stacktrace, one for me, one for hook
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
		    s_count_live = 1;
		    options += 6;
		    opts = true;
		}
		else if (! strncmp (options, ":all", 4))
		{
		    IgProf::debug ("FD: enabling everything\n");
		    s_count_used = 1;
		    s_count_leaks = 1;
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

    if (enable && !opts)
    {
	IgProf::debug ("FD: defaulting to total descriptor counting\n");
	s_count_used = 1;
    }

    if (enable)
    {
        if (s_count_leaks)
	    s_live = IgProf::liveMap ("File Descriptor Leaks");

        IgHook::hook (igopen_hook.raw);
        IgHook::hook (igopen64_hook.raw);
        IgHook::hook (igclose_hook.raw);
        IgHook::hook (igdup_hook.raw);
        IgHook::hook (igdup2_hook.raw);
        IgHook::hook (igsocket_hook.raw);
        IgHook::hook (igaccept_hook.raw);
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

static int
igopen (const char *fn, int flags, int mode)
{
    IGPROF_TRACE (("(%d igopen %s %d %d\n", s_enabled, fn, flags, mode));
    IgProfLock lock (s_enabled);
    int result = (*igopen_hook.typed.chain) (fn, flags, mode);

    if (lock.enabled () > 0 && result != -1)
	add (result);

    IGPROF_TRACE ((" -> %d)\n", result));
    return result;
}

static int
igopen64 (const char *fn, int flags, int mode)
{
    IGPROF_TRACE (("(%d igopen64 %s %d %d\n", s_enabled, fn, flags, mode));
    IgProfLock lock (s_enabled);
    int result = (*igopen64_hook.typed.chain) (fn, flags, mode);

    if (lock.enabled () > 0 && result != -1)
	add (result);

    IGPROF_TRACE ((" -> %d)\n", result));
    return result;
}

static int
igclose (int fd)
{
    IGPROF_TRACE (("(%d igclose %d\n", s_enabled, fd));
    IgProfLock lock (s_enabled);
    int result = (*igclose_hook.typed.chain) (fd);

    if (lock.enabled () > 0 && result != -1)
	remove (fd);

    IGPROF_TRACE ((" -> %d)\n", result));
    return result;
}

static int
igdup (int fd)
{
    IGPROF_TRACE (("(%d igdup %d\n", s_enabled, fd));
    IgProfLock lock (s_enabled);
    int result = (*igdup_hook.typed.chain) (fd);

    if (lock.enabled () > 0 && result != -1)
	add (result);

    IGPROF_TRACE ((" -> %d)\n", result));
    return result;
}

static int
igdup2 (int fd, int newfd)
{
    IGPROF_TRACE (("(%d igdup2 %d %d\n", s_enabled, fd, newfd));
    IgProfLock lock (s_enabled);
    int result = (*igdup2_hook.typed.chain) (fd, newfd);

    if (lock.enabled () > 0 && result != -1)
    {
	remove (newfd);
	add (newfd);
    }

    IGPROF_TRACE ((" -> %d)\n", result));
    return result;
}

static int
igsocket (int domain, int type, int proto)
{
    IGPROF_TRACE (("(%d igsocket %d %d %d\n", s_enabled, domain, type, proto));
    IgProfLock lock (s_enabled);
    int result = (*igsocket_hook.typed.chain) (domain, type, proto);

    if (lock.enabled () > 0 && result != -1)
	add (result);

    IGPROF_TRACE ((" -> %d)\n", result));
    return result;
}

static int
igaccept (int fd, struct sockaddr *addr, socklen_t *len)
{
    IGPROF_TRACE (("(%d igaccept %d %d\n", s_enabled, fd, how));
    IgProfLock lock (s_enabled);
    int result = (*igaccept_hook.typed.chain) (fd, addr, len);

    if (lock.enabled () > 0 && result != -1)
	add (result);

    IGPROF_TRACE ((" -> %d)\n", result));
    return result;
}

static bool autoboot = (IgProfFileDesc::initialize (), true);
