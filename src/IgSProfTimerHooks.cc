//<<<<<< INCLUDES                                                       >>>>>>

#include "IgSProfTimerHooks.h"
#include "IgMProfTreeSingleton.h"

#include <sys/time.h>
#include <sys/param.h>
#include <iostream>
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

void 
IGUANA_sprof_sigprof_hook (void)
{
    IgMProfProfileTreeSingleton::instance()->addCurrentStacktrace (1, 3, 0);    
}

void
IGUANA_sprof_dispose_hook (void)
{
    std::cerr << "Stopping profiler" << std::endl;
    
    signal (SIGPROF,SIG_DFL );    
    struct itimerval timings;
    timings.it_interval.tv_sec = 0;
    timings.it_interval.tv_usec = 0;
    timings.it_value.tv_sec = 0;
    timings.it_value.tv_usec = 0;
        
    setitimer(ITIMER_PROF, &timings, 0);    
}


/*FIXME: multithreading under linux will give wrong results.
 */
void
IGUANA_sprof_initialize_hook (void)
{
    std::cerr << "Starting profiler" << std::endl;
    
    signal (SIGPROF,(sighandler_t) IGUANA_sprof_sigprof_hook );    
    struct itimerval timings;
    timings.it_interval.tv_sec = 0;
    timings.it_interval.tv_usec = 1000;
    timings.it_value.tv_sec = 0;
    timings.it_value.tv_usec = 1000;
        
    setitimer(ITIMER_PROF, &timings, 0);    
}
