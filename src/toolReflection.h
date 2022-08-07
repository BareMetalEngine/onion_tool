#pragma once

#include "codeParser.h"

//--

class FileGenerator;

struct ProjectReflection
{
    struct RefelctionFile
    {
        fs::path absoluitePath;
        CodeTokenizer tokenized;
    };

    struct RefelctionProject
    {
        std::string mergedName;
        std::vector<RefelctionFile*> files;
        fs::path reflectionFilePath;
        fs::file_time_type reflectionFileTimstamp;
    };

    std::vector<RefelctionFile*> files;
    std::vector<RefelctionProject*> projects;

    ~ProjectReflection();

    bool extractFromArgs(const std::string& fileList, const std::string& projectName, const fs::path& outputFile);
    bool extractFromFileList(const std::vector<fs::path>& fileList, const std::string& projectName, const fs::path& outputFile);
    bool filterProjects();
    bool tokenizeFiles();
    bool parseDeclarations();
    bool generateReflection(FileGenerator& files) const;

private:
    bool generateReflectionForProject(const RefelctionProject& p, std::stringstream& f) const;
};

//--

class ToolReflection
{
public:
    ToolReflection();

    int run(const Commandline& cmdline);
    bool runStatic(FileGenerator& fileGenerator, const std::vector<fs::path>& fileList, const std::string& projectName, const fs::path& outputFile);
};

//--