#ifndef MEM_PROF_LIB_IG_MPROF_TREE_LEAF_FACTORY_H
# define MEM_PROF_LIB_IG_MPROF_TREE_LEAF_FACTORY_H

//<<<<<< INCLUDES                                                       >>>>>>

# include "Ig_Imports/MemProfLib/interface/config.h"

//<<<<<< PUBLIC DEFINES                                                 >>>>>>
//<<<<<< PUBLIC CONSTANTS                                               >>>>>>
//<<<<<< PUBLIC TYPES                                                   >>>>>>
class IgMProfTreeLeaf;

//<<<<<< PUBLIC VARIABLES                                               >>>>>>
//<<<<<< PUBLIC FUNCTIONS                                               >>>>>>
//<<<<<< CLASS DECLARATIONS                                             >>>>>>

class MEM_PROF_LIB_API IgMProfTreeLeafFactory
{
private:
    int m_allocatedLeafCount;
    IgMProfTreeLeaf *m_leafPool;
    int m_poolSize;    
public:    
    IgMProfTreeLeafFactory();
    IgMProfTreeLeaf *create(void);
};


//<<<<<< INLINE PUBLIC FUNCTIONS                                        >>>>>>
//<<<<<< INLINE MEMBER FUNCTIONS                                        >>>>>>

#endif // MEM_PROF_LIB_IG_MPROF_TREE_LEAF_FACTORY_H
