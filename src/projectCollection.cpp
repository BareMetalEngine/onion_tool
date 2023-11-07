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
		if (m_solutionName.empty() && !mod->globalSolutionName.empty())
			m_solutionName = mod->globalSolutionName;

		for (const auto& path : mod->globalIncludePaths)
			PushBackUnique(m_rootIncludePaths, path);

		for (const auto* proj : mod->projects)
		{
			// do not extract test modules that are coming from externally referenced projects
			if (proj->type == ProjectType::TestApplication && !mod->local)
				continue;

			auto* info = new ProjectInfo();
			info->parentModule = mod;
			info->manifest = proj;
			info->rootPath = proj->rootPath;
			info->name = proj->name;
			info->globalNamespace = mod->globalNamespace;

			// HACK!
			if (proj->type == ProjectType::AutoLibrary)
			{
				bool makeShared = (config.linking == LinkingType::Shared);

				// HACK! Third party libraries without the proper macro definition can't compile as shared libs
				if (proj->optionThirdParty && proj->thirdPartySharedLocalBuildDefine.empty())
					makeShared = false;

				if (makeShared)
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

bool ProjectCollection::resolveDependency(const std::string_view name, std::vector<ProjectInfo*>& outProjects, bool soft, std::vector<std::string>* outMissingDependencies) const
{
	if (EndsWith(name, "*"))
	{
		const auto pattern = name.substr(0, name.length() - 1);
		for (auto* proj : m_projects)
		{
			// we are only tracking libs
			if (proj->manifest->type == ProjectType::SharedLibrary || proj->manifest->type == ProjectType::StaticLibrary)
			{
				if (BeginsWith(proj->name, pattern))
				{
					const auto remainingName = proj->name.substr(pattern.length());
					if (remainingName.find('/') == std::string_view::npos)
					{
						PushBackUnique(outProjects, proj);
					}
				}
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
			else if (proj->manifest->type == ProjectType::HeaderLibrary)
			{
				// allowed
				PushBackUnique(outProjects, proj);
			}
			else
			{
				LogError() << "Project '" << proj->name << "' is not a library and can't be a dependency";

				if (outMissingDependencies)
					PushBackUnique(*outMissingDependencies, std::string(name));

				return false;
			}

			return true;
		}
		else // dependency not resolved
		{
			if (!soft)
			{
				if (outMissingDependencies)
					PushBackUnique(*outMissingDependencies, std::string(name));

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
		if (!config.flagDevBuild)
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
		LogInfo() << "Filtered " << numRemoved << " project(s) from the solution due to development flag";
	}

	return true;
}

bool ProjectCollection::resolveDependencies(const Configuration& config)
{
	bool valid = true;

	std::vector<std::string> missingProjectDependencies;
	for (auto* proj : m_projects)
		valid &= proj->resolveDependencies(*this, &missingProjectDependencies);

	if (!missingProjectDependencies.empty())
	{
		valid = false;

		LogError() << "Found " << missingProjectDependencies.size() << " missing project dependencies!";

		for (const auto& name : missingProjectDependencies)
		{
			std::stringstream str;
			str << "Missing project '" << name << "'";

			bool first = true;

			for (auto* proj : m_projects)
			{
				bool hasDependency = false;
				for (const auto& info : proj->manifest->dependencies)
				{
					if (info == name)
					{
						hasDependency = true;
						break;
					}
				}

				for (const auto& info : proj->manifest->optionalDependencies)
				{
					if (info == name)
					{
						hasDependency = true;
						break;
					}
				}

				if (hasDependency)
				{
					if (first)
						str << " referenced in: ";
					else
						str << ", ";

					str << "'" << proj->name << "'";
					first = false;
				}
			}

			LogError() << str.str();
		}
	}

	return valid;
}

bool ProjectCollection::resolveLibraries(ExternalLibraryReposistory& libs)
{
	bool valid = true;

	for (auto* proj : m_projects)
		valid &= proj->resolveLibraries(libs);

	return valid;
}
