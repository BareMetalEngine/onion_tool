#include "common.h"
#include "utils.h"
#include "moduleManifest.h"
#include "moduleConfiguration.h"
#include "xmlUtils.h"

//--

ModuleConfigurationManifest::ModuleConfigurationManifest()
{}

ModuleConfigurationManifest::~ModuleConfigurationManifest()
{
}

//--

bool ModuleConfigurationManifest::save(const fs::path& manifestPath)
{
	std::stringstream f;
	writeln(f, "<?xml version=\"1.0\" encoding=\"utf-8\"?>");
	writelnf(f, "<ModuleConfiguration>");

	for (const auto& entry : modules)
	{
		if (entry.hash.empty())
			writelnf(f, "<ModuleEntry path=\"%hs\" local=\"%hs\" />", entry.path.c_str(), entry.local ? "true" : "false");
		else
			writelnf(f, "<ModuleEntry path=\"%hs\" hash=\"%hs\" local=\"%hs\" />", entry.path.c_str(), entry.hash.c_str(), entry.local ? "true" : "false");
	}

	writeln(f, "</ModuleConfiguration>");

	return SaveFileFromString(manifestPath, f.str(), false, false);
}

static bool ParseConfigurationEntry(const XMLNode* node, ModuleConfigurationEntry& dep)
{
	dep.path = XMLNodeAttrbiute(node, "path");
	dep.hash = XMLNodeAttrbiute(node, "hash");
	dep.local = XMLNodeAttrbiute(node, "local") == "true";

	if (dep.path.empty())
		return false;

	return true;
}

ModuleConfigurationManifest* ModuleConfigurationManifest::Load(const fs::path& manifestPath)
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

	const auto* root = doc.first_node("ModuleConfiguration");
	if (!root)
	{
		std::cerr << KRED << "[BREAKING] File at '" << manifestPath << "' is not a module configuration file\n" << RST;
		return nullptr;
	}

	auto ret = std::make_unique<ModuleConfigurationManifest>();
	ret->rootPath = manifestPath.parent_path().make_preferred();

	// TODO: author string
	// TODO: license string

	XMLNodeIterate(root, "ModuleEntry", [&ret](const XMLNode* node)
		{
			ModuleConfigurationEntry dep;
			if (ParseConfigurationEntry(node, dep))
			{
				ret->modules.push_back(dep);
			}
			else
			{
				std::cerr << KRED << "[BREAKING] Module manifest XML at '" << ret->rootPath << "' has invalid dependency definition\n" << RST;
			}
		});

	return ret.release();
}

//--