//<<<<<< INCLUDES                                                       >>>>>>

#include "Ig_Tools/IgProf/src/IgProf.h"
#include "Ig_Tools/IgProf/src/IgProfMem.h"
#include "Ig_Tools/IgHook/interface/IgHook.h"
#include "Ig_Tools/IgHook/interface/IgHookTrace.h"
#include "Ig_Tools/IgHook/interface/IgHookLiveMap.h"
#include <sys/types.h>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include <cerrno>
#include <list>
#include <map>
#include <set>
#include <unistd.h>
#include <sys/signal.h>
#include <pthread.h>
#include <dlfcn.h>

//<<<<<< PRIVATE DEFINES                                                >>>>>>
//<<<<<< PRIVATE CONSTANTS                                              >>>>>>
//<<<<<< PRIVATE TYPES                                                  >>>>>>

typedef std::map<const char *, IgHookLiveMap *> LiveMaps;
typedef std::list<void (*) (void)>		ActivationList;

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

// Data for this profiler module
static int		s_enabled	= 0;
static bool		s_initialized	= false;
static bool		s_pthreads	= false;
static unsigned long	s_guard		= 0;
static pthread_t	s_mainthread;

// Static data that needs to be constructed lazily on demand
static LiveMaps &livemaps (void) { static LiveMaps s; return s; }
static ActivationList &activations (void) { static ActivationList s; return s; }
static ActivationList &deactivations (void) { static ActivationList s; return s; }

// Locking primitives
static inline unsigned long
compare_and_swap (unsigned long *value, unsigned long oldval, unsigned long newval)
{
#if __i386
    unsigned long prev;
    asm __volatile__ (
	"lock; cmpxchgl %1, %2"
	: "=a" (prev)
	: "q" (newval), "m" (*value), "0" (oldval)
	: "memory");
    return prev;
#else
# error your platform is not supported!
#endif
}

// static bool
// test_and_set (unsigned long *value)
// { return compare_and_swap (value, 0, 1) == 0; }

static void
release_guard (unsigned long *value)
{ *value = 0; __asm __volatile ("" : : : "memory"); }

//<<<<<< PUBLIC FUNCTION DEFINITIONS                                    >>>>>>
//<<<<<< MEMBER FUNCTION DEFINITIONS                                    >>>>>>

/** Lock profiling system and grab the value of @a enabled.
    Later calls to #enabled() will indicate if the calling
    module has already been disabled by someone else.  */
IgProfLock::IgProfLock (int &enabled)
{
    // See comments in/on IgProf::lock() to understand this.
    m_locked = IgProf::lock (this);
    m_enabled = enabled;
    if (m_locked)
        IgProf::deactivate ();
}

/** Release the lock on the profiling system.  */
IgProfLock::~IgProfLock (void)
{
    // See comments in/on IgProf::lock() to understand this.
    if (m_locked)
    {
        IgProf::activate ();
	IgProf::unlock (this);
    }
}

//////////////////////////////////////////////////////////////////////
/** Initialise the profiler core itself.  Prepares the the program
    for profiling.  Captures various exit points so we generate a
    dump before the program goes "out".  Automatically triggered
    to run on library load.  All profiler modules should invoke
    this method before doing their own initialisation.  */
void
IgProf::initialize (void)
{
    if (s_initialized) return;
    s_initialized = true;

    void *program = dlopen (0, RTLD_NOW);
    if (program && dlsym (program, "pthread_create"))
    {
	s_pthreads = true;
	initThread ();
    }
    dlclose (program);

    s_mainthread = pthread_self ();
    IgProf::debug ("Profiler core loaded\n");
    IgHook::hook (doexit_hook_main.raw);
    IgHook::hook (doexit_hook_main2.raw);
    IgHook::hook (dokill_hook_main.raw);
#if __linux
    if (doexit_hook_main.raw.chain)  IgHook::hook (doexit_hook_libc.raw);
    if (doexit_hook_main2.raw.chain) IgHook::hook (doexit_hook_libc2.raw);
    if (dokill_hook_main.raw.chain)  IgHook::hook (dokill_hook_libc.raw);
#endif
    IgProf::onactivate (&IgProf::enable);
    IgProf::ondeactivate (&IgProf::disable);
    IgProf::enable ();
}

/** Return @c true if the process was linked against threading package.  */
bool
IgProf::isMultiThreaded (void)
{ return s_pthreads; }

/** Setup a thread so it can be used in profiling.  This should be
    called for every thread that will participate in profiling.  */
void
IgProf::initThread (void)
{}

/** Acquire a lock on the profiler system.  All profiling modules
    should call this method (through use of #IgProfLock) before
    messing with global state, such as recording call traces.

    Returns @c true to indicate that a lock was successfully acquired.
    May return @c false for some systems in some rather obscure (but
    frequently occuring) circumstances to indicate that the calling
    thread is already in the process of trying to obtain the lock
    and trying to re-acquire it might cause dead-lock.  This can
    happen if profiler modules "stop on each other" through signal
    handlers.  */
