#pragma once

//--

class ToolSign
{
public:
    ToolSign();

    int run(const Commandline& cmdline);

private:
    fs::path m_envPath;
};

//--

