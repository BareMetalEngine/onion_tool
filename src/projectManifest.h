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
    HeaderLibrary, // library that has only headers, does not produce any likable content
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

enum class ProjectTestFramework : uint8_t
{
	GTest, // Windows only - project does not have console window
	Catch2, // typical console window app
};

struct Configuration;

struct ProjectManifest
{
    std::string name;
	std::string guid; // project GUID - generated based on project name or manually assigned
    std::string solutionGroupName; // name of the top-level group we should place the project at
    std::string localGroupName; // name of the sub-group in the top level group we should be at

    fs::path loadPath; // path this manifest was loaded from
    fs::path rootPath; // full project's directory

    ProjectType type = ProjectType::Disabled; // type of the project

    ProjectLibraryLinkType optionLinkType = ProjectLibraryLinkType::Auto; // how they library should be linked
    ProjectAppSubsystem optionSubstem = ProjectAppSubsystem::Console;
    ProjectTestFramework optionTestFramework = ProjectTestFramework::GTest;

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
    bool optionLegacy = false; // legacy project without the src/include structure
    bool optionThirdParty = false; // third party library
    bool optionFrozen = false; // project is frozen - a precompiled dll/lib is used instead of compiling it but appears as if it's there
    bool optionEngineOnly = false; // project is "engine only" - included only when building raw engine (not the game)
	bool optionHasInit = false; // project has the initialization function called after reflection was established
	bool optionHasPreInit = false; // project has the initialization function called before reflection was established
    std::string optionAdvancedInstructionSet;

    std::vector<std::string> dependencies; // dependencies on another projects
    std::vector<std::string> optionalDependencies; // soft dependencies on another projects (we may continue if projects is NOT available)
    std::vector<std::string> libraryDependencies; // dependencies on third party libraries

    std::vector<fs::path> localIncludePaths; // additional include path just for the project
    std::vector<std::string> legacySourceDirectories; // directories with the source code (legacy only)

    std::vector<fs::path> exportedIncludePaths; // additional include path just for the project

    std::vector<std::pair<std::string, std::string>> localDefines; // local defines to add when compiling this project alone
    std::vector<std::pair<std::string, std::string>> globalDefines; // global defines to add for the whole solution (usually stuff like HAS_EDITOR, HAS_PHYSX4, etc)

	std::vector<fs::path> frozenDeployFiles; // files to deploy to the binary directory for this project
	std::vector<fs::path> frozenLibraryFiles; // files to link with for this project

	//std::vector<fs::path> thirdPartyIncludeDirectories; // additional include path just for the project
	std::vector<fs::path> thirdPartySourceFiles; // source files to compile (manual list)
    std::string thirdPartySharedLocalBuildDefine; // defined globally when building third party lib as DLL
    std::string thirdPartySharedGlobalExportDefine; // defined globally when third party is going to be built as DLL

    std::string appClassName; // for automatically generated main with application interfaces - this is the app class name
    std::string appHeaderName; // for automatically generated main with application interfaces - this is the app header file 
	std::vector<std::string> appSystemClasses; // application system classes to auto initialize with this module
    bool appDisableLogOnStart; // start application without logging (silent output)

    //--

    // load project manifest, NOTE: loading is configuration dependent due to conditions
    //static ProjectManifest* Load(const fs::path& path, const Configuration& config);
    static ProjectManifest* Load(const void* node, const fs::path& modulePath, const Configuration& config);

private:
    static bool LoadKey(const void* node, const fs::path& modulePath, ProjectManifest* ret);
    static bool LoadKeySet(const void* node, const fs::path& modulePath, ProjectManifest* ret, const Configuration& config);

    std::vector<std::string> _temp_localIncludePaths;
    std::vector<std::string> _temp_exportedIncludePaths;
};

//--