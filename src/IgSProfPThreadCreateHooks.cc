//<<<<<< INCLUDES                                                       >>>>>>

//#include <iostream>
#include <dlfcn.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>

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
    unsigned int __igprof_magic_threadArray[1024];    
    bool	 __igprof_magic_inPthreadLock = false;    
    
    typedef int (*pthread_create_ptr) (void * thread,
				       const void * attr,
				       void *(*start_routine) (void *),
				       void * arg);

    typedef int (*pthread_sigmask_ptr) (int how, 
				    const sigset_t *newmask, 
				    sigset_t *oldmask);
    
    typedef void *(* PthreadHandlerRoutine) (void *);    
    typedef int (*pthread_mutex_lock_ptr) (pthread_mutex_t *mutex);
    typedef int (*pthread_mutex_unlock_ptr) (pthread_mutex_t *mutex);
    typedef int (*setitimer_ptr) (int which, const struct itimerval *value, struct itimerval *ovalue);
    typedef sighandler_t (*signal_ptr) (int signum, sighandler_t handler);
    typedef int (*pthread_join_ptr)(pthread_t th, void **thread_return);    
        
    typedef struct 
    {
	PthreadHandlerRoutine startRoutine;
	void *arg;	
    }HandlerArgs;    
    

    void *__igprof_magic_wrapperRoutine (HandlerArgs *args)
    {
	static pthread_sigmask_ptr realPthreadSigMask = 0;
	if (!realPthreadSigMask)
	{
	    realPthreadSigMask = (pthread_sigmask_ptr)
				 dlsym (RTLD_NEXT, 
					"pthread_sigmask");
	}
	
	sigset_t newset;
	sigemptyset (&newset);
	
	sigaddset (&newset, SIGPROF);
	realPthreadSigMask (SIG_UNBLOCK, &newset, NULL);
	
	PthreadHandlerRoutine startRoutine = args->startRoutine;
	
	//	std::cerr << "IgProf: pthread_create being called!"
	//	  << "IgProf enabled SIGPROF for thread"
	//	  << (int) pthread_self () 
	//	  << " as well." << std::endl;
		
	__igprof_magic_threadArray[__igprof_magic_lastThread++] 
	    = pthread_self ();
		
	return startRoutine(args->arg);		
    }
    
    int pthread_create (pthread_t * thread,
			const pthread_attr_t * attr,
			void *(*start_routine) (void *),
			void * arg) __THROW

    {
	static pthread_create_ptr realPthreadCreate = 0;
	fprintf (stderr, "pthread_create_called\n");
	fflush (NULL);
	
	if (!realPthreadCreate)
	{
	    realPthreadCreate = (pthread_create_ptr) dlsym (RTLD_NEXT, 
							    "pthread_create");
	}
	
	HandlerArgs args;
	args.startRoutine = start_routine;
	args.arg = arg;
   
	return realPthreadCreate (thread,
				  attr,
				  (PthreadHandlerRoutine) __igprof_magic_wrapperRoutine, 
				  (void *) &args);	
    }

//     int pthread_sigmask(int how, 
// 			const sigset_t *newmask, 
// 			sigset_t *oldmask) __THROW
//     {
// 	static pthread_sigmask_ptr realPthreadSigMask = 0;
	
// 	fprintf (stderr, "pthread_sigmask\n");
// 	fflush (NULL);
// 	// Reimplementing pthread_sigmask as well. We ALWAYS enable
// 	// SIGPROF handling...
// 	if (!realPthreadSigMask) 
// 	{
// 	    realPthreadSigMask = (pthread_sigmask_ptr) dlsym (RTLD_NEXT, 
// 							      "pthread_sigmask");
// 	}
	
// 	//std::cerr << "IgProf: pthread_sigmask called!"
// 	//	  << "IgProf will enable SIGPROF ANYWAY!\n"
// 	//	  << std::endl;
		
// 	sigset_t newset;
// 	sigemptyset (&newset);
// 	sigaddset (&newset, SIGPROF);
// 	int result = realPthreadSigMask (how, newmask, NULL);
// 	if (!__igprof_magic_inPthreadLock)
// 	    pthread_sigmask (SIG_UNBLOCK, &newset, oldmask);
// 	return result;	
//     }
    
//     int pthread_mutex_lock(pthread_mutex_t *mutex) __THROW	
//     {	
// 	int result;
// 	static pthread_mutex_lock_ptr realPthreadMutexLock = 0;
	
//     	if (__igprof_magic_inPthreadLock == false)
// 	{
// 	    //fprintf (stderr, "pthread_mutex_lock called\n");
// 	    //fflush (NULL);
	
// 	    __igprof_magic_inPthreadLock = true;
// 	    if (!realPthreadMutexLock)
// 		realPthreadMutexLock = (pthread_mutex_lock_ptr) dlsym (RTLD_NEXT, 
// 								       "pthread_mutex_lock");       

// 	     result = realPthreadMutexLock (mutex);
// 	    __igprof_magic_inPthreadLock = false;
// 	}
	
// 	return result;	
//     }

//     int pthread_mutex_unlock(pthread_mutex_t *mutex) __THROW	
//     {	
// 	int result;
// 	static pthread_mutex_unlock_ptr realPthreadMutexUnlock = 0;
	
//     	if (__igprof_magic_inPthreadLock == false)
// 	{
// 	    //	    fprintf (stderr, "pthread_mutex_unlock called\n");
// 	    //fflush (NULL);
	
// 	    __igprof_magic_inPthreadLock = true;
// 	    if (!realPthreadMutexUnlock)
// 		realPthreadMutexUnlock = (pthread_mutex_unlock_ptr) dlsym (RTLD_NEXT, 
// 									   "pthread_mutex_unlock");       

// 	     result = realPthreadMutexUnlock (mutex);
// 	    __igprof_magic_inPthreadLock = false;
// 	}
	
// 	return result;	
//     }

    int setitimer(int which, 
		  const struct itimerval *value, 
		  struct itimerval *ovalue) 
    {
	fprintf (stderr, "setitimer called\n");
	fflush (NULL);

	int result;
	static setitimer_ptr realSetITimer = 0;
	if (!realSetITimer)
	{
	    realSetITimer = (setitimer_ptr) dlsym (RTLD_NEXT, "setitimer");	    
	}
	result = realSetITimer (which, value, ovalue);
	
	return result;	
    }


    sighandler_t signal(int signum, sighandler_t handler) __THROW	
    {
	fprintf (stderr, "signal called\n");
	fflush (stderr);
	sighandler_t result;
	static signal_ptr realSignal = 0;
	
	if (!realSignal)
	{
	    realSignal = (signal_ptr) dlsym (RTLD_NEXT, "signal");	    
	}
	result = realSignal (signum, handler);
	return result;	
    }
    
    int pthread_join(pthread_t th, void **thread_return)
// FIXME: hack to cope with different thorwing model of pthreads
// functions in glibc2.3
#if __GLIBC__ == 2
# if __GLIBC_MINOR__ == 2
 __THROW
# endif
#endif
    {
	fprintf (stderr, "pthread_join called\n");
	fflush (stderr);
	int result;
	static pthread_join_ptr real_pthread_join = 0;
	
	if (!real_pthread_join)
	{
	    real_pthread_join = (pthread_join_ptr) dlsym (RTLD_NEXT, "pthread_join");	    
	}
	result = real_pthread_join (th, thread_return);
	return result;	
    }
}
