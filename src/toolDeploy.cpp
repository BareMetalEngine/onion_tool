#include "common.h"
#include "toolDeploy.h"
#include "configuration.h"
#include "moduleConfiguration.h"
#include "moduleRepository.h"
#include "projectCollection.h"
#include "projectManifest.h"
#include "externalLibraryRepository.h"
#include "moduleManifest.h"
#include "externalLibrary.h"

#pragma optimize("", off)

//--

extern bool ProjectBinaryName(const ProjectInfo* project, std::string& outName);

//--

struct ProjectDeployFile
{
    fs::path sourcePath;
    fs::path destinationPath;
};

class ProjectDeployExplorer
{
public:
    ProjectDeployExplorer(const ProjectCollection& collection, const fs::path& deployPath, const fs::path& binaryPath)
        : m_collection(collection)
        , m_deployPath(deployPath)
        , m_binaryPath(binaryPath)
    {}

    bool exploreApp(std::string_view name)
    {
        const auto* project = m_collection.findProject(name);
        if (!project) {
			std::cerr << KRED << "[BREAKING] Unable to find deployable application '" << name << "'\n" << RST;
			return false;
        }

        if (project->manifest->type != ProjectType::Application)
        {
			std::cerr << KRED << "[BREAKING] Project '" << name << "' is not a deployable application\n" << RST;
			return false;
        }

        exploreProject(project);
        return true;
    }

    void printStats() const
    {
		std::cout << "Collected " << m_projectsToDeploy.size() << " projects to deploy from " << m_modulesToDeploy.size() << " modules\n";
		std::cout << "Collected " << m_librariesToDeploy.size() << " libraries to deploy\n";
    }

    bool collectFiles(std::vector<ProjectDeployFile>& outFiles) const
    {
        bool valid = true;

        for (const auto* dep : m_modulesToDeploy)
            valid &= collectFilesFromModule(dep, outFiles);

		for (const auto* dep : m_projectsToDeploy)
			valid &= collectFilesFromProject(dep, outFiles);

		for (const auto* dep : m_librariesToDeploy)
			valid &= collectFilesFromLibrary(dep, outFiles);

        return valid;
    }

private:
    const ProjectCollection& m_collection;
    fs::path m_deployPath;
    fs::path m_binaryPath;

	std::unordered_set<const ModuleManifest*> m_modulesToDeploy;
	std::unordered_set<const ProjectInfo*> m_projectsToDeploy;
	std::unordered_set<const ExternalLibraryManifest*> m_librariesToDeploy;

	void exploreProject(const ProjectInfo* project)
	{
		// skip test apps, even if directly referenced somehow
		if (project->manifest->type == ProjectType::TestApplication)
			return;

		// add only once
		if (!m_projectsToDeploy.insert(project).second)
			return;

		// collect modules of projects (mostly for data lists)
		if (project->parentModule)
			m_modulesToDeploy.insert(project->parentModule);

		// explore dependencies
		for (const auto* dep : project->resolvedDependencies)
			exploreProject(dep);

		// collect libraries
		for (const auto* dep : project->resolvedLibraryDependencies)
			m_librariesToDeploy.insert(dep);
	}

    bool collectFilesFromDirectory(const fs::path& sourcePath, const fs::path& destPath, std::vector<ProjectDeployFile>& outFiles) const
    {
		bool valid = true;

		try
		{
			if (fs::is_directory(sourcePath))
			{
				for (const auto& entry : fs::directory_iterator(sourcePath))
				{
					const auto name = entry.path().filename().u8string();

                    if (entry.is_directory())
                    {
                        const auto childDestPath = (destPath / name).make_preferred();
                        valid &= collectFilesFromDirectory(entry.path(), childDestPath, outFiles);
                    }
                    else if (entry.is_regular_file())
                    {
                        ProjectDeployFile info;
                        info.sourcePath = entry.path();
                        info.destinationPath = (destPath / name).make_preferred();
                        outFiles.push_back(info);
                    }
				}
			}
		}
		catch (fs::filesystem_error& e)
		{
			std::cout << "Filesystem Error: " << e.what() << "\n";
			valid = false;
		}

		return valid;
    }

    bool collectFilesFromModule(const ModuleManifest* info, std::vector<ProjectDeployFile>& outFiles) const
    {
        bool valid = true;

        for (const auto& data : info->moduleData)
        {
            if (data.published)
            {
                if (BeginsWith(data.mountPath, "/"))
                {
                    const auto safeMountPath = std::string(data.mountPath.c_str() + 1);

                    const auto destinationDirectory = (m_deployPath / safeMountPath).make_preferred();
                    std::cout << "Found module mount directory '" << data.mountPath << "' that will be deployed as " << destinationDirectory << "\n";
                    valid &= collectFilesFromDirectory(data.sourcePath, destinationDirectory, outFiles);
                }
            }
        }

        return valid;
    }

