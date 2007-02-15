//<<<<<< INCLUDES                                                       >>>>>>

#include "Ig_Tools/IgProf/src/IgProf.h"
#include "Ig_Tools/IgProf/src/IgProfPool.h"
#include "Ig_Tools/IgProf/src/IgProfAtomic.h"
#include "Ig_Tools/IgHook/interface/IgHook.h"
#include "Ig_Tools/IgHook/interface/IgHookTrace.h"
#include "Ig_Tools/IgHook/interface/IgHookLiveMap.h"
#include <sys/types.h>
#include <sys/time.h>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include <cerrno>
#include <list>
#include <map>
#include <unistd.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <dlfcn.h>

#ifdef __APPLE__
# include <crt_externs.h>
# define program_invocation_name **_NSGetArgv()
#endif

//<<<<<< PRIVATE DEFINES                                                >>>>>>
//<<<<<< PRIVATE CONSTANTS                                              >>>>>>
//<<<<<< PRIVATE TYPES                                                  >>>>>>

// Used to capture real user start arguments in our custom thread wrapper
struct IgProfWrappedArg { void * (*start_routine) (void *); void *arg; };
struct IgProfPoolAlloc { IgProfPool *pool; int fd; bool perthread; };
struct IgProfNodeCache { void *addr; IgHookTrace *node; };
struct IgProfReadBuf { size_t size; size_t n; char *data; char *pos; off_t off; };
class IgProfExitDump { public: ~IgProfExitDump (); };

typedef std::map<const char *, IgHookLiveMap *> IgProfLiveMaps;
typedef std::list<IgProfAtomic *>		IgProfGuardList;
typedef std::list<void (*) (void)>		IgProfCallList;
#if __GNUC__
typedef __gnu_cxx::hash_map
    <unsigned long, void *, __gnu_cxx::hash<unsigned long>,
     std::equal_to<unsigned long>,
     IgHookAlloc< std::pair<unsigned long, void *> > > IgProfSymCache;
#else
typedef std::map
    <unsigned long, void *, std::less<unsigned long>,
     IgHookAlloc< std::pair<unsigned long, void *> > > IgProfSymCache;
#endif

//<<<<<< PRIVATE VARIABLE DEFINITIONS                                   >>>>>>
//<<<<<< PUBLIC VARIABLE DEFINITIONS                                    >>>>>>
//<<<<<< CLASS STRUCTURE INITIALIZATION                                 >>>>>>
//<<<<<< PRIVATE FUNCTION DEFINITIONS                                   >>>>>>

// Traps for this profiling module
IGPROF_DUAL_HOOK (1, void, doexit, _main, _libc,
		  (int code), (code),
		  "exit", 0, "libc.so.6")
IGPROF_DUAL_HOOK (1, void, doexit, _main2, _libc2,
		  (int code), (code),
		  "_exit", 0, "libc.so.6")
IGPROF_DUAL_HOOK (2, int,  dokill, _main, _libc,
		  (pid_t pid, int sig), (pid, sig),
		  "kill", 0, "libc.so.6")

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
static const int	N_POOLS		= 8;
static const unsigned	MAX_DATA	= 512;
static const int	MAX_FNAME	= 1024;
static IgProfAtomic	s_enabled	= 0;
static bool		s_initialized	= false;
static bool		s_activated	= false;
static bool		s_pthreads	= false;
static bool		s_quitting	= 0;
static double		s_clockres	= 0;
static IgProfPoolAlloc	*s_pools	= 0;
static pthread_t	s_mainthread;
static pthread_t	s_readthread;
static fd_set		s_poolfd;
static pthread_key_t	s_poolkey;
static pthread_mutex_t	s_poollock;
static pthread_mutex_t	s_lock;
static IgProfNodeCache	s_nodecache [MAX_DATA];
static IgProfSymCache	*s_symcache;
static char		s_outname [MAX_FNAME];
static char		s_dumpflag [MAX_FNAME];

// Static data that needs to be constructed lazily on demand
static IgProfLiveMaps &livemaps (void)
{ static IgProfLiveMaps *s = new IgProfLiveMaps; return *s; }

static IgProfCallList &threadinits (void)
{ static IgProfCallList *s = new IgProfCallList; return *s; }

// Helper function to initialise pools.
static void
initPool (IgProfPoolAlloc *pools, int pool, bool perthread)
{
    IgProfPool *p = new IgProfPool (pool, s_pthreads, s_pthreads && !perthread);
    pools [pool].pool = p;
    pools [pool].fd = p->readfd ();
    pools [pool].perthread = perthread;

    if (s_pthreads)
    {
	pthread_mutex_lock (&s_poollock);
	FD_SET (pools[pool].fd, &s_poolfd);
	pthread_mutex_unlock (&s_poollock);
    }
}

