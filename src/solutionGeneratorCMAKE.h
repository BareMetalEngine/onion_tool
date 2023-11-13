#pragma once

#include "solutionGenerator.h"

//--

class SolutionGeneratorCMAKE : public SolutionGenerator
{
public:
    SolutionGeneratorCMAKE(FileRepository& files, const Configuration& config, std::string_view mainGroup);

	virtual bool generateSolution(FileGenerator& gen, fs::path* outSolutionPath) override final;
	virtual bool generateProjects(FileGenerator& gen) override final;

private:
    fs::path m_cmakeScriptsPath;

    fs::path m_platformToolchainFile;
    fs::path m_platformIncludeDirectory;

    bool initializePlatform();

    bool generateProjectFile(const SolutionProject* project, std::stringstream& outContent) const;

    bool shouldStaticLinkProject(const SolutionProject* project) const;
};

//--