#include <cmath>

void 
foo1 (void)
{
	asin (0.5);
}

void 
foo2 (void)
{
	exp (2.0);
}

int
main (int /*argc*/, char **/*argv*/)
{
	for (int i = 0; i < 100000000; i++)
       	{
	    foo1 ();
	}

	for (int i = 0; i < 100000000; i++) 
	{
	    foo2 ();	    
	}
}
