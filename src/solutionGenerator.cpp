#include "common.h"
#include "configuration.h"
#include "utils.h"
#include "project.h"
#include "projectManifest.h"
#include "projectCollection.h"
#include "externalLibrary.h"
#include "moduleManifest.h"
#include "fileGenerator.h"
#include "fileRepository.h"
#include "solutionGenerator.h"
#include "toolEmbed.h"
#include "toolReflection.h"

//--

SolutionProjectFile::SolutionProjectFile()
{}

SolutionProjectFile::~SolutionProjectFile()
{}

//--

SolutionGroup::SolutionGroup()
{}

SolutionGroup::~SolutionGroup()
{
    for (auto* group : children)
        delete group;
}

//--

SolutionProject::SolutionProject()
{}

SolutionProject::~SolutionProject()
{
    for (auto* file : files)
        delete file;
}

//--

SolutionGenerator::SolutionGenerator(FileRepository& files, const Configuration& config, std::string_view mainGroup)
    : m_config(config)
    , m_files(files)
{
    m_rootGroup = new SolutionGroup;
    m_rootGroup->name = mainGroup;
    m_rootGroup->mergedName = mainGroup;
    m_rootGroup->assignedVSGuid = GuidFromText(m_rootGroup->mergedName);

    m_sharedGlueFolder = config.solutionPath / "generated" / "_shared";
}

SolutionGenerator::~SolutionGenerator()
{
    for (auto* proj : m_projects)
        delete proj;

    delete m_rootGroup;
}

struct OrderedGraphBuilder
{
    std::unordered_map<SolutionProject*, int> depthMap;    

    bool insertProject(SolutionProject* p, int depth, std::vector<SolutionProject*>& stack)
    {
        if (find(stack.begin(), stack.end(), p) != stack.end())
        {
            std::cout << "Recursive project dependencies found when project '" << p->name << "' was encountered second time\n";
            for (const auto* proj : stack)
                std::cout << "  Reachable from '" << proj->name << "'\n";
            return false;
        }

        auto currentDepth = depthMap[p];
        if (depth > currentDepth)
        {
            depthMap[p] = depth;

            stack.push_back(p);

            for (auto* dep : p->directDependencies)
                if (!insertProject(dep, depth + 1, stack))
                    return false;

            stack.pop_back();
        }

        return true;
    }

    void extractOrderedList(std::vector<SolutionProject*>& outList) const
    {
        std::vector<std::pair<SolutionProject*, int>> pairs;
        std::copy(depthMap.begin(), depthMap.end(), std::back_inserter(pairs));
        std::sort(pairs.begin(), pairs.end(), [](const auto& a, const auto& b) -> bool {
            if (a.second != b.second)
                return a.second > b.second;
            return a.first->name < b.first->name;
            });
        
        for (const auto& pair : pairs)
            outList.push_back(pair.first);
    }
};

SolutionGroup* SolutionGenerator::findOrCreateGroup(std::string_view name, SolutionGroup* parent)
{
    for (auto* group : parent->children)
        if (group->name == name)
            return group;

    auto* group = new SolutionGroup;
    group->name = name;
    group->mergedName = parent->mergedName + "_" + std::string(name);
    group->parent = parent;
    group->assignedVSGuid = GuidFromText(group->mergedName);
    parent->children.push_back(group);
    return group;
}

SolutionProject* SolutionGenerator::findProject(std::string_view name) const
{
    auto it = m_projectNameMap.find(std::string(name));
    if (it != m_projectNameMap.end())
        return it->second;
    return nullptr;
}

SolutionGroup* SolutionGenerator::createGroup(std::string_view name, SolutionGroup* parent)
{
    std::vector<std::string_view> parts;
    SplitString(name, "_", parts);

    auto* cur = parent ? parent : m_rootGroup;
    for (const auto& part : parts)
        cur = findOrCreateGroup(part, cur);

    return cur;
}

static bool HasDependency(const SolutionProject* project, std::string_view name)
{
    for (const auto* dep : project->allDependencies)
        if (dep->name == name)
            return true;
    return false;
}

