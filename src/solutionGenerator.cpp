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

    m_sharedGlueFolder = config.derivedSolutionPathBase / "generated" / "_shared";
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
            LogInfo() << "Recursive project dependencies found when project '" << p->name << "' was encountered second time";
            for (const auto* proj : stack)
                LogInfo() << "  Reachable from '" << proj->name << "'";
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
    group->assignedVSGuid = GuidFromText("GROUP" + group->mergedName);
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
    // cache folder
    {
        SolutionDataFolder data;
        data.mountPath = "/Cache/";
        data.dataPath = (m_config.cachePath / "internal").make_preferred();
        m_dataFolders.push_back(data);
    }

    // create projects
    std::unordered_set<const ModuleManifest*> usedModules;
    std::unordered_map<SolutionProject*, const ProjectInfo*> projectMap;
    std::unordered_map<const ProjectInfo*, SolutionProject*> projectRevMap;
    for (const auto* proj : collection.projects())
    {
        // create wrapper
        auto* generatorProject = new SolutionProject;
        generatorProject->type = proj->manifest->type;
        generatorProject->name = ReplaceAll(proj->name, "/", "_");
        generatorProject->localGroupName = proj->manifest->localGroupName;
		generatorProject->globalNamespace = proj->globalNamespace;

        // paths
		generatorProject->rootPath = proj->rootPath;
        generatorProject->generatedPath = m_config.derivedSolutionPathBase / "generated" / proj->name;
        generatorProject->projectPath = m_config.derivedSolutionPathBase / "projects" / proj->name;
        generatorProject->outputPath = m_config.derivedSolutionPathBase / "output" / proj->name;

        // options
        generatorProject->optionUsePrecompiledHeaders = proj->manifest->optionUsePrecompiledHeaders;
        generatorProject->optionGenerateMain = proj->manifest->optionGenerateMain;
		generatorProject->optionUsePreMain = proj->manifest->optionHasPreMain;
        generatorProject->optionUseStaticInit = proj->manifest->optionUseStaticInit;
        generatorProject->optionUseWindowSubsystem = (proj->manifest->optionSubstem == ProjectAppSubsystem::Windows);
        generatorProject->optionWarningLevel = proj->manifest->optionWarningLevel;
        generatorProject->optionUseExceptions = proj->manifest->optionUseExceptions;
        generatorProject->optionUseGtest = (proj->manifest->optionTestFramework == ProjectTestFramework::GTest);
        generatorProject->optionDetached = proj->manifest->optionDetached;
        generatorProject->optionExportApplicataion = proj->manifest->optionExportApplicataion;
        generatorProject->optionUseEmbeddedFiles = false;
        generatorProject->optionLegacy = proj->manifest->optionLegacy;
        generatorProject->optionThirdParty = proj->manifest->optionThirdParty;
        generatorProject->appHeaderName = proj->manifest->appHeaderName;
        generatorProject->appClassName = proj->manifest->appClassName;
        generatorProject->appDisableLogOnStart = proj->manifest->appDisableLogOnStart;
        generatorProject->appSystemClasses = proj->manifest->appSystemClasses;
        generatorProject->assignedVSGuid = proj->manifest->guid;
        generatorProject->libraryDependencies = proj->resolvedLibraryDependencies;
        generatorProject->localDefines = proj->manifest->localDefines;
        generatorProject->globalDefines = proj->manifest->globalDefines;
        generatorProject->legacySourceDirectories = proj->manifest->legacySourceDirectories;
        generatorProject->optionHasInit = proj->manifest->optionHasInit;
        generatorProject->optionHasPreInit = proj->manifest->optionHasPreInit;
        generatorProject->thirdPartySharedLocalBuildDefine = proj->manifest->thirdPartySharedLocalBuildDefine;
        generatorProject->thirdPartySharedGlobalExportDefine = proj->manifest->thirdPartySharedGlobalExportDefine;
        generatorProject->optionThirdParty = proj->manifest->optionThirdParty;
        generatorProject->optionAdvancedInstructionSet = proj->manifest->optionAdvancedInstructionSet;
        generatorProject->optionFrozen = proj->manifest->optionFrozen;
		generatorProject->frozenLibraryFiles = proj->manifest->frozenLibraryFiles;

        // if we have the "init.cpp" file enable the init and pre-init automatically
        for (const auto* file : proj->files)
        {
            if (file->name == "init.cpp")
            {
                generatorProject->optionHasInit = true;
				generatorProject->optionHasPreInit = true;
            }
        }

        // include paths
        for (const auto& path : proj->manifest->localIncludePaths)
            generatorProject->additionalIncludePaths.push_back(path);
		for (const auto& path : proj->manifest->exportedIncludePaths) {
            LogInfo() << path;
            generatorProject->exportedIncludePaths.push_back(path);
        }

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

            // media file ?
            if (file->type == ProjectFileType::MediaFile)
                generatorProject->optionUseEmbeddedFiles = true;

            generatorProject->files.push_back(info);
        }

        // assign project to solution group
        {
            const bool isLocalProject = proj->parentModule ? proj->parentModule->local : true;
            const auto rootGroupName = isLocalProject ? (proj->manifest ? proj->manifest->solutionGroupName : "") : "external";
            const auto rootGroup = rootGroupName.empty() ? m_rootGroup : createGroup(rootGroupName);

            generatorProject->group = createGroup(generatorProject->localGroupName, rootGroup);
            //LogInfo() << "Project '" << proj->name << "' (GUID: " << generatorProject->assignedVSGuid << ") assigned to group '" << generatorProject->localGroupName 
                //<< "' in root group '" << rootGroup->mergedName << "', final group: '" << generatorProject->group->mergedName << "', GUID: " << generatorProject->group->assignedVSGuid;

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

    // build "global" dependency graph for the whole solution
    // this represents overall project dependencies
    {
		OrderedGraphBuilder graph;

        for (auto* proj : m_projects)
        {
            std::vector<SolutionProject*> stack;
            stack.push_back(proj);

            for (auto* dep : proj->directDependencies)
                validDeps &= graph.insertProject(dep, 1, stack);
        }

        // use the ordered list as project list, so we always have projects from most basic to most complicated
		graph.extractOrderedList(this->m_projects);

        for (const auto* proj : m_projects)
            LogInfo() << proj->name;
    }

	// disable static initialization on projects that don't use the core
    for (auto* proj : m_projects)
    {
        if (proj->optionUseReflection && !HasDependency(proj, "core_object") && proj->name != "core_object")
            proj->optionUseReflection = false;

		if (proj->optionUseStaticInit && !HasDependency(proj, "core_system") && proj->name != "core_system")
			proj->optionUseStaticInit = false;

		if (proj->optionUseEmbeddedFiles && !HasDependency(proj, "core_file"))
			proj->optionUseEmbeddedFiles = false;
    }

    // create the _rtti_generator project and make everybody a dependency
    {
        if (auto* coreObjectsProject = findProject("core_object"))
        {
			// create wrapper
			auto* generatorProject = new SolutionProject;
			generatorProject->type = ProjectType::RttiGenerator;
			generatorProject->name = "_rtti_generator";

			// paths
			generatorProject->rootPath = fs::path(); // not coming from source
			generatorProject->generatedPath = m_config.derivedSolutionPathBase / "generated" / generatorProject->name;
			generatorProject->projectPath = m_config.derivedSolutionPathBase / "projects" / generatorProject->name;
			generatorProject->outputPath = m_config.derivedSolutionPathBase / "output" / generatorProject->name;

			// options
			generatorProject->optionUsePrecompiledHeaders = false;
			generatorProject->optionGenerateMain = false;
            generatorProject->optionUsePreMain = false;
			generatorProject->optionUseStaticInit = false;
			generatorProject->optionUseWindowSubsystem = false;
			generatorProject->optionWarningLevel = false;
			generatorProject->optionUseExceptions = false;
			generatorProject->optionDetached = false;
			generatorProject->optionExportApplicataion = false;
			generatorProject->optionUseEmbeddedFiles = false;
            generatorProject->optionUseReflection = false;
            generatorProject->assignedVSGuid = GuidFromText("_rtti_generator");

            // make sure all projects depend on the RTTI generator
            for (auto* project : m_projects)
                project->directDependencies.push_back(generatorProject);

			// register in solution and map
			m_projects.push_back(generatorProject);
			m_projectNameMap[generatorProject->name] = generatorProject;

            // add to the root group
            m_rootGroup->projects.push_back(generatorProject);
        }
    }

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
        for (const auto& path : mod->globalIncludePaths)
            PushBackUnique(m_sourceRoots, path);

    // extract data folders from used modules
    std::unordered_set<std::string> mountPaths;
    for (const auto* mod : usedModules)
    {
        for (const auto& entry : mod->moduleData)
        {
            const auto path = ToLower(entry.mountPath);
            if (!mountPaths.insert(path).second)
            {
                LogError() << "Duplicated entry for mounting data to '" << entry.mountPath << "'";
                validDeps = false;
                continue;
            }

            /*if (m_config.flagShipmentBuild && !entry.published)
            {
				LogWarning() << "Non-publishable data at '" << entry.mountPath << "' will not be mounted";
				validDeps = false;
				continue;
            }*/

            SolutionDataFolder data;
            data.mountPath = entry.mountPath;
            data.dataPath = entry.sourcePath;
            m_dataFolders.push_back(data);
        }
    }

    // return final validation flag
    return validDeps;
}

