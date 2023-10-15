#include "common.h"
#include "configuration.h"
#include "moduleManifest.h"
#include "projectManifest.h"
#include "utils.h"
#include "aws.h"
#include "toolConfigure.h"
#include "moduleConfiguration.h"
#include "externalLibrary.h"
#include "externalLibraryInstaller.h"

//--

ModuleResolver::ModuleInfo::~ModuleInfo()
{
	delete manifest;
}

//--

ModuleResolver::ModuleResolver(const fs::path& cachePath)
	: m_cachePath(cachePath)
{
}

ModuleResolver::~ModuleResolver()
{
	for (auto it : m_localModules)
		delete it.second;

	for (auto it : m_remoteModules)
		delete it.second;

	for (auto it : m_modulesByGuid)
		delete it.second;

	for (auto it : m_downloadedRepositories)
		delete it.second;
}

static bool ExploreProjectDependencies(const ProjectManifest* proj, const std::unordered_map<std::string, const ProjectManifest*>& projectMap, std::unordered_set<const ProjectManifest*>& outUsedProjects)
{
	// only visit project once
	if (outUsedProjects.insert(proj).second)
		return true;

	// explore dependencies
	bool valid = true;
	for (const auto& dep : proj->dependencies)
	{
		const auto it = projectMap.find(dep);
		if (it == projectMap.end())
		{
			LogError() << "Project '" << proj->rootPath << "' has dependency on '" << dep << "' that could not be find in any loaded modules";
			valid = false;
		}
		else
		{
			valid &= ExploreProjectDependencies(it->second, projectMap, outUsedProjects);
		}
	}

	// explore optional dependencies (but to not fail if they don't exist)
	for (const auto& dep : proj->dependencies)
	{
		const auto it = projectMap.find(dep);
		if (it == projectMap.end())
		{
			LogWarning() << "Project '" << proj->rootPath << "' has OPTIONAL dependency on '" << dep << "' that could not be find in any loaded modules";
		}
		else
		{
			auto copiedUsedProjects = outUsedProjects;
			if (ExploreProjectDependencies(it->second, projectMap, copiedUsedProjects))
			{
				outUsedProjects = copiedUsedProjects;
			}
			else
			{
				LogWarning() << "Project '" << dep << "' that was OPTIONAL dependency of '" << proj->rootPath << "' has missing it's dependencies and will not be remembered as a dependency";
			}
		}
	}

	return valid;

}

void ModuleResolver::collectLibrarySources(std::vector<ModuleLibrarySource>* outSources) const
{
	for (const auto& dep : m_modulesByGuid)
	{
		const auto* moduleInfo = dep.second;

		for (const auto& source : moduleInfo->manifest->librarySources)
			outSources->push_back(source);
	}
}

bool ModuleResolver::exportToManifest(ModuleConfigurationManifest& cfg) const
{
	assert(!cfg.rootPath.empty());

	// we must have a valid solution name
	if (m_solutionName.empty())
	{
		LogError() << "No solution name specified. At least one module must speiciy the <GlobalSolutionName>xxx</GlobalSolutionName>";
		return false;
	}

	// use the defined solution name
	cfg.solutionName = m_solutionName;

	// gather projects and modules
	std::unordered_map<std::string, const ProjectManifest*> projectsByName;

	{
		for (const auto& dep : m_modulesByGuid)
		{
			const auto* moduleInfo = dep.second;

			ModuleConfigurationEntry entry;
			entry.path = moduleInfo->path;
			entry.local = moduleInfo->local;
			cfg.modules.push_back(entry);

			for (const auto* proj : moduleInfo->manifest->projects)
			{
				// do not consider test projects in non-root modules
				if (!moduleInfo->root && proj->type == ProjectType::TestApplication)
					continue;

				// duplicated project ?
				const auto it = projectsByName.find(proj->name);
				if (it != projectsByName.end())
				{
					LogError() << "Project '" << proj->name << "' is defined in more than one module, configuration is invalid";
					return false;
				}

				// add to list
				projectsByName[proj->name] = proj;

			}
		}

		LogInfo() << "Configured " << m_modulesByGuid.size() << " module(s) containing " << projectsByName.size() << " project(s)";
	}

	//--

	// gather dependencies reachable from final applications
	std::unordered_set<const ProjectManifest*> usedProjects;
	{
		bool valid = true;
		for (const auto& it : projectsByName)
			valid &= ExploreProjectDependencies(it.second, projectsByName, usedProjects);

		if (!valid)
		{
			LogError() << "Project configuration failed, there are some dependencies missing";
			return false;
		}

		LogInfo() << "Discovered " << usedProjects.size()  << " used project out of " << projectsByName.size() << " total projects(s)";
	}

	//--

	// collect libraries from the actually used projects only
	std::unordered_set<std::string> libraryNames;
	{
		bool valid = true;
		for (const auto* proj : usedProjects)
		{
			for (const auto& lib : proj->libraryDependencies)
			{
				if (libraryNames.insert(lib).second)
					LogInfo() << "Discovered third party library '" << lib << "' as being used by the projects";				
			}
		}

		LogInfo() << "Discovered " << libraryNames.size() << " used third party libraries";
	}

	//--

	for (const auto& libName : libraryNames)
	{
		ModuleLibraryEntry info;
		info.name = libName;
		cfg.libraries.push_back(info);
	}

	//--

	return true;
}

