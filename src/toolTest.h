#pragma once

//--

class ToolTest
{
public:
    ToolTest();

    int run(const char* argv0, const Commandline& cmdline);
	void printUsage();

private:    
};

//--