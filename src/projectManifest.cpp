#include "common.h"
#include "utils.h"
#include "projectManifest.h"
#include "xmlUtils.h"
#include "configuration.h"

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
        return true;
	}
	else if (value == "DetachedSharedLibrary")
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
	else if (value == "HeaderLibrary")
	{
		manifest->type = ProjectType::HeaderLibrary;
		return true;
	}
    else if (value == "Disabled")
    {
        manifest->type = ProjectType::Disabled;
        return true;
    }

	LogError() << "Unknown ProjectType option '" << value << "'";
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

	LogError() << "Unknown ProjectType option '" << value << "'";
	return false;
}

static bool EvalTestFramework(ProjectManifest* manifest, const XMLNode* node)
{
	const auto value = XMLNodeValue(node);

	if (value == "GTest")
	{
		manifest->optionTestFramework = ProjectTestFramework::GTest;
		return true;
	}
	else if (value == "Catch2")
	{
		manifest->optionTestFramework = ProjectTestFramework::Catch2;
		return true;
	}

	LogError() << "Unknown TestFramework option '" << value << "'";
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
        LogError() << "Failed to load project manifest from '" << manifestPath << "'";
        return nullptr;
    }

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

    const auto* root = doc.first_node("Project");
    if (!root)
    {
		LogError() << "Manifest XML at '" << manifestPath << "' is not a project manifest";
        return nullptr;
    }

    auto ret = Load(root);
    if (!ret)
        return nullptr;

    ret->rootPath = manifestPath.parent_path().make_preferred();
    return ret;
}
#endif

static bool EvalThirdPartySourceFile(std::string_view text, const fs::path& moduleDirectory, std::vector<fs::path>* outFilePaths)
{
	std::vector<std::string_view> lines;
	SplitString(text, "\n", lines);

	for (const auto line : lines)
	{
		std::vector<std::string_view> fileNames;
		SplitString(line, ";", fileNames);

		for (auto name : fileNames)
		{
			name = Trim(name);

			if (name.empty())
				continue;

			if (BeginsWith(name, "#"))
				continue; // ignored

			const auto filePath = (moduleDirectory / name).make_preferred();

			if (!fs::is_regular_file(filePath))
			{
				LogError() << "File " << filePath << " does not exist";
				return false;
			}

			outFilePaths->push_back(filePath);
		}
	}

	return true;
}

static bool EvalAdvancedInstructionSet(std::string& ret, const XMLNode* node)
{
	const auto value = XMLNodeValue(node);
	if (value == "AVX" || value == "AVX2" || value == "AVX512")
	{
		ret = value;
		return true;
	}
	
	return false;
}

bool ProjectManifest::LoadKey(const void* nodePtr, const fs::path& modulePath, ProjectManifest* ret)
{
	bool valid = true;

	XMLNode* node = (XMLNode*)nodePtr;
	std::string_view option = XMLNodeTag(node);

	if (ret->type == ProjectType::TestApplication)
		ret->optionGenerateMain = true;

	if (option == "Subsystem")
		valid &= EvalSubsystemType(ret, node);
	else if (option == "TestFramework")
		valid &= EvalTestFramework(ret, node);
	else if (option == "AppClass")
		ret->appClassName = XMLNodeValue(node);
	else if (option == "AppNoLog")
		ret->appDisableLogOnStart = XMLNodeValueBool(node, ret->appDisableLogOnStart);
	else if (option == "Name")
		ret->name = XMLNodeValue(node);
	else if (option == "SourceRoot")
		ret->rootPath = fs::weakly_canonical((modulePath / XMLNodeValue(node)).make_preferred());
	else if (option == "Legacy")
		ret->optionLegacy = XMLNodeValueBool(node, ret->optionLegacy);
	else if (option == "ThirdParty")
		ret->optionThirdParty = XMLNodeValueBool(node, ret->optionThirdParty);
	else if (option == "LegacySourceDirectory")
		ret->legacySourceDirectories.push_back(std::string(node->value()));
	else if (option == "AppHeader")
		ret->appHeaderName = XMLNodeValue(node);
	else if (option == "Guid")
		ret->guid = XMLNodeValue(node);
	else if (option == "DeveloperOnly")
		ret->optionDevOnly = XMLNodeValueBool(node, ret->optionDevOnly);
	else if (option == "EngineOnly")
		ret->optionEngineOnly = XMLNodeValueBool(node, ret->optionEngineOnly);
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
	else if (option == "HasPreMain")
		ret->optionHasPreMain = XMLNodeValueBool(node, ret->optionHasPreMain);
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
	else if (option == "HasInit")
		ret->optionHasInit = XMLNodeValueBool(node, ret->optionHasInit);
	else if (option == "HasPreInit")
		ret->optionHasPreInit = XMLNodeValueBool(node, ret->optionHasPreInit);
	else if (option == "ThirdPartySourceFile")
		return EvalThirdPartySourceFile(XMLNodeValue(node), ret->rootPath, &ret->thirdPartySourceFiles);
	else if (option == "ThirdPartySharedLocalBuildDefine")
		ret->thirdPartySharedLocalBuildDefine = XMLNodeValue(node);
	else if (option == "ThirdPartySharedGlobalExportDefine")
		ret->thirdPartySharedGlobalExportDefine = XMLNodeValue(node);
	else if (option == "Frozen")
		ret->optionFrozen = XMLNodeValueBool(node, ret->optionFrozen);
	else if (option == "FrozenDeployFiles")
		return EvalThirdPartySourceFile(XMLNodeValue(node), ret->rootPath, &ret->frozenDeployFiles);
	else if (option == "FrozenLibraryFiles")
		return EvalThirdPartySourceFile(XMLNodeValue(node), ret->rootPath, &ret->frozenLibraryFiles);
	else if (option == "LocalIncludeDirectory")
		ret->_temp_localIncludePaths.push_back(std::string(node->value()));
	else if (option == "ExportedIncludeDirectory")
		ret->_temp_exportedIncludePaths.push_back(std::string(node->value()));
	else if (option == "GroupName")
		ret->localGroupName = XMLNodeValue(node);
	else if (option == "AdvancedInstructionSet")
		valid &= EvalAdvancedInstructionSet(ret->optionAdvancedInstructionSet, node);
	else
	{
		LogError() << "Unknown project's manifest option '" << option << "'";
		valid = false;
	}

	return valid;
}

