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
    extern unsigned int __igprof_magic_threadArray[1024];    
}

pthread_mutex_t __igprof_magic_profilerMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_t __sprof_magic_workerThreadID = 0;

void 
IGUANA_sprof_sigprof_hook (void)
{
    //    pthread_mutex_lock (&__igprof_magic_profilerMutex);
    
    static int j = 0;
    
    if (pthread_self () == __sprof_magic_workerThreadID)
    {
     	if (j++ % 1000 == 0)
	{
	  std::cerr << j/1000 << " signals received " 
		    << std::endl;
    	}
	for (unsigned int i = 0;
	     i < __igprof_magic_lastThread;
	     i++)
	{
		//	std::cerr << " Trying to signal tid " 
		//	  << __igprof_magic_threadArray[i] << std::endl;	
		pthread_kill (__igprof_magic_threadArray[i], SIGPROF);	
	}
    }
    else
    {
        IgMProfProfileTreeSingleton::instance()->addCurrentStacktrace (1, 3, 0);
    }
    
    //pthread_mutex_unlock (&__igprof_magic_profilerMutex);
}

void
IGUANA_sprof_dispose_hook (void)
{
    std::cerr << "IgProf: Stopping profiler" << std::endl;
    
    //    signal (SIGPROF,SIG_DFL );    
    struct itimerval timings;
    timings.it_interval.tv_sec = 0;
    timings.it_interval.tv_usec = 0;
    timings.it_value.tv_sec = 0;
    timings.it_value.tv_usec = 0;
        
    //setitimer(ITIMER_PROF, &timings, 0);    
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


void *
__sprof_magic_workerThread (void *)
{
    std::cerr << "IgProf: Starting profiler on thread " 
	      << pthread_self ()
	      << std::endl;
    //pthread_mutex_init (&__igprof_magic_profilerMutex, NULL);
    
    __sprof_magic_workerThreadID = pthread_self ();    
    pthread_atfork (0, 0, IGUANA_sprof_atfork_child);    
    signal (SIGPROF,(sighandler_t) IGUANA_sprof_sigprof_hook );    
    struct itimerval timings;
    timings.it_interval.tv_sec = 0;
    timings.it_interval.tv_usec = 1000;
    timings.it_value.tv_sec = 0;
    timings.it_value.tv_usec = 1000;
    
    setitimer(ITIMER_PROF, &timings, 0);
    sigset_t waitingSet;
    sigemptyset (&waitingSet);    
    sigaddset (&waitingSet, SIGPROF);    
    while (true)
    {
	//nanosleep (100000);
    	    //sleep (1);
	//sigwait (&waitingSet, &sig);
    }
    
    return 0;    
}

typedef int (*pthread_create_ptr) (void * __thread,
				   const void * __attr,
				   void *(*__start_routine) (void *),
				   void * __arg);

void
IGUANA_sprof_initialize_hook (void)
{
    pthread_create_ptr realPthreadCreate = (pthread_create_ptr) 
					   dlsym (RTLD_NEXT, 
						  "pthread_create");
    
    pthread_t thread;
    realPthreadCreate (&thread, 0, __sprof_magic_workerThread, 0);
    __igprof_magic_threadArray[__igprof_magic_lastThread++] = pthread_self ();
    
//     std::cerr << "IgProf: Starting profiler" << std::endl;
//     pthread_mutex_init (&__igprof_magic_profilerMutex, NULL);
    
//     pthread_atfork (0, 0, IGUANA_sprof_atfork_child);    
//     signal (SIGPROF,(sighandler_t) IGUANA_sprof_sigprof_hook );    
//     struct itimerval timings;
//     timings.it_interval.tv_sec = 0;
//     timings.it_interval.tv_usec = 1000;
//     timings.it_value.tv_sec = 0;
//     timings.it_value.tv_usec = 1000;
    
//     setitimer(ITIMER_PROF, &timings, 0);
}
