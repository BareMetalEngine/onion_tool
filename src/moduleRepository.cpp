#include "common.h"
#include "utils.h"
#include "configuration.h"
#include "moduleManifest.h"
#include "moduleRepository.h"
#include "moduleConfiguration.h"

//--

ModuleRepository::ModuleRepository()
{}

ModuleRepository::~ModuleRepository()
{
	for (auto* modul : m_modules)
		delete modul;
}

bool ModuleRepository::installConfiguredModule(const fs::path& absoluteModuleFilePath, std::string_view hash, bool local, bool verifyVersions)
{
	// load the manifest
	auto* manifest = ModuleManifest::Load(absoluteModuleFilePath, local ? "" : "External");
	if (!manifest)
	{
		std::cerr << "Failed to load module manifest from " << absoluteModuleFilePath << "\n";
		return false;
	}

	// set the local flag
	manifest->local = local;

	// install the loaded module
	std::cout << "Installed local module at " << absoluteModuleFilePath << " with " << manifest->projects.size() << " project(s)\n";
	m_modules.push_back(manifest);
	return true;
}

bool ModuleRepository::installConfiguredModules(const ModuleConfigurationManifest& config, bool verifyVersions)
{
	bool valid = true;

	for (const auto& entry : config.modules)
	{
		const auto absoluteModuleFilePath = fs::weakly_canonical((config.rootPath / entry.path).make_preferred());
		valid &= installConfiguredModule(absoluteModuleFilePath, entry.hash, entry.local, verifyVersions);
	}

	return valid;
}

//--

#if 0
bool ModuleRepository::installLibraries(ExternalLibraryReposistory& outRepository)
{
	bool valid = true;

	for (const auto* mod : m_modules)
	{
		for (const auto& dep : mod->libraryDependencies)
		{
			if (!dep.localRelativePath.empty())
			{
				const auto libraryPath = (mod->rootPath / dep.localRelativePath).make_preferred();
				valid &= outRepository.installLocalLibraryPack(libraryPath);
			}
			else if (!dep.gitRepoPath.empty())
			{
				valid &= outRepository.installExternalLibraryPack(dep.gitRepoPath, dep.gitVersionHash);
			}
		}
	}

	return valid;
}
#endif

//--