#include "common.h"
#include "utils.h"
#include "toolTest.h"
#include "moduleConfiguration.h"
#include "moduleRepository.h"
#include "configurationList.h"
#include "configuration.h"
#include "projectCollection.h"
#include "project.h"
#include "projectManifest.h"

//--

ToolTest::ToolTest()
{}

void ToolTest::printUsage()
{
	std::cout << KBOLD << "onion test [options]\n" << RST;
	std::cout << "\n";
	std::cout << "General options:\n";
	std::cout << "  -module=<path to module to configure>\n";
	std::cout << "  -parallel - run all tests in parallel\n";
	std::cout << "  -fastfail - stop after first failure\n";
	std::cout << "\n";
}

static bool IsTestableProject(const ProjectInfo* proj)
{
	if (proj->manifest->type == ProjectType::TestApplication)
		return true;

	if (proj->manifest->type == ProjectType::Application && proj->manifest->optionSelfTest == true)
		return true;

	return false;
}

bool ProjectBinaryName(const ProjectInfo* project, std::string& outName)
{
	const auto safeName = ReplaceAll(project->name, "/", "_");

	switch (project->manifest->type)
	{
	case ProjectType::TestApplication:
	case ProjectType::Application:
#ifdef _WIN32
		outName = safeName + ".exe";
#else
		outName = safeName;
#endif
		return true;

	case ProjectType::SharedLibrary:
#ifdef _WIN32
		outName = safeName + ".dll";
#else
		outName = safeName + ".so";
#endif
		return true;
	}

	return false;
}

static bool ProjectBinaryPath(const ProjectInfo* project, const Configuration& config, fs::path& outPath)
{
	std::string executableName;
	if (!ProjectBinaryName(project, executableName))
		return false;

	outPath = (config.binaryPath / executableName).make_preferred();
	return true;
}

static bool RunTestsForConfiguration(const ModuleRepository& modules, const Configuration& config, const Commandline& cmdLine)
{
	std::cout << "Running tests for configuration '" << config.mergedName() << "'\n";

	// populate project structure
	ProjectCollection structure;
	if (!structure.populateFromModules(modules.modules(), config))
	{
		std::cerr << KRED << "[BREAKING] Failed to populate project structure from installed modules\n" << RST;
		return false;
	}

	if (!structure.filterProjects(config))
	{
		std::cerr << KRED << "[BREAKING] Failed to filter project structure\n" << RST;
		return false;
	}

	std::vector<const ProjectInfo*> testProjects;
	for (const auto* proj : structure.projects())
		if (IsTestableProject(proj))
			testProjects.push_back(proj);

	if (testProjects.empty())
	{
		std::cout << "No test projects found in configuration\n";
		return true;
	}

	std::cout << "Collected " << testProjects.size() << " projects for testing\n";

	bool valid = true;
	for (const auto* proj : testProjects)
	{
		fs::path binaryPath;
		if (ProjectBinaryPath(proj, config, binaryPath))
		{
			const auto binaryDirectory = binaryPath.parent_path();

			if (!fs::is_regular_file(binaryPath))
			{
				std::cerr << KRED << "[BREAKING] Failed to find binary for project '" << proj->name << "' at " << binaryPath << "\n" << RST;
				valid = false;
				continue;
			}

			std::stringstream command;
#ifndef _WIN32
			command << std::string("./");
#endif
			command << binaryPath.filename().u8string();

			// TODO: self-test arguments

			if (!RunWithArgsInDirectory(binaryDirectory, command.str()))
			{
				std::cerr << KRED << "[BREAKING] Test for project '" << proj->name << "' failed!\n" << RST;
				valid = false;
				continue;
			}
		}
	}

	return valid;
}

