#ifndef IG_PROF_IG_PROF_H
# define IG_PROF_IG_PROF_H

//<<<<<< INCLUDES                                                       >>>>>>

# include "Ig_Tools/IgHook/interface/IgHook.h"

//<<<<<< PUBLIC DEFINES                                                 >>>>>>

#define IGPROF_HOOK(type, fun, myfun) \
    static IgHook::TypedData<type> myfun##_hook = { { 0, #fun, 0, &myfun, 0, 0, 0 } }

#define IGPROF_LIBHOOK(type, fun, lib, myfun) \
    static IgHook::TypedData<type> myfun##_hook = { { 0, #fun, lib, &myfun, 0, 0, 0 } }

//<<<<<< PUBLIC CONSTANTS                                               >>>>>>
//<<<<<< PUBLIC TYPES                                                   >>>>>>

class IgHookTrace;
class IgHookLiveMap;

//<<<<<< PUBLIC VARIABLES                                               >>>>>>
//<<<<<< PUBLIC FUNCTIONS                                               >>>>>>
//<<<<<< CLASS DECLARATIONS                                             >>>>>>

class IgProf
{
public:
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

private:
    friend class IgProfLock;
    static void			lock (void);
    static void			unlock (void);

    static void			enable (void);
    static void			disable (void);
};

class IgProfLock
{
public:
    IgProfLock (int &enabled);
    ~IgProfLock (void);

    int		enabled (void);

private:
    IgProfLock (const IgProfLock &);
    IgProfLock &operator= (const IgProfLock &);

    int		m_enabled;
};

//<<<<<< INLINE PUBLIC FUNCTIONS                                        >>>>>>
//<<<<<< INLINE MEMBER FUNCTIONS                                        >>>>>>

inline int
IgProfLock::enabled (void)
{ return m_enabled; }

#endif // IG_PROF_IG_PROF_H
