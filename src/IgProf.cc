//<<<<<< INCLUDES                                                       >>>>>>

#include "Ig_Tools/IgProf/src/IgProf.h"
#include "Ig_Tools/IgProf/src/IgProfMem.h"
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
#include <set>
#include <unistd.h>
#include <sys/signal.h>
#include <pthread.h>
#include <dlfcn.h>

//<<<<<< PRIVATE DEFINES                                                >>>>>>
//<<<<<< PRIVATE CONSTANTS                                              >>>>>>
//<<<<<< PRIVATE TYPES                                                  >>>>>>

class IgProfExitDump { public: ~IgProfExitDump (); };
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
static bool		s_activated	= false;
static bool		s_pthreads	= false;
static double		s_clockres	= 0;
static pthread_t	s_mainthread;
static pthread_key_t	s_troot;
static pthread_mutex_t	s_lock;

// Static data that needs to be constructed lazily on demand
static LiveMaps &livemaps (void)
{ static LiveMaps *s = new LiveMaps; return *s; }

static ActivationList &activations (void)
{ static ActivationList *s = new ActivationList; return *s; }

static ActivationList &deactivations (void)
{ static ActivationList *s = new ActivationList; return *s; }

//<<<<<< PUBLIC FUNCTION DEFINITIONS                                    >>>>>>
//<<<<<< MEMBER FUNCTION DEFINITIONS                                    >>>>>>

/** Lock profiling system and grab the value of @a enabled.
    Later calls to #enabled() will indicate if the calling
    module has already been disabled by someone else.
    Never call this from asynchronous signals! */
IgProfLock::IgProfLock (int &enabled)
{
    m_locked = IgProf::lock ();
    m_enabled = enabled;
    IgProf::deactivate ();
}

/** Release the lock on the profiling system.  */
IgProfLock::~IgProfLock (void)
{
    IgProf::activate ();
    IgProf::unlock ();
}

//////////////////////////////////////////////////////////////////////
/** Initialise the profiler core itself.  Prepares the the program
    for profiling.  Captures various exit points so we generate a
    dump before the program goes "out".  Automatically triggered
    to run on library load.  All profiler modules should invoke
    this method before doing their own initialisation.

    Returns @c true if profiling is activated in this process.  */
