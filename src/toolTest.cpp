#include "common.h"
#include "utils.h"
#include "toolTest.h"
#include "moduleConfiguration.h"
#include "moduleRepository.h"
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

	outPath = (config.derivedBinaryPath / executableName).make_preferred();
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

int ToolTest::run(const Commandline& cmdline)
{
	//--

	Configuration config;
	if (!Configuration::Parse(cmdline, config))
		return 1;

	//--

	const auto moduleConfigPath = config.platformConfigurationFile();
	const auto moduleConfig = std::unique_ptr<ModuleConfigurationManifest>(ModuleConfigurationManifest::Load(moduleConfigPath));
	if (!moduleConfig)
	{
		std::cerr << KRED << "[BREAKING] Unable to load platform configuration from " << moduleConfigPath << ", run \"onion configure\" to properly configure the environment before generating any projects\n" << RST;
		return 1;
	}

	ModuleRepository modules;
	if (!modules.installConfiguredModules(*moduleConfig, false))
	{
		std::cerr << KRED << "[BREAKING] Failed to verify configured module at \"" << moduleConfigPath << "\"\n" << RST;
		return 1;
	}


	//--

	if (!RunTestsForConfiguration(modules, config, cmdline))
	{
		std::cerr << KRED "[BREAKING] Some tests failed\n" << RST;
		return 1;
	}

	//--
	
	return 0;
}

//--