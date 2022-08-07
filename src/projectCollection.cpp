#include "common.h"
#include "moduleManifest.h"
#include "projectCollection.h"
#include "externalLibrary.h"
#include "configuration.h"
#include "project.h"
#include "projectManifest.h"
#include "utils.h"

//--

ProjectCollection::ProjectCollection()
{}

ProjectCollection::~ProjectCollection()
{
	for (auto* proj : m_projects)
		delete proj;
}

//--

static std::string MakeProjectName(std::string_view rootPath)
{
	return ReplaceAll(rootPath, "\\", "/");
}

bool ProjectCollection::populateFromModules(const std::vector<const ModuleManifest*>& modules, const Configuration& config)
{
	bool valid = true;

	for (const auto* mod : modules)
	{
		if (!mod->projectsRootPath.empty())
			m_rootIncludePaths.push_back(mod->projectsRootPath);

		for (const auto* proj : mod->projects)
		{
			// do not extract test modules that are coming from externally referenced projects
			if (proj->type == ProjectType::TestApplication && !mod->local)
				continue;

			auto* info = new ProjectInfo();
			info->parentModule = mod;
			info->manifest = proj;
			info->rootPath = proj->rootPath;
			info->name = MakeProjectName(proj->localRoot);

			// HACK!
			if (proj->type == ProjectType::AutoLibrary)
			{
				if (config.libs == LibraryType::Shared)
					const_cast<ProjectManifest*>(proj)->type = ProjectType::SharedLibrary;
				else
					const_cast<ProjectManifest*>(proj)->type = ProjectType::StaticLibrary;
			}

			m_projects.push_back(info);
			m_projectsMap[info->name] = info;
		}
	}

	return valid;
}

ProjectInfo* ProjectCollection::findProject(std::string_view name) const
{
	return Find<std::string, ProjectInfo*>(m_projectsMap, std::string(name), nullptr);
}

//--

bool ProjectCollection::scanContent(uint32_t& outTotalFiles) const
{
	std::atomic<bool> valid = true;
	std::atomic<uint32_t> numFiles = 0;

	#pragma omp parallel for
	for (int i = 0; i < m_projects.size(); ++i)
	{
		auto* project = m_projects[i];

		if (!project->scanContent())
			valid = false;

		numFiles += (uint32_t)project->files.size();
	}

	outTotalFiles = numFiles.load();
	return valid;
}

//--

bool ProjectCollection::resolveDependency(const std::string_view name, std::vector<ProjectInfo*>& outProjects, bool soft) const
{
	if (EndsWith(name, "_*"))
	{
		const auto pattern = name.substr(0, name.length() - 1);
		for (auto* proj : m_projects)
		{
			// we are only tracking libs
			if (proj->manifest->type == ProjectType::SharedLibrary || proj->manifest->type == ProjectType::StaticLibrary)
			{
				if (BeginsWith(proj->name, pattern))
					PushBackUnique(outProjects, proj);
			}
		}

		return true;
	}
	else
	{
		auto* proj = findProject(name);
		if (proj)
		{
			if (proj->manifest->type == ProjectType::SharedLibrary || proj->manifest->type == ProjectType::StaticLibrary)
			{
				PushBackUnique(outProjects, proj);
			}
			else
			{
				std::cerr << KRED << "[BREAKING] Project '" << proj->name << "' is not a library and can't be a dependency\n" << RST;
				return false;
			}

			return true;
		}
		else // dependency not resolved
		{
			if (!soft)
			{
				std::cerr << KRED << "[BREAKING] No project named '" << name << "' found in all loaded modules\n" << RST;
				return false;
			}
		}
	}

	return false;
}

bool ProjectCollection::filterProjects(const Configuration& config)
{
	// clear old mapping
	auto oldProjects = std::move(m_projects);
	m_projects.clear();
	m_projectsMap.clear();

	// applications are used in development mode
	for (auto* proj : oldProjects)
	{
		// in the shipment config we don't emit tests and dev only projects
		if (config.flagShipmentBuild)
		{
			if (proj->manifest->optionDevOnly || proj->manifest->type == ProjectType::TestApplication)
				continue;
		}

		// skip disabled projects
		if (proj->manifest->type == ProjectType::Disabled)
			continue;

		// mark executables as "used" so we can include other stuff
		m_projects.push_back(proj);
		m_projectsMap[proj->name] = proj;
	}

	if (oldProjects.size() != m_projects.size())
	{
		const auto numRemoved = oldProjects.size() - m_projects.size();
		std::cout << "Filtered " << numRemoved << " project(s) from the solution due to development flag\n";
	}

	return true;
}

bool ProjectCollection::resolveDependencies(const Configuration& config)
{
	bool valid = true;

	for (auto* proj : m_projects)
		valid &= proj->resolveDependencies(*this);

	return valid;
}

bool ProjectCollection::resolveLibraries(ExternalLibraryReposistory& libs)
{
	bool valid = true;

	for (auto* proj : m_projects)
		valid &= proj->resolveLibraries(libs);

	return valid;
}
