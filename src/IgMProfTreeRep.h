#ifndef IG_PROF_IG_MPROF_TREE_REP_H
# define IG_PROF_IG_MPROF_TREE_REP_H

//<<<<<< INCLUDES                                                       >>>>>>

# include "Ig_Tools/IgProf/interface/config.h"
# include "IgMProfTypedefs.h"
# include "IgMProfAllocator.h"
# include <ext/hash_map>
# include <map>

//<<<<<< PUBLIC DEFINES                                                 >>>>>>
//<<<<<< PUBLIC CONSTANTS                                               >>>>>>
//<<<<<< PUBLIC TYPES                                                   >>>>>>
class IgMProfTreeLeafFactory;
class IgMProfTreeLeaf;
class IgMProfSymbolMap;
class IgMProfSymbolFilter;
class IgMProfAllocation;

//<<<<<< PUBLIC VARIABLES                                               >>>>>>
//<<<<<< PUBLIC FUNCTIONS                                               >>>>>>
//<<<<<< CLASS DECLARATIONS                                             >>>>>>

class IG_PROF_API IgMProfTreeRep
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

    IgMProfTreeRep (IgMProfSymbolFilter *filter);
    void addCurrentStacktrace (allocationSize_t count, 
			       unsigned int frames,
			       memAddress_t allocationPosition);
    
    /** Removes an allocation from the tree, by looking up the last
	node in the endNodeTable which did the allocation and walking
	the three backward
    */
    void removeAllocation (memAddress_t allocation);
    
private:    
    typedef std::map<memAddress_t,
	IgMProfAllocation,
	std::less<memAddress_t>, 
	IgMProfAllocator<IgMProfAllocation> > EndNodeMap;    

    IgMProfTreeLeafFactory 	*m_leafFactory;
    void 			*m_backtraceLog[1024];    
    IgMProfSymbolMap 		*m_symbolMap;    
    IgMProfSymbolFilter 	*m_filter;    
    bool 			m_allSymbolsDone;
    bool 			m_filtering;    
    bool			m_totalSum;    
    EndNodeMap 			m_endNodeMap;    
};

//<<<<<< INLINE PUBLIC FUNCTIONS                                        >>>>>>
//<<<<<< INLINE MEMBER FUNCTIONS                                        >>>>>>

#endif // IG_PROF_IG_MPROF_TREE_REP_H
