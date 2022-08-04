#include "common.h"
#include "moduleManifest.h"
#include "utils.h"
#include "toolConfigure.h"
#include "moduleConfiguration.h"

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

bool ModuleResolver::exportToManifest(ModuleConfigurationManifest& cfg) const
{
	assert(!cfg.rootPath.empty());

	// start with local dependencies
	uint32_t numModules = 0;
	uint32_t numProjects = 0;
	const auto rootDir = cfg.rootPath.parent_path().make_preferred();
	for (const auto& dep : m_modulesByGuid)
	{
		ModuleConfigurationEntry entry;
		entry.path = fs::relative(dep.second->path, rootDir).u8string();
		entry.local = dep.second->local;
		cfg.modules.push_back(entry);		

		numModules += 1;
		numProjects += (uint32_t)dep.second->manifest->projects.size();
	}

	std::cout << "Configured " << numModules << " module(s) containing " << numProjects << " project(s)\n";
	return true;
}

bool ModuleResolver::processModuleFile(const fs::path& moduleDirectory, bool localFile)
{
	// load main manifest
	if (!processSingleModuleFile(moduleDirectory, localFile))
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
		std::cerr << KRED << "[BREAKING] Failed to resolve local dependencies of modules, configuration failed\n" << RST;
		return false;
	}

	return true;
}

bool ModuleResolver::processSingleModuleFile(const fs::path& moduleDirectory, bool localFile)
{
	const auto moduleManifestPath = (moduleDirectory / MODULE_MANIFEST_NAME).make_preferred();
	if (!fs::is_regular_file(moduleManifestPath))
	{
		std::cerr << KRED << "[BREAKING] Module directory " << moduleManifestPath << " does not contain '" << MODULE_MANIFEST_NAME << "' file\n" << RST;
		return false;
	}

	auto* manifest = ModuleManifest::Load(moduleManifestPath);
	if (!manifest)
	{
		std::cerr << KRED << "[BREAKING] File " << moduleManifestPath << " does not contain valid '" << MODULE_MANIFEST_NAME << "' file\n" << RST;
		return false;
	}

	// it's legal for a local module to override a remote one
	{
		auto it = m_modulesByGuid.find(manifest->guid);
		if (it != m_modulesByGuid.end())
		{
			auto* mod = it->second;

			if (mod->local || !localFile)
			{
				std::cerr << KRED << "[BREAKING] Duplicated module " << manifest->guid << " found in configuration - same module is loaded from different sources, this is not supported\n" << RST;
				return false;
			}
			else
			{
				std::cout << KYEL << "Discarded remote module " << manifest->guid << " at " << mod->path << " as local version at " << moduleDirectory << " was found!\n" << RST;
				m_modulesByGuid.erase(it);
				delete mod;
			}
		}

		// define module
		auto* info = new ModuleInfo();
		info->guid = manifest->guid;
		info->local = localFile;
		info->path = moduleDirectory;
		info->manifest = manifest;
		m_modulesByGuid[info->guid] = info;

		std::cout << KGRN << "Registered module '" << manifest->guid << "'\n" << RST;
	}

	bool valid = true;

	for (const auto& dep : manifest->moduleDependencies)
		valid &= processSingleModuleDependency(moduleDirectory, dep);

	if (!valid)
	{
		std::cerr << KRED << "[BREAKING] Failed to process dependencies of module " << manifest->guid << " loaded form " << moduleDirectory << "\n" << RST;
		return false;
	}

	return true;
}