static bool
readsafe (IgProfReadBuf &buf, int fd, void *into, size_t n, bool end, const char *msg)
{
    // If the buffer is underfilled, fill it now.
    if (buf.n < n && fd != -1)
    {
	memmove (buf.data, buf.pos, buf.n);
	buf.pos = buf.data;

	while (buf.n < buf.size)
	{
	    ssize_t m = read (fd, buf.data + buf.n, buf.size - buf.n);
	    if (m < 0 && errno == EINTR)
		continue;
	    else if (m < 0)
	    {
		IgProf::debug ("INTERNAL ERROR: %s (fd %d, errno %d)\n",
			       msg, fd, errno);
		abort ();
	    }
	    if (m == 0)
		break;
	    buf.n += m;
	}

	buf.off = lseek (fd, 0, SEEK_CUR);
    }

    // Now read from our buffer.  It's always guaranteed to be larger
    // than any actual profile data read we are going to make.  The
    // only reason it might not have enough data is that our read
    // came short, meaning there is no more data in the file.
    if (buf.n < n && end)
    {
	IgProf::debug ("End of profile data on descriptor %d pos %lld, buffered %lu\n",
		       fd, (long long) lseek (fd, 0, SEEK_CUR), (unsigned long) buf.n);
	return false;
    }

    if (buf.n < n)
    {
	IgProf::debug ("INTERNAL ERROR: %s (fd %d, expected %lu, have %lu)\n",
		       msg, fd, (unsigned long) n, (unsigned long) buf.n);
	abort ();
    }

    memcpy (into, buf.pos, n);
    buf.pos += n;
    buf.n -= n;
    return true;
}

//<<<<<< PUBLIC FUNCTION DEFINITIONS                                    >>>>>>
//<<<<<< MEMBER FUNCTION DEFINITIONS                                    >>>>>>

/** Lock and disable the profiling system, but grab the
    global enabled flag just before that.  Later calls
    to #enabled() will indicate if profiling is enabled.
    Never call this from asynchronous signals! */
IgProfLock::IgProfLock (void)
{
    if ((m_enabled = IgProf::disable ()))
	m_locked = IgProf::lock ();
    else
	m_locked = false;
}

/** Release the lock on the profiling system.  */
IgProfLock::~IgProfLock (void)
{
    if (m_locked)
	IgProf::unlock ();

    IgProf::enable ();
}

//////////////////////////////////////////////////////////////////////
/** Initialise the profiler core itself.  Prepares the the program
    for profiling.  Captures various exit points so we generate a
    dump before the program goes "out".  Automatically triggered
    to run on library load.  All profiler modules should invoke
    this method before doing their own initialisation.

    Returns @c true if profiling is activated in this process.  */