bool SolutionGenerator::extractProjects(const ProjectCollection& collection)
{
    // create projects
    std::unordered_set<const ModuleManifest*> usedModules;
    std::unordered_map<SolutionProject*, const ProjectInfo*> projectMap;
    std::unordered_map<const ProjectInfo*, SolutionProject*> projectRevMap;
    for (const auto* proj : collection.projects())
    {
        // create wrapper
        auto* generatorProject = new SolutionProject;
        generatorProject->type = proj->manifest->type;
        generatorProject->name = proj->name;

        // paths
		generatorProject->rootPath = proj->rootPath;
        generatorProject->generatedPath = m_config.solutionPath / "generated" / proj->name;
        generatorProject->projectPath = m_config.solutionPath / "projects" / proj->name;
        generatorProject->outputPath = m_config.solutionPath / "output" / proj->name;
        //generatorProject->hasEmbeddedFiles = false;// proj->hasMedia;

        // options
        generatorProject->optionUsePrecompiledHeaders = proj->manifest->optionUsePrecompiledHeaders;
        generatorProject->optionGenerateMain = proj->manifest->optionGenerateMain;
        generatorProject->optionUseStaticInit = proj->manifest->optionUseStaticInit;
        generatorProject->optionUseWindowSubsystem = (proj->manifest->optionSubstem == ProjectAppSubsystem::Windows);
        generatorProject->optionWarningLevel = proj->manifest->optionWarningLevel;
        generatorProject->optionUseExceptions = proj->manifest->optionUseExceptions;
        generatorProject->optionDetached = proj->manifest->optionDetached;
        generatorProject->optionExportApplicataion = proj->manifest->optionExportApplicataion;
        generatorProject->appHeaderName = proj->manifest->appHeaderName;
        generatorProject->appClassName = proj->manifest->appClassName;
        generatorProject->assignedVSGuid = proj->manifest->guid;
        generatorProject->libraryDependencies = proj->resolvedLibraryDependencies;
        generatorProject->localDefines = proj->manifest->localDefines;
        generatorProject->globalDefines = proj->manifest->globalDefines;

        // register in solution and map
        m_projects.push_back(generatorProject);
        m_projectNameMap[proj->name] = generatorProject;
        projectMap[generatorProject] = proj;
        projectRevMap[proj] = generatorProject;

        // collect modules that were actually used
        if (proj->parentModule)
            usedModules.insert(proj->parentModule);

        // public header file - only if not detached
        {
            const auto publicHeader = (proj->rootPath / "include/public.h").make_preferred();
            if (fs::is_regular_file(publicHeader))
                generatorProject->localPublicHeader = publicHeader;
        }

        // private header file
        {
			const auto privateHeader = (proj->rootPath / "src/private.h").make_preferred();
            if (fs::is_regular_file(privateHeader))
                generatorProject->localPrivateHeader = privateHeader;
        }

        // create file wrappers
        for (const auto* file : proj->files)
        {
            auto* info = new SolutionProjectFile;
            info->absolutePath = file->absolutePath;
            info->projectRelativePath = file->projectRelativePath;
            info->scanRelativePath = file->scanRelativePath;
            info->filterPath = fs::relative(file->absolutePath.parent_path(), proj->rootPath).u8string();
            if (info->filterPath == ".")
                info->filterPath.clear();
            info->name = file->name;
            //info->originalFile = file;
            info->type = file->type;

			// precompiled header settings
            if (proj->manifest->optionUsePrecompiledHeaders)
                info->usePrecompiledHeader = EndsWith(file->name, ".cpp");
            else
                info->usePrecompiledHeader = false;

            generatorProject->files.push_back(info);
        }

        // determine root group
        {
            const bool isLocalProject = proj->parentModule ? proj->parentModule->local : true;
            const auto parentGroup = isLocalProject ? m_rootGroup : createGroup("external");
            const auto groupName = PartBefore(proj->name, "_");

            generatorProject->group = createGroup(groupName, parentGroup);
            generatorProject->group->projects.push_back(generatorProject);
        }        
    }

    // map dependencies
    for (auto* localProj : m_projects)
    {
        if (auto* proj = Find<SolutionProject*, const ProjectInfo*>(projectMap, localProj, nullptr))
        {
            for (const auto* dep : proj->resolvedDependencies)
            {
                if (auto* depProject = Find<const ProjectInfo*, SolutionProject*>(projectRevMap, dep, nullptr))
                {
                    localProj->directDependencies.push_back(depProject);
                }
            }
        }
    }

	// build merged dependencies
	bool validDeps = true;
	for (auto* proj : m_projects)
	{
		std::vector<SolutionProject*> stack;
		stack.push_back(proj);

		OrderedGraphBuilder graph;
		for (auto* dep : proj->directDependencies)
			validDeps &= graph.insertProject(dep, 1, stack);
		graph.extractOrderedList(proj->allDependencies);
	}

	// disable static initialization on projects that don't use the core
    for (auto* proj : m_projects)
    {
        if (proj->optionUseReflection && !HasDependency(proj, "bm_core_object"))
            proj->optionUseReflection = false;

		if (proj->optionUseStaticInit && !HasDependency(proj, "bm_core_system"))
			proj->optionUseStaticInit = false;
    }

    // create the _rtti_generator project
    /*{
        bool needsRttiGenerator = false;

        if (auto* coreObjectsProject = findProject("bm_core_object"))
        {
            // scan all project for the need of RTTI generator
            for (auto* proj : m_projects)
            {
                if (proj->type == ProjectType::Application || proj->type == ProjectType::SharedLibrary || proj->type == ProjectType::SharedLibrary)
                {
                    // determine if the project depends on the core object project, if so it can has rtti generator
                    if (Contains(proj->allDependencies, coreObjectsProject))
                    {
                        needsRttiGenerator = true;
                        proj->hasReflection = true;
                    }
                }
            }

            // TODO: create rtti generator project ?

        }
    }*/   

    // build final project list
    {
        auto temp = std::move(m_projects);
		m_projects.clear();

        std::vector<SolutionProject*> stack;

        OrderedGraphBuilder graph;
        for (auto* proj : temp)
            validDeps &= graph.insertProject(proj, 1, stack);

        graph.extractOrderedList(m_projects);
    }

    // extract base include directories (source code roots)
    for (const auto* mod : usedModules)
        m_sourceRoots.push_back(mod->sourceRootPath);

    // return final validation flag
    return validDeps;
}

