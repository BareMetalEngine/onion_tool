#pragma once

//--

struct ExternalLibraryManifest;

class ExternalLibraryReposistory
{
public:
    ExternalLibraryReposistory(const fs::path& cachePath);
    ~ExternalLibraryReposistory();

	inline const std::vector<ExternalLibraryManifest*>& libraries() const { return m_libraries; }

	bool installLibrary(std::string_view name);

	ExternalLibraryManifest* findLibrary(std::string_view name) const;

private:
	fs::path m_downloadCachePath; // path where libraries are downloaded
	fs::path m_unpackCachePath; // path where libraries are unpacked

	std::vector<ExternalLibraryManifest*> m_libraries; // all discovered libraries
	std::unordered_map<std::string, ExternalLibraryManifest*> m_librariesMap; // libraries by name

	bool installLocalLibraryPack(const fs::path& manifestPath);

	bool determineLibraryRepositoryNameName(std::string_view name, std::string_view& outRepository, std::string_view& outName) const;
};

//--