#include "common.h"
#include "utils.h"
#include "toolBuild.h"
#include "configuration.h"

//--

static bool BuildConfigurationCmake(const Configuration& cfg, const Commandline& cmdLine)
{
	// test if cmake exists
	if (0 != std::system("cmake --version"))
	{
		std::cerr << KRED << "[BREAKING] Could not detect CMAKE\n" << RST;
		return false;
	}

	// generate cmake file
	if (0 != std::system("cmake ."))
	{
		std::cerr << KRED << "[BREAKING] Could not run CMAKE config\n" << RST;
		return false;
	}

	// build cmake file
	if (0 != std::system("cmake --build . -- -j`nproc`"))
	{
		std::cerr << KRED << "[BREAKING] Could not run CMAKE config\n" << RST;
		return false;
	}

	// built
	return true;
}

static bool FindSolution(const fs::path& solutionDir, fs::path& outSolutionDir)
{
	bool valid = true;

	try
	{
		if (fs::is_directory(solutionDir))
		{
			for (const auto& entry : fs::directory_iterator(solutionDir))
			{
				const auto name = entry.path().filename().u8string();

				if (entry.is_regular_file() && EndsWith(name, ".sln"))
				{
					outSolutionDir = (solutionDir / name).make_preferred();
					return true;
				}
			}
		}
	}
	catch (fs::filesystem_error& e)
	{
		std::cout << KRED << "[EXCEPTION] File system Error: " << e.what() << "\n" << RST;
		valid = false;
	}

	return valid;
}

static bool BuildConfigurationVS(const Configuration& cfg, const Commandline& cmdLine)
{
	// get the path to the ms build
	std::stringstream msBuildPathStr;
	if (!RunWithArgsAndCaptureOutput("\"\"%ProgramFiles(x86)%\\Microsoft Visual Studio\\Installer\\vswhere.exe\"\" -latest -prerelease -products * -requires Microsoft.Component.MSBuild -find MSBuild\\**\\Bin\\MSBuild.exe", msBuildPathStr))
	{
		std::cerr << KRED << "[BREAKING] Failed to determine path to MSBuild.exe, is the Visual Studio installed?\n" << RST;
		return false;
	}

	// is this a valid executable
	const auto msBuildPath = fs::path(Trim(msBuildPathStr.str()));
	if (!fs::is_regular_file(msBuildPath))
	{
		std::cerr << KRED << "[BREAKING] MSBuild not found at " << msBuildPath << "\n" << RST;
		return false;
	}
	else
	{
		std::cerr << KGRN << "MSBuild found at " << msBuildPath << "\n" << RST;
	}

	// find solution file
	fs::path solutionFilePath;
	if (!FindSolution(cfg.derivedSolutionPath, solutionFilePath))
	{
		std::cerr << KRED << "[BREAKING] Failed to find solution file at " << cfg.derivedSolutionPath << "\n" << RST;
		return false;
	}
	else
	{
		std::cerr << KGRN << "Solution found at " << solutionFilePath << "\n" << RST;
	}

	// compile the solution
	std::stringstream args;
	args << EscapeArgument(msBuildPath.u8string());
	args << "/p:Platform=x64";
	args << " ";
	args << EscapeArgument(solutionFilePath.u8string());
	const auto cmd = args.str();
	std::cout << "Running: '" << cmd << "'\n";
	if (0 != std::system(cmd.c_str()))
	{
		std::cerr << KRED << "[BREAKING] Building failed\n" << RST;
		return false;
	}

	// built
	return true;
}


static bool BuildConfiguration(const Configuration& cfg, const Commandline& cmdLine)
{
	// info
	std::cout << "Building '" << cfg.mergedName() << "'\n";
	
	// no solution directory
	if (!fs::is_directory(cfg.derivedSolutionPath))
	{
		std::cerr << "[BREAKING] Make file/solution directory " << cfg.derivedSolutionPath << " does not exist\n";
		return false;
	}

	// change to solution path
	std::error_code er;
	const auto rootPath = fs::current_path();
	fs::current_path(cfg.derivedSolutionPath, er);
	if (er)
	{
		std::cerr << "[BREAKING] Failed to change directory to " << cfg.derivedSolutionPath << ", error: " << er << "\n";
		return false;
	}

	// build with generator
	bool valid = true;
	try
	{
		if (cfg.generator == GeneratorType::CMake)
		{
			valid = BuildConfigurationCmake(cfg, cmdLine);
		}
		else if (cfg.generator == GeneratorType::VisualStudio19 || cfg.generator == GeneratorType::VisualStudio22)
		{
			valid = BuildConfigurationVS(cfg, cmdLine);
		}
		else
		{
			std::cerr << KRED << "[BREAKING] Building from generator '" << NameEnumOption(cfg.generator) << "' is not supported\n" << RST;
			valid = false;
		}
	}
	catch (fs::filesystem_error& e)
	{
		std::cerr << KRED << "[EXCEPTION] File system Error: " << e.what() << "\n" << RST;
		valid = false;
	}
	catch (std::exception& e)
	{
		std::cerr << KRED << "[EXCEPTION] General Error: " << e.what() << "\n" << RST;
		valid = false;
	}

	// change back the path
	fs::current_path(rootPath, er);
	return valid;
}

//--

ToolBuild::ToolBuild()
{}

void ToolBuild::printUsage()
{
	auto platform = DefaultPlatform();

	std::cout << KBOLD << "onion build [options]\n" << RST;
	std::cout << "\n";
	std::cout << "General options:\n";
	std::cout << "  -module=<module to build>\n";
	std::cout << "  -platform=" << PrintEnumOptions(platform) << "\n";
	std::cout << "\n";
}

int ToolBuild::run(const Commandline& cmdline)
{
	Configuration config;
	if (!Configuration::Parse(cmdline, config))
		return 1;

	if (!BuildConfiguration(config, cmdline))
		return 1;

	return 0;
}

//--