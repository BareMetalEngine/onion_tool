#pragma once

#include <memory>

// test exported class
class LIB_API FooClass
{
public:
	FooClass();
	~FooClass();

	int test(); // returns 42
};

// test exported method
extern LIB_API FooClassPtr MakeFoo();