#ifndef IG_PROF_IG_MPROF_TREE_LEAF_FACTORY_H
# define IG_PROF_IG_MPROF_TREE_LEAF_FACTORY_H

//<<<<<< INCLUDES                                                       >>>>>>

# include "Ig_Tools/IgProf/interface/config.h"

//<<<<<< PUBLIC DEFINES                                                 >>>>>>
//<<<<<< PUBLIC CONSTANTS                                               >>>>>>
//<<<<<< PUBLIC TYPES                                                   >>>>>>
class IgMProfTreeLeaf;

//<<<<<< PUBLIC VARIABLES                                               >>>>>>
//<<<<<< PUBLIC FUNCTIONS                                               >>>>>>
//<<<<<< CLASS DECLARATIONS                                             >>>>>>

class IG_PROF_API IgMProfTreeLeafFactory
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

#endif // IG_PROF_IG_MPROF_TREE_LEAF_FACTORY_H
