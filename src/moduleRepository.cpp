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

bool ModuleRepository::installConfiguredModule(const fs::path& absoluteModulePath, std::string_view hash, bool local, bool verifyVersions)
{
	// load the manifest
	const auto manifestPath = absoluteModulePath / MODULE_MANIFEST_NAME;
	auto* manifest = ModuleManifest::Load(manifestPath);
	if (!manifest)
	{
		std::cerr << "Failed to load module manifest from " << manifestPath << "\n";
		return false;
	}

	// set the local flag
	manifest->local = local;

	// install the loaded module
	std::cout << "Installed local module at " << manifestPath << " with " << manifest->projects.size() << " project(s)\n";
	m_modules.push_back(manifest);
	return true;
}

bool ModuleRepository::installConfiguredModules(const ModuleConfigurationManifest& config, bool verifyVersions)
{
	bool valid = true;
	for (const auto& entry : config.modules)
	{
		const auto absoluteModulePath = fs::weakly_canonical((config.rootPath / entry.path).make_preferred());
		valid &= installConfiguredModule(absoluteModulePath, entry.hash, entry.local, verifyVersions);
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