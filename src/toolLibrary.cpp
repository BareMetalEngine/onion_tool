#include "common.h"
#include "utils.h"
#include "toolLibrary.h"
#include "toolRelease.h"
#include "libraryManifest.h"
#include "externalLibrary.h"
#include "git.h"

//--

struct ToolLibraryConfig
{
	fs::path libraryManifestPath; // file path
	fs::path srcRootPath;
	fs::path buildRootPath;
	fs::path deployRootPath;
	fs::path packageRootPath;
	PlatformType platform = DefaultPlatform();

	bool performClone = true;
	bool performConfigure = true;
	bool performBuild = true;
	bool performDeploy = true;	
	bool performPackage = true;

	bool ignorePullErrors = false;
	bool forceOperation = false;
	bool releaseToGitHub = false;

	fs::path srcPath;
	fs::path buildPath;
	fs::path deployPath;
	
	std::string releaseName;
};

static std::string FormatReleaseName()
{
	std::stringstream txt;
	txt << "weekly";
	txt << GetCurrentWeeklyTimestamp();
	return txt.str();
}

static bool ParseArgs(const Commandline& cmdline, ToolLibraryConfig& outConfig)
{
	{
		const auto str = cmdline.get("platform");
		if (!str.empty())
		{
			if (!ParsePlatformType(str, outConfig.platform))
			{
				std::cerr << KRED "[BREAKING] Unknown platform \"" << str << "\"\n" << RST;
				std::cout << "Valid platforms are : " << PrintEnumOptions(DefaultPlatform()) << "\n";
				return false;
			}
		}
	}

	{
		const auto str = cmdline.get("library");
		if (str.empty())
		{
			std::cerr << KRED "[BREAKING] Missing -library argument\n" << RST;
			return false;
		}

		const auto libraryPath = fs::weakly_canonical(fs::path(str).make_preferred());
		if (!fs::is_regular_file(libraryPath))
		{
			std::cerr << KRED "[BREAKING] File \"" << libraryPath << "\" does not exit\n" << RST;
			return false;
		}

		outConfig.libraryManifestPath = libraryPath;
	}

	{
		const auto str = cmdline.get("srcPath");
		if (str.empty())
		{
			outConfig.srcRootPath = (outConfig.libraryManifestPath.parent_path() / ".source").make_preferred();
		}
		else
		{
			outConfig.srcRootPath = fs::weakly_canonical(fs::path(str).make_preferred());
		}

		if (!CreateDirectories(outConfig.srcRootPath))
		{
			std::cerr << KRED "[BREAKING] Failed to create directory \"" << outConfig.srcRootPath << "\n" << RST;
			return false;
		}
	}

	{
		const auto str = cmdline.get("buildPath");
		if (str.empty())
		{
			outConfig.buildRootPath = (outConfig.libraryManifestPath.parent_path() / ".build").make_preferred();
		}
		else
		{
			outConfig.buildRootPath = fs::weakly_canonical(fs::path(str).make_preferred());
		}

		if (!CreateDirectories(outConfig.buildRootPath))
		{
			std::cerr << KRED "[BREAKING] Failed to create directory \"" << outConfig.buildRootPath << "\n" << RST;
			return false;
		}
	}

	{
		const auto str = cmdline.get("deployPath");
		if (str.empty())
		{
			outConfig.deployRootPath = (outConfig.libraryManifestPath.parent_path() / ".out").make_preferred();
		}
		else
		{
			outConfig.deployRootPath = fs::weakly_canonical(fs::path(str).make_preferred());
		}

		if (!CreateDirectories(outConfig.deployRootPath))
		{
			std::cerr << KRED "[BREAKING] Failed to create directory \"" << outConfig.deployRootPath << "\n" << RST;
			return false;
		}
	}

	{
		const auto str = cmdline.get("packagePath");
		if (str.empty())
		{
			outConfig.packageRootPath = (outConfig.libraryManifestPath.parent_path() / ".packages").make_preferred();
		}
		else
		{
			outConfig.packageRootPath = fs::weakly_canonical(fs::path(str).make_preferred());
		}

		if (!CreateDirectories(outConfig.packageRootPath))
		{
			std::cerr << KRED "[BREAKING] Failed to create directory \"" << outConfig.packageRootPath << "\n" << RST;
			return false;
		}
	}

	{
		const auto str = cmdline.get("step");
		if (!str.empty())
		{
			std::vector<std::string_view> steps;
			SplitString(str, ",", steps);

			outConfig.performClone = Contains<std::string_view>(steps, "clone");
			outConfig.performConfigure = Contains<std::string_view>(steps, "configure");
			outConfig.performDeploy = Contains<std::string_view>(steps, "deploy");
			outConfig.performBuild = Contains<std::string_view>(steps, "build");
			outConfig.performPackage = Contains<std::string_view>(steps, "package");
		}
	}

	if (cmdline.has("release"))
	{
		outConfig.releaseToGitHub = true;

		const auto str = cmdline.get("release");
		if (str.empty())
			outConfig.releaseName = FormatReleaseName();
		else
			outConfig.releaseName = str;
	}

	outConfig.forceOperation = cmdline.has("force");
	return true;
}

