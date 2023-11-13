#pragma once

//--

class Commandline;

struct Configuration
{
	PlatformType platform; // target platform (windows, linux, etc)
	GeneratorType generator; // code generator (cmake, vs2022, etc)
    SolutionType solutionType;

    fs::path executablePath; // path to onion.exe

    fs::path moduleDirPath;
    fs::path moduleFilePath; // (-module) path to root configured module directory (Z:\projects\core\)
    fs::path tempPath; // (-tempPath) shared temp folder (Z:\projects\core\.temp) 
    fs::path cachePath; // (-cachePath) shared cache folder that could be retained between builds (Z:\projects\core\.cache) - UPDATED ONLT IN THE "configure" PHASE

    fs::path derivedConfigurationPathBase; // where are all the configuration related things go (Z:\projects\core\.temp\windows.vs2022.static.dev\)
    fs::path derivedSolutionPathBase; // where are the generated solution files written go (Z:\projects\core\.temp\windows.vs2022.static.dev\build\)
    fs::path derivedBinaryPathBase; // "bin" folder when all crap is written (Z:\projects\core\.temp\windows.vs2022.static.dev\bin\)

    bool flagStaticBuild = false; // build is "static" - no runtime code generation, all has to be pregenerated in the "generate" step

    Configuration();

    //--

    // windows.vs2019.standalone.final
    // linux.cmake.dev.release
    std::string mergedName() const;

    // path to platform configuration file (contains resolved modules and libraries)
    // Z:\projects\core\.temp\windows.config
	fs::path platformConfigurationFile() const;

    //--

	static bool Parse(const Commandline& cmd, Configuration& cfg);

    //--

	bool save(const fs::path& path) const;
	bool load(const fs::path& path);

    //--

private:
	static bool ParseOptions(const Commandline& cmd, Configuration& cfg);
    static bool ParsePaths(const Commandline& cmd, Configuration& cfg);
};

//--
