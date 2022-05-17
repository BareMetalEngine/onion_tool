#pragma once

//--

class ToolSign
{
public:
    ToolSign();

    int run(const char* argv0, const Commandline& cmdline);

private:
    fs::path m_envPath;
};

//--

