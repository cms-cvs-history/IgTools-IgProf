//<<<<<< INCLUDES                                                       >>>>>>

#include "IgMProfTypedefs.h"
#include "IgMProfTreeRep.h"
#include "IgMProfTreeLeaf.h"
#include "IgMProfTreeLeafFactory.h"
#include "IgMProfSymbolMap.h"
#include "IgMProfSymbolFilter.h"
#include "Ig_Tools/IgProf/src/IgMProfConfiguration.h"
#include "Ig_Tools/IgProf/src/IgMProfAllocation.h"
#include <dlfcn.h>
#include <execinfo.h>

//<<<<<< PRIVATE DEFINES                                                >>>>>>
//<<<<<< PRIVATE CONSTANTS                                              >>>>>>
//<<<<<< PRIVATE TYPES                                                  >>>>>>
//<<<<<< PRIVATE VARIABLE DEFINITIONS                                   >>>>>>
//<<<<<< PUBLIC VARIABLE DEFINITIONS                                    >>>>>>
//<<<<<< CLASS STRUCTURE INITIALIZATION                                 >>>>>>
//<<<<<< PRIVATE FUNCTION DEFINITIONS                                   >>>>>>
//<<<<<< PUBLIC FUNCTION DEFINITIONS                                    >>>>>>
//<<<<<< MEMBER FUNCTION DEFINITIONS                                    >>>>>>

IgMProfTreeRep::IgMProfTreeRep (IgMProfSymbolFilter *filter)
    :m_rootLeaf (new IgMProfTreeLeaf),
     m_leafFactory (new IgMProfTreeLeafFactory),
     m_symbolMap (new IgMProfSymbolMap),
     m_filter (filter),
     m_allSymbolsDone (false),
     m_filtering (IgMProfConfigurationSingleton::instance ()->m_filtering),
     m_totalSum (IgMProfConfigurationSingleton::instance ()->m_totalSum)
{    
    if (m_filter == 0)
    {
	m_filter = new IgMProfSymbolFilter ();	
    }    
}
    
void 
IgMProfTreeRep::addCurrentStacktrace (allocationSize_t count, 
				      unsigned int frames,
				      memAddress_t allocationPosition)
{
    
    size_t symbolsSize;

    if (m_allSymbolsDone == false)
    {
	m_allSymbolsDone = m_filter->updateFilterMap ();	
    }    

    symbolsSize = backtrace (m_backtraceLog, 1024);

    assert (symbolsSize < 1023);	    
	    
    IgMProfTreeLeaf *currentLeaf = m_rootLeaf;

    for (unsigned int i = symbolsSize-2; i >= frames; i--)
    {
	currentLeaf->m_count += count;
	currentLeaf->m_num++;

	if (m_totalSum)
	    // If we are in totalsum mode, m_maxCount is the
	    // cumulative sum of all the sizes of the allocation
	    // performed.
	    currentLeaf->m_maxCount += count;	
	else if (currentLeaf->m_count > currentLeaf->m_maxCount)
	    // If we are in max live allocation mode, let's keep
	    // m_maxCount updated with the maximal live.
	    currentLeaf->m_maxCount = currentLeaf->m_count;

	memAddress_t address = (memAddress_t) m_backtraceLog[i];
	address = m_symbolMap->closerSymbol (address);
	
	// If filtering is active (m_filtering == true) stop if the
	// symbols belongs to the filtering list.
	if (m_filtering == true && 
	    m_filter->filter (address))
	    break;

	// Insert address into the tree
	IgMProfTreeLeaf::leafIterator_t currentLeafIterator = currentLeaf->find (address);
		       
	if (currentLeafIterator == currentLeaf->end ())
	{
	    // If the address is not found among children, create a
	    // new node in the tree and set its parent to the current
	    // one.
	    IgMProfTreeLeaf *newLeaf = m_leafFactory->create ();
	    currentLeaf->insert (address, newLeaf);
	    newLeaf->setParent (currentLeaf);	    
	    currentLeaf = newLeaf;
	}
	else
	{
	    currentLeaf = currentLeafIterator->second;		   
	}
    }
    
    // Add the last node to the map off end nodes.
    m_endNodeMap[allocationPosition].m_node = currentLeaf;
    m_endNodeMap[allocationPosition].m_size = count;
}

void 
IgMProfTreeRep::removeAllocation (memAddress_t allocation)
{
    IgMProfAllocation &alloc = m_endNodeMap [allocation];
    
    IgMProfTreeLeaf *currentLeaf = alloc.m_node;
    allocationSize_t count = alloc.m_size;
        
    // Go up in the callback tree.
    while (currentLeaf)
    {
	currentLeaf->m_count -= count;
	currentLeaf = currentLeaf->getParent ();	
    }    
    m_endNodeMap.erase (allocation);    
}
