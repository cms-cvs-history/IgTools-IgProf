//<<<<<< INCLUDES                                                       >>>>>>

#include "Ig_Imports/MemProfLib/src/IgMProfLinearBrowser.h"
#include "Ig_Imports/MemProfLib/src/IgMProfLinearRep.h"
#include "Ig_Imports/MemProfLib/src/IgMProfMallocHooks.h"
#include "Ig_Imports/MemProfLib/src/IgMProfTypedefs.h"
#include <execinfo.h>
#include <dlfcn.h>
#ifdef __GLIBC__
# include <malloc.h>
#endif
#include <sys/types.h>
#include <unistd.h>
#include <iostream>
#include <sstream>
#include <map>

//<<<<<< PRIVATE DEFINES                                                >>>>>>
//<<<<<< PRIVATE CONSTANTS                                              >>>>>>
//<<<<<< PRIVATE TYPES                                                  >>>>>>
//<<<<<< PRIVATE VARIABLE DEFINITIONS                                   >>>>>>
//<<<<<< PUBLIC VARIABLE DEFINITIONS                                    >>>>>>
//<<<<<< CLASS STRUCTURE INITIALIZATION                                 >>>>>>
//<<<<<< PRIVATE FUNCTION DEFINITIONS                                   >>>>>>
//<<<<<< PUBLIC FUNCTION DEFINITIONS                                    >>>>>>
//<<<<<< MEMBER FUNCTION DEFINITIONS                                    >>>>>>

IgMProfLinearBrowser::IgMProfLinearBrowser (IgMProfLinearRep *rep, const char *filename)
    :m_rep (rep),
     m_filename (filename),
     m_leaksOut ()
{
    std::ostringstream converter;
    converter << getpid ();
    m_filename += converter.str () + ".leaks";    
}

void IgMProfLinearBrowser::dump (void)
{
    __malloc_hook = old_malloc_hook; 
    __realloc_hook = old_realloc_hook;
    __free_hook = old_free_hook;
    __memalign_hook = old_memalign_hook;

    std::cerr << "MemProfLib: Dumping leaks informations in file "
	      << m_filename << "...";    

    m_leaksOut.open (m_filename.c_str ());
    
    m_leaksOut << "-------------------------------------------------\n";
    m_leaksOut << "Memory debugger final report\n";
    m_leaksOut << "-------------------------------------------------\n";
    
    m_leaksOut << "Maximal live heap size: " 
	       << m_rep->maxLiveHeapSize () << " bytes\n";
    m_leaksOut << "Number of possible leaks: " 
	       << m_rep->numberOfLeaks () << "\n";    
    m_leaksOut << "Amount of memory lost in possible leaks: " 
	       << m_rep->totalLeaksSize () << "\n";    

    IgMProfLinearRep::AllocationsMap &allocationMap = m_rep->m_allocationsMap;

    std::map<memAddress_t, int> resultMap;
    
    for ( IgMProfLinearRep::AllocationsMap::iterator i = allocationMap.begin ();
	  i != allocationMap.end ();
	  i++)
    {
	Dl_info info;

	bool done = false;
	
	for (int j = 0; 
	     j < 4 && done == false; 
	     j++)
	{
	    if (dladdr ((void *) i->second->m_backtraceLog[j], &info) && 
		info.dli_fname && 
		info.dli_fname[0] && 
		info.dli_saddr)
	    {
		const char *symname = info.dli_sname && info.dli_sname[0] ? info.dli_sname : "<?>";
		
		if ((std::string (symname) != "_Znaj") &&
		    (std::string (symname) != "_Znwj") &&
		    (std::string (symname) != "_ZNSt24__default_alloc_templateILb1ELi0EE14_S_chunk_allocEjRi") &&
		    (std::string (symname) != "_ZNSt24__default_alloc_templateILb1ELi0EE9_S_refillEj") &&
		    (std::string (symname) != "_ZNSt24__default_alloc_templateILb1ELi0EE8allocateEj"))
		{
		    done = true;		    
		    memAddress_t symaddr = (memAddress_t) info.dli_saddr;
		    resultMap[symaddr] += i->second->m_size;
		}
	    }
	}
    }

    std::vector<memAddress_t> order;

    for (std::map<memAddress_t, int>::iterator i = resultMap.begin ();
	 i != resultMap.end (); 
	 i++)
    {
	order.push_back (i->first);	
    }    

    std::sort (order.begin (), order.end (), SortByAllocs (&resultMap));    

    for (std::vector<memAddress_t>::iterator i = order.begin ();
	 i != order.end ();
	 i++)
    {
	const char *symname;
	const char *libname;
	
	Dl_info info;
	if  (dladdr ((void *) *i, &info) && 
	     info.dli_fname && 
	     info.dli_fname[0] && 
	     info.dli_saddr)
	{
	    symname = (info.dli_sname && info.dli_sname[0] ? info.dli_sname : "<?>");
	    libname = info.dli_fname;
	}	

	m_leaksOut << "---------------------------------------------" 
		   << std::endl;
	m_leaksOut << resultMap[*i] << " bytes allocated by " 
		   << symname << " ("<< libname << ")" 
		   << " never freed" << std::endl;	
    }    

    std::cerr << "Done" << std::endl;    

    __malloc_hook = IGUANA_memdebug_malloc_hook;    
    __realloc_hook = IGUANA_memdebug_realloc_hook;
    __memalign_hook = IGUANA_memdebug_memalign_hook;    
    __free_hook = IGUANA_memdebug_free_hook;
}