bool EvalPlatformFilters(const XMLNode* node, PlatformType platform)
{
	{
		const auto txt = XMLNodeAttrbiute(node, "include");
		LogInfo() << "Include filter '" << txt << "', platform: " << NameEnumOption(platform);
		if (!txt.empty())
		{
			std::vector<std::string_view> options;
			SplitString(txt, ",", options);

			for (const auto& opt : options)
			{
				LogInfo() << "Checking filter '" << opt << "'";
				if (MatchesPlatform(platform, opt))
				{
					LogInfo() << "Matched filter '" << opt << "'";
					return true;
				}
			}

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
				if (MatchesPlatform(platform, txt))
					return false;

			return true;
		}
	}

	return false;
}

bool EvalLinkFilters(const XMLNode* node, LinkingType linking)
{
	{
		const auto txt = XMLNodeAttrbiute(node, "include");
		if (!txt.empty())
		{
			std::vector<std::string_view> options;
			SplitString(txt, ",", options);

			for (const auto& txt : options)
				if (MatchesLinking(linking, txt))
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
				if (MatchesLinking(linking, txt))
					return false;

			return true;
		}
	}

	return true;
}

bool ProjectManifest::LoadKeySet(const void* root, const fs::path& modulePath, ProjectManifest* ret, const Configuration& config)
{
	bool valid = true;

	XMLNodeIterate((XMLNode*)root, [&](const XMLNode* node, std::string_view option)
		{
			if (option == "FilterPlatform")
			{
				if (EvalPlatformFilters(node, config.platform))
				{
					valid &= LoadKeySet(node, modulePath, ret, config);
				}
			}
			else if (option == "FilterLinking")
			{
				if (EvalLinkFilters(node, config.linking))
				{
					valid &= LoadKeySet(node, modulePath, ret, config);
				}
			}
			else
			{
				valid &= LoadKey(node, modulePath, ret);				
			}
		});

	return valid;
}

ProjectManifest* ProjectManifest::Load(const void* rootPtr, const fs::path& modulePath, const Configuration& config)
{
    auto ret = std::make_unique<ProjectManifest>();
    ret->rootPath = modulePath;
	ret->loadPath = modulePath;

	bool valid = true;

    XMLNode* root = (XMLNode*)rootPtr;
    valid &= EvalProjectType(ret.get(), XMLNodeTag(root));
	valid &= LoadKeySet(root, modulePath, ret.get(), config);

	if (ret->name.empty())
	{
		LogError() << "Project without a name at " << modulePath;
		return nullptr;
	}

    if (ret->rootPath.empty())
	{
        LogError() << "There's no root directory specified for a project '" << ret->name << "' at " << modulePath;
		return nullptr;
	}
	if (!fs::is_directory(ret->rootPath))
	{
		LogError() << "Directory " << ret->rootPath << " is not a valid directory for project '" << ret->name << " at " << modulePath;
		return nullptr;
	}

	if (ret->guid.empty())
		ret->guid = GuidFromText(ret->name + ret->rootPath.u8string().c_str());

	if (ret->localGroupName.empty())
		ret->localGroupName = PartBeforeLast(ret->name, "_");
	else
		LogWarning() << "Project '" << ret->name << "' uses custom solution group '" << ret->localGroupName << "'";

    for (const std::string& relativePath : ret->_temp_localIncludePaths)
    {
		const fs::path globalIncludePath = fs::weakly_canonical((modulePath / relativePath).make_preferred());
		LogInfo() << "Found project local include path " << globalIncludePath;

		if (fs::is_directory(globalIncludePath))
		{
			ret->localIncludePaths.push_back(globalIncludePath);
		}
		else
		{
			LogError() << "Specified local include path " << globalIncludePath << " does not point to a valid directory";
			valid = false;
		}
    }

	for (const std::string& relativePath : ret->_temp_exportedIncludePaths)
	{
		const fs::path globalIncludePath = fs::weakly_canonical((modulePath / relativePath).make_preferred());
		LogInfo() << "Found project export include path " << globalIncludePath;

		if (fs::is_directory(globalIncludePath))
		{
			ret->exportedIncludePaths.push_back(globalIncludePath);
		}
		else
		{
			LogError() << "Specified exported include path " << globalIncludePath << " does not point to a valid directory";
			valid = false;
		}
	}

	if (ret->optionLegacy || ret->optionThirdParty)
		ret->optionUsePrecompiledHeaders = false;

	if (!valid)
	{
		LogError() << "There were errors parsing project '" << ret->name << "' at " << modulePath;
		return nullptr;
	}

	return ret.release();
}

//--