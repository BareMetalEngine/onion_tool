#include "build.h"
#include "lib/include/foo.h"

#include <iostream>

int main()
{
	auto foo = MakeFoo();
	auto value = foo->test();
	std::cout << "The foo is: " << value << "\n"; 
	return 0;
}