//--

bool SolutionGenerator::generateAutomaticCode(FileGenerator& fileGenerator)
{
    std::atomic<bool> valid = true;

    //#pragma omp parallel for - can't use in parallel as there are dependencies on file existence
    for (int i = 0; i < m_projects.size(); ++i)
    {
        auto* project = m_projects[i];
        if (!generateAutomaticCodeForProject(project, fileGenerator))
            valid = false;
    }

    return valid;
}

bool SolutionGenerator::generateAutomaticCodeForProject(SolutionProject* project, FileGenerator& fileGenerator)
{
    bool valid = true;

    // Google test framework files
    if (project->type == ProjectType::TestApplication)
    {
		// sources 
		static const char* gtestFiles[] = {
			"gtest-assertion-result.cc", "gtest-death-test.cc", "gtest-filepath.cc", "gtest-matchers.cc",
			"gtest-port.cc", "gtest-printers.cc", "gtest-test-part.cc", "gtest-typed-test.cc", "gtest.cc", "gtest-internal-inl.h"
		};

		// extract includes
		{
			fs::path fullPath;
			if (m_files.resolveDirectoryPath("tools/gtest/include", fullPath))
			{
                project->additionalIncludePaths.push_back(fullPath);
			}
            else
            {
                return false;
            }
		}

        // extract the source code files
        for (const auto* sourceFileName : gtestFiles)
        {
            const auto localPath = std::string("tools/gtest/src/") + sourceFileName;

            fs::path fullPath;
            if (m_files.resolveFilePath(localPath, fullPath))
            {
                auto* info = new SolutionProjectFile;
                info->type = ProjectFileType::CppSource;
                info->absolutePath = fullPath;
                info->filterPath = "_gtest";
                info->name = sourceFileName;
                project->files.push_back(info);
            }
            else
            {
                return false;
            }
        }
    }

    // generate reflection file
    {
        if (project->optionUseReflection)
        {
			const auto reflectionFilePath = (project->generatedPath / "reflection.cpp").make_preferred();

            // generate reflection statically
            if (m_config.staticBuild)
            {
                std::vector<fs::path> sourceFiles;

                for (const auto* file : project->files)
                    if (file->type == ProjectFileType::CppSource)
                        sourceFiles.push_back(file->absolutePath);

                ToolReflection tool;
                valid &= tool.runStatic(fileGenerator, sourceFiles, project->name, reflectionFilePath);
            }

			auto* info = new SolutionProjectFile;
			info->type = ProjectFileType::CppSource;
			info->absolutePath = reflectionFilePath;
			info->filterPath = "_generated";
			info->name = "reflection.cpp";
			project->files.push_back(info);

			project->localReflectionFile = reflectionFilePath;

            // DO NOT WRITE as it's written by the reflection tool
            //info->generatedFile = createFile(info->absolutePath);
            //valid &= generateProjectDefaultReflection(project, info->generatedFile->content);
        }
    }

    // libraries generate the glue file
    {
        auto localGlueHeader = m_sharedGlueFolder / project->name;
        localGlueHeader += "_glue.inl";

        auto* info = new SolutionProjectFile;
        info->absolutePath = localGlueHeader;
        info->type = ProjectFileType::CppHeader;
        info->filterPath = "_generated";
        info->name = project->name + "_glue.inl";
        project->files.push_back(info);

        project->localGlueHeader = info->absolutePath;

        auto generatedFile = fileGenerator.createFile(info->absolutePath);
        valid &= generateProjectGlueHeaderFile(project, generatedFile->content);
    }

    // embedded files
    {
        struct FileToEmbed
        {
            const SolutionProjectFile* original = nullptr;
            const SolutionProjectFile* embed = nullptr;
        };

        std::vector<FileToEmbed> embedFiles;
        auto oldFiles = project->files;
        for (const auto* file : oldFiles)
        {
            if (file->type == ProjectFileType::MediaFile)
            {
				auto embeddedMediaFilePath = (project->generatedPath / "media" / file->scanRelativePath).make_preferred();
                embeddedMediaFilePath += ".cxx";

				auto* info = new SolutionProjectFile;
				info->absolutePath = embeddedMediaFilePath;
                info->projectRelativePath = "generated\\" + file->scanRelativePath;
				info->type = ProjectFileType::CppSource;
				info->filterPath = "_packed_media";
                info->name = file->name + ".cxx";
				project->files.push_back(info);

                // for static builds generate the file now
                if (m_config.staticBuild)
                {
                    FileToEmbed fileInfo;
                    fileInfo.original = file;
                    fileInfo.embed = info;
                    embedFiles.push_back(fileInfo);
                }
            }
        }

        #pragma omp parallel for
        for (int i = 0; i < embedFiles.size(); ++i)
        {
            const auto& info = embedFiles[i];

            ToolEmbed tool;
            valid &= tool.writeFile(fileGenerator, info.original->absolutePath, project->name, info.original->scanRelativePath, info.embed->absolutePath);
        }
    }

    // generate precompiled root header
    {
        if (project->optionUsePrecompiledHeaders)
        {
            {
                auto* info = new SolutionProjectFile;
                info->absolutePath = project->generatedPath / "build.h";
                info->type = ProjectFileType::CppHeader;
                info->filterPath = "_generated";
                info->name = "build.h";
                project->files.push_back(info);
                project->localBuildHeader = info->absolutePath;

                auto generatedFile = fileGenerator.createFile(info->absolutePath);
                valid &= generateProjectBuildHeaderFile(project, generatedFile->content);
            }

            {
                auto* info = new SolutionProjectFile;
                info->absolutePath = project->generatedPath / "build.cpp";
                info->type = ProjectFileType::CppSource;
                info->filterPath = "_generated";
                info->name = "build.cpp";
                project->files.push_back(info);

				auto generatedFile = fileGenerator.createFile(info->absolutePath);
                valid &= generateProjectBuildSourceFile(project, generatedFile->content);
            }
        }
    }

    // reflection file


    // entry point
    if (project->type == ProjectType::Application)
    {
        if (project->optionGenerateMain)
        {
            auto* info = new SolutionProjectFile;
            info->absolutePath = project->generatedPath / "main.cpp";
            info->type = ProjectFileType::CppSource;
            info->filterPath = "_generated";
            info->name = "main.cpp";            
            project->files.push_back(info);

            auto generatedFile = fileGenerator.createFile(info->absolutePath);
            valid &= generateProjectAppMainSourceFile(project, generatedFile->content);
        }
    }
	else if (project->type == ProjectType::TestApplication)
	{
		auto* info = new SolutionProjectFile;
		info->absolutePath = project->generatedPath / "main.cpp";
		info->type = ProjectFileType::CppSource;
		info->filterPath = "_generated";
		info->name = "main.cpp";
		project->files.push_back(info);

        auto generatedFile = fileGenerator.createFile(info->absolutePath);
		valid &= generateProjectTestMainSourceFile(project, generatedFile->content);
	}

    // process additional file types
	for (auto* file : project->files)
	{
		if (file->type == ProjectFileType::Bison)
			valid &= processBisonFile(project, file);
	}

	// move build.cpp to the front of the file list
	for (auto it = project->files.begin(); it != project->files.end(); ++it)
	{
		auto* file = *it;
		if (file->name == "build.cpp" || file->name == "build.cxx")
		{
			project->files.erase(it);
			project->files.insert(project->files.begin(), file);
			break;
		}
	}

    // done
    return valid;
}