//--

static bool LibraryCloneRepo_GitHub(const LibraryManifest& lib, ToolLibraryConfig& config)
{
	if (!RunWithArgs("git --version"))
	{
		std::cerr << KRED << "[BREAKING] Git not found in PATH, please install it\n" << RST;
		return false;
	}

	// clone/pull
	if (fs::is_directory(config.srcPath))
	{
		if (!lib.sourceBuild)
		{
			RunWithArgsInDirectory(config.srcPath, "git reset --hard");
			RunWithArgsInDirectory(config.srcPath, "git clean -xfd");

			if (!RunWithArgsInDirectory(config.srcPath, "git pull"))
			{
				if (config.ignorePullErrors)
				{
					std::cerr << KYEL << "[BREAKING] Failed to update library '" << lib.name << "' from repository " << lib.sourceURL << ", using existing code\n" << RST;
				}
				else
				{
					std::cerr << KRED << "[BREAKING] Failed to update library '" << lib.name << "' from repository " << lib.sourceURL << "\n" << RST;
					return false;
				}
			}
		}
	}
	else
	{
		std::stringstream command;
		command << "git clone --depth 1 --single-branch ";
		if (!lib.sourceBranch.empty())
			command << "--branch " << lib.sourceBranch << " ";
		command << lib.sourceURL;
		command << " ";
		command << lib.name;

		if (!RunWithArgsInDirectory(config.srcRootPath, command.str()))
		{
			std::cerr << KRED << "[BREAKING] Failed to clone library '" << lib.name << "' from repository " << lib.sourceURL << "'\n" << RST;
			return false;
		}
	}

	// verify repository
	{
		if (!RunWithArgsInDirectory(config.srcPath, "git fsck --full"))
		{
			std::cerr << KRED << "[BREAKING] Failed to verify library '" << lib.name << "' fetched from repository " << lib.sourceURL << "'\n" << RST;
			return false;
		}
	}

	// get head commit hash, this verifies that the git repo is valid
	{
		std::stringstream hash;
		if (!RunWithArgsInDirectoryAndCaptureOutput(config.srcPath, "git rev-parse --verify HEAD", hash))
		{
			std::cerr << KRED << "[BREAKING] Failed to fetch root hash from library '" << lib.name << "' fetched from repository " << lib.sourceURL << "'\n" << RST;
			return false;
		}

		lib.rootHash = Trim(hash.str());
	}

	std::cout << KGRN << "Fetched library '" << lib.name << "' from repository " << lib.sourceURL << "' at hash " << lib.rootHash << "\n" << RST;
	return true;
}

