#ifndef IG_PROF_IG_PROF_H
# define IG_PROF_IG_PROF_H

//<<<<<< INCLUDES                                                       >>>>>>

# include "Ig_Tools/IgHook/interface/IgHook.h"

//<<<<<< PUBLIC DEFINES                                                 >>>>>>

#define IGPROF_HOOK(type, fun, myfun) \
    static IgHook::TypedData<type> myfun##_hook = { { 0, #fun, 0, &myfun, 0, 0, 0 } }

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
    static void			dump (void);

    static void			initialize (void);
    static void			enable (void);
    static void			disable (void);
    static void			onexit (void (*func) (void));
    static void			runexit (void);
};

//<<<<<< INLINE PUBLIC FUNCTIONS                                        >>>>>>
//<<<<<< INLINE MEMBER FUNCTIONS                                        >>>>>>

#endif // IG_PROF_IG_PROF_H
