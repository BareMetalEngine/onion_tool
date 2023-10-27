#include "common.h"
#include "utils.h"
#include "toolLibrary.h"
#include "toolRelease.h"
#include "libraryManifest.h"
#include "externalLibrary.h"
#include "externalLibraryInstaller.h"
#include "aws.h"
#include <thread>

//--

struct ToolLibraryConfig
{
	fs::path libraryManifestPath; // file path
	fs::path tempRootPath; // ($ManifsetDir)/.temp
	fs::path srcRootPath; // ($ManifsetDir)/.temp/<platform/source
	fs::path buildRootPath; // ($ManifsetDir)/.temp/<platform/build
	fs::path deployRootPath; // ($ManifsetDir)/.temp/<platform/out
	fs::path packageRootPath; // ($ManifsetDir)/.temp/<platform/package
	fs::path commitRootPath; // ($ManifsetDir)/.temp/<platform/commit
	fs::path downloadRootPath; // ($ManifsetDir)/.temp/<platform/download
	fs::path dependenciesRootPath; // ($ManifsetDir)/.temp/<platform/dependencies
	fs::path hacksRootPath; // ($ManifsetDir)/hacks
	PlatformType platform = DefaultPlatform();

	bool performClone = true;
	bool performConfigure = true;
	bool performBuild = true;
	bool performDeploy = true;	
	bool performPackage = true;

	bool ignorePullErrors = false;
	bool forceOperation = false;
	bool upload = false;

    fs::path unpackPath; // {$ManifsetDir}/.temp/<platform/source/{$LibraryName}
	fs::path srcPath; // {$ManifsetDir}/.temp/<platform/source/{$LibraryName}
	fs::path buildPath; // {$ManifsetDir}/.temp/<platform/build/{$LibraryName}
	fs::path deployPath; // {$ManifsetDir}/.temp/<platform/out/{$LibraryName}
	fs::path hackPath; // {$ManifsetDir}/hacks/{$LibraryName}
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
				std::stringstream str2;
				str2 << PrintEnumOptions(DefaultPlatform());

				LogError() << "Unknown platform \"" << str << "\"";
				LogInfo() << "Valid platforms are : " << str2.str();
				return false;
			}
		}
	}

	{
		const auto str = cmdline.get("library");
		if (str.empty())
		{
			LogError() << "Missing -library argument";
			return false;
		}

		const auto libraryPath = fs::weakly_canonical(fs::path(str).make_preferred());
		if (!fs::is_regular_file(libraryPath))
		{
			LogError() << "File " << libraryPath << " does not exist";
			return false;
		}

		outConfig.libraryManifestPath = libraryPath;
	}

	{
		const auto str = cmdline.get("tempPath");
		if (str.empty())
		{
			const auto platformName = NameEnumOption(outConfig.platform);
			outConfig.tempRootPath = (outConfig.libraryManifestPath.parent_path() / ".temp" / platformName).make_preferred();
		}
		else
		{
			outConfig.tempRootPath = fs::weakly_canonical(fs::path(str).make_preferred());
		}

		if (!CreateDirectories(outConfig.tempRootPath))
		{
			LogError() << "Failed to create directory \"" << outConfig.tempRootPath;
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
			LogError() << "Failed to create directory \"" << outConfig.srcRootPath;
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
			LogError() << "Failed to create directory \"" << outConfig.buildRootPath;
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
			LogError() << "Failed to create directory \"" << outConfig.commitRootPath;
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
			LogError() << "Failed to create directory \"" << outConfig.deployRootPath;
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
			LogError() << "Failed to create directory \"" << outConfig.packageRootPath;
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
			LogError() << "Failed to create directory \"" << outConfig.downloadRootPath;
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
			LogError() << "Failed to create directory \"" << outConfig.dependenciesRootPath;
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

	if (cmdline.has("upload"))
		outConfig.upload = true;

	outConfig.forceOperation = cmdline.has("force");
	return true;
}

//--

static bool LibraryCloneRepo_GitHub(const LibraryManifest& lib, ToolLibraryConfig& config)
{
	if (!RunWithArgs("git --version"))
	{
		LogError() << "Git not found in PATH, please install it";
		return false;
	}

	// clone/pull
	if (fs::is_directory(config.srcPath))
	{
		LogInfo() << "Source directory " << config.srcPath << " already exists, syncing only";

		if (!lib.config.sourceBuild)
		{
			RunWithArgsInDirectory(config.srcPath, "git reset --hard");
			RunWithArgsInDirectory(config.srcPath, "git clean -xfd");

			if (!RunWithArgsInDirectory(config.srcPath, "git pull"))
			{
				if (config.ignorePullErrors)
				{
					LogWarning() << "Failed to update library '" << lib.name << "' from repository " << lib.config.sourceURL << ", using existing code";
				}
				else
				{
					LogError() << "Failed to update library '" << lib.name << "' from repository " << lib.config.sourceURL;
					return false;
				}
			}
		}
	}
	else
	{
		std::stringstream command;
		command << "git clone --depth 1 --single-branch --recurse-submodules ";
		if (!lib.config.sourceBranch.empty())
			command << "--branch " << lib.config.sourceBranch << " ";
		command << lib.config.sourceURL;
		command << " ";
		command << lib.name;

		if (!RunWithArgsInDirectory(config.srcRootPath, command.str()))
		{
			LogError() << "Failed to clone library '" << lib.name << "' from repository " << lib.config.sourceURL << "'";
			return false;
		}
	}

	// cloned
	LogInfo() << "Source directory " << config.srcPath << " cloned from '" << lib.config.sourceURL << "'";

	// verify repository
	{
		if (!RunWithArgsInDirectory(config.srcPath, "git fsck --full"))
		{
			LogError() << "Failed to verify library '" << lib.name << "' fetched from repository " << lib.config.sourceURL << "'";
			return false;
		}
	}

	// get head commit hash, this verifies that the git repo is valid
	{
		std::stringstream hash;
		if (!RunWithArgsInDirectoryAndCaptureOutput(config.srcPath, "git rev-parse --verify HEAD", hash))
		{
			LogError() << "Failed to fetch root hash from library '" << lib.name << "' fetched from repository " << lib.config.sourceURL << "'";
			return false;
		}

		lib.rootHash = Trim(hash.str());
	}

	// apply hacks
	if (fs::is_directory(config.hackPath))
	{
		LogWarning() << "Applying hacks to library!";

		uint32_t numCopied = 0;
		if (!CopyFilesRecursive(config.hackPath, config.srcPath, &numCopied))
		{
			LogError() << "Failed to apply hack to library '" << lib.name << "' in directory " << config.srcPath;
			return false;
		}

		LogWarning() << "Applied " << numCopied << " hack files to library '" << lib.name << "'";
	}

	LogSuccess() << "Fetched library '" << lib.name << "' from repository " << lib.config.sourceURL << "' at hash " << lib.rootHash;
	return true;
}

static bool LibraryCloneRepo_URL(const LibraryManifest& lib, ToolLibraryConfig& config)
{
	const auto downloadPathName = PartAfterLast(lib.config.sourceURL, "/");
	if (downloadPathName.empty())
	{
		LogError() << "Download URL '" << lib.config.sourceURL << "' does not contain valid file name";
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
		cmd << lib.config.sourceURL;

		LogInfo() << cmd.str();

		if (!RunWithArgs(cmd.str()))
		{
			LogError() << "Failed to download file from '" << lib.config.sourceURL;
			return false;
		}

		if (!fs::is_regular_file(downloadFileName))
		{ 
			LogError() << "Downloaded file '" << downloadFileName << "' does not exist";
			return false;
		}
	}

	if (!fs::is_directory(config.unpackPath))
	{
		const auto srcPath = (config.srcRootPath / lib.name).make_preferred();
		if (!CreateDirectories(srcPath))
			return false;

		std::stringstream cmd;
		cmd << "tar -xvf ";
		cmd << downloadFileName;
		cmd << " -C ";
		cmd << srcPath;

		if (!RunWithArgs(cmd.str()))
		{
			LogError() << "Failed to download file from '" << lib.config.sourceURL;
			return false;
		}
	}

	return true;
}

static bool LibraryCloneRepo(const LibraryManifest& lib, ToolLibraryConfig& config)
{
	if (lib.config.sourceType == LibrarySourceType::GitHub)
		return LibraryCloneRepo_GitHub(lib, config);
	else if (lib.config.sourceType == LibrarySourceType::FileOnTheInternet)
		return LibraryCloneRepo_URL(lib, config);
	else
		return false;
}

//--

struct DependencySymbol
{
	std::string name;
	fs::path value;
};

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

static bool LibraryConfigure(const Commandline& cmdLine, const LibraryManifest& lib, ToolLibraryConfig& config)
{
	// make sure build directory exists
	if (!CreateDirectories(config.buildPath))
		return false;

	// install dependencies
	std::vector<DependencySymbol> additionalDefines;
	if (!lib.config.dependencies.empty())
	{
		LibraryInstaller libraryInstaller(config.platform, config.dependenciesRootPath);
		if (!libraryInstaller.installOfflinePackedDirectory(config.packageRootPath))
			return false;

		for (const auto& dep : lib.config.dependencies)
		{
			std::string verison;
			fs::path manifestPath;
            std::unordered_set<std::string> requiredPackages;
			if (libraryInstaller.install(dep.name, &manifestPath, &verison, &requiredPackages))
			{
				// load the library manifest
				auto dependencyManifest = ExternalLibraryManifest::Load(manifestPath);
				if (!dependencyManifest)
				{
					LogError() << "Failed to load library manifest found in unpacked dependency library at '" << manifestPath;
					return false;
				}

				// include var ?
				if (!dep.includeVar.empty())
				{
					std::vector<fs::path> includePaths;
					dependencyManifest->collectIncludeDirectories(config.platform, &includePaths);

					for (const auto& path : includePaths)
					{
						LogInfo() << "Found include '" << dep.includeVar << "' as " << path;
						additionalDefines.push_back({ dep.includeVar, path });
					}
				}

				// libraries ?
				for (const auto& libVar : dep.libraryVars)
				{
					fs::path foundPath;

					std::vector<fs::path> libraryPaths;
					dependencyManifest->collectLibraries(config.platform, &libraryPaths);

					if (libVar.fileName.empty())
					{
						if (libraryPaths.size() == 1)
						{
							foundPath = libraryPaths[0];
						}
					}
					else
					{
						for (const auto& libFile : libraryPaths)
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
						if (!libVar.fileName.empty())
						{
							LogError() << "Dependency library '" << dep.name << "' has no library file named '" << libVar.fileName << "'";
							return false;
						}
						else
						{
							if (!libraryPaths.size())
							{
								LogError() << "Dependency library '" << dep.name << "' has no library file to link with!";
							}
							else
							{
								LogError() << "Dependency library '" << dep.name << "' has more than one library file to link with. Specify file name directly.!";

								for (const auto& libFile : libraryPaths)
								{
									LogInfo() << "Potential library file: '" << libFile << "'";
								}
							}
						}

						return false;
					}
					else
					{
						LogInfo() << "Found library '" << libVar.varName << "' as " << foundPath;
						additionalDefines.push_back({ libVar.varName, foundPath });
					}
				}
			}
			else
			{
				LogError() << "Dependency library '" << dep.name << "' failed to install";
				return false;
			}
		}
	}
	
	// run the config command in the build directory
	const auto runDirectory = fs::weakly_canonical((config.buildPath / lib.config.configRelativePath).make_preferred());

	// determine the relative path
	const auto sourceRelativeToBuild = fs::relative(config.srcPath, runDirectory).make_preferred();

	// replace some stuff in the command
	auto command = lib.config.configCommand;
	command = ReplaceAll(command, "${SourcePath}", sourceRelativeToBuild.u8string());
	command = ReplaceAll(command, "${SourceAbsPath}", config.srcPath.u8string());
	command = ReplaceAll(command, "${AdditionalDefines}", FormatAdditionalDefines(additionalDefines));

	// run the command
	if (!RunWithArgsInDirectory(runDirectory, command))
	{
		LogError() << "Failed to configure library '" << lib.name << "'";
		return false;
	}

	// configured	
	LogSuccess() << "Library '" << lib.name << "' configured";
	return true;
}

//--

static bool LibraryBuild(const LibraryManifest& lib, ToolLibraryConfig& config)
{
	// make sure build directory exists
	if (!CreateDirectories(config.buildPath))
		return false;

	// run the config command in the build directory
	const auto runDirectory = fs::weakly_canonical((config.buildPath / lib.config.buildRelativePath).make_preferred());

	// determine the relative path
	const auto buildRelativeToRun = fs::relative(config.buildPath, runDirectory).make_preferred();
	const auto sourceRelativeToRun = fs::relative(config.srcPath, runDirectory).make_preferred();

	// replace some stuff in the command
	auto command = lib.config.buildCommand;
	command = ReplaceAll(command, "${SourcePath}", sourceRelativeToRun.u8string());
	command = ReplaceAll(command, "${SourceAbsPath}", config.srcPath.u8string());
	command = ReplaceAll(command, "${BuildPath}", buildRelativeToRun.u8string());
	command = ReplaceAll(command, "${BuildAbsPath}", config.buildPath.u8string());

#ifdef _WIN32
	{
		command = ReplaceAll(command, "${MT}", "-- -m");
	}
#else
	{
		const auto arguments = std::string("-- -j") + std::to_string(std::thread::hardware_concurrency());
		command = ReplaceAll(command, "${MT}", arguments);
	}
#endif

	// run the command
	if (!RunWithArgsInDirectory(runDirectory, command))
	{
		LogError() << "Failed to build library '" << lib.name << "'";
		return false;
	}

	// configured	
	LogSuccess() << "Library '" << lib.name << "' built";
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

	if (!fs::is_directory(sourcePath))
		return 0;

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

	for (const auto& info : lib.config.artifacts)
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
					LogError() << "Failed to collect build artifacts at '" << searchParentDir << "' in form of " << searchFileName << "." << searchExtension <<
						(info.recursive ? " (recrusive)" : "(non recrusive)");
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
					LogError() << "Failed to collect build artifact at '" << fullSearchFile;
					valid = false;
				}
			}
		}
	}

	return valid;
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

    for (const auto& name : lib.config.additionalSystemLibraries)
        writelnf(f, "<AdditionalSystemLibrary>%hs</AdditionalSystemLibrary>", name.c_str());
    for (const auto& name : lib.config.additionalSystemPackages)
        writelnf(f, "<AdditionalSystemPackage>%hs</AdditionalSystemPackage>", name.c_str());
    for (const auto& name : lib.config.additionalSystemFrameworks)
        writelnf(f, "<AdditionalSystemFramework>%hs</AdditionalSystemFramework>", name.c_str());

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
	LogSuccess() << "Collected " << artifacts.size() << " artifacts for library '" << lib.name << "'";

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
		LogError() << "Failed to deploy all files for library '" << lib.name << "'";
		return false;
	}

	// done
	LogSuccess() << "Deployed " << numActuallyCopied << " file(s) (out of total " << artifacts.size() << ") for library '" << lib.name << "'";
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
		LogError() << "Tar not found in PATH, please install it";
		return false;
	}

	// get manifest for the library and load it
	const auto manifestPath = LibraryManifestPath(lib, config);
	const auto manifest = ExternalLibraryManifest::Load(manifestPath);
	if (!manifest)
	{
		LogError() << "Failed to load output manifest for library '" << lib.name << "', was the library built and deployed correctly?";
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
			LogError() << "Required file " << filePath << " is missing";
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
				LogWarning() << "Output archive " << archivePath << " is up to date but a -force switch is used so it will be rebuilt";
			}
			else
			{
				LogSuccess() << "Output archive " << archivePath << " is up to date, skipping packaging!";
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
			LogError() << "Failed to package " << archivePath;
			return false;
		}
	}

	// done
	LogSuccess() << "Packaged library '" << lib.name << "'";
	return true;
}

//--

static std::string MakeLibraryObjectName(PlatformType platform, std::string_view name)
{
	std::stringstream str;
	str << "libraries/";
	str << NameEnumOption(platform);
	str << "/";
	str << name;
	str << ".zip";
	return str.str();
}

static bool LibraryUpload(AWSConfig& aws, const LibraryManifest& lib, ToolLibraryConfig& config)
{
	// get manifest for the library and load it
	const auto manifestPath = LibraryManifestPath(lib, config);
	const auto manifest = ExternalLibraryManifest::Load(manifestPath);
	if (!manifest)
	{
		LogError() << "Failed to load output manifest for library '" << lib.name << "', was the library built and deployed correctly?";
		return false;
	}

	// get archive name
	const auto archivePath = LibraryArchivePath(*manifest, config);
	if (!fs::is_regular_file(archivePath))
	{
		LogError() << "Archived library file " << archivePath << " does not exist, there's nothing to publish" << RST;
		return false;
	}

	// upload file
	if (!AWS_S3_UploadLibrary(aws, archivePath, config.platform, manifest->name))
	{
		LogError() << "Archived library file " << archivePath << " failed to upload to AWS" << RST;
		return false;
	}

	// uploaded
	LogSuccess() << "Archived library file " << archivePath << " was uploaded to AWS S3" << RST;
	return true;	
}

//--

#if 0
static bool LibraryCommit(GitHubConfig& git, const LibraryManifest& lib, ToolLibraryConfig& config)
{
	// get manifest for the library and load it
	const auto manifestPath = LibraryManifestPath(lib, config);
	const auto manifest = ExternalLibraryManifest::Load(manifestPath);
	if (!manifest)
	{
		LogError() << "Failed to load output manifest for library '" << lib.name << "', was the library built and deployed correctly?";
		return false;
	}

	// get archive name
	const auto archivePath = LibraryArchivePath(*manifest, config);
	if (!fs::is_regular_file(archivePath))
	{
		LogError() << "Archived library file " << archivePath << " does not exist, there's nothing to publish" << RST;
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
	LogInfo() << "Found unused upload directory at " << checkoutDir;

	// push the update
	double waitTime = 1.0;
	int numRetries = 20;
	while (numRetries-- > 0)
	{
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
				LogError() << "Failed to do a sparse checkout on " << config.commitRepo << " into " << checkoutDirName;
				return false;
			}
		}

		// setup the partial checkout
		{
			// git sparse-checkout set "windows/zlib.zip"
			std::stringstream command;
			command << "git sparse-checkout set \"";
			command << libraryFile;
			command << "\"";
			if (!RunWithArgsInDirectory(checkoutDir, command.str()))
			{
				LogError() << "Failed to setup sparse checkout";
				return false;
			}
		}

		// checkout the current lib file
		{
			// git checkout
			std::stringstream command;
			command << "git checkout";
			if (!RunWithArgsInDirectory(checkoutDir, command.str()))
			{
				LogError() << "Failed to setup sparse checkout";
				return false;
			}
		}

		// copy the file
		const auto targetFile = (checkoutDir / libraryFile).make_preferred();
		if (!CopyFile(archivePath, targetFile))
		{
			LogError() << "Failed to copy " << archivePath;
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
				LogError() << "Failed to setup sparse checkout";
				return false;
			}
		}

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
				LogError() << "Failed to create commit";
				return false;
			}
		}
	
		// push the update
		{
			// git add "windows/zlib.zip"
			std::stringstream command;
			command << "git push ";
			if (!config.commitToken.empty())
			{
				const auto coreRepoName = PartAfter(config.commitRepo, "https://");
				LogInfo() << "Repo name: '" << config.commitRepo << "'";
				LogInfo() << "Repo core: '" << coreRepoName << "'";
				command << "-q https://" << config.commitToken << "@" << coreRepoName;
			}

			if (RunWithArgsInDirectory(checkoutDir, command.str()))
			{
				LogSuccess() << "New packed library pushed";
				return true;
			}
		}

		// wait
		LogInfo() << "Initial push failed, waiting for " << waitTime;
		std::this_thread::sleep_for(std::chrono::duration<double>(waitTime));
		waitTime = waitTime * 1.5f;

		// remove folder and try again
		fs::remove_all(checkoutDir);
	}

	// updated
	LogError() << "Failed to push file";
	return false;
}
#endif

//--

ToolLibrary::ToolLibrary()
{}

void ToolLibrary::printUsage()
{
	auto platform = DefaultPlatform();

	std::stringstream str;
	str << PrintEnumOptions(platform);

	LogInfo() << "onion library [options]";
	LogInfo() << "";
	LogInfo() << "Build configuration options:";
	LogInfo() << "  -platform=" << str.str();
	LogInfo() << "";
	LogInfo() << "General options:";
	LogInfo() << "  -library=<library to build>";
	LogInfo() << "  -step=[clone|configure|build|deploy]";
	LogInfo() << "  -srcDir=<path to source directory where original repository is downloaded>";
	LogInfo() << "  -buildDir=<path to build directory where all the build files are stored>";
	LogInfo() << "  -deployDir=<path where all final library files and includes are copied to>";
	LogInfo() << "";
}

int ToolLibrary::run(const Commandline& cmdline)
{
	ToolLibraryConfig config;
	if (!ParseArgs(cmdline, config))
		return 1;

	//--

	LibraryFilters filters;
	filters.platform = config.platform;

	const auto library = LibraryManifest::Load(config.libraryManifestPath, filters);
	if (!library)
	{
		LogError() << "Failed to load library manifest from " << config.libraryManifestPath;
		return 1;
	}

	if (library->config.sourceRelativePath.empty())
		config.srcPath = (config.srcRootPath / library->name).make_preferred();
	else
		config.srcPath = (config.srcRootPath / library->name / library->config.sourceRelativePath).make_preferred();
	if (library->config.sourceBuild)
		config.buildPath = config.srcPath;
	else
		config.buildPath = (config.buildRootPath / library->name).make_preferred();
	config.deployPath = (config.deployRootPath / library->name).make_preferred();
	config.hackPath = (config.hacksRootPath / library->name).make_preferred();
	LogInfo() << "Hack path: " << config.hackPath << " " << fs::is_directory(config.hackPath);

	//--

	AWSConfig aws(true);
	if (config.upload)
	{
		if (!aws.init(cmdline))
		{
			LogError() << "Failed to initialize AWS interface";
			return-1;
		}
	}

	if (config.performClone)
	{
		if (!LibraryCloneRepo(*library, config))
		{
			LogError() << "Clone step for library " << library->name << " failed";
			return 1;
		}
	}

	if (config.performConfigure)
	{
		if (!LibraryConfigure(cmdline, *library, config))
		{
			LogError() << "Configure step for library " << library->name << " failed";
			return 1;
		}
	}

	if (config.performBuild)
	{
		if (!LibraryBuild(*library, config))
		{
			LogError() << "Build step for library " << library->name << " failed";
			return 1;
		}
	}

	if (config.performDeploy)
	{
		if (!LibraryDeploy(*library, config))
		{
			LogError() << "Deploy step for library " << library->name << " failed";
			return 1;
		}
	}

	if (config.performPackage)
	{
		if (!LibraryPackage(*library, config))
		{
			LogError() << "Package step for library " << library->name << " failed";
			return 1;
		}
	}

	if (config.upload)
	{
		if (!LibraryUpload(aws, *library, config))
		{
			LogError() << "Upload step for library " << library->name << " failed";
			return 1;
		}
	}

	//--

	// done
	return 0;
}

//--