#include "common.h"
#include "utils.h"
#include "toolSign.h"
#include "fileRepository.h"

//--

ToolSign::ToolSign()
{}

int ToolSign::run(const char* argv0, const Commandline& cmdline)
{
	const auto builderExecutablePath = fs::absolute(argv0);
	if (!fs::is_regular_file(builderExecutablePath))
	{
		std::cerr << KRED << "[BREAKING] Invalid local executable name: " << builderExecutablePath << "\n" << RST;
		return 1;
	}

	const auto filePath = fs::weakly_canonical(cmdline.get("file"));
	if (!fs::is_regular_file(filePath))
	{
		std::cerr << KRED << "[BREAKING] Target executable path does not exist: " << filePath << "\n" << RST;
		return 1;
	}

	const auto tempPath = builderExecutablePath.parent_path() / ".temp";

	FileRepository fileRepository;
	if (!fileRepository.initialize(builderExecutablePath, tempPath))
	{
		std::cerr << KRED << "[BREAKING] Failed to initialize file repository\n" << RST;
		return 1;
	}

	//const auto signTool = 



	return 0;
}

//--