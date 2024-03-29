#include "common.h"
#include "utils.h"
#include "moduleManifest.h"
#include "moduleConfiguration.h"
#include "xmlUtils.h"

//--

ModuleConfigurationManifest::ModuleConfigurationManifest()
{}

ModuleConfigurationManifest::~ModuleConfigurationManifest()
{}

//--

bool ModuleConfigurationManifest::save(const fs::path& manifestPath)
{
	std::stringstream f;
	writeln(f, "<?xml version=\"1.0\" encoding=\"utf-8\"?>");
	writelnf(f, "<ModuleConfiguration>");

	if (!solutionName.empty())
		writelnf(f, "  <SolutionName>%hs</SolutionName>", solutionName.c_str());

	const auto manifestDirectory = manifestPath.parent_path();

	for (const auto& entry : modules)
	{
		const auto relativePath = fs::relative(entry.path, manifestDirectory).u8string();

		if (entry.hash.empty())
			writelnf(f, "<ModuleEntry path=\"%hs\" local=\"%hs\" />", relativePath.c_str(), entry.local ? "true" : "false");
		else
			writelnf(f, "<ModuleEntry path=\"%hs\" hash=\"%hs\" local=\"%hs\" />", relativePath.c_str(), entry.hash.c_str(), entry.local ? "true" : "false");
	}

	for (const auto& entry : libraries)
	{
		const auto relativePath = fs::relative(entry.path, manifestDirectory).u8string();
		writelnf(f, "<LibraryEntry name=\"%hs\" path=\"%hs\" version=\"%hs\" />", entry.name.c_str(), relativePath.c_str(), entry.version.c_str());
	}

	writeln(f, "</ModuleConfiguration>");

	return SaveFileFromString(manifestPath, f.str(), false, false);
}

static bool ParseConfigurationEntry(const XMLNode* node, ModuleConfigurationEntry& dep, const fs::path& manifestDirectory)
{
	std::string_view path = XMLNodeAttrbiute(node, "path");
	if (path.empty())
		return false;

	dep.path = fs::weakly_canonical(fs::absolute(manifestDirectory / path)).make_preferred();
	if (!fs::is_regular_file(dep.path))
	{
		LogError() << "Module manifest '" << dep.path << "' does not exist, loaded configuration is not valid";
		return false;
	}

	dep.hash = XMLNodeAttrbiute(node, "hash");
	dep.local = XMLNodeAttrbiute(node, "local") == "true";

	return true;
}

static bool ParseLibraryEntry(const XMLNode* node, ModuleLibraryEntry& dep, const fs::path& manifestDirectory)
{
	std::string_view path = XMLNodeAttrbiute(node, "path");
	if (path.empty())
		return false;

	dep.path = fs::weakly_canonical(fs::absolute(manifestDirectory / path)).make_preferred();
	if (!fs::is_regular_file(dep.path))
	{
		LogError() << "Directory '" << dep.path << "' does not exist, loaded configuration is not valid";
		return false;
	}

	dep.name = XMLNodeAttrbiute(node, "name");
	if (dep.name.empty())
		return false;

	dep.version = XMLNodeAttrbiute(node, "version");
	if (dep.version.empty())
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
		LogError() << "Error parsing XML '" << manifestPath << "': " << e.what();
		return nullptr;
	}

	const auto* root = doc.first_node("ModuleConfiguration");
	if (!root)
	{
		LogError() << "File at '" << manifestPath << "' is not a module configuration file";
		return nullptr;
	}

	auto ret = std::make_unique<ModuleConfigurationManifest>();
	ret->rootPath = manifestPath.parent_path().make_preferred();

	const auto manifestDirectory = manifestPath.parent_path();

	// TODO: author string
	// TODO: license string

	bool valid = true;
	XMLNodeIterate(root, [&valid, &ret, &manifestDirectory](const XMLNode* node, std::string_view option)
		{
			if (option == "ModuleEntry")
			{
				ModuleConfigurationEntry dep;
				if (ParseConfigurationEntry(node, dep, manifestDirectory))
					ret->modules.push_back(dep);
				else
					valid = false;
			}
			else if (option == "LibraryEntry")
			{
				ModuleLibraryEntry dep;
				if (ParseLibraryEntry(node, dep, manifestDirectory))
					ret->libraries.push_back(dep);
				else
					valid = false;
			}
			else if (option == "SolutionName")
			{
				ret->solutionName = std::string(XMLNodeValue(node));
			}
			else
			{
				LogError() << "Module manifest XML at '" << ret->rootPath << "' has invalid element '" << option << "'";
			}
		});

	if (!valid)
	{
		LogError() << "Failed to parse configuration XML at " << manifestPath;
		return nullptr;
	}

	return ret.release();
}

//--