//<<<<<< INCLUDES                                                       >>>>>>

#include "IgSProfTimerHooks.h"
#include "IgMProfTreeSingleton.h"

#include <sys/time.h>
#include <sys/param.h>
#include <sys/types.h>
#include <iostream>
#include <signal.h>
#include <pthread.h>
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

extern "C"
{
    extern unsigned int __igprof_magic_lastThread;    
}

void 
IGUANA_sprof_sigprof_hook (void)
{
    //    std::cerr << getpid () << " of group " 
    //	      << getpgrp () << " signaled" 
    //	      << std::endl;

    for (unsigned int i = pthread_self ()+1;
	 i <= __igprof_magic_lastThread;
	 i++)
    {
    //	std::cerr << "Trying to signal tid " << i << std::endl;	
	pthread_kill (i, SIGPROF);	
    }
	
    IgMProfProfileTreeSingleton::instance()->addCurrentStacktrace (1, 3, 0);
}

void
IGUANA_sprof_dispose_hook (void)
{
    std::cerr << "IgProf: Stopping profiler" << std::endl;
    
    signal (SIGPROF,SIG_DFL );    
    struct itimerval timings;
    timings.it_interval.tv_sec = 0;
    timings.it_interval.tv_usec = 0;
    timings.it_value.tv_sec = 0;
    timings.it_value.tv_usec = 0;
        
    setitimer(ITIMER_PROF, &timings, 0);    
}

void
IGUANA_sprof_atfork_child (void)
{
    // This function is called each time a program forks (either
    // because of a pthread_create or of a real fork().  This allows
    // to get information for multithreaded programs as well besides
    // the main thread.

    // FIXME: this is VERY specific to threads/fork implementation
    // found in certain version of linux...I doubt seriously it will
    // work on something != 2.4...

    signal (SIGPROF,(sighandler_t) IGUANA_sprof_sigprof_hook);
    struct itimerval timings;
    timings.it_interval.tv_sec = 0;
    timings.it_interval.tv_usec = 1000;
    timings.it_value.tv_sec = 0;
    timings.it_value.tv_usec = 1000;
        
    setitimer(ITIMER_PROF, &timings, 0);    
}


void
IGUANA_sprof_initialize_hook (void)
{
    std::cerr << "IgProf: Starting profiler" << std::endl;
    pthread_atfork (0, 0, IGUANA_sprof_atfork_child);    
    signal (SIGPROF,(sighandler_t) IGUANA_sprof_sigprof_hook );    
    struct itimerval timings;
    timings.it_interval.tv_sec = 0;
    timings.it_interval.tv_usec = 1000;
    timings.it_value.tv_sec = 0;
    timings.it_value.tv_usec = 1000;
    
    setitimer(ITIMER_PROF, &timings, 0);    
}
