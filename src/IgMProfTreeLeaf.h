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

/** This is a leaf of the callback tree.  According to different
    values of the totalsum env variable (-ts option with igprof) it
    keeps the cumulative information about the memory allocated under
    this this branch of the tree or the total max heap size for it.
    All the information relative to children of this node are stored
    in the m_leafMap map.
 */

class IG_PROF_API IgMProfTreeLeaf
{
public:
    typedef __gnu_cxx::hash_map<int, IgMProfTreeLeaf *, __gnu_cxx::hash<int>,
	std::equal_to<int>, IgMProfAllocator<IgMProfTreeLeaf *> > leafMap_t;    
    typedef leafMap_t::iterator leafIterator_t;    

    /** Current allocation under this node. In the end, this results
     * in the possible leaked memory.*/
    allocationSize_t 	m_count;
    /** Max live heap size for this function and its children or the
     * cumulative sum of all the allocations, according wether or not
     * totalsum has been specified in MPROF env variable. */
    allocationSize_t	m_maxCount;    
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
	  m_maxCount (0),
	  m_num (1),
	  m_parent (0)
	{
	}        
    
    /**@return the parent for this node*/
    IgMProfTreeLeaf *getParent (void) {return m_parent;}
    /**Sets the parent of this node*/
    void setParent (IgMProfTreeLeaf *parent) {m_parent = parent;}    
private:	
    leafMap_t m_leafMap; 
    
    IgMProfTreeLeaf *m_parent;    
};


//<<<<<< INLINE PUBLIC FUNCTIONS                                        >>>>>>
//<<<<<< INLINE MEMBER FUNCTIONS                                        >>>>>>

#endif // IG_PROF_IG_MPROF_TREE_LEAF_H
