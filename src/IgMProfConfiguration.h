#ifndef MEM_PROF_LIB_IG_MPROF_CONFIGURATION_H
# define MEM_PROF_LIB_IG_MPROF_CONFIGURATION_H

//<<<<<< INCLUDES                                                       >>>>>>

# include "Ig_Imports/MemProfLib/interface/config.h"
# include <string>
//<<<<<< PUBLIC DEFINES                                                 >>>>>>
//<<<<<< PUBLIC CONSTANTS                                               >>>>>>
//<<<<<< PUBLIC TYPES                                                   >>>>>>
//<<<<<< PUBLIC VARIABLES                                               >>>>>>
//<<<<<< PUBLIC FUNCTIONS                                               >>>>>>
//<<<<<< CLASS DECLARATIONS                                             >>>>>>

/*! This class helds configuration of the profiler. You can modify these by setting
 *  the proper $MEMPROF option.
 *  	- Possible $MEMPROF options are:
 *    	- tree: output the complete calltree informations.
 *    	- noflat: do not output the flat profiling information.
 *    	- profilespeed: profiles speed rather than memory allocations.
 *    	- byselfsorting: sorts the flat profile entries by self allocations/time first rather then cumulative one.
 *    	- density: shows the mean amount of memory allocated. Useful to understand whether to use a pool allocator or not.
 *    	- verbose: show debug informations.
 *    	- leaks: enables the possible leaks checker.
 *	- nofiltering: does not filter any symbol from the output.
 */
class MEM_PROF_LIB_API IgMProfConfiguration
{
public:
    IgMProfConfiguration (void)
	:m_treeOutput (false),
	 m_flatOutput (true),
	 m_mallocProfiler (true),
	 m_bySelfSorting (false),
	 m_density (false),
	 m_verbose (false),
	 m_reverse (false),
	 m_checkLeaks (false),
	 m_filtering (true)
	{
	    char *env = getenv("MEMPROF");
	    if (env != NULL)
	    {		
		std::string m_memprofEnv = std::string (" ")+env+" ";

		if (m_memprofEnv.find ("noflat") != std::string::npos) 
		    m_flatOutput = false;

		if (m_memprofEnv.find ("tree") != std::string::npos) 
		    m_treeOutput = true;

		if (m_memprofEnv.find ("profilespeed") != std::string::npos) 
		    m_mallocProfiler = false;			

		if (m_memprofEnv.find ("byselfsorting") != std::string::npos) 
		    m_bySelfSorting = true;

		if (m_memprofEnv.find ("density") != std::string::npos) 
		    m_density = true;

		if (m_memprofEnv.find ("verbose") != std::string::npos) 
		    m_verbose = true;		

		if (m_memprofEnv.find ("reverse") != std::string::npos) 
		    m_reverse = true;		

		if (m_memprofEnv.find ("leaks") != std::string::npos) 
		    m_checkLeaks = true;		

		if (m_memprofEnv.find ("nofiltering") != std::string::npos) 
		    m_filtering = false;		
	    }	    
	}        

    bool m_treeOutput;
    bool m_flatOutput;
    bool m_mallocProfiler;
    bool m_bySelfSorting;    
    bool m_density;    
    bool m_verbose;
    bool m_reverse;    
    bool m_checkLeaks;
    bool m_filtering;    
    
    // implicit copy constructor
    // implicit assignment operator
    // implicit destructor
};

class MEM_PROF_LIB_API IgMProfConfigurationSingleton : private IgMProfConfiguration
{
private:
    IgMProfConfigurationSingleton (void)
	{
	}    
public:
    static IgMProfConfiguration *instance (void)
	{
	    static IgMProfConfiguration* __instance = 0;
	    if (__instance == 0)
	    {
		__instance = new IgMProfConfiguration ();		
	    }
	    return __instance;	    
	}    
};

//<<<<<< INLINE PUBLIC FUNCTIONS                                        >>>>>>
//<<<<<< INLINE MEMBER FUNCTIONS                                        >>>>>>

#endif // MEM_PROF_LIB_IG_MPROF_CONFIGURATION_H