static bool LibraryCloneRepo_URL(const LibraryManifest& lib, ToolLibraryConfig& config)
{
	return false;
}

static bool LibraryCloneRepo(const LibraryManifest& lib, ToolLibraryConfig& config)
{
	if (lib.sourceType == LibrarySourceType::GitHub)
		return LibraryCloneRepo_GitHub(lib, config);
	else if (lib.sourceType == LibrarySourceType::FileOnTheInternet)
		return LibraryCloneRepo_URL(lib, config);
	else
		return false;
}

//--

static bool LibraryConfigure(const LibraryManifest& lib, ToolLibraryConfig& config)
{
	// make sure build directory exists
	if (!CreateDirectories(config.buildPath))
		return false;
	
	// run the config command in the build directory
	const auto runDirectory = fs::weakly_canonical((config.buildPath / lib.configRelativePath).make_preferred());

	// determine the relative path
	const auto sourceRelativeToBuild = fs::relative(config.srcPath, runDirectory).make_preferred();

	// replace some stuff in the command
	auto command = lib.configCommand;
	command = ReplaceAll(command, "${SourcePath}", sourceRelativeToBuild.u8string());
	command = ReplaceAll(command, "${SourceAbsPath}", config.srcPath.u8string());

	// run the command
	if (!RunWithArgsInDirectory(runDirectory, command))
	{
		std::cerr << KRED << "[BREAKING] Failed to configure library '" << lib.name << "'\n" << RST;
		return false;
	}

	// configured	
	std::cout << KGRN << "Library '" << lib.name << "' configured\n" << RST;
	return true;
}

//--

static bool LibraryBuild(const LibraryManifest& lib, ToolLibraryConfig& config)
{
	// make sure build directory exists
	if (!CreateDirectories(config.buildPath))
		return false;

	// run the config command in the build directory
	const auto runDirectory = fs::weakly_canonical((config.buildPath / lib.buildRelativePath).make_preferred());

	// determine the relative path
	const auto buildRelativeToRun = fs::relative(config.buildPath, runDirectory).make_preferred();

	// replace some stuff in the command
	auto command = lib.buildCommand;
	command = ReplaceAll(command, "${BuildPath}", buildRelativeToRun.u8string());
	command = ReplaceAll(command, "${BuildAbsPath}", config.srcPath.u8string());

	// run the command
	if (!RunWithArgsInDirectory(runDirectory, command))
	{
		std::cerr << KRED << "[BREAKING] Failed to build library '" << lib.name << "'\n" << RST;
		return false;
	}

	// configured	
	std::cout << KGRN << "Library '" << lib.name << "' built\n" << RST;
	return true;
}

//--

struct LibraryFinalizedArtifact
{
	LibraryArtifactType type;

	std::string name;
	std::string relativePath;
	fs::path sourcePath;
	fs::path targetPath;
};

static bool IsWildcard(std::string_view path)
{
	for (const auto ch : path)
		if (ch == '*') return true;

	return false;
}

static void SplitWildcard(std::string_view path, std::string_view& outParentDir, std::string_view& outFileName, std::string_view& outExtension)
{
	outParentDir = PartBeforeLast(path, "/");

	const auto fundamentalFileName = PartAfterLast(path, "/", true);
	outFileName = PartBeforeLast(fundamentalFileName, ".");
	outExtension = PartAfterLast(fundamentalFileName, ".");
}

static bool MatchWildcard(std::string_view txt, std::string_view pattern)
{
	if (pattern == "*")
		return !txt.empty();

	return txt == pattern;
}

static bool MatchFile(std::string_view name, std::string_view fileNameMatch, std::string_view extensionMatch)
{
	const auto fileName = PartBeforeLast(name, ".");
	const auto extension = PartAfterLast(name, ".");

	return MatchWildcard(fileNameMatch, fileNameMatch) && MatchWildcard(extension, extensionMatch);
}