//--

bool SolutionGenerator::generateAutomaticCode(FileGenerator& fileGenerator)
{
    std::atomic<bool> valid = true;

	// TODO: fix
    //#pragma omp parallel for - can't use in parallel as there are dependencies on file existence
    for (int i = 0; i < m_projects.size(); ++i)
    {
        auto* project = m_projects[i];
        if (!generateAutomaticCodeForProject(project, fileGenerator))
        {
            LogError() << "Failed to generate automatic code for project '" << project->name << "'";
            valid = false;
        }
    }

	const char* ConfigurationNames[] = {
    	"debug", "checked", "release", "profile", "final"
	};

    // generate the data mapping file
    for (const auto* configName : ConfigurationNames)
    {
        const auto binaryPath = (m_config.derivedBinaryPathBase / configName).make_preferred();
        const auto fstabFilePath = (binaryPath / "fstab.cfg").make_preferred();

		auto generatedFile = fileGenerator.createFile(fstabFilePath);
        if (!generateSolutionFstabFile(binaryPath, generatedFile->content))
            valid = false;
    }

    return valid;
}

bool SolutionGenerator::generateAutomaticCodeForProject(SolutionProject* project, FileGenerator& fileGenerator)
{
    bool valid = true;

    // HACK
    if (project->name == "_rtti_generator" && m_config.platform == PlatformType::Prospero)// || (project->type == ProjectType::HeaderLibrary && m_config.platform == PlatformType::Prospero))
    {
		auto info = new SolutionProjectFile();
        info->absolutePath = (project->generatedPath / "dummy.cpp").make_preferred();
        info->name = "dummy.cpp";
        info->filterPath = "_generated";
		info->type = ProjectFileType::CppSource;
        project->files.push_back(info);

		auto generatedFile = fileGenerator.createFile(info->absolutePath);
        auto& f = generatedFile->content;
        writelnf(f, "int dummy_%hs() { return 0; }", project->name.c_str());
    }

	// Third party libraries don't generate code
	if (project->optionThirdParty)
		return true;

	// Header only project's don't generate code either
	if (project->type == ProjectType::HeaderLibrary)
		return true;

    // Google test framework files
    if (project->type == ProjectType::TestApplication)
    {
        // GTest framework
        if (project->optionUseGtest)
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
                    LogError() << "Failed to extract GoogleTest framework needed for project '" << project->name << "'";
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
                    LogError() << "Failed to extract GoogleTest framework needed for project '" << project->name << "'";
                    return false;
                }
            }
        }
        else
        {
			// sources 
			static const char* gtestFiles[] = {
				"catch2.cpp"
            };

			// extract includes
			{
				fs::path fullPath;
				if (m_files.resolveDirectoryPath("tools/catch2/include", fullPath))
				{
					project->additionalIncludePaths.push_back(fullPath);
				}
				else
				{
					LogError() << "Failed to extract GoogleTest framework needed for project '" << project->name << "'";
					return false;
				}
			}

			// extract the source code files
			for (const auto* sourceFileName : gtestFiles)
			{
				const auto localPath = std::string("tools/catch2/src/") + sourceFileName;

				fs::path fullPath;
				if (m_files.resolveFilePath(localPath, fullPath))
				{
					auto* info = new SolutionProjectFile;
					info->type = ProjectFileType::CppSource;
					info->absolutePath = fullPath;
					info->filterPath = "_catch2";
					info->name = sourceFileName;
					project->files.push_back(info);
				}
				else
				{
					LogError() << "Failed to extract Catch2 framework needed for project '" << project->name << "'";
					return false;
				}
			}
        }
    }

    // generate reflection file
    {
#if 0
		if (project->optionUseReflection)
		{
			const auto reflectionFilePath = (project->generatedPath / "reflection.txt").make_preferred();

			auto* info = new SolutionProjectFile;
			info->type = ProjectFileType::TextFile;
			info->absolutePath = reflectionFilePath;
			info->filterPath = "_generated";
			info->name = "reflection.txt";
			project->files.push_back(info);

			auto generatedFile = fileGenerator.createFile(info->absolutePath);
			for (const auto* file : project->files)
				if (file->type == ProjectFileType::CppSource && file->name != "reflection.cpp")
					writelnf(generatedFile->content, "%s", file->absolutePath.u8string().c_str());
		}
#endif

        if (project->optionUseReflection)
        {
			const auto reflectionFilePath = (project->generatedPath / "reflection.cpp").make_preferred();

            // generate reflection statically
            if (m_config.flagStaticBuild)
            {
                std::vector<fs::path> sourceFiles;

                for (const auto* file : project->files)
                    if (file->type == ProjectFileType::CppSource)
                        sourceFiles.push_back(file->absolutePath);

                ToolReflection tool;
                if (!tool.runStatic(fileGenerator, sourceFiles, project->name, project->globalNamespace, project->appSystemClasses, reflectionFilePath))
                {
                    LogError() << "Failed to generate static reflection for project '" << project->name << "'";
                    valid = false;
                }
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
        if (!generateProjectGlueHeaderFile(project, generatedFile->content))
        {
            LogError() << "Failed to generate glue header file for project '" << project->name << "'";
            valid = false;
        }
    }

    // project runner for platforms that don't emit executables
    if (m_config.platform == PlatformType::Wasm && (project->type == ProjectType::Application || project->type == ProjectType::TestApplication))
    {
		const char* ConfigurationNames[] = {
    		"debug", "checked", "release", "profile", "final"
		};

        for (const auto* configName : ConfigurationNames)
        {
            auto runDirectory = m_config.derivedBinaryPathBase / configName;
            auto localGlueHeader = runDirectory / project->name;
            localGlueHeader += "_run.bat";

            auto* info = new SolutionProjectFile;
            info->absolutePath = localGlueHeader;
            info->type = ProjectFileType::Unknown;
            info->filterPath = "_generated";
            info->name = project->name + "_run.bat";
            project->files.push_back(info);

            auto generatedFile = fileGenerator.createFile(info->absolutePath);
            if (!generateProjectHostingBatchFile(project, generatedFile->content, runDirectory))
            {
                LogError() << "Failed to generate glue header file for project '" << project->name << "'";
                valid = false;
            }
        }
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
                if (m_config.flagStaticBuild)
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
            if (!tool.writeFile(fileGenerator, info.original->absolutePath, project->name, info.original->scanRelativePath, info.embed->absolutePath))
            {
                LogError() << "Failed to write embedded file '" << info.original->scanRelativePath << "' in project '" << project->name << "'";
                valid = false;
            }
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
                if (!generateProjectBuildHeaderFile(project, generatedFile->content))
                {
                    LogError() << "Failed to generate build.h for project '" << project->name << "'";
                    valid = false;
                }
            }

            {
                auto* info = new SolutionProjectFile;
                info->absolutePath = project->generatedPath / "build.cpp";
                info->type = ProjectFileType::CppSource;
                info->filterPath = "_generated";
                info->name = "build.cpp";
                project->files.push_back(info);

				auto generatedFile = fileGenerator.createFile(info->absolutePath);
                if (!generateProjectBuildSourceFile(project, generatedFile->content))
                {
					LogError() << "Failed to generate build.cpp for project '" << project->name << "'";
					valid = false;
                }
            }

			{
				auto* info = new SolutionProjectFile;
				info->absolutePath = project->generatedPath / "module.cpp";
				info->type = ProjectFileType::CppSource;
				info->filterPath = "_generated";
				info->name = "module.cpp";
				project->files.push_back(info);

				auto generatedFile = fileGenerator.createFile(info->absolutePath);
				if (!generateProjectModuleSourceFile(project, generatedFile->content))
				{
					LogError() << "Failed to generate build.cpp for project '" << project->name << "'";
					valid = false;
				}
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
            if (!generateProjectAppMainSourceFile(project, generatedFile->content))
			{
				LogError() << "Failed to generate main.cpp for project '" << project->name << "'";
				valid = false;
			}
        }
    }
	else if (project->type == ProjectType::TestApplication)
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
            valid &= generateProjectTestMainSourceFile(project, generatedFile->content);
        }
	}

    // process additional file types
	for (auto* file : project->files)
	{
        if (file->type == ProjectFileType::Bison)
        {
            if (!processBisonFile(project, file))
            {
                LogError() << "Failed to process BISON file '" << file->scanRelativePath << "' in project '" << project->name << "'";
                valid = false;
            }
        }
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
    writeln(f, "* Engine Static Lib Initialization Code");
    writeln(f, "* Auto generated, do not modify");
    writeln(f, "* Build system source code licensed under MIP license");
    writeln(f, "***/");
    writeln(f, "");

    writeln(f, "#include \"build.h\"");

    const auto hasSystem = HasDependency(project, "core_system") || (project->name == "core_system");
	const auto hasPreMain = project->optionUsePreMain;

    if (!project->appHeaderName.empty())
        writelnf(f, "#include \"%hs\"", project->appHeaderName.c_str());
    writeln(f, "");

    if (m_config.platform == PlatformType::Windows)
    {
        writeln(f, "extern \"C\" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 711; }");
        writeln(f, "extern \"C\" { __declspec(dllexport) extern const char* D3D12SDKPath = u8\".\\\\\"; }");
        writeln(f, "");
    }

    if (project->appHeaderName.empty())
    {
        writelnf(f, "extern int %hs_main(int argc, char** argv);", project->globalNamespace.c_str(), project->globalNamespace.c_str());
        writeln(f, "");
    }

    if (m_config.platform == PlatformType::Windows && project->optionUseWindowSubsystem)
    {
        writeln(f, "#include <Windows.h>");
        writeln(f, "");

        writeln(f, "int __stdcall wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {");
    }
    else
    {
        writeln(f, "int main(int argc, char** argv) {");
    }

    if (!project->appDisableLogOnStart)
    {
        writelnf(f, "    %hs::Log::EnableOutput(true);", project->globalNamespace.c_str());
        writelnf(f, "    %hs::Log::EnableDetails(true);", project->globalNamespace.c_str());
    }

    {
        writelnf(f, "    extern void InitModule_%hs(void*);", project->name.c_str());
        if (m_config.platform == PlatformType::Windows)
        {
            if (project->optionUseWindowSubsystem)
                writelnf(f, "    InitModule_%hs((void*)hInstance);", project->name.c_str());
            else
                writelnf(f, "    InitModule_%hs((void*)GetModuleHandle(NULL));", project->name.c_str());
        }
        else
        {
            writelnf(f, "    InitModule_%hs(nullptr);", project->name.c_str());
        }
        writeln(f, "");

        writelnf(f, "    if (!%hs::modules::HasAllModulesInitialize()) {", project->globalNamespace.c_str());
        writeln(f, "      TRACE_ERROR(\"No all required modules were initialized, application cannot start\");");
		if (project->optionUseWindowSubsystem)
            writeln(f, "      MessageBoxA(NULL, \"No all required modules (DLLs) were initialized, application cannot start.\", \"Startup error\", MB_ICONERROR | MB_TASKMODAL);");
        writeln(f, "      return 5;");
        writeln(f, "    }");
        writeln(f, "");
    }


	writeln(f, "");

    // app init
    if (!project->appClassName.empty())
    {
        writelnf(f, "    %hs app;", project->appClassName.c_str());

        if (m_config.platform == PlatformType::Windows && project->optionUseWindowSubsystem)
            writelnf(f, "    return platform_main(__argc, __argv, app);");
        else
           writelnf(f, "    return platform_main(argc, argv, app);");
    }
    else
    {
        writelnf(f, "    return %hs_main(argc, argv);", project->globalNamespace.c_str());
    }

    // exit
    writeln(f, "}");
    return true;
}

