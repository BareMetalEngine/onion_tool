#pragma once

//--

class ToolBuild
{
public:
    ToolBuild();

    int run(const char* argv0, const Commandline& cmdline);
	void printUsage(const char* argv0);

private:
    fs::path m_envPath;
};

//--