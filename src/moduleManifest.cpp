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
				LogError() << "Unknown module's manifest option '" << option << "' in Data block";
				valid = false;
			}
		});

	if (data.localSourcePath.empty())
	{
		LogError() << "There's no root directory specified for data";
		valid = false;
	}
	else
	{
		data.sourcePath = fs::weakly_canonical(fs::absolute((moduleRootPath / data.localSourcePath).make_preferred()));

		if (!fs::is_directory(data.sourcePath))
		{
			LogError() << "Data root directory " << data.sourcePath << " does not exist";
			valid = false;
		}
	}

	if (data.mountPath.empty())
	{
		LogError() << "There's no mount directory specified for data";
		valid = false;
	}
	else if (!BeginsWith(data.mountPath, "/") || !BeginsWith(data.mountPath, "/"))
	{
		LogError() << "Mount path should start and end with /";
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
		LogError() << "Error parsing XML '" << manifestPath << "': " << e.what();
		return nullptr;
	}

	const auto* root = doc.first_node("Module");
	if (!root)
	{
		LogError() << "Manifest XML at '" << manifestPath << "' is not a module manifest";
		return nullptr;
	}

	auto ret = std::make_unique<ModuleManifest>();
	ret->guid = GuidFromText(manifestPath.u8string());

	bool valid = true;
	bool recursiveError = false;

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
					LogError() << "Include expects a path to build.xml to include";
					valid = false;
				}
				else
				{
					const fs::path includeManifestPath = fs::weakly_canonical((manifestPath.parent_path() / relativePath).make_preferred());
					//LogInfo() << "Including module manifest at " << includeManifestPath;

					if (fs::is_regular_file(includeManifestPath))
					{
						if (auto* included = ModuleManifest::Load(includeManifestPath, localProjectGroup))
						{
							ret->moduleData.insert(ret->moduleData.end(), included->moduleData.begin(), included->moduleData.end());
							ret->moduleDependencies.insert(ret->moduleDependencies.end(), included->moduleDependencies.begin(), included->moduleDependencies.end());
							ret->projects.insert(ret->projects.end(), included->projects.begin(), included->projects.end());
							ret->globalIncludePaths.insert(ret->globalIncludePaths.end(), included->globalIncludePaths.begin(), included->globalIncludePaths.end());
							ret->librarySources.insert(ret->librarySources.end(), included->librarySources.begin(), included->librarySources.end());
						}
						else
						{
							//LogError() << "Specified include module " << includeManifestPath << " failed to load";
							recursiveError = true;
							valid = false;
						}
					}
					else
					{
						LogError() << "Specified include module " << includeManifestPath << " does not exist";
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
					LogError() << "Module manifest XML at '" << manifestPath << "' has invalid dependency definition";
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
					LogError() << "Module manifest XML at '" << manifestPath << "' has invalid data definition";
					valid = false;
				}
			}

			// Solution project group
			else if (name == "ProjectGroupName")
			{
				localProjectGroup = std::string(XMLNodeValue(node));
			}

			// Online (AWS) library endpoint
			else if (name == "LibraryOnlineEndpoint")
			{
				ModuleLibrarySource source;
				source.type = "aws";
				source.data = std::string(XMLNodeValue(node));
				ret->librarySources.push_back(source);
			}

			// Offline packed library folder
			else if (name == "LibraryLocalPackedPath")
			{
				const std::string relativePath = std::string(XMLNodeValue(node));
				if (relativePath.empty())
				{
					LogError() << "MainOfflineLibraryPath expects a valid path, use './' to indicate current directory";
					valid = false;
				}
				else
				{
					const fs::path path = fs::weakly_canonical((manifestPath.parent_path() / relativePath).make_preferred());
					if (fs::is_directory(path))
					{
						ModuleLibrarySource source;
						source.type = "packed";
						source.data = path.u8string();
						ret->librarySources.push_back(source);
					}
					else
					{
						LogError() << "Specified local library path " << path << " does not point to a valid directory";
						valid = false;
					}
				}
			}

			// Offline loose library folder
			else if (name == "LibraryLocalPath")
			{
				const std::string relativePath = std::string(XMLNodeValue(node));
				if (relativePath.empty())
				{
					LogError() << "MainOfflineLibraryPath expects a valid path, use './' to indicate current directory";
					valid = false;
				}
				else
				{
					const fs::path path = fs::weakly_canonical((manifestPath.parent_path() / relativePath).make_preferred());
					if (fs::is_directory(path))
					{
						ModuleLibrarySource source;
						source.type = "loose";
						source.data = path.u8string();
						ret->librarySources.push_back(source);
					}
					else
					{
						LogError() << "Specified local library path " << path << " does not point to a valid directory";
						valid = false;
					}
				}
			}
			
			// Global include directory
			else if (name == "GlobalIncludePath")
			{
				const std::string relativePath = std::string(XMLNodeValue(node));
				if (relativePath.empty())
				{
					LogError() << "GlobalIncludePath expects a valid path, use './' to indicate current directory";
					valid = false;
				}
				else
				{
					const fs::path globalIncludePath = fs::weakly_canonical((manifestPath.parent_path() / relativePath).make_preferred());
					LogInfo() << "Found global include path " << globalIncludePath;

					if (fs::is_directory(globalIncludePath))
					{
						PushBackUnique(ret->globalIncludePaths, globalIncludePath);
					}
					else
					{
						LogError() << "Specified global include path " << globalIncludePath << " does not point to a valid directory";
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
					LogError() << "Module manifest XML at '" << manifestPath << "' has invalid project definition";
					valid = false;
				}
			}

			// Unknown tag
			else
			{
				LogError() << "Module manifest XML at '" << manifestPath << "' has invalid tag '" << name << "'";
				valid = false;
			}
		});

	if (!valid)
	{
		if (!recursiveError)
			LogError() << "Module manifest XML at '" << manifestPath << "' is invalid and could not be loaded";
		return nullptr;
	}

	return ret.release();
}

//--