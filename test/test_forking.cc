#include <cmath>
#include <unistd.h>

void 
a (void)
{
    for  (int i = 0; i < 1000000; i++)
    {
	exp(1);	
    }    
}

void 
b (void)
{
    for  (int i = 0; i < 1000000; i++)
    {
	exp(1);	
    }    
}

int
main (int /*argc*/, char **/*argv*/)
{
    int pid = 0;
    
    pid = fork ();
    if (!pid)
    {
	for (int i = 0; i < 10; i++)
	{
	    b ();	    
	}    
    }
    else
    {
	for (int i = 0; i < 15; i++)
	{
	    a ();	
	}	
    }
}
