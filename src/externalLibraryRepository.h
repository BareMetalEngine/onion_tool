#pragma once

//--

struct ExternalLibraryManifest;

class ExternalLibraryReposistory
{
public:
    ExternalLibraryReposistory();
    ~ExternalLibraryReposistory();

	inline const std::vector<ExternalLibraryManifest*>& libraries() const { return m_libraries; }

	bool installLibraryPacks(const std::vector<fs::path>& paths);
    bool installLibraryPack(const fs::path& path);

	bool deployFiles(const fs::path& targetPath) const;

	ExternalLibraryManifest* findLibrary(std::string_view name) const;

private:
	std::vector<ExternalLibraryManifest*> m_libraries; // all discovered libraries
	std::unordered_map<std::string, ExternalLibraryManifest*> m_librariesMap; // libraries by name
};

//--