bool ModuleResolver::processModuleFile(const fs::path& moduleFilePath, bool localFile)
{
	// load main manifest
	if (!processSingleModuleFile(moduleFilePath, localFile))
		return false;

	// resolved ALL dependencies in a recursive pattern
	bool valid = true;
	bool hadUnresolvedDependencies = true;
	while (valid && hadUnresolvedDependencies)
	{
		hadUnresolvedDependencies = false;
		valid &= processUnresolvedLocalDepndencies(hadUnresolvedDependencies);
		valid &= processUnresolvedRemoteDepndencies(hadUnresolvedDependencies);
	}

	// final error
	if (!valid)
	{
		LogError() << "Failed to resolve local dependencies of modules, configuration failed";
		return false;
	}

	return true;
}

bool ModuleResolver::processSingleModuleFile(const fs::path& moduleManifestPath, bool localFile)
{
	if (!fs::is_regular_file(moduleManifestPath))
	{
		LogError() << "File " << moduleManifestPath << " doest not exist";
		return false;
	}

	auto* manifest = ModuleManifest::Load(moduleManifestPath, localFile ? "" : "External");
	if (!manifest)
	{
		LogError() << "File " << moduleManifestPath << " or one of the included files cannot be loaded";
		return false;
	}

	// use the solution name from the first loaded module
	if (m_solutionName.empty())
		m_solutionName = manifest->globalSolutionName;

	// it's legal for a local module to override a remote one
	{
		auto it = m_modulesByGuid.find(manifest->guid);
		if (it != m_modulesByGuid.end())
		{
			auto* mod = it->second;

			if (mod->local || !localFile)
			{
				LogError() << "Duplicated module " << manifest->guid << " found in configuration - same module is loaded from different sources, this is not supported";
				return false;
			}
			else
			{
				LogWarning() << "Discarded remote module " << manifest->guid << " at " << mod->path << " as local version at " << moduleManifestPath << " was found!";
				m_modulesByGuid.erase(it);
				delete mod;
			}
		}

		// define module
		auto* info = new ModuleInfo();
		info->guid = manifest->guid;
		info->local = localFile;
		info->root = (m_modulesByGuid.size() == 0); // first module added is the root one
		info->path = moduleManifestPath;
		info->manifest = manifest;
		m_modulesByGuid[info->guid] = info;

		LogSuccess() << "Registered module '" << manifest->guid << "'";
	}

	bool valid = true;

	for (const auto& dep : manifest->moduleDependencies)
		valid &= processSingleModuleDependency(moduleManifestPath.parent_path(), dep);

	if (!valid)
	{
		LogError() << "Failed to process dependencies of module " << manifest->guid << " loaded form " << moduleManifestPath;
		return false;
	}

	return true;
}

bool ModuleResolver::processSingleModuleDependency(const fs::path& moduleDirectory, const ModuleDepdencencyInfo& dep)
{
	// local path ?
	if (dep.gitRepoPath.empty())
	{
		const auto fullLocalPath = fs::weakly_canonical((moduleDirectory / dep.localRelativePath / "build.xml").make_preferred());
		if (!fs::is_regular_file(fullLocalPath))
		{
			LogError() << "Local relative path " << dep.localRelativePath << " resolved as " << fullLocalPath << " does not point to a valid module directory";
			return false;
		}

		const auto fullLocalPathKey = fullLocalPath.u8string();
		if (m_localModules.find(fullLocalPathKey) == m_localModules.end())
		{
			LogInfo() << "Discovered local module dependency at " << fullLocalPath;

			auto* info = new LocalDependency();
			info->fullPath = fullLocalPath;
			m_localModules[fullLocalPathKey] = info;
		}

		return true;
	}

	// get lower-case path for a key
	const auto key = ToLower(dep.gitRepoPath) + "/" + dep.localRelativePath;

	// already known ?
	if (m_remoteModules.find(key) == m_remoteModules.end())
	{
		LogInfo() << "Discovered remote module dependency on '" << dep.gitRepoPath << "'";

		auto* info = new RemoteDependency();
		info->gitRepository = dep.gitRepoPath;
		info->gitRelativePath = dep.localRelativePath;
		m_remoteModules[key] = info;
	}

	return true;
}