static uint32_t LibraryCollectArtifactsFromDirectory(LibraryArtifactType type, const fs::path& baseSourcePath, const fs::path& sourcePath, const fs::path& deployPath, std::string_view fileNameMatch, std::string_view extensionMatch, bool recrusive, std::vector<LibraryFinalizedArtifact>& outArtifacts)
{
	uint32_t count = 0;

	for (const auto& entry : fs::directory_iterator(sourcePath))
	{
		const auto name = entry.path().filename().u8string();

		if (entry.is_regular_file())
		{
			if (MatchFile(name, fileNameMatch, extensionMatch))
			{
				LibraryFinalizedArtifact artifact;
				artifact.type = type;
				artifact.name = name;
				artifact.relativePath = ReplaceAll(fs::relative(entry.path(), baseSourcePath).make_preferred().u8string(), "\\", "/");
				artifact.sourcePath = entry.path();
				artifact.targetPath = (deployPath / name).make_preferred();
				outArtifacts.push_back(artifact);
				count += 1;
			}
		}
		else if (entry.is_directory() && recrusive)
		{
			const auto deploySubDirectoryPath = (deployPath / name).make_preferred();
			count += LibraryCollectArtifactsFromDirectory(type, baseSourcePath, entry.path(), deploySubDirectoryPath, fileNameMatch, extensionMatch, recrusive, outArtifacts);
		}
	}

	return count;
}

static bool LibraryCollectArtifacts(const LibraryManifest& lib, ToolLibraryConfig& config, std::vector<LibraryFinalizedArtifact>& outArtifacts)
{
	bool valid = true;

	for (const auto& info : lib.artifacts)
	{
		const auto baseSourcePath = (info.location == LibraryArtifactLocation::Build) ? config.buildPath : config.srcPath;

		for (const auto& file : info.files)
		{
			std::string_view searchParentDir;
			std::string_view searchFileName;
			std::string_view searchExtension;
			SplitWildcard(file, searchParentDir, searchFileName, searchExtension);

			const auto fullSearchPath = fs::weakly_canonical((baseSourcePath / searchParentDir).make_preferred());
			const auto fullDeployPath = fs::weakly_canonical((config.deployPath / info.deployPath).make_preferred());

			if (IsWildcard(file))
			{
				if (!LibraryCollectArtifactsFromDirectory(info.type, fullSearchPath, fullSearchPath, fullDeployPath, searchFileName, searchExtension, info.recursive, outArtifacts))
				{
					std::cerr << KRED << "[BREAKING] Failed to collect build artifacts at '" << searchParentDir << "' in form of " << searchFileName << "." << searchExtension <<
						(info.recursive ? " (recrusive)" : "(non recrusive)") << "\n" << RST;
					valid = false;
				}
			}
			else
			{
				const auto fullName = std::string(searchFileName) + "." + std::string(searchExtension);
				const auto fullSearchFile = (fullSearchPath / fullName).make_preferred();
				
				if (fs::is_regular_file(fullSearchFile))
				{
					LibraryFinalizedArtifact artifact;
					artifact.type = info.type;
					artifact.name = fullName;
					artifact.relativePath = ReplaceAll(fs::path(searchFileName).make_preferred().u8string(), "\\", "/");
					artifact.sourcePath = fullSearchFile;
					artifact.targetPath = (fullDeployPath / fullName).make_preferred();
					outArtifacts.push_back(artifact);
				}
				else
				{
					std::cerr << KRED << "[BREAKING] Failed to collect build artifact at '" << fullSearchFile << "\n" << RST;
					valid = false;
				}
			}
		}
	}

	return true;
}

static fs::path LibraryManifestPath(const LibraryManifest& lib, ToolLibraryConfig& config)
{
	const auto manifestPath = (config.deployPath / (lib.name + ".onion")).make_preferred();
	return manifestPath;
}

