#include "common.h"
#include "toolReflection.h"
#include "fileGenerator.h"


//--

ProjectReflection::~ProjectReflection()
{

}

static bool ProjectsNeedsReflectionUpdate(const fs::path& reflectionFile, const std::vector<ProjectReflection::RefelctionFile*>& files, fs::file_time_type& outNewstTimestamp)
{
    /*if (files.empty())
        return true;*/

    try
    {
        if (!fs::is_regular_file(reflectionFile))
        {
            LogInfo() << "Detected deleted reflection file '" << reflectionFile << "', reflection has to be rebuilt for project";
            return true;
        }

        const auto reflectionFileTimestamp = fs::last_write_time(reflectionFile);
        //LogInfo() << "Reflection timestamp for " << reflectionFile << ": " << reflectionFileTimestamp.time_since_epoch().count();

        outNewstTimestamp = reflectionFileTimestamp;

        bool hasChangedDetected = false;
        for (const auto* file : files)
        {
            const auto sourceFileTimestamp = fs::last_write_time(file->absolutePath);
          //  LogInfo() << "Timestamp for source " << file->absoluitePath << ": " << sourceFileTimestamp.time_since_epoch().count();

            if (sourceFileTimestamp > reflectionFileTimestamp)
            {
                if (sourceFileTimestamp > outNewstTimestamp)
                {
                    //LogInfo() << "Reflection timestamp for " << reflectionFile << ": " << reflectionFileTimestamp.time_since_epoch().count();
                    //LogInfo() << "Timestamp for source " << file->absoluitePath << ": " << sourceFileTimestamp.time_since_epoch().count();
                    LogInfo() << "Detected change in file " << file->absolutePath << ", reflection has to be scanned for project";
                    outNewstTimestamp = sourceFileTimestamp;
					hasChangedDetected = true;
                }
            }
        }


        if (hasChangedDetected)
        {
            LogInfo() << "Some files used to build " << reflectionFile << " changed, reflection will have to be refreshed";
            return true;
        }

        return false;
    }
    catch (...)
    {
        //LogInfo() << "Detected problems checking reflection file '" << reflectionFile << "', reflection has to be rebuilt for project";
        return false;
    }
}

template <typename TP>
std::string print_time(TP tp)
{
	using namespace std::chrono;
	auto sctp = time_point_cast<system_clock::duration>(tp - TP::clock::now()
		+ system_clock::now());

	auto tt = system_clock::to_time_t(sctp);
	std::tm* gmt = std::gmtime(&tt);
	std::stringstream buffer;
	buffer << std::put_time(gmt, "%A, %d %B %Y %H:%M");
	return buffer.str();
}

bool ProjectReflection::GetFileTime(const fs::path& path, fs::file_time_type& outLastWriteTime)
{
    try
    {
        std::error_code ec;
        outLastWriteTime = fs::last_write_time(path, ec);
        //LogInfo() << "Last write time " << path << ": " << print_time(outLastWriteTime);
        return !ec;
    }
	catch (...)
	{
		return false;
	}
}

bool ProjectReflection::CheckFileUpToDate(const fs::file_time_type& referenceTime, const fs::path& path)
{
    fs::file_time_type lastModificationTime;
    if (!GetFileTime(path, lastModificationTime))
    {
        LogWarning() << "Compact list not up to date because " << path << " does not exist";
        return false; // file does not exist
    }

    if (lastModificationTime > referenceTime)
    {
		LogInfo() << "Compact list not up to date because " << path << " was modified: " << print_time(lastModificationTime) << " > " << print_time(referenceTime);
		return false; // file does not exist
    }

    return true;
}

bool ProjectReflection::CheckIfCompactListUpToDate(const fs::path& outputFilePath, const std::vector<CompactProjectInfo>& compactProjects)
{
    fs::file_time_type referenceTime;
    if (!GetFileTime(outputFilePath, referenceTime))
    {
        LogWarning() << "Compact list not up to date because " << outputFilePath << " does not exist";
        return false; // there's no compact list xD
    }

    for (const auto& proj : compactProjects)
    {
        if (!CheckFileUpToDate(referenceTime, proj.sourceDirectoryPath))
            return false;
		if (!CheckFileUpToDate(referenceTime, proj.vxprojFilePath))
			return false;
    }

    return true; // file list seems to be up to date
}

