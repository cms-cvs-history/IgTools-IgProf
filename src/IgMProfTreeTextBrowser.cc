//<<<<<< INCLUDES                                                       >>>>>>

#include "IgMProfTreeTextBrowser.h"
#include "IgMProfConfiguration.h"
#include "IgMProfTreeRep.h"
#include "IgMProfTreeLeaf.h"
#include "IgMProfMallocHooks.h"
#include "IgMProfTypedefs.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <dlfcn.h>
#include <stdlib.h>
#ifdef __GLIBC__
# include <malloc.h>
#endif
#include <sys/types.h>
#include <unistd.h>

//<<<<<< PRIVATE DEFINES                                                >>>>>>
//<<<<<< PRIVATE CONSTANTS                                              >>>>>>
//<<<<<< PRIVATE TYPES                                                  >>>>>>
//<<<<<< PRIVATE VARIABLE DEFINITIONS                                   >>>>>>
//<<<<<< PUBLIC VARIABLE DEFINITIONS                                    >>>>>>
//<<<<<< CLASS STRUCTURE INITIALIZATION                                 >>>>>>
//<<<<<< PRIVATE FUNCTION DEFINITIONS                                   >>>>>>
//<<<<<< PUBLIC FUNCTION DEFINITIONS                                    >>>>>>
//<<<<<< MEMBER FUNCTION DEFINITIONS                                    >>>>>>

IgMProfTreeTextBrowser::IgMProfTreeTextBrowser( IgMProfTreeRep *representable, const char * filename, bool dumpMax)
    :m_representable (representable),
     m_treeout (),
     m_flatout (),
     m_filename (filename),
     m_dumpMax (dumpMax),
     m_density (IgMProfConfigurationSingleton::instance ()->m_density)
{
    std::ostringstream converter;
    converter << getpid ();
    m_treeFilename = m_filename+converter.str () + ".tree";
    m_flatFilename = m_filename+converter.str () + ".flat";
}

void 
IgMProfTreeTextBrowser::dumpTreeLeaf (IgMProfTreeLeaf *leaf, int level, int caller) 
{    
    for (IgMProfTreeLeaf::leafIterator_t i = leaf->begin () ; i != leaf->end () ; i++)
    {
	// Dump out the tree part
	IgMProfTreeLeaf &currentLeaf = *(i->second);
       
	int divider = m_density ? currentLeaf.m_num : 1;

	memAddress_t address = i->first;
	memAddress_t symaddr = address;
	const char *libname = "<?>";
	const char *symname = "<?>";
	Dl_info info;

	m_treeout << std::string ((size_t) level, ' ');
	if (dladdr ((void *) address, &info) && info.dli_fname && info.dli_fname[0] && info.dli_saddr)
	{
	    libname = info.dli_fname;
	    symname = (info.dli_sname && info.dli_sname[0] ? info.dli_sname : "<?>");
	    symaddr = (memAddress_t) info.dli_saddr;
	}

	if(IgMProfConfigurationSingleton::instance()->m_treeOutput)
	{		
	    m_treeout << std::hex << address << "/" << symaddr << " " << libname << " "  << symname
		      << "+" << (address-symaddr) << " "
		      << std::dec << currentLeaf.m_maxCount/divider << std::endl;	   
	}
	

	// Dump the m_maxCount or m_count field of the node according
	// to m_dumpMaxCount (this is done because leak information
	// are in m_count, whereas allocation information are in
	// m_maxCount (both max live allocation and allocation sum).
	if (m_dumpMax)	  
	    m_flat [caller]->addChild (symaddr, currentLeaf.m_maxCount/divider);
	else
	    m_flat [caller]->addChild (symaddr, currentLeaf.m_count/divider);

	IgMProfFlatNode *&flat = m_flat[symaddr];
	if (! flat)
	    flat = new IgMProfFlatNode (libname, symname);
		
	flat->addCaller (caller);

	if (m_dumpMax)	  
	    flat->m_allocs += currentLeaf.m_maxCount/divider;
	else
	    flat->m_allocs += currentLeaf.m_count/divider;

	flat->m_calls++;

	// Recurse
	if ( currentLeaf.size() != 0  )
	    dumpTreeLeaf (i->second, level+1, symaddr);		
    }
}

