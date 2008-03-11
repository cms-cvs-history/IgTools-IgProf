#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <iostream>
#include <cassert>

void a5(int i) { free(malloc(30 + i%10)); }
void a4(int i) { a5(i); }
void a3(int i) { a4(i); }
void a2(int i) { i % 40 > 20 ? a3(i) : a4(i); }
void a1(int i) { i % 20 > 10 ? a2(i) : a3(i); }

void
a (int j)
{
    std::cerr << "a(" << getpid () << "[" << pthread_self ()
	      << "], " << j << ")\n";

    for  (int i = 0; i < 1000000; i++)
	a1 (i);
}

void b13(int i) { free(malloc(20 + i % 10)); }
void b12(int i) { b13(i); }
void b11(int i) { b12(i); }
void b10(int i) { b11(i); }
void b9(int i) { b10(i); }
void b8(int i) { b9(i); }
void b7(int i) { b8(i); }
void b6(int i) { b7(i); }
void b5(int i) { b6(i); }
void b4(int i) { i % 20 > 10 ? b5(i) : b6(i); }
void b3(int i) { i % 20 > 10 ? b4(i) : b5(i); }
void b2(int i) { i % 40 > 20 ? b3(i) : b4(i); }
void b1(int i) { i % 20 > 10 ? b2(i) : b3(i); }

void *
b (void *)
{
    for (int j = 0; j < 10; j++)
    {
        std::cerr << "b(" << getpid () << "[" << pthread_self ()
		  << "], " << j << ")\n";
	
	for  (int i = 0; i < 1000000; i++)
	    b1 (i);
    }
    return 0;
}

int
main (int /*argc*/, char ** /*argv*/)
{
    static const int NTHREADS = 10;
    pthread_t threads [NTHREADS];
    for (int i = 0; i < NTHREADS; ++i)
        pthread_create (&threads [i], 0, b, 0);

    for (int i = 0; i < 10; i++)
	a (i);	

    for (int i = 0; i < NTHREADS; ++i)
	pthread_join (threads [i], 0);
}
