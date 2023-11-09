#pragma once

#include "codeParser.h"

//--

class FileGenerator;

struct ProjectReflection
{
    struct CompactProjectInfo
    {
		std::string name;
        std::string globalNamespace;
        std::string applicationSystemClasses;
        std::string vxprojFilePath; // input
        std::string sourceDirectoryPath; // input
        std::string reflectionFilePath; // output

		std::vector<fs::path> sourceFiles; // found
    };

    struct RefelctionFile
    {
        fs::path absolutePath;
        std::string globalNamespace;
        bool sourceFile = false;
        CodeTokenizer tokenized;
    };

    struct RefelctionProject
    {
        std::string mergedName;
        std::string globalNamespace;
        std::vector<RefelctionFile*> files;
        std::string applicationSystemClasses;
        fs::path reflectionFilePath;
        fs::file_time_type reflectionFileTimstamp;
    };

    std::vector<RefelctionFile*> files;
    std::vector<RefelctionProject*> projects;

    ~ProjectReflection();

    bool extractFromExpandedList(const fs::path& fileList);
    bool extractFromCompactList(const fs::path& fileList, const fs::path& outputReadTlog, const fs::path& outputWriteTlog);
    bool extractFromFileList(const std::vector<fs::path>& fileList, const std::string& projectName, const std::string& globalNamespace, const std::vector<std::string>& appSystemClassNames, const fs::path& outputFile);
    bool filterProjects();
    bool tokenizeFiles();
    bool parseDeclarations();
    bool generateReflection(FileGenerator& files) const;

    static bool LoadCompactProjectsFromFileList(const fs::path& inputFilePath, std::vector<CompactProjectInfo>& outCompactProjects);
    static bool CheckIfCompactListUpToDate(const fs::path& outputFilePath, const std::vector<CompactProjectInfo>& compactProjects);
    static bool CheckFileUpToDate(const fs::file_time_type& referenceTime, const fs::path& path);
    static bool GetFileTime(const fs::path& path, fs::file_time_type& outLastWriteTime);
    static bool CollectSourcesFromDirectory(const fs::path& dir, std::vector<fs::path>& outSources, fs::file_time_type& outTimeStamp);
    static void PrintExpandedFileList(std::stringstream& f, const std::vector<CompactProjectInfo>& compactProjects);
    static void PrintReadTlog(std::stringstream& f, const std::vector<CompactProjectInfo>& compactProjects);
    static void PrintWriteTlog(std::stringstream& f, const std::vector<CompactProjectInfo>& compactProjects);

private:
    bool generateReflectionForProject(const RefelctionProject& p, std::stringstream& f) const;
};

//--

class ToolReflection
{
public:
    ToolReflection();

    int run(const Commandline& cmdline);
    bool runStatic(FileGenerator& fileGenerator, const std::vector<fs::path>& fileList, const std::string& projectName, const std::string& globalNamespace, const std::vector<std::string>& appSystemClassNames, const fs::path& outputFile);
};

//--