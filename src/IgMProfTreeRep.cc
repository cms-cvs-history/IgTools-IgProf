//<<<<<< INCLUDES                                                       >>>>>>

#include "IgMProfTypedefs.h"
#include "IgMProfTreeRep.h"
#include "IgMProfTreeLeaf.h"
#include "IgMProfTreeLeafFactory.h"
#include "IgMProfSymbolMap.h"
#include "IgMProfSymbolFilter.h"
#include "Ig_Tools/IgProf/src/IgMProfConfiguration.h"
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
     m_filtering (IgMProfConfigurationSingleton::instance ()->m_filtering)
{    
    if (m_filter == 0)
    {
	m_filter = new IgMProfSymbolFilter ();	
    }    
}
    
void 
IgMProfTreeRep::addCurrentStacktrace (allocationSize_t count, 
				      unsigned int frames)
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
	
	memAddress_t address = (memAddress_t) m_backtraceLog[i];
	address = m_symbolMap->closerSymbol (address);
	
	//If filtering is active (m_filtering == true) stop if the
	//symbols belongs to the filtering list. 

	// FIXME: there is some strange behavior of the first entry in
	//the stacktrace: sometimes dlsym maps it to _Znwj (new
	//operator) which is then filtered if the filtering is
	//active. Now I've patched the code so that it is never
	//filtered for such an entry, would be nice to understand why
	//it fails to be mapped correctly, though.
	if (m_filtering == true && 
	    m_filter->filter (address))
	    break;

	// Insert address into the tree
	IgMProfTreeLeaf::leafIterator_t currentLeafIterator = currentLeaf->find (address);
		
	if (currentLeafIterator == currentLeaf->end ())
	{
	    IgMProfTreeLeaf *newLeaf = m_leafFactory->create ();
	    currentLeaf->insert (address, newLeaf);
	    currentLeaf = newLeaf;
	}
	else
	{
	    currentLeaf = currentLeafIterator->second;		   
	}
    }
}
