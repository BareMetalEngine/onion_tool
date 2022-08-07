#pragma once

#include "utils.h"
#include "project.h"

//--

class ToolMake
{
public:
    ToolMake();
    int run(const Commandline& cmdline);
    void printUsage();
};


//--