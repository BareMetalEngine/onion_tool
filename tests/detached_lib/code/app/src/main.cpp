#include "build.h"
#include "lib/include/foo.h"
#include <iostream>

#ifdef _WIN32

typedef int(__cdecl* TFooFunc)();

#include <Windows.h>

int main()
{
	HMODULE hLib = LoadLibraryA("lib.dll");
	if (!hLib)
	{
		std::cout << "Failed to load library!\n";
		return - 1;
	}

	auto func = (TFooFunc)GetProcAddress(hLib, "GetMystery");
	if (!func)
	{
		std::cout << "Failed to find function in library!\n";
		return 1;
	}

	auto value = func();
	std::cout << "The foo is: " << value << "\n"; 
	return 0;
}

#else

typedef int(*TFooFunc)();

#include <dlfcn.h>

int main()
{
#ifdef __APPLE__
	void* lib = dlopen("liblib.dylib", RTLD_NOW);
#else
    void* lib = dlopen("liblib.so", RTLD_NOW);
#endif
	if (!lib)
	{
		std::cout << "Failed to load library!\n";
		return 1;
	}

	auto func = (TFooFunc)dlsym(lib, "GetMystery");
	if (!func)
	{
		std::cout << "Failed to find function in library!\n";
		return 1;
	}

	auto value = func();
	std::cout << "The foo is: " << value << "\n";
	return 0;
}

#endif