bool SolutionGenerator::generateProjectTestMainSourceFile(const SolutionProject* project, std::stringstream& f)
{
    writeln(f, "/***");
    writeln(f, "* Onion Static Lib Initialization Code");
    writeln(f, "* Auto generated, do not modify");
    writeln(f, "* Build system source code licensed under MIP license");
    writeln(f, "***/");
    writeln(f, "");

    writeln(f, "#include \"build.h\"");

    const auto hasPreMain = project->optionUsePreMain;

    writeln(f, "");

    if (project->optionUseGtest)
        writeln(f, "#include \"gtest/gtest.h\"");
    else
        writeln(f, "#include \"catch2/cach2.hpp\"");
    writeln(f, "");

    if (hasPreMain)
    {
        writeln(f, "extern bool pre_main(int argc, char** argv, int* exitCode);");
        writeln(f, "");
    }

    writeln(f, "int main(int argc, char** argv) {");

	writelnf(f, "    extern void InitModule_%hs(void*);", project->name.c_str());
    writelnf(f, "    InitModule_%hs(nullptr);", project->name.c_str());
	writeln(f, "");

    writeln(f, "");
    writeln(f, "  int ret = 0;");
    writeln(f, "");

	if (hasPreMain)
		writeln(f, "  if (!pre_main(argc, argv, &ret)) {");

    if (project->optionUseGtest)
    {
        writeln(f, "  testing::InitGoogleTest(&argc, argv);");
        writeln(f, "  ret = RUN_ALL_TESTS();");
    }
    else
    {
		writeln(f, "  ret = Catch::Session().run(argc, argv);");
	}

	if (hasPreMain)
		writeln(f, "  }");

    writeln(f, "  return ret;");
    writeln(f, "}");

    return true;
}

