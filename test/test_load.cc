#include <cmath>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <iostream>
#include <cassert>

void *
a (void *)
{
    std::cerr << "a(" << getpid () << "[" << pthread_self () << "])\n";
    for  (int i = 0; i < 1000000; i++)
    {
	void *ptr = malloc (100);
	exp (1);	
	free (ptr);
    }    
    return 0;
}

void *
b (void *)
{
    for (int j = 0; j < 10; j++)
    {
        std::cerr << "b(" << getpid () << "[" << pthread_self () << "])\n";
	
	for  (int i = 0; i < 1000000; i++)
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
        pthread_create (&threads [i], 0, i % 2 ? b : b, 0);

    for (int i = 0; i < 10; i++)
	a (0);	

    for (int i = 0; i < 10; ++i)
	pthread_join (threads [i], 0);
}