bool ProjectReflection::LoadCompactProjectsFromFileList(const fs::path& inputFilePath, std::vector<CompactProjectInfo>& outCompactProjects)
{
    try
    {
        std::ifstream file(inputFilePath);

        std::string str;
        while (std::getline(file, str))
        {
            if (str == "PROJECT")
            {
                CompactProjectInfo info;
                std::getline(file, info.name);
                std::getline(file, info.globalNamespace);
                std::getline(file, info.vxprojFilePath);
                std::getline(file, info.sourceDirectoryPath);
                std::getline(file, info.reflectionFilePath);
                outCompactProjects.push_back(info);
                continue;
            }
        }

        LogInfo() << "Loaded " << outCompactProjects.size() << " project entries";
    }
    catch (const std::exception& e)
    {
        LogError() << "Error parsing reflection list " << e.what();
        return false;
    }

    return true;
}


bool ProjectReflection::CollectSourcesFromDirectory(const fs::path& directoryPath, std::vector<fs::path>& outSources, fs::file_time_type& outTimeStamp)
{
	bool valid = true;

	try
	{
		if (fs::is_directory(directoryPath))
		{
            outTimeStamp = std::max(outTimeStamp, fs::last_write_time(directoryPath));

			for (const auto& entry : fs::directory_iterator(directoryPath))
			{
				const auto name = entry.path().filename().u8string();
                if (name.c_str()[0] == '.')
                    continue; // skip the hidden crap

                if (entry.is_directory())
                {
                    valid &= CollectSourcesFromDirectory(entry.path(), outSources, outTimeStamp);
                }
                else if (entry.is_regular_file())
                {
                    if (EndsWith(name, ".cpp") && name != "reflection.cpp" && name != "build.cpp")
                    {
                        auto path = fs::path(entry.path()).make_preferred();
                        outSources.push_back(path);

                        outTimeStamp = std::max(outTimeStamp, fs::last_write_time(path));
                    }
                }
			}
		}
	}
	catch (fs::filesystem_error& e)
	{
        LogError() << "Filesystem Error: " << e.what();
		valid = false;
	}

	return valid;
}

void ProjectReflection::PrintExpandedFileList(std::stringstream& f, const std::vector<CompactProjectInfo>& compactProjects)
{
	for (const auto& proj : compactProjects)
	{
        writeln(f, "PROJECT");
        writeln(f, proj.name);
        writeln(f, proj.globalNamespace);
        writeln(f, fs::path(proj.reflectionFilePath).u8string());
		for (const auto& filePath : proj.sourceFiles)
			writeln(f, filePath.u8string().c_str());
	}
}

void ProjectReflection::PrintReadTlog(std::stringstream& f, const std::vector<CompactProjectInfo>& compactProjects)
{
    writeln(f, GetExecutablePath()); // rebuild every time build tool changes as well

    for (const auto& proj : compactProjects)
    {
        writeln(f, fs::path(proj.vxprojFilePath).make_preferred().u8string().c_str());
        for (const auto& filePath : proj.sourceFiles)
            writeln(f, filePath.u8string().c_str());
    }
}

void ProjectReflection::PrintWriteTlog(std::stringstream& f, const std::vector<CompactProjectInfo>& compactProjects)
{
	for (const auto& proj : compactProjects)
		writeln(f, fs::path(proj.reflectionFilePath).make_preferred().u8string().c_str());
}

