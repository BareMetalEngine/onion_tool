#pragma once

//--

class ToolLibrary
{
public:
    ToolLibrary();

    int run(const Commandline& cmdline);
    void printUsage();

private:
    fs::path m_envPath;
};

//--