#include "build.h"
#include "foo.h"

FooClass::FooClass()
{
}

FooClass::~FooClass()
{
}

int FooClass::test()
{
	return THE_FOO_CONSTANT;
}

FooClassPtr MakeFoo()
{
	return std::make_unique<FooClass>();
}