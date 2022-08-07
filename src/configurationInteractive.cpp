#ifndef _MSC_VER
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#endif

#include "common.h"
#include "configuration.h"
#include "project.h"
#include "utils.h"

//--

#ifdef __APPLE__
#import <sys/proc_info.h>
#import <libproc.h>
#endif

#ifdef _MSC_VER
#include <Windows.h>
#include <conio.h>

static void ClearConsole()
{
    COORD topLeft = { 0, 0 };
    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO screen;
    DWORD written;

    GetConsoleScreenBufferInfo(console, &screen);
    FillConsoleOutputCharacterA(console, ' ', screen.dwSize.X * screen.dwSize.Y, topLeft, &written);
    FillConsoleOutputAttribute(console, FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_BLUE, screen.dwSize.X * screen.dwSize.Y, topLeft, &written);
    SetConsoleCursorPosition(console, topLeft);
}
#else
static void ClearConsole()
{
    std::cout << "\x1B[2J\x1B[H";
}

static struct termios old, current;

void initTermios() {
    tcgetattr(0, &old);
    current = old;
    current.c_lflag &= ~ICANON;
    current.c_lflag &= ~ECHO;
    tcsetattr(0, TCSANOW, &current);
}

void resetTermios() {
    tcsetattr(0, TCSANOW, &old);
}

char _getch()
{
    char ch;
    initTermios();
    ch = getchar();
    resetTermios();
    return ch;
}

#endif
#include "utils.h"

template< typename T >
bool ConfigEnum(T& option, const char* title)
{
    ClearConsole();

    int maxOption = (int)T::MAX;

    std::cout << title << std::endl;
    std::cout << std::endl;
    std::cout << "Options:\n";

    for (int i = 0; i < maxOption; ++i)
    {
        std::cout << "  " << (i + 1) << "): ";
        std::cout << NameEnumOption((T)i);

        if (option == (T)i)
            std::cout << " (current)";
        std::cout << std::endl;
    }
    std::cout << std::endl;
    std::cout << std::endl;

    std::cout << "Press (1-" << maxOption << ") to select option\n";
    std::cout << "Press (ENTER) to use current option (" << NameEnumOption(option) << ")\n";
    std::cout << "Press (ESC) to exit\n";

    for (;;)
    {
        auto ch = _getch();
        std::cout << "Code: " << (int)ch << std::endl;
        if (ch == 13 || ch == 10)
            return true;
        if (ch == 27)
            return false;

        if (ch >= '1' && ch < ('1' + maxOption))
        {
            option = (T)(ch - '1');
            return true;
        }
    }
}

static void PrintConfig(const Configuration& cfg)
{
    std::cout << "  Platform  : " << NameEnumOption(cfg.platform) << std::endl;
    std::cout << "  Generator : " << NameEnumOption(cfg.generator) << std::endl;
    std::cout << "  Libraries : " << NameEnumOption(cfg.libs) << std::endl;
    std::cout << "  Config    : " << NameEnumOption(cfg.configuration) << std::endl;
}

bool RunInteractiveConfig(Configuration& cfg, const fs::path& configPath)
{
    if (cfg.load(configPath))
    {
        std::cout << std::endl;
        std::cout << "Loaded existing configuration:" << std::endl;
        PrintConfig(cfg);
        std::cout << std::endl;
        std::cout << std::endl;
        std::cout << "Press (ENTER) to use" << std::endl;
        std::cout << "Press (ESC) to edit" << std::endl;

        for (;;)
        {
            auto ch = _getch();
            std::cout << "Code: " << (int)ch << std::endl;
            if (ch == 13 || ch == 10)
                return true;
            if (ch == 27)
                break;
        }
    }

    ClearConsole();
    if (!ConfigEnum(cfg.platform, "Select build platform:"))
        return false;

    if (cfg.platform == PlatformType::Windows || cfg.platform == PlatformType::UWP)
    {
        ClearConsole();
        if (!ConfigEnum(cfg.generator, "Select solution generator:"))
            return false;
    }
    else if (cfg.platform == PlatformType::Prospero || cfg.platform == PlatformType::Scarlett)
    {
        cfg.generator = GeneratorType::VisualStudio19;
    }
    else
    {
        cfg.generator = GeneratorType::CMake;
    }

    ClearConsole();
    if (!ConfigEnum(cfg.libs, "Select libraries type:"))
        return false;

    ClearConsole();
    if (!ConfigEnum(cfg.configuration, "Select configuration type:"))
        return false;

    ClearConsole();

    if (!cfg.save(configPath))
        std::cout << "Failed to save configuration!\n";

    std::cout << "Running with config:\n";
    PrintConfig(cfg);

    return true;
}

//--

