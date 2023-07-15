#include "common.h"
#include "utils.h"
#include "externalLibrary.h"
#include "xmlUtils.h"

//--

ExternalLibraryManifest::ExternalLibraryManifest()
{}

bool ExternalLibraryManifest::LoadPlatform(const XMLNode* root, ExternalLibraryPlatform* outPlatform, ExternalLibraryManifest* lib)
{
	bool valid = true;
	XMLNodeIterate(root, "File", [&](const XMLNode* node)
		{
			const auto relativePath = XMLNodeValue(node);
			const auto fullPath = (lib->rootPath / relativePath).make_preferred();
			if (fs::is_regular_file(fullPath))
			{
				lib->allFiles.emplace_back(fullPath);
			}
			else
			{
				LogError() << "Missing generic file for '" << lib->name << "' at " << fullPath;
				valid = false;
			}
		});

	XMLNodeIterate(root, "Link", [&](const XMLNode* node)
		{
			const auto relativePath = XMLNodeValue(node);
			const auto fullPath = (lib->rootPath / relativePath).make_preferred();
			if (fs::is_regular_file(fullPath))
			{
				LogInfo() << "Found library file for '" << lib->name << "' at " << fullPath;
				outPlatform->libraryFiles.push_back(fullPath);
				lib->allFiles.emplace_back(fullPath);
			}
			else
			{
				LogError() << "Missing library file for '" << lib->name << "' at " << fullPath;
				valid = false;
			}
		});

	XMLNodeIterate(root, "Deploy", [&](const XMLNode* node)
		{
			const auto relativePath = XMLNodeValue(node);
			const auto fullPath = (lib->rootPath / relativePath).make_preferred();
			if (fs::is_regular_file(fullPath))
			{
				LogInfo() << "Found deploy file for '" << lib->name << "' at " << fullPath;

				ExternalLibraryDeployFile file;
				file.absoluteSourcePath = fullPath;
				file.relativeDeployPath = fs::path(relativePath).filename().u8string();
				outPlatform->deployFiles.emplace_back(file);
				lib->allFiles.emplace_back(fullPath);
			}
			else
			{
				LogError() << "Missing deploy file for '" << lib->name << "' at " << fullPath;
				valid = false;
			}
		});

	XMLNodeIterate(root, "AdditionalSystemLibrary", [&](const XMLNode* node)
		{
			const auto name = XMLNodeValue(node);
			outPlatform->additionalSystemLibraries.push_back(std::string(name));
			LogInfo() << "Additional system library needed for '" << lib->name << "': '" << name << "'";
		});

	XMLNodeIterate(root, "AdditionalSystemPackage", [&](const XMLNode* node)
		{
			const auto name = XMLNodeValue(node);
			outPlatform->additionalSystemPackages.push_back(std::string(name));
			LogInfo() << "Additional system package needed for '" << lib->name << "': '" << name << "'";
		});

	XMLNodeIterate(root, "AdditionalSystemFramework", [&](const XMLNode* node)
		{
			const auto name = XMLNodeValue(node);
			outPlatform->additionalSystemFrameworks.push_back(std::string(name));
			LogInfo() << "Additional system framework needed for '" << lib->name << "': '" << name << "'";
		});

	return valid;
}

