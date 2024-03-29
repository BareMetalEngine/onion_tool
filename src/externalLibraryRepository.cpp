#include "common.h"
#include "utils.h"
#include "externalLibrary.h"
#include "externalLibraryRepository.h"
#include "moduleConfiguration.h"

//--

ExternalLibraryReposistory::ExternalLibraryReposistory()
{
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
		return false;

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

bool ExternalLibraryReposistory::deployFiles(ConfigurationType configuration, const fs::path& targetPath) const
{
	bool valid = true;

	for (const auto* dep : m_libraries)
		valid &= dep->deployFilesToTarget(m_platform, configuration, targetPath);

	return valid;
}

bool ExternalLibraryReposistory::installConfiguredLibraries(const ModuleConfigurationManifest& config)
{
	bool valid = true;

	for (const auto& lib : config.libraries)
		valid &= installLibrary(lib.name, lib.path);

	return valid;
}

const ExternalLibraryManifest* ExternalLibraryReposistory::findLibrary(std::string_view name) const
{
	auto it = m_librariesMap.find(std::string(name));
	if (it != m_librariesMap.end())
		return it->second;

	return nullptr;
}

bool ExternalLibraryReposistory::installLibrary(std::string_view name, const fs::path& path)
{
	// library already installed
	{
		auto it = m_librariesMap.find(std::string(name));
		if (it != m_librariesMap.end())
		{
			if (it->second->rootPath == path)
				return true;

			LogError() << "Library '" << name << "' is already installed at " << it->second->rootPath << ", second location at " << path << " ignored";
			return false;
		}
	}

	// load the manifest
	auto manifest = ExternalLibraryManifest::Load(path);
	if (!manifest)
	{
		LogError() << "Library '" << name << " at " << path << " has INVALID manifest file";
		return false;
	}

	// create the entry
	auto* libPtr = manifest.release();
	m_librariesMap[std::string(name)] = libPtr;
	m_libraries.push_back(libPtr);
	LogInfo() << "Registered library '" << libPtr->name << "' at " << path;
	return libPtr;
}

//--