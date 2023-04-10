#include "common.h"
#include "utils.h"
#include "projectManifest.h"
#include "xmlUtils.h"

//--

static bool EvalProjectType(ProjectManifest* manifest, const std::string_view value)
{
    if (value == "AutoLibrary" || value == "Library")
    {
        manifest->type = ProjectType::AutoLibrary;
        return true;
    }
    else if (value == "StaticLibrary")
    {
        manifest->type = ProjectType::StaticLibrary;
        return true;
    }
	else if (value == "SharedLibrary")
	{
		manifest->type = ProjectType::SharedLibrary;
        manifest->optionDetached = true;
        return true;
	}
    else if (value == "Application")
    {
        manifest->type = ProjectType::Application;
        return true;
    }
    else if (value == "TestApplication")
    {
        manifest->type = ProjectType::TestApplication;
        return true;
    }
    else if (value == "Disabled")
    {
        manifest->type = ProjectType::Disabled;
        return true;
    }

    std::cerr << "Unknown ProjectType option '" << value << "'\n";
    return false;
}

static bool EvalSubsystemType(ProjectManifest* manifest, const XMLNode* node)
{
	const auto value = XMLNodeValue(node);

	if (value == "Console")
	{
        manifest->optionSubstem = ProjectAppSubsystem::Console;
		return true;
	}
	else if (value == "Windows")
	{
		manifest->optionSubstem = ProjectAppSubsystem::Windows;
		return true;
	}

	std::cerr << "Unknown ProjectType option '" << value << "'\n";
	return false;
}

static void InsertPreprocessor(std::vector<std::pair<std::string, std::string>>& prep, std::string_view key, std::string_view value)
{
    for (auto& p : prep)
    {
        if (p.first == key)
        {
            p.second = value;
            return;
        }
    }

    prep.push_back(std::pair<std::string, std::string>(key, value));
}

static bool EvalPreprocessor(std::vector<std::pair<std::string, std::string>>& prep, const XMLNode* node)
{
	const auto value = XMLNodeValue(node);

    std::vector<std::string_view> defines;
    SplitString(value, ";", defines);

    for (const auto& def : defines)
    {
        const auto kv = SplitIntoKeyValue(def);
        InsertPreprocessor(prep, kv.first, kv.second);
    }

    return true;
}

//--

#if 0
ProjectManifest* ProjectManifest::Load(const fs::path& manifestPath)
{
    std::string txt;
    if (!LoadFileToString(manifestPath, txt))
    {
        std::cerr << "[BREAKING] Failed to load project manifest from '" << manifestPath << "'\n";
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

    const auto* root = doc.first_node("Project");
    if (!root)
    {
        std::cout << "Manifest XML at '" << manifestPath << "' is not a project manifest\n";
        return nullptr;
    }

    auto ret = Load(root);
    if (!ret)
        return nullptr;

    ret->rootPath = manifestPath.parent_path().make_preferred();
    return ret;
}
#endif

ProjectManifest* ProjectManifest::Load(const void* rootPtr, const fs::path& modulePath)
{
    auto ret = std::make_unique<ProjectManifest>();
    ret->rootPath = modulePath;

	bool valid = true;

    XMLNode* root = (XMLNode*)rootPtr;
    valid &= EvalProjectType(ret.get(), XMLNodeTag(root));

	XMLNodeIterate(root, [&](const XMLNode* node, std::string_view option)
		{
			// TODO: filter

            if (option == "Subsystem")
                valid &= EvalSubsystemType(ret.get(), node);
            else if (option == "AppClass")
                ret->appClassName = XMLNodeValue(node);
			else if (option == "AppNoLog")
				ret->appDisableLogOnStart = XMLNodeValueBool(node, ret->appDisableLogOnStart);
			else if (option == "Name")
				ret->name = XMLNodeValue(node);
            else if (option == "SourceRoot")
                ret->rootPath = fs::weakly_canonical((modulePath / XMLNodeValue(node)).make_preferred());
			else if (option == "AppHeader")
				ret->appHeaderName = XMLNodeValue(node);
			else if (option == "Guid")
				ret->guid = XMLNodeValue(node);
			else if (option == "DeveloperOnly")
				ret->optionDevOnly = XMLNodeValueBool(node, ret->optionDevOnly);
			else if (option == "WarningLevel")
				ret->optionWarningLevel = XMLNodeValueInt(node, ret->optionWarningLevel);
			else if (option == "InitializeStaticDependencies")
				ret->optionUseStaticInit = XMLNodeValueBool(node, ret->optionUseStaticInit);
			else if (option == "UsePrecompiledHeaders")
				ret->optionUsePrecompiledHeaders = XMLNodeValueBool(node, ret->optionUsePrecompiledHeaders);
			else if (option == "UseExceptions")
				ret->optionUseExceptions = XMLNodeValueBool(node, ret->optionUseExceptions);
			else if (option == "GenerateMain")
				ret->optionGenerateMain = XMLNodeValueBool(node, ret->optionGenerateMain);
			else if (option == "GenerateSymbols")
				ret->optionGenerateSymbols = XMLNodeValueBool(node, ret->optionGenerateSymbols);
			else if (option == "SelfTest")
				ret->optionSelfTest = XMLNodeValueBool(node, ret->optionGenerateSymbols);
			else if (option == "Dependency")
				ret->dependencies.push_back(std::string(node->value()));
			else if (option == "OptionalDependency")
				ret->optionalDependencies.push_back(std::string(node->value()));
			else if (option == "LibraryDependency")
				ret->libraryDependencies.push_back(std::string(node->value()));
			else if (option == "PreprocessorDefines")
				valid &= EvalPreprocessor(ret->localDefines, node);
			else if (option == "GlobalPreprocessorDefines")
				valid &= EvalPreprocessor(ret->globalDefines, node);
			else
			{
				std::cerr << "Unknown project's manifest option '" << option << "'\n";
				valid = false;
			}
		});

	if (ret->name.empty())
	{
		std::cerr << KRED << "[BREAKING] Project without a name at " << modulePath << "\n" << RST;
		return nullptr;
	}

    if (ret->rootPath.empty())
	{
        std::cerr << KRED << "[BREAKING] There's no root directory specified for a project '" << ret->name << "' at " << modulePath << "\n" << RST;
		return nullptr;
	}
	if (!fs::is_directory(ret->rootPath))
	{
		std::cerr << KRED << "[BREAKING] Directory " << ret->rootPath << " is not a valid directory for project '" << ret->name << " at " << modulePath << "\n" << RST;
		return nullptr;
	}

	if (ret->guid.empty())
		ret->guid = GuidFromText(ret->rootPath.u8string().c_str());

	if (!valid)
	{
		std::cerr << KRED << "[BREAKING] There were errors parsing project '" << ret->name << "' at " << modulePath << "\n" << RST;
		return nullptr;
	}

	return ret.release();
}

//--