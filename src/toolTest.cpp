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
	LogInfo() << "onion test [options]";
	LogInfo() << "";
	LogInfo() << "General options:";
	LogInfo() << "  -module=<path to module to configure>";
	LogInfo() << "  -parallel - run all tests in parallel";
	LogInfo() << "  -fastfail - stop after first failure";
	LogInfo() << "  -config=<release|debug|final|profile|checked> - configuration to run (defaults to Release)";
	LogInfo() << "";
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

        default:
            break;
	}

	return false;
}

static bool ProjectBinaryPath(const ProjectInfo* project, const Configuration& config, const std::string& runtimeConfig, fs::path& outPath)
{
	std::string executableName;
	if (!ProjectBinaryName(project, executableName))
		return false;

	outPath = (config.derivedBinaryPathBase / ToLower(runtimeConfig) / executableName).make_preferred();
	return true;
}

static bool RunTestsForConfiguration(const ModuleRepository& modules, const Configuration& config, const Commandline& cmdLine)
{
	// configuration
	std::string runtimeConfig(cmdLine.get("config", "Release"));
	LogInfo() << "Running tests for configuration '" << config.mergedName() << "' at runtime config '" << runtimeConfig << "'";

	// populate project structure
	ProjectCollection structure;
	if (!structure.populateFromModules(modules.modules(), config))
	{
		LogError() << "Failed to populate project structure from installed modules";
		return false;
	}

	if (!structure.filterProjects(config))
	{
		LogError() << "Failed to filter project structure";
		return false;
	}

	std::vector<const ProjectInfo*> testProjects;
	for (const auto* proj : structure.projects())
		if (IsTestableProject(proj))
			testProjects.push_back(proj);

	if (testProjects.empty())
	{
		LogInfo() << "No test projects found in configuration";
		return true;
	}

	LogInfo() << "Collected " << testProjects.size() << " projects for testing";

	bool valid = true;
	for (const auto* proj : testProjects)
	{
		fs::path binaryPath;
		if (ProjectBinaryPath(proj, config, runtimeConfig, binaryPath))
		{
			const auto binaryDirectory = binaryPath.parent_path();

			if (!fs::is_regular_file(binaryPath))
			{
				LogError() << "Failed to find binary for project '" << proj->name << "' at " << binaryPath;
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
				LogError() << "Test for project '" << proj->name << "' failed!";
				valid = false;
				continue;
			}
			else
			{
				LogSuccess() << "Test for project '" << proj->name << "' succeeded!";
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
		LogError() << "Unable to load platform configuration from " << moduleConfigPath << ", run \"onion configure\" to properly configure the environment before generating any projects";
		return 1;
	}

	ModuleRepository modules;
	if (!modules.installConfiguredModules(*moduleConfig, false))
	{
		LogError() << "Failed to verify configured module at \"" << moduleConfigPath << "\"";
		return 1;
	}


	//--

	if (!RunTestsForConfiguration(modules, config, cmdline))
	{
		LogError() << "Some tests failed";
		return 1;
	}

	//--
	
	return 0;
}

//--