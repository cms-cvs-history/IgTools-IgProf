#include <cmath>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <iostream>
#include <cassert>

void a5(int i) { void *ptr = malloc(100); exp(1); free (ptr); }
void a4(int i) { a5(i); }
void a3(int i) { a4(i); }
void a2(int i) { i % 40 > 20 ? a3(i) : a4(i); }
void a1(int i) { i % 20 > 10 ? a2(i) : a3(i); }

void
a (int j)
{
    std::cerr << "a(" << getpid () << "[" << pthread_self ()
	      << "], " << j << ")\n";

    for  (int i = 0; i < 100000; i++)
	a1(i);
}

void b5(int i) { void *ptr = malloc(200); exp(1); free (ptr); }
void b4(int i) { b5(i); }
void b3(int i) { b4(i); }
void b2(int i) { i % 40 > 20 ? b3(i) : b4(i); }
void b1(int i) { i % 20 > 10 ? b2(i) : b3(i); }

void *
b (void *)
{
    for (int j = 0; j < 10; j++)
    {
        std::cerr << "b(" << getpid () << "[" << pthread_self ()
		  << "], " << j << ")\n";
	
	for  (int i = 0; i < 100000; i++)
	    b1(i);
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
