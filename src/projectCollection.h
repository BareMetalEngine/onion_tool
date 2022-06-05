#pragma once

//--

struct Configuration;
struct ProjectInfo;
struct ExternalLibraryManifest;
struct ModuleManifest;
class ExternalLibraryReposistory;

class ProjectCollection
{
public:
    ProjectCollection();
    ~ProjectCollection();

    //--

    inline const std::vector<fs::path>& rootIncludePaths() const { return m_rootIncludePaths; }
    inline const std::vector<ProjectInfo*>& projects() const { return m_projects; }

    //--

    bool populateFromModules(const std::vector<const ModuleManifest*>& modules, const Configuration& config);
    bool filterProjects(const Configuration& config);
    bool resolveDependencies(const Configuration& config);
    bool resolveLibraries(ExternalLibraryReposistory& libs);
    bool scanContent(uint32_t& outTotalFiles) const;

    bool resolveDependency(const std::string_view name, std::vector<ProjectInfo*>& outProjects, bool soft) const;

    //--

    ProjectInfo* findProject(std::string_view name) const;

    //--

private:
	std::vector<fs::path> m_rootIncludePaths; // set of root include paths (one for each used module)

	std::vector<ProjectInfo*> m_projects; // all discovered projects
	std::unordered_map<std::string, ProjectInfo*> m_projectsMap; // projects by name
};

//--