bool ModuleResolver::processSingleModuleDependency(const fs::path& moduleDirectory, const ModuleDepdencencyInfo& dep)
{
	// local path ?
	if (dep.gitRepoPath.empty())
	{
		const auto fullLocalPath = fs::weakly_canonical((moduleDirectory / dep.localRelativePath).make_preferred());
		const auto fullLocalModuleFilePath = fullLocalPath / MODULE_MANIFEST_NAME;

		if (!fs::is_regular_file(fullLocalModuleFilePath))
		{
			std::cerr << KRED << "[BREAKING] Local relative path " << dep.localRelativePath << " resolved as " << fullLocalPath << " does not point to a valid module directory\n" << RST;
			return false;
		}

		const auto fullLocalPathKey = fullLocalPath.u8string();
		if (m_localModules.find(fullLocalPathKey) == m_localModules.end())
		{
			std::cout << "Discovered local module dependency at " << fullLocalPath << "\n";

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
		std::cout << "Discovered remote module dependency on '" << dep.gitRepoPath << "'\n";

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

		std::cout << "Found branch '" << branchName << "' as ref " << hash << " in repository " << repoPath << "\n";
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

	std::cerr << KRED << "[BREAKING] Failed to determine usable branch for repository " << repoPath << ", available branches: " << MakeNameList(branches) << "\n" << RST;	
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
	std::cout << KGRN << "Repository " << repoPath << " will use branch '" << branchName << "'\n" << RST;

	// assemble folder name
	fs::path downloadPath;
	if (!getRepositoryDownloadPath(repoPath, branchName, downloadPath))
	{
		std::cerr << KRED << "[BREAKING] Failed to determine download directory for repository '" << repoPath << "' at branch '" << branchName << "'\n" << RST;
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
			std::cerr << KYEL << "[WARNING] Failed to pull latest for repository '" << repoPath << "' at branch '" << branchName << "'\n" << RST;
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
			std::cerr << KRED << "[BREAKING] Failed to create cache directory '" << parentCloneDir << "\n" << RST;
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
		std::cerr << KRED << "[BREAKING] Failed to clone repository '" << repoPath << "' at branch '" << branchName << "'\n" << RST;
		return false;
	}

	// done
	outDownloadPath = downloadPath;	
	return true;
}

//--

//--

ToolConfigure::ToolConfigure()
{}

void ToolConfigure::printUsage()
{
	std::cout << KBOLD << "onion configure [options]\n" << RST;
	std::cout << "\n";
	std::cout << "General options:\n";
	std::cout << "  -module=<path to module to configure>\n";
	std::cout << "  -configPath=<path where the generated configuration should be written\n";
	std::cout << "\n";
}

int ToolConfigure::run(const char* argv0, const Commandline& cmdline)
{
	const auto builderExecutablePath = fs::absolute(argv0);
	if (!fs::is_regular_file(builderExecutablePath))
	{
		std::cerr << KRED << "[BREAKING] Invalid local executable name: " << builderExecutablePath << "\n" << RST;
		return false;
	}

#ifdef _WIN32
    if (!CheckVersion("git", "git version", ".windows", "2.32.0"))
        return false;
#else
	if (!CheckVersion("git", "git version", "", "2.32.0"))
		return false;
#endif
	if (!CheckVersion("git-lfs", "git-lfs/", "(", "3.0.0"))
        return false;

	fs::path modulePath;
	{
		const auto str = cmdline.get("module");
		if (str.empty())
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
		else
		{
			modulePath = fs::weakly_canonical(fs::absolute(fs::path(str).make_preferred()));

			const auto moduleFilePath = (modulePath / MODULE_MANIFEST_NAME).make_preferred();
			if (!fs::is_regular_file(moduleFilePath))
			{
				std::cerr << KRED << "[BREAKING] Module directory " << modulePath << " does not contain '" << MODULE_MANIFEST_NAME << "' file\n" << RST;
				return false;
			}
		}
	}

	std::cout << "Using module at " << modulePath << "\n";

	auto cachePath = (modulePath / ".cache").make_preferred();
	{
		const auto str = cmdline.get("cachePath");
		if (!str.empty())
			cachePath = fs::weakly_canonical(fs::absolute(fs::path(str).make_preferred()));
	}
	std::cout << "Using cache at " << cachePath << "\n";

	auto configPath = (modulePath / CONFIGURATION_NAME).make_preferred();
	{
		const auto str = cmdline.get("configPath");
		if (!str.empty())
			configPath = fs::weakly_canonical(fs::absolute(fs::path(str).make_preferred()));
	}
	std::cout << "Using configuration at " << configPath << "\n";

	//--

	// resolve all modules, download dependencies and libraries
	ModuleResolver resolver(cachePath);
	if (!resolver.processModuleFile(modulePath, true))
	{
		std::cerr << KRED << "[BREAKING] Configuration failed\n" << RST;
		return 1;
	}

	// export resolved configuration
	ModuleConfigurationManifest config;
	config.rootPath = configPath;
	if (!resolver.exportToManifest(config))
	{
		std::cerr << KRED << "[BREAKING] Configuration export failed\n" << RST;
		return-1;
	}

	//--

	// write configuration file
	if (!config.save(configPath))
	{
		std::cerr << KRED << "[BREAKING] Configuration saving failed\n" << RST;
		return 1;
	}

	std::cout << KGRN << "Configuration saved\n" << RST;
	return 0;
}

//--