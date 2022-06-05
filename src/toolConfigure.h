#pragma once

//--

class FileGenerator;
struct GeneratedFile;
struct ModuleConfigurationManifest;
struct ModuleDepdencencyInfo;
struct ModuleManifest;

//--

class ModuleResolver
{
public:
    ModuleResolver(const fs::path& cachePath);
    ~ModuleResolver();

    bool processModuleFile(const fs::path& moduleDirectory, bool localFile);
    bool exportToManifest(ModuleConfigurationManifest& cfg) const;    

private:
    struct ModuleInfo
    {
        std::string guid;
        fs::path path;
        bool local = false;
        ModuleManifest* manifest;

        ~ModuleInfo();
    };

    struct RemoteDependency
    {
        std::string gitRepository;
        std::string gitRelativePath;
        fs::path localPath; // once downloaded and resolved
        bool resolved = false;
    };

    struct LocalDependency
    {
        fs::path fullPath;
        bool resolved = false;
    };

    struct DownloadedRepository
    {
        std::string repository;
        fs::path localPath;
    };

    fs::path m_cachePath;

    std::unordered_map<std::string, ModuleInfo*> m_modulesByGuid;
    std::unordered_map<std::string, RemoteDependency*> m_remoteModules;
    std::unordered_map<std::string, LocalDependency*> m_localModules;
    std::unordered_map<std::string, DownloadedRepository*> m_downloadedRepositories;

    bool processSingleModuleFile(const fs::path& moduleDirectory, bool localFile);
    bool processSingleModuleDependency(const fs::path& moduleDirectory, const ModuleDepdencencyInfo& dep);

    bool processUnresolvedLocalDepndencies(bool& hadUnresolvedDependnecies);
    bool processUnresolvedRemoteDepndencies(bool& hadUnresolvedDependnecies);

    bool collectUnresolvedLocalDependencies(std::vector<LocalDependency*>& outDeps) const;
    bool collectUnresolvedRemoteDependencies(std::vector<RemoteDependency*>& outDeps) const;

    bool ensureRepositoryDownloaded(std::string_view repoPath, fs::path& outDownloadPath);

    bool getRepositoryDownloadPath(std::string_view repoPath, std::string_view branchName, fs::path& outPath) const;
    bool listRepositoryBranches(std::string_view repoPath, std::unordered_map<std::string, std::string>& outBranchNamesWithHashes) const;
    bool getRepositoryBranchName(std::string_view repoPath, std::string& outBranchNameToDownload) const;
};

//--

class ToolConfigure
{
public:
    ToolConfigure();

    int run(const char* argv0, const Commandline& cmdline);
	void printUsage();

private:
    fs::path m_envPath;
};

//--