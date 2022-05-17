#include "common.h"
#include "utils.h"
#include "externalLibrary.h"
#include "externalLibraryRepository.h"

//--

ExternalLibraryReposistory::ExternalLibraryReposistory()
{}

ExternalLibraryReposistory::~ExternalLibraryReposistory()
{
	for (auto* lib : m_libraries)
		delete lib;
}

bool ExternalLibraryReposistory::installLibraryPacks(const std::vector<fs::path>& paths)
{
	bool valid = true;

	for (const auto& path : paths)
		valid &= installLibraryPack(path);

	return valid;
}

bool ExternalLibraryReposistory::installLibraryPack(const fs::path& path)
{
	if (fs::is_directory(path))
	{
		std::cout << "Scanning for libraries at " << path << "\n";

		uint32_t numLibraries = 0;
		try
		{
			for (const auto& entry : fs::directory_iterator(path))
			{
				if (entry.is_directory())
				{
					const auto manifestFilePath = entry.path() / "manifest.txt";
					if (fs::is_regular_file(manifestFilePath))
					{
						std::cout << "Found library manifest at '" << manifestFilePath << "'\n";

						if (auto lib = ExternalLibraryManifest::Load(manifestFilePath))
						{
							if (m_librariesMap.find(lib->name) != m_librariesMap.end())
							{
								std::cout << KYEL << "Library '" << lib->name << "' is already defined, skipping second definition\n" << RST;
								continue;
							}

							auto* libPtr = lib.release();
							m_librariesMap[lib->name] = libPtr;
							m_libraries.push_back(libPtr);
							numLibraries += 1;
						}
					}
				}
			}
		}
		catch (fs::filesystem_error& e)
		{
			std::cerr << KRED << "[EXCEPTION] File system Error: " << e.what() << "\n" << RST;
		}

		std::cout << "Discovered " << numLibraries << " libraries(s) at local path\n";
		return true;
	}
	else
	{
		std::cerr << KRED << "[BREAKING] Directory " << path << " is not a valid library pack directory\n" << RST;
		return false;
	}
}

bool ExternalLibraryReposistory::deployFiles(const fs::path& targetPath) const
{
	bool valid = true;

	for (const auto* lib : m_libraries)
	{
		if (lib->used)
		{
			for (const auto& file : lib->deployFiles)
			{
				fs::path targetPath = targetPath / file.relativeDeployPath;
				valid &= CopyNewerFile(file.absoluteSourcePath, targetPath);
			}
		}
	}

	return valid;
}

ExternalLibraryManifest* ExternalLibraryReposistory::findLibrary(std::string_view name) const
{
	return Find<std::string, ExternalLibraryManifest*>(m_librariesMap, std::string(name), nullptr);
}

//--