static void LibraryBuildManifest(const LibraryManifest& lib, ToolLibraryConfig& config, const std::vector<LibraryFinalizedArtifact>& artifacts, const fs::path& manifestDir, fs::file_time_type timestamp, std::stringstream& f)
{
	uint64_t timestampValue = timestamp.time_since_epoch().count();

	writeln(f, "<?xml version=\"1.0\" encoding=\"utf-8\"?>");
	writelnf(f, "<ExternalLibrary name=\"%hs\" hash=\"%hs\" timestamp=\"%llu\">", lib.name.c_str(), lib.rootHash.c_str(), timestampValue);

	for (const auto& artifact : artifacts)
	{
		const auto relativePath = ReplaceAll(fs::relative(artifact.targetPath, manifestDir).make_preferred().u8string(), "\\", "/");

		if (artifact.type == LibraryArtifactType::Library)
			writelnf(f, "<Link>%hs</Link>", relativePath.c_str());
		else if (artifact.type == LibraryArtifactType::Deploy)
			writelnf(f, "<Deploy>%hs</Deploy>", relativePath.c_str());
		else
			writelnf(f, "<File>%hs</File>", relativePath.c_str());
	}

	writeln(f, "</ExternalLibrary>");
}

static bool LibraryDeploy(const LibraryManifest& lib, ToolLibraryConfig& config)
{
	// make sure build directory exists
	if (!CreateDirectories(config.deployPath))
		return false;

	// load the manifest
	std::vector<LibraryFinalizedArtifact> artifacts;
	if (!LibraryCollectArtifacts(lib, config, artifacts))
		return false;

	// stats
	std::cout << KGRN << "Collected " << artifacts.size() << " artifacts for library '" << lib.name << "'\n" << RST;

	// copy artifacts (if newer, to the build directory)
	bool valid = true;
	uint32_t numActuallyCopied = 0;	
	fs::file_time_type newestFile;
	for (const auto& info : artifacts)
	{
		bool copied = false;
		if (CopyNewerFile(info.sourcePath, info.targetPath, &copied))
		{
			if (copied)
				numActuallyCopied += 1;

			const auto fileTime = fs::last_write_time(info.targetPath);
			if (fileTime > newestFile)
				newestFile = fileTime;
		}
		else
		{
			valid = false;
		}
	}

	// write manifest
	if (valid)
	{
		const auto manifestPath = LibraryManifestPath(lib, config);
		const auto manifestDir = manifestPath.parent_path();

		std::stringstream f;
		LibraryBuildManifest(lib, config, artifacts, manifestDir, newestFile, f);
		valid &= SaveFileFromString(manifestPath, f.str(), false, false);
	}

	if (!valid)
	{
		std::cout << KRED << "Failed to deploy all files for library '" << lib.name << "'\n" << RST;
		return false;
	}

	// done
	std::cout << KGRN << "Deployed " << numActuallyCopied << " file(s) (out of total " << artifacts.size() << ") for library '" << lib.name << "'\n" << RST;
	return true;
}

//--

static fs::path LibraryArchivePath(const ExternalLibraryManifest& lib, ToolLibraryConfig& config)
{
	std::stringstream fileName;
	fileName << "lib_";
	fileName << lib.name;

	/*if (lib.hash.empty())
	{
		fileName << "_";
		fileName << lib.timestamp;
	}
	else
	{
		fileName << "_";
		fileName << lib.hash;
	}*/
	fileName << "_";
	fileName << NameEnumOption(config.platform);

	fileName << ".zip";

	const auto archivePath = (config.packageRootPath / fileName.str()).make_preferred();
	return archivePath;
}

