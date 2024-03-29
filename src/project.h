#pragma once

//--

enum class ProjectFileType : uint8_t
{
	Unknown,
	CppHeader,
	CppSource,
	Bison,
	WindowsResources,
	BuildScript,
	MediaFile,
	TextFile,
	NatVis,
	NasmAssembly,
	LocalStaticLibrary,
};

struct ModuleManifest;
struct ProjectManifest;
struct ExternalLibraryManifest;
struct ProjectInfo;

class ProjectCollection; 
class ExternalLibraryReposistory;

struct ProjectFileInfo
{
	std::string name; // "test.cpp"

	fs::path absolutePath; // full path to file on disk "Z:\\sth\\src\\base\\math\\src\\vector3.cpp"
	std::string projectRelativePath; // path in project "src/vector3.cpp"
	std::string scanRelativePath; // path in the scan folder (ie. src/) "vector3.cpp"

    ProjectFileType type = ProjectFileType::Unknown; // type of the file

    bool optionUsePch = true; // use PCH on the file
    int optionWarningLevel = 4; // warning level on the file

    const ProjectInfo* originalProject = nullptr; // back pointer to project

    //--

    ProjectFileInfo();
};

struct ProjectInfo
{
	std::string name; // bm/core/math
	fs::path rootPath; // directory with "build.xml"
	std::string globalNamespace;

    const ProjectManifest* manifest = nullptr; // original manifest
	const ModuleManifest* parentModule = nullptr; // module this project is from (may be null for generated projects)
    
	//--

	std::vector<ProjectInfo*> resolvedDependencies; // resolved dependencies on other projects
	std::vector<const ExternalLibraryManifest*> resolvedLibraryDependencies; // resolved dependencies on libraries

	std::vector<ProjectFileInfo*> files; // discovered FINAL project files
	std::unordered_set<fs::path> filesPaths;

	//--

    ProjectInfo();
	~ProjectInfo();

	bool scanContent(); // scan for actual files, fails if any of the hand-specified files are missing
	bool resolveDependencies(const ProjectCollection& projects, std::vector<std::string>* outMissingProjectDependencies);
	bool resolveLibraries(ExternalLibraryReposistory& libs);

    //--

private:
	enum class ScanType : uint8_t
	{
		PrivateFiles,
		PublicFiles,
		MediaFiles,
		ResourceFiles,
	};

	bool internalTryAddFileFromPath(const fs::path& scanRootPath, const fs::path& absolutePath, ScanType type);
	bool scanFilesAtDir(const fs::path& scanRootPath, const fs::path& directoryPath, ScanType type, bool recursive);
};

//--