//<<<<<< INCLUDES                                                       >>>>>>

#include <dlfcn.h>
#include <stdio.h>
#include <pthread.h>
#include <signal.h>
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

// Let's override pthread_create to enable signal handling...This is
// probably the coolest piece of code I have ever written..

extern "C"
{
    unsigned int __igprof_magic_lastThread = 0;
    
    typedef int (*pthread_create_ptr) (void * __thread,
				       const void * __attr,
				       void *(*__start_routine) (void *),
				       void * __arg);
        
    typedef void *(* PthreadHandlerRoutine) (void *);    

    typedef struct 
    {
	PthreadHandlerRoutine startRoutine;
	void *arg;	
    }HandlerArgs;    
    

    void *__igprof_magic_wrapperRoutine (HandlerArgs *args)
    {
	sigset_t newset;
	sigemptyset (&newset);
	
	sigaddset (&newset, SIGPROF);
	pthread_sigmask (SIG_UNBLOCK, &newset, NULL);
	
	PthreadHandlerRoutine startRoutine = args->startRoutine;
	
	fprintf (stderr, "IgProf: pthread_create being called!"
		 "IgProf enabled SIGPROF for thread %d as well.", (int) pthread_self ());
	
	fflush (NULL);
	
	if (__igprof_magic_lastThread < pthread_self ())
	    __igprof_magic_lastThread = pthread_self ();
	
	return startRoutine(args->arg);		
    }
    
    int pthread_create (pthread_t * __thread,
			const pthread_attr_t * __attr,
			void *(*__start_routine) (void *),
			void * __arg) __THROW
    {
	pthread_create_ptr realPthreadCreate = (pthread_create_ptr)
					       dlsym (RTLD_NEXT, 
						      "pthread_create");
	HandlerArgs args;
	args.startRoutine = __start_routine;
	args.arg = __arg;
   
	return realPthreadCreate (__thread,
				  __attr,
				  (PthreadHandlerRoutine) __igprof_magic_wrapperRoutine, 
				  (void *) &args);	
    }
}