struct LinkedProject
{
    const SolutionProject* project = nullptr;
    bool staticallyLinked = false;
};

static void CollectDirectlyLinkedProjects(const SolutionProject* project, std::unordered_set< const SolutionProject*>& outVisited, std::vector<LinkedProject>& outLinkedProjects, int depth, bool recurse)
{
    if (!outVisited.insert(project).second)
        return;

    // collect only projects linked by the main one
    if (depth)
    {
        LinkedProject entry;
        entry.staticallyLinked = (project->type == ProjectType::StaticLibrary);
        entry.project = project;
        outLinkedProjects.push_back(entry);
    }

    // check local dependencies
    if (recurse)
    {
        for (const auto* dep : project->directDependencies)
        {
			// Recurse on statically linked projects, but do not recurse on dynamically linked ones
            if (dep->type == ProjectType::StaticLibrary)
            {
                // all static libraries must be linked together at the top module - dll, exe, etc
                CollectDirectlyLinkedProjects(dep, outVisited, outLinkedProjects, depth + 1, true);
            }
            else if (dep->type == ProjectType::SharedLibrary && project->type != ProjectType::StaticLibrary)
            {
                // dynamic libaries dont have to propagate the static libraries to their parents
                CollectDirectlyLinkedProjects(dep, outVisited, outLinkedProjects, depth + 1, false);
            }
        }
    }
}

