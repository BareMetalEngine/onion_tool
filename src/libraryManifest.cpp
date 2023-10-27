#include "common.h"
#include "utils.h"
#include "libraryManifest.h"
#include "xmlUtils.h"

//--

static bool EvalFilters(const XMLNode* node, const LibraryFilters& filters)
{
	{
		const auto txt = XMLNodeAttrbiute(node, "include");
		if (!txt.empty())
		{
			std::vector<std::string_view> options;
			SplitString(txt, ",", options);

			for (const auto& txt : options)
				if (MatchesPlatform(filters.platform, txt))
					return true;

			return false;
		}
	}

	{
		const auto txt = XMLNodeAttrbiute(node, "exclude");
		if (!txt.empty())
		{
			std::vector<std::string_view> options;
			SplitString(txt, ",", options);

			for (const auto& txt : options)
				if (MatchesPlatform(filters.platform, txt))
					return false;

			return true;
		}
	}

	return true;
}

static bool EvalLibrarySourceType(LibraryManifestConfig* manifest, const XMLNode* node)
{
	const auto value = XMLNodeValue(node);

	if (value == "GitHub")
	{
		manifest->sourceType = LibrarySourceType::GitHub;
		return true;
	}
	else if (value == "URL")
	{
		manifest->sourceType = LibrarySourceType::FileOnTheInternet;
		return true;
	}	

	LogError() << "Unknown LibrarySourceType option '" << value << "'";
	return false;
}

static bool EvalLibraryArtifactType(LibraryArtifactInfo& info, const XMLNode* node)
{
	const auto value = XMLNodeValue(node);

	if (value == "Header")
	{
		info.type = LibraryArtifactType::Header;
		return true;
	}
	else if (value == "Library")
	{
		info.type = LibraryArtifactType::Library;
		return true;
	}
	else if (value == "Deploy")
	{
		info.type = LibraryArtifactType::Deploy;
		return true;
	}

	LogError() << "Unknown LibraryArtifactType option '" << value << "'";
	return false;
}

static bool EvalLibraryArtifactLocation(LibraryArtifactInfo& info, const XMLNode* node)
{
	const auto value = XMLNodeValue(node);

	if (value == "Build")
	{
		info.location = LibraryArtifactLocation::Build;
		return true;
	}
	else if (value == "Source")
	{
		info.location = LibraryArtifactLocation::Source;
		return true;
	}
	
	LogError() << "Unknown LibraryArtifactLocation option '" << value << "'";
	return false;
}

static bool EvalLibraryArtifact(const std::string& loadPath, LibraryManifestConfig* manifest, const XMLNode* node)
{
	LibraryArtifactInfo info;

	bool valid = true;
	XMLNodeIterate(node, [&valid, &info](const XMLNode* node, std::string_view option)
		{
			if (option == "Type")
				valid &= EvalLibraryArtifactType(info, node);
			else if (option == "Location")
				valid &= EvalLibraryArtifactLocation(info, node);
			else if (option == "File")
				info.files.emplace_back(XMLNodeValue(node));
			else if (option == "Destination")
				info.deployPath = XMLNodeValue(node);			
			else if (option == "Recursive")
				info.recursive = XMLNodeValueBool(node);
			else
			{
				LogError() << "Unknown library's manifest option '" << option << "'";
				valid = false;
			}
		});

	if (!valid)
	{
		LogError() << "There were errors parsing artifact definition in a library manifest at '" << loadPath << "'";
		return false;
	}

	manifest->artifacts.push_back(info);
	return true;
}

static bool EvalLibraryDependency(const std::string& loadPath, LibraryManifestConfig* manifest, const XMLNode* node)
{
	LibraryDependencyInfo info;

	bool valid = true;
	XMLNodeIterate(node, [&valid, &info](const XMLNode* node, std::string_view option)
		{
			if (option == "Library")
				info.name = XMLNodeValue(node);
			else if (option == "Repository")
				info.repo = XMLNodeValue(node);
			else if (option == "IncludeVar")
				info.includeVar = XMLNodeValue(node);
			else if (option == "LibraryVar")
			{
				LibraryDependencyVar var;
				var.varName = XMLNodeValue(node);
				var.fileName = XMLNodeAttrbiute(node, "file");
				info.libraryVars.push_back(var);
			}
			else
			{
				LogError() << "Unknown library's manifest option '" << option << "'";
				valid = false;
			}
		});

	if (info.name.empty())
	{
		LogError() << "Missing name of the library dependency in a library manifest from '" << loadPath;
		return false;
	}

	if (info.repo.empty())
	{
		LogError() << "Missing repository of the library dependency in a library manifest from '" << loadPath;
		return false;
	}
	
	if (!valid)
	{
		LogError() << "There were errors parsing artifact definition in a library manifest from '" << loadPath;
		return false;
	}

	manifest->dependencies.push_back(info);
	return true;
}

