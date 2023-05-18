#pragma once

//--

enum class ProjectType : uint8_t
{
    Disabled, // we have a project but it is not compiling to anything
    Application, // executable application
    TestApplication, // automatic application running tests
    StaticLibrary, // statically linked library
    SharedLibrary, // dynamically linked library
    AutoLibrary, // automatic library
    RttiGenerator, // hack project to generate RTTI
};

enum class ProjectLibraryLinkType : uint8_t
{
	Auto,
	AlwaysStatic,
	AlwaysDynamic,
	AlwaysDetachedDynamic,
};

enum class ProjectBuildTypeFilter : uint8_t
{
	Full, // include project everywhere
	DevOnly, // include project only in dev solutions
};

enum class ProjectAppSubsystem : uint8_t
{
	Windows, // Windows only - project does not have console window
	Console, // typical console window app
};

struct Configuration;

struct ProjectManifest
{
    std::string name;
	std::string guid; // project GUID - generated based on project name or manually assigned
    std::string groupName; // name of the group we should place the project at

    fs::path rootPath; // full project's directory

    ProjectType type = ProjectType::Disabled; // type of the project

    ProjectLibraryLinkType optionLinkType = ProjectLibraryLinkType::Auto; // how they library should be linked
    ProjectAppSubsystem optionSubstem = ProjectAppSubsystem::Console;

    int optionWarningLevel = 4;
    bool optionDevOnly = false; // project is development only project and will be skipped in final build
    bool optionDetached = false; // dynamic library only - do not link directly
    bool optionUseStaticInit = true; // project requires static dependencies initialization
    bool optionUsePrecompiledHeaders = true; // project uses precompiled headers
    bool optionUseExceptions = false; // project uses exceptions
    bool optionGenerateMain = false; // generate automatic main.cpp for the project (executables only)
	bool optionGenerateSymbols = true; // generate debug symbols for the project
    bool optionExportApplicataion = true; // application should be exported to other modules
    bool optionSelfTest = false; // project is a self-test project (without gtest)
    bool optionHasPreMain = false; // project has additional pre_main than can override default behavior

    std::vector<std::string> dependencies; // dependencies on another projects
    std::vector<std::string> optionalDependencies; // soft dependencies on another projects (we may continue if projects is NOT available)
    std::vector<std::string> libraryDependencies; // dependencies on third party libraries

    std::vector<std::pair<std::string, std::string>> localDefines; // local defines to add when compiling this project alone
    std::vector<std::pair<std::string, std::string>> globalDefines; // global defines to add for the whole solution (usually stuff like HAS_EDITOR, HAS_PHYSX4, etc)

    std::string appClassName; // for automatically generated main with application interfaces - this is the app class name
    std::string appHeaderName; // for automatically generated main with application interfaces - this is the app header file 
    bool appDisableLogOnStart; // start application without logging (silent output)

    //--

    // load project manifest, NOTE: loading is configuration dependent due to conditions
    //static ProjectManifest* Load(const fs::path& path, const Configuration& config);
    static ProjectManifest* Load(const void* node, const fs::path& modulePath);

    //--
};

//--