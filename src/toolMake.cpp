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
    std::cout << KBOLD << "onion generate [options]\n" << RST;
    std::cout << "\n";
    std::cout << "General options:\n";
	std::cout << "  -module=<path to configured module directory>\n";
	std::cout << "  -build=<build configuration string>\n";
    std::cout << "  -tempDir=<custom temporary directory>\n";
    std::cout << "  -cacheDir=<custom library/module cache directory>\n";
	std::cout << "\n";
}

int ToolMake::run(const Commandline& cmdline)
{
    //--

    Configuration config;
    if (!Configuration::Parse(cmdline, config))
        return 1;   

    //--

    std::cout << "Running with configuration " << config.mergedName() << "\n";

    //--

    const auto moduleConfigPath = config.platformConfigurationFile();
    const auto moduleConfig = std::unique_ptr<ModuleConfigurationManifest>(ModuleConfigurationManifest::Load(moduleConfigPath));
    if (!moduleConfig)
    {
        std::cerr << KRED << "[BREAKING] Unable to load platform configuration from " << moduleConfigPath << ", run \"onion configure\" to properly configure the environment before generating any projects\n" << RST;
		return 1;
    }

    const bool verifyVersions = !cmdline.has("noverify");

    ModuleRepository modules;
    if (!modules.installConfiguredModules(*moduleConfig, verifyVersions))
    {
        std::cerr << KRED << "[BREAKING] Failed to verify configured module at \"" << config.modulePath << "\"\n" << RST;
        return 1;
    }

    //--

    ProjectCollection structure;
    if (!structure.populateFromModules(modules.modules(), config))
    {
        std::cerr << KRED << "[BREAKING] Failed to populate project structure from installed modules\n" << RST;
        return 1;
    }

    if (!structure.filterProjects(config))
    {
        std::cerr << KRED << "[BREAKING] Failed to filter project structure\n" << RST;
        return 1;
    }

    if (!structure.resolveDependencies(config))
    {
        std::cerr << KRED << "[BREAKING] Failed to resolve project internal dependencies\n" << RST;
        return 1;
    }

    //--

	ExternalLibraryReposistory libraries;
    if (!libraries.installConfiguredLibraries(*moduleConfig))
    {
		std::cerr << KRED << "[BREAKING] Failed to install third party libraries\n" << RST;
		return 1;
    }

	if (!structure.resolveLibraries(libraries))
	{
		std::cerr << KRED << "[BREAKING] Failed to resolve third party libraries\n" << RST;
		return 1;
	}

    if (!libraries.deployFiles(config.derivedBinaryPath))
    {
        std::cerr << KRED << "[BREAKING] Failed to deploy library files\n" << RST;
        return 1;
    }

    //--

    uint32_t totalFiles = 0;
    if (!structure.scanContent(totalFiles))
    {
        std::cerr << KRED << "[BREAKING] Failed to scan projects content\n" << RST;
        return 1;
    }

    std::cout << "Found " << totalFiles << " total file(s) across " << structure.projects().size() << " project(s) from " << modules.modules().size() << " module(s)\n";

    //--

    FileRepository fileRepository;
    if (!fileRepository.initialize(config.executablePath, config.tempPath))
    {
		std::cerr << KRED << "[BREAKING] Failed to initialize file repository\n" << RST;
		return 1;
    }

    //--

    auto mainModuleName = config.modulePath.filename().u8string();
    if (mainModuleName.empty())
        mainModuleName = "onion";

    auto codeGenerator = CreateSolutionGenerator(fileRepository, config, mainModuleName);
    if (!codeGenerator)
    {
		std::cerr << KRED << "[BREAKING] Failed to create code generator\n" << RST;
		return 1;
    }

    if (!codeGenerator->extractProjects(structure))
    {
		std::cerr << KRED << "[BREAKING] Failed to extract project structure into code generator\n" << RST;
		return 1;
    }

    //--

    FileGenerator files;
    if (!codeGenerator->generateAutomaticCode(files))
    {
		std::cerr << KRED << "[BREAKING] Failed to generate automatic code\n" << RST;
		return 1;
    }

	if (!codeGenerator->generateSolution(files))
	{
		std::cerr << KRED << "[BREAKING] Failed to generate solution\n" << RST;
		return 1;
	}

	if (!codeGenerator->generateProjects(files))
	{
		std::cerr << KRED << "[BREAKING] Failed to generate projects\n" << RST;
		return 1;
	}

    //--

    if (!files.saveFiles())
    {
        std::cerr << KRED << "[BREAKING] Failed to save files\n" << RST;
        return 1;
    }

    //--

    return 0;
}

//--
