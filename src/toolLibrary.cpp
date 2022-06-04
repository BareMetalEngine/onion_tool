#include "common.h"
#include "utils.h"
#include "toolLibrary.h"
#include "toolRelease.h"
#include "libraryManifest.h"
#include "externalLibrary.h"
#include "git.h"
#include <thread>

//--

struct ToolLibraryConfig
{
	fs::path libraryManifestPath; // file path
	fs::path tempRootPath; // ($ManifsetDir)/.temp
	fs::path srcRootPath; // ($ManifsetDir)/.temp/source
	fs::path buildRootPath; // ($ManifsetDir)/.temp/build
	fs::path deployRootPath; // ($ManifsetDir)/.temp/out
	fs::path packageRootPath; // ($ManifsetDir)/.temp/package
	fs::path commitRootPath; // ($ManifsetDir)/.temp/commit
	fs::path downloadRootPath; // ($ManifsetDir)/.temp/download
	fs::path dependenciesRootPath; // ($ManifsetDir)/.temp/dependencies
	fs::path hacksRootPath; // ($ManifsetDir)/hacks
	PlatformType platform = DefaultPlatform();

	bool performClone = true;
	bool performConfigure = true;
	bool performBuild = true;
	bool performDeploy = true;	
	bool performPackage = true;

	bool ignorePullErrors = false;
	bool forceOperation = false;
	bool releaseToGitHub = false;
	bool commitToGitHub = false;

	fs::path srcPath; // {$ManifsetDir}/.source/{$LibraryName}
	fs::path buildPath; // {$ManifsetDir}/.build/{$LibraryName}
	fs::path deployPath; // {$ManifsetDir}/.out/{$LibraryName}
	fs::path hackPath; // {$ManifsetDir}/hacks/{$LibraryName}
	
