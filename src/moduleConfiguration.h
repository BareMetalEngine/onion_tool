#pragma once

//--

// configured module
struct ModuleConfigurationEntry
{
	std::string path; // directory in cache holding this module
    std::string hash; // needed module hash or version (if downloaded)
    bool local = false;
};

// manifest of the module
struct ModuleConfigurationManifest
{
    fs::path rootPath; // file path
    std::vector<ModuleConfigurationEntry> modules; // configured modules

    //--

    ModuleConfigurationManifest();
    ~ModuleConfigurationManifest();

    bool save(const fs::path& manifestPath);

    static ModuleConfigurationManifest* Load(const fs::path& manifestPath);

    //--  
};

//--
