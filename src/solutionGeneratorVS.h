#pragma once

#include "solutionGenerator.h"

//--

class SolutionGeneratorVS : public SolutionGenerator
{
public:
    SolutionGeneratorVS(FileRepository& files, const Configuration& config, std::string_view mainGroup);

    virtual bool generateSolution(FileGenerator& gen, fs::path* outSolutionPath) override final;
    virtual bool generateProjects(FileGenerator& gen) override final;

private:
    fs::path m_visualStudioScriptsPath;

    const char* m_projectVersion = nullptr;
    const char* m_toolsetVersion = nullptr;

    bool generateSourcesProjectFile(const SolutionProject* project, std::stringstream& outContent) const;
    bool generateSourcesProjectFilters(const SolutionProject* project, std::stringstream& outContent) const;
    bool generateSourcesProjectFileEntry(const SolutionProject* project, const SolutionProjectFile* file, std::stringstream& f) const;

    bool generateRTTIGenProjectFile(const SolutionProject* project, const fs::path& reflectionListPath, std::stringstream& outContent) const;
    //bool generateEmbeddedMediaProjectFile(const SolutionProject* project, std::stringstream& outContent) const;

    void extractSourceRoots(const SolutionProject* project, std::vector<fs::path>& outPaths) const;

    void printSolutionDeclarations(std::stringstream& f, const SolutionGroup* g);
    void printSolutionParentLinks(std::stringstream& f, const SolutionGroup* g);
};

//--