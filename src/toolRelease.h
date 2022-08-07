#pragma once

struct VersionInfo
{
    std::string prefix = "v";
    int majorVersion = 1;
    int minorVersion = 0;
    int releaseVersion = 0;

    static std::unique_ptr<VersionInfo> LoadConfig(const fs::path& path);
};

//--

class ToolRelease
{
public:
    ToolRelease();

    int run(const Commandline& cmdline);
    void printUsage();

private:
    fs::path m_envPath;
};

//--

struct GitHubConfig;
extern bool Release_GetCurrentReleaseId(const GitHubConfig& git, const Commandline& cmdline, std::string& outReleaseId);
extern bool Release_GetNextVersion(GitHubConfig& git, const Commandline& cmdline, std::string& outVersion);

//--