bool
IgProf::lock (void *owner)
{
    // Ok, this code is funky.  Initially it was a simple mutex op
    // the pthread_mutex_lock(&s_lock).  On GNU/Linux (CERN RH 7.3,
    // GLIBC 2.2.x), one can not re-enter pthread_mutex_{,un}lock()
    // in the same thread twice.  It can only handle non-concurrent
    // locking attempts in the same thread with recursive mutexes.
    // The performance profiler triggers this condition: we get the
    // SIGPROF signal while some other profiling module in the same
    // thread is already trying to obtain or release the lock.  The
    // result is a dead-lock.
    //
    // So we don't do that.  Instead, we use global lock and just
    // IgProfLock "sorry, you can't have the lock now"; it will then
    // do the right thing.  The cost: you have to define assembler
    // sequence or some other function that can do compare-and-swap.
    //
    // A previous version of this code had thread-local storage for
    // the guard, which was tested with compare_and_swap to see
    // if thread already holds the lock, and then proceeded to use
    // the mutex if it was safe.  That worked admirably well until
    // the thread exits, memory is freed -- and memory profiler's
    // free routine tries to use the lock, and now-invalid thread
    // local storage.  Again, result: dead-lock.
    //
    // The function returns "false" to tell IgProfLock we failed to
    // grab the lock.  This in turn causes it pretend to the caller
    // that the profiler module is disabled (whether it was or not).
    // In reality, it really makes no difference whether we use a
    // mutex or the simple global guard -- once the profiler is off,
    // it's off.  We might as well make IgProfLock lie.
    //
    // Note that this also handles getting *two* different signals
    // in the *same* thread quickly in succession, e.g. if the
    // profiled program allocates memory or uses files in signal
    // handlers.
    if (s_pthreads && compare_and_swap (&s_guard, 0, (unsigned long) owner))
	// The lock is already held.
	return false;

    return s_initialized;
}

/** Release profiling system lock after successful call to #lock().  */
void
IgProf::unlock (void *owner)
{
    // See lock() for description on what's going on.
    if (s_pthreads)
    {
	if (s_guard != (unsigned long) owner)
	{
	    debug ("%lu[%lu] guard owner mismatch %p %p\n",
		   (unsigned long) getpid (),
		   (unsigned long) pthread_self (),
		   owner, (void *) s_guard);
	    IGPROF_ASSERT (! "lock mismatch");
	}

	release_guard (&s_guard);
    }
}

/** Enable this profiling module.  Only call within #IgProfLock.
    Normally called automatically through activation by #IgProfLock.
    Allows recursive enable/disable.  */
void
IgProf::enable (void)
{ s_enabled++; }

/** Disable this profiling module.  Only call within #IgProfLock.
    Normally called automatically through activation by #IgProfLock.
    Allows recursive enable/disable.  */
void
IgProf::disable (void)
{ s_enabled--; }

/** Register @a func to be run when a lock on the profiling system
    is about to be released and all all profiling modules need to
    be reactivated.  Note that the activation functions must support
    recursive activation; incrementing an @c int is sufficient.  */
void
IgProf::onactivate (void (*func) (void))
{ activations ().push_back (func); }

/** Register @a func to be run when a lock on the profiling system
    has just been acquired and all all profiling modules need to
    be deactivated.  Note that the deactivation functions must support
    recursive deactivation; decrementing an @c int is sufficient.  */
void
IgProf::ondeactivate (void (*func) (void))
{ deactivations ().push_back (func); }

/** Activate all profiler modules.  Only use through #IgProfLock.  */
void
IgProf::activate (void)
{
    if (! s_initialized)
	return;

    ActivationList		&l = activations ();
    ActivationList::iterator	i = l.begin ();
    ActivationList::iterator	end = l.end ();
    for ( ; i != end; ++i)
	(*i) ();
}

/** Deactivate all profiler modules.  Only use through #IgProfLock.  */
void
IgProf::deactivate (void)
{
    if (! s_initialized)
	return;

    ActivationList			&l = deactivations ();
    ActivationList::reverse_iterator	i = l.rbegin ();
    ActivationList::reverse_iterator	end = l.rend ();
    for ( ; i != end; ++i)
	(*i) ();
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
    #IgProfLock.  */
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
    IgProf::deactivate ();

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
	bool		nonheap = IgHookTrace::symbol (trace [i], sym, lib, offset);
	fprintf (stderr, "  %p %s + %d [%s]\n", trace [i], sym, offset, lib);
	if (! nonheap) delete [] sym;
    }

    // abort ();
    IgProf::activate ();
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
	fprintf (stderr, "*** IgProf: ");

	va_list args;
	va_start (args, format);
	vfprintf (stderr, format, args);
	va_end (args);
    }
}

