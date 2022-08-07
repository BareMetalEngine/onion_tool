#include "common.h"
#include "utils.h"
#include "toolSign.h"
#include "fileRepository.h"

//--

ToolSign::ToolSign()
{}

int ToolSign::run(const Commandline& cmdline)
{
	const auto filePath = fs::weakly_canonical(cmdline.get("file"));
	if (!fs::is_regular_file(filePath))
	{
		std::cerr << KRED << "[BREAKING] Target executable path does not exist: " << filePath << "\n" << RST;
		return 1;
	}

	//const auto signTool = 



	return 0;
}

//--