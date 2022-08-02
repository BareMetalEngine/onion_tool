#pragma once

//--

class Commandline;

struct Configuration
{
    BuildType build;
    PlatformType platform;
    GeneratorType generator;
    LibraryType libs;
    ConfigurationType configuration;

    fs::path configPath;
    fs::path builderExecutablePath;
    //fs::path builderEnvPath;

    fs::path modulePath; // path to root configured module directory
    fs::path tempPath; // shared temp folder
    fs::path solutionPath; // build folder
    fs::path binaryPath; // "bin" folder when all crap is written

    bool staticBuild = false; // build is "static" - no runtime code generation, all has to be pregenerated

    Configuration();

    // windows.vs2019.standalone.final
    // linux.cmake.dev.release
    std::string mergedName() const;

    bool parseOptions(const char* executable, const Commandline& cmd);
    bool parsePaths(const char* executable, const Commandline& cmd);

    bool save(const fs::path& path) const;
    bool load(const fs::path& path);
};

//--
