#pragma once

#include "utils.h"
#include "project.h"

//--

class ToolDeploy
{
public:
    ToolDeploy();
    int run(const Commandline& cmdline);
    void printUsage();
};


//--