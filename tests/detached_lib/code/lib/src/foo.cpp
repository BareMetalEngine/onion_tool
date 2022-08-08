#include "build.h"
#include "foo.h"

extern "C" int GetMystery()
{
	return THE_FOO_CONSTANT;
}