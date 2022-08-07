#pragma once

//--

class ToolBuild
{
public:
    ToolBuild();

    int run(const Commandline& cmdline);
	void printUsage();

private:
    fs::path m_envPath;
};

//--