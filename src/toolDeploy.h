#pragma once

#include "utils.h"
#include "project.h"

//--

class ToolDeploy
{
public:
    ToolDeploy();
    int run(const char* argv0, const Commandline& cmdline);
    void printUsage(const char* argv0);
};


//--