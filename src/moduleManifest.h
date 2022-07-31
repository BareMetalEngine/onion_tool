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
    bool published = false; // is the data published in the final build 
};

// manifest of the module
struct ModuleManifest
{
    std::string guid; // module guid
    fs::path rootPath; // where is the module

    fs::path projectsRootPath; // where are the projects (code/)
    std::vector<ProjectManifest*> projects; // local projects in the module

    std::vector<ModuleDepdencencyInfo> moduleDependencies; // other modules we depend on
    std::vector<ModuleDataInfo> moduleData; // exposed data folders

    mutable bool local = true;

    //--

    ModuleManifest();
    ~ModuleManifest();

    static ModuleManifest* Load(const fs::path& manifestPath);

    //--  
};

//--