bool ModuleResolver::collectUnresolvedLocalDependencies(std::vector<LocalDependency*>& outDeps) const
{
	bool hasUnresolved = false;

	for (auto& it : m_localModules)
	{
		if (!it.second->resolved)
		{
			it.second->resolved = true;
			outDeps.push_back(it.second);
			hasUnresolved = true;
		}
	}

	return hasUnresolved;
}

bool ModuleResolver::collectUnresolvedRemoteDependencies(std::vector<RemoteDependency*>& outDeps) const
{
	bool hasUnresolved = false;

	for (auto& it : m_remoteModules)
	{
		if (!it.second->resolved)
		{
			it.second->resolved = true;
			outDeps.push_back(it.second);
			hasUnresolved = true;
		}
	}

	return hasUnresolved;
}

bool ModuleResolver::processUnresolvedLocalDepndencies(bool& hadUnresolvedDependnecies)
{
	std::vector<LocalDependency*> deps;
	hadUnresolvedDependnecies |= collectUnresolvedLocalDependencies(deps);

	bool valid = true;
	for (const auto& dep : deps)
		valid &= processSingleModuleFile(dep->fullPath, true);

	return valid;
}

bool ModuleResolver::processUnresolvedRemoteDepndencies(bool& hadUnresolvedDependnecies)
{
	std::vector<RemoteDependency*> deps;
	hadUnresolvedDependnecies |= collectUnresolvedRemoteDependencies(deps);

	bool valid = true;
	for (const auto& dep : deps)
	{
		fs::path cachedDownloadPath;
		if (ensureRepositoryDownloaded(dep->gitRepository, cachedDownloadPath))
		{
			dep->localPath = fs::weakly_canonical((cachedDownloadPath / dep->gitRelativePath).make_preferred());
			valid &= processSingleModuleFile(dep->localPath, false /*local*/);
		}
	}

	return valid;
}

bool ModuleResolver::getRepositoryDownloadPath(std::string_view repoPath, std::string_view branchName, fs::path& outPath) const
{
	// https://github.com/BareMetalEngine/build_tool.git

	std::vector<std::string_view> parts;
	SplitString(repoPath, "/", parts);

	// skip the "https"
	if (!parts.empty() && ToLower(parts[0]) == "https:")
		parts.erase(parts.begin());
	else if (!parts.empty() && ToLower(parts[0]) == "http:")
		parts.erase(parts.begin());

	// build path in cache
	auto path = m_cachePath;
	for (auto part : parts)
	{
		if (EndsWith(part, ".git"))
			part = PartBefore(part, ".git");

		path = path / part;
	}

	// branch name
	outPath = path / branchName;
	return true;
}

bool ModuleResolver::listRepositoryBranches(std::string_view repoPath, std::unordered_map<std::string, std::string>& outBranchNamesWithHashes) const
{
	std::stringstream args;
	args << "git ls-remote ";
	args << repoPath;

	std::vector<std::string> lines;
	if (!RunWithArgsAndCaptureOutputIntoLines(args.str(), lines))
		return false;

	for (const auto& txt : lines)
	{
		Parser line(txt);

		std::string_view hash;
		if (!line.parseString(hash))
			continue;

		if (!line.parseWhitespaces())
			continue;

		std::string_view refName;
		if (!line.parseString(refName))
			continue;

		if (!BeginsWith(refName, "refs/heads/"))
			continue;

		const auto branchName = PartAfter(refName, "refs/heads/");
		if (branchName.empty())
			continue;

		LogInfo() << "Found branch '" << branchName << "' as ref " << hash << " in repository " << repoPath;
		outBranchNamesWithHashes[std::string(branchName)] = std::string(branchName);
	}

	return true;
}

static std::string MakeNameList(const std::unordered_map<std::string, std::string>& names)
{
	std::stringstream txt;
	bool separator = false;

	for (const auto& it : names)
	{
		if (separator)
			txt << ",";
		txt << it.first;
		separator = true;
	}

	return txt.str();
}