bool ProjectReflection::extractFromExpandedList(const fs::path& fileList)
{
    try
    {
        std::ifstream file(fileList);

        RefelctionProject* project = nullptr;

        std::string str;
        uint32_t numFiles = 0;
        while (std::getline(file, str))
        {
            if (str == "PROJECT")
            {
                project = new RefelctionProject();
                projects.push_back(project);

                std::getline(file, project->mergedName);
                std::getline(file, project->globalNamespace);
                std::getline(file, str);
                project->reflectionFilePath = fs::path(str).make_preferred();
                continue;
            }

            if (project)
            {
                auto* file = new RefelctionFile();
                file->absolutePath = str;
                file->globalNamespace = project->globalNamespace;
                file->tokenized.contextPath = str;
                project->files.push_back(file);

                numFiles += 1;
            }
        }

        LogInfo() << "Loaded " << numFiles << " files from " << projects.size() << " projects for reflection";
    }
    catch (const std::exception& e)
    {
        LogError() << "Error parsing reflection list " << e.what();
        return false;
    }

    return true;
}

bool ProjectReflection::extractFromCompactList(const fs::path& fileList, const fs::path& outputReadTlog, const fs::path& outputWriteTlog)
{
    // load the compact project list - just the basic information that does not depend on the directory content
    std::vector<CompactProjectInfo> compactProjects;
    LoadCompactProjectsFromFileList(fileList, compactProjects);

    // read/write tlogs
    bool hasValidTlogs = true;
    if (!outputWriteTlog.empty()) hasValidTlogs &= fs::is_regular_file(outputWriteTlog);
    if (!outputReadTlog.empty()) hasValidTlogs &= fs::is_regular_file(outputReadTlog);

    // check if need to expand the file list
    auto fileListExpanded = fileList;
    fileListExpanded += ".expanded";
    if (!CheckIfCompactListUpToDate(fileListExpanded, compactProjects) || !hasValidTlogs)
    {
        // expand the projects (basically collect source files)
        fs::file_time_type expandedTimestamp;
        for (auto& proj : compactProjects)
        {
            if (!CollectSourcesFromDirectory(proj.sourceDirectoryPath, proj.sourceFiles, expandedTimestamp))
                return false;

            // make sure to include source directory and project file in the reflection BS
            expandedTimestamp = std::max(expandedTimestamp, fs::last_write_time(proj.vxprojFilePath));
            expandedTimestamp = std::max(expandedTimestamp, fs::last_write_time(proj.sourceDirectoryPath));

            std::sort(proj.sourceFiles.begin(), proj.sourceFiles.end());
        }

		// write read tlog
		if (!outputReadTlog.empty())
		{
			std::stringstream str;
			PrintReadTlog(str, compactProjects);
			SaveFileFromString(outputReadTlog, str.str(), false, false, nullptr, expandedTimestamp);
		}

		// write read tlog
		if (!outputWriteTlog.empty())
		{
			std::stringstream str;
			PrintWriteTlog(str, compactProjects);
			SaveFileFromString(outputWriteTlog, str.str(), false, false, nullptr, expandedTimestamp);
		}

        // write expanded file
        // NOTE: should be written LAST (after tlogs)
        {
            std::stringstream str;
            PrintExpandedFileList(str, compactProjects);
            SaveFileFromString(fileListExpanded, str.str(), false, false, nullptr, expandedTimestamp);
            GetFileTime(fileListExpanded, expandedTimestamp);
        }
    }

    // load the expanded list and continue
    return extractFromExpandedList(fileListExpanded);
}

#if 0
bool ProjectReflection::extractFromArgs(const std::string& fileList, const std::string& projectName, const fs::path& outputFile)
{
	std::string txt;
	if (!LoadFileToString(fileList, txt))
	{
		LogInfo() << "Error loading file list " << fileList;
		return false;
	}

	std::vector<std::string_view> filePaths;
	SplitString(txt, "\n", filePaths);
	LogInfo() << "Gathered " << filePaths.size() << " files to process in project '" << projectName << "'";
	LogInfo() << "Output will be written to " << outputFile ;

    std::vector<fs::path> filePathsEx;
    for (const auto& path : filePaths)
        filePathsEx.push_back(Trim(path));

    return extractFromFileList(filePathsEx, projectName, outputFile);
}
#endif

