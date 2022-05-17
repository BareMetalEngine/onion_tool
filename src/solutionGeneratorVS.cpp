#include "common.h"
#include "utils.h"
#include "configuration.h"
#include "fileGenerator.h"
#include "fileRepository.h"
#include "project.h"
#include "projectManifest.h"
#include "solutionGeneratorVS.h"
#include "externalLibrary.h"

SolutionGeneratorVS::SolutionGeneratorVS(FileRepository& files, const Configuration& config, std::string_view mainGroup)
    : SolutionGenerator(files, config, mainGroup)
{
    m_files.resolveDirectoryPath("vs", m_visualStudioScriptsPath);

    if (config.generator == GeneratorType::VisualStudio19)
    {
        m_projectVersion = "16.0";
        m_toolsetVersion = "v142";
    }
    else if (config.generator == GeneratorType::VisualStudio22)
    {
        m_projectVersion = "17.0";
        m_toolsetVersion = "v143";
    }
}

void SolutionGeneratorVS::printSolutionDeclarations(std::stringstream& f, const SolutionGroup* g)
{
    writelnf(f, "Project(\"{2150E333-8FDC-42A3-9474-1A3956D46DE8}\") = \"%s\", \"%s\", \"%s\"", g->name.c_str(), g->name.c_str(), g->assignedVSGuid.c_str());
    writeln(f, "EndProject");

    for (const auto* child : g->children)
        printSolutionDeclarations(f, child);

    for (const auto* p : g->projects)
    {
        auto projectClassGUID = "";
        auto projectFilePath = p->projectPath / p->name;
        projectFilePath += ".vcxproj";

        writelnf(f, "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"%s\", \"%s\", \"%s\"", p->name.c_str(), projectFilePath.u8string().c_str(), p->assignedVSGuid.c_str());

        /*for (const auto* dep : p->directDependencies)
        {
            if (dep->optionDetached && dep->type == ProjectType::SharedLibrary)
            {
                writelnf(f, "  ProjectSection(ProjectDependencies) = postProject");
                writelnf(f, "    %hs = %hs", dep->assignedVSGuid.c_str(), dep->assignedVSGuid.c_str());
                writelnf(f, "  EndProjectSection");
            }
        }*/

        writeln(f, "EndProject");
    }
}

/*void SolutionGeneratorVS::printSolutionScriptDeclarations(std::stringstream& f)
{
    if (!m_gen.scriptProjects.empty())
    {
        const auto groupText = GuidFromText("Scripts");

        writelnf(f, "Project(\"{2150E333-8FDC-42A3-9474-1A3956D46DE8}\") = \"Scripts\", \"Scripts\", \"%s\"", groupText.c_str());
        writeln(f, "EndProject");

        for (const auto* p : m_gen.scriptProjects)
        {
            writelnf(f, "Project(\"{FAE04EC0-301F-11D3-BF4B-00C04F79EFBC}\") = \"%s\", \"%s\", \"%s\"", p->name.c_str(), p->projectFile.u8string().c_str(), p->assignedVSGuid.c_str());
            writeln(f, "EndProject");
        }
    }
}*/

/*void SolutionGeneratorVS::printSolutionParentScriptLinks(std::stringstream& f)
{
    if (!m_gen.scriptProjects.empty())
    {
        const auto groupText = GuidFromText("Scripts");

        for (const auto* p : m_gen.scriptProjects)
            writelnf(f, "		%s = %s", p->assignedVSGuid.c_str(), groupText.c_str());
    }
}*/

void SolutionGeneratorVS::printSolutionParentLinks(std::stringstream& f, const SolutionGroup* g)
{
    for (const auto* child : g->children)
    {
        // {808CE59B-D2F0-45B3-90A4-C63953B525F5} = {943E2949-809F-4411-A11F-51D51E9E579B}
        writelnf(f, "\t\t%s = %s", child->assignedVSGuid.c_str(), g->assignedVSGuid.c_str());
        printSolutionParentLinks(f, child);
    }

    for (const auto* child : g->projects)
    {
        // {808CE59B-D2F0-45B3-90A4-C63953B525F5} = {943E2949-809F-4411-A11F-51D51E9E579B}
        writelnf(f, "\t\t%s = %s", child->assignedVSGuid.c_str(), g->assignedVSGuid.c_str());
    }
}

static const char* NameVisualStudioConfiguration(ConfigurationType config)
{    
    switch (config)
    {
        case ConfigurationType::Checked: return "Checked";
        case ConfigurationType::Release: return "Release";
        case ConfigurationType::Debug: return "Debug";
        case ConfigurationType::Final: return "Final";
    }

    return "Release";
}

