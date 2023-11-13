#include "common.h"
#include "utils.h"
#include "moduleManifest.h"
#include "projectManifest.h"
#include "xml/rapidxml.hpp"
#include "xmlUtils.h"
#include "configuration.h"

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
	else if (tag == "HeaderLibrary")
		return true;
	else if (tag == "FrozenLibrary")
		return true;
	else if (tag == "DetachedSharedLibrary")
		return true;
	else if (tag == "Application")
		return true;
	else if (tag == "ThirdPartyLibrary")
		return true;
	else if (tag == "TestApplication")
		return true;
	else if (tag == "Disabled")
		return true;
	return false;
}

extern bool EvalPlatformFilters(const XMLNode* node, PlatformType platform);
extern bool EvalSolutionFilters(const XMLNode* node, SolutionType solutionType);

bool ModuleManifest::LoadKeySet(ModuleManifest* ret, const void* nodePtr, const Configuration& config, bool topLevel)
{
	XMLNode* root = (XMLNode*)nodePtr;

	bool valid = true;
	bool recursiveError = false;

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
					const fs::path includeManifestPath = fs::weakly_canonical((ret->path.parent_path() / relativePath).make_preferred());
					//LogInfo() << "Including module manifest at " << includeManifestPath;

					if (fs::is_regular_file(includeManifestPath))
					{
						if (auto* included = ModuleManifest::Load(includeManifestPath, ret->localProjectGroup, config, false))
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

			// Filter by platform
			else if (name == "FilterPlatform")
			{
				if (EvalPlatformFilters(node, config.platform))
				{
					valid &= LoadKeySet(ret, node, config, topLevel);
				}
			}

			// Filter by lib type
			else if (name == "FilterSolutionType")
			{
				if (EvalSolutionFilters(node, config.solutionType))
				{
					valid &= LoadKeySet(ret, node, config, topLevel);
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
					LogError() << "Module manifest XML at '" << ret->path << "' has invalid dependency definition";
					valid = false;
				}
			}

			// Data mapping
			else if (name == "Data")
			{
				ModuleDataInfo data;
				if (ParseData(node, ret->path.parent_path(), data))
				{
					ret->moduleData.push_back(data);
				}
				else
				{
					LogError() << "Module manifest XML at '" << ret->path << "' has invalid data definition";
					valid = false;
				}
			}

			// Solution project group
			else if (name == "ProjectGroupName")
			{
				ret->localProjectGroup = std::string(XMLNodeValue(node));
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
					const fs::path path = fs::weakly_canonical((ret->path.parent_path() / relativePath).make_preferred());
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
					const fs::path path = fs::weakly_canonical((ret->path.parent_path() / relativePath).make_preferred());
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
					const fs::path globalIncludePath = fs::weakly_canonical((ret->path.parent_path() / relativePath).make_preferred());
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

			// Namespace for all of the
			else if (name == "GlobalNamespace")
			{
				ret->globalNamespace = std::string(XMLNodeValue(node));;
			}

			// Project namespace
			else if (name == "GlobalSolutionName")
			{
				ret->globalSolutionName = std::string(XMLNodeValue(node));;
			}

			// Project template
			else if (IsValidProjectTag(name))
			{
				if (auto project = ProjectManifest::Load(node, ret->path.parent_path(), config))
				{
					project->solutionGroupName = ret->localProjectGroup;
					ret->projects.push_back(project);
				}
				else
				{
					LogError() << "Module manifest XML at '" << ret->path << "' has invalid project definition";
					valid = false;
				}
			}

			// Unknown tag
			else
			{
				LogError() << "Module manifest XML at '" << ret->path << "' has invalid tag '" << name << "'";
				valid = false;
			}
		});

	if (!valid)
	{
		if (!recursiveError)
			LogError() << "Module manifest XML at '" << ret->path << "' is invalid and could not be loaded";
		return false;
	}

	return valid;
}

ModuleManifest* ModuleManifest::Load(const fs::path& manifestPath, std::string_view projectGroup, const Configuration& config, bool topLevel)
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
	ret->path = manifestPath;
	ret->guid = GuidFromText(manifestPath.u8string());
	ret->localProjectGroup = projectGroup;

	if (!LoadKeySet(ret.get(), root, config, topLevel))
		return nullptr;


	if (topLevel)
	{
		if (ret->globalNamespace.empty())
			LogError() << "Module manifest XML at '" << manifestPath << "' should specify the global namespace via <GlobalNamespace>name</GlobalNamespace>";

		if (ret->globalSolutionName.empty())
			LogError() << "Module manifest XML at '" << manifestPath << "' should specify the solution name via <GlobalSolutionName>name</GlobalSolutionName>";
	}

	return ret.release();
}

//--