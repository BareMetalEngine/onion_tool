#include "common.h"
#include "utils.h"
#include "moduleManifest.h"
#include "projectManifest.h"
#include "xml/rapidxml.hpp"
#include "xmlUtils.h"

//--

ModuleManifest::ModuleManifest()
{}

ModuleManifest::~ModuleManifest()
{}

static bool ParseDependency(const XMLNode* node, ModuleDepdencencyInfo& dep)
{
	dep.gitRepoPath = XMLNodeAttrbiute(node, "repo");
	dep.localRelativePath = XMLNodeAttrbiute(node, "path");

	if (dep.gitRepoPath.empty() && dep.localRelativePath.empty())
		return false;

	return true;
}

static bool ParseData(const XMLNode* node, const fs::path& moduleRootPath, ModuleDataInfo& data)
{
	bool valid = true;

	XMLNodeIterate(node, [&valid, &data](const XMLNode* node, std::string_view option)
		{
			// TODO: filter

			if (option == "VirtualPath")
				data.mountPath = XMLNodeValue(node);
			else if (option == "SourcePath")
				data.localSourcePath = XMLNodeValue(node);
			else
			{
				std::cerr << "Unknown module's manifest option '" << option << "' in Data block\n";
				valid = false;
			}
		});

	if (data.localSourcePath.empty())
	{
		std::cerr << KRED << "[BREAKING] There's no root directory specified for data\n" << RST;
		valid = false;
	}
	else
	{
		data.sourcePath = fs::weakly_canonical(fs::absolute((moduleRootPath / data.localSourcePath).make_preferred()));

		if (!fs::is_directory(data.sourcePath))
		{
			std::cerr << KRED << "[BREAKING] Data root directory " << data.sourcePath << " does not exist\n" << RST;
			valid = false;
		}
	}

	if (data.mountPath.empty())
	{
		std::cerr << KRED << "[BREAKING] There's no mount directory specified for data\n" << RST;
		valid = false;
	}
	else if (!BeginsWith(data.mountPath, "/") || !BeginsWith(data.mountPath, "/"))
	{
		std::cerr << KRED << "[BREAKING] Mount path should start and end with /\n" << RST;
		valid = false;
	}

	return valid;
}

static std::string ProjectMergedNameFromPath(const std::string_view path)
{
	std::vector<std::string_view> parts;
	SplitString(path, "/", parts);

	std::string ret;
	for (const auto& part : parts)
	{
		if (!ret.empty())
			ret += "_";
		ret += part;
	}

	return ret;
}

static bool IsValidProjectTag(std::string_view tag)
{
	if (tag == "AutoLibrary" || tag == "Library")
		return true;
	else if (tag == "StaticLibrary")
		return true;
	else if (tag == "SharedLibrary")
		return true;
	else if (tag == "Application")
		return true;
	else if (tag == "TestApplication")
		return true;
	else if (tag == "Disabled")
		return true;
	return false;
}

