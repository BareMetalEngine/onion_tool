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
	dep.name = XMLNodeAttrbiute(node, "name");
	dep.gitRepoPath = XMLNodeAttrbiute(node, "repo");
	dep.localRelativePath = XMLNodeAttrbiute(node, "path");

	if (dep.name.empty())
		return false;

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
	ret->projectsRootPath = (ret->rootPath / "code").make_preferred();
	ret->guid = XMLNodeAttrbiute(root, "guid");

	// TODO: validate guid format
	if (ret->guid.empty())
	{
		std::cerr << KRED << "[BREAKING] Module manifest XML at '" << ret->rootPath << "' has no guid specified\n" << RST;
		return nullptr;
	}

	bool valid = true;

	// TODO: author string
	// TODO: license string

	XMLNodeIterate(root, "ModuleDependency", [&ret, &valid](const XMLNode* node)
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

	XMLNodeIterate(root, [&ret, &valid](const XMLNode* node, std::string_view name)
		{
			if (name == "ModuleDependency")
				return;

			if (!IsValidProjectTag(name))
			{
				std::cerr << KRED << "[BREAKING] Module manifest XML at '" << ret->rootPath << "' has invalid project tag\n" << RST;
				valid = false;
				return;
			}

			if (auto project = ProjectManifest::Load(node, ret->projectsRootPath))
			{
				ret->projects.push_back(project);
			}
			else
			{
				std::cerr << KRED << "[BREAKING] Module manifest XML at '" << ret->rootPath << "' has invalid project definition\n" << RST;
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