static bool LibraryPackage(const LibraryManifest& lib, ToolLibraryConfig& config)
{
	// make
	if (!RunWithArgs("tar --version"))
	{
		std::cerr << KRED << "[BREAKING] Tar not found in PATH, please install it\n" << RST;
		return false;
	}

	// get manifest for the library and load it
	const auto manifestPath = LibraryManifestPath(lib, config);
	const auto manifest = ExternalLibraryManifest::Load(manifestPath);
	if (!manifest)
	{
		std::cerr << KRED << "[BREAKING] Failed to load output manifest for library '" << lib.name << "', was the library built and deployed correctly?\n" << RST;
		return false;
	}

	// check if we all files to pack exist
	bool valid = true;
	fs::file_time_type newestFile;
	for (const auto& filePath : manifest->allFiles)
	{
		if (fs::is_regular_file(filePath))
		{
			const auto fileTime = fs::last_write_time(filePath);
			if (fileTime > newestFile)
				newestFile = fileTime;
		}
		else
		{
			std::cout << KRED << "Required file " << filePath << " is missing\n" << RST;
			valid = false;
		}
	}

	if (!valid)
		return false;

	// get archive name
	const auto archivePath = LibraryArchivePath(*manifest, config);

	// try to skip packaging if there's nothing to package
	if (fs::is_regular_file(archivePath))
	{
		const auto fileTime = fs::last_write_time(archivePath);
		if (newestFile < fileTime)
		{
			if (config.forceOperation)
			{
				std::cout << KYEL << "Output archive " << archivePath << " is up to date but a -force switch is used so it will be rebuilt\n" << RST;
			}
			else
			{
				std::cout << KGRN << "Output archive " << archivePath << " is up to date, skipping packaging!\n" << RST;
				return true;
			}
		}
	}

	// package
	{
		std::stringstream command;
		command << "tar -acf ";
		command << EscapeArgument(archivePath.u8string());
		command << " .";

		if (!RunWithArgsInDirectory(config.deployPath, command.str()))
		{
			std::cout << KRED << "Failed to package " << archivePath << "\n" << RST;
			return false;
		}
	}

	// done
	std::cout << KGRN << "Packaged library '" << lib.name << "'\n" << RST;
	return true;
}

//--

static bool LibraryRelease(GitHubConfig& git, const LibraryManifest& lib, ToolLibraryConfig& config, std::string_view releaseId)
{
	// get manifest for the library and load it
	const auto manifestPath = LibraryManifestPath(lib, config);
	const auto manifest = ExternalLibraryManifest::Load(manifestPath);
	if (!manifest)
	{
		std::cerr << KRED << "[BREAKING] Failed to load output manifest for library '" << lib.name << "', was the library built and deployed correctly?\n" << RST;
		return false;
	}

	// get archive name
	const auto archivePath = LibraryArchivePath(*manifest, config);
	if (!fs::is_regular_file(archivePath))
	{
		std::cerr << KRED << "[BREAKING] Archived library file " << archivePath << " does not exist, there's nothing to publish" << RST;
		return false;
	}

	// asset file name
	const auto assetFileName = archivePath.filename().u8string();

	// list all current artifacts of the release
	std::vector<GitArtifactInfo> artifacts;
	if (!GitApi_ListReleaseArtifacts(git, releaseId, artifacts))
	{
		std::cerr << KRED << "[BREAKING] Failed to list git artifacts for release '" << config.releaseName << "' at ID " << releaseId << "\n" << RST;
		return false;
	}

	// check if we have existing deployment of such file
	{
		std::string matchingAssetID;
		for (const auto& info : artifacts)
		{
			if (info.name == assetFileName)
			{
				matchingAssetID = info.id;
				break;
			}
		}

		if (!matchingAssetID.empty())
		{
			std::cout << "Github Release Asset for '" << assetFileName << "' in release '" << config.releaseName << "' already found at ID " << matchingAssetID << "\n";
			return false;
		}
	}

	// push asset
	{
		if (!GitApi_UploadReleaseArtifact(git, releaseId, assetFileName, archivePath))
		{
			std::cerr << KRED << "[BREAKING] Upload failed" << RST;
			return false;
		}
	}

	return true;	
}

//--

ToolLibrary::ToolLibrary()
{}

