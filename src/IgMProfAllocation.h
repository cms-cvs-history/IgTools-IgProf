#ifndef IG_PROF_IG_MPROF_ALLOCATION_H
# define IG_PROF_IG_MPROF_ALLOCATION_H

#include "Ig_Tools/IgProf/src/IgMProfTypedefs.h"

//<<<<<< INCLUDES                                                       >>>>>>
//<<<<<< PUBLIC DEFINES                                                 >>>>>>
//<<<<<< PUBLIC CONSTANTS                                               >>>>>>
//<<<<<< PUBLIC TYPES                                                   >>>>>>
class IgMProfTreeLeaf;

//<<<<<< PUBLIC VARIABLES                                               >>>>>>
//<<<<<< PUBLIC FUNCTIONS                                               >>>>>>
//<<<<<< CLASS DECLARATIONS                                             >>>>>>

struct IgMProfAllocation
{
    IgMProfTreeLeaf 	*m_node;
    allocationSize_t	m_size;    
};

//<<<<<< INLINE PUBLIC FUNCTIONS                                        >>>>>>
//<<<<<< INLINE MEMBER FUNCTIONS                                        >>>>>>

#endif // IG_PROF_IG_MPROF_ALLOCATION_H