bool RunWithArgs(std::string_view cmd, int* outCode /*= nullptr*/)
{
    std::cout << "Running: '" << cmd << "'\n";

    auto code = std::system(std::string(cmd).c_str());

    if (outCode)
        *outCode = code;

    if (code != 0)
    {
		std::cerr << KRED << "[BREAKING] Failed to run external command, exit code: " << code << "\n";
		return false;
    }

    return true;
}

bool RunWithArgsInDirectory(const fs::path& dir, std::string_view cmd, int* outCode /*= nullptr*/)
{
	// change to solution path
	std::error_code er;
	const auto rootPath = fs::current_path();
	fs::current_path(dir, er);
	if (er)
	{
		std::cerr << "[BREAKING] Failed to change directory to " << dir << ", error: " << er << "\n";
		return false;
	}

	// build with generator
	bool valid = true;
	try
	{
        valid = RunWithArgs(cmd, outCode);
	}
	catch (fs::filesystem_error& e)
	{
		std::cerr << KRED << "[EXCEPTION] File system Error: " << e.what() << "\n" << RST;
		valid = false;
	}
	catch (std::exception& e)
	{
		std::cerr << KRED << "[EXCEPTION] General Error: " << e.what() << "\n" << RST;
		valid = false;
	}

	// change back the path
	fs::current_path(rootPath, er);
	return valid;
}

bool RunWithArgsInDirectoryAndCaptureOutput(const fs::path& dir, std::string_view cmd, std::stringstream& outStr, int* outCode /*= nullptr*/)
{
	// change to solution path
	std::error_code er;
	const auto rootPath = fs::current_path();
	fs::current_path(dir, er);
	if (er)
	{
		std::cerr << "[BREAKING] Failed to change directory to " << dir << ", error: " << er << "\n";
		return false;
	}

	// build with generator
	bool valid = true;
	try
	{
		valid = RunWithArgsAndCaptureOutput(cmd, outStr, outCode);
	}
	catch (fs::filesystem_error& e)
	{
		std::cerr << KRED << "[EXCEPTION] File system Error: " << e.what() << "\n" << RST;
		valid = false;
	}
	catch (std::exception& e)
	{
		std::cerr << KRED << "[EXCEPTION] General Error: " << e.what() << "\n" << RST;
		valid = false;
	}

	// change back the path
	fs::current_path(rootPath, er);
	return valid;
}

bool RunWithArgsAndCaptureOutput(std::string_view cmd, std::stringstream& outStr, int* outCode /*= nullptr*/)
{
    std::cout << "Running: '" << cmd << "'\n";

#ifdef _WIN32
	FILE* f = _popen(std::string(cmd).c_str(), "r");
#else
    FILE* f = popen(std::string(cmd).c_str(), "r");
#endif
    if (!f)
    {
        std::cerr << KRED << "[BREAKING] popen failed\n";
        return false;
    }

    // capture input
    char buffer[1024];
	while (fgets(buffer, sizeof(buffer), f))
	{
        outStr << buffer;
	}

    // close
    if (!feof(f))
    {
		std::cerr << KRED << "[BREAKING] Failed to read popen pipe to the end\n";
		return false;
    }

    // done
#ifdef _WIN32
    auto code = _pclose(f);
#else
    auto code = pclose(f);
#endif

	if (outCode)
		*outCode = code;

    // the app itself may fail
    if (code != 0)
    {
        std::cerr << KRED << "[BREAKING] Failed to run external command, exit code: " << code << "\n";
		return false;
    }

    return true;
}

bool RunWithArgsAndCaptureOutputIntoLines(std::string_view cmd, std::vector<std::string>& outLines, int* outCode /*= nullptr*/)
{
    std::stringstream str;
    if (!RunWithArgsAndCaptureOutput(cmd, str, outCode))
        return false;

    const auto s = str.str();

    std::vector<std::string_view> lines;
    SplitString(s, "\n", lines);

    for (const auto& line : lines)
    {
        const auto txt = Trim(line);
        if (!txt.empty())
            outLines.emplace_back(txt);
    }

    return true;
}

std::string GetExecutablePath()
{
    char exepath[1024];
    memset(exepath, 0, sizeof(exepath));

#ifdef _WIN32
    GetModuleFileNameA(GetModuleHandle(NULL), exepath, sizeof(exepath));
#elif defined(__APPLE__)
    proc_pidpath(getpid(), exepath, sizeof(exepath));
    printf("path: %s\n", exepath);
#else
    char arg1[20];
    sprintf(arg1, "/proc/%d/exe", getpid());
    if (readlink(arg1, exepath, sizeof(exepath)) < 0)
	return "";
#endif

    return std::string(exepath);
}
