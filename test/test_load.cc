#include <cmath>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <iostream>
#include <cassert>

void
a (int j)
{
    std::cerr << "a(" << getpid () << "[" << pthread_self ()
	      << "], " << j << ")\n";

    for  (int i = 0; i < 10000; i++)
    {
	void *ptr = malloc (100);
	exp (1);	
	free (ptr);
    }    
}

void *
b (void *)
{
    for (int j = 0; j < 10; j++)
    {
        std::cerr << "b(" << getpid () << "[" << pthread_self ()
		  << "], " << j << ")\n";
	
	for  (int i = 0; i < 10000; i++)
	{
	    void *ptr = malloc (200);
	    exp (1);	
	    free (ptr);
	}
    }
    return 0;
}

int
main (int /*argc*/, char ** /*argv*/)
{
    pthread_t threads [10];
    for (int i = 0; i < 10; ++i)
        pthread_create (&threads [i], 0, b, 0);

    for (int i = 0; i < 10; i++)
	a (i);	

    for (int i = 0; i < 10; ++i)
	pthread_join (threads [i], 0);
}
