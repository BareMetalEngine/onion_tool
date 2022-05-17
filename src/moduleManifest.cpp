#include "common.h"
#include "utils.h"
#include "moduleManifest.h"
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

ModuleManifest* ModuleManifest::Load(const fs::path& manifestPath)
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
	ret->rootPath = manifestPath.parent_path().make_preferred();
	ret->name = XMLNodeAttrbiute(root, "name");
	if (ret->name.empty())
	{
		std::cerr << KRED << "[BREAKING] Module manifest XML at '" << manifestPath << "' is missing the module name\n" << RST;
		return nullptr;
	}

	bool valid = true;

	// TODO: author string
	// TODO: license string

	XMLNodeIterate(root, "Dependency", [&ret, &valid](const XMLNode* node)
		{
			ModuleDepdencencyInfo dep;
			if (ParseDependency(node, dep))
			{
				ret->moduleDependencies.push_back(dep);
			}
			else
			{
				std::cerr << KRED << "[BREAKING] Module manifest XML at '" << ret->rootPath << "' has invalid dependency definition\n" << RST;
				valid = false;
			}
		});

	XMLNodeIterate(root, "Library", [&ret, &valid](const XMLNode* node)
		{
			ModuleDepdencencyInfo dep;
			if (ParseDependency(node, dep))
			{
				ret->libraryDependencies.push_back(dep);
			}
			else
			{
				std::cerr << KRED << "[BREAKING] Module manifest XML at '" << ret->rootPath << "' has invalid library definition\n" << RST;
				valid = false;
			}
		});

	const auto sourceRootDir = ret->rootPath / "code";
	if (fs::is_directory(sourceRootDir))
	{
		ret->sourceRootPath = sourceRootDir;

		XMLNodeIterate(root, "Project", [&ret, &sourceRootDir, &valid](const XMLNode* node)
			{
				const auto relativePath = XMLNodeValue(node);
				if (relativePath.empty())
				{
					std::cerr << KRED << "[BREAKING] Module manifest XML at '" << ret->rootPath << "' has project without a path\n" << RST;
					valid = false;
				}
				else
				{

					const auto projectPath = sourceRootDir / relativePath;
					const auto projectManifestPath = projectPath / PROJECT_MANIFEST_NAME;

					if (fs::is_regular_file(projectManifestPath))
					{
						ModuleProjectInfo project;
						project.name = ProjectMergedNameFromPath(relativePath);
						project.manifestPath = projectManifestPath;
						ret->projects.push_back(project);
					}
					else
					{
						std::cerr << KRED << "[BREAKING] Module manifest XML at '" << ret->rootPath << "' references project '" << relativePath << "' but there's no manifest for it at " << projectManifestPath << "\n" << RST;
						valid = false;
					}
				}
			});
	}

	if (!valid)
	{
		std::cerr << KRED << "[BREAKING] Module manifest XML at '" << manifestPath << "' is invalid\n" << RST;
		return nullptr;
	}

	return ret.release();
}

//--