static const char* NameVisualStudioPlatform(PlatformType config)
{
    switch (config)
    {
    case PlatformType::UWP: return "UWP";
    case PlatformType::Windows: return "x64";
    case PlatformType::Prospero: return "Prospero";
    case PlatformType::Scarlett: return "Gaming.Xbox.Scarlett.x64";
    }

    return "x64";
}

bool SolutionGeneratorVS::generateSolution(FileGenerator& gen)
{
    const auto solutionCoreName = ToLower(m_rootGroup->name);
    const auto solutionFileName = solutionCoreName + "." + m_config.mergedName() + ".sln";

    auto* file = gen.createFile(m_config.solutionPath / solutionFileName);
    auto& f = file->content;

    std::cout << "-------------------------------------------------------------------------------------------\n";
    std::cout << "-- SOLUTION FILE: " << file->absolutePath << "\n";
    std::cout << "-------------------------------------------------------------------------------------------\n";

    writeln(f, "Microsoft Visual Studio Solution File, Format Version 12.00");
    /*if (toolset.equals("v140")) {
        writeln(f, "# Visual Studio 14");
        writeln(f, "# Generated file, please do not modify");
        writeln(f, "VisualStudioVersion = 14.0.25420.1");
        writeln(f, "MinimumVisualStudioVersion = 10.0.40219.1");
    }
    else*/
    {
        writeln(f, "# Visual Studio Version 17");
        writeln(f, "VisualStudioVersion = 17.1.32328.378");
        writeln(f, "MinimumVisualStudioVersion = 10.0.40219.1");
    }

    printSolutionDeclarations(f, m_rootGroup);
    //printSolutionScriptDeclarations(f);

	writeln(f, "Global");

    {
        const auto c = NameVisualStudioConfiguration(m_config.configuration);
        const auto p = NameVisualStudioPlatform(m_config.platform);

        writeln(f, "\tGlobalSection(SolutionConfigurationPlatforms) = preSolution");
        writelnf(f, "\t\t%s|%s = %s|%s", c, p, c, p);
        writeln(f, "\tEndGlobalSection");

        // project configs
        writeln(f, "\tGlobalSection(ProjectConfigurationPlatforms) = postSolution");

        for (const auto* px : m_projects)
        {
            writelnf(f, "\t\t%s.%s|%s.ActiveCfg = %s|%s", px->assignedVSGuid.c_str(), c, p, c, p);
            writelnf(f, "\t%s.%s|%s.Build.0 = %s|%s", px->assignedVSGuid.c_str(), c, p, c, p);
        }

        writeln(f, "\tEndGlobalSection");
    }

    {
        writeln(f, "\tGlobalSection(SolutionProperties) = preSolution");
        writeln(f, "\t\tHideSolutionNode = FALSE");
        writeln(f, "\tEndGlobalSection");
    }

    {
        writeln(f, "\tGlobalSection(NestedProjects) = preSolution");
        printSolutionParentLinks(f, m_rootGroup);
        //printSolutionParentScriptLinks(f);
        writeln(f, "\tEndGlobalSection");
    }

    /*{
        writeln(f, "  GlobalSection(ExtensibilityGlobals) = postSolution");
        writeln(f, "    SolutionGuid = {5BD79A42-26CF-4E78-A4EB-70F7CDCEF592}");
        writeln(f, "  EndGlobalSection");
    }*/

    writeln(f, "EndGlobal");
    return true;
}

bool SolutionGeneratorVS::generateProjects(FileGenerator& gen)
{
    bool valid = true;

    for (const auto* p : m_projects)
    {
        if (p->type == ProjectType::SharedLibrary || p->type == ProjectType::StaticLibrary || p->type == ProjectType::Application || p->type == ProjectType::TestApplication)
        {
            {
                auto projectFilePath = p->projectPath / p->name;
                projectFilePath += ".vcxproj";

                auto* file = gen.createFile(projectFilePath);
                valid &= generateSourcesProjectFile(p, file->content);
            }

            {
                auto projectFilePath = p->projectPath / p->name;
                projectFilePath += ".vcxproj.filters";

                auto* file = gen.createFile(projectFilePath);
                valid &= generateSourcesProjectFilters(p, file->content);
            }
        }
        /*else if (p->originalProject->type == ProjectType::RttiGenerator)
        {
            {
                auto projectFilePath = p->projectPath / p->name;
                projectFilePath += ".vcxproj";

                auto* file = gen.createFile(projectFilePath);
                valid &= generateRTTIGenProjectFile(p, file->content);
            }
        }
        else if (p->originalProject->type == ProjectType::EmbeddedMedia)
        {
            {
                auto projectFilePath = p->projectPath / p->name;
                projectFilePath += ".vcxproj";

                auto* file = m_gen.createFile(projectFilePath);
                valid &= generateEmbeddedMediaProjectFile(p, file->content);
            }
        }*/
    }

    return true;
}

