#include "build.h"
#include "common.h"

static int TheVariable = 42;

int64_t GetCommonValue()
{
	return (int64_t)&TheVariable;
}