bool
IgProf::initialize (int *moduleid, void (*threadinit) (void), bool perthread)
{
    if (! s_initialized)
    {
	s_initialized = true;

	const char *options = IgProf::options ();
	if (! options || ! *options)
	{
	    IgProf::debug ("$IGPROF not set, not profiling this process\n");
	    return s_activated = false;
	}

	for (const char *opts = options; *opts; )
	{
	    while (*opts == ' ' || *opts == ',')
		++opts;

	    if (! strncmp (opts, "igprof:out='", 12))
	    {
		int i = 0;
		opts += 12;
		while (i < MAX_FNAME-1 && *opts && *opts != '\'')
		    s_outname[i++] = *opts++;
		s_outname[i] = 0;
	    }
	    else if (! strncmp (opts, "igprof:dump='", 13))
	    {
		int i = 0;
		opts += 13;
		while (i < MAX_FNAME-1 && *opts && *opts != '\'')
		    s_dumpflag[i++] = *opts++;
		s_dumpflag[i] = 0;
	    }
	    else
		opts++;

	    while (*opts && *opts != ',' && *opts != ' ')
		opts++;
	}

	if (! s_outname[0])
	    sprintf (s_outname, "igprof.%ld", (long) getpid ());

	s_pools = new IgProfPoolAlloc [N_POOLS];
	memset (s_pools, 0, N_POOLS * sizeof (*s_pools));

	void *program = dlopen (0, RTLD_NOW);
	if (program && dlsym (program, "pthread_create"))
	{
	    s_pthreads = true;
	    pthread_mutexattr_t attrs;
	    pthread_mutexattr_init (&attrs);
	    pthread_mutexattr_settype (&attrs, PTHREAD_MUTEX_RECURSIVE);
	    pthread_mutex_init (&s_lock, &attrs);
	    pthread_mutex_init (&s_poollock, &attrs);
	    pthread_key_create (&s_poolkey, 0);
	    pthread_create (&s_readthread, 0, &IgProf::profileListenThread, 0);
	}
	dlclose (program);

	const char *target = getenv ("IGPROF_TARGET");
	s_mainthread = pthread_self ();
	if (target && ! strstr (program_invocation_name, target))
	{
	    IgProf::debug ("Current process not selected for profiling:"
		       	    " process '%s' does not match '%s'\n",
		       	    program_invocation_name, target);
	    return s_activated = false;
    	}

	itimerval precision;
	itimerval interval = { { 0, 10000 }, { 100, 0 } };
	itimerval nullified = { { 0, 0 }, { 0, 0 } };
	setitimer (ITIMER_PROF, &interval, 0);
	getitimer (ITIMER_PROF, &precision);
	setitimer (ITIMER_PROF, &nullified, 0);
	s_clockres = (precision.it_interval.tv_sec
		      + 1e-6 * precision.it_interval.tv_usec);

	IgProf::debug ("Activated in %s, timing resolution %f, %s,"
		       " main thread id 0x%lx\n",
		       program_invocation_name, s_clockres,
		       s_pthreads ? "multi-threaded" : "no threads",
		       s_mainthread);
	IgProf::debug ("Options: %s\n", options);

	IgHook::hook (doexit_hook_main.raw);
	IgHook::hook (doexit_hook_main2.raw);
	IgHook::hook (dokill_hook_main.raw);
 #if __linux
	if (doexit_hook_main.raw.chain)  IgHook::hook (doexit_hook_libc.raw);
	if (doexit_hook_main2.raw.chain) IgHook::hook (doexit_hook_libc2.raw);
	if (dokill_hook_main.raw.chain)  IgHook::hook (dokill_hook_libc.raw);
#endif
	if (s_pthreads)
	{
	    IgHook::hook (dopthread_create_hook_main.raw);
#if __linux
	    IgHook::hook (dopthread_create_hook_pthread20.raw);
	    IgHook::hook (dopthread_create_hook_pthread21.raw);
#endif
	}
	s_activated = true;
	s_enabled = 1;
    }

    if (! s_activated)
	return false;

    if (! moduleid)
	return true;

    IgProf::disable ();

    int pool;
    for (pool = 0; pool < N_POOLS; ++pool)
	if (! s_pools [pool].pool)
	{
	    initPool (s_pools, pool, perthread);
	    *moduleid = pool;
	    break;
	}

    if (pool == N_POOLS)
    {
	 IgProf::debug ("Too many profilers enabled (%d), need to"
			" rebuild IgProf with larger N_POOLS\n",
			N_POOLS);
	 abort ();
    }

    if (threadinit)
	threadinits ().push_back (threadinit);

    IgProf::enable ();
    return true;
}

/** Return @c true if the process was linked against threading package.  */
bool
IgProf::isMultiThreaded (void)
{ return s_pthreads; }

/** Setup a thread so it can be used in profiling.  This should be
    called for every thread that will participate in profiling.  */
void
IgProf::initThread (void)
{
    IgProfPoolAlloc *pools = new IgProfPoolAlloc [N_POOLS];
    memset (pools, 0, N_POOLS * sizeof (*pools));
    pthread_setspecific (s_poolkey, pools);

    for (int i = 0; i < N_POOLS && s_pools[i].pool; ++i)
	if (s_pools[i].perthread)
	    initPool (pools, i, true);
}

/** Finalise a thread.  */
void
IgProf::exitThread (bool final)
{
    if (! s_pthreads && ! final)
	return;

    IgProfReadBuf   buf = { 32*1024, 0, 0, 0, 0 };
    IgProfReadBuf   zbuf = { 1024*1024, 0, 0, 0, 0 };
    pthread_t       thread = pthread_self ();
    IgProfPoolAlloc *pools
	= (thread == s_mainthread ? s_pools
	   : (IgProfPoolAlloc *) pthread_getspecific (s_poolkey));

    if (! s_pthreads && final)
    {
	buf.data = (char *) malloc (buf.size);
	zbuf.data = (char *) malloc (zbuf.size);
    }

    for (int i = 0; i < N_POOLS && pools; ++i)
    {
	IgProfPool *p = pools[i].pool;
	if (! p) continue;

	p->finish ();
	pools [i].pool = 0;
	delete p;

        if (! s_pthreads && final)
	{
	    struct stat info;
	    if (fstat (pools [i].fd, &info))
	        continue;

	    while (true)
	    {
	        if (profileReadHunk (pools [i].fd, buf, zbuf)
	            || lseek (pools [i].fd, 0, SEEK_CUR) >= info.st_size)
		    break;
            }

	    if (pools[i].fd >= 0)
	        close (pools [i].fd);
	}
    }

    if (thread == s_mainthread)
	s_pools = 0;
    else
        pthread_setspecific (s_poolkey, 0);

    delete [] pools;
    free (buf.data);
    free (zbuf.data);
}

/** Return a profile accumulation pool for a profiler in the
    current thread.  It is safe to call this function from any
    thread and in asynchronous signal handlers at any time, no
    locks on the profiling system need to be taken.
    
    Returns the pool to use or a null to indicate no data should
    be gathered in the calling context, for example if the profile
    core itself has already been destroyed.  */