void ToolLibrary::printUsage(const char* argv0)
{
	auto platform = DefaultPlatform();

	std::cout << KBOLD << "onion library [options]\n" << RST;
	std::cout << "\n";
	std::cout << "Build configuration options:\n";
	std::cout << "  -platform=" << PrintEnumOptions(platform) << "\n";
	std::cout << "\n";
	std::cout << "General options:\n";
	std::cout << "  -library=<library to build>\n";
	std::cout << "  -step=[clone|configure|build|deploy]\n";
	std::cout << "  -srcDir=<path to source directory where original repository is downloaded>\n";
	std::cout << "  -buildDir=<path to build directory where all the build files are stored>\n";
	std::cout << "  -deployDir=<path where all final library files and includes are copied to>\n";
	std::cout << "\n";
}

int ToolLibrary::run(const char* argv0, const Commandline& cmdline)
{
	const auto builderExecutablePath = fs::absolute(argv0);
	if (!fs::is_regular_file(builderExecutablePath))
	{
		std::cerr << KRED << "[BREAKING] Invalid local executable name: " << builderExecutablePath << "\n" << RST;
		return 1;
	}

	ToolLibraryConfig config;
	if (!ParseArgs(cmdline, config))
		return 1;

	//--

	LibraryFilters filters;
	filters.platform = config.platform;

	const auto library = LibraryManifest::Load(config.libraryManifestPath, filters);
	if (!library)
	{
		std::cerr << KRED << "[BREAKING] Failed to load library manifest from " << config.libraryManifestPath << "\n" << RST;
		return 1;
	}

	config.srcPath = (config.srcRootPath / library->name).make_preferred();
	if (library->sourceBuild)
		config.buildPath = config.srcPath;
	else
		config.buildPath = (config.buildRootPath / library->name).make_preferred();
	config.deployPath = (config.deployRootPath / library->name).make_preferred();

	//--

	GitHubConfig git;
	std::string releaseId;
	if (config.releaseToGitHub)
	{
		const auto libraryRoot = config.libraryManifestPath.parent_path();
		if (!git.init(libraryRoot, cmdline))
		{
			std::cerr << KRED << "[BREAKING] Failed to initialize Git for release mode\n" << RST;
			return-1;
		}

		if (!Release_GetCurrentReleaseId(git, cmdline, releaseId))
		{
			std::cerr << KRED << "[BREAKING] No active release in progress\n" << RST;
			return 1;
		}
	}

	if (config.performClone)
	{
		if (!LibraryCloneRepo(*library, config))
		{
			std::cerr << KRED << "[BREAKING] Clone step for library " << library->name << " failed\n" << RST;
			return 1;
		}
	}

	if (config.performConfigure)
	{
		if (!LibraryConfigure(*library, config))
		{
			std::cerr << KRED << "[BREAKING] Configure step for library " << library->name << " failed\n" << RST;
			return 1;
		}
	}

	if (config.performBuild)
	{
		if (!LibraryBuild(*library, config))
		{
			std::cerr << KRED << "[BREAKING] Build step for library " << library->name << " failed\n" << RST;
			return 1;
		}
	}

	if (config.performDeploy)
	{
		if (!LibraryDeploy(*library, config))
		{
			std::cerr << KRED << "[BREAKING] Deploy step for library " << library->name << " failed\n" << RST;
			return 1;
		}
	}

	if (config.performPackage)
	{
		if (!LibraryPackage(*library, config))
		{
			std::cerr << KRED << "[BREAKING] Package step for library " << library->name << " failed\n" << RST;
			return 1;
		}
	}

	if (config.releaseToGitHub)
	{
		if (!LibraryRelease(git, *library, config, releaseId))
		{
			std::cerr << KRED << "[BREAKING] Release step for library " << library->name << " failed\n" << RST;
			return 1;
		}
	}

	//--

	// done
	return 0;
}

//--