/** Dump out all unique symbols.  */
static void
dumpSymbols (FILE *output, IgHookTrace *node, std::set<void *> &addresses)
{
    if (node->address () && ! addresses.count (node->address ()))
    { 
	const char	*sym;
	const char	*lib;
	int		offset;
	bool		nonheap = node->symbol (sym, lib, offset);

	// FIXME: url quote lib name!
	fprintf (output, "  <sym addr=\"%p\" offset=\"%d\" name=\"%s\" lib=\"%s\"/>\n",
		 node->address (), offset, sym ? sym : "", lib ? lib : "");

	if (! nonheap) delete [] sym;

	addresses.insert (node->address ());
    }

    for (IgHookTrace *kid = node->children (); kid; kid = kid->next ())
	dumpSymbols (output, kid, addresses);
}

/** Dump out the trace tree.  */
static void
dumpTrace (FILE *output, IgHookTrace *node, int depth)
{
    if (depth > 0)
    { 
	for (int i = 0; i <= depth; ++i)
	    fputc (' ', output);

	fprintf (output, "<node id=\"%p\" symaddr=\"%p\">\n", (void *) node, node->address ());
	for (IgHookTrace::CounterValue *val = node->counters (); val; val = val->next ())
	{
	    for (int i = 0; i <= depth+1; ++i)
	        fputc (' ', output);

	    fprintf (output, "<counter name=\"%s\" value=\"%lu\" count=\"%lu\"/>\n",
		     val->counter ()->m_name, val->value (), val->count ());
	}
    }

    for (IgHookTrace *kid = node->children (); kid; kid = kid->next ())
	dumpTrace (output, kid, depth+1);

    if (depth > 0)
    {
	for (int i = 0; i <= depth; ++i)
	    fputc (' ', output);

	fprintf (output, "</node>\n");
    }
}

/** Dump out the profiler data: trace tree and live maps.  */
void
IgProf::dump (void)
{
    static bool dumping = false;
    if (dumping) return;
    dumping = true;

    char filename [64];
    sprintf (filename, "igprof.%ld", (long) getpid ());

    FILE *output = fopen (filename, "w+");
    if (! output)
    {
	IgProf::debug ("can't write to output %s: %d\n", filename, errno);
	dumping = false;
	return;
    }

    IgProf::debug ("dumping state to %s\n", filename);
    std::set<void *> addresses;
#if __linux
    fprintf (output, "<igprof program=\"%s\" pid=\"%lu\">\n",
	     program_invocation_name, (unsigned long) getpid ());
#else
    fprintf (output, "<igprof pid=\"%lu\">\n", (unsigned long) getpid ());
#endif
    fprintf (output, " <symbols>\n");
    dumpSymbols (output, IgProf::root (), addresses);
    fprintf (output, " </symbols>\n");
    fprintf (output, " <trace>\n");
    dumpTrace (output, IgProf::root (), 0);
    fprintf (output, " </trace>\n");
    LiveMaps::iterator i = livemaps ().begin ();
    LiveMaps::iterator end = livemaps ().end ();
    for ( ; i != end; ++i)
    {
        fprintf (output, " <live map=\"%s\" size=\"%lu\">\n", i->first, i->second->size ());
        IgHookLiveMap::Iterator m = i->second->begin ();
        IgHookLiveMap::Iterator mend = i->second->end ();
        for ( ; m != mend; ++m)
            fprintf (output, "  <leak node=\"%p\" resource=\"%ld\" info=\"%lu\"/>\n",
		     (void *) m->second.first, m->first, m->second.second);
        fprintf (output, " </live>\n");
    }

    fprintf (output, "</igprof>\n");
    fclose (output);
    dumping = false;
}


//////////////////////////////////////////////////////////////////////
/** Trapped calls to exit() and _exit().  Dump out profiler data.  */
static void
doexit (IgHook::SafeData<igprof_doexit_t> &hook, int code)
{
    {
	// Disable if we were already enabled.  Note that on
	// Linux, end of thread calls into __pthread_do_exit()
	// which actually calls exit() and lands here.  So be
	// sure not to disable the system unnecessarily.

        IgProfLock lock (s_enabled);
	pthread_t thread = pthread_self ();
        if (lock.enabled () > 0 && thread == s_mainthread)
        {
            IgProf::debug ("%s() called, dumping state\n", hook.function);
	    IgProf::dump ();
            IgProf::debug ("igprof quitting\n");

            IgProf::deactivate (); // twice disabled!
            s_initialized = false; // signal local data is unsafe to use
            s_pthreads = false; // make sure we no longer use threads stuff
	}
	else if (s_pthreads && thread != s_mainthread)
	    IgProf::debug ("thread %lu %s(), continuing\n",
			   (unsigned long) thread, hook.function);
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
        IgProfLock lock (s_enabled);
	if (lock.enabled () > 0)
	{
	    IgProf::debug ("kill(%d,%d) called, dumping state\n", (int) pid, sig);
	    IgProf::dump ();
	}
    }
    return hook.chain (pid, sig);
}

//////////////////////////////////////////////////////////////////////
static bool autoboot = (IgProf::initialize (), true);