bool ProjectReflection::extractFromFileList(const std::vector<fs::path>& filePaths, const std::string& projectName, const fs::path& outputFile)
{
	auto* project = new RefelctionProject();
    project->mergedName = projectName;
    project->reflectionFilePath = outputFile;
	projects.push_back(project);

    for (const auto& path : filePaths)
    {
        const auto fileName = path.filename().u8string();

        if (fileName == "reflection.cpp" || fileName == "main.cpp" || fileName == "init.cpp" || fileName == "build.cpp")
            continue;

        const auto sourceFile = EndsWith(fileName.c_str(), ".cpp");
        const auto headerFile = EndsWith(fileName.c_str(), ".h");

        if (sourceFile || headerFile)
        {
            auto* file = new RefelctionFile();
            file->absolutePath = path;
            file->tokenized.contextPath = path;
            file->sourceFile = sourceFile;
            project->files.push_back(file);
        }
	}

    return true;
}

bool ProjectReflection::filterProjects()
{
    auto oldProjects = std::move(projects);

    std::mutex lock;

    #pragma omp parallel for
    for (int i = 0; i < oldProjects.size(); ++i)
    {
        auto* p = oldProjects[i];

        if (ProjectsNeedsReflectionUpdate(p->reflectionFilePath, p->files, p->reflectionFileTimstamp))
        {
            lock.lock();

            projects.push_back(p);

            for (auto* f : p->files)
                files.push_back(f);

            lock.unlock();
        }
    }

    LogInfo() << "Found " << files.size() << " files from " << projects.size() << " projects that need to be checked";
    return true;
}

bool ProjectReflection::tokenizeFiles()
{
    bool valid = true;

    #pragma omp parallel for
    for (int i=0; i<files.size(); ++i)
    {
        auto* file = files[i];

        std::string content;
        if (LoadFileToString(file->absolutePath, content))
        {
            valid &= file->tokenized.tokenize(content);
        }
        else
        {
            LogInfo() << "Failed to load content of file " << file->absolutePath;
            valid = false;
        }
    }

    return valid;
}

bool ProjectReflection::parseDeclarations()
{
    std::atomic<uint32_t> valid = 1;

    #pragma omp parallel for
    for (int i = 0; i < files.size(); ++i)
    {
        auto* file = files[i];
        if (!file->tokenized.process(file->globalNamespace))
        {
            LogError() << "[BREKAING] Failed to process declaration from " << file->absolutePath;
            valid = 0;
        }
    }

    uint32_t totalDeclarations = 0;
    for (auto* file : files)
        totalDeclarations += (uint32_t)file->tokenized.declarations.size();

    LogInfo() << "Discovered " << totalDeclarations << " declarations";

    return valid;
}

bool ProjectReflection::generateReflection(FileGenerator& files) const
{
    std::atomic<bool> valid = true;

    #pragma omp parallel for
    for (int i=0; i<projects.size(); ++i)
	{
        const auto* p = projects[i];

        auto file = files.createFile(p->reflectionFilePath);
        file->customtTime = p->reflectionFileTimstamp;
        if (!generateReflectionForProject(*p, file->content))
        {
            LogError() << "RTTI generation for project '" << p->mergedName << "' failed";
            valid = false;
        }
    }

    return valid.load();
}

struct ExportedDeclaration
{
    const CodeTokenizer::Declaration* declaration = nullptr;
    int priority = 0;
};

static void ExtractDeclarations(const ProjectReflection::RefelctionProject& p, std::vector<ExportedDeclaration>& outList)
{
    for (const auto* file : p.files)
    {
        for (const auto& decl : file->tokenized.declarations)
        {
            ExportedDeclaration info;
            info.declaration = &decl;

            if (decl.type == CodeTokenizer::DeclarationType::CUSTOM_TYPE)
                info.priority = 10;
            else if (decl.type == CodeTokenizer::DeclarationType::ENUM)
                info.priority = 20;
            else if (decl.type == CodeTokenizer::DeclarationType::BITFIELD)
                info.priority = 30;
            else if (decl.type == CodeTokenizer::DeclarationType::CLASS)
            {
                if (EndsWith(decl.name, "Metadata"))
                    info.priority = 40;
                else
                    info.priority = 41;
            }
            else if (decl.type == CodeTokenizer::DeclarationType::GLOBAL_FUNC)
                info.priority = 50;
			else if (decl.type == CodeTokenizer::DeclarationType::STRINGID)
				info.priority = 0;
			else if (decl.type == CodeTokenizer::DeclarationType::LOG_CHANNEL)
				info.priority = 1;

            outList.push_back(info);
        }
    }

    sort(outList.begin(), outList.end(), [](const ExportedDeclaration& a, const ExportedDeclaration& b)
        {
            if (a.priority != b.priority)
                return a.priority < b.priority;

            return a.declaration->name < b.declaration->name;
        });
}

