#ifndef MEM_PROF_LIB_IG_MPROF_TREE_SINGLETON_H
# define MEM_PROF_LIB_IG_MPROF_TREE_SINGLETON_H

//<<<<<< INCLUDES                                                       >>>>>>

# include "Ig_Imports/MemProfLib/interface/config.h"
# include "Ig_Imports/MemProfLib/src/IgMProfSymbolFilter.h"
# include "IgMProfTreeRep.h"

//<<<<<< PUBLIC DEFINES                                                 >>>>>>
//<<<<<< PUBLIC CONSTANTS                                               >>>>>>
//<<<<<< PUBLIC TYPES                                                   >>>>>>
//<<<<<< PUBLIC VARIABLES                                               >>>>>>
//<<<<<< PUBLIC FUNCTIONS                                               >>>>>>
//<<<<<< CLASS DECLARATIONS                                             >>>>>>

class MEM_PROF_LIB_API IgMProfMallocTreeSingleton
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

class MEM_PROF_LIB_API IgMProfProfileTreeSingleton
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

#endif // MEM_PROF_LIB_IG_MPROF_TREE_SINGLETON_H
