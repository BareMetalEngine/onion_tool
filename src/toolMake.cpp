#include "common.h"
#include "toolMake.h"
#include "toolReflection.h"
#include "fileGenerator.h"
#include "fileRepository.h"
#include "configuration.h"
#include "project.h"
#include "projectManifest.h"
#include "projectCollection.h"
#include "externalLibrary.h"
#include "externalLibraryRepository.h"
#include "moduleRepository.h"
#include "solutionGeneratorVS.h"
#include "solutionGeneratorCMAKE.h"
#include "moduleConfiguration.h"

//--

static std::unique_ptr<SolutionGenerator> CreateSolutionGenerator(FileRepository& files, const Configuration& config, std::string_view mainModuleName)
{
    if (config.generator == GeneratorType::VisualStudio22 || config.generator == GeneratorType::VisualStudio19)
        return std::make_unique<SolutionGeneratorVS>(files, config, mainModuleName);
    else if (config.generator == GeneratorType::CMake)
        return std::make_unique<SolutionGeneratorCMAKE>(files, config, mainModuleName);
    else
        return nullptr;
}

//--

ToolMake::ToolMake()
{}

void ToolMake::printUsage()
{
    LogInfo() << "onion generate [options]";
    LogInfo() << "";
    LogInfo() << "General options:";
	LogInfo() << "  -module=<path to configured module directory>";
	LogInfo() << "  -build=<build configuration string>";
    LogInfo() << "  -tempDir=<custom temporary directory>";
    LogInfo() << "  -cacheDir=<custom library/module cache directory>";
	LogInfo() << "";
}

int ToolMake::run(const Commandline& cmdline)
{
    //--

    Configuration config;
    if (!Configuration::Parse(cmdline, config))
        return 1;   

    //--

    LogInfo() << "Running with configuration " << config.mergedName();

    //--

    const auto moduleConfigPath = config.platformConfigurationFile();
    const auto moduleConfig = std::unique_ptr<ModuleConfigurationManifest>(ModuleConfigurationManifest::Load(moduleConfigPath));
    if (!moduleConfig)
    {
        LogError() << "Unable to load platform configuration from " << moduleConfigPath << ", run \"onion configure\" to properly configure the environment before generating any projects";
		return 1;
    }

    const bool verifyVersions = !cmdline.has("noverify");

    ModuleRepository modules;
    if (!modules.installConfiguredModules(*moduleConfig, verifyVersions))
    {
        LogError() << "Failed to verify configured module at \"" << config.moduleFilePath << "\"";
        return 1;
    }

    //--

    ProjectCollection structure;
    if (!structure.populateFromModules(modules.modules(), config))
    {
        LogError() << "Failed to populate project structure from installed modules";
        return 1;
    }

    if (!structure.filterProjects(config))
    {
        LogError() << "Failed to filter project structure";
        return 1;
    }

    if (!structure.resolveDependencies(config))
    {
        LogError() << "Failed to resolve project internal dependencies";
        return 1;
    }

    //--

	ExternalLibraryReposistory libraries;
    if (!libraries.installConfiguredLibraries(*moduleConfig))
    {
		LogError() << "Failed to install third party libraries";
		return 1;
    }

	if (!structure.resolveLibraries(libraries))
	{
		LogError() << "Failed to resolve third party libraries";
		return 1;
	}

    for (const auto& configType : CONFIGURATIONS)
    {
        const auto configurationName = NameEnumOption(configType);
        const auto binaryFolder = (config.derivedBinaryPathBase / ToLower(configurationName)).make_preferred();

		if (!libraries.deployFiles(configType, binaryFolder))
		{
			LogError() << "Failed to deploy library files";
			return 1;
		}
    }

    //--

    uint32_t totalFiles = 0;
    if (!structure.scanContent(totalFiles))
    {
        LogError() << "Failed to scan projects content";
        return 1;
    }

    LogInfo() << "Found " << totalFiles << " total file(s) across " << structure.projects().size() << " project(s) from " << modules.modules().size() << " module(s)";

    //--

    FileRepository fileRepository;
    if (!fileRepository.initialize(config.executablePath, config.tempPath))
    {
		LogError() << "Failed to initialize file repository";
		return 1;
    }

    //--

    auto mainModuleName = moduleConfig->solutionName;
	if (mainModuleName.empty())
	{
		LogError() << "No solution name specified";
		return 1;
	}

    auto codeGenerator = CreateSolutionGenerator(fileRepository, config, mainModuleName);
    if (!codeGenerator)
    {
		LogError() << "Failed to create code generator";
		return 1;
    }

    if (!codeGenerator->extractProjects(structure))
    {
		LogError() << "Failed to extract project structure into code generator";
		return 1;
    }

    //--

    FileGenerator files;
    if (!codeGenerator->generateAutomaticCode(files))
    {
		LogError() << "Failed to generate automatic code";
		return 1;
    }

	if (!codeGenerator->generateSolution(files))
	{
		LogError() << "Failed to generate solution";
		return 1;
	}

	if (!codeGenerator->generateProjects(files))
	{
		LogError() << "Failed to generate projects";
		return 1;
	}

    //--

    if (!files.saveFiles(false))
    {
        LogError() << "Failed to save files";
        return 1;
    }

    //--

    LogSuccess() << "Solution file generated";
    return 0;
}

//--
