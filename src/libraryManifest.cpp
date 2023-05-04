#include "common.h"
#include "utils.h"
#include "libraryManifest.h"
#include "xmlUtils.h"

//--

static bool EvalFilters(const XMLNode* node, const LibraryFilters& filters)
{
	const auto txt = XMLNodeAttrbiute(node, "platform");
	if (txt.empty())
		return true;

	std::vector<std::string_view> options;
	SplitString(txt, ",", options);

	for (const auto option : options)
	{
		if (option == "*")
			return true;
		if (option == "windows" && filters.platform == PlatformType::Windows)
			return true;
		else if (option == "linux" && filters.platform == PlatformType::Linux)
			return true;
        else if (option == "darwin" && (filters.platform == PlatformType::Darwin || filters.platform == PlatformType::DarwinArm))
            return true;
        else if (option == "darwin_arm" && filters.platform == PlatformType::DarwinArm)
            return true;
        else if (option == "darwin_x86" && filters.platform == PlatformType::Darwin)
            return true;

		// TODO: rest
	}

	return false;
}

static bool EvalLibrarySourceType(LibraryManifest* manifest, const XMLNode* node)
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

	std::cerr << "Unknown LibrarySourceType option '" << value << "'\n";
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

	std::cerr << "Unknown LibraryArtifactType option '" << value << "'\n";
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
	
	std::cerr << "Unknown LibraryArtifactLocation option '" << value << "'\n";
	return false;
}

static bool EvalLibraryArtifact(LibraryManifest* manifest, const XMLNode* node, const LibraryFilters& filters)
{
	LibraryArtifactInfo info;

	bool valid = true;
	XMLNodeIterate(node, [&valid, &info, &filters](const XMLNode* node, std::string_view option)
		{
			if (!EvalFilters(node, filters))
				return;

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
				std::cerr << "Unknown library's manifest option '" << option << "'\n";
				valid = false;
			}
		});

	if (!valid)
	{
		std::cerr << KRED << "[BREAKING] There were errors parsing artifact definition in a library manifest from '" << manifest->loadPath << "\n" << RST;
		return false;
	}

	manifest->artifacts.push_back(info);
	return true;
}

static bool EvalLibraryDependency(LibraryManifest* manifest, const XMLNode* node, const LibraryFilters& filters)
{
	LibraryDependencyInfo info;
	info.repo = DEFAULT_DEPENDENCIES_REPO;

	bool valid = true;
	XMLNodeIterate(node, [&valid, &info, &filters](const XMLNode* node, std::string_view option)
		{
			if (!EvalFilters(node, filters))
				return;

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
				std::cerr << "Unknown library's manifest option '" << option << "'\n";
				valid = false;
			}
		});

	if (info.name.empty())
	{
		std::cerr << KRED << "[BREAKING] Missing name of the library dependency in a library manifest from '" << manifest->loadPath << "\n" << RST;
		return false;
	}
	
	if (!valid)
	{
		std::cerr << KRED << "[BREAKING] There were errors parsing artifact definition in a library manifest from '" << manifest->loadPath << "\n" << RST;
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

LibraryManifest::LibraryManifest()
{}

std::unique_ptr<LibraryManifest> LibraryManifest::Load(const fs::path& manifestPath, const LibraryFilters& filters)
{
	std::string txt;
	if (!LoadFileToString(manifestPath, txt))
	{
		std::cerr << "[BREAKING] Failed to load library manifest from '" << manifestPath << "'\n";
		return nullptr;
	}

	XMLDoc doc;
	try
	{
		doc.parse<0>((char*)txt.c_str());
	}
	catch (std::exception& e)
	{
		std::cout << "Error parsing XML '" << manifestPath << "': " << e.what() << "\n";
		return nullptr;
	}

	const auto* root = doc.first_node("Library");
	if (!root)
	{
		std::cout << "Manifest XML at '" << manifestPath << "' is not a library manifest\n";
		return nullptr;
	}

	auto ret = std::make_unique<LibraryManifest>();
	ret->loadPath = fs::path(manifestPath).make_preferred();
	ret->loadPlatform = filters.platform;

	ret->name = XMLNodeAttrbiute(root, "name");
	if (ret->name.empty())
	{
		std::cout << "Manifest XML at '" << manifestPath << "' has no library name specified\n";
		return nullptr;
	}

	bool valid = true;
	XMLNodeIterate(root, [&valid, &ret, &filters](const XMLNode* node, std::string_view option)
		{
			if (!EvalFilters(node, filters))
				return;

			if (option == "SourceType")
				valid &= EvalLibrarySourceType(ret.get(), node);
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
				valid &= EvalLibraryArtifact(ret.get(), node, filters);
			else if (option == "Dependency")
				valid &= EvalLibraryDependency(ret.get(), node, filters);
            else if (option == "AdditionalSystemLibrary")
                ret->additionalSystemLibraries.emplace_back(XMLNodeValue(node));
            else if (option == "AdditionalSystemPackage")
                ret->additionalSystemPackages.emplace_back(XMLNodeValue(node));
            else if (option == "AdditionalSystemFramework")
                ret->additionalSystemFrameworks.emplace_back(XMLNodeValue(node));
			else
			{
				std::cerr << "Unknown library's manifest option '" << option << "'\n";
				valid = false;
			}
		});

	if (!valid)
	{
		std::cerr << KRED << "[BREAKING] There were errors parsing project manifest from '" << manifestPath << "\n" << RST;
		return nullptr;
	}

	return ret;
}

//--