IgProfPool *
IgProf::pool (int moduleid)
{
    // Check which pool to return.  We return the one from s_pools
    // in non-threaded applications and always in the main thread.
    // We never return a pool in the read thread.  In other threads
    // we return the main thread pool if a single pool was requested,
    // otherwise a per-thread pool.
    pthread_t thread = pthread_self ();
    IgProfPoolAlloc *pools = s_pools;
    if (! s_initialized || (s_pthreads && thread == s_readthread))
	pools = 0;
    else if (thread != s_mainthread && s_pools [moduleid].perthread)
	pools = (IgProfPoolAlloc *) pthread_getspecific (s_poolkey);

    return pools ? pools[moduleid].pool : 0;
}

/** Acquire a lock on the profiler system.  All profiling modules
    should call this method (through use of #IgProfLock) before
    messing with significant portions of the profiler global state.
    However calls to #pool() do not need to be protected by locks.

    Returns @c true to indicate that a lock was successfully acquired,
    @c false otherwise, for example before and after the profiling
    system is properly initialised.

    This function must not be called from asynchronous signals.  */
bool
IgProf::lock (void)
{
    if (s_pthreads)
    {
	pthread_mutex_lock (&s_lock);
	return true;
    }

    return s_initialized;
}

/** Release profiling system lock after successful call to #lock().  */
void
IgProf::unlock (void)
{
    if (s_pthreads)
	pthread_mutex_unlock (&s_lock);
}

/** Check if the profiler is currently enabled.  This function should
    be called by asynchronous signal handlers to check whether they
    should record profile data -- not for the actual running where
    the value of the flag has little useful value, but to make sure
    no data is gathered after the system has started to close down.  */
bool
IgProf::enabled (void)
{
    return s_enabled > 0;
}

/** Enable the profiling system.  This is safe to call from anywhere,
    but note that profiling system will only be enabled, not unlocked.
    Use #IgProfLock instead if you need to manage exclusive access.
    
    Returns @c true if the profiler is enabled after the call. */
bool
IgProf::enable (void)
{
    IgProfAtomic newval = IgProfAtomicInc (&s_enabled);
    return newval > 0;
}

/** Disable the profiling system.  This is safe to call from anywhere,
    but note that profiling system will only be disabled, not locked.
    Use #IgProfLock instead if you need to manage exclusive access.
    
    Returns @c true if the profiler was enabled before the call.  */
bool
IgProf::disable (void)
{
    IgProfAtomic newval = IgProfAtomicDec (&s_enabled);
    return newval >= 0;
}

/** Get user-provided profiling options.  */
const char *
IgProf::options (void)
{
     static const char *s_options = getenv ("IGPROF");
     return s_options;
}

/** Get the root of the profiling counter tree.  Only access the the
    tree within an #IgProfLock.  In general, only call this method
    when the profiler module is constructed; if necessary within
    #IgProfLock.  Note that this means you should not call this
    method from places where #IgProfLock cannot be called from,
    such as in asynchronous signal handlers.  */
IgHookTrace *
IgProf::root (void)
{
    static IgHookTrace *s_root = new IgHookTrace;
    return s_root;
}

/** Get leak/live tracking map named @a label.  The argument must be
    statically allocated (a string literal will do).  Returns pointer
    to the named map.  In general, only call this method when the
    profiler module is constructed; if necessary within #IgProfLock.  */
IgHookLiveMap *
IgProf::liveMap (const char *label)
{
    IgHookLiveMap *&map = livemaps () [label];
    if (! map)
	map = new IgHookLiveMap;

    return map;
}

/** Internal assertion helper routine.  */
int
IgProf::panic (const char *file, int line, const char *func, const char *expr)
{
    IgProf::disable ();

#if __linux
    fprintf (stderr, "%s: ", program_invocation_name);
#endif
    fprintf (stderr, "%s:%d: %s: assertion failure: %s\n", file, line, func, expr);

    void *trace [128];
    int levels = IgHookTrace::stacktrace (trace, 128);
    for (int i = 2; i < levels; ++i)
    {
	const char	*sym = 0;
	const char	*lib = 0;
	int		offset = 0;
	int		liboffset = 0;
	bool		nonheap = IgHookTrace::symbol (trace [i], sym, lib, offset, liboffset);
	fprintf (stderr, "  %p %s + %d [%s + %d]\n", trace [i], sym, offset, lib, liboffset);
	if (! nonheap) delete [] sym;
    }

    // abort ();
    IgProf::enable ();
    return 1;
}

/** Internal printf()-like debugging utility.  Produces output if
    $IGPROF_DEBUGGING environment variable is set to any value.  */
