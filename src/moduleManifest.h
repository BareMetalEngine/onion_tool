#pragma once

//--

struct Configuration;
struct ProjectManifest;

// module dependency
struct ModuleDepdencencyInfo
{
    std::string gitRepoPath; // path to git repo (https://github.com/repo.git)
    std::string localRelativePath; // local path relative to the current manifest file (e.g "modules/crap/")
};

// module data
struct ModuleDataInfo
{
    std::string mountPath; // /data/core/ - where is the data mounted in the virtual file system
    std::string localSourcePath; // as in XML
    fs::path sourcePath; // physical path on disk to the data
};

// module library reference
struct ModuleLibrarySource
{
    std::string type;
    std::string data;
};

// manifest of the module
struct ModuleManifest
{
    std::string guid;

    std::vector<ProjectManifest*> projects; // local projects in the module

    std::vector<ModuleDepdencencyInfo> moduleDependencies; // other modules we depend on
    std::vector<ModuleDataInfo> moduleData; // exposed data folders
	std::vector<ModuleLibrarySource> librarySources; // source of third party libraries
    std::vector<fs::path> globalIncludePaths; // global include paths for source code (root source)

    mutable bool local = true;

    //--

    ModuleManifest();
    ~ModuleManifest();

    static ModuleManifest* Load(const fs::path& manifestPath, std::string_view projectGroup);

    //--  
};

//--