int ToolTest::run(const char* argv0, const Commandline& cmdline)
{
	const auto builderExecutablePath = fs::absolute(argv0);
	if (!fs::is_regular_file(builderExecutablePath))
	{
		std::cerr << KRED << "[BREAKING] Invalid local executable name: " << builderExecutablePath << "\n" << RST;
		return false;
	}

	//--

	fs::path modulePath, buildListPath;
	{
		const auto str = cmdline.get("module");
		if (str.empty())
		{
			{
				auto testPath = fs::current_path() / MODULE_MANIFEST_NAME;
				if (!fs::is_regular_file(testPath))
				{
					std::cerr << KRED "[BREAKING] Onion build tool run in a directory without \"" << MODULE_MANIFEST_NAME << "\", specify path to a valid module via -module\n";
					return false;
				}
				else
				{
					modulePath = fs::weakly_canonical(fs::current_path().make_preferred());
				}
			}

			{
				auto testPath = fs::current_path() / BUILD_LIST_NAME;
				if (!fs::is_regular_file(testPath))
				{
					std::cerr << KRED "[BREAKING] Onion build tool run in a directory without \"" << BUILD_LIST_NAME << "\", specify path to a valid build list via -module\n";
					return 1;
				}
				else
				{
					buildListPath = fs::weakly_canonical(testPath.make_preferred());
				}
			}
		}
		else
		{
			modulePath = fs::weakly_canonical(fs::absolute(fs::path(str).make_preferred()));

			const auto moduleFilePath = (modulePath / MODULE_MANIFEST_NAME).make_preferred();
			if (!fs::is_regular_file(moduleFilePath))
			{
				std::cerr << KRED << "[BREAKING] Module directory " << modulePath << " does not contain '" << MODULE_MANIFEST_NAME << "' file\n" << RST;
				return false;
			}

			buildListPath = fs::weakly_canonical(fs::absolute(fs::path(str).make_preferred() / BUILD_LIST_NAME));
			if (!fs::is_regular_file(buildListPath))
			{
				std::cerr << KRED << "[BREAKING] Directory " << buildListPath << " does not contain '" << BUILD_LIST_NAME << "' file\n" << RST;
				return 1;
			}
		}
	}

	std::cout << "Using module at " << modulePath << "\n";
	std::cout << "Using build list at " << buildListPath << "\n";

	//--

	const auto moduleConfigPath = modulePath / CONFIGURATION_NAME;
	const auto moduleConfig = std::unique_ptr<ModuleConfigurationManifest>(ModuleConfigurationManifest::Load(moduleConfigPath));
	if (!moduleConfig)
	{
		const auto moduleDefinitionPath = modulePath / MODULE_MANIFEST_NAME;
		if (fs::is_regular_file(moduleDefinitionPath))
			std::cerr << KRED << "[BREAKING] Module at \"" << modulePath << "\" was not configured, run:\nonion configure -module=\"" << modulePath << "\"\n" << RST;
		else
			std::cerr << KRED << "[BREAKING] Directory \"" << modulePath << "\" does not contain properly configured module (or any module to be fair)\n" << RST;
		return 1;
	}

	const bool verifyVersions = !cmdline.has("noverify");

	ModuleRepository modules;
	if (!modules.installConfiguredModules(*moduleConfig, verifyVersions))
	{
		std::cerr << KRED << "[BREAKING] Failed to verify configured module at \"" << modulePath << "\"\n" << RST;
		return 1;
	}

	//--

	auto platform = DefaultPlatform();

	{
		const auto str = cmdline.get("platform");
		if (!str.empty())
		{
			if (!ParsePlatformType(str, platform))
			{
				std::cerr << KRED "[BREAKING] Unknown platform \"" << str << "\"\n" << RST;
				std::cout << "Valid platforms are : " << PrintEnumOptions(DefaultPlatform()) << "\n";
				return 1;
			}
		}
	}

	//--

	const auto buildList = ConfigurationList::Load(buildListPath);
	if (!buildList)
	{
		std::cerr << KRED "[BREAKING] Failed to load build list from " << buildListPath << "\n";
		return 1;
	}

	// collect configurations for selected platform
	std::vector<Configuration> buildConfigurations;
	buildList->collect(platform, buildConfigurations);
	if (buildConfigurations.empty())
	{
		std::cout << KYEL "[WARNING] Nothing to build for platform \"" << NameEnumOption(platform) << "\"\n";
		return 0;
	}

	//--

	bool valid = true;
	for (const auto& config : buildConfigurations)
	{
		valid &= RunTestsForConfiguration(modules, config, cmdline);
	}

	if (!valid)
	{
		std::cerr << KRED "[BREAKING] Some tests failed\n" << RST;
		return 1;
	}

	//--
	
	return 0;
}

//--