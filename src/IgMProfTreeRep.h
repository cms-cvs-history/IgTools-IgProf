#ifndef MEM_PROF_LIB_IG_MPROF_TREE_REP_H
# define MEM_PROF_LIB_IG_MPROF_TREE_REP_H

//<<<<<< INCLUDES                                                       >>>>>>

# include "Ig_Imports/MemProfLib/interface/config.h"
# include "IgMProfTypedefs.h"
# include "IgMProfAllocator.h"
# include <ext/hash_map>

//<<<<<< PUBLIC DEFINES                                                 >>>>>>
//<<<<<< PUBLIC CONSTANTS                                               >>>>>>
//<<<<<< PUBLIC TYPES                                                   >>>>>>

class IgMProfTreeLeafFactory;
class IgMProfTreeLeaf;
class IgMProfSymbolMap;
class IgMProfSymbolFilter;

//<<<<<< PUBLIC VARIABLES                                               >>>>>>
//<<<<<< PUBLIC FUNCTIONS                                               >>>>>>
//<<<<<< CLASS DECLARATIONS                                             >>>>>>

class MEM_PROF_LIB_API IgMProfTreeRep
{
public:
    IgMProfTreeLeaf *m_rootLeaf;

    memAddress_t m_operatorNew;    
    memAddress_t m_operatorNewArray;    
    memAddress_t m_malloc;    
    memAddress_t m_calloc;    
    memAddress_t m_realloc;

    typedef __gnu_cxx::hash_map<memAddress_t, 
	memAddress_t, 
	__gnu_cxx::hash<int>,
	std::equal_to<int>, 
	IgMProfAllocator<memAddress_t> > dladdrMap_t;    

    dladdrMap_t m_dladdrmap;

    IgMProfTreeRep(IgMProfSymbolFilter *filter);
    void addCurrentStacktrace(allocationSize_t count, unsigned int frames);
private:    
    IgMProfTreeLeafFactory *m_leafFactory;
    void *m_backtraceLog[1024];    
    IgMProfSymbolMap *m_symbolMap;    
    IgMProfSymbolFilter *m_filter;    
    bool m_allSymbolsDone;
    bool m_filtering;    
};

//<<<<<< INLINE PUBLIC FUNCTIONS                                        >>>>>>
//<<<<<< INLINE MEMBER FUNCTIONS                                        >>>>>>

#endif // MEM_PROF_LIB_IG_MPROF_TREE_REP_H
