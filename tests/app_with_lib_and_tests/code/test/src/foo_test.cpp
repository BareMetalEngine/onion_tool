#include "build.h"
#include "lib/include/foo.h"

TEST(Foo, ReturnsExpectedValue)
{
	auto foo = MakeFoo();
	auto value = foo->test();
	EXPECT_EQ(42, value);
}