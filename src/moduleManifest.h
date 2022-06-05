#pragma once

//--

struct Configuration;
struct ProjectManifest;

// module dependency
struct ModuleDepdencencyInfo
{
	std::string name; // local module name under which we'll lookup references
    std::string gitRepoPath; // path to git repo (https://github.com/repo.git)
    std::string localRelativePath; // local path relative to the current manifest file (e.g "modules/crap/")
};

/* module project
struct ModuleProjectInfo
{
    std::string name; // project name (merged name)
    fs::path manifestPath; // project build.xml path
};*/

// manifest of the module
struct ModuleManifest
{
    std::string guid; // module guid
    fs::path rootPath; // where is the module

    fs::path projectsRootPath; // where are the projects (code/)
    std::vector<ProjectManifest*> projects; // local projects in the module

    std::vector<ModuleDepdencencyInfo> moduleDependencies; // other modules we depend on

    mutable bool local = true;

    //--

    ModuleManifest();
    ~ModuleManifest();

    static ModuleManifest* Load(const fs::path& manifestPath);

    //--  
};

//--
