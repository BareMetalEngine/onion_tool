#include "common.h"
#include "project.h"
#include "toolMake.h"
#include "toolReflection.h"
#include "toolEmbed.h"
#include "toolConfigure.h"
#include "toolBuild.h"
#include "toolLibrary.h"
#include "toolRelease.h"
#include "toolSign.h"
#include "toolGlueFiles.h"
#include "toolTest.h"
#include "toolDeploy.h"

static bool NeedsQuotes(std::string_view txt)
{
    for (const uint8_t ch : txt)
    {
        if (ch <= 32) return true;
        if (ch >= 127) return true;
    }

    return txt.empty();
}

static std::string MergeCommandline(int argc, char** argv)
{
    std::string ret;

    for (int i = 1; i < argc; ++i) {
        if (!ret.empty())
            ret += " ";

        const auto kv = SplitIntoKeyValue(argv[i]);
        if (kv.second.empty() || !NeedsQuotes(kv.second))
        {
            ret += argv[i];
        }
        else
        {
            ret += kv.first;
            ret += "=\"";
            ret += kv.second;
            ret += "\"";
        }
    }

    return ret;
}

static void PrintUsage()
{
    LogInfo() << "";
    LogInfo() << "---------------------------------------------------------";
    ToolConfigure().printUsage();
    LogInfo() << "---------------------------------------------------------";
    ToolMake().printUsage();
	LogInfo() << "---------------------------------------------------------";
	ToolBuild().printUsage();
	LogInfo() << "---------------------------------------------------------";
	ToolLibrary().printUsage();
	LogInfo() << "---------------------------------------------------------";
	ToolRelease().printUsage();
	LogInfo() << "---------------------------------------------------------";
	ToolDeploy().printUsage();
	LogInfo() << "---------------------------------------------------------";
	ToolTest().printUsage();
}

int main(int argc, char** argv)
{
#ifndef _WIN32
    //setvbuf(stdout, NULL, _IONBF, 0);
    //setvbuf(stderr, NULL, _IONBF, 0);
#endif

    Commandline cmdLine;
    if (!cmdLine.parse(MergeCommandline(argc, argv)) || cmdLine.commands.size() != 1)
    {
		LogInfo() << "Build Tool v1.0";
        PrintUsage();
        return 1;
    }

    if (!cmdLine.has("nologo"))
    {
        LogInfo() << "Build Tool v1.0";
    }

    const auto& tool = cmdLine.commands[0];
	if (tool == "configure")
	{
		ToolConfigure tool;
		return tool.run(cmdLine);
	}
    else if (tool == "make" || tool == "generate")
    {
        ToolMake tool;
        return tool.run(cmdLine);
    }	
    else if (tool == "reflection")
    {
        ToolReflection tool;
        return tool.run(cmdLine);
    }
	else if (tool == "embed")
	{
		ToolEmbed tool;
		return tool.run(cmdLine);
	}
	else if (tool == "build")
	{
		ToolBuild tool;
		return tool.run(cmdLine);
	}
	else if (tool == "library")
	{
		ToolLibrary tool;
		return tool.run(cmdLine);
	}
	else if (tool == "release")
	{
		ToolRelease tool;
		return tool.run(cmdLine);
	}
	else if (tool == "glue")
	{
		ToolGlueFiles tool;
		return tool.run(cmdLine);
	}
	else if (tool == "sign")
	{
		ToolSign tool;
		return tool.run(cmdLine);
	}
	else if (tool == "test")
	{
		ToolTest tool;
		return tool.run(cmdLine);
	}
	else if (tool == "deploy")
	{
		ToolDeploy tool;
		return tool.run(cmdLine);
	}
    else
    {
        LogError() << "Unknown tool specified";
        PrintUsage();
        return 1;
    }

    return 0;
}
