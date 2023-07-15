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
			LogError() << "Unable to find deployable application '" << name << "'";
			return false;
        }

        if (project->manifest->type != ProjectType::Application)
        {
			LogError() << "Project '" << name << "' is not a deployable application";
			return false;
        }

        exploreProject(project);
        return true;
    }

    void printStats() const
    {
		LogInfo() << "Collected " << m_projectsToDeploy.size() << " projects to deploy from " << m_modulesToDeploy.size() << " modules";
		LogInfo() << "Collected " << m_librariesToDeploy.size() << " libraries to deploy";
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
			LogInfo() << "Filesystem Error: " << e.what();
			valid = false;
		}

		return valid;
    }

    bool collectFilesFromModule(const ModuleManifest* info, std::vector<ProjectDeployFile>& outFiles) const
    {
        bool valid = true;

        for (const auto& data : info->moduleData)
        {
            //if (data.published)
            {
                if (BeginsWith(data.mountPath, "/"))
                {
                    const auto safeMountPath = std::string(data.mountPath.c_str() + 1);

                    const auto destinationDirectory = (m_deployPath / safeMountPath).make_preferred();
                    LogInfo() << "Found module mount directory '" << data.mountPath << "' that will be deployed as " << destinationDirectory;
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
				LogError() << "Project '" << info->name << "' is missing executable " << executablePath;
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
        /*for (const auto& file : lib->deployFiles)
        {
			ProjectDeployFile deployInfo;
			deployInfo.sourcePath = file.absoluteSourcePath;
			deployInfo.destinationPath = (m_deployPath / file.relativeDeployPath).make_preferred();
			outFiles.push_back(deployInfo);
        }*/

        return true;
	}
};

//--

ToolDeploy::ToolDeploy()
{}

void ToolDeploy::printUsage()
{
    LogInfo() << "onion deploy [options]";
    LogInfo() << "";
	LogInfo() << "General options:";
	LogInfo() << "  -module=<path to configured module directory>";
	LogInfo() << "  -app=<application project to deploy>";
	LogInfo() << "  -build=<build configuration string>";
	LogInfo() << "  -tempDir=<custom temporary directory>";
	LogInfo() << "  -cacheDir=<custom library/module cache directory>";
    LogInfo() << "  -deployDir=<where all stuff is copied to>";
	LogInfo() << "";
}

int ToolDeploy::run(const Commandline& cmdline)
{
    //--

    Configuration config;
    if (!Configuration::Parse(cmdline, config))
		return 1;

    //--

	fs::path deployDir;
	{
		const auto& str = cmdline.get("deployDir");
		if (str.empty())
            deployDir = (config.derivedConfigurationPathBase / "deploy").make_preferred();
		else
            deployDir = fs::weakly_canonical(fs::absolute(fs::path(str).make_preferred()));

		std::error_code ec;
		if (!fs::is_directory(deployDir, ec))
		{
			if (!fs::create_directories(deployDir, ec))
			{
				LogError() << "Failed to create deploy directory " << deployDir;
				return false;
			}
		}
	}

    //--

    LogInfo() << "Running with configuration " << config.mergedName();
    LogInfo() << "Deploying to " << deployDir;

    //--

    std::vector<std::string> applicationsToDeploy = cmdline.getAll("app");
    if (applicationsToDeploy.empty())
    {
        LogError() << "Please specify application(s) to deploy with the -app argument. Auto-deploying everything is not supported (to much noise).";
        return false;
    }

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
		LogError() << "Failed to install configured third party libraries";
		return 1;
    }

	if (!structure.resolveLibraries(libraries))
	{
		LogError() << "Failed to resolve third party libraries";
		return 1;
	}

    //--

    ProjectDeployExplorer deployList(structure, deployDir, config.derivedBinaryPathBase);
    for (const auto name : applicationsToDeploy)
        if (!deployList.exploreApp(name))
            return 1;

    std::vector<ProjectDeployFile> deployFiles;
    if (!deployList.collectFiles(deployFiles))
        return 1;

    LogInfo() << "Collected " << deployFiles.size() << " actual files to deploy";

    uint32_t numCopied = 0;
    for (const auto& file : deployFiles)
    {
        bool actuallyCopied = false;
        if (!CopyNewerFile(file.sourcePath, file.destinationPath, &actuallyCopied))
        {
			LogError() << "Failed to copy file " << file.sourcePath << " to " << file.destinationPath;
			return 1;
        }

        if (actuallyCopied)
            numCopied += 1;
    }

    LogInfo() << "Copied " << numCopied << " files out of " << deployFiles.size() << " files collected";
    return 0;
}

//--