void SolutionGeneratorVS::extractSourceRoots(const SolutionProject* project, std::vector<fs::path>& outPaths) const
{
    for (const auto& sourceRoot : m_sourceRoots)
        outPaths.push_back(sourceRoot);

    // TODO: remove
    if (!project->rootPath.empty())
    {
        outPaths.push_back(project->rootPath / "src");
        outPaths.push_back(project->rootPath / "include");
    }

    outPaths.push_back(m_config.solutionPath / "generated/_shared");
    outPaths.push_back(project->generatedPath);

    for (const auto& path : project->additionalIncludePaths)
        outPaths.push_back(path);

    /*for (const auto& path : project->originalProject->localIncludeDirectories)
    {
        const auto fullPath = project->originalProject->rootPath / path;
        outPaths.push_back(fullPath);
    }*/
}

static void CollectDefineString(std::vector<std::pair<std::string, std::string>>& ar, std::string_view name, std::string_view value)
{
	for (auto& entry : ar)
	{
		if (entry.first == name)
		{
			entry.second = value;
			return;
		}
	}

	ar.emplace_back(std::make_pair(name, value));
}

static void CollectDefineStrings(std::vector<std::pair<std::string, std::string>>& ar, const std::vector<std::pair<std::string, std::string>>& defs)
{
    for (const auto& def : defs)
        CollectDefineString(ar, def.first, def.second);
}

