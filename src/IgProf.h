#ifndef IG_PROF_IG_PROF_H
# define IG_PROF_IG_PROF_H

//<<<<<< INCLUDES                                                       >>>>>>

# include "Ig_Tools/IgHook/interface/IgHook.h"
# include "Ig_Tools/IgProf/src/IgProfMacros.h"

//<<<<<< PUBLIC DEFINES                                                 >>>>>>
//<<<<<< PUBLIC CONSTANTS                                               >>>>>>
//<<<<<< PUBLIC TYPES                                                   >>>>>>

class IgHookTrace;
class IgHookLiveMap;

//<<<<<< PUBLIC VARIABLES                                               >>>>>>
//<<<<<< PUBLIC FUNCTIONS                                               >>>>>>
//<<<<<< CLASS DECLARATIONS                                             >>>>>>

/** Core profiling implementation.  Implements utilities needed
    to implement actual profiler modules as well as final dumps. */
class IgProf
{
public:
    static int			panic (const char *file, int line,
	    			       const char *func, const char *expr);
    static void			debug (const char *format, ...);
    static const char *		options (void);
    static IgHookTrace *	root (void);
    static IgHookLiveMap *	liveMap (const char *label);

    static void			initialize (void);
    static void			dump (void);

    static void			onactivate (void (*func) (void));
    static void			ondeactivate (void (*func) (void));

    static void			activate (void);
    static void			deactivate (void);

    static void			initThread (void);
    static bool			isMultiThreaded (void);

private:
    friend class IgProfLock;
    static bool			lock (void *);
    static void			unlock (void *);

    static void			enable (void);
    static void			disable (void);
};

/** Acquire a lock on the profiling system.  Obtains the
    lock and deactivates all profiler modules.  */
class IgProfLock
{
public:
    IgProfLock (int &enabled);
    ~IgProfLock (void);

    int		enabled (void);

private:
    IgProfLock (const IgProfLock &);
    IgProfLock &operator= (const IgProfLock &);

    bool	m_locked;
    int		m_enabled;
};

//<<<<<< INLINE PUBLIC FUNCTIONS                                        >>>>>>
//<<<<<< INLINE MEMBER FUNCTIONS                                        >>>>>>

/** Return the state of this profiler module after the lock has been
    acquired.  Value greater than zero indicates the module was active
    before the lock was obtained and the caller can proceed to record
    information.  If the return value is zero or less, caller should
    only do whatever is minimally required to mimic what ever function
    it trapped, or ignore the request.  */
inline int
IgProfLock::enabled (void)
{ return m_locked ? m_enabled : 0; }

#endif // IG_PROF_IG_PROF_H