bool SolutionGenerator::generateProjectBuildSourceFile(const SolutionProject* project, std::stringstream& f)
{
    writeln(f, "/***");
    writeln(f, "* Precompiled Header");
    writeln(f, "* Auto generated, do not modify");
    writeln(f, "***/");
    writeln(f, "");

    writeln(f, "#include \"build.h\"");
    writeln(f, "");

    return true;
}

bool SolutionGenerator::generateProjectModuleSourceFile(const SolutionProject* project, std::stringstream& f)
{
    writeln(f, "/***");
    writeln(f, "* Module definition file");
    writeln(f, "* Auto generated, do not modify");
    writeln(f, "***/");
    writeln(f, "");

    writeln(f, "#include \"build.h\"");
    writeln(f, "");

	const auto hasSystem = HasDependency(project, "core_system") || (project->name == "core_system");
    const auto hasFileSystem = HasDependency(project, "core_file");

    // embedded files header
    if (project->optionUseEmbeddedFiles)
    {
        writeln(f, "#include \"core/file/include/embeddedFile.h\"");
    }

    // collect all projects that are STATICALLY linked to this project
    std::vector<LinkedProject> staticallyLinkedProjects, orderedStaticallyLinkedProjects;
    {
        std::unordered_set< const SolutionProject*> visited;
        CollectDirectlyLinkedProjects(project, visited, staticallyLinkedProjects, 0, true);

        // collect in right order!
        for (const auto* dep : project->allDependencies)
        {
            for (const auto& linkedProject : staticallyLinkedProjects)
            {
                if (linkedProject.project == dep)
                {
                    orderedStaticallyLinkedProjects.push_back(linkedProject);
                    break;
                }
            }
        }                
    }

    // determine if project requires static initialization (the apps and console apps require that)
    // then pull in the library linkage, for apps we pull much more crap
    if (m_config.generator == GeneratorType::VisualStudio19 || m_config.generator == GeneratorType::VisualStudio22)
    {
        // direct third party libraries used by this project
        std::vector<fs::path> exportedLibraryFiles;
        std::unordered_set<const ExternalLibraryManifest*> exportedLibs;
        for (const auto& proj : staticallyLinkedProjects)
        {
            if (proj.staticallyLinked)
            {
                for (const auto* dep : proj.project->libraryDependencies)
                    exportedLibs.insert(dep);

                if (proj.project->optionFrozen)
                {
                    for (const auto& libraryPath : proj.project->frozenLibraryFiles)
                        PushBackUnique(exportedLibraryFiles, libraryPath);
                }
            }
        }

        // our direct dependencies
		for (const auto* dep : project->libraryDependencies)
			exportedLibs.insert(dep);

        // collect actual library files
		for (const auto* dep : exportedLibs)
			dep->collectLibraries(m_config.platform, &exportedLibraryFiles);

        // print the libraries
        if (!exportedLibraryFiles.empty())
        {
            writeln(f, "// Libraries");
            for (const auto& linkPath : exportedLibraryFiles)
            {
				std::stringstream ss;
				ss << linkPath;
				writelnf(f, "#pragma comment( lib, %s )", ss.str().c_str());
            }
            writeln(f, "");
        }

        bool hasLocalLibraries = false;
        for (const auto* file : project->files)
        {
            if (file->type == ProjectFileType::LocalStaticLibrary)
            {
                if (!hasLocalLibraries)
                {
                    writeln(f, "// Local Libraries");
                    hasLocalLibraries = true;
                }

				std::stringstream ss;
				ss << file->absolutePath;
				writelnf(f, "#pragma comment( lib, %s )", ss.str().c_str());
            }

            if (!hasLocalLibraries)
                writeln(f, "");
        }
    }

    // pre init
    if (project->optionHasPreInit)
    {
		writeln(f, "// Module Pre-Initialization");
		writelnf(f, "extern void PreInit_%hs();", project->name.c_str());
		writeln(f, "");
    }
    
    // reflection initialization methods
    if (project->optionUseReflection)
    {
        writeln(f, "// Initialization for reflection");
        writelnf(f, "extern void InitializeReflection_%hs();", project->name.c_str());
        writeln(f, "");
    }
    
	// init
	if (project->optionHasInit)
	{
		writeln(f, "// Module Initialization");
		writelnf(f, "extern void Init_%hs();", project->name.c_str());
		writeln(f, "");
	}
	
    // embedded files initialization
    if (project->optionUseEmbeddedFiles)
    {
		for (const auto* file : project->files)
		{
			if (file->type == ProjectFileType::MediaFile)
			{
				const auto symbolPrefix = std::string("EMBED_") + std::string(project->name) + "_";
				const auto symbolCoreName = symbolPrefix + std::string(PartAfter(MakeSymbolName(file->projectRelativePath), "media_"));

                writelnf(f, "// File: '%hs'", file->projectRelativePath.c_str());
				writelnf(f, "extern const char* %hs_PATH;", symbolCoreName.c_str());
				writelnf(f, "extern const uint8_t* %hs_DATA;", symbolCoreName.c_str());
				writelnf(f, "extern const unsigned int %hs_SIZE;", symbolCoreName.c_str());
				writelnf(f, "extern const uint64_t %hs_CRC;", symbolCoreName.c_str());
				writelnf(f, "extern const uint64_t %hs_TS;", symbolCoreName.c_str());
				writelnf(f, "extern const char* %hs_SPATH;", symbolCoreName.c_str());
				writeln(f, "");
			}
		}

        writeln(f, "// Embedded media files registration");
		writelnf(f, "void InitializeEmbeddedFiles_%hs() {", project->name.c_str());
		for (const auto* file : project->files)
		{
			if (file->type == ProjectFileType::MediaFile)
			{
				const auto symbolPrefix = std::string("EMBED_") + std::string(project->name) + "_";
                const auto symbolCoreName = symbolPrefix + std::string(PartAfter(MakeSymbolName(file->projectRelativePath), "media_"));

                if (hasFileSystem)
                {
                    // virtual void registerFile(const char* path, const void* data, uint32_t size, uint64_t crc, const char* sourcePath, TimeStamp sourceTimeStamp) = 0;
                    writelnf(f, "%hs::EmbeddedFiles().registerFile(%hs_PATH, %hs_DATA, %hs_SIZE, %hs_CRC, %hs_SPATH, %hs::TimeStamp(%hs_TS));",
                        project->globalNamespace.c_str(),
                        symbolCoreName.c_str(), symbolCoreName.c_str(), symbolCoreName.c_str(),
                        symbolCoreName.c_str(), symbolCoreName.c_str(), project->globalNamespace.c_str(), symbolCoreName.c_str());
                }
			}
		}

		writeln(f, "}");
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
		    for (const auto& dep : orderedStaticallyLinkedProjects)
		    {
                if (dep.project->optionDetached || dep.project == project)
                    continue;
                if (dep.project->optionLegacy || dep.project->optionThirdParty)
                    continue;
                if (dep.project->type == ProjectType::HeaderLibrary)
                    continue;

                if (dep.staticallyLinked)
                {
                    writelnf(f, "    extern void InitModule_%s(void*);", dep.project->name.c_str());
                    writelnf(f, "    InitModule_%s(handle);", dep.project->name.c_str());
                }
                else if (hasSystem)
                {
                    writelnf(f, "    %hs::modules::LoadDynamicModule(\"%s\");", project->globalNamespace.c_str(), dep.project->name.c_str());
                }
			}
		}

        // initialize self
        if (project->optionUseStaticInit && hasSystem)
        {
            std::stringstream dependenciesString;
            bool first = true;

            for (const auto* dep : project->allDependencies)
            {
                if (Contains<SolutionProject*>(project->directDependencies, (SolutionProject*)dep))
                {
                    if (dep->name == "core_system")
                        continue;
					if (dep->type == ProjectType::HeaderLibrary)
						continue;

                    bool hasSystemDependency = false;
                    for (const auto* subDep : dep->allDependencies)
                    {
                        if (subDep->name == "core_system")
                        {
                            hasSystemDependency = true;
                            break;
                        }
                    }

                    if (hasSystemDependency)
                    {
                        if (!first) dependenciesString << ";";
                        first = false;
                        dependenciesString << dep->name;
                    }
                }
            }

            writelnf(f, "    const char* deps = \"%s\";", dependenciesString.str().c_str());

            std::stringstream initFunctions;

            if (project->optionHasPreInit)
                initFunctions << "PreInit_" << project->name << "(); ";

            if (project->optionUseReflection && project->name != "core_reflection")
                initFunctions << "InitializeReflection_" << project->name << "(); ";

            if (project->optionUseEmbeddedFiles)
				initFunctions << "InitializeEmbeddedFiles_" << project->name << "(); ";

			if (project->optionHasInit)
				initFunctions << "Init_" << project->name << "(); ";

            writelnf(f, "    %hs::modules::TModuleInitializationFunc initFunc = []() { %hs; };", project->globalNamespace.c_str(), initFunctions.str().c_str());
            writelnf(f, "    %hs::modules::RegisterModule(\"%hs\", __DATE__, __TIME__, _MSC_FULL_VER, initFunc, deps);", project->globalNamespace.c_str(), project->name.c_str());
            writelnf(f, "    %hs::modules::InitializePendingModules();", project->globalNamespace.c_str());
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

    return true;
}

bool SolutionGenerator::generateProjectGlueHeaderFile(const SolutionProject* project, std::stringstream& f)
{
    const auto upperName = ToUpper(project->name);
    const auto macroName = upperName + "_GLUE";
    const auto apiName = upperName + "_API";
    const auto exportsMacroName = upperName + "_EXPORTS";

    writeln(f, "/***");
    writeln(f, "* Precompiled Header");
    writeln(f, "* Auto generated, do not modify - add stuff to public.h instead");
    writeln(f, "***/");
    writeln(f, "");
    writeln(f, "#pragma once");
    writeln(f, "");

    // if compiling as test app include the macro first to allow some headers to reconfigure for tests
    if (project->type == ProjectType::TestApplication)
    {
        if (project->optionUseGtest)
        {
            writeln(f, "// We are running tests");
            writeln(f, "#define WITH_GTEST");
            writeln(f, "");
        }
        else
        {
			writeln(f, "// We are running tests");
			writeln(f, "#define WITH_CATCH2");
			writeln(f, "");
        }
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

    // special handling of application systems
    if (project->name == "runtime_app")
    {
        // generate the class mapping for each registered application system class
        writeln(f, "// Application system classes forward declarations");
        for (const auto* globalProject : m_projects)
        {
            // forward declarations
            for (const auto className : globalProject->appSystemClasses)
            {
                std::vector<std::string_view> names;
                SplitString(className, "::", names);

                std::stringstream line;
                for (uint32_t i = 0; i < names.size() - 1; ++i)
                {
                    line << "namespace ";
                    line << names[i];
                    line << " {";
                }

                line << "class " << names.back() << ";";

                for (uint32_t i = 0; i < names.size() - 1; ++i)
                    line << "} ";

                writeln(f, line.str());
            }
        }
        writeln(f, "");

        // helper resolver
        // TODO: move to actual header
        writeln(f, "// Application system helper index resolver helper template");
        writeln(f, "template< typename T > struct ApplicationSystemClassIndexResolver {};");
        writeln(f, "");

        writeln(f, "// Application system helper class registration");
        writeln(f, "extern RUNTIME_APP_API void RegisterAppSystemClass(const char* className, int assignedIndex);");
        writeln(f, "");

        // map classes to indices
		uint32_t index = 1;
		writeln(f, "// Application system class index resolver");
        for (const auto* globalProject : m_projects)
        {
            // forward declarations
            for (const auto className : globalProject->appSystemClasses)
            {
                writelnf(f, "template<> struct ApplicationSystemClassIndexResolver<%hs> { static constexpr int CLASS_INDEX = %u; };", className.c_str(), index++);
            }
        }
		writeln(f, "");

		writeln(f, "// Helper function to get application system index based on compile-time class");
		writeln(f, "template< typename T > static inline constexpr int GetApplicationSystemIndex() { return ApplicationSystemClassIndexResolver<T>::CLASS_INDEX; }");
        writeln(f, "");
    }

    // include glue headers from all dependencies
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
    writeln(f, "* Precompiled Header");
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
        if (project->optionUseGtest)
        {
			writeln(f, "// Google Test Suite");
			writeln(f, "#include \"gtest/gtest.h\"");
			writeln(f, "");
        }
        else
        {
            writeln(f, "// Catch2 Test Suite");
            writeln(f, "#include \"catch2/catch2.hpp\"");
            writeln(f, "");
        }
    }

    return true;
}

bool SolutionGenerator::generateProjectHostingBatchFile(const SolutionProject* project, std::stringstream& f, const fs::path& binaryPath)
{
    writelnf(f, "start chrome \"http://localhost:8000/%hs.html\"", project->name.c_str());
    writelnf(f, "npx statikk --port 8000 --coi \"%hs\"", binaryPath.u8string().c_str());
    return true;
}

#if 0
bool SolutionGenerator::generateSolutionReflectionFileTlogList(std::stringstream& f)
{
    f << m_config.executablePath.u8string();

    uint32_t numReflectedFiles = 0;
    for (const auto* proj : m_projects)
    {
        for (const auto* file : proj->files)
        {
            if (file->type == ProjectFileType::CppSource)
            {
				if (file->name == "build.cpp" || file->name == "reflection.cpp")
					continue;

                f << file->absolutePath.u8string();
                numReflectedFiles += 1;
            }
        }
    }

    LogInfo() << "Found " << numReflectedFiles << " source code files for reflection";
    return true;
}
#endif

bool SolutionGenerator::generateSolutionReflectionFileProcessingList(std::stringstream& f)
{
    for (const auto* proj : m_projects)
    {
        if (!proj->localReflectionFile.empty() && !proj->rootPath.empty() && !proj->optionLegacy && !proj->optionThirdParty)
        {
            auto projectFilePath = (proj->projectPath / proj->name);
            projectFilePath += ".vcxproj";
            projectFilePath.make_preferred();

			auto projectSourceDirectory = (proj->rootPath / "src");
            projectSourceDirectory.make_preferred();

            writelnf(f, "PROJECT");
            writelnf(f, "%hs", proj->name.c_str());
            writelnf(f, "%hs", proj->globalNamespace.c_str());

            if (!proj->appSystemClasses.empty())
            {
                std::stringstream appSystemClassNames;
                for (const auto& cls : proj->appSystemClasses)
                {
                    appSystemClassNames << cls;
                    appSystemClassNames << ";";
                }
                writelnf(f, "%hs", appSystemClassNames.str().c_str());
            }
            else
            {
                writeln(f, ";");
            }

            writelnf(f, "%hs", projectFilePath.u8string().c_str());
			writelnf(f, "%hs", projectSourceDirectory.u8string().c_str());
            writelnf(f, "%hs", proj->localReflectionFile.u8string().c_str());
        }
    }

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

	LogInfo() << "Found " << numMediaFiles << " embedded media build files";*/
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
    if (m_config.flagStaticBuild)
    {
#ifdef _WIN32
		fs::path toolPath;
        if (!m_files.resolveDirectoryPath("tools/bison/windows", toolPath))
            return false;

		const auto executablePath = (toolPath / "win_bison.exe").make_preferred();
#elif defined(__APPLE__)
        fs::path toolPath;
#if defined(__arm64__)
        if (!m_files.resolveDirectoryPath("tools/bison/darwin_arm", toolPath))
            return false;
#else
        if (!m_files.resolveDirectoryPath("tools/bison/darwin", toolPath))
            return false;
#endif

        const auto executablePath = (toolPath / "run_bison.sh").make_preferred();
#else
		fs::path toolPath;
		if (!m_files.resolveDirectoryPath("tools/bison/linux", toolPath))
			return false;

		const auto executablePath = (toolPath / "run_bison.sh").make_preferred();
#endif

        if (IsFileSourceNewer(file->absolutePath, parserFile) ||
        IsFileSourceNewer(file->absolutePath, symbolsFile))
        {
            std::error_code ec;
            if (!fs::is_directory(project->generatedPath))
            {
                if (!fs::create_directories(project->generatedPath, ec))
                {
                    LogInfo() << "BISON tool failed because output directory \"" << project->generatedPath << "\" can't be created: " << ec;
                    return false;
                }
            }

            std::stringstream params;
            params << executablePath.u8string() << " ";
            params << "\"" << file->absolutePath.u8string() << "\" ";
            params << "-o\"" << parserFile.u8string() << "\" ";
#ifdef __APPLE__
            params << "--header=\"" << symbolsFile.u8string() << "\" ";
#else
            params << "--defines=\"" << symbolsFile.u8string() << "\" ";
#endif
            params << "--report-file=\"" << reportPath.u8string() << "\" ";
            params << "--verbose";

            const auto activeDir = fs::current_path();
            const auto bisonDir = executablePath.parent_path().make_preferred();
            std::error_code er;
            fs::current_path(bisonDir, er);
            if (er)
            {
                LogInfo() << "BISON tool failed to switch active directory to " << bisonDir;
                return false;
            }

            auto code = std::system(params.str().c_str());

            fs::current_path(activeDir);

            if (code != 0)
            {
                LogInfo() << "BISON tool failed with exit code " << code;
                return false;
            }
            else
            {
                LogInfo() << "BISON tool finished and generated " << parserFile;
            }
        }
        else
        {
            LogInfo() << "BISON tool skipped because '" << parserFile << "' is up to date";
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

bool SolutionGenerator::generateSolutionFstabFile(const fs::path& binaryPath, std::stringstream& outContent)
{
    for (const auto& data : m_dataFolders)
    {
        std::error_code ec;
        const auto relativePath = fs::relative(data.dataPath, binaryPath, ec);
        if (!ec)
        {
			writeln(outContent, "DATA_RELATIVE");
			writeln(outContent, data.mountPath.c_str());
            writeln(outContent, relativePath.u8string().c_str());
        }
        else
        {
			writeln(outContent, "DATA_ABSOLUTE");
			writeln(outContent, data.mountPath.c_str());
            writeln(outContent, data.dataPath.u8string().c_str());
        }
    }

    writeln(outContent, "EOF");
    return true;
}

//--

void SolutionGenerator::CollectDefineString(std::string_view name, std::string_view value, TDefines* outDefines)
{
	for (auto& entry : *outDefines)
	{
		if (entry.first == name)
		{
			entry.second = value;
			return;
		}
	}

    outDefines->emplace_back(std::make_pair(name, value));
}

void SolutionGenerator::CollectDefineStrings(const TDefines& defs, TDefines* outDefines)
{
	for (const auto& def : defs)
		CollectDefineString(def.first, def.second, outDefines);
}

void SolutionGenerator::CollectDefineStringsFromSimpleList(std::string_view txt, TDefines* outDefines)
{
	std::vector<std::string_view> macros;
	SplitString(txt, ";", macros);

	for (auto part : macros)
	{
		part = Trim(part);
        if (!part.empty())
        {
            std::string_view key, value;
            if (SplitString(part, "=", key, value))
            {
                key = Trim(key);
                value = Trim(value);

                if (!key.empty())
                    CollectDefineString(key, value, outDefines);
            }
            else
            {
                CollectDefineString(part, "1", outDefines);
            }
        }
	}
}

void SolutionGenerator::collectDefines(const SolutionProject* project, TDefines* outDefines) const
{
    CollectDefineString("__SSE__", "", outDefines);
    CollectDefineString("__SSE2__", "", outDefines);
    CollectDefineString("__SSE3__", "", outDefines);
    CollectDefineString("__SSSE3__", "", outDefines); // I love to learn the story of this define
    CollectDefineString("__SSE4_1__", "", outDefines);
    CollectDefineString("__SSE4_2__", "", outDefines);

    if (m_config.platform != PlatformType::Wasm)
    {
        if (project->optionAdvancedInstructionSet == "AVX" || project->optionAdvancedInstructionSet == "AVX2")
            CollectDefineString("__AVX__", "", outDefines);
        else if (project->optionAdvancedInstructionSet == "AVX2")
            CollectDefineString("__AVX2__", "", outDefines);
    }

	for (const auto* dep : project->allDependencies)
		CollectDefineStrings(dep->globalDefines, outDefines);

	CollectDefineStrings(project->globalDefines, outDefines);
	CollectDefineStrings(project->localDefines, outDefines);

	for (const auto* dep : project->allDependencies)
	{
		if (project->type == ProjectType::StaticLibrary && dep->type == ProjectType::SharedLibrary)
		{
			LogWarning() << "Static library '" << project->name << " is using a shared library (DLL) '" << dep->name << "' this is not allowed and may not work!";
		}

		if (dep->optionThirdParty && dep->type == ProjectType::SharedLibrary)
			CollectDefineStringsFromSimpleList(dep->thirdPartySharedGlobalExportDefine, outDefines);
	}

	if (project->type == ProjectType::SharedLibrary)
	{
		CollectDefineStringsFromSimpleList(project->thirdPartySharedGlobalExportDefine, outDefines);
		CollectDefineStringsFromSimpleList(project->thirdPartySharedLocalBuildDefine, outDefines);
	}
}

void SolutionGenerator::collectSourceRoots(const SolutionProject* project, std::vector<fs::path>* outPaths) const
{
    for (const auto& sourceRoot : m_sourceRoots)
        outPaths->push_back(sourceRoot);

    for (const auto* dep : project->allDependencies)
        for (const auto& path : dep->exportedIncludePaths)
            outPaths->push_back(path);

    // TODO: remove
    if (!project->rootPath.empty())
    {
        if (project->optionLegacy)
        {
            outPaths->push_back(project->rootPath);
        }
        else if (project->optionThirdParty)
        {
            outPaths->push_back(project->rootPath);
        }
        else
        {
            outPaths->push_back(project->rootPath / "src");
            outPaths->push_back(project->rootPath / "include");
        }
    }

    outPaths->push_back(m_config.derivedSolutionPathBase / "generated/_shared");
    outPaths->push_back(project->generatedPath);

    for (const auto& path : project->additionalIncludePaths)
        outPaths->push_back(path);

    /*for (const auto& path : project->originalProject->localIncludeDirectories)
    {
        const auto fullPath = project->originalProject->rootPath / path;
        outPaths.push_back(fullPath);
    }*/
}

//--