ModuleManifest* ModuleManifest::Load(const fs::path& manifestPath, std::string_view projectGroup)
{
	std::string txt;
	if (!LoadFileToString(manifestPath, txt))
		return nullptr;

	XMLDoc doc;
	try
	{
		doc.parse<0>((char*)txt.c_str());
	}
	catch (std::exception& e)
	{
		std::cerr << KRED << "[BREAKING] Error parsing XML '" << manifestPath << "': " << e.what() << "\n" << RST;
		return nullptr;
	}

	const auto* root = doc.first_node("Module");
	if (!root)
	{
		std::cerr << KRED << "[BREAKING] Manifest XML at '" << manifestPath << "' is not a module manifest\n" << RST;
		return nullptr;
	}

	auto ret = std::make_unique<ModuleManifest>();
	ret->guid = GuidFromText(manifestPath.u8string());

	bool valid = true;

	// TODO: author string
	// TODO: license string

	std::string localProjectGroup = std::string(projectGroup);

	XMLNodeIterate(root, [&](const XMLNode* node, std::string_view name)
		{
			// TODO: evaluate condition
			
			// Include directive
			if (name == "Include")
			{
				const std::string relativePath = std::string(XMLNodeValue(node));
				if (relativePath.empty())
				{
					std::cerr << KRED << "[BREAKING] Include expects a path to build.xml to include\n" << RST;
					valid = false;
				}
				else
				{
					const fs::path includeManifestPath = fs::weakly_canonical((manifestPath.parent_path() / relativePath).make_preferred());
					std::cout << "Including module manifest at " << includeManifestPath << "\n";

					if (fs::is_regular_file(includeManifestPath))
					{
						if (auto* included = ModuleManifest::Load(includeManifestPath, localProjectGroup))
						{
							ret->moduleData.insert(ret->moduleData.end(), included->moduleData.begin(), included->moduleData.end());
							ret->moduleDependencies.insert(ret->moduleDependencies.end(), included->moduleDependencies.begin(), included->moduleDependencies.end());
							ret->projects.insert(ret->projects.end(), included->projects.begin(), included->projects.end());
							ret->globalIncludePaths.insert(ret->globalIncludePaths.end(), included->globalIncludePaths.begin(), included->globalIncludePaths.end());
						}
						else
						{
							std::cerr << KRED << "[BREAKING] Specified include module " << includeManifestPath << " failed to load\n" << RST;
							valid = false;
						}
					}
					else
					{
						std::cerr << KRED << "[BREAKING] Specified include module " << includeManifestPath << " does not exist\n" << RST;
						valid = false;
					}
				}
			}

			// Dependency on external module
			else if (name == "ModuleDependency")
			{
				ModuleDepdencencyInfo dep;
				if (ParseDependency(node, dep))
				{
					ret->moduleDependencies.push_back(dep);
				}
				else
				{
					std::cerr << KRED << "[BREAKING] Module manifest XML at '" << manifestPath << "' has invalid dependency definition\n" << RST;
					valid = false;
				}
			}

			// Data mapping
			else if (name == "Data")
			{
				ModuleDataInfo data;
				if (ParseData(node, manifestPath.parent_path(), data))
				{
					ret->moduleData.push_back(data);
				}
				else
				{
					std::cerr << KRED << "[BREAKING] Module manifest XML at '" << manifestPath << "' has invalid data definition\n" << RST;
					valid = false;
				}
			}

			// Solution project group
			else if (name == "ProjectGroupName")
			{
				localProjectGroup = std::string(XMLNodeValue(node));
			}

			// Global include directory
			else if (name == "GlobalIncludePath")
			{
				const std::string relativePath = std::string(XMLNodeValue(node));
				if (relativePath.empty())
				{
					std::cerr << KRED << "[BREAKING] GlobalIncludePath expects a valid path, use './' to indicate current directory\n" << RST;
					valid = false;
				}
				else
				{
					const fs::path globalIncludePath = fs::weakly_canonical((manifestPath.parent_path() / relativePath).make_preferred());
					std::cout << "Found global include path " << globalIncludePath << "\n";

					if (fs::is_directory(globalIncludePath))
					{
						PushBackUnique(ret->globalIncludePaths, globalIncludePath);
					}
					else
					{
						std::cerr << KRED << "[BREAKING] Specified global include path " << globalIncludePath << " does not point to a valid directory\n" << RST;
						valid = false;
					}
				}
			}

			// Project template
			else if (IsValidProjectTag(name))
			{
				if (auto project = ProjectManifest::Load(node, manifestPath.parent_path()))
				{
					project->groupName = localProjectGroup;
					ret->projects.push_back(project);
				}
				else
				{
					std::cerr << KRED << "[BREAKING] Module manifest XML at '" << manifestPath << "' has invalid project definition\n" << RST;
					valid = false;
				}
			}

			// Unknown tag
			else
			{
				std::cerr << KRED << "[BREAKING] Module manifest XML at '" << manifestPath << "' has invalid project tag\n" << RST;
				valid = false;
			}
		});

	if (!valid)
	{
		std::cerr << KRED << "[BREAKING] Module manifest XML at '" << manifestPath << "' is invalid\n" << RST;
		return nullptr;
	}

	return ret.release();
}

//--