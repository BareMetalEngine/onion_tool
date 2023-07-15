#pragma once

#include "xmlUtils.h"

//--

struct ExternalLibraryDeployFile
{
	fs::path absoluteSourcePath;
	std::string relativeDeployPath;
};

struct ExternalLibraryPlatform
{
	PlatformType platform;
	std::vector<fs::path> libraryFiles; // individual files to link with
	std::vector<ExternalLibraryDeployFile> deployFiles; // individual files to deploy - copy to binary folder
	std::vector<std::string> additionalSystemLibraries; // additional libraries to link with
	std::vector<std::string> additionalSystemPackages; // additional system packages that must be installed
	std::vector<std::string> additionalSystemFrameworks; // additional frameworks to links with (ios/macos)
};

struct ExternalLibraryManifest
{
	fs::path rootPath; // path to library directory (set on load)

	std::string name; // "curl" - directory name
	std::string platform; // target platform library was built for
	std::string hash; // build hash
	//std::string timestamp;

	std::vector<fs::path> allFiles; // all files referenced by the library

	//--

	ExternalLibraryManifest();

	static std::unique_ptr<ExternalLibraryManifest> Load(const fs::path& manifestPath); // load manifest from given file (usually a LIBRARY file)

	bool deployFilesToTarget(PlatformType platformType, ConfigurationType configuration, const fs::path& targetPath) const;

	void collectLibraries(PlatformType platformType, std::vector<fs::path>* outLibraryPaths) const;

	void collectIncludeDirectories(PlatformType platformType, std::vector<fs::path>* outLibraryPaths) const;

	void collectAdditionalSystemPackages(PlatformType platformType, std::unordered_set<std::string>* outPackages) const;
	void collectAdditionalSystemFrameworks(PlatformType platformType, std::unordered_set<std::string>* outPackages) const;

	//--

private:
	static bool LoadPlatform(const XMLNode* node, ExternalLibraryPlatform* outPlatform, ExternalLibraryManifest* outManifest);

	fs::path includePath; // path to library's include folder

	ExternalLibraryPlatform defaultPlatform;
	std::vector<ExternalLibraryPlatform> customPlatforms;
};

//--
//--