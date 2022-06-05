#include "common.h"
#include "toolMake.h"
#include "toolReflection.h"
#include "fileGenerator.h"
#include "fileRepository.h"
#include "configuration.h"
#include "configurationList.h"
#include "configurationInteractive.h"
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

void ToolMake::printUsage(const char* argv0)
{
	Configuration cfg;
	Commandline cmdLine;
	cfg.parseOptions(argv0, cmdLine);

    std::cout << KBOLD << "onion make [options]\n" << RST;
    std::cout << "\n";
    std::cout << "Build configuration options:\n";
	std::cout << "  -platform=" << PrintEnumOptions(cfg.platform) << "\n";
    std::cout << "  -config=" << PrintEnumOptions(cfg.configuration) << "\n";
	std::cout << "  -generator=" << PrintEnumOptions(cfg.generator) << "\n";
    std::cout << "  -libs=" << PrintEnumOptions(cfg.libs) << "\n";
    std::cout << "  -build=" << PrintEnumOptions(cfg.build) << "\n";
    std::cout << "\n";
    std::cout << "General options:\n";
    std::cout << "  -module=<path to configured module directory>\n";
    std::cout << "  -buildDir=<general build directory where all the build files are stored>\n";
    std::cout << "  -deployDir=<path where all final executables are written - if not specified a %{buildDir}/.bin/ is used>\n";
    std::cout << "  -outDir=<path where all temporary build files are written - if not specified a %{buildDir}/.temp/ is used>\n";    
	std::cout << "\n";

	std::cout << "Current configuration (if no arguments given): " << KBOLD << KGRN << cfg.mergedName() << RST << "\n";
	std::cout << "\n";
}

int ToolMake::run(const char* argv0, const Commandline& cmdline)
{
    //--

    Configuration config;
    if (!config.parseOptions(argv0, cmdline)) {
        std::cerr << KRED << "[BREAKING] Invalid/incomplete configuration\n" << RST;
        return 1;
    }

    if (cmdline.has("interactive"))
        if (!RunInteractiveConfig(config))
            return false;

    if (!config.parsePaths(argv0, cmdline)) {
        std::cerr << KRED << "[BREAKING] Invalid/incomplete configuration\n" << RST;
        return 1;
    }

    //--

    std::cout << "Running with configuration " << config.mergedName() << "\n";

    //--

    const auto moduleConfigPath = config.modulePath / CONFIGURATION_NAME;
    const auto moduleConfig = std::unique_ptr<ModuleConfigurationManifest>(ModuleConfigurationManifest::Load(moduleConfigPath));
    if (!moduleConfig)
    {
        const auto moduleDefinitionPath = config.modulePath / MODULE_MANIFEST_NAME;
        if (fs::is_regular_file(moduleDefinitionPath))
            std::cerr << KRED << "[BREAKING] Module at \"" << config.modulePath << "\" was not configured, run:\nonion configure -module=\"" << config.modulePath << "\"\n" << RST;
        else
			std::cerr << KRED << "[BREAKING] Directory \"" << config.modulePath << "\" does not contain properly configured module (or any module to be fair)\n" << RST;
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

    ExternalLibraryReposistory libraries(config.tempPath / "cache");
    //libraries.installLibraryPack()
    /*if (!modules.installLibraries(libraries))
    {
        std::cerr << KRED << "[BREAKING] Failed to install libraries needed by the used modules\n" << RST;
        return -1;
    }*/

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

    if (!structure.resolveDependencies(libraries, config))
    {
        std::cerr << KRED << "[BREAKING] Failed to filter project structure\n" << RST;
        return 1;
    }

    //--

    uint32_t totalFiles = 0;
    if (!structure.scanContent(totalFiles))
    {
        std::cerr << KRED << "[BREAKING] Failed to scan project's content\n" << RST;
        return 1;
    }

    std::cout << "Found " << totalFiles << " total file(s) across " << structure.projects().size() << " project(s) from " << modules.modules().size() << " module(s)\n";

    //--

    /*if (!libraries.deployFiles(config.deployPath))
    {
		std::cerr << KRED << "[BREAKING] Failed to deploy library files\n" << RST;
		return 1;
    }*/

    //--

    FileRepository fileRepository;
    if (!fileRepository.initialize(config.builderExecutablePath, config.tempPath))
    {
		std::cerr << KRED << "[BREAKING] Failed to initialize file repository\n" << RST;
		return 1;
    }

    //--

	auto codeGenerator = CreateSolutionGenerator(fileRepository, config, moduleConfig->name);
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

    {
        const auto buildListFile = config.modulePath / BUILD_LIST_NAME;
        const auto configs = ConfigurationList::Load(buildListFile);

        if (configs->append(config))
        {
            if (!configs->save(buildListFile))
            {
                std::cerr << KYEL << "[WARNING] Failed to save build list " << buildListFile << "\n" << RST;
            }
        }
    }

    //--

    return 0;
}

//--
