#pragma once

#define _CRT_SECURE_NO_WARNINGS

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <iostream>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string_view>
#include <algorithm>
#include <filesystem>
#include <atomic>

#include <assert.h>
#include <string.h>

enum class PlatformType : uint8_t {
    Windows,
    UWP,
    Linux,
    Scarlett,
    Prospero,
	iOS,
	Android,

    MAX,
};

enum class ConfigurationType : uint8_t {
    Debug,
    Checked,
    Release,
    Final, // release + even more code removed

    MAX,
};

enum class BuildType : uint8_t {
    Development, // includes development projects - editor, asset import, etc
    //Standalone, // glued standalone module files
    Shipment, // only final shipable executable (just game)

    MAX,
};

enum class LibraryType : uint8_t {
    Shared, // dlls
    Static, // static libs

    MAX,
};

enum class GeneratorType : uint8_t {
    VisualStudio19,
    VisualStudio22,
    CMake,

    MAX,
};

namespace fs = std::filesystem;

static inline const std::string_view MODULE_MANIFEST_NAME = "module.onion";
static inline const std::string_view PROJECT_MANIFEST_NAME = "project.onion";
static inline const std::string_view CONFIGURATION_NAME = ".configuration";
static inline const std::string_view BUILD_LIST_NAME = ".builds";

#ifndef _WIN32
#define _stricmp strcasecmp
#define vsprintf_s(x, size, txt, args) vsprintf(x, txt, args)
#define sprintf_s(x, size, txt, ...) sprintf(x, txt, __VA_ARGS__)
#endif

#ifdef _WIN32
    #define RST  ""
    #define KRED  ""
    #define KGRN  ""
    #define KYEL  ""
    #define KBLU  ""
    #define KMAG  ""
    #define KCYN  ""
    #define KWHT  ""
    #define KBOLD ""

    #define FRED(x) KRED x RST
    #define FGRN(x) KGRN x RST
    #define FYEL(x) KYEL x RST
    #define FBLU(x) KBLU x RST
    #define FMAG(x) KMAG x RST
    #define FCYN(x) KCYN x RST
    #define FWHT(x) KWHT x RST

    #define BOLD(x) "" x RST
    #define UNDL(x) "" x RST
#else
    #define RST  "\x1B[0m"
    #define KRED  "\x1B[31m"
    #define KGRN  "\x1B[32m"
    #define KYEL  "\x1B[33m"
    #define KBLU  "\x1B[34m"
    #define KMAG  "\x1B[35m"
    #define KCYN  "\x1B[36m"
    #define KWHT  "\x1B[37m"
    #define KBOLD "\x1B[1m"

    #define FRED(x) KRED x RST
    #define FGRN(x) KGRN x RST
    #define FYEL(x) KYEL x RST
    #define FBLU(x) KBLU x RST
    #define FMAG(x) KMAG x RST
    #define FCYN(x) KCYN x RST
    #define FWHT(x) KWHT x RST

    #define BOLD(x) "\x1B[1m" x RST
    #define UNDL(x) "\x1B[4m" x RST
#endif