bool SolutionGeneratorVS::generateSourcesProjectFile(const SolutionProject* project, std::stringstream& f) const
{
    writeln(f, "<?xml version=\"1.0\" encoding=\"utf-8\"?>");
    writeln(f, "<!-- Auto generated file, please do not edit -->");

    writelnf(f, "<Project DefaultTargets=\"Build\" ToolsVersion=\"%s\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">", m_projectVersion);

    if (m_config.platform == PlatformType::Windows)
        writelnf(f, "<Import Project=\"%s\\SharedConfigurationSetup.props\"/>", m_visualStudioScriptsPath.u8string().c_str());
    else if (m_config.platform == PlatformType::UWP)
        writelnf(f, "<Import Project=\"%s\\SharedConfigurationSetupUWP.props\"/>", m_visualStudioScriptsPath.u8string().c_str());
    else if (m_config.platform == PlatformType::Prospero)
        writelnf(f, "<Import Project=\"%s\\SharedConfigurationSetupProspero.props\"/>", m_visualStudioScriptsPath.u8string().c_str());
    else if (m_config.platform == PlatformType::Scarlett)
        writelnf(f, "<Import Project=\"%s\\SharedConfigurationSetupScarlett.props\"/>", m_visualStudioScriptsPath.u8string().c_str());

    writeln(f, "<PropertyGroup>");
    writelnf(f, "  <PlatformToolset>%s</PlatformToolset>", m_toolsetVersion);
    writelnf(f, "  <VCProjectVersion>%s</VCProjectVersion>", m_projectVersion);
    writeln(f, "  <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>");
    writeln(f, "  <WindowsTargetPlatformMinVersion>10.0</WindowsTargetPlatformMinVersion>");
    writeln(f, "  <DefaultLanguage>en-US</DefaultLanguage>");

    if (m_config.platform == PlatformType::UWP)
    {
        writeln(f, "  <AppContainerApplication>true</AppContainerApplication>");
        writeln(f, "  <MinimumVisualStudioVersion>14.0</MinimumVisualStudioVersion>");
        writeln(f, "  <AppContainerApplication>true</AppContainerApplication>");
    }
    else if (m_config.platform == PlatformType::Windows)
    {
    }

    {
        f << "  <SourcesRoot>";
        std::vector<fs::path> sourceRoots;
        extractSourceRoots(project, sourceRoots);

        for (auto& root : sourceRoots)
            f << root.make_preferred().u8string() << "\\;";

        f << "</SourcesRoot>\n";
    }

    {
        f << "  <LibraryIncludePath>";
        /*for (const auto* lib : project->originalProject->resolvedDependencies)
            if (lib->type == ProjectType::LocalLibrary && lib->optionGlobalInclude)
                    f << (lib->rootPath / "include").u8string() << "\\;";*/

        /*for (const auto* source : project->originalProject->moduleSourceProjects)
            if (source->type == ProjectType::LocalLibrary)
                f << (source->rootPath / "include").u8string() << "\\;";*/

        for (const auto* lib : project->libraryDependencies)
            if (!lib->includePath.empty())
                f << lib->includePath.u8string() << "\\;";
        f << "</LibraryIncludePath>\n";
    }

    writelnf(f, " 	<ProjectOutputPath>%s\\</ProjectOutputPath>", project->outputPath.u8string().c_str());
    writelnf(f, " 	<ProjectGeneratedPath>%s\\</ProjectGeneratedPath>", project->generatedPath.u8string().c_str());
    writelnf(f, " 	<ProjectPublishPath>%s\\</ProjectPublishPath>", m_config.deployPath.u8string().c_str());
    writelnf(f, " 	<ProjectSourceRoot>%s\\</ProjectSourceRoot>", project->rootPath.u8string().c_str());
    writelnf(f, " 	<ProjectMediaRoot>%s\\</ProjectMediaRoot>", (project->rootPath / "media").u8string().c_str());

    if (!m_config.staticBuild) // tool paths for runtime rebuilds
    {
        fs::path toolPath;
        if (m_files.resolveDirectoryPath("tools/bison/windows/", toolPath))
        {
            const auto bisonExecutablePath = (toolPath / "win_bison.exe").make_preferred();
            writelnf(f, " 	<ProjectBisonToolPath>%s</ProjectBisonToolPath>", bisonExecutablePath.u8string().c_str());
        }

		writelnf(f, " 	<ProjectOnionExecutable>%s</ProjectOnionExecutable>", m_config.builderExecutablePath.u8string().c_str());
    }

	if (project->optionWarningLevel == 0)
		writeln(f, "    <ProjectWarningLevel>TurnOffAllWarnings</ProjectWarningLevel> ");
    else if (project->optionWarningLevel == 1)
        writeln(f, "    <ProjectWarningLevel>Level1</ProjectWarningLevel> ");
	else if (project->optionWarningLevel == 2)
		writeln(f, "    <ProjectWarningLevel>Level2</ProjectWarningLevel> ");
	else if (project->optionWarningLevel == 3)
		writeln(f, "    <ProjectWarningLevel>Level3</ProjectWarningLevel> ");
	else if (project->optionWarningLevel == 4)
		writeln(f, " 	<ProjectWarningLevel>Level4</ProjectWarningLevel> ");

    if (project->type == ProjectType::Application || project->type == ProjectType::TestApplication)
    {
        if (project->optionUseWindowSubsystem)
        {
            writeln(f, " 	<ConfigurationType>Application</ConfigurationType>");
            writeln(f, " 	<SubSystem>Windows</SubSystem>");
        }
        else
        {
            writeln(f, " 	<ConfigurationType>Application</ConfigurationType>");
            writeln(f, " 	<SubSystem>Console</SubSystem>");
        }
    }
    else if (project->type == ProjectType::StaticLibrary)
    {
        writeln(f, " 	<ConfigurationType>StaticLibrary</ConfigurationType>");
    }
    else if (project->type == ProjectType::SharedLibrary)
    {
        writeln(f, " 	<ConfigurationType>DynamicLibrary</ConfigurationType>");
    }

    f << " 	<ProjectPreprocessorDefines>$(ProjectPreprocessorDefines);";

    if (m_config.libs == LibraryType::Shared)
    {
        f << ToUpper(project->name) << "_EXPORTS;";

        if (project->type == ProjectType::SharedLibrary)
            f << ToUpper(project->name) << "_DLL;";        
    }

    f << "PROJECT_NAME=" << project->name << ";";

    if (!project->optionUseWindowSubsystem)
        f  << "CONSOLE;";

    if (m_config.build == BuildType::Development)
        f << "BUILD_WITH_DEVTOOLS;";

    if (m_config.platform == PlatformType::UWP)
        f << "WINAPI_FAMILY=WINAPI_FAMILY_APP;";

    for (const auto* dep : project->allDependencies)
        f << "HAS_" << ToUpper(dep->name) << ";";

    for (const auto* dep : project->allDependencies)
        if (dep->type == ProjectType::SharedLibrary)
            f << ToUpper(dep->name) << "_DLL;";

    if (m_config.libs == LibraryType::Static || project->type == ProjectType::StaticLibrary)
        f << "BUILD_AS_LIBS;";

    if (project->type == ProjectType::SharedLibrary)
        f << "BUILD_DLL;";
    else if (project->type == ProjectType::StaticLibrary)
        f << "BUILD_LIB;";

    /*if (project->originalProject->hasTestData)
    {
        const auto testFolderPath = (project->originalProject->rootPath / "test").make_preferred();
        f << "TEST_DATA_PATH=\"" << testFolderPath.u8string() << "\";";
    }*/

    // custom defines
    {
        std::vector<std::pair<std::string, std::string>> defs;

        for (const auto* dep : project->allDependencies)
            CollectDefineStrings(defs, dep->globalDefines);
        CollectDefineStrings(defs, project->localDefines);

        for (const auto& def : defs)
        {
            //std::cout << "Adding define '" << def.first << "' = '" << def.second << "'\n";
            if (def.second.empty())
                f << def.first << ";";
            else
                f << def.first << "=" << def.second << ";";
        }
    }    

    f << "</ProjectPreprocessorDefines>\n";

    writelnf(f, " 	<ProjectGuid>%s</ProjectGuid>", project->assignedVSGuid.c_str());
    writeln(f, "</PropertyGroup>");

    writelnf(f, "<Import Project=\"%s\\SharedItemGroups.props\"/>", m_visualStudioScriptsPath.u8string().c_str());
    writelnf(f, " <Import Project=\"%s\\Shared.targets\"/>", m_visualStudioScriptsPath.u8string().c_str());

    /*long numAssemblyFiles = this.files.stream().filter(pf->pf.type == FileType.ASSEMBLY).count();
    if (numAssemblyFiles > 0) {
        writeln(f, "<ImportGroup Label=\"ExtensionSettings\" >");
        writeln(f, "  <Import Project=\"$(VCTargetsPath)\\BuildCustomizations\\masm.props\" />");
        writeln(f, "</ImportGroup>");
    }*/

    writeln(f, "<ItemGroup>");
    for (const auto* pf : project->files)
        generateSourcesProjectFileEntry(project, pf, f);
    writeln(f, "</ItemGroup>");

    writeln(f, "<ItemGroup>");
    if (m_config.libs == LibraryType::Static)
    {
        // in lib mode only the application reports dependencies - this way ALL projects can be compiled at the same time
        if (project->type == ProjectType::Application || project->type == ProjectType::TestApplication)
        {
            for (const auto* dep : project->allDependencies)
            {
                auto projectFilePath = dep->projectPath / dep->name;
                projectFilePath += ".vcxproj";

                writelnf(f, " <ProjectReference Include=\"%s\">", projectFilePath.u8string().c_str());
                writelnf(f, "   <Project>%s</Project>", dep->assignedVSGuid.c_str());
                if (dep->optionDetached)
                    writelnf(f, "   <LinkLibraryDependencies>false</LinkLibraryDependencies>");
                writeln(f, " </ProjectReference>");
            }
        }
    }
    else if (m_config.libs == LibraryType::Shared)
    {
        // in DLL mode we reference DIRECT dependencies only
        for (const auto* dep : project->directDependencies)
        {
            auto projectFilePath = dep->projectPath / dep->name;
            projectFilePath += ".vcxproj";

            writelnf(f, " <ProjectReference Include=\"%s\">", projectFilePath.u8string().c_str());
            writelnf(f, "   <Project>%s</Project>", dep->assignedVSGuid.c_str());
			if (dep->optionDetached)
				writelnf(f, "   <LinkLibraryDependencies>false</LinkLibraryDependencies>");
            writeln(f, " </ProjectReference>");
        }
    }
	writeln(f, "</ItemGroup>");

    /*if (project->originalProject->type == ProjectType::LocalApplication)
    {
        for (const auto* dep : m_gen.scriptProjects)
        {
            auto projectFilePath = dep->projectPath / dep->name;
            projectFilePath += ".csproj";

            writelnf(f, " <ProjectReference Include=\"%s\">", projectFilePath.u8string().c_str());
            writelnf(f, "   <Project>%s</Project>", dep->assignedVSGuid.c_str());
            writelnf(f, "   <Private>false</Private>");
            writelnf(f, "   <ReferenceOutputAssembly>false</ReferenceOutputAssembly>");
            writeln(f, " </ProjectReference>");
        }
    }*/

    writeln(f, " <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.targets\"/>");

    /*if (numAssemblyFiles > 0) {
        writeln(f, "<ImportGroup Label=\"ExtensionTargets\">");
        writeln(f, "  <Import Project=\"$(VCTargetsPath)\\BuildCustomizations\\masm.targets\" />");
        writeln(f, "</ImportGroup>");
    }*/

    writeln(f, "</Project>");

    return true;
}

bool SolutionGeneratorVS::generateSourcesProjectFileEntry(const SolutionProject* project, const SolutionProjectFile* file, std::stringstream& f) const
{
    switch (file->type)
    {
        case ProjectFileType::CppHeader:
        {
            writelnf(f, "   <ClInclude Include=\"%s\">", file->absolutePath.u8string().c_str());
            /*if (!file->useInCurrentBuild)
                writeln(f, "   <ExcludedFromBuild>true</ExcludedFromBuild>");*/
            writeln(f, "   </ClInclude>");
            break;
        }

        case ProjectFileType::CppSource:
        {
            writelnf(f, "   <ClCompile Include=\"%s\">", file->absolutePath.u8string().c_str());

            if (project->optionUsePrecompiledHeaders)
            {
                if (file->name == "build.cpp" || file->name == "build.cxx")
                    writeln(f, "      <PrecompiledHeader>Create</PrecompiledHeader>");
                else if (file->usePrecompiledHeader)
                    writeln(f, "      <PrecompiledHeader>Use</PrecompiledHeader>");
                else
					writeln(f, "      <PrecompiledHeader>NotUsing</PrecompiledHeader>");
				
            }
            else
            {
                writeln(f, "      <PrecompiledHeader>NotUsing</PrecompiledHeader>");
            }

            /*if (!file->useInCurrentBuild)
                writeln(f, "      <ExcludedFromBuild>true</ExcludedFromBuild>");*/

            if (m_config.platform == PlatformType::UWP)
            {
                const auto ext = file->absolutePath.extension().u8string();
                auto compileAsWinRT = false;
                
                if (ext == ".cxx" || project->type == ProjectType::Application)
                    compileAsWinRT = true;

                if (compileAsWinRT)
                    writeln(f, "      <CompileAsWinRT>true</CompileAsWinRT>");
                else                
                    writeln(f, "      <CompileAsWinRT>false</CompileAsWinRT>");
            }

            if (!file->projectRelativePath.empty())
            {
                const auto relativePath = fs::path(file->projectRelativePath).parent_path().u8string();
                const auto fullPath = (project->outputPath / "obj" / relativePath).make_preferred();

				std::error_code ec;
				if (!fs::is_directory(fullPath, ec))
				{
					if (!fs::create_directories(fullPath, ec))
					{
						std::cout << "Failed to create solution directory " << fullPath << "\n";
						return false;
					}
				}

                writelnf(f, "      <ObjectFileName>$(IntDir)\\%s\\</ObjectFileName>", relativePath.c_str());
            }

            writeln(f, "   </ClCompile>");
            break;
        }

        case ProjectFileType::Bison:
        {
            if (m_config.staticBuild)
            {
				writelnf(f, "   <None Include=\"%s\">", file->absolutePath.u8string().c_str());
				writelnf(f, "      <SubType>Bison</SubType>");
				writeln(f, "   </None>");
            }
            else
            {
                writelnf(f, "   <BisonScripts Include=\"%s\" />", file->absolutePath.u8string().c_str());
            }
            break;
        }

        case ProjectFileType::WindowsResources:
        {
            writelnf(f, "   <ResourceCompile Include=\"%s\"/>", file->absolutePath.u8string().c_str());
            break;
        }

        case ProjectFileType::MediaFile:
        {
            if (m_config.staticBuild)
                writelnf(f, "   <None Include=\"%s\"/>", file->absolutePath.u8string().c_str());
            else
                writelnf(f, "   <MediaFile Include=\"%s\"/>", file->absolutePath.u8string().c_str());
            break;
        }

        case ProjectFileType::NatVis:
        {
            writelnf(f, "   <Natvis Include=\"%s\"/>", file->absolutePath.u8string().c_str());
            break;
        }

        /*case VSIXMANIFEST:
        {
            f.writelnf("   <None Include=\"%s\">", pf.absolutePath);
            f.writelnf("      <SubType>Designer</SubType>");
            f.writelnf("   </None>");
            break;
        }*/

    }

    return true;
}

static void AddFilterSection(std::string_view path, std::vector<std::string>& outFilterSections)
{
    if (outFilterSections.end() == find(outFilterSections.begin(), outFilterSections.end(), path))
        outFilterSections.push_back(std::string(path));
}

static void AddFilterSectionRecursive(std::string_view path, std::vector<std::string>& outFilterSections)
{
    auto pos = path.find_last_of('\\');
    if (pos != -1)
    {
        auto left = path.substr(0, pos);
        AddFilterSectionRecursive(left, outFilterSections);
    }

    AddFilterSection(path, outFilterSections);
    
}

bool SolutionGeneratorVS::generateSourcesProjectFilters(const SolutionProject* project, std::stringstream& f) const
{
    writeln(f, "<?xml version=\"1.0\" encoding=\"utf-8\"?>");
    writeln(f, "<!-- Auto generated file, please do not edit -->");
    writeln(f, "<Project ToolsVersion=\"4.0\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">");

    std::vector<std::string> filterSections;

    // file entries
    {
        writeln(f, "<ItemGroup>");
        for (const auto* file : project->files)
        {
            const char* filterType = "None";

            switch (file->type)
            {
                case ProjectFileType::CppHeader:
                    filterType = "ClInclude";
                    break;
                case ProjectFileType::CppSource:
                    filterType = "ClCompile";
                    break;
				case ProjectFileType::Bison:
					filterType = m_config.staticBuild ? "None" : "BisonScripts";
					break;
                case ProjectFileType::MediaFile:
                    filterType = m_config.staticBuild ? "None" : "MediaFile";
                    break;
				case ProjectFileType::NatVis:
					filterType = "NatVis";
					break;
            }

            if (file->filterPath.empty())
            {
                writelnf(f, "  <%s Include=\"%s\"/>", filterType, file->absolutePath.u8string().c_str());
            }
            else
            {
                writelnf(f, "  <%s Include=\"%s\">", filterType, file->absolutePath.u8string().c_str());
                writelnf(f, "    <Filter>%s</Filter>", file->filterPath.c_str());
                writelnf(f, "  </%s>", filterType);

                AddFilterSectionRecursive(file->filterPath, filterSections);
            }
        }           

        writeln(f, "</ItemGroup>");
    }

    // filter section
    {
        writeln(f, "<ItemGroup>");

        for (const auto& section : filterSections)
        {
            const auto guid = GuidFromText(section);

            writelnf(f, "  <Filter Include=\"%s\">", section.c_str());
            writelnf(f, "    <UniqueIdentifier>%s</UniqueIdentifier>", guid.c_str());
            writelnf(f, "  </Filter>");
        }

        writeln(f, "</ItemGroup>");
    }

    writeln(f, "</Project>");

    return true;
}

#if 0
bool SolutionGeneratorVS::generateEmbeddedMediaProjectFile(const SolutionProject* project, std::stringstream& f) const
{
    writeln(f, "<?xml version=\"1.0\" encoding=\"utf-8\"?>");
    writeln(f, "<!-- Auto generated file, please do not edit -->");

    writelnf(f, "<Project DefaultTargets=\"Build\" ToolsVersion=\"%s\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">", m_projectVersion);

    if (m_config.platform == PlatformType::Windows)
        writelnf(f, "<Import Project=\"%s\\SharedConfigurationSetup.props\"/>", m_visualStudioScriptsPath.u8string().c_str());
    else if (m_config.platform == PlatformType::UWP)
        writelnf(f, "<Import Project=\"%s\\SharedConfigurationSetupUWP.props\"/>", m_visualStudioScriptsPath.u8string().c_str());
    else if (m_config.platform == PlatformType::Prospero)
        writelnf(f, "<Import Project=\"%s\\SharedConfigurationSetupProspero.props\"/>", m_visualStudioScriptsPath.u8string().c_str());
    else if (m_config.platform == PlatformType::Scarlett)
        writelnf(f, "<Import Project=\"%s\\SharedConfigurationSetupScarlett.props\"/>", m_visualStudioScriptsPath.u8string().c_str());

    writeln(f, "<PropertyGroup>");
    writelnf(f, "  <PlatformToolset>%s</PlatformToolset>", m_toolsetVersion);
    writelnf(f, "  <VCProjectVersion>%s</VCProjectVersion>", m_projectVersion);
    writeln(f, "  <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>");
    writeln(f, "  <ModuleType>Empty</ModuleType>");
    writeln(f, " <SolutionType>SharedLibraries</SolutionType>");
    writelnf(f, "  <ProjectGuid>%s</ProjectGuid>", project->assignedVSGuid.c_str());
    writeln(f, "  <DisableFastUpToDateCheck>true</DisableFastUpToDateCheck>");
    writelnf(f, " 	<ProjectOutputPath>%s\\</ProjectOutputPath>", project->outputPath.u8string().c_str());
    writelnf(f, " 	<ProjectPublishPath>%s\\</ProjectPublishPath>", m_config.deployPath.u8string().c_str());
    writeln(f, "</PropertyGroup>");
    writeln(f, "  <PropertyGroup>");
    writeln(f, "    <PreBuildEventUseInBuild>true</PreBuildEventUseInBuild>");
    writeln(f, "  </PropertyGroup>");
    writeln(f, "  <ItemDefinitionGroup>");
    writeln(f, "    <PreBuildEvent>");

    auto toolPath = (m_config.deployPath / "tool_fxc.exe").u8string();
    auto toolPathStr = std::string(toolPath.c_str());

    {
        std::stringstream cmd;
        //writelnf(f, "<Error Text=\"No tool to compile embedded media found, was tool_embedd compiled properly?\" Condition=\"!Exists('$%hs')\" />", toolPath.c_str());

        for (const auto* pf : project->files)
        {
            if (pf->type == ProjectFileType::MediaFileList)
            {
                f << "      <Command>" << toolPath << " pack -input=" << pf->absolutePath.u8string() << "</Command>\n";
            }
        }
    }

    writeln(f, "    </PreBuildEvent>");
    writeln(f, "  </ItemDefinitionGroup>");
    writelnf(f, "<Import Project=\"%s\\SharedItemGroups.props\"/>", m_visualStudioScriptsPath.u8string().c_str());
    writeln(f, "  <ItemGroup>");
    //for (const auto* pf : project->files)
      //  generateSourcesProjectFileEntry(project, pf, f);
    writeln(f, "  </ItemGroup>");
    writeln(f, "  <ItemGroup>");
    for (const auto* dep : project->directDependencies)
    {
        auto projectFilePath = dep->projectPath / dep->name;
        projectFilePath += ".vcxproj";

        writelnf(f, " <ProjectReference Include=\"%s\">", projectFilePath.u8string().c_str());
        writelnf(f, "   <Project>%s</Project>", dep->assignedVSGuid.c_str());
        writeln(f, " </ProjectReference>");
    }
    writeln(f, "  </ItemGroup>");
	writelnf(f, " <Import Project=\"%s\\Shared.targets\"/>", m_visualStudioScriptsPath.u8string().c_str());
    writeln(f, " <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.targets\"/>");
    writeln(f, "</Project>");

    return true;
}

bool SolutionGeneratorVS::generateRTTIGenProjectFile(const SolutionProject* project, std::stringstream& f) const
{
    writeln(f, "<?xml version=\"1.0\" encoding=\"utf-8\"?>");
    writeln(f, "<!-- Auto generated file, please do not edit -->");

    writelnf(f, "<Project DefaultTargets=\"Build\" ToolsVersion=\"%s\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">", m_projectVersion);

    if (m_config.platform == PlatformType::Windows)
        writelnf(f, "<Import Project=\"%s\\SharedConfigurationSetup.props\"/>", m_visualStudioScriptsPath.u8string().c_str());
    else if (m_config.platform == PlatformType::UWP)
        writelnf(f, "<Import Project=\"%s\\SharedConfigurationSetupUWP.props\"/>", m_visualStudioScriptsPath.u8string().c_str());
    else if (m_config.platform == PlatformType::Prospero)
        writelnf(f, "<Import Project=\"%s\\SharedConfigurationSetupProspero.props\"/>", m_visualStudioScriptsPath.u8string().c_str());
    else if (m_config.platform == PlatformType::Scarlett)
        writelnf(f, "<Import Project=\"%s\\SharedConfigurationSetupScarlett.props\"/>", m_visualStudioScriptsPath.u8string().c_str());

    writeln(f,  "<PropertyGroup>");
    writelnf(f, "  <PlatformToolset>%s</PlatformToolset>", m_toolsetVersion);
    writelnf(f, "  <VCProjectVersion>%s</VCProjectVersion>", m_projectVersion);
    writeln(f,  "  <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>");
    writeln(f,  "  <ModuleType>Empty</ModuleType>");
    writeln(f,   " <SolutionType>SharedLibraries</SolutionType>");
    writelnf(f, "  <ProjectGuid>%s</ProjectGuid>", project->assignedVSGuid.c_str());
    writeln(f,  "  <DisableFastUpToDateCheck>true</DisableFastUpToDateCheck>");
    writelnf(f, " 	<ProjectOutputPath>%s\\</ProjectOutputPath>", project->outputPath.u8string().c_str());
    writelnf(f, " 	<ProjectPublishPath>%s\\</ProjectPublishPath>", m_config.deployPath.u8string().c_str());
    writeln(f,  "</PropertyGroup>");
    writeln(f,  "  <PropertyGroup>");
    writeln(f,  "    <PreBuildEventUseInBuild>true</PreBuildEventUseInBuild>");
    writeln(f,  "  </PropertyGroup>");
    writeln(f,  "  <ItemDefinitionGroup>");
    writeln(f,  "    <PreBuildEvent>");

    {
        std::stringstream cmd;
        f << "      <Command>";
        f << m_config.builderExecutablePath.u8string() << " ";
        f << "-tool=reflection ";

        const auto reflectionListPath = project->generatedPath / "rtti_list.txt";
        f << "-list= \"" << reflectionListPath.u8string() << "\"";
        /*f << "-build=" << NameEnumOption(m_config.build) << " ";
        f << "-config=" << NameEnumOption(m_config.configuration) << " ";
        f << "-platform=" << NameEnumOption(m_config.platform) << " ";
        f << "-libs=" << NameEnumOption(m_config.libs) << " ";
        f << "-generator=" << NameEnumOption(m_config.generator) << " ";*/
        f << "</Command>\n";
    }

    writeln(f, "    </PreBuildEvent>");
    writeln(f, "  </ItemDefinitionGroup>");
    writelnf(f, "<Import Project=\"%s\\SharedItemGroups.props\"/>", m_visualStudioScriptsPath.u8string().c_str());
    writeln(f, "  <ItemGroup>");
    writeln(f, "  </ItemGroup>");
    writeln(f, "  <ItemGroup>");
    writeln(f, "  </ItemGroup>");
    writelnf(f, " <Import Project=\"%s\\Shared.targets\"/>", m_visualStudioScriptsPath.u8string().c_str());
    writeln(f, " <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.targets\"/>");
    writeln(f, "</Project>");

    return true;
}
#endif

//--