	bool collectFilesFromProject(const ProjectInfo* info, std::vector<ProjectDeployFile>& outFiles) const
	{
        std::string executableName;
        if (ProjectBinaryName(info, executableName))
        {
            const auto executablePath = m_binaryPath / executableName;

            if (!fs::is_regular_file(executablePath))
            {
				std::cerr << KRED << "[BREAKING] Project '" << info->name << "' is missing executable " << executablePath << "\n" << RST;
                return false;
            }

            ProjectDeployFile deployInfo;
            deployInfo.sourcePath = executablePath;
            deployInfo.destinationPath = (m_deployPath / executableName).make_preferred();
            outFiles.push_back(deployInfo);
        }

		return true;
	}

	bool collectFilesFromLibrary(const ExternalLibraryManifest* lib, std::vector<ProjectDeployFile>& outFiles) const
	{
        for (const auto& file : lib->deployFiles)
        {
			ProjectDeployFile deployInfo;
			deployInfo.sourcePath = file.absoluteSourcePath;
			deployInfo.destinationPath = (m_deployPath / file.relativeDeployPath).make_preferred();
			outFiles.push_back(deployInfo);
        }

        return true;
	}
};

//--

ToolDeploy::ToolDeploy()
{}

void ToolDeploy::printUsage(const char* argv0)
{
	Configuration cfg;
	Commandline cmdLine;
	cfg.parseOptions(argv0, cmdLine);

    std::cout << KBOLD << "onion deploy [options]\n" << RST;
    std::cout << "\n";
    std::cout << "Build configuration options (only selected configuration is deployed):\n";
	std::cout << "  -platform=" << PrintEnumOptions(cfg.platform) << "\n";
    std::cout << "  -config=" << PrintEnumOptions(cfg.configuration) << "\n";
	std::cout << "  -generator=" << PrintEnumOptions(cfg.generator) << "\n";
    std::cout << "  -libs=" << PrintEnumOptions(cfg.libs) << "\n";
    std::cout << "  -build=" << PrintEnumOptions(cfg.build) << "\n";
    std::cout << "\n";
    std::cout << "General options:\n";
    std::cout << "  -deployDir=<where all stuff is copied to>\n";
    std::cout << "  -app=<application project to deploy>\n";
	std::cout << "\n";

	std::cout << "Current configuration (if no arguments given): " << KBOLD << KGRN << cfg.mergedName() << RST << "\n";
	std::cout << "\n";
}

int ToolDeploy::run(const char* argv0, const Commandline& cmdline)
{
    //--

    Configuration config;
    if (!config.parseOptions(argv0, cmdline)) {
        std::cerr << KRED << "[BREAKING] Invalid/incomplete configuration\n" << RST;
        return 1;
    }

	if (!config.parsePaths(argv0, cmdline)) {
		std::cerr << KRED << "[BREAKING] Invalid/incomplete configuration\n" << RST;
		return 1;
	}

    //--

	fs::path deployDir;
	{
		const auto& str = cmdline.get("deployDir");
		if (str.empty())
		{
			//std::cout << "No deploy directory specified, using default one\n";

			std::string solutionPartialPath = ".deploy/";
			solutionPartialPath += config.mergedName();

			deployDir = config.modulePath / solutionPartialPath;
		}
		else
		{
            deployDir = fs::weakly_canonical(fs::absolute(fs::path(str).make_preferred()));
		}

        deployDir.make_preferred();

		std::error_code ec;
		if (!fs::is_directory(deployDir, ec))
		{
			if (!fs::create_directories(deployDir, ec))
			{
				std::cerr << KRED << "[BREAKING] Failed to create deploy directory " << deployDir << "\n" << RST;
				return false;
			}
		}
	}

    //--

    std::cout << "Running with configuration " << config.mergedName() << "\n";
    std::cout << "Deploying to " << deployDir << "\n";

    //--

    std::vector<std::string> applicationsToDeploy = cmdline.getAll("app");
    if (applicationsToDeploy.empty())
    {
        std::cerr << KRED << "[BREAKING] Please specify application(s) to deploy with the -app argument. Auto-deploying everything is not supported (to much noise).\n" << RST;
        return false;
    }

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

	ExternalLibraryReposistory libraries(config.tempPath / "cache", config.platform);
	if (!structure.resolveLibraries(libraries))
	{
		std::cerr << KRED << "[BREAKING] Failed to resolve third party libraries\n" << RST;
		return 1;
	}

    //--

    ProjectDeployExplorer deployList(structure, deployDir, config.binaryPath);
    for (const auto name : applicationsToDeploy)
        if (!deployList.exploreApp(name))
            return 1;

    std::vector<ProjectDeployFile> deployFiles;
    if (!deployList.collectFiles(deployFiles))
        return 1;

    std::cout << "Collected " << deployFiles.size() << " actual files to deploy\n";

    uint32_t numCopied = 0;
    for (const auto& file : deployFiles)
    {
        bool actuallyCopied = false;
        if (!CopyNewerFile(file.sourcePath, file.destinationPath, &actuallyCopied))
        {
			std::cerr << KRED << "[BREAKING] Failed to copy file " << file.sourcePath << " to " << file.destinationPath << "\n" << RST;
			return 1;
        }

        if (actuallyCopied)
            numCopied += 1;
    }

    std::cout << "Copied " << numCopied << " files out of " << deployFiles.size() << " files collected\n";
    return 0;
}

//--
