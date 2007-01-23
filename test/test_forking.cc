#include <cmath>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <iostream>
#include <cassert>

void
a (void)
{
    std::cerr << "a () called by thread " << getpid () << std::endl;
    for  (int i = 0; i < 1000000; i++)
    {
	exp(1);	
    }    
}

void *
b (void *)
{
    std::cerr << "b() called by thread " << getpid () << std::endl;
    sigset_t newset;
    sigset_t oldset;

    for (int j  = 0; j < 10; j++)
    {
	std::cerr << "thread " << getpid ()<< " still alive" << std::endl;
	for  (int i = 0; i < 10000000; i++)
	{
	    exp(1);
	}
    }
    return 0;
}

int
main (int /*argc*/, char ** /*argv*/)
{
    int pid = 0;
    
    pthread_t thread;
    
    pthread_create (&thread, 0, b, 0);
//    pid = fork ();

    if (pid)
    {
	for (int i = 0; i < 30; i++)
	{
	    b (0);	    
	}    
    }
    else
    {
	for (int i = 0; i < 30; i++)
	{
	    a ();	
	}	
    }

    pthread_join (thread, 0);
}
