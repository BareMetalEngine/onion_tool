#pragma once

//--

struct Configuration;

// module dependency
struct ModuleDepdencencyInfo
{
    std::string gitRepoPath; // path to git repo (https://github.com/repo.git)
    std::string localRelativePath; // local path relative to the current manifest file (e.g "modules/crap/")
};

// module project
struct ModuleProjectInfo
{
    std::string name; // project name (merged name)
    fs::path manifestPath; // project build.xml path
};

// manifest of the module
struct ModuleManifest
{
    std::string name; // custom module name

    fs::path rootPath; // where is the module

    fs::path sourceRootPath; // where are the sources
    std::vector<ModuleProjectInfo> projects; // local projects in the module

    std::vector<ModuleDepdencencyInfo> moduleDependencies; // other modules we depend on
    std::vector<ModuleDepdencencyInfo> libraryDependencies; // third party libraries we depend on    

    mutable bool local = true;

    //--

    ModuleManifest();
    ~ModuleManifest();

    static ModuleManifest* Load(const fs::path& manifestPath);

    //--  
};

//--
