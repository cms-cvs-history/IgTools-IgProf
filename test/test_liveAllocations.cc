int
main (int /*argc*/, char ** /*argv*/)
{
    for (int i = 0; i < 10; i++)
    {
	char *a = new char;
	delete a;	
    }    
}