void
IgProf::debug (const char *format, ...)
{
    static const char *debugging = getenv ("IGPROF_DEBUGGING");
    if (debugging)
    {
	timeval tv;
	gettimeofday (&tv, 0);
	fprintf (stderr, "*** IgProf(%lu, %.3f): ",
		 (unsigned long) getpid(),
		 tv.tv_sec + 1e-6*tv.tv_usec);

	va_list args;
	va_start (args, format);
	vfprintf (stderr, format, args);
	va_end (args);
    }
}

/** Helper function to read one profile entry.
    This function can be called from one thread only, ever.  */
int
IgProf::profileReadHunk (int &fd, IgProfReadBuf &buf, IgProfReadBuf &zbuf)
{
    unsigned long words;
    unsigned long data [MAX_DATA];

    if (! s_symcache)
	s_symcache = new IgProfSymCache;

    if (! readsafe (buf, fd, &words, sizeof (words), true,
		    "Failed to read profile header"))
	return -1;

    if (words > MAX_DATA)
    {
	IgProf::debug ("INTERNAL ERROR: Too much profile data in"
		       " profile fd %d (%lu > %lu)\n",
		       fd, words, MAX_DATA);
	abort ();
    }

    readsafe (buf, fd, data, words * sizeof (data[0]), false,
	      "Failed to read profile data");

    IgHookTrace *node = 0;
    for (unsigned long i = 0; i < words; )
    {
	unsigned long type = data[i++];
	switch (type)
	{
	case IgProfPool::END:
	    if (fd != -1)
	    {
		close (fd);
	        fd = -1;
	    }
	    return 1;

	case IgProfPool::FILEREF:
	    {
		IGPROF_ASSERT (&zbuf != &buf);

		int newfd = data[i++];

		zbuf.n = 0;
		zbuf.off = 0;
		zbuf.pos = zbuf.data;

		while (! profileReadHunk (newfd, zbuf, zbuf))
		    ;
	    }
	    node = 0;
	    break;

	case IgProfPool::MEMREF:
	    {
		unsigned long	info = data[i++];
		char		*buffer = (char *) data[i++];
		unsigned long	size = data[i++];
		IgProfReadBuf	thisbuf = { size, size, buffer, buffer, size };
		int		newfd = -1;

		while (! profileReadHunk (newfd, thisbuf, zbuf))
		    ;

		IgProfPool::release (info);
	    }
	    node = 0;
	    break;

	case IgProfPool::STACK:
	    {
	        node = root ();
		unsigned int start = i-1;
		unsigned int depth = data[i++];
	        for (unsigned int j = 0, valid = 1; j < depth; ++j, ++i)
	        {
		    if (i >= words)
		    {
			IgProf::debug ("OOPS: stack trace went beyond end of available data: "
				       " FD=%d DEPTH=%u START=%u WORDS=%lu\n",
				       fd, depth, start, words);
			node = 0;
			break;
		    }

		    void *addr = (void *) data[i];
		    if (valid && s_nodecache [j].addr == addr)
			node = s_nodecache [j].node;
		    else
		    {
			IgProfSymCache::iterator symaddr = s_symcache->find (data[i]);
			if (symaddr == s_symcache->end())
			{
			    void *mapped = IgHookTrace::tosymbol (addr);
			    symaddr = s_symcache->insert
				(IgProfSymCache::value_type (data[i], mapped)).first;
			}
			node = node->child (symaddr->second);
			s_nodecache[j].addr = addr;
			s_nodecache[j].node = node;
			valid = 0;
		    }
	        }
	    }
	    break;

	case IgProfPool::TICK:
	case IgProfPool::MAX:
	case IgProfPool::ACQUIRE:
	case IgProfPool::RELEASE:
	    {
		IgHookTrace::Counter *counter = (IgHookTrace::Counter *) data[i++];
		IgHookTrace::Counter *pcounter = (IgHookTrace::Counter *) data[i++];
		unsigned long	     amount = data[i++];
		unsigned long	     resource = data[i++];
		unsigned long long   val = 0;

		if (type == IgProfPool::TICK || type == IgProfPool::ACQUIRE)
		    val = node->counter (counter)->add (amount);
		else if (type == IgProfPool::MAX)
		    val = node->counter (counter)->max (amount);

		if (pcounter)
		    node->counter (pcounter)->max (val);

		if (type == IgProfPool::ACQUIRE)
		{
		    IgHookLiveMap *live = IgProf::liveMap (counter->m_name);
		    IgHookLiveMap::Iterator pos = live->find (resource);
		    if (pos != live->end ())
		    {
		        IgProf::debug ("New %s resource %lu was never freed, previously from\n",
				       counter->m_name, resource);
			int nnn = 0;
			for (IgHookTrace *n = pos->second.first; n; n = n->parent ())
			    fprintf (stderr, "  %10p%s", n->address (), ++nnn % 6 ? "   " : "\n");
			fputc ('\n', stderr);

		        size_t      size = pos->second.second;
		        IgHookTrace *resnode = pos->second.first;
		        IGPROF_ASSERT (resnode->counter (counter)->value () >= size);
		        resnode->counter (counter)->sub (size);
		        live->remove (pos);
		    }
		    live->insert (resource, node, amount);
		}
		else if (type == IgProfPool::RELEASE)
		{
		    IgHookLiveMap *live = IgProf::liveMap (counter->m_name);
		    IgHookLiveMap::Iterator info = live->find (resource);
		    if (info == live->end ())
			// Probably allocated before our tracking.
			break;

		    size_t      size = info->second.second;
		    IgHookTrace *resnode = info->second.first;
		    IGPROF_ASSERT (resnode->counter (counter)->value () >= size);
		    resnode->counter (counter)->sub (size);
		    live->remove (info);
		}
	    }
	    break;

	default:
	    IgProf::debug ("INTERNAL ERROR: unexpected profile type %lu"
			   " in profile descriptor %d at hunk index %lu of %lu\n",
			   type, fd, i, words);
	    abort ();
	}
   }

   return 0;
}