bool ModuleResolver::getRepositoryBranchName(std::string_view repoPath, std::string& outBranchNameToDownload) const
{
	std::unordered_map<std::string, std::string> branches;
	if (!listRepositoryBranches(repoPath, branches))
		return false;

	// TODO: branch version override
	// TODO: better logic

	if (Contains<std::string, std::string>(branches, "latest"))
	{
		outBranchNameToDownload = "latest";
		return true;
	}

	if (Contains<std::string, std::string>(branches, "stable"))
	{
		outBranchNameToDownload = "stable";
		return true;
	}

	if (Contains<std::string, std::string>(branches, "master"))
	{
		outBranchNameToDownload = "master";
		return true;
	}

	if (Contains<std::string, std::string>(branches, "main"))
	{
		outBranchNameToDownload = "main";
		return true;
	}

	LogError() << "Failed to determine usable branch for repository " << repoPath << ", available branches: " << MakeNameList(branches);	
	return false;
}

bool ModuleResolver::ensureRepositoryDownloaded(const std::string_view repoPath, fs::path& outDownloadPath)
{
	const auto key = ToLower(repoPath);

	// repository already downloaded ?
	if (auto* repo = Find<std::string, DownloadedRepository*>(m_downloadedRepositories, key, nullptr))
	{
		outDownloadPath = repo->localPath;
		return true;
	}

	// ask for branch list
	std::string branchName;
	if (!getRepositoryBranchName(repoPath, branchName))
		return false;

	// info
	LogSuccess() << "Repository " << repoPath << " will use branch '" << branchName << "'";

	// assemble folder name
	fs::path downloadPath;
	if (!getRepositoryDownloadPath(repoPath, branchName, downloadPath))
	{
		LogError() << "Failed to determine download directory for repository '" << repoPath << "' at branch '" << branchName << "'";
		return false;
	}

	// store repository placement
	{
		auto* info = new DownloadedRepository;
		info->repository = repoPath;
		info->localPath = downloadPath;
		m_downloadedRepositories[key] = info;
	}

	// does the directory exit ?
	if (fs::is_directory(downloadPath))
	{
		// pull latest
		if (!RunWithArgsInDirectory(downloadPath, "git pull"))
		{
			LogWarning() << "Failed to pull latest for repository '" << repoPath << "' at branch '" << branchName << "'";
		}

		// we continue since we do have some code there
		outDownloadPath = downloadPath;
		return true;
	}

	// make sure repo directory exists in cache
	const auto parentCloneDir = downloadPath.parent_path();
	if (!fs::is_directory(parentCloneDir))
	{
		std::error_code ec;
		if (!fs::create_directories(parentCloneDir, ec))
		{
			LogError() << "Failed to create cache directory '" << parentCloneDir;
			return false;
		}
	}

	// assemble a clone command
	std::stringstream command;
	command << "git clone --depth 1 --branch " << branchName << " ";
	command << repoPath;
	command << " " << branchName;

	// pull latest
	if (!RunWithArgsInDirectory(parentCloneDir, command.str()))
	{
		LogError() << "Failed to clone repository '" << repoPath << "' at branch '" << branchName << "'";
		return false;
	}

	// done
	outDownloadPath = downloadPath;	
	return true;
}

//--

ToolConfigure::ToolConfigure()
{}

void ToolConfigure::printUsage()
{
	LogInfo() << "onion configure [options]";
	LogInfo() << "";
	LogInfo() << "General options:";
	LogInfo() << "  -module=<path to module to configure>";
	LogInfo() << "  -configPath=<path where the generated configuration should be written";
	LogInfo() << "";
}

static bool CheckSoftwareVersions()
{
#ifdef _WIN32
    if (!CheckVersion("git", "git version", ".windows", "2.32.0"))
        return false;
#else
    if (!CheckVersion("git", "git version", "", "2.32.0"))
        return false;
#ifdef __APPLE__
    if (!CheckVersion("clang", "version", "-", "13.0.0"))
        return false;
#else
    if (!CheckVersion("gcc", ")", "", "11.0.0"))
        return false;
    if (!CheckVersion("clang", "version", "-", "14.0.0"))
        return false;
#endif
#endif
    if (!CheckVersion("curl", "curl", "(", "7.10.0"))
        return false;

#ifndef _WIN32
#ifdef __APPLE__
    if (!CheckVersion("m4", "GNU M4", "", "1.4.0"))
        return false;
#else
    if (!CheckVersion("m4", "m4 (GNU M4)", "", "1.4.0"))
        return false;
#endif
#endif

    return true;
}

