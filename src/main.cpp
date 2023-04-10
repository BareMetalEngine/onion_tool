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
    std::cout << "\n";
    std::cout << "---------------------------------------------------------\n";
    ToolConfigure().printUsage();
    std::cout << "---------------------------------------------------------\n";
    ToolMake().printUsage();
	std::cout << "---------------------------------------------------------\n";
	ToolBuild().printUsage();
	std::cout << "---------------------------------------------------------\n";
	ToolLibrary().printUsage();
	std::cout << "---------------------------------------------------------\n";
	ToolRelease().printUsage();
	std::cout << "---------------------------------------------------------\n";
	ToolDeploy().printUsage();
	std::cout << "---------------------------------------------------------\n";
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
		std::cout << "Build Tool v1.0\n";
        PrintUsage();
        return 1;
    }

    if (!cmdLine.has("nologo"))
    {
        std::cout << "Build Tool v1.0\n";
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
        std::cerr << "Unknown tool specified :(\n\n";
        PrintUsage();
        return 1;
    }

    return 0;
}
