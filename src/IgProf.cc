//<<<<<< INCLUDES                                                       >>>>>>

#include "Ig_Tools/IgProf/src/IgProf.h"
#include "Ig_Tools/IgProf/src/IgProfMem.h"
#include "Ig_Tools/IgHook/interface/IgHook.h"
#include "Ig_Tools/IgHook/interface/IgHookTrace.h"
#include "Ig_Tools/IgHook/interface/IgHookLiveMap.h"
#include <sys/types.h>
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include <cerrno>
#include <list>
#include <map>
#include <unistd.h>
#include <sys/signal.h>

//<<<<<< PRIVATE DEFINES                                                >>>>>>
//<<<<<< PRIVATE CONSTANTS                                              >>>>>>
//<<<<<< PRIVATE TYPES                                                  >>>>>>
//<<<<<< PRIVATE VARIABLE DEFINITIONS                                   >>>>>>
//<<<<<< PUBLIC VARIABLE DEFINITIONS                                    >>>>>>
//<<<<<< CLASS STRUCTURE INITIALIZATION                                 >>>>>>
//<<<<<< PRIVATE FUNCTION DEFINITIONS                                   >>>>>>

static void igexit (int);
static void ig_exit (int);
static int  igkill (pid_t pid, int sig);

IGPROF_HOOK (void (int), exit, igexit);
IGPROF_HOOK (void (int), _exit, ig_exit);
IGPROF_HOOK (int (pid_t, int), kill, igkill);

static int					s_enabled	= 0;
static bool					s_initialized	= false;

static std::map<const char *,IgHookLiveMap *> &
livemaps (void)
{ static std::map<const char *, IgHookLiveMap *> s_livemaps; return s_livemaps; }

static std::list<void (*) (void)> &
exitlist (void)
{ static std::list<void (*) (void)> s_exitlist; return s_exitlist; }

//<<<<<< PUBLIC FUNCTION DEFINITIONS                                    >>>>>>
//<<<<<< MEMBER FUNCTION DEFINITIONS                                    >>>>>>

void
IgProf::initialize (void)
{
    if (s_initialized) return;
    s_initialized = true;

    IgProf::debug ("Profiler core loaded\n");
    IgHook::hook (igexit_hook.raw);
    IgHook::hook (ig_exit_hook.raw);
    IgHook::hook (igkill_hook.raw);
    IgProf::enable ();
}

void
IgProf::enable (void)
{ s_enabled++; }

void
IgProf::disable (void)
{ s_enabled--; }

void
IgProf::onexit (void (*func) (void))
{ exitlist ().push_back (func); }

void
IgProf::runexit (void)
{
    if (s_enabled <= 0)
	return;

    std::list<void (*) (void)> &l = exitlist ();
    std::list<void (*) (void)>::iterator i = l.begin ();
    std::list<void (*) (void)>::iterator end = l.end ();
    for (; i != end; ++i)
	(*i) ();

    IgProf::disable ();
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
dumpTrace (FILE *output, IgHookTrace *node, int depth)
{
    if (depth == 0)
	fprintf (output, "Trace\n");
    else
    { 
	for (int i = 0; i < depth; ++i)
	    fputc (' ', output);

	const char	*sym;
	const char	*lib;
	int		offset;
	bool		nonheap = node->symbol (sym, lib, offset);

	fprintf (output, "T[%p %p+%d %s (%s)]:", (void *) node, node->address (), offset, sym, lib);
	for (IgHookTrace::CounterValue *val = node->counters (); val; val = val->next ())
	    fprintf (output, " C(%s,%lu)", val->counter ()->m_name, val->value ());
	fputc ('\n', output);

	if (! nonheap) delete [] sym;
    }

    for (IgHookTrace *kid = node->children (); kid; kid = kid->next ())
	dumpTrace (output, kid, depth+1);
}

static void
dumpLiveMap (FILE *output, const char *label, IgHookLiveMap *live)
{
    fprintf (output, "\nLiveMap %s (%d)\n", label, live->size ());
    IgHookLiveMap::Iterator i = live->begin ();
    IgHookLiveMap::Iterator end = live->end ();
    for ( ; i != end; ++i)
        fprintf (output, " M[%p] = (%ld, %lu)\n",
		 (void *) i->second.first, i->first, i->second.second);
}

void
IgProf::dump (void)
{
    static bool dumping = false;
    if (dumping) return;
    dumping = true;

    IgProfMem::disable ();

    char filename [64];
    sprintf (filename, "igprof.%ld", (long) getpid ());

    FILE *output = fopen (filename, "w+");
    if (! output)
    {
	IgProf::debug ("can't write to output %s: %d\n", filename, errno);
	return;
    }

    dumpTrace (output, IgProf::root (), 0);
    std::map<const char *, IgHookLiveMap *>::iterator i = livemaps ().begin ();
    std::map<const char *, IgHookLiveMap *>::iterator end = livemaps ().end ();
    while (i != end)
    {
	dumpLiveMap (output, i->first, i->second);
	++i;
    }

    fclose (output);
    dumping = false;
    IgProfMem::enable ();
}


//////////////////////////////////////////////////////////////////////
static void igexit (int code)
{
    if (s_enabled > 0)
    {
        IgProf::debug ("exit() called, dumping state\n");
	IgProf::dump ();
        IgProf::debug ("igprof quitting\n");
    }

    IgProf::runexit ();
    (*igexit_hook.typed.chain) (code);
}

static void ig_exit (int code)
{
    if (s_enabled > 0)
    {
        IgProf::debug ("_exit() called, dumping state\n");
	IgProf::dump ();
        IgProf::debug ("igprof quitting\n");
    }

    IgProf::runexit ();
    (*ig_exit_hook.typed.chain) (code);
}

static int igkill (pid_t pid, int sig)
{
    if (s_enabled > 0
	&& (pid == 0 || pid == getpid ())
	&& (sig == SIGHUP || sig == SIGINT || sig == SIGQUIT
	    || sig == SIGILL || sig == SIGABRT || sig == SIGFPE
	    || sig == SIGKILL || sig == SIGSEGV || sig == SIGPIPE
	    || sig == SIGALRM || sig == SIGTERM || sig == SIGUSR1
	    || sig == SIGUSR2 || sig == SIGBUS || sig == SIGIOT))
    {
	IgProf::debug ("kill(%d,%d) called, dumping state\n", (int) pid, sig);
	IgProf::dump ();
    }

    return (*igkill_hook.typed.chain) (pid, sig);
}

//////////////////////////////////////////////////////////////////////
static bool autoboot = (IgProf::initialize (), true);
