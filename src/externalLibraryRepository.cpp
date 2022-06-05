#include "common.h"
#include "utils.h"
#include "externalLibrary.h"
#include "externalLibraryRepository.h"

//--

ExternalLibraryReposistory::ExternalLibraryReposistory(const fs::path& cachePath)
{
	m_downloadCachePath = (cachePath / "lib_download").make_preferred();
	m_unpackCachePath = (cachePath / "lib_unpack").make_preferred();
}

ExternalLibraryReposistory::~ExternalLibraryReposistory()
{
	for (auto* lib : m_libraries)
		delete lib;
}

bool ExternalLibraryReposistory::installLocalLibraryPack(const fs::path& manifestPath)
{
	auto lib = ExternalLibraryManifest::Load(manifestPath);
	if (!lib)
	{
		std::cout << KYEL << "File " << manifestPath << "does not contain library manifest\n" << RST;
		return false;
	}

	if (m_librariesMap.find(lib->name) != m_librariesMap.end())
	{
		std::cout << KYEL << "Library '" << lib->name << "' is already defined, skipping second definition\n" << RST;
		return false;
	}

	auto* libPtr = lib.release();
	m_librariesMap[lib->name] = libPtr;
	m_libraries.push_back(libPtr);
	std::cout << "Registered library '" << libPtr->name << "' at " << manifestPath << "\n";
	return true;
}

bool ExternalLibraryReposistory::determineLibraryRepositoryNameName(std::string_view name, std::string_view& outRepository, std::string_view& outName) const
{
	return false;
}

bool ExternalLibraryReposistory::installLibrary(std::string_view name)
{
	// library already installed
	if (findLibrary(name))
		return true;

	return false;
}


ExternalLibraryManifest* ExternalLibraryReposistory::findLibrary(std::string_view name) const
{
	return Find<std::string, ExternalLibraryManifest*>(m_librariesMap, std::string(name), nullptr);
}

//--