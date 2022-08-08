#include "build.h"
#include "dyn1/include/dynamic.h"
#include "dyn2/include/dynamic.h"

#include <iostream>

int main()
{
	auto value1 = GetDynamic1Value();
	auto value2 = GetDynamic2Value();
	std::cout << "The dynamic 1 value is: " << value1 << "\n"; 
	std::cout << "The dynamic 2 value is: " << value2 << "\n";
	return 0;
}