bool
IgProf::initialize (void)
{
    if (s_initialized) return s_activated;
    s_initialized = true;

    void *program = dlopen (0, RTLD_NOW);
    if (program && dlsym (program, "pthread_create"))
    {
	s_pthreads = true;
	pthread_mutexattr_t attrs;
	pthread_mutexattr_init (&attrs);
	pthread_mutexattr_settype (&attrs, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init (&s_lock, &attrs);
	pthread_key_create (&s_troot, 0);
	initThread ();
    }
    dlclose (program);

    const char *target = getenv ("IGPROF_TARGET");
    s_mainthread = pthread_self ();
    if (target && ! strstr (program_invocation_name, target))
    {
	IgProf::debug ("Current process not selected for profiling\n");
	return s_activated = false;
    }

    itimerval interval = { { 0, 5000 }, { 100, 0 } };
    itimerval precision;
    setitimer (ITIMER_PROF, &interval, 0);
    getitimer (ITIMER_PROF, &precision);
    interval.it_interval.tv_sec = interval.it_interval.tv_usec = 0;
    setitimer (ITIMER_PROF, &interval, 0);
    s_clockres = (precision.it_interval.tv_sec
		  + 1e-6 * precision.it_interval.tv_usec);

    IgProf::debug ("Profiler core loaded, running %s, timing resolution %lf\n",
		   s_pthreads ? "multi-threaded" : "without threads", s_clockres);
    IgProf::debug ("Options: %s\n", IgProf::options());

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
    return s_activated = true;
}

/** Return @c true if the process was linked against threading package.  */
bool
IgProf::isMultiThreaded (void)
{ return s_pthreads; }

/** Setup a thread so it can be used in profiling.  This should be
    called for every thread that will participate in profiling.  */
void
IgProf::initThread (void)
{ threadRoot (); }

/** Finalise a thread.  */
void
IgProf::exitThread (void)
{
    if (! s_pthreads)
	return;

    IgHookTrace *troot = (IgHookTrace *) pthread_getspecific (s_troot);
    if (! troot)
	return;

    pthread_setspecific (s_troot, 0);
    pthread_mutex_lock (&s_lock);
    root ()->merge (troot);
    pthread_mutex_unlock (&s_lock);
}

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
    #IgProfLock.  Note that this means you should not call this
    method from places where #IgProfLock cannot be called from,
    such as in asynchronous signal handlers.  */
IgHookTrace *
IgProf::root (void)
{
    static IgHookTrace *s_root = new IgHookTrace;
    return s_root;
}

/** Get thread-specific stack trace root.  Use this instead of #root()
    in asynchronous signals.  However you must then not use #IgProfLock
    or call anything that might call it.  Thread-specific trace trees
    are automatically merged to the main tree when the thread exits.  */
IgHookTrace *
IgProf::threadRoot (void)
{
    // It is unsafe to use pthread primitives in asynchronous signals.
    // The only allowed operation is sem_post(), and in fact experience
    // shows that using pthread locks with the performance profiler in
    // fact does break sooner or later.  So provide profiler modules
    // traces they can modify with interlocking with the rest of the
    // system.
    if (s_pthreads)
    {
	IgHookTrace *troot = (IgHookTrace *) pthread_getspecific (s_troot);
	if (! troot)
	{
	    troot = new IgHookTrace;
	    pthread_setspecific (s_troot, troot);
	}

	return troot;
    }

    return root ();
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
	int		liboffset = 0;
	bool		nonheap = IgHookTrace::symbol (trace [i], sym, lib, offset, liboffset);
	fprintf (stderr, "  %p %s + %d [%s + %d]\n", trace [i], sym, offset, lib, liboffset);
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

    const char *options = IgProf::options ();
    char       outname [1024];

    outname [0] = 0;
    while (options && *options)
    {
	while (*options == ' ' || *options == ',')
	    ++options;

	if (! strncmp (options, "out='", 5))
	{
	    int i = 0;
	    options += 5;
	    while (i < 1023 && *options && *options != '\'')
		outname[i++] = *options++;
	    outname[i] = 0;
	}
	else
	    options++;

	while (*options && *options != ',' && *options != ' ')
	    options++;
    }

    if (! *outname)
        sprintf (outname, "igprof.%ld", (long) getpid ());

    struct timeval tstart, tend;
    gettimeofday (&tstart, 0);

    FILE *output = (outname[0] == '|'
		    ? (unsetenv("LD_PRELOAD"), popen(outname+1, "w"))
		    : fopen (outname, "w+"));
    if (! output)
    {
	IgProf::debug ("can't write to output %s: %d\n", outname, errno);
	dumping = false;
	return;
    }

    IgProf::debug ("dumping state to %s\n", outname);
#if __linux
    fprintf (output, "P=(ID=%lu N=(%s) T=%lf)\n",
	     (unsigned long) getpid (), program_invocation_name, s_clockres);
#else
    fprintf (output, "P=(ID=%lu T=%lf)\n", (unsigned long) getpid (), s_clockres);
#endif
    dumpProfile (output, IgProf::root ());
    LiveMaps::iterator i = livemaps ().begin ();
    LiveMaps::iterator end = livemaps ().end ();
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

    fclose (output);
    gettimeofday (&tend, 0);
    IgProf::debug ("dump took %.2lf seconds\n",
		   (tend.tv_sec + 1e-6 * tend.tv_usec)
		   - (tstart.tv_sec + 1e-6 * tstart.tv_usec));
    dumping = false;
}


//////////////////////////////////////////////////////////////////////
/** Trapped calls to exit() and _exit().  */
static void
doexit (IgHook::SafeData<igprof_doexit_t> &hook, int code)
{
    // Force the merge of per-thread profile tree into the main tree
    // if a thread calls exit().  Then forward the call.
    {
        IgProfLock lock (s_enabled);
	pthread_t thread = pthread_self ();
	if (s_pthreads)
	{
	    IgProf::debug ("merging thread %lu profile on %s(%d)\n",
			   (unsigned long) thread, hook.function, code);
	    IgProf::exitThread ();
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
/** Dump out profile data when application is about to exit. */
IgProfExitDump::~IgProfExitDump (void)
{
    IgProf::deactivate ();
    IgProf::dump ();
    IgProf::debug ("igprof quitting\n");
    s_initialized = false; // signal local data is unsafe to use
    s_pthreads = false; // make sure we no longer use threads stuff
}

//////////////////////////////////////////////////////////////////////
static bool autoboot = IgProf::initialize ();
static IgProfExitDump exitdump;