bool SolutionGenerator::generateProjectAppMainSourceFile(const SolutionProject* project, std::stringstream& f)
{
    writeln(f, "/***");
    writeln(f, "* Inferno Engine Static Lib Initialization Code");
    writeln(f, "* Auto generated, do not modify");
    writeln(f, "* Build system source code licensed under MIP license");
    writeln(f, "***/");
    writeln(f, "");

    writeln(f, "#include \"build.h\"");

    const auto hasSystem = HasDependency(project, "bm_core_system");
    const auto hasTasks = HasDependency(project, "bm_core_task");

    if (!project->appHeaderName.empty())
        writelnf(f, "#include \"%hs\"", project->appHeaderName.c_str());
    writelnf(f, "#include \"core/containers/include/commandLine.h\"");
    writeln(f, "");

    if (project->appHeaderName.empty())
    {
        writeln(f, "extern int bm_main(const bm::CommandLine& cmdLine);");
        writeln(f, "");
    }

    bool windowsCommandLine = false;
    if (m_config.platform == PlatformType::Windows)
    {
		writeln(f, "#include <Windows.h>");
		writeln(f, "");

        if (project->optionUseWindowSubsystem)
        {
			writeln(f, "int __stdcall wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {");
			windowsCommandLine = true;
        }
        else
        {
			writeln(f, "int main(int argc, char** argv) {");
        }
    }
    else
    {
        writeln(f, "int main(int argc, char** argv) {");
    }

    {
        writelnf(f, "    extern void InitModule_%hs(void*);", project->name.c_str());
        if (m_config.platform == PlatformType::Windows)
        {
            if (project->optionUseWindowSubsystem)
                writelnf(f, "    InitModule_%hs((void*)hInstance);", project->name.c_str());
            else
                writelnf(f, "    InitModule_%hs((void*)GetModuleInstance(NULL));", project->name.c_str());
        }
        else
        {
            writelnf(f, "    InitModule_%hs(nullptr);", project->name.c_str());
        }
        writeln(f, "");
    }


	writeln(f, "");
	writeln(f, "  int ret = 0;");
	writeln(f, "");

    // commandline
    writeln(f, "  bm::CommandLine commandLine;");
    if (windowsCommandLine)
        writeln(f, "  if (!commandLine.parse(pCmdLine, false))");
    else
        writeln(f, "  if (!commandLine.parse(argc, argv)) {");
    writeln(f, "    return 1;");
    writeln(f, "");

    // profiling init
    if (hasSystem)
    {
        writeln(f, "  bm::InitProfiling(commandLine);");
        writeln(f, "");
    }

    // task init
    if (hasTasks)
    {
        writeln(f, "  if (!bm::InitTaskThreads(commandLine)) {");
        writeln(f, "    bm::CloseProfiling();");
        writeln(f, "    return 1;");
        writeln(f, "  }");
        writeln(f, "");
    }

    // app init
    if (!project->appClassName.empty())
    {
        writelnf(f, "  %hs app;", project->appClassName.c_str());
        writelnf(f, "  if (app.init(commandLine)) {");
        writelnf(f, "    while (app.update()) {};");
        writeln (f, "  } else {" );
        writelnf(f, "    ret = 1;");
        writeln (f, "  }");
        writeln(f, "");
    }
    else
    {
        writelnf(f, "    ret = bm_main(commandLine);");
    }

    // close task system
	if (hasTasks)
	{
		writeln(f, "  bm::CloseTaskThreads();");
		writeln(f, "");
	}

    // close profiling
    if (hasSystem)
    {
        writeln(f, "  bm::CloseProfiling();");
        writeln(f, "");
    }

    // exit
	writeln(f, "  return ret;");
    writeln(f, "}");
    return true;
}