static bool CheckSystemPackageInstalled(const std::string& name)
{
    bool valid = true;
#ifdef _WIN32
    // TODO
#elif defined(__APPLE__)
    std::stringstream args;
    args << "brew list --versions | grep ";
    args << name;
    args << " > /dev/null";

    int exitCode = -1;
    RunWithArgs(args.str(), &exitCode);
    valid = (exitCode == 0);
#else
    std::stringstream args;
    args << "dpkg -s ";
    args << name;
    args << " > /dev/null";

    int exitCode = -1;
    RunWithArgs(args.str(), &exitCode);
    valid = (exitCode == 0);
#endif

    if (valid)
        LogInfo() << "Found package '" << name << "' installed on the system";
    else
        LogError() << "System package '" << name << "' is not installed on the system";

    return valid;
}

static std::string BuildPackagesString(const std::unordered_set<std::string>& packages)
{
    std::stringstream str;

    bool first = true;
    for (const auto& name : packages)
    {
        if (!first) str << " ";
        str << name;
        first = false;
    }

    return str.str();
}

int ToolConfigure::run(const Commandline& cmdline)
{
	//--

	Configuration config;
	if (!Configuration::Parse(cmdline, config))
		return false;

	//--

    if (!cmdline.has("skipVersionCheck"))
    {
        if (!CheckSoftwareVersions())
            return false;
    }
    else
    {
        LogWarning() << "System software version check was disabled (-skipVersionCheck)";
    }

	//--

	const auto configPath = config.platformConfigurationFile();
	LogInfo() << "Using configuration file " << configPath;

	//--

	// resolve all modules, download dependencies and libraries
	ModuleResolver resolver(config.cachePath);
	if (!resolver.processModuleFile(config.moduleFilePath, true))
	{
		LogError() << "Configuration failed";
		return 1;
	}

	// export resolved configuration
	ModuleConfigurationManifest manifest;
	manifest.rootPath = configPath;
	manifest.platform = config.platform;
	if (!resolver.exportToManifest(manifest))
	{
		LogError() << "Configuration export failed";
		return 1;
	}

	//--

	// initialize library installer with library sources
	LibraryInstaller libraries(config.platform, config.cachePath);
	{
		std::vector<ModuleLibrarySource> librarySources;
		resolver.collectLibrarySources(&librarySources);

		bool valid = true;
		for (const auto& source : librarySources)
		{
			if (source.type == "aws")
			{
				valid &= libraries.installOnlineAWSEndpointSource(source.data);
			}
			else if (source.type == "packed")
			{
				valid &= libraries.installOfflinePackedDirectory(fs::path(source.data));
			}
			else if (source.type == "loose")
			{
				valid &= libraries.installOfflineLooseDirectory(fs::path(source.data));
			}
			else
			{
				LogError() << "Invalid library source type '" << source.type << "' pointing to '" << source.data << "'";
				valid = false;
			}
		}

		if (!valid)
		{
			LogError() << "Third party library repository failed to initialize";
			return 1;
		}
	}

	// install libraries
	std::unordered_set<std::string> requiredSystemPacakges;
	{
		bool valid = true;
		for (auto& lib : manifest.libraries)
		{
			fs::path installPath;
			std::string installVersion;
			if (!libraries.install(lib.name, &installPath, &installVersion, &requiredSystemPacakges))
			{
				LogError() << "External third-party library '" << lib.name << "' failed to initialize";
				valid = false;
			}

			lib.path = installPath;
			lib.version = installVersion;
		}

		if (!valid)
		{
			LogError() << "Failed to install all required libraries, project is not configured properly";
			return 1;
		}
	}

	//--

#ifndef _WIN32
    requiredSystemPacakges.insert("m4");
#ifndef __APPLE__
    requiredSystemPacakges.insert("libncurses-dev");
#endif
#endif

    if (cmdline.has("exportPackageList"))
    {
        const auto packagesString = BuildPackagesString(requiredSystemPacakges);
        LogWarning() << "Following system packages are required but are not checked: '" << packagesString << "'";

        const auto packagesFilePath = fs::path(cmdline.get("exportPackageList"));
        SaveFileFromString(packagesFilePath, packagesString);
    }
    else if (!cmdline.has("skipPackageCheck"))
    {
        bool valid = true;
        for (const auto& name : requiredSystemPacakges)
            valid &= CheckSystemPackageInstalled(name);

        if (!valid)
        {
            LogError() << "Required system packages are not installed, please install them and run the configuration again";
            return 1;
        }
    }
    else
    {
        LogWarning() << "System packages version check was disabled (-skipPackageCheck)";
    }

    //--

	// write configuration file
	if (!manifest.save(configPath))
	{
		LogError() << "Configuration saving failed";
		return 1;
	}

	LogSuccess() << "Configuration saved";
	return 0;

	//--
}

//--