	std::string releaseName;
	std::string commitRepo;
	std::string commitToken;
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
			std::cerr << KRED "[BREAKING] File " << libraryPath << " does not exist\n" << RST;
			return false;
		}

		outConfig.libraryManifestPath = libraryPath;
	}

	{
		const auto str = cmdline.get("tempPath");
		if (str.empty())
		{
			outConfig.tempRootPath = (outConfig.libraryManifestPath.parent_path() / ".temp").make_preferred();
		}
		else
		{
			outConfig.tempRootPath = fs::weakly_canonical(fs::path(str).make_preferred());
		}

		if (!CreateDirectories(outConfig.tempRootPath))
		{
			std::cerr << KRED "[BREAKING] Failed to create directory \"" << outConfig.tempRootPath << "\n" << RST;
			return false;
		}
	}

	{
		const auto str = cmdline.get("srcPath");
		if (str.empty())
		{
			outConfig.srcRootPath = (outConfig.tempRootPath / "source").make_preferred();
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
			outConfig.buildRootPath = (outConfig.tempRootPath / "build").make_preferred();
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
		const auto str = cmdline.get("commitPath");
		if (str.empty())
		{
			outConfig.commitRootPath = (outConfig.tempRootPath / "commit").make_preferred();
		}
		else
		{
			outConfig.commitRootPath = fs::weakly_canonical(fs::path(str).make_preferred());
		}

		if (!CreateDirectories(outConfig.commitRootPath))
		{
			std::cerr << KRED "[BREAKING] Failed to create directory \"" << outConfig.commitRootPath << "\n" << RST;
			return false;
		}
	}

	{
		const auto str = cmdline.get("deployPath");
		if (str.empty())
		{
			outConfig.deployRootPath = (outConfig.tempRootPath / "deploy").make_preferred();
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
			outConfig.packageRootPath = (outConfig.tempRootPath / "packages").make_preferred();
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
		const auto str = cmdline.get("downloadPath");
		if (str.empty())
		{
			outConfig.downloadRootPath = (outConfig.tempRootPath / "download").make_preferred();
		}
		else
		{
			outConfig.downloadRootPath = fs::weakly_canonical(fs::path(str).make_preferred());
		}

		if (!CreateDirectories(outConfig.downloadRootPath))
		{
			std::cerr << KRED "[BREAKING] Failed to create directory \"" << outConfig.downloadRootPath << "\n" << RST;
			return false;
		}
	}

	{
		const auto str = cmdline.get("dependenciesPath");
		if (str.empty())
		{
			outConfig.dependenciesRootPath = (outConfig.tempRootPath / "dependencies").make_preferred();
		}
		else
		{
			outConfig.dependenciesRootPath = fs::weakly_canonical(fs::path(str).make_preferred());
		}

		if (!CreateDirectories(outConfig.dependenciesRootPath))
		{
			std::cerr << KRED "[BREAKING] Failed to create directory \"" << outConfig.dependenciesRootPath << "\n" << RST;
			return false;
		}
	}	

	{
		const auto str = cmdline.get("hacksPath");
		if (str.empty())
		{
			outConfig.hacksRootPath = (outConfig.libraryManifestPath.parent_path() / "hacks").make_preferred();
		}
		else
		{
			outConfig.hacksRootPath = fs::weakly_canonical(fs::path(str).make_preferred());
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

	if (cmdline.has("commit"))
	{
		outConfig.commitToGitHub = true;

		const auto str = cmdline.get("commit");
		if (str.empty())
			outConfig.commitRepo = DEFAULT_DEPENDENCIES_REPO;
		else
			outConfig.commitRepo = str;

		outConfig.commitToken = cmdline.get("token");
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
		std::cout << "Source directory " << config.srcPath << " already exists, syncing only\n";

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

	// cloned
	std::cout << "Source directory " << config.srcPath << " cloned from '" << lib.sourceURL << "'\n";

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

	// apply hacks
	if (fs::is_directory(config.hackPath))
	{
		std::cout << KYEL << "Applying hacks to library!\n" << RST;

		uint32_t numCopied = 0;
		if (!CopyFilesRecursive(config.hackPath, config.srcPath, &numCopied))
		{
			std::cerr << KRED << "[BREAKING] Failed to apply hack to library '" << lib.name << "' in directory " << config.srcPath << "\n" << RST;
			return false;
		}

		std::cout << KYEL << "Applied " << numCopied << " hack files to library '" << lib.name << "'\n" << RST;
	}

	std::cout << KGRN << "Fetched library '" << lib.name << "' from repository " << lib.sourceURL << "' at hash " << lib.rootHash << "\n" << RST;
	return true;
}

static bool LibraryCloneRepo_URL(const LibraryManifest& lib, ToolLibraryConfig& config)
{
	const auto downloadPathName = PartAfterLast(lib.sourceURL, "/");
	if (downloadPathName.empty())
	{
		std::cerr << KRED << "[BREAKING] Download URL '" << lib.sourceURL << "' does not contain valid file name\n" << RST;
		return false;
	}

	const auto downloadFileName = (config.downloadRootPath / downloadPathName).make_preferred();
	{
		// curl --silent -z onion.exe -L -O https://github.com/BareMetalEngine/onion_tool/releases/latest/download/onion.exe
		std::stringstream cmd;
		cmd << "curl --silent -z ";
		cmd << downloadFileName;
		cmd << " -L -o ";
		cmd << downloadFileName;
		cmd << " ";
		cmd << lib.sourceURL;

		std::cout << cmd.str() << "\n";

		if (!RunWithArgs(cmd.str()))
		{
			std::cerr << KRED << "[BREAKING] Failed to download file from '" << lib.sourceURL << "\n" << RST;
			return false;
		}

		if (!fs::is_regular_file(downloadFileName))
		{ 
			std::cerr << KRED << "[BREAKING] Downloaded file '" << downloadFileName << "' does not exist\n" << RST;
			return false;
		}
	}

	if (!fs::is_directory(config.srcPath))
	{
		if (!CreateDirectories(config.srcPath))
			return false;

		std::stringstream cmd;
		cmd << "tar -xvf ";
		cmd << downloadFileName;
		cmd << " -C ";
		cmd << config.srcPath;

		std::cout << cmd.str() << "\n";

		if (!RunWithArgs(cmd.str()))
		{
			std::cerr << KRED << "[BREAKING] Failed to download file from '" << lib.sourceURL << "\n" << RST;
			return false;
		}
	}

	return true;
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

static std::string MakeDependencyArchiveFileName(ToolLibraryConfig& config, const LibraryDependencyInfo& dep)
{
	std::stringstream str;
	str << "libs/";
	str << NameEnumOption(config.platform);
	str << "/";
	str << dep.name;
	str << ".zip";
	return str.str();
}

struct DependencySymbol
{
	std::string name;
	fs::path value;
};

static bool LibraryInstallDependency(const LibraryManifest& lib, ToolLibraryConfig& config, const LibraryDependencyInfo& dep, std::vector<DependencySymbol>& outDefines)
{
	const auto dependencyDownloadDirectory = (config.dependenciesRootPath / "download").make_preferred();
	const auto dependencyUnpackDirectory = (config.dependenciesRootPath / "unpacked" / dep.name).make_preferred();

	if (!CreateDirectories(dependencyDownloadDirectory))
		return false;
	if (!CreateDirectories(dependencyUnpackDirectory))
		return false;

	const auto dependencyDirectory = (dependencyDownloadDirectory / dep.name).make_preferred();
	const auto dependencyFile = MakeDependencyArchiveFileName(config, dep);
	const auto dependencyFileFullPath = (dependencyDirectory / dependencyFile).make_preferred();

	// sync only if not there already
	if (!fs::is_directory(dependencyDirectory))
	{
		// partial sync of the target repo
		// git clone --no-checkout --filter=blob:none https://github.com/BareMetalEngine/dependencies.git
		{
			std::stringstream command;
			command << "git clone --sparse --no-checkout --filter=blob:none ";
			command << dep.repo;
			command << " ";
			command << dep.name;
			if (!RunWithArgsInDirectory(dependencyDownloadDirectory, command.str()))
			{
				std::cout << KRED << "Failed to fetch dependency repository " << dep.repo << "\n" << RST;
				return false;
			}
		}

		// setup the partial checkout
		{
			// git sparse-checkout set "/windows/zlib.zip"
			std::stringstream command;
			command << "git sparse-checkout set \"/"; // NOTE the / !!!
			command << dependencyFile;
			command << "\"";
			if (!RunWithArgsInDirectory(dependencyDirectory, command.str()))
			{
				std::cout << KRED << "Failed to download dependency library for " << dep.name << "\n" << RST;
				return false;
			}
		}

		// checkout the current lib file
		{
			// git sparse-checkout set "/windows/zlib.zip"
			std::stringstream command;
			command << "git checkout";
			if (!RunWithArgsInDirectory(dependencyDirectory, command.str()))
			{
				std::cout << KRED << "Failed to download dependency library for " << dep.name << "\n" << RST;
				return false;
			}
		}
	}

	// pull the latest file
	{
		// git sparse-checkout set "/windows/zlib.zip"
		std::stringstream command;
		command << "git pull";
		if (!RunWithArgsInDirectory(dependencyDirectory, command.str()))
		{
			std::cout << KRED << "Failed to download dependency library for " << dep.name << "\n" << RST;
			return false;
		}
	}

	// check if file exists 
	if (!fs::is_regular_file(dependencyFileFullPath))
	{
		std::cout << KRED << "Failed to download dependency archive " << dependencyFileFullPath << "\n" << RST;
		return false;
	}

	// decompress the directory
	{
		if (!CreateDirectories(dependencyUnpackDirectory))
			return false;

		std::stringstream cmd;
		cmd << "tar -xvf ";
		cmd << dependencyFileFullPath;
		cmd << " -C ";
		cmd << dependencyUnpackDirectory;

		std::cout << cmd.str() << "\n";

		if (!RunWithArgs(cmd.str()))
		{
			std::cerr << KRED << "[BREAKING] Failed to decompress downloaded dependency file '" << dependencyFileFullPath << "\n" << RST;
			return false;
		}
	}

	// check if library was properly unpacked
	const auto dependencyManifestPath = (dependencyUnpackDirectory / (dep.name + ".onion")).make_preferred();
	if (!fs::is_regular_file(dependencyManifestPath))
	{
		std::cerr << KRED << "[BREAKING] No library manifest found in unpacked dependency library at '" << dependencyManifestPath << "\n" << RST;
		return false;
	}

	// load the library manifest
	auto dependencyManifest = ExternalLibraryManifest::Load(dependencyManifestPath);
	if (!dependencyManifest)
	{
		std::cerr << KRED << "[BREAKING] Failed to load library manifest found in unpacked dependency library at '" << dependencyManifestPath << "\n" << RST;
		return false;
	}

	// include var ?
	if (!dep.includeVar.empty())
	{
		if (!dependencyManifest->includePath.empty())
		{
			std::cout << "Found include '" << dep.includeVar << "' as " << dependencyManifest->includePath << "\n";
			outDefines.push_back({ dep.includeVar, dependencyManifest->includePath });
		}
	}

	// libraries ?
	for (const auto& libVar : dep.libraryVars)
	{
		fs::path foundPath;

		if (libVar.fileName.empty())
		{
			if (dependencyManifest->libraryFiles.size() == 1)
			{
				foundPath = dependencyManifest->libraryFiles[0];
			}
		}
		else
		{
			for (const auto& libFile : dependencyManifest->libraryFiles)
			{
				if (libFile.filename().u8string() == libVar.fileName)
				{
					foundPath = libFile;
					break;
				}
			}
		}

		if (foundPath.empty())
		{
			std::cerr << KRED << "[BREAKING] Dependency library '" << dep.name << "' has no library file named '" << libVar.fileName << "'\n" << RST;
			return false;
		}
		else
		{
			std::cout << "Found library '" << libVar.varName << "' as " << foundPath << "\n";
			outDefines.push_back({ libVar.varName, foundPath });
		}
	}

	return true;
}

//--

static std::string FormatAdditionalDefines(const std::vector<DependencySymbol>& defs)
{
	std::stringstream str;

	for (const auto& def : defs)
	{
		str << "-D";
		str << def.name;
		str << "=";
		str << def.value;
		str << " ";
	}

	return str.str();
}

static bool LibraryConfigure(const LibraryManifest& lib, ToolLibraryConfig& config)
{
	// make sure build directory exists
	if (!CreateDirectories(config.buildPath))
		return false;

	// install dependencies
	std::vector<DependencySymbol> additionalDefines;
	for (const auto& dep : lib.dependencies)
		if (!LibraryInstallDependency(lib, config, dep, additionalDefines))
			return false;
	
	// run the config command in the build directory
	const auto runDirectory = fs::weakly_canonical((config.buildPath / lib.configRelativePath).make_preferred());

	// determine the relative path
	const auto sourceRelativeToBuild = fs::relative(config.srcPath, runDirectory).make_preferred();

	// replace some stuff in the command
	auto command = lib.configCommand;
	command = ReplaceAll(command, "${SourcePath}", sourceRelativeToBuild.u8string());
	command = ReplaceAll(command, "${SourceAbsPath}", config.srcPath.u8string());
	command = ReplaceAll(command, "${AdditionalDefines}", FormatAdditionalDefines(additionalDefines));

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
	const auto sourceRelativeToRun = fs::relative(config.srcPath, runDirectory).make_preferred();

	// replace some stuff in the command
	auto command = lib.buildCommand;
	command = ReplaceAll(command, "${SourcePath}", sourceRelativeToRun.u8string());
	command = ReplaceAll(command, "${SourceAbsPath}", config.srcPath.u8string());
	command = ReplaceAll(command, "${BuildPath}", buildRelativeToRun.u8string());
	command = ReplaceAll(command, "${BuildAbsPath}", config.buildPath.u8string());
	command = ReplaceAll(command, "${NumThreads}", std::to_string(std::thread::hardware_concurrency()));

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
		command << "tar -cavf ";
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

static bool LibraryCommit(GitHubConfig& git, const LibraryManifest& lib, ToolLibraryConfig& config)
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

	// target file for publishing
	std::string libraryFile;
	libraryFile += "libs/";
	libraryFile += NameEnumOption(config.platform);
	libraryFile += "/";
	libraryFile += lib.name;
	libraryFile += ".zip";

	// determine checkout directory
	uint32_t count = 1;
	auto checkoutDirName = std::string("upload");
	auto checkoutDir = (config.commitRootPath / checkoutDirName).make_preferred();
	while (fs::is_directory(checkoutDir))
	{
		checkoutDirName = std::string("upload") + std::to_string(count);
		checkoutDir = (config.commitRootPath / checkoutDirName).make_preferred();
		count += 1;
	}

	// list found directory
	std::cout << "Found unused upload directory at " << checkoutDir << "\n";

	// partial sync of the target repo
	// git clone --no-checkout --filter=blob:none https://github.com/BareMetalEngine/dependencies.git
	{
		std::stringstream command;
		command << "git clone --sparse --no-checkout --filter=blob:none ";
		command << config.commitRepo;
		command << " ";
		command << checkoutDirName;
		if (!RunWithArgsInDirectory(config.commitRootPath, command.str()))
		{
			std::cout << KRED << "Failed to package " << archivePath << "\n" << RST;
			return false;
		}
	}

	// setup the partial checkout
	{
		// git sparse-checkout set "/windows/zlib.zip"
		std::stringstream command;
		command << "git sparse-checkout set \"/"; // NOTE the / !!!
		command << libraryFile;
		command << "\"";
		if (!RunWithArgsInDirectory(checkoutDir, command.str()))
		{
			std::cout << KRED << "Failed to setup sparse checkout\n" << RST;
			return false;
		}
	}

	// checkout the current lib file
	{
		// git sparse-checkout set "/windows/zlib.zip"
		std::stringstream command;
		command << "git checkout";
		if (!RunWithArgsInDirectory(checkoutDir, command.str()))
		{
			std::cout << KRED << "Failed to setup sparse checkout\n" << RST;
			return false;
		}
	}

	// copy the file
	const auto targetFile = (checkoutDir / libraryFile).make_preferred();
	if (!CopyFile(archivePath, targetFile))
	{
		std::cout << KRED << "Failed to copy " << archivePath << "\n" << RST;
		return false;
	}

	// add to commit
	{
		// git add "windows/zlib.zip"
		std::stringstream command;
		command << "git add \"";
		command << libraryFile;
		command << "\"";
		if (!RunWithArgsInDirectory(checkoutDir, command.str()))
		{
			std::cout << KRED << "Failed to setup sparse checkout\n" << RST;
			return false;
		}
	}

	// nothing to commit ?
	// git diff --exit-code
	/*{
		int outCode = 0;

		// git add "windows/zlib.zip"
		std::stringstream command;
		command << "git diff --cached --exit-code";
		RunWithArgsInDirectory(checkoutDir, command.str(), &outCode);

		if (0 == outCode)
		{
			std::cout << "Nothing to submit!\n";
			return true;
		}
	}*/

	// make a commit
	{
		// git add "windows/zlib.zip"
		std::stringstream command;
		command << "git commit --allow-empty -m \"[ci skip] Uploaded ";
		command << lib.name;
		command << " for ";
		command << NameEnumOption(config.platform);
		if (!lib.rootHash.empty())
			command << " (built from hash " << lib.rootHash << ")";
		command << "\"";
		if (!RunWithArgsInDirectory(checkoutDir, command.str()))
		{
			std::cout << KRED << "Failed to create commit\n" << RST;
			return false;
		}
	}

	// push the update
	double waitTime = 1.0;
	int numRetries = 20;
	while (numRetries-- > 0)
	{
		// push the update
		{
			// git add "windows/zlib.zip"
			std::stringstream command;
			command << "git push ";
			if (!config.commitToken.empty())
			{
				const auto coreRepoName = PartAfter(config.commitRepo, "https://");
				std::cout << "Repo name: '" << config.commitRepo << "'\n";
				std::cout << "Repo core: '" << coreRepoName << "'\n";
				command << "-q https://" << config.commitToken << "@" << coreRepoName;
			}

			if (RunWithArgsInDirectory(checkoutDir, command.str()))
			{
				std::cout << KGRN << "New packed library pushed\n" << RST;
				return true;
			}
		}

		// wait
		std::cout << "Initial push failed, waiting for " << waitTime << "\n";
		std::this_thread::sleep_for(std::chrono::duration<double>(waitTime));
		waitTime = waitTime * 1.5f;

		// pull with rebase
		{
			// git add "windows/zlib.zip"
			std::stringstream command;
			command << "git pull --rebase";
			if (!RunWithArgsInDirectory(checkoutDir, command.str()))
			{
				std::cout << KRED << "Failed to rebase after failed push\n" << RST;
				return false;
			}
		}
	}

	// updated
	std::cout << KRED << "Failed to push file\n" << RST;
	return false;
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
	config.hackPath = (config.hacksRootPath / library->name).make_preferred();
	std::cout << "Hack path: " << config.hackPath << " " << fs::is_directory(config.hackPath) << "\n";

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

	if (config.commitToGitHub)
	{
		if (!LibraryCommit(git, *library, config))
		{
			std::cerr << KRED << "[BREAKING] Release step for library " << library->name << " failed\n" << RST;
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