//<<<<<< INCLUDES                                                       >>>>>>
#include "IgMProfBootstrap.h"
#include "IgMProfTreeTextBrowser.h"
#include "IgMProfLinearBrowser.h"
#include "IgMProfMallocHooks.h"
#include "IgMProfTreeSingleton.h"
#include "IgMProfLinearRep.h"
#include "IgSProfTimerHooks.h"
#include "IgMProfConfiguration.h"

#include <signal.h>

//<<<<<< PRIVATE DEFINES                                                >>>>>>
//<<<<<< PRIVATE CONSTANTS                                              >>>>>>
//<<<<<< PRIVATE TYPES                                                  >>>>>>
//<<<<<< PRIVATE VARIABLE DEFINITIONS                                   >>>>>>
//<<<<<< PUBLIC VARIABLE DEFINITIONS                                    >>>>>>
//<<<<<< CLASS STRUCTURE INITIALIZATION                                 >>>>>>
//<<<<<< PRIVATE FUNCTION DEFINITIONS                                   >>>>>>
//<<<<<< PUBLIC FUNCTION DEFINITIONS                                    >>>>>>
//<<<<<< MEMBER FUNCTION DEFINITIONS                                    >>>>>>

/** An instance of this class is statically allocated in the library so that all the code 
    in the constructor is executed at load time. 
 */

IgMProfBootstrap::IgMProfBootstrap (void)
{
    if (IgMProfConfigurationSingleton::instance ()->m_mallocProfiler == true )
    {	
	m_mallocBrowser = new IgMProfTreeTextBrowser (IgMProfMallocTreeSingleton::instance (), 
						      "allocation",
						      true);    
	if (IgMProfConfigurationSingleton::instance ()->m_checkLeaks)
	    m_leaksBrowser = new IgMProfTreeTextBrowser (IgMProfMallocTreeSingleton::instance (),
							 "leaks",
							 false);	
        IGUANA_memdebug_initialize_hook ();
    }
    else 
    {
	m_profileBrowser = new IgMProfTreeTextBrowser (IgMProfProfileTreeSingleton::instance (), 
						       "performance", 
						       true);    
	IGUANA_sprof_initialize_hook ();
    }
    m_oldAbortSigHandler = signal (SIGABRT, 
				   (sighandler_t) IgMProfBootstrap::abortSigHandler);
}

IgMProfBootstrap::~IgMProfBootstrap (void)
{
    IgMProfBootstrap::dumpStatus ();    
}   

void
IgMProfBootstrap::dumpStatus (void)
{
    IGUANA_memdebug_disable_hooks ();    

    if (m_mallocBrowser) 
    {
	m_mallocBrowser->dump();
	delete m_mallocBrowser;	    
    }
    
    if (m_leaksBrowser)
    {
	m_leaksBrowser->dump ();
	delete m_profileBrowser;	    
    }
    
    if (m_profileBrowser)   
    {
	IGUANA_sprof_dispose_hook ();	
	m_profileBrowser->dump ();    	
	delete m_profileBrowser;    
    }

    IGUANA_memdebug_enable_hooks ();
}

 
void
IgMProfBootstrap::abortSigHandler (int t)
{
    IgMProfBootstrap::dumpStatus ();
    m_oldAbortSigHandler (t);    
}

sighandler_t IgMProfBootstrap::m_oldAbortSigHandler = 0;
IgMProfTreeTextBrowser *IgMProfBootstrap::m_mallocBrowser = 0;
IgMProfTreeTextBrowser *IgMProfBootstrap::m_profileBrowser = 0;
IgMProfTreeTextBrowser *IgMProfBootstrap::m_leaksBrowser = 0;    
