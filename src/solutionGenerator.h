#pragma once

//--

struct Configuration;
struct GeneratedFile;
struct ProjectInfo;
struct ExternalLibraryManifest;
class ProjectCollection;
class FileGenerator;

//--

struct SolutionProjectFile
{
	std::string name; // test.cpp
	std::string filterPath; // win
	std::string projectRelativePath; // src/win/test.cpp
	std::string scanRelativePath;
	fs::path absolutePath; // Z:\\projects\\crap\\module\\src\\win\\test.cpp
	bool usePrecompiledHeader = false;

	ProjectFileType type = ProjectFileType::Unknown;

	//--

	SolutionProjectFile();
	~SolutionProjectFile();
};

struct SolutionProject;

struct SolutionGroup
{
	std::string name;
	std::string mergedName;

	std::string assignedVSGuid;

    SolutionGroup* parent = nullptr;
	std::vector<SolutionGroup*> children;
	std::vector<SolutionProject*> projects;

	//--

	SolutionGroup();
	~SolutionGroup();
};

struct SolutionProject
{
	ProjectType type = ProjectType::Disabled;

	bool optionUsePrecompiledHeaders = false;
	bool optionUseExceptions = false;
	bool optionUseWindowSubsystem = false;
	bool optionUseReflection = true;
	bool optionUseEmbeddedFiles = false;
	bool optionUseStaticInit = false;
	bool optionDetached = false;
	bool optionExportApplicataion = false;
	bool optionGenerateMain = false;
	bool optionUsePreMain = false;
	bool optionLegacy = false;
	bool optionThirdParty = false;
	bool optionHasInit = false;
	bool optionHasPreInit = false;
	bool optionUseGtest = false;
	bool optionFrozen = false;
	int optionWarningLevel = 4;
	std::string optionAdvancedInstructionSet;

    SolutionGroup* group = nullptr;

	std::string name; // final project name 
	std::string localGroupName; // name of the project group
	std::string globalNamespace; // global namespace in the project

	fs::path rootPath; // original path to project files
	fs::path generatedPath; // generated/base_math/
	fs::path outputPath; // output/base_math/
	fs::path projectPath; // project/base_math/

	fs::path localGlueHeader; // _glue.inl file
	fs::path localPublicHeader; // include/public.h file
	fs::path localPrivateHeader; // src/private.h file
	fs::path localBuildHeader; // generate/base_math/build.h file
	fs::path localReflectionFile; // generated/base_math/reflection.cpp

	std::vector<SolutionProject*> directDependencies;
	std::vector<SolutionProject*> allDependencies;
	std::vector<const ExternalLibraryManifest*> libraryDependencies;

	std::vector<SolutionProjectFile*> files; // may be empty
	std::vector<fs::path> additionalIncludePaths;
	std::vector<fs::path> exportedIncludePaths;
	std::vector<std::string> legacySourceDirectories;

	std::string assignedVSGuid;	

	std::string appHeaderName;
	std::string appClassName;
	bool appDisableLogOnStart = false;

	std::string thirdPartySharedLocalBuildDefine;
	std::string thirdPartySharedGlobalExportDefine;
	std::vector<fs::path> thirdPartyDeployFiles;

	std::vector<fs::path> frozenLibraryFiles;

	std::vector<std::pair<std::string, std::string>> localDefines;
	std::vector<std::pair<std::string, std::string>> globalDefines;

	//--

	SolutionProject();
	~SolutionProject();
};

struct SolutionDataFolder
{
	fs::path dataPath;
	std::string mountPath;
};

//--

class FileRepository;

class SolutionGenerator
{
public:
    SolutionGenerator(FileRepository& files, const Configuration& config, std::string_view mainGroup);
    virtual ~SolutionGenerator();

    inline SolutionGroup* rootGroup() const { return m_rootGroup; }
    inline const std::vector<SolutionProject*>& projects() const { return m_projects; }
    inline const std::vector<fs::path>& sourceRoots() const { return m_sourceRoots; }
	inline const std::vector< SolutionDataFolder>& dataFolders() const { return m_dataFolders; }

    bool extractProjects(const ProjectCollection& collection);
    bool generateAutomaticCode(FileGenerator& fileGenerator);

	virtual bool generateSolution(FileGenerator& gen, fs::path* outSolutionPath) = 0;
	virtual bool generateProjects(FileGenerator& gen) = 0;

protected:
	const Configuration& m_config;
	FileRepository& m_files;

	//---

	fs::path m_sharedGlueFolder;

    SolutionGroup* m_rootGroup = nullptr;
    std::vector<SolutionProject*> m_projects;
	//std::unordered_map<const ProjectInfo*, SolutionProject*> m_projectMap;
	std::unordered_map<std::string, SolutionProject*> m_projectNameMap;

	std::vector<fs::path> m_sourceRoots;
	std::vector<SolutionDataFolder> m_dataFolders;

	SolutionGroup* createGroup(std::string_view name, SolutionGroup* parent = nullptr);
	SolutionProject* findProject(std::string_view name) const;

	//---

    bool generateAutomaticCodeForProject(SolutionProject* project, FileGenerator& fileGenerator);

    bool processBisonFile(SolutionProject* project, const SolutionProjectFile* file);

	bool generateProjectGlueHeaderFile(const SolutionProject* project, std::stringstream& outContent);
    bool generateProjectBuildSourceFile(const SolutionProject* project, std::stringstream& outContent);
    bool generateProjectBuildHeaderFile(const SolutionProject* project, std::stringstream& outContent);
	bool generateProjectAppMainSourceFile(const SolutionProject* project, std::stringstream& outContent);
	bool generateProjectTestMainSourceFile(const SolutionProject* project, std::stringstream& outContent);

    bool generateSolutionEmbeddFileList(std::stringstream& outContent);
	bool generateSolutionReflectionFileProcessingList(std::stringstream& outContent);
	bool generateSolutionFstabFile(const fs::path& binaryPath, std::stringstream& outContent);

    SolutionGroup* findOrCreateGroup(std::string_view name, SolutionGroup* parent);

    //--
};

//--