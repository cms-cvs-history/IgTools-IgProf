#include <iostream>

class A
{
public:
	void *operator new(size_t s)
	{
		void *dummy = ::operator new (s);
		A *me = static_cast<A*>(dummy);
		std::cerr << "my new operator" << std::endl;
		return me;
	}
};

int
main (int /*argc*/, char **/*argv*/)
{
	new A;
}
