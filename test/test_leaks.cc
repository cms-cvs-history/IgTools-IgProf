void test_leak ()
{
	new char [9];
}

void test_leak2 ()
{
	char *a = new char [10];
	delete a; 
}

void test_leak3 ()
{
	new char;
}

int 
main ()
{
	char *arrays[100];	
	for (int i = 0 ; i < 100 ; i++) 
	{
		arrays [i] = new char [10];
	}
	
	for (int i = 0 ; i < 99 ; i++) 
	{
		delete[] arrays[i];
	}
	test_leak ();
	test_leak2 ();
	test_leak3 ();
}