std::unique_ptr<ExternalLibraryManifest> ExternalLibraryManifest::Load(const fs::path& manifestPath)
{
	std::string txt;
	if (!LoadFileToString(manifestPath, txt))
	{
		LogError() << "Failed to load library manifest from '" << manifestPath << "'";
		return nullptr;
	}

	XMLDoc doc;
	try
	{
		doc.parse<0>((char*)txt.c_str());
	}
	catch (std::exception& e)
	{
		LogError() << "Error parsing XML '" << manifestPath << "': " << e.what();
		return nullptr;
	}

	const auto* root = doc.first_node("ExternalLibrary");
	if (!root)
	{
		LogError() << "Manifest XML at '" << manifestPath << "' is not a library manifest";
		return nullptr;
	}

	std::ifstream file(manifestPath);
	if (file.fail())
	{
		LogError() << "Unable to open file '" << manifestPath << "'";
		return nullptr;
	}

	auto lib = std::make_unique<ExternalLibraryManifest>();
	lib->name = XMLNodeAttrbiute(root, "name");
	lib->hash = XMLNodeAttrbiute(root, "hash");
	//lib->timestamp = XMLNodeAttrbiute(root, "timestamp");	
	lib->rootPath = manifestPath.parent_path().make_preferred();

	if (lib->name.empty())
	{
		LogError() << "File at '" << manifestPath << "' has no name attribute in the 'ExternalLibrary' tag";
		return nullptr;
	}

	{
		auto includePath = (lib->rootPath / "include").make_preferred();
		if (fs::is_directory(includePath))
		{
			LogInfo() << "Found include directory for '" << lib->name << "' at " << includePath;
			lib->includePath = includePath;
		}
	}

	lib->defaultPlatform.platform = PlatformType::MAX;

	bool valid = true;
	valid &= LoadPlatform(root, &lib->defaultPlatform, lib.get());

	XMLNodeIterate(root, "Platform", [&](const XMLNode* node)
		{
			const auto platformName = XMLNodeAttrbiute(node, "platform");

			PlatformType platformType;
			if (ParsePlatformType(platformName, platformType))
			{
				lib->customPlatforms.emplace_back();
				lib->customPlatforms.back().platform = platformType;

				if (!LoadPlatform(root, &lib->customPlatforms.back(), lib.get()))
				{
					LogError() << "Unable to parse custom platform library definition for platform '" << lib->name << "' at " << manifestPath;
					valid = false;
				}
			}
			else
			{
				LogError() << "Unknown platform type '" << platformName << "' specified in external library at " << manifestPath;
				valid = false;
			}
		});



	if (!valid)
		return nullptr;

	lib->allFiles.emplace_back(manifestPath);

	LogInfo() << "Loaded manifest for library '" << lib->name << "'";
	return lib;
}

//--

bool ExternalLibraryManifest::deployFilesToTarget(PlatformType platformType, ConfigurationType configuration, const fs::path& targetPath) const
{
	bool valid = true;

	for (const auto& file : defaultPlatform.deployFiles)
	{
		const auto finalTargetPath = (targetPath / file.relativeDeployPath).make_preferred();
		valid &= CopyNewerFile(file.absoluteSourcePath, finalTargetPath);
	}

	for (const auto& platform : customPlatforms)
	{
		if (platform.platform == platformType)
		{
			for (const auto& file : platform.deployFiles)
			{
				const auto finalTargetPath = (targetPath / file.relativeDeployPath).make_preferred();
				valid &= CopyNewerFile(file.absoluteSourcePath, finalTargetPath);
			}
		}
	}

	return valid;
}

void ExternalLibraryManifest::collectLibraries(PlatformType platformType, std::vector<fs::path>* outLibraryPaths) const
{
	for (const auto& file : defaultPlatform.libraryFiles)
		PushBackUnique(*outLibraryPaths, file);

	for (const auto& platform : customPlatforms)
	{
		if (platform.platform == platformType)
		{
			for (const auto& file : platform.libraryFiles)
				PushBackUnique(*outLibraryPaths, file);
		}
	}
}

void ExternalLibraryManifest::collectAdditionalSystemPackages(PlatformType platformType, std::unordered_set<std::string>* outPackages) const
{
	for (const auto& file : defaultPlatform.additionalSystemPackages)
		outPackages->insert(file);

	for (const auto& platform : customPlatforms)
	{
		if (platform.platform == platformType)
		{
			for (const auto& file : platform.additionalSystemPackages)
				outPackages->insert(file);
		}
	}
}

void ExternalLibraryManifest::collectAdditionalSystemFrameworks(PlatformType platformType, std::unordered_set<std::string>* outPackages) const
{
	for (const auto& file : defaultPlatform.additionalSystemFrameworks)
		outPackages->insert(file);

	for (const auto& platform : customPlatforms)
	{
		if (platform.platform == platformType)
		{
			for (const auto& file : platform.additionalSystemFrameworks)
				outPackages->insert(file);
		}
	}
}

void ExternalLibraryManifest::collectIncludeDirectories(PlatformType platformType, std::vector<fs::path>* outLibraryPaths) const
{
	if (!includePath.empty())
		outLibraryPaths->push_back(includePath);
}

//--