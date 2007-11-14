#ifndef IG_PROF_IG_PROF_H
# define IG_PROF_IG_PROF_H

//<<<<<< INCLUDES                                                       >>>>>>

# include "IgTools/IgHook/interface/IgHook.h"
# include "IgTools/IgProf/src/IgProfMacros.h"

//<<<<<< PUBLIC DEFINES                                                 >>>>>>
//<<<<<< PUBLIC CONSTANTS                                               >>>>>>
//<<<<<< PUBLIC TYPES                                                   >>>>>>

class IgProfLock;
class IgProfPool;

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

    static bool			initialize (int *moduleid,
					    void (*threadinit) (void),
					    bool perthread);

    static void			initThread (void);
    static void			exitThread (bool final);
    static bool			isMultiThreaded (void);

    static bool			enabled (void);
    static bool			enable (void);
    static bool			disable (void);
    static IgProfPool *		pool (int moduleid);

    static void			dump (void);

private:
    friend class IgProfLock;
    static bool			lock (void);
    static void			unlock (void);

    static int			profileRead (int fd, void *buf);
    static void			profileExtend (unsigned char *&buf, unsigned int &size,
					       unsigned int lo, unsigned int hi);
    static void *		profileListenThread (void *);
};

/** Acquire a lock on the profiling system.  Obtains the lock and
    disables the profile data gathering.  Profiling modules making
    use of the #IgProf::pool() method (only) should in general just
    use #IgProf::enable() and #IgProf::disable(); this class should
    be used only for access to the rest of guts of the profilers.  */
class IgProfLock
{
public:
    IgProfLock (void);
    ~IgProfLock (void);

    bool	enabled (void);

private:
    IgProfLock (const IgProfLock &);
    IgProfLock &operator= (const IgProfLock &);

    bool	m_locked;
    bool	m_enabled;
};

//<<<<<< INLINE PUBLIC FUNCTIONS                                        >>>>>>
//<<<<<< INLINE MEMBER FUNCTIONS                                        >>>>>>

/** Return @c true if the profiler system was active at the time the
    lock was acquired.  If this function returns @c false, the caller
    should avoid recording anything in the profiler system.  */
inline bool
IgProfLock::enabled (void)
{ return m_enabled && m_locked; }

#endif // IG_PROF_IG_PROF_H
