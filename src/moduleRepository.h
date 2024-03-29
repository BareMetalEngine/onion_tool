#pragma once

#include "common.h"
#include "utils.h"

//--

struct Configuration;
struct ModuleManifest;
struct ModuleConfigurationManifest;

class ModuleRepository
{
public:
	ModuleRepository(const Configuration& config);
    ~ModuleRepository();

	inline const std::vector<const ModuleManifest*>& modules() const { return (const std::vector<const ModuleManifest*>&) m_modules; }

	bool installConfiguredModule(const fs::path& absoluteModuleFilePath, std::string_view hash, bool local, bool verifyVersions);
	bool installConfiguredModules(const ModuleConfigurationManifest& config, bool verifyVersions);

private:
	std::vector<ModuleManifest*> m_modules; // selected modules for compilation
	const Configuration& m_config;
};

//--