#ifndef IG_PROF_IG_MPROF_TREE_LEAF_H
# define IG_PROF_IG_MPROF_TREE_LEAF_H

//<<<<<< INCLUDES                                                       >>>>>>

# include "Ig_Tools/IgProf/interface/config.h"
# include <ext/hash_map>
# include "IgMProfAllocator.h"
# include "IgMProfTypedefs.h"

//<<<<<< PUBLIC DEFINES                                                 >>>>>>
//<<<<<< PUBLIC CONSTANTS                                               >>>>>>
//<<<<<< PUBLIC TYPES                                                   >>>>>>
//<<<<<< PUBLIC VARIABLES                                               >>>>>>
//<<<<<< PUBLIC FUNCTIONS                                               >>>>>>
//<<<<<< CLASS DECLARATIONS                                             >>>>>>

/** This is a leaf of the callback tree. It keeps the cumulative information about the memory allocated
    under this this branch of the tree.
    All the information relative to children of this node are stored in the m_leafMap map.
 */

class IG_PROF_API IgMProfTreeLeaf
{
public:
    typedef __gnu_cxx::hash_map<int, IgMProfTreeLeaf *, __gnu_cxx::hash<int>,
	std::equal_to<int>, IgMProfAllocator<IgMProfTreeLeaf *> > leafMap_t;    
    typedef leafMap_t::iterator leafIterator_t;    

    /** Cumulative sum of all the allocation performed under this node.
     */
    allocationSize_t 	m_count;
    /** Number of allocation performed*/
    int 		m_num;    
    /** begin() of the iteration on the children nodes*/
    leafIterator_t begin (void) { return m_leafMap.begin (); }
    /** end() of the iteration on children nodes*/
    leafIterator_t end (void) { return m_leafMap.end (); }    
    leafIterator_t find (int what) { return m_leafMap.find (what); }
    void insert (int key, IgMProfTreeLeaf *leaf) { m_leafMap.insert (std::pair<int,IgMProfTreeLeaf*> (key,leaf)); }    
    /**return the number of children nodes*/
    int size (void) { return m_leafMap.size (); }
    /**cumulative memory sum is set to zero at construction of the node*/
    IgMProfTreeLeaf (void)
	: m_count (0),
	  m_num (1)
	{
	}        
private:	
    leafMap_t m_leafMap; 
};


//<<<<<< INLINE PUBLIC FUNCTIONS                                        >>>>>>
//<<<<<< INLINE MEMBER FUNCTIONS                                        >>>>>>

#endif // IG_PROF_IG_MPROF_TREE_LEAF_H
