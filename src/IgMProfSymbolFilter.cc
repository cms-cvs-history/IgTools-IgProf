//<<<<<< INCLUDES                                                       >>>>>>

#include "IgMProfSymbolFilter.h"
#include "IgMProfConfiguration.h"
#include <link.h>
#include <sstream>

//<<<<<< PRIVATE DEFINES                                                >>>>>>
//<<<<<< PRIVATE CONSTANTS                                              >>>>>>
//<<<<<< PRIVATE TYPES                                                  >>>>>>
//<<<<<< PRIVATE VARIABLE DEFINITIONS                                   >>>>>>
//<<<<<< PUBLIC VARIABLE DEFINITIONS                                    >>>>>>
//<<<<<< CLASS STRUCTURE INITIALIZATION                                 >>>>>>
//<<<<<< PRIVATE FUNCTION DEFINITIONS                                   >>>>>>
//<<<<<< PUBLIC FUNCTION DEFINITIONS                                    >>>>>>
//<<<<<< MEMBER FUNCTION DEFINITIONS                                    >>>>>>

IgMProfSymbolFilter::IgMProfSymbolFilter (void)
    :m_dynamicCount (0),
     m_symbolsToBeExcluded (0)
{
    std::stringstream configBuffer;    
    std::ifstream configFile;
    const char *homepath = getenv ("HOME");
    assert (homepath!=0);    
    std::cerr << std::string (homepath)+"/.memproflibrc" << std::endl;
	    
    configFile.open((std::string (homepath)+"/.memproflibrc").c_str ());

    if (IgMProfConfigurationSingleton::instance ()->m_mallocProfiler == true)
    {
	configBuffer << "exclude _Znaj\n";
	configBuffer << "exclude _Znwj\n";
	configBuffer << "exclude malloc\n";
	configBuffer << "exclude calloc\n";
	configBuffer << "exclude realloc\n";
	configBuffer << std::flush;
    }
    
    if(configFile.is_open ())
    {	
	configBuffer << configFile.rdbuf () << std::flush;
	configBuffer.seekg (0, std::ios_base::beg);
	configFile.close ();
    }

    while(configBuffer.eof () == false)
    {
	std::string key;
	std::string value;
	    
	configBuffer >> key;
	configBuffer >> value;
	    
	if (key == "exclude")
	{
	    m_filtersNotDoneSet[value] = 0;
	    if (IgMProfConfigurationSingleton::instance ()->m_verbose == true)
	    {
		std::cerr << "Configuration asked to mask out symbol " 
			  << value << std::endl;		    
	    }		
	}		
    }

    m_symbolsToBeExcluded = m_filtersNotDoneSet.size ();    
    updateFilterMap ();    
}

bool 
IgMProfSymbolFilter::updateFilterMap (void)
{    
    //Count the number of dynamically loaded objects.
    int j = 0;    
    while (_DYNAMIC[j].d_tag != DT_NULL)
    {
	j++;	
    }    

    //If the number of dynamically loaded object changes, than update
    //of the symbol filters is tried.
    if (j > m_dynamicCount && 
	m_symbolsToBeExcluded > 0)
    {
	m_dynamicCount = j;

	for (std::map<std::string, int>::iterator i = m_filtersNotDoneSet.begin(); 
	     i != m_filtersNotDoneSet.end (); 
	     i++)
	{
	    memAddress_t sym = (memAddress_t) dlsym (RTLD_NEXT, i->first.c_str());
	    	    
	    if (sym != 0)
	    {
		std::cerr << "MemProfLib: Masking out " << i->first 
			  << " @" << std::hex << sym << std::dec << std::endl;
		if (m_filterSet.find (m_filtersNotDoneSet[i->first]) != m_filterSet.end ())
		    m_filterSet.erase (sym);
		else
		{
		    m_symbolsToBeExcluded--;		    
		}
				
		m_filterSet.insert (sym);
		m_filtersNotDoneSet[i->first] = sym;		
	    }			    
	} 	
    }    
    return m_symbolsToBeExcluded <= 0;    
}    
    
bool 
IgMProfSymbolFilter::filter (memAddress_t address)
{
    return (m_filterSet.find (address) != m_filterSet.end ());
}    
