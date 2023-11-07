#pragma once

//--

class FileGenerator;
struct Configuration;
struct GeneratedFile;
struct ModuleConfigurationManifest;
struct ModuleDepdencencyInfo;
struct ModuleManifest;
struct ModuleLibrarySource;

struct ModuleReferencedLibrary
{
    std::string name;
    uint32_t version = 1;
};

//--

class ModuleResolver
{
public:
    ModuleResolver(const Configuration& config, const fs::path& cachePath);
    ~ModuleResolver();

    inline const std::vector<fs::path>& globalIncludePaths() const { return m_globalIncludePaths; }

    bool processModuleFile(const fs::path& moduleFilePath, bool localFile);
    bool exportToManifest(ModuleConfigurationManifest& cfg) const;    

    void collectLibrarySources(std::vector<ModuleLibrarySource>* outSources) const;

private:
    struct ModuleInfo
    {
        std::string guid;
        fs::path path;
        bool local = false;
        bool root = false;
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

    const Configuration& m_config;

    fs::path m_cachePath;

    std::string m_solutionName;

    std::vector<fs::path> m_globalIncludePaths;

    std::unordered_map<std::string, ModuleInfo*> m_modulesByGuid;
    std::unordered_map<std::string, RemoteDependency*> m_remoteModules;
    std::unordered_map<std::string, LocalDependency*> m_localModules;
    std::unordered_map<std::string, DownloadedRepository*> m_downloadedRepositories;

    bool processSingleModuleFile(const fs::path& moduleManifestPath, bool localFile);
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

    int run(const Commandline& cmdline);
	void printUsage();

private:
    fs::path m_envPath;
};

//--