void
IgMProfTreeTextBrowser::dumpFlatProfile (void)
{
    // Count how much was allocated in the children of each node
    for (FlatMap::iterator i = m_flat.begin (); i != m_flat.end (); ++i)
	for (std::map<int,std::pair<int,allocationSize_t> >::iterator c = i->second->m_children.begin (); c != i->second->m_children.end (); ++c)
	    i->second->m_childrenAllocs += c->second.second;


    // Sort flat profile by most allocated to least
    std::vector<int> order;
    for (FlatMap::iterator i = m_flat.begin (); i != m_flat.end (); ++i)
	order.push_back (i->first);
    std::sort (order.begin (), order.end (), SortFlatByAllocs (&m_flat));

    // Print out the flat profile
    m_flatout << "\n\nFlat profile:\n";
    for (std::vector<int>::iterator i = order.begin (); i != order.end (); ++i)
    {
	m_flatout << std::string (60, '-') << "\n";
	IgMProfFlatNode *n = m_flat [*i];

	// Dump callers
	for (std::set<int>::iterator c = n->m_callers.begin (); c != n->m_callers.end (); ++c)
	{
	    IgMProfFlatNode *caller = m_flat [*c];
	    std::map<int,std::pair<int,allocationSize_t> >::iterator thiscall
		= caller->m_children.find (*i);

	    m_flatout << ""
		      << "\t" << std::setw (15) << std::setfill (' ') << caller->m_allocs
		      << "\t" << std::setw (15) << std::setfill (' ') << thiscall->second.second
		      << "\t" << thiscall->second.first
		      << "/" << n->m_calls
		      << "\t\t  " << caller->m_symname
		      << " (" << caller->m_libname
		      << ") [" << (std::find (order.begin (), order.end (), *c) - order.begin ()) << "]\n";
	}
	// Dump info on this item
	m_flatout << "[" << (i - order.begin ()) << "]"
		  << "\t" << std::setw (15) << std::setfill (' ') << n->m_allocs
		  << "\t" << std::setw (15) << std::setfill (' ') << (long long) (n->m_allocs-n->m_childrenAllocs)
		  << "\t" << n->m_calls
		  << "\t\t" << n->m_symname
		  << " (" << n->m_libname << ")\n";
		
	// Dump children
	for (std::map<int,std::pair<int,allocationSize_t> >::iterator c = n->m_children.begin (); c != n->m_children.end (); ++c)
	{
	    IgMProfFlatNode *callee = m_flat [c->first];
	    m_flatout << ""
		      << "\t" << std::setw (15) << std::setfill (' ') << callee->m_allocs
		      << "\t" << std::setw (15) << std::setfill (' ') << c->second.second
		      << "\t" << c->second.first
		      << "/" << callee->m_calls
		      << "\t\t  " << callee->m_symname
		      << " (" << callee->m_libname
		      << ") [" << (std::find (order.begin (), order.end (), c->first) - order.begin ()) << "]\n";
	}
    }	
}

void
IgMProfTreeTextBrowser::dump(void)
{	    
    std::ostringstream converter;
    converter << getpid ();
    m_treeFilename = m_filename+converter.str () + ".tree";
    m_flatFilename = m_filename+converter.str () + ".flat";

    m_flat [0] = new IgMProfFlatNode ("<system>", "<spontaneous>");

    if(IgMProfConfigurationSingleton::instance()->m_treeOutput)
    {		
	std::cerr << "MemProfLib: Dumping tree information into: " 
		  << m_treeFilename 
		  << "...Please wait, this might take a while...";	    
	m_treeout.open(m_treeFilename.c_str ());		
    }
    
    dumpTreeLeaf(m_representable->m_rootLeaf,0,0);
    
    if(IgMProfConfigurationSingleton::instance()->m_treeOutput)
    {
	std::cerr << "Done" << std::endl;
	m_treeout.close();		
    }

    if(IgMProfConfigurationSingleton::instance()->m_flatOutput)
    {		
	std::cerr << "MemProfLib: Dumping flat information into: " 
		  << m_flatFilename 
		  << "...Please wait, this might take a while...";	    
	m_flatout.open (m_flatFilename.c_str() );	    
	dumpFlatProfile ();
	m_flatout.close();		
	std::cerr << "Done" << std::endl;	    
    }
}
