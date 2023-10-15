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
		LogError() << "Could not detect CMAKE";
		return false;
	}

	// generate cmake file
	if (0 != std::system("cmake ."))
	{
		LogError() << "Could not run CMAKE config";
		return false;
	}

	// build cmake file
	if (0 != std::system("cmake --build . -- -j`nproc`"))
	{
		LogError() << "Could not run CMAKE config";
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
		LogError() << "[EXCEPTION] File system Error: " << e.what();
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
		LogError() << "Failed to determine path to MSBuild.exe, is the Visual Studio installed?";
		return false;
	}

	// is this a valid executable
	const auto msBuildPath = fs::path(Trim(msBuildPathStr.str()));
	if (!fs::is_regular_file(msBuildPath))
	{
		LogError() << "MSBuild not found at " << msBuildPath;
		return false;
	}
	else
	{
		LogSuccess() << "MSBuild found at " << msBuildPath;
	}

	// find solution file
	fs::path solutionFilePath;
	if (!FindSolution(cfg.derivedSolutionPathBase, solutionFilePath))
	{
		LogError() << "Failed to find solution file at " << cfg.derivedSolutionPathBase;
		return false;
	}
	else
	{
		LogSuccess() << "Solution found at " << solutionFilePath;
	}

	// configuration
	std::string config(cmdLine.get("config", "Release"));

	// compile the solution
	std::stringstream args;
	args << EscapeArgument(msBuildPath.u8string());
	args << "/p:Platform=x64 ";
	args << "/p:Configuration=" << config << " ";
	args << EscapeArgument(solutionFilePath.u8string());
	const auto cmd = args.str();
	LogInfo() << "Running: '" << cmd << "'";
	if (0 != std::system(cmd.c_str()))
	{
		LogError() << "Building failed";
		return false;
	}

	// built
	return true;
}


static bool BuildConfiguration(const Configuration& cfg, const Commandline& cmdLine)
{
	// info
	LogInfo() << "Building '" << cfg.mergedName() << "'";
	
	// no solution directory
	if (!fs::is_directory(cfg.derivedSolutionPathBase))
	{
		LogError() << "Make file/solution directory " << cfg.derivedSolutionPathBase << " does not exist";
		return false;
	}

	// change to solution path
	std::error_code er;
	const auto rootPath = fs::current_path();
	fs::current_path(cfg.derivedSolutionPathBase, er);
	if (er)
	{
		LogError() << "Failed to change directory to " << cfg.derivedSolutionPathBase << ", error: " << er;
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
			LogError() << "Building from generator '" << NameEnumOption(cfg.generator) << "' is not supported";
			valid = false;
		}
	}
	catch (fs::filesystem_error& e)
	{
		LogError() << "[EXCEPTION] File system Error: " << e.what();
		valid = false;
	}
	catch (std::exception& e)
	{
		LogError() << "[EXCEPTION] General Error: " << e.what();
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

	std::stringstream str;
	str << PrintEnumOptions(platform);

	LogInfo() << "onion build [options]";
	LogInfo() << "";
	LogInfo() << "General options:";
	LogInfo() << "  -module=<module to build>";
	LogInfo() << "  -config=<release|debug|final|profile|checked> - configuration to build (defaults to Release)";
	LogInfo() << "  -platform=" << str.str() << "";
	LogInfo() << "";
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