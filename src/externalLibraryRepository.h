#pragma once

//--

struct ExternalLibraryManifest;
struct ModuleConfigurationManifest;

class ExternalLibraryReposistory
{
public:
    ExternalLibraryReposistory();
    ~ExternalLibraryReposistory();

	inline const std::vector<ExternalLibraryManifest*>& libraries() const { return m_libraries; }

	bool installConfiguredLibraries(const ModuleConfigurationManifest& config);
	bool installLibrary(std::string_view name, const fs::path& path);

	const ExternalLibraryManifest* findLibrary(std::string_view name) const;

	bool deployFiles(const fs::path& targetPath) const;

private:
	PlatformType m_platform;

	//fs::path m_downloadCachePath; // path where libraries are downloaded
	//fs::path m_unpackCachePath; // path where libraries are unpacked

	std::vector<ExternalLibraryManifest*> m_libraries; // all discovered libraries
	std::unordered_map<std::string, ExternalLibraryManifest*> m_librariesMap; // libraries by name

	//ExternalLibraryManifest* installLibrary(std::string_view name);

	bool determineLibraryRepositoryNameName(std::string_view name, std::string_view& outRepository, std::string_view& outName, std::string_view& outBranch) const;
};

//--