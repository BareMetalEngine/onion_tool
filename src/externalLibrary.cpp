#include "common.h"
#include "utils.h"
#include "externalLibrary.h"
#include "xmlUtils.h"

//--

ExternalLibraryManifest::ExternalLibraryManifest()
{}

std::unique_ptr<ExternalLibraryManifest> ExternalLibraryManifest::Load(const fs::path& manifestPath)
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

	const auto* root = doc.first_node("ExternalLibrary");
	if (!root)
	{
		std::cout << "Manifest XML at '" << manifestPath << "' is not a library manifest\n";
		return nullptr;
	}

	std::ifstream file(manifestPath);
	if (file.fail())
	{
		std::cout << "Unable to open file '" << manifestPath << "'\n";
		return nullptr;
	}

	auto lib = std::make_unique<ExternalLibraryManifest>();
	lib->name = XMLNodeAttrbiute(root, "name");
	lib->hash = XMLNodeAttrbiute(root, "hash");
	lib->timestamp = XMLNodeAttrbiute(root, "timestamp");	
	lib->rootPath = manifestPath.parent_path().make_preferred();

	if (lib->name.empty() || lib->timestamp.empty())
	{
		std::cout << "File at '" << manifestPath << "' has errors in library definition\n";
		return nullptr;
	}

	{
		auto includePath = (lib->rootPath / "include").make_preferred();
		if (fs::is_directory(includePath))
		{
			std::cout << "Found include directory for '" << lib->name << "' at \"" << includePath << "\"\n";
			lib->includePath = includePath;
		}
	}

	bool valid = true;
	XMLNodeIterate(root, "Link", [&valid, &lib](const XMLNode* node)
		{
			const auto relativePath = XMLNodeValue(node);
			const auto fullPath = (lib->rootPath / relativePath).make_preferred();
			if (fs::is_regular_file(fullPath))
			{
				std::cout << "Found library file for '" << lib->name << "' at \"" << fullPath << "\"\n";
				lib->libraryFiles.push_back(fullPath);
				lib->allFiles.emplace_back(fullPath);
			}
			else
			{
				std::cout << "Missing library file for '" << lib->name << "' at \"" << fullPath << "\"\n";
				valid = false;
			}
		});

	XMLNodeIterate(root, "Deploy", [&valid, &lib](const XMLNode* node)
		{
			const auto relativePath = XMLNodeValue(node);
			const auto fullPath = (lib->rootPath / relativePath).make_preferred();
			if (fs::is_regular_file(fullPath))
			{
				std::cout << "Found deploy file for '" << lib->name << "' at \"" << fullPath << "\"\n";

				ExternalLibraryDeployFile file;
				file.absoluteSourcePath = fullPath;
				file.relativeDeployPath = relativePath;
				lib->deployFiles.emplace_back(file);
				lib->allFiles.emplace_back(fullPath);
			}
			else
			{
				std::cout << "Missing deploy file for '" << lib->name << "' at \"" << fullPath << "\"\n";
				valid = false;
			}
		});

	XMLNodeIterate(root, "File", [&valid, &lib](const XMLNode* node)
		{
			const auto relativePath = XMLNodeValue(node);
			const auto fullPath = (lib->rootPath / relativePath).make_preferred();
			if (fs::is_regular_file(fullPath))
			{
				lib->allFiles.emplace_back(fullPath);
			}
			else
			{
				std::cout << "Missing generic file for '" << lib->name << "' at \"" << fullPath << "\"\n";
				valid = false;
			}
		});

	if (!valid)
		return nullptr;

	lib->allFiles.emplace_back(manifestPath);

	std::cout << "Loaded manifest for library '" << lib->name << "'\n";
	return lib;
}

//--