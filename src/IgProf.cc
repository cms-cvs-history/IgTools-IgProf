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

static void igexit (int);
static void ig_exit (int);
static int  igkill (pid_t pid, int sig);

#if __linux
static void igcexit (int);
static void igc_exit (int);
static int  igckill (pid_t pid, int sig);
#endif

IGPROF_HOOK (void (int),			exit,	igexit);
IGPROF_HOOK (void (int),			_exit,	ig_exit);
IGPROF_HOOK (int (pid_t, int),			kill,	igkill);

#if __linux
IGPROF_LIBHOOK ("libc.so.6", void (int),	exit,	igcexit);
IGPROF_LIBHOOK ("libc.so.6", void (int),	_exit,	igc_exit);
IGPROF_LIBHOOK ("libc.so.6", int (pid_t, int),	kill,	igckill);
#endif

static int		s_enabled	= 0;
static bool		s_initialized	= false;
static bool		s_pthreads	= false;
static pthread_mutex_t	s_lock;
// There is an odd bug in linux pthread mutex handling where if
// we re-enter recursive mutex locking in the same *thread* twice,
// e.g. through signal handling, it dead-locks.  So try to avoid
// that.
static pthread_key_t	s_thread_locked;


static LiveMaps &livemaps (void) { static LiveMaps s; return s; }
static ActivationList &activations (void) { static ActivationList s; return s; }
static ActivationList &deactivations (void) { static ActivationList s; return s; }

//<<<<<< PUBLIC FUNCTION DEFINITIONS                                    >>>>>>
//<<<<<< MEMBER FUNCTION DEFINITIONS                                    >>>>>>

IgProfLock::IgProfLock (int &enabled)
{
    m_locked = IgProf::lock ();
    m_enabled = enabled;
    IgProf::deactivate ();
}

IgProfLock::~IgProfLock (void)
{
    IgProf::activate ();
    if (m_locked) IgProf::unlock ();
}

//////////////////////////////////////////////////////////////////////
void
IgProf::initialize (void)
{
    if (s_initialized) return;
    s_initialized = true;

    void *program = dlopen (0, RTLD_NOW);
    if (program && dlsym (program, "pthread_create"))
    {
	s_pthreads = true;
        pthread_mutexattr_t attrs;
        pthread_mutexattr_init (&attrs);
        pthread_mutexattr_settype (&attrs, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init (&s_lock, &attrs);
        pthread_key_create (&s_thread_locked, 0);
    }
    dlclose (program);

    IgProf::debug ("Profiler core loaded\n");
    IgHook::hook (igexit_hook.raw);
    IgHook::hook (ig_exit_hook.raw);
    IgHook::hook (igkill_hook.raw);
#if __linux
    IgHook::hook (igcexit_hook.raw);
    IgHook::hook (igc_exit_hook.raw);
    IgHook::hook (igckill_hook.raw);
#endif
    IgProf::onactivate (&IgProf::enable);
    IgProf::ondeactivate (&IgProf::disable);
    IgProf::enable ();
}

bool
IgProf::lock (void)
{
    if (s_pthreads)
    {
        void *inlock = pthread_getspecific (s_thread_locked);
        if (! inlock)
        {
	    pthread_setspecific (s_thread_locked, &s_thread_locked);
            pthread_mutex_lock (&s_lock);
	    return true;
        }
    }
    return false;
}

void
IgProf::unlock (void)
{
    if (s_pthreads)
    {
        pthread_mutex_unlock (&s_lock);
	pthread_setspecific (s_thread_locked, 0);
    }
}

void
IgProf::enable (void)
{ s_enabled++; }

void
IgProf::disable (void)
{ s_enabled--; }

void
IgProf::onactivate (void (*func) (void))
{ activations ().push_back (func); }

void
IgProf::ondeactivate (void (*func) (void))
{ deactivations ().push_back (func); }

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

void
IgProf::deactivate (void)
{
    if (! s_initialized)
	return;

    ActivationList		&l = deactivations ();
    ActivationList::iterator	i = l.begin ();
    ActivationList::iterator	end = l.end ();
    for ( ; i != end; ++i)
	(*i) ();
}

const char *
IgProf::options (void)
{
     static const char *s_options = getenv ("IGPROF");
     return s_options;
}

IgHookTrace *
IgProf::root (void)
{
    static IgHookTrace *s_root = new IgHookTrace;
    return s_root;
}

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
    for (int i = 0; i < levels; ++i)
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
    fprintf (output, "<igprof>\n");
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
static void
doexit (void)
{
    IgProfLock lock (s_enabled);
    if (lock.enabled () > 0)
    {
        IgProf::debug ("exit() called, dumping state\n");
	IgProf::dump ();
        IgProf::debug ("igprof quitting\n");
    }

    IgProf::deactivate (); // twice disabled!
    s_initialized = false; // signal local data is unsafe to use
    s_pthreads = false; // make sure we no longer use threads stuff
}

static void
dokill (pid_t pid, int sig)
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
}

static void igexit (int code) { doexit (); (*igexit_hook.typed.chain) (code); }
static void ig_exit (int code) { doexit (); (*ig_exit_hook.typed.chain) (code); }
static int igkill (pid_t pid, int sig) { dokill (pid, sig); return (*igkill_hook.typed.chain) (pid, sig); }

#if __linux
static void igcexit (int code) { doexit (); (*igcexit_hook.typed.chain) (code); }
static void igc_exit (int code) { doexit (); (*igc_exit_hook.typed.chain) (code); }
static int igckill (pid_t pid, int sig) { dokill (pid, sig); return (*igckill_hook.typed.chain) (pid, sig); }
#endif

//////////////////////////////////////////////////////////////////////
static bool autoboot = (IgProf::initialize (), true);
