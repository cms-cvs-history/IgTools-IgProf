#ifndef IG_PROF_IG_MPROF_TREE_TEXT_BROWSER_H
# define IG_PROF_IG_MPROF_TREE_TEXT_BROWSER_H

//<<<<<< INCLUDES                                                       >>>>>>

# include "Ig_Tools/IgProf/interface/config.h"
# include <map>
# include "IgMProfAllocator.h"
# include "IgMProfStacktraceFlatNode.h"
# include "IgMProfConfiguration.h"
# include <fstream>
# include <string>

//<<<<<< PUBLIC DEFINES                                                 >>>>>>
//<<<<<< PUBLIC CONSTANTS                                               >>>>>>
//<<<<<< PUBLIC TYPES                                                   >>>>>>
class IgMProfTreeRep;
class IgMProfTreeLeaf;

//<<<<<< PUBLIC VARIABLES                                               >>>>>>
//<<<<<< PUBLIC FUNCTIONS                                               >>>>>>
//<<<<<< CLASS DECLARATIONS                                             >>>>>>

class IG_PROF_API IgMProfTreeTextBrowser
{
private:
    typedef std::map<int,IgMProfFlatNode *, std::less<int>, IgMProfAllocator<IgMProfFlatNode *> > FlatMap;
    IgMProfTreeRep	*m_representable;
    FlatMap		m_flat;
    std::ofstream	m_treeout;
    std::ofstream	m_flatout;
    std::string 	m_treeFilename;
    std::string 	m_flatFilename;
    std::string 	m_filename;    
public:
    
    IgMProfTreeTextBrowser (IgMProfTreeRep *representable, const char *filename);
    void dumpTreeLeaf (IgMProfTreeLeaf *leaf, int level, int caller);
    
    struct SortFlatByAllocs 
    {
	FlatMap *m_map;
	bool 	m_bySelfSorting;
	bool 	m_reverse;	

	SortFlatByAllocs (FlatMap *m) 
	    :m_map (m),
	    m_bySelfSorting (IgMProfConfigurationSingleton::instance ()->m_bySelfSorting),
	    m_reverse (IgMProfConfigurationSingleton::instance ()->m_reverse)
	    {		
	    }

	bool operator() (int a1, int a2) 
	    {
		IgMProfFlatNode *a1node = (*m_map)[a1];
		IgMProfFlatNode *a2node = (*m_map)[a2];
	    
		//Sort first by accumulated allocs and then by self allocation.
		long long result = a1node->m_allocs - a2node->m_allocs;
	    
		if((result == 0) || m_bySelfSorting)
		{		
		    result = (a1node->m_allocs - a1node->m_childrenAllocs)
			     - (a2node->m_allocs - a2node->m_childrenAllocs);
		    if((result == 0) && m_bySelfSorting)
		    {
			result = a1node->m_allocs - a2node->m_allocs;
		    }		
		}
		return m_reverse ^ (result > 0);	    
	    }	
    };    

    void dumpFlatProfile (void);
    void dump (void);
};

//<<<<<< INLINE PUBLIC FUNCTIONS                                        >>>>>>
//<<<<<< INLINE MEMBER FUNCTIONS                                        >>>>>>

#endif // IG_PROF_IG_MPROF_TREE_TEXT_BROWSER_H