//--

LibraryFilters::LibraryFilters()
{
	platform = DefaultPlatform();
}

//--

bool LibraryManifestConfig::LoadOption(const std::string& loadPath, const void* nodePtr, std::string_view option, LibraryManifestConfig* ret)
{
	const XMLNode* node = (const XMLNode*)nodePtr;

	if (option == "SourceType")
		return EvalLibrarySourceType(ret, node);
	else if (option == "SourceURL")
		ret->sourceURL = XMLNodeValue(node);
	else if (option == "SourceBranch")
		ret->sourceBranch = XMLNodeValue(node);
	else if (option == "SourceRelativePath")
		ret->sourceRelativePath = XMLNodeValue(node);
	else if (option == "ConfigCommand")
		ret->configCommand = XMLNodeValue(node);
	else if (option == "BuildCommand")
		ret->buildCommand = XMLNodeValue(node);
	else if (option == "SourceBuild")
		ret->sourceBuild = XMLNodeValueBool(node);
	else if (option == "Artifact")
		return EvalLibraryArtifact(loadPath, ret, node);
	else if (option == "Dependency")
		return EvalLibraryDependency(loadPath, ret, node);
	else if (option == "AdditionalSystemLibrary")
		ret->additionalSystemLibraries.emplace_back(XMLNodeValue(node));
	else if (option == "AdditionalSystemPackage")
		ret->additionalSystemPackages.emplace_back(XMLNodeValue(node));
	else if (option == "AdditionalSystemFramework")
		ret->additionalSystemFrameworks.emplace_back(XMLNodeValue(node));
	else
		return false;

	return true;
}

bool LibraryManifestConfig::LoadCollection(const std::string& loadPath, const void* nodePtr, LibraryManifestConfig* ret)
{
	const XMLNode* node = (const XMLNode*)nodePtr;

	bool valid = true;
	XMLNodeIterate(node, [&loadPath, &valid, &ret](const XMLNode* node, std::string_view option)
		{
			valid &= LoadOption(loadPath, node, option, ret);
		});

	return valid;
}

//--

LibraryManifest::LibraryManifest()
{}

std::unique_ptr<LibraryManifest> LibraryManifest::Load(const fs::path& manifestPath, const LibraryFilters& filters)
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
		LogInfo() << "Error parsing XML '" << manifestPath << "': " << e.what();
		return nullptr;
	}

	const auto* root = doc.first_node("Library");
	if (!root)
	{
		LogInfo() << "Manifest XML at '" << manifestPath << "' is not a library manifest";
		return nullptr;
	}

	auto ret = std::make_unique<LibraryManifest>();
	ret->loadPath = fs::path(manifestPath).make_preferred();
	ret->loadPlatform = filters.platform;

	ret->name = XMLNodeAttrbiute(root, "name");
	if (ret->name.empty())
	{
		LogInfo() << "Manifest XML at '" << manifestPath << "' has no library name specified";
		return nullptr;
	}

	bool valid = true;
	XMLNodeIterate(root, [&valid, &ret, &filters](const XMLNode* node, std::string_view option)
		{
			if (option == "Platform")
			{
				if (EvalFilters(node, filters))
				{
					valid &= LibraryManifestConfig::LoadCollection(ret->loadPath.u8string(), node, &ret->config);
				}
			}
			else
			{
				valid &= LibraryManifestConfig::LoadOption(ret->loadPath.u8string(), node, option, &ret->config);
			}
		});

	if (!valid)
	{
		LogError() << "There were errors parsing project manifest from '" << manifestPath;
		return nullptr;
	}

	return ret;
}

//--