bool ProjectReflection::generateReflectionForProject(const RefelctionProject& p, std::stringstream& f) const
{
    writeln(f, "/// RTTI Glue Code Generator");
    writeln(f, "/// AUTOGENERATED FILE - ALL EDITS WILL BE LOST");
    writeln(f, "");
    writeln(f, "// --------------------------------------------------------------------------------");
    writeln(f, "");
    writeln(f, "#include \"build.h\"");

    std::vector<ExportedDeclaration> declarations;
    ExtractDeclarations(p, declarations);

    std::unordered_set<std::string> uniqueLogChannels;
    std::unordered_set<std::string> uniqueNames;
    for (const auto& d : declarations)
    {
        if (d.declaration->type == CodeTokenizer::DeclarationType::GLOBAL_FUNC)
        {
            writelnf(f, "namespace %s { extern void RegisterGlobalFunc_%s(); }", d.declaration->scope.c_str(), d.declaration->name.c_str());
        }
        else if (d.declaration->type == CodeTokenizer::DeclarationType::LOG_CHANNEL)
        {
            if (uniqueLogChannels.insert(d.declaration->name).second)
            {
                writelnf(f, "namespace %s { TRACE_DEFINE_LOG_CHANNEL(%s); }", d.declaration->scope.c_str(), d.declaration->name.c_str());
            }
        }
		else if (d.declaration->type == CodeTokenizer::DeclarationType::STRINGID)
		{
            if (uniqueNames.insert(d.declaration->name).second)
            {
                writelnf(f, "namespace %s { DEFINE_STRING_ID(%s); }", d.declaration->scope.c_str(), d.declaration->name.c_str());
            }
		}
        else
        {
            writelnf(f, "namespace %s { extern void CreateType_%s(); }", d.declaration->scope.c_str(), d.declaration->name.c_str());
            writelnf(f, "namespace %s { extern void InitType_%s(int phase); }", d.declaration->scope.c_str(), d.declaration->name.c_str());
            writelnf(f, "namespace %s { extern void FinishType_%s(int phase); }", d.declaration->scope.c_str(), d.declaration->name.c_str());
        }
    }

    writeln(f, "");
    writeln(f, "// --------------------------------------------------------------------------------");
    writeln(f, "");

    writelnf(f, "void InitializeReflection_%s()", p.mergedName.c_str());
    writeln(f, "{");

    for (const auto& d : declarations)
    {
        if (d.declaration->type == CodeTokenizer::DeclarationType::LOG_CHANNEL)
        {
            if (uniqueLogChannels.insert(d.declaration->name).second)
            {
                writelnf(f, "TRACE_DEFINE_LOG_CHANNEL(%s::%s);", d.declaration->scope.c_str(), d.declaration->name.c_str());
            }
        }
        else if (d.declaration->type == CodeTokenizer::DeclarationType::STRINGID)
        {
            writelnf(f, "%s::InitStringID_%s();", d.declaration->scope.c_str(), d.declaration->name.c_str());
        }
    }

    for (const auto& d : declarations)
    {
        if (d.declaration->type == CodeTokenizer::DeclarationType::CLASS || d.declaration->type == CodeTokenizer::DeclarationType::CUSTOM_TYPE || d.declaration->type == CodeTokenizer::DeclarationType::ENUM || d.declaration->type == CodeTokenizer::DeclarationType::BITFIELD)
        {
            writelnf(f, "%s::CreateType_%s();", d.declaration->scope.c_str(), d.declaration->name.c_str());// , typeName.c_str());
        }
    }

    for (const auto& d : declarations)
    {
        if (d.declaration->type == CodeTokenizer::DeclarationType::CLASS || d.declaration->type == CodeTokenizer::DeclarationType::CUSTOM_TYPE || d.declaration->type == CodeTokenizer::DeclarationType::ENUM || d.declaration->type == CodeTokenizer::DeclarationType::BITFIELD)
        {
            writelnf(f, "%s::InitType_%s(0);",
                d.declaration->scope.c_str(), d.declaration->name.c_str());
        }
    }

    for (const auto& d : declarations)
    {
        if (d.declaration->type == CodeTokenizer::DeclarationType::CLASS || d.declaration->type == CodeTokenizer::DeclarationType::ENUM || d.declaration->type == CodeTokenizer::DeclarationType::BITFIELD || d.declaration->type == CodeTokenizer::DeclarationType::CUSTOM_TYPE)
            writelnf(f, "%s::FinishType_%s(0);", d.declaration->scope.c_str(), d.declaration->name.c_str());
    }

	for (const auto& d : declarations)
	{
		if (d.declaration->type == CodeTokenizer::DeclarationType::CLASS)
		{
			writelnf(f, "%s::InitType_%s(1);",
				d.declaration->scope.c_str(), d.declaration->name.c_str());
		}
	}

	for (const auto& d : declarations)
	{
		if (d.declaration->type == CodeTokenizer::DeclarationType::CLASS)
			writelnf(f, "%s::FinishType_%s(1);", d.declaration->scope.c_str(), d.declaration->name.c_str());
	}

    for (const auto& d : declarations)
    {
        if (d.declaration->type == CodeTokenizer::DeclarationType::GLOBAL_FUNC)
        {
            writelnf(f, "%s::RegisterGlobalFunc_%s();",
                d.declaration->scope.c_str(), d.declaration->name.c_str());
        }
    }

    writeln(f, "}");

    writeln(f, "");
    writeln(f, "// --------------------------------------------------------------------------------");
    writeln(f, "");

    writelnf(f, "void InitializeTests_%s()", p.mergedName.c_str());
    writeln(f, "{");
    writeln(f, "}");

    writeln(f, "// --------------------------------------------------------------------------------");
    writeln(f, "");

    return true;
}

