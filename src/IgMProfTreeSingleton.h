#ifndef IG_PROF_IG_MPROF_TREE_SINGLETON_H
# define IG_PROF_IG_MPROF_TREE_SINGLETON_H

//<<<<<< INCLUDES                                                       >>>>>>

# include "Ig_Tools/IgProf/interface/config.h"
# include "Ig_Tools/IgProf/src/IgMProfSymbolFilter.h"
# include "IgMProfTreeRep.h"

//<<<<<< PUBLIC DEFINES                                                 >>>>>>
//<<<<<< PUBLIC CONSTANTS                                               >>>>>>
//<<<<<< PUBLIC TYPES                                                   >>>>>>
//<<<<<< PUBLIC VARIABLES                                               >>>>>>
//<<<<<< PUBLIC FUNCTIONS                                               >>>>>>
//<<<<<< CLASS DECLARATIONS                                             >>>>>>

class IG_PROF_API IgMProfMallocTreeSingleton
{
public:
    static IgMProfTreeRep *instance(void)
	{
	    static IgMProfTreeRep *__instance = 0;
	    if ( __instance == 0)
	    {
		__instance = new IgMProfTreeRep(new IgMProfSymbolFilter ());		
	    }
	    return __instance;	    
	}    
};

class IG_PROF_API IgMProfProfileTreeSingleton
{
public:
    static IgMProfTreeRep *instance(void)
	{
	    static IgMProfTreeRep *__instance = 0;
	    if ( __instance == 0)
	    {
		__instance = new IgMProfTreeRep(new IgMProfSymbolFilter());		
	    }
	    return __instance;	    
	}    
};


//<<<<<< INLINE PUBLIC FUNCTIONS                                        >>>>>>
//<<<<<< INLINE MEMBER FUNCTIONS                                        >>>>>>

#endif // IG_PROF_IG_MPROF_TREE_SINGLETON_H
