#pragma once

//--

struct ExternalLibraryDeployFile
{
	fs::path absoluteSourcePath;
	std::string relativeDeployPath;
};

struct ExternalLibraryManifest
{
	fs::path rootPath; // path to library directory (set on load)

	std::string name; // "curl" - directory name
	std::string platform; // target platform library was built for
	std::string hash; // build hash
	std::string timestamp;

	fs::path includePath; // path to library's include folder
	std::vector<fs::path> libraryFiles; // individual files to link with
	std::vector<ExternalLibraryDeployFile> deployFiles; // individual files to deploy - copy to binary folder

	std::vector<fs::path> allFiles; // all files referened by the library

	mutable bool used = false; // library is used

	//--

	ExternalLibraryManifest();

	static std::unique_ptr<ExternalLibraryManifest> Load(const fs::path& manifestPath); // load manifest from given file (usually a LIBRARY file)

	//--
};

//--
//--