/** Helper thread function to receive profile data from other threads. */
void *
IgProf::profileListenThread (void *)
{
    int		  dodump = 0;
    IgProfReadBuf buf = { 32*1024, 0, 0, 0, 0 };
    IgProfReadBuf zbuf = { 1024*1024, 0, 0, 0, 0 };
    buf.data = (char *) malloc (buf.size);
    zbuf.data = (char *) malloc (zbuf.size);

    while (true)
    {
	fd_set current;

	// Capture a copy of current set of file descriptors.
	pthread_mutex_lock (&s_poollock);
        memcpy (&current, &s_poolfd, sizeof (s_poolfd));
	pthread_mutex_unlock (&s_poollock);

	// Check how many descriptors we have in use.
	int maxfd = -1;
	int nset = 0;
	struct stat st;
	for (int i = 0; i < FD_SETSIZE; ++i)
	    if (FD_ISSET (i, &current))
	    {
		maxfd = i;
		nset++;
	    }

	// Read profile data from all files ready with data.
	// If we read the end-of-stream marker, remove the
	// file descriptor from the available ones; it has
	// already been closed by profileReadHunk().
	for (int lastfd = 0; nset > 0; ++lastfd, --nset)
	{
	    while (! FD_ISSET (lastfd, &current))
		++lastfd;

	    buf.off = lseek (lastfd, 0, SEEK_CUR);
	    buf.pos = buf.data;
	    buf.n = 0;

	    int done;
	    do
	    {
	        if (fstat (lastfd, &st) || buf.off >= st.st_size)
		{
		    // Reset file position and buffer.
		    lseek (lastfd, buf.off - buf.n, SEEK_SET);
		    break;
		}

		int fd = lastfd;
		done = profileReadHunk (fd, buf, zbuf);
		if (done == 1)
		{
		    pthread_mutex_lock (&s_poollock);
		    FD_CLR (lastfd, &s_poolfd);
		    if (fd >= 0) FD_SET (fd, &s_poolfd);
		    pthread_mutex_unlock (&s_poollock);
		}
	    } while (! done);
	}

	// If we are done processing, quit.  Give threads max ~1s to quit.
	if (s_quitting && (maxfd < 0 || ++s_quitting > 100))
	    break;

	// Check every once in a while if a dump has been requested.
	if (s_dumpflag[0] && ! (++dodump % 128) && ! stat (s_dumpflag, &st))
	{
	    unlink (s_dumpflag);
	    IgProf::dump ();
	    dodump = 0;
	}

	// Have a nap.
	usleep (10000);
    }

    free (buf.data);
    free (zbuf.data);
    return 0;
}

/** Dump out the profile data.  */
static void
dumpProfile (FILE *output, IgHookTrace *node, void *infoptr = 0)
{
    typedef std::pair<int,unsigned long> SymInfo;
    typedef std::pair<unsigned long, SymInfo> SymDesc;
#if __GNUC__
    typedef __gnu_cxx::hash_map
	<unsigned long, SymInfo, __gnu_cxx::hash<unsigned long>,
	 std::equal_to<unsigned long>,
	 IgHookAlloc< std::pair<unsigned long, SymInfo> > >
	SymIndex;
    typedef __gnu_cxx::hash_map
	<const char *, int, __gnu_cxx::hash<const char *>,
	 std::equal_to<const char *>,
	 IgHookAlloc< std::pair<const char *, int> > >
	CounterIndex;
    typedef __gnu_cxx::hash_map
	<const char *, int, __gnu_cxx::hash<const char *>,
	 std::equal_to<const char *>,
	 IgHookAlloc< std::pair<const char *, int> > >
	LibIndex;
#else
    typedef std::map
	<unsigned long, SymInfo, std::less<unsigned long>,
	 IgHookAlloc< std::pair<unsigned long, SymInfo> > >
	SymIndex;
    typedef std::map
	<const char *, int, std::less<const char *>,
	 IgHookAlloc< std::pair<const char *, int> > >
	CounterIndex;
    typedef std::map
	<const char *, int, std::less<const char *>,
	 IgHookAlloc< std::pair<const char *, int> > >
	LibIndex;
#endif

    struct Info
    {
	CounterIndex	counters;
	LibIndex	libs;
	SymIndex	syms;
	int		depth;
	int		nsyms;
    };

    Info *info = (Info *) infoptr;
    if (! info)
    {
	info = new Info;
        info->depth = 0;
	info->nsyms = 0;
    }

    if (node->address ()) // No address at root
    {
	unsigned long		calladdr = (unsigned long) node->address ();
	SymIndex::iterator	sym = info->syms.find (calladdr);
	if (sym != info->syms.end ())
	    fprintf (output, "C%d FN%d+%d",
		     info->depth, sym->second.first,
		     (int) (calladdr - sym->second.second));
	else
	{
	    const char	*symname;
	    const char	*libname;
	    int		offset;
	    int		liboffset;
	    bool	fixed;

	    fixed = node->symbol (symname, libname, offset, liboffset);
	    if (! libname) libname = "";

	    LibIndex::iterator lib = info->libs.find (libname);
	    bool	       needlib = false;
	    int		       libid;

	    if (lib != info->libs.end ())
		libid = lib->second;
	    else
	    {
		libid = info->libs.size ();
		info->libs.insert (std::pair<const char *,int>(libname, libid));
		needlib = true;
	    }

	    unsigned long	symaddr = calladdr - offset;
	    bool		needsym = false;
	    int			symid;

	    sym = info->syms.find (symaddr);
	    if (sym != info->syms.end ())
		symid = sym->second.first;
	    else
	    {
		symid = info->nsyms++;
		info->syms.insert (SymDesc (calladdr, SymInfo (symid, symaddr)));
		if (offset != 0)
		    info->syms.insert (SymDesc (symaddr, SymInfo (symid, symaddr)));
		needsym = true;
	    }

	    if (needlib)
		fprintf(output, "C%d FN%d=(F%d=(%s)+%d N=(%s))+%d",
			info->depth, symid, libid, libname ? libname : "",
			liboffset, symname ? symname : "", offset);
	    else if (needsym)
		fprintf(output, "C%d FN%d=(F%d+%d N=(%s))+%d",
			info->depth, symid, libid, liboffset,
			symname ? symname : "", offset);
	    else
		fprintf(output, "C%d FN%d+%d", info->depth, symid, offset);

	    if (! fixed) delete [] symname;
	}

	for (IgHookTrace::CounterValue *val = node->counters (); val; val = val->next ())
	{
	    if (! val->value ())
		continue;

	    const char			*ctrname = val->counter ()->m_name;
	    CounterIndex::iterator	ctr = info->counters.find (ctrname);

	    if (ctr != info->counters.end ())
		fprintf (output, " V%d:(%llu,%llu)",
			 ctr->second, val->value (), val->count ());
	    else
	    {
		int ctrid = info->counters.size ();
		info->counters.insert (std::pair<const char *, int>
				       (ctrname, ctrid));
		fprintf (output, " V%d=(%s):(%llu,%llu)",
			 ctrid, ctrname, val->value (), val->count ());
	    }
	}

	fputc ('\n', output);
    }

    // FIXME: Dump out leaks from the function here.

    info->depth++;
    for (IgHookTrace *kid = node->children (); kid; kid = kid->next ())
	dumpProfile (output, kid, info);
    info->depth--;

    if (! infoptr)
	delete info;
}

/** Dump out the profiler data: trace tree and live maps.  */
void
IgProf::dump (void)
{
    static bool dumping = false;
    if (dumping) return;
    dumping = true;

    FILE *output = (s_outname[0] == '|'
		    ? (unsetenv("LD_PRELOAD"), popen(s_outname+1, "w"))
		    : fopen (s_outname, "w+"));
    if (! output)
    {
	IgProf::debug ("can't write to output %s: %d\n", s_outname, errno);
	dumping = false;
	return;
    }

    IgProf::debug ("dumping state to %s\n", s_outname);
    fprintf (output, "P=(ID=%lu N=(%s) T=%f)\n",
	     (unsigned long) getpid (), program_invocation_name, s_clockres);
    dumpProfile (output, IgProf::root ());
#if 0
    IgProfLiveMaps::iterator i = livemaps ().begin ();
    IgProfLiveMaps::iterator end = livemaps ().end ();
    int nmaps = 0;
    for ( ; i != end; ++i)
    {
        fprintf (output, "LM%d=(S=%lu N=(%s))", nmaps++, i->second->size (), i->first);
        IgHookLiveMap::Iterator m = i->second->begin ();
        IgHookLiveMap::Iterator mend = i->second->end ();
        for ( ; m != mend; ++m)
            fprintf (output, " LK=(N=%p R=%ld I=%lu)",
		     (void *) m->second.first, m->first, m->second.second);
	fputc('\n', output);
    }
#endif

    fclose (output);
    dumping = false;
}


//////////////////////////////////////////////////////////////////////
/** A wrapper for starting user threads to enable profiling.  */
static void *
threadWrapper (void *arg)
{
    // Get arguments.
    IgProfWrappedArg *wrapped = (IgProfWrappedArg *) arg;
    void *(*start_routine) (void*) = wrapped->start_routine;
    void *start_arg = wrapped->arg;
    delete wrapped;

    // Report the thread and enable per-thread profiling pools.
    if (s_initialized)
    {
        IgProf::debug ("captured thread id 0x%lx for profiling (%p, %p)\n",
		       (unsigned long) pthread_self (),
		       (void *) start_routine,
		       start_arg);

	IgProf::initThread ();
    }

    // Make sure we've called stack trace code at least once in
    // this thread before the profile signal hits.  Linux's
    // backtrace() seems to want to call pthread_once() which
    // can be bad news inside the signal handler.
    void *dummy = 0; IgHookTrace::stacktrace (&dummy, 1);

    // Run per-profiler initialisation.
    if (s_initialized)
    {
        IgProfCallList				&l = threadinits ();
        IgProfCallList::reverse_iterator	i = l.rbegin ();
        IgProfCallList::reverse_iterator	end = l.rend ();
        for ( ; i != end; ++i)
	    (*i) ();
    }

    // Run the user thread.
    void *ret = (*start_routine) (start_arg);

    // Harvest thread profile result.
    if (s_initialized)
    {
        IgProf::debug ("leaving thread id 0x%lx from profiling (%p, %p)\n",
		       (unsigned long) pthread_self (),
		       (void *) start_routine,
		       start_arg);
        IgProf::exitThread (false);
    }
    return ret;
}

/** Trap thread creation to run per-profiler initialisation.  */
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
    IgProf::disable ();
    IgProfWrappedArg *wrapped = new IgProfWrappedArg;
    wrapped->start_routine = start_routine;
    wrapped->arg = arg;
    IgProf::enable ();
    int ret = hook.chain (thread, attr, &threadWrapper, wrapped);
    return ret;
}

/** Trapped calls to exit() and _exit().  */
static void
doexit (IgHook::SafeData<igprof_doexit_t> &hook, int code)
{
    // Force the merge of per-thread profile tree into the main tree
    // if a thread calls exit().  Then forward the call.
    {
        IgProfLock lock;
	pthread_t  thread = pthread_self ();
	if (s_pthreads && thread != s_mainthread)
	{
	    IgProf::debug ("merging thread id 0x%lx profile on %s(%d)\n",
			   (unsigned long) thread, hook.function, code);
	    IgProf::exitThread (false);
	}
    }
    hook.chain (code);
}

/** Trapped calls to kill().  Dump out profiler data if the signal
    looks dangerous.  Mostly really to trap calls to abort().  */
static int
dokill (IgHook::SafeData<igprof_dokill_t> &hook, pid_t pid, int sig)
{
    if ((pid == 0 || pid == getpid ())
	&& (sig == SIGHUP || sig == SIGINT || sig == SIGQUIT
	    || sig == SIGILL || sig == SIGABRT || sig == SIGFPE
	    || sig == SIGKILL || sig == SIGSEGV || sig == SIGPIPE
	    || sig == SIGALRM || sig == SIGTERM || sig == SIGUSR1
	    || sig == SIGUSR2 || sig == SIGBUS || sig == SIGIOT))
    {
        IgProfLock lock;
	if (lock.enabled () > 0)
	{
	    IgProf::debug ("kill(%d,%d) called, dumping state\n", (int) pid, sig);
	    IgProf::dump ();
	}
    }
    return hook.chain (pid, sig);
}

//////////////////////////////////////////////////////////////////////
/** Dump out profile data when application is about to exit. */
IgProfExitDump::~IgProfExitDump (void)
{
    if (! s_activated) return;
    IgProf::debug ("merging thread id 0x%lx profile on final exit\n",
		   (unsigned long) pthread_self ());
    IgProf::exitThread (true);
    s_activated = false;
    s_quitting = 1;
    if (s_pthreads) pthread_join (s_readthread, 0);
    IgProf::disable ();
    IgProf::dump ();
    IgProf::debug ("igprof quitting\n");
    s_initialized = false; // signal local data is unsafe to use
    s_pthreads = false; // make sure we no longer use threads stuff
}

//////////////////////////////////////////////////////////////////////
static bool autoboot = IgProf::initialize (0, 0, false);
static IgProfExitDump exitdump;