//--

ToolReflection::ToolReflection()
{}

bool ToolReflection::runStatic(FileGenerator& fileGenerator, const std::vector<fs::path>& fileList, const std::string& projectName, const fs::path& outputFile)
{
	ProjectReflection reflection;
	if (!reflection.extractFromFileList(fileList, projectName, outputFile))
		return false;

	if (!reflection.filterProjects())
		return false;

	if (reflection.files.empty() && reflection.projects.empty())
		return true;

	if (!reflection.tokenizeFiles())
		return false;

	if (!reflection.parseDeclarations())
		return false;

	LogInfo() << "Generating reflection files...";

    return reflection.generateReflection(fileGenerator);
}

int ToolReflection::run(const Commandline& cmdline)
{
	ProjectReflection reflection;
    {
        std::string fileListPath = cmdline.get("list");
        if (fileListPath.empty())
        {
            LogInfo() << "Reflection file list must be specified by -list";
            return 1;
        }

        const auto readTlogPath = fs::path(cmdline.get("readTlog")).make_preferred();
        const auto writeTlogPath = fs::path(cmdline.get("writeTlog")).make_preferred();

        if (!reflection.extractFromCompactList(fs::path(fileListPath), readTlogPath, writeTlogPath))
            return 2;
    }

	if (!reflection.filterProjects())
        return 2;

    if (reflection.files.empty() && reflection.projects.empty())
    {
        LogInfo() << "Noting to update in reflection";
        return 0;
    }

	if (!reflection.tokenizeFiles())
		return 3;

	if (!reflection.parseDeclarations())
		return 4;

	LogInfo() << "Generating reflection files...";

    FileGenerator files;
	if (!reflection.generateReflection(files))
		return 5;

    if (!files.saveFiles())
        return 6;

	return 0;
}

//--