bool SolutionGenerator::generateProjectTestMainSourceFile(const SolutionProject* project, std::stringstream& f)
{
    writeln(f, "/***");
    writeln(f, "* Inferno Engine Static Lib Initialization Code");
    writeln(f, "* Auto generated, do not modify");
    writeln(f, "* Build system source code licensed under MIP license");
    writeln(f, "***/");
    writeln(f, "");

    writeln(f, "#include \"build.h\"");

    const auto hasTasks = HasDependency(project, "bm_core_task");
    const auto hasSystem = HasDependency(project, "bm_core_system");
    const auto hasContainers = HasDependency(project, "bm_core_containers");

    if (hasContainers)
        writelnf(f, "#include \"core/containers/include/commandLine.h\"");
    writeln(f, "");

    writeln(f, "#include \"gtest/gtest.h\"");
    writeln(f, "");

    writeln(f, "int main(int argc, char** argv) {");        

	writelnf(f, "    extern void InitModule_%hs(void*);", project->name.c_str());
    writelnf(f, "    InitModule_%hs(nullptr);", project->name.c_str());
	writeln(f, "");

    writeln(f, "");
    writeln(f, "  int ret = 0;");
    writeln(f, "");

    if (hasSystem)
    {
        writeln(f, "  bm::InitProfiling();");
        writeln(f, "");
    }

    if (hasContainers)
    {
        writeln(f, "  bm::CommandLine commandLine;");
        writeln(f, "  commandLine.parse(argc, argv);");
        writeln(f, "");

        if (hasTasks)
        {
            writeln(f, "  if (!bm::InitTaskThreads(commandLine)) {");
            writeln(f, "    bm::CloseProfiling();");
            writeln(f, "    return -1;");
            writeln(f, "  }");
            writeln(f, "");
        }

        {
            writeln(f, "  if (!commandLine.hasParam(\"interactive\")) {");
            writeln(f, "    #ifdef PLATFORM_LINUX");
            writeln(f, "      signal(SIGPIPE, SIG_IGN);");
            writeln(f, "    #endif");
            writeln(f, "");
            writeln(f, "    testing::InitGoogleTest(&argc, argv);");
            writeln(f, "    ret = RUN_ALL_TESTS();");
            writeln(f, "  }");
            writeln(f, "");
        }

        if (hasTasks)
        {
            writeln(f, "  bm::CloseTaskThreads();");
            writeln(f, "");
        }
    }
    else
    {
        writeln(f, "  testing::InitGoogleTest(&argc, argv);");
        writeln(f, "  ret = RUN_ALL_TESTS();");
        writeln(f, "");
    }

    if (hasSystem)
    {
        writeln(f, "  bm::CloseProfiling();");
        writeln(f, "");
    }

    writeln(f, "  return ret;");
    writeln(f, "}");

    return true;
}

static void CollectDirectlyStaticallyLinkedProjects(const SolutionProject* project, std::unordered_set< const SolutionProject*>& outVisited, std::vector<const SolutionProject*>& outStaticallyLinked, int depth)
{
    // visit only once
    if (!outVisited.insert(project).second)
        return;

    // this is a dynamically linked stuff
    if (depth && project->type != ProjectType::StaticLibrary)
        return;

    // collect
    outStaticallyLinked.push_back(project);

    // check local dependencies
    for (const auto* dep : project->directDependencies)
        CollectDirectlyStaticallyLinkedProjects(dep, outVisited, outStaticallyLinked, depth + 1);
}

bool SolutionGenerator::generateProjectBuildSourceFile(const SolutionProject* project, std::stringstream& f)
{
    writeln(f, "/***");
    writeln(f, "* Onion Precompiled Header");
    writeln(f, "* Auto generated, do not modify");
    writeln(f, "***/");
    writeln(f, "");

    writeln(f, "#include \"build.h\"");
    writeln(f, "");

    // collect all projects that are STATICALLY linked to this project
    std::vector<const SolutionProject*> staticallyLinkedProjects;
    {
        std::unordered_set< const SolutionProject*> visited;
        CollectDirectlyStaticallyLinkedProjects(project, visited, staticallyLinkedProjects, 0);
    }

    // determine if project requires static initialization (the apps and console apps require that)
    // then pull in the library linkage, for apps we pull much more crap
    if (m_config.generator == GeneratorType::VisualStudio19 || m_config.generator == GeneratorType::VisualStudio22)
    {
        // direct third party libraries used by this project
        std::unordered_set<const ExternalLibraryManifest*> exportedLibs;
        for (const auto* proj : staticallyLinkedProjects)
            for (const auto* dep : proj->libraryDependencies)
                exportedLibs.insert(dep);

        if (!exportedLibs.empty())
        {
            writeln(f, "// Libraries");
            for (const auto* dep : exportedLibs)
            {
                for (const auto& linkPath : dep->libraryFiles)
                {
                    std::stringstream ss;
                    ss << linkPath;
                    writelnf(f, "#pragma comment( lib, %s )", ss.str().c_str());
                }
            }
            writeln(f, "");
        }
    }

    // reflection initialization methods
    if (project->optionUseReflection)
    {
        writeln(f, "// Initialization for reflection");
        writelnf(f, "extern void InitializeReflection_%hs();", project->name.c_str());
        writeln(f, "");
    }
    else
    {
        writeln(f, "// Dummy initialization for reflection");
        writelnf(f, "void InitializeReflection_%hs() {}", project->name.c_str());
        writeln(f, "");
    }

    // embedded files initialization
    {
        writeln(f, "// Dummy initialization for embedded files");
        writelnf(f, "void InitializeEmbeddedFiles_%hs() {}", project->name.c_str());
        writeln(f, "");
    }

    // module handle
    if (project->type != ProjectType::StaticLibrary)
    {
        writeln(f, "// Local shared library handle");
        writeln(f, "void* GModuleHandle = nullptr;");
        writeln(f, "");
    }

    // local static module initialization
    {
        writeln(f, "// Project initialization code");
        writelnf(f, "void InitModule_%hs(void* handle) {", project->name.c_str());
        if (project->type != ProjectType::StaticLibrary)
            writelnf(f, "    GModuleHandle = handle;");
        else
            writelnf(f, "    (void)handle;");

        // initialize statically linked modules
        if (project->type != ProjectType::StaticLibrary)
        {
		    for (const auto* dep : staticallyLinkedProjects)
		    {
                if (dep != project && !dep->optionDetached)
                {
                    writelnf(f, "    extern void InitModule_%s(void*);", dep->name.c_str());
                    writelnf(f, "    InitModule_%s(handle);", dep->name.c_str());
                }
			}
		}

        // initialize self
        if (project->optionUseStaticInit) {
            writelnf(f, "    bm::modules::TModuleInitializationFunc initFunc = []() { InitializeReflection_%hs(); InitializeTests_%hs(); InitializeEmbeddedFiles_%hs(); };", project->name.c_str(), project->name.c_str(), project->name.c_str());
            writelnf(f, "    bm::modules::RegisterModule(\"%hs\", __DATE__, __TIME__, _MSC_FULL_VER, initFunc, handle);", project->name.c_str());

            if (project->type == ProjectType::Application || project->type == ProjectType::TestApplication)
                writelnf(f, "    bm::modules::InitializePendingModules();");
        }
        writeln(f, "}");
        writeln(f, "");
    }

    // dll main
    if (project->type == ProjectType::SharedLibrary && m_config.platform == PlatformType::Windows)
    {
        writeln(f, "unsigned char __stdcall DllMain(void* moduleInstance, unsigned long nReason, void*) {");
        writelnf(f, "    if (nReason == 1) InitModule_%hs(moduleInstance);", project->name.c_str());
        writeln(f, "    return 1;");
        writeln(f, "}");
        writeln(f, "");
    }

    /*if (project->hasEmbeddedFiles)
    {
        writelnf(f, "void InitializeEmbeddedFiles_%hs() {", project->mergedName.c_str());
        for (const auto* file : project->originalProject->files)
        {
            if (file->type == ProjectFileType::MediaScript && file->originalProject)
            {
                std::string symbolName = ToUpper(file->originalProject->mergedName) + "_" + ToUpper(PartBefore(file->name, "."));
                std::string functionName = "RegisterEmbeddedFiles_" + symbolName;
                writelnf(f, "  extern void %hs();", functionName.c_str());
                writelnf(f, "  %hs();", functionName.c_str());
            }
        }
        writeln(f, "}");
        writeln(f, "");
    }*/


    return true;
}

bool SolutionGenerator::generateProjectGlueHeaderFile(const SolutionProject* project, std::stringstream& f)
{
    const auto upperName = ToUpper(project->name);
    const auto macroName = upperName + "_GLUE";
    const auto apiName = upperName + "_API";
    const auto exportsMacroName = upperName + "_EXPORTS";

    writeln(f, "/***");
    writeln(f, "* Onion Precompiled Header");
    writeln(f, "* Auto generated, do not modify - add stuff to public.h instead");
    writeln(f, "***/");
    writeln(f, "");
    writeln(f, "#pragma once");
    writeln(f, "");

    // if compiling as test app include the macro first to allow some headers to reconfigure for tests
    if (project->type == ProjectType::TestApplication)
    {
        writeln(f, "// We are running tests");
        writeln(f, "#define WITH_GTEST");
        writeln(f, "");
    }

    // Library interface
    if (m_config.platform == PlatformType::Windows)
    {
        if (project->type == ProjectType::SharedLibrary)
        {
            if (project->optionDetached)
            {
                writeln(f, "// Detached shared library API macro");
                writeln(f, "#ifdef " + exportsMacroName);
                writelnf(f, "    #define %s __declspec( dllexport )", apiName.c_str());
                writeln(f, "#else");
                writelnf(f, "    #define %s", apiName.c_str());
                writeln(f, "#endif");
                writeln(f, "");
            }
            else
            {
                writeln(f, "// Shared library API macro");
                writeln(f, "#ifdef " + exportsMacroName);
                writelnf(f, "    #define %s __declspec( dllexport )", apiName.c_str());
                writeln(f, "#else");
                writelnf(f, "    #define %s __declspec( dllimport )", apiName.c_str());
                writeln(f, "#endif");
                writeln(f, "");
            }
        }
        else if (project->type == ProjectType::StaticLibrary)
        {
            writeln(f, "// Static library dummy API macro");
            writeln(f, "#define " + apiName);
            writeln(f, "");
        }
    }
    else
    {
        writeln(f, "// Library dummy API macro");
		writeln(f, "#define " + apiName);
		writeln(f, "");
    }

	//if (project->type == ProjectType::Application || project->type == ProjectType::TestApplication)
	if (!project->allDependencies.empty())
	{
		writeln(f, "// Glue header from project dependencies");

		for (const auto* dep : project->allDependencies)
		{
			if (!dep->localGlueHeader.empty())
			{
				if (dep->type == ProjectType::SharedLibrary || dep->type == ProjectType::StaticLibrary)
				{
					writeln(f, "#include \"" + dep->localGlueHeader.u8string() + "\"");
				}
                else
                {
                    writelnf(f, "// Project %hs is not a library", dep->name.c_str());
                }
			}
            else
            {
                writelnf(f, "// Project %hs has no glue header", dep->name.c_str());
            }
		}

		writeln(f, "");
	}

	// local public header
	if (!project->localPublicHeader.empty())
	{
		writeln(f, "// Local public header");
		writeln(f, "#include \"" + project->localPublicHeader.u8string() + "\"");
		writeln(f, "");
	}    	

    return true;
}

bool SolutionGenerator::generateProjectBuildHeaderFile(const SolutionProject* project, std::stringstream& f)
{
	const auto upperName = ToUpper(project->name);
	const auto macroName = upperName + "_GLUE";
	const auto apiName = upperName + "_API";
	const auto exportsMacroName = upperName + "_EXPORTS";

    writeln(f, "/***");
    writeln(f, "* Onion Precompiled Header");
    writeln(f, "* Auto generated, do not modify - add stuff to public.h instead");
    writeln(f, "***/");
    writeln(f, "");
    writeln(f, "#pragma once");
    writeln(f, "");
    
    // include local glue header
    if (!project->localGlueHeader.empty())
	{
		writeln(f, "// Glue file");
        writeln(f, "#include \"" + project->localGlueHeader.u8string() + "\"");
        writeln(f, "");
    }

	// local private header
	if (!project->localPrivateHeader.empty())
	{
		writeln(f, "// Local private header");
		writeln(f, "#include \"" + project->localPrivateHeader.u8string() + "\"");
		writeln(f, "");
	}

    // Google Test Suite
    if (project->type == ProjectType::TestApplication)
    {
        writeln(f, "// Google Test Suite");
        writeln(f, "#include \"gtest/gtest.h\"");
		writeln(f, "");
    }

    return true;
}

bool SolutionGenerator::generateSolutionReflectionFileList(std::stringstream& f)
{
    /*writeln(f, NameEnumOption(m_config.platform));
    writeln(f, NameEnumOption(m_config.build));

    uint32_t numReflectedFiles = 0;
    uint32_t numTotalFiles = 0;
    for (const auto* proj : m_projects)
    {
        if (!proj->originalProject)
            continue;

        numTotalFiles += (uint32_t)proj->files.size();

        if (proj->hasReflection)
        {
            writelnf(f, "PROJECT");
            writelnf(f, "%hs", proj->mergedName.c_str());
            writelnf(f, "%hs", proj->localReflectionFile.u8string().c_str());

            for (const auto* file : proj->files)
            {
                if (file->type == ProjectFileType::CppSource)
                {
                    writelnf(f, "%hs", file->absolutePath.u8string().c_str());
                    numReflectedFiles += 1;
                }
            }
        }
    }

    std::cout << "Found " << numReflectedFiles << " source code files for reflection of (" << numTotalFiles << " total)\n";*/
    return true;
}

bool SolutionGenerator::generateSolutionEmbeddFileList(std::stringstream& f)
{
	/*writeln(f, NameEnumOption(config.platform));

	uint32_t numMediaFiles = 0;
	for (const auto* proj : projects)
	{
		if (!proj->hasEmbeddedFiles)
			continue;

		for (const auto* file : proj->files)
		{
			if (file->type == ProjectFileType::MediaScript)
			{
				writelnf(f, "%hs", proj->mergedName.c_str());

				writelnf(f, "%hs", file->absolutePath.u8string().c_str());

				const auto compiledMediaFileName = std::string("EmbeddedMedia_") + proj->mergedName + "_" + std::string(PartBefore(file->name, ".")) + "_data.cpp";
				const auto compiledMediaFilePath = proj->generatedPath / compiledMediaFileName;
				writelnf(f, "%hs", compiledMediaFilePath.u8string().c_str());

				numMediaFiles += 1;
			}
		}
	}

	std::cout << "Found " << numMediaFiles << " embedded media build files\n";*/
	return true;
}

//--

bool SolutionGenerator::processBisonFile(SolutionProject* project, const SolutionProjectFile* file)
{
    const auto coreName = PartBefore(file->name, ".");

    std::string parserFileName = std::string(coreName) + "_Parser.cpp";
    std::string symbolsFileName = std::string(coreName) + "_Symbols.h";
    std::string reportFileName = std::string(coreName) + "_Report.txt";

    fs::path parserFile = project->generatedPath / parserFileName;
    fs::path symbolsFile = project->generatedPath / symbolsFileName;
    fs::path reportPath = project->generatedPath / reportFileName;

    parserFile = parserFile.make_preferred();
    symbolsFile = symbolsFile.make_preferred();
    reportPath = reportPath.make_preferred();

    // static build generates the file directly
    if (m_config.staticBuild)
    {
#ifdef _WIN32
		fs::path toolPath;
        if (!m_files.resolveDirectoryPath("tools/bison/windows", toolPath))
            return false;

		const auto executablePath = (toolPath / "win_bison.exe").make_preferred();
#else
		fs::path toolPath;
		if (!m_files.resolveDirectoryPath("tools/bison/linux", toolPath))
			return false;

		const auto executablePath = (toolPath / "run_bison.sh").make_preferred();
#endif

        if (IsFileSourceNewer(file->absolutePath, parserFile))
        {
            std::error_code ec;
            if (!fs::is_directory(project->generatedPath))
            {
                if (!fs::create_directories(project->generatedPath, ec))
                {
                    std::cout << "BISON tool failed because output directory \"" << project->generatedPath << "\" can't be created: " << ec << "\n";
                    return false;
                }
            }

            std::stringstream params;
            params << executablePath.u8string() << " ";
            params << "\"" << file->absolutePath.u8string() << "\" ";
            params << "-o\"" << parserFile.u8string() << "\" ";
            params << "--defines=\"" << symbolsFile.u8string() << "\" ";
            params << "--report-file=\"" << reportPath.u8string() << "\" ";
            params << "--verbose";

            const auto activeDir = fs::current_path();
            const auto bisonDir = executablePath.parent_path().make_preferred();
            std::error_code er;
            fs::current_path(bisonDir, er);
            if (er)
            {
                std::cout << "BISON tool failed to switch active directory to '" << bisonDir << "'\n";
                return false;
            }

            auto code = std::system(params.str().c_str());

            fs::current_path(activeDir);

            if (code != 0)
            {
                std::cout << "BISON tool failed with exit code " << code << "\n";
                return false;
            }
            else
            {
                std::cout << "BISON tool finished and generated '" << parserFile << "'\n";
            }
        }
        else
        {
            std::cout << "BISON tool skipped because '" << parserFile << "' is up to date\n";
        }
    }

    {
        auto* generatedFile = new SolutionProjectFile();
        generatedFile->absolutePath = parserFile;
        generatedFile->name = parserFileName;
        generatedFile->filterPath = "_generated";
        generatedFile->type = ProjectFileType::CppSource;
        project->files.push_back(generatedFile);
    }

    {
        auto* generatedFile = new SolutionProjectFile();
        generatedFile->absolutePath = symbolsFile;
        generatedFile->name = symbolsFileName;
        generatedFile->filterPath = "_generated";
        generatedFile->type = ProjectFileType::CppHeader;
        project->files.push_back(generatedFile);
    }

    return true;
}

//--
