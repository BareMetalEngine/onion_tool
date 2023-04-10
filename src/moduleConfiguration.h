#pragma once

//--

// configured module
struct ModuleConfigurationEntry
{
	fs::path path; // path to module build.xml
    std::string hash; // needed module hash or version (if downloaded)
    bool local = false; // this is a local module
    bool root = false; // this is a root module
};

// configured library
struct ModuleLibraryEntry
{
    std::string name; // name of the library
    std::string version; // version of the library
    fs::path path; // directory in cache holding that library (for current platform)
};

// manifest of the module
struct ModuleConfigurationManifest
{
    fs::path rootPath; // file path
    std::string name;
    PlatformType platform; // platform we are configuring for
    std::vector<ModuleConfigurationEntry> modules; // configured modules
    std::vector<ModuleLibraryEntry> libraries; // configured libraries

    //--

    ModuleConfigurationManifest();
    ~ModuleConfigurationManifest();

    bool save(const fs::path& manifestPath);

    static ModuleConfigurationManifest* Load(const fs::path& manifestPath);

    //--  
};

//--
