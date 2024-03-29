#pragma once

//--

/*
Old python definition
LIBRARY_ZLIB = {
	"name": "zlib",
	"repo" : "https://github.com/madler/zlib.git",

	"config" : "",
	"build" : "--build . --config Release",
	"platforms" : PLATFORMS,

	"artifacts" : [
		{
			"type": "library",
			"location" : "build",
			"platform" : ["windows"] ,
			"files" : ["Release/zlibstatic.lib"]
		},
		{
			"type": "header",
			"location" : "source",
			"platform" : PLATFORMS, # all platforms
			#"target_prefix_path": "zlib/", # most apps expect the zlib to be at "zlib/zlib.h"
			"files" : ["zlib.h"]
		},
		{
			"type": "header",
			"location" : "build",
			"platform" : PLATFORMS, # all platforms
			#"target_prefix_path": "zlib/", # most apps expect the zlib to be at "zlib/zlib.h"
			"files" : ["zconf.h"]
		}
	]
}
*/

enum class LibrarySourceType : uint8_t
{
	Invalid,
	GitHub,
	FileOnTheInternet,
};

enum class LibraryArtifactType : uint8_t
{
	Invalid,
	Header, // header files
	Library, // libraries to link against
	Deploy, // file to deploy
};

enum class LibraryArtifactLocation : uint8_t
{
	Invalid,
	Source, // artifact is located in the source directory (usually headers)
	Build, // artifact is located in the build directory (usually built libs/dlls)
};

struct LibraryArtifactInfo
{
	LibraryArtifactType type = LibraryArtifactType::Invalid;
	LibraryArtifactLocation location = LibraryArtifactLocation::Build;
	std::vector<std::string> files; // input files (NOTE: could be a *.* pattern)
	std::string deployPath; // deploy sub directory
	bool recursive = false;
};

struct LibraryDependencyVar
{
	std::string fileName;
	std::string varName;
};

struct LibraryDependencyInfo
{
	std::string name; // name of the library
	std::string repo; // repository

	std::string includeVar;
	std::vector<LibraryDependencyVar> libraryVars;
};

struct LibraryFilters
{
	PlatformType platform;

	LibraryFilters();
};

struct LibraryManifestConfig
{
	LibrarySourceType sourceType = LibrarySourceType::Invalid;
	std::string sourceURL; // github/wget URL (if zip then it's unzipped)
	std::string sourceRelativePath; // relative path inside the repo/archive we treat as the library root
	std::string sourceBranch; // branch to pull
	bool sourceBuild = false; // building happens in source :(

	std::string configRelativePath;
	std::string configCommand;

	std::string buildRelativePath;
	std::string buildCommand;

	std::vector<LibraryArtifactInfo> artifacts;
	std::vector<LibraryDependencyInfo> dependencies;
	std::vector<std::string> additionalSystemLibraries;
	std::vector<std::string> additionalSystemPackages;
	std::vector<std::string> additionalSystemFrameworks;

	static bool LoadOption(const std::string& loadPath, const void* node, std::string_view option, LibraryManifestConfig* ret);
	static bool LoadCollection(const std::string& loadPath, const void* node, LibraryManifestConfig* ret);
};

struct LibraryManifest
{
	std::string name; // "curl" - directory name

	fs::path loadPath; // file path (set on load)
	PlatformType loadPlatform; // platform this manifest was compiled for

	mutable std::string rootHash; // version hash (from GIT)

	LibraryManifestConfig config; // merged/parsed config (assembled from all the <Platform> blocks)

	LibraryManifest();

	static std::unique_ptr<LibraryManifest> Load(const fs::path& manifestPath, const LibraryFilters& filters); // load manifest from given file (usually a LIBRARY file)

	//--
};

//---