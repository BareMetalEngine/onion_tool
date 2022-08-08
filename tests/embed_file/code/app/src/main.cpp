#include "build.h"
#include <iostream>

extern const unsigned char* EMBED_app_dir_file_txt_DATA;

int main()
{
	std::cout << "The embedded data is: \"" << (const char*)EMBED_app_dir_file_txt_DATA << "\"" << std::endl;
	return 0;
}