#include "common.h"
#include "utils.h"
#include "externalLibrary.h"
#include "externalLibraryRepository.h"

//--

ExternalLibraryReposistory::ExternalLibraryReposistory(const fs::path& cachePath, PlatformType platform)
	: m_platform(platform)
{
	m_downloadCachePath = (cachePath / "lib_download").make_preferred();
	m_unpackCachePath = (cachePath / "lib_unpack").make_preferred();
}

ExternalLibraryReposistory::~ExternalLibraryReposistory()
{
	for (auto* lib : m_libraries)
		delete lib;
}

bool ExternalLibraryReposistory::determineLibraryRepositoryNameName(std::string_view name, std::string_view& outRepository, std::string_view& outName, std::string_view& outBranch) const
{
	outBranch = "main";

	if (!SplitString(name, ":", outRepository, outName))
	{
		outRepository = DEFAULT_DEPENDENCIES_REPO;
		outName = name;
	}

	return true;
}

static std::string MakeLibraryPath(PlatformType platform, std::string_view name)
{
	std::string libraryFile;
	libraryFile += "libs/";
	libraryFile += NameEnumOption(platform);
	libraryFile += "/";
	libraryFile += name;
	libraryFile += ".zip";
	return libraryFile;
}

bool ExternalLibraryReposistory::deployFiles(const fs::path& targetPath) const
{
	bool valid = true;

	for (const auto* dep : m_libraries)
		valid &= dep->deployFilesToTarget(targetPath);

	return valid;
}

ExternalLibraryManifest* ExternalLibraryReposistory::installLibrary(std::string_view name)
{
	// library already installed
	{
		auto it = m_librariesMap.find(std::string(name));
		if (it != m_librariesMap.end())
			return it->second;
	}

	// determine the repository and 
	std::string_view libraryRepository, libraryName, libraryBranch;
	if (!determineLibraryRepositoryNameName(name, libraryRepository, libraryName, libraryBranch))
	{
		std::cout << KRED << "[BREAKING] Unable to determine repository for library '" << name << "'\n" << RST;
		m_librariesMap[std::string(name)] = nullptr;
		return nullptr;
	}

	// determine the storage path
	const auto repositoryStoragePath = (libraryRepository == DEFAULT_DEPENDENCIES_REPO) ? "default" : GuidFromText(libraryRepository);
	const auto downloadPath = (m_downloadCachePath / repositoryStoragePath / libraryName / libraryBranch).make_preferred();

	// download/sync library file
	const auto localLibraryFile = MakeLibraryPath(m_platform, libraryName);
	const auto actualLibraryFile = (downloadPath / localLibraryFile).make_preferred();
	std::cout << "Library file name: " << actualLibraryFile << "\n";

	// file exists
	if (!fs::is_directory(downloadPath))
	{
		// root path
		const auto downloadParentPath = downloadPath.parent_path();
		CreateDirectories(downloadParentPath);

		// partial sync of the target repo
		// git clone --no-checkout --filter=blob:none https://github.com/BareMetalEngine/dependencies.git
		{
			std::stringstream command;
			command << "git clone --sparse --no-checkout --filter=blob:none ";
			command << libraryRepository;
			command << " ";
			command << downloadPath.filename().u8string();
			if (!RunWithArgsInDirectory(downloadParentPath, command.str()))
			{
				std::cout << KRED << "[BREAKING] Failed to do a sparse checkout on " << libraryRepository << " into " << downloadPath << "\n" << RST;
				m_librariesMap[std::string(name)] = nullptr;
				return nullptr;
			}
		}

		// setup the partial checkout
		{
			// git sparse-checkout set "/windows/zlib.zip"
			std::stringstream command;
			command << "git sparse-checkout set \"/"; // NOTE the / !!!
			command << localLibraryFile;
			command << "\"";
			if (!RunWithArgsInDirectory(downloadPath, command.str()))
			{
				std::cout << KRED << "[BREAKING] Failed to setup sparse checkout for library '" << name << "'\n" << RST;
				m_librariesMap[std::string(name)] = nullptr;
				return nullptr;
			}
		}

		// checkout the current lib file
		{
			// git checkout
			std::stringstream command;
			command << "git checkout";
			if (!RunWithArgsInDirectory(downloadPath, command.str()))
			{
				std::cout << KRED << "[BREAKING] Failed to follow up with sparse checkout for library '" << name << "'\n" << RST;
				m_librariesMap[std::string(name)] = nullptr;
				return nullptr;
			}
		}
	}
	else
	{
		// get latest
		std::stringstream command;
		command << "git pull";
		if (!RunWithArgsInDirectory(downloadPath, command.str()))
		{
			std::cout << KRED << "[BREAKING] Failed to follow up with sparse checkout for library '" << name << "'\n" << RST;
			m_librariesMap[std::string(name)] = nullptr;
			return nullptr;
		}
	}

	// no archive file
	if (!fs::is_regular_file(actualLibraryFile))
	{
		std::cout << KRED << "[BREAKING] Downloaded file for library '" << name << "' does not exist: " << actualLibraryFile << "\n" << RST;
		m_librariesMap[std::string(name)] = nullptr;
		return nullptr;
	}

	// get the hash of the library file
	// git log -n 1 --pretty=format:%H -- build_windows.bat
	std::string libraryVersion;
	{
		std::stringstream command, results;
		command << "git log -n 1 --pretty=format:%H -- ";
		command << localLibraryFile;
		if (!RunWithArgsInDirectoryAndCaptureOutput(downloadPath, command.str(), results))
		{
			std::cout << KRED << "[BREAKING] Failed to do a sparse checkout on " << libraryRepository << " into " << downloadPath << "\n" << RST;
			m_librariesMap[std::string(name)] = nullptr;
			return nullptr;
		}

		libraryVersion = results.str();
		std::cout << KGRN << "Library '" << name << "' is at version '" << libraryVersion << "'\n" << RST;
	}

	// each library version is unpacked to separate directory
	// TODO: garbage collect old directories!!!!
	const auto unpackPath = (m_unpackCachePath / repositoryStoragePath / libraryName / libraryVersion).make_preferred();
	if (!fs::is_directory(unpackPath))
	{
		std::cout << "Library '" << name << "' is not yet unpacked and will be unpacked\n";

		CreateDirectories(unpackPath);

		// unpack
		{
			std::stringstream cmd;
			cmd << "tar -xvf ";
			cmd << actualLibraryFile;
			cmd << " -C ";
			cmd << unpackPath;
			if (!RunWithArgs(cmd.str()))
			{
				std::cerr << KRED << "[BREAKING] Failed to unpack library '" << name << " into " << unpackPath << "\n" << RST;
				m_librariesMap[std::string(name)] = nullptr;
				return nullptr;
			}
		}
	}

	// we should have matching manifest path 
	const auto unpackManifestPath = unpackPath / (std::string(libraryName) + ".onion");
	if (!fs::is_regular_file(unpackManifestPath))
	{
		std::cerr << KRED << "[BREAKING] Unpacked library '" << name << " into " << unpackPath << " has missing manifest file\n" << RST;
		m_librariesMap[std::string(name)] = nullptr;
		return nullptr;
	}

	// load the manifest
	auto manifest = ExternalLibraryManifest::Load(unpackManifestPath);
	if (!manifest)
	{
		std::cerr << KRED << "[BREAKING] Unpacked library '" << name << " into " << unpackPath << " has INVALID manifest file\n" << RST;
		m_librariesMap[std::string(name)] = nullptr;
		return nullptr;
	}

	// create the entry
	auto* libPtr = manifest.release();
	m_librariesMap[std::string(name)] = libPtr;
	m_libraries.push_back(libPtr);
	std::cout << "Registered library '" << libPtr->name << "' at " << unpackManifestPath << "\n";
	return libPtr;
}

//--