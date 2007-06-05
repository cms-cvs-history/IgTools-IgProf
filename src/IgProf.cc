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
#include <unistd.h>
#include <sys/mman.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <dlfcn.h>

#ifdef __APPLE__
# include <crt_externs.h>
# define program_invocation_name **_NSGetArgv()
#endif

#if ! defined MAP_ANONYMOUS && defined MAP_ANON
# define MAP_ANONYMOUS MAP_ANON
#endif

//<<<<<< PRIVATE DEFINES                                                >>>>>>
//<<<<<< PRIVATE CONSTANTS                                              >>>>>>
//<<<<<< PRIVATE TYPES                                                  >>>>>>

// Used to capture real user start arguments in our custom thread wrapper
struct IgProfWrappedArg { void * (*start_routine) (void *); void *arg; };
struct IgProfPoolAlloc { IgProfPool *pool; int fd; bool perthread; };
struct IgProfDumpInfo { int depth; int nsyms; int nlibs; int nctrs; };
class IgProfExitDump { public: ~IgProfExitDump (); };
typedef std::list<void (*) (void)> IgProfCallList;

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
static const int	MAX_FNAME	= 1024;
static IgProfAtomic	s_enabled	= 0;
static bool		s_initialized	= false;
static bool		s_activated	= false;
static bool		s_pthreads	= false;
static volatile int	s_quitting	= 0;
static double		s_clockres	= 0;
static IgProfPoolAlloc	*s_pools	= 0;
static IgProfTrace	*s_masterbuf	= 0;
static char		s_masterbufdata [sizeof (IgProfTrace)];
static pthread_t	s_mainthread;
static pthread_t	s_readthread;
static fd_set		s_poolfd;
static pthread_key_t	s_poolkey;
static pthread_mutex_t	s_poollock;
static pthread_mutex_t	s_lock;
static char		s_outname [MAX_FNAME];
static char		s_dumpflag [MAX_FNAME];

// Static data that needs to be constructed lazily on demand
static IgProfCallList &threadinits (void)
{ static IgProfCallList *s = new IgProfCallList; return *s; }

static void
initPool (IgProfPoolAlloc *pools, int pool, bool perthread)
{
    int options = IgProfTrace::OptResources;
    if (s_pthreads)
	options |= IgProfPool::OptBuffered;
    if (s_pthreads && !perthread)
	options |= IgProfPool::OptShared;
    
    IgProfPool *p = new IgProfPool (pool, options);
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
readsafe (int fd, void *into, size_t n, ssize_t *ntot = 0)
{
    ssize_t nread = 0;
    while (nread < (ssize_t) n)
    {
	ssize_t m = read (fd, (char *) into + nread, n - nread);
	if (m == -1 && errno == EINTR)
	    continue;
	else if (m < 0)
	{
	    IgProf::debug ("INTERNAL ERROR: Failed to read profile data"
			   " (fd %d, errno %d = %s)\n",
			   fd, errno, strerror (errno));
	    abort ();
	}
	nread += m;
    }
    
    if (ntot) *ntot = nread;
    return nread == (ssize_t) n;
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

    if (! s_masterbuf)
    {
	unsigned char	*buf = 0;
	unsigned int	size = 0;
	int		opts = (IgProfTrace::OptResources
				| IgProfTrace::OptSymbolAddress);

	profileExtend (buf, size, 0, 0);
	s_masterbuf = new (s_masterbufdata) IgProfTrace;
	s_masterbuf->setup (buf, size, opts, &profileExtend);
    }

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

    void	    *buf = 0;
    pthread_t       thread = pthread_self ();
    IgProfPoolAlloc *pools
	= (thread == s_mainthread ? s_pools
	   : (IgProfPoolAlloc *) pthread_getspecific (s_poolkey));

    if (! s_pthreads && final)
	buf = malloc (IgProfPool::DEFAULT_SIZE);

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
		// The file descriptor is closed at the end by profileRead().
	        if (profileRead (pools [i].fd, buf)
	            || lseek (pools [i].fd, 0, SEEK_CUR) >= info.st_size)
		    break;
            }
	}
    }

    if (thread == s_mainthread)
	s_pools = 0;
    else
        pthread_setspecific (s_poolkey, 0);

    free (buf);
    delete [] pools;
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

/** Helper function to read one profile entry from the header file. */
int
IgProf::profileRead (int fd, void *buf)
{
    uintptr_t hdr [3];
    if (! readsafe (fd, hdr, sizeof (hdr)))
	return -1;

    switch (hdr[0])
    {
    case IgProfPool::END:
	close (fd);
	return 1;

    case IgProfPool::FILEREF:
	{
	    IgProfTrace::Header *h = (IgProfTrace::Header *) buf;
	    if (readsafe (hdr[1], buf, sizeof (*h)))
	    {
	        // debug ("merging %lu bytes profile data via disk (fd=%d)\n",
		//        h->size, (int) hdr[1]);
		if (readsafe (hdr[1], (char *)buf+sizeof *h, h->size-sizeof *h))
		    s_masterbuf->merge (buf);
	    }
	    close (hdr[1]);
	}
	break;

    case IgProfPool::MEMREF:
	// debug ("merging %lu bytes profile data via memory (buf=%p)\n",
	//        ((IgProfTrace::Header *) hdr[1])->size, (void *) hdr[1]);
	s_masterbuf->merge ((void *) hdr[1]);
	IgProfPool::release (hdr[2]);
	break;

    default:
	debug ("INTERNAL ERROR: unexpected profile type %d in profile"
	       " descriptor %d\n", (int) hdr[0], fd);
	abort ();
    }

    return 0;
}

/** Extend the backing store of a profile trace buffer. */
void
IgProf::profileExtend (unsigned char *&buf, unsigned int &size,
		       unsigned int lo, unsigned int hi)
{
    unsigned int newsize = size + 32*1024*1024;
    void	 *newbuf = mmap (0, newsize, PROT_READ | PROT_WRITE,
				 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (newbuf == MAP_FAILED)
    {
	IgProf::debug ("Failed to allocate memory for profile buffer: %s (%d)\n",
		       strerror (errno), errno);
	abort ();
    }

    if (buf)
    {
	memcpy (newbuf, buf, lo);
	memcpy ((char *) newbuf + hi + (newsize - size),
		buf + hi,
		size - hi);
	munmap (buf, size);
    }
    
    buf = (unsigned char *) newbuf;
    size = newsize;
}

/** Helper thread function to receive profile data from other threads. */
void *
IgProf::profileListenThread (void *)
{
    void *buf = malloc (IgProfPool::DEFAULT_SIZE);
    int  dodump = 0;

    while (true)
    {
	fd_set current;
	struct stat st;

	// Capture a copy of current set of file descriptors.
	pthread_mutex_lock (&s_poollock);
        memcpy (&current, &s_poolfd, sizeof (s_poolfd));
	pthread_mutex_unlock (&s_poollock);

	// Read profile data from all pool communication files.  When
	// at end-of-stream marker, close the file (in profileRead())
	// and remove the file descriptor from the s_poolfd set.
	bool finished = true;
	for (int fd = 0; fd < FD_SETSIZE; ++fd)
	{
	    while (fd < FD_SETSIZE && ! FD_ISSET (fd, &current))
		++fd;

	    int done;
	    do
	    {
	        if (fstat (fd, &st) || lseek (fd, 0, SEEK_CUR) >= st.st_size)
		    break;

		if ((done = profileRead (fd, buf)) == 1)
		{
		    pthread_mutex_lock (&s_poollock);
		    FD_CLR (fd, &s_poolfd);
		    pthread_mutex_unlock (&s_poollock);
		}
		else
		    finished = false;
	    } while (! done);
	}

	// If we are done processing, quit.  Give threads ~1s to quit.
	if (s_quitting && (finished || ++s_quitting > 100))
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

    free (buf);
    return 0;
}

/** Dump out the profile data.  */
static void
dumpProfile (FILE *output, IgProfTrace::Stack *node, IgProfDumpInfo &info)
{
    if (node->address) // No address at root
    {
	IgProfTrace::Binary  *bin;
	IgProfTrace::Symbol  *sym = s_masterbuf->getSymbol (node->address);

	if (sym->id >= 0)
	    fprintf (output, "C%d FN%d+%d", info.depth, sym->id, sym->symoffset);
	else
	{
	    const char	*symname = sym->name;
	    char	symgen [32];

	    sym->id = info.nsyms++;

	    if (! symname || ! *symname)
	    {
		sprintf (symgen, "@?%p", sym->address);
		symname = symgen;
	    }

	    bin = s_masterbuf->getBinary (sym->binary);
	    if (bin->id >= 0)
		fprintf(output, "C%d FN%d=(F%d+%d N=(%s))+%d",
			info.depth, sym->id, bin->id, sym->binoffset,
			symname, sym->symoffset);
	    else
	    {
		bin->id = info.nlibs++;
		fprintf(output, "C%d FN%d=(F%d=(%s)+%d N=(%s))+%d",
			info.depth, sym->id, bin->id,
			bin->name ? bin->name : "", sym->binoffset,
			symname, sym->symoffset);
	    }
	}

	IgProfTrace::PoolIndex cidx = node->counters;
	while (cidx)
	{
	    IgProfTrace::Counter *ctr = s_masterbuf->getCounter (cidx);
	    if (ctr->ticks || ctr->peak)
	    {
		if (ctr->def->id >= 0)
		    fprintf (output, " V%d:(%ju,%ju,%ju)",
			     ctr->def->id, ctr->ticks, ctr->value, ctr->peak);
		else
		    fprintf (output, " V%d=(%s):(%ju,%ju,%ju)",
			     (ctr->def->id = info.nctrs++), ctr->def->name,
			     ctr->ticks, ctr->value, ctr->peak);

		IgProfTrace::PoolIndex ridx = ctr->resources;
		while (ridx)
		{
		    IgProfTrace::Resource *res = s_masterbuf->getResource (ridx);
		    fprintf (output, ";LK=(%p,%ju)", (void *) res->resource, res->size);
		    ridx = res->nextlive;
		}
	    }
	    cidx = ctr->next;
	}
	fputc ('\n', output);
    }

    info.depth++;
    IgProfTrace::PoolIndex kidx = node->children;
    while (kidx)
    {
	node = s_masterbuf->getStack (kidx);
	dumpProfile (output, node, info);
	kidx = node->sibling;
    }
    info.depth--;
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

    IgProfDumpInfo info = { 0, 0, 0, 0 };
    IgProf::debug ("dumping state to %s\n", s_outname);
    fprintf (output, "P=(ID=%lu N=(%s) T=%f)\n",
	     (unsigned long) getpid (), program_invocation_name, s_clockres);
    dumpProfile (output, s_masterbuf->getStack (s_masterbuf->stackRoot ()), info);
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
