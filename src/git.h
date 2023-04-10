#pragma once

#include <memory>
#include <string>

//--

class SimpleJson;
typedef std::shared_ptr<SimpleJson> SimpleJsonPtr;

struct SimpleJsonToken;
class RequestArgs;

//--

struct GitHubConfig
{
	fs::path path;
	std::string token;
	std::string user;
	std::string repo;
	std::string branch;
	std::string head;
	std::string remote;

	GitHubConfig();

	bool init(const fs::path& path, const Commandline& cmdLine);
	bool init(const Commandline& cmdLine);

	std::string endpoint(std::string_view name) const; // returns API endpoint for this project
	std::string uploadEndpoint(std::string_view name) const; // returns API endpoint for this project

	static fs::path FindRepoPath();

	static std::string GetPublicToken(const fs::path& path); // rexdex:gpg_...
	static std::string GetBranchName(const fs::path& path); // main
	static std::string GetHeadRef(const fs::path& path); // 0232352525235235352
	static std::string GetRemote(const fs::path& path); // https://github.com/Repo/project.git
	static std::string GetRemoteUser(const fs::path& path); // BareMetalEngine
	static std::string GetRemoteRepo(const fs::path& path); // onion

	//--

	SimpleJsonToken post(std::string_view endpoint, const SimpleJson& json) const;
	SimpleJsonToken patch(std::string_view endpoint, const SimpleJson& json) const;
	SimpleJsonToken get(std::string_view endpoint, const RequestArgs& args = RequestArgs()) const;
	SimpleJsonToken del(std::string_view endpoint, const RequestArgs& args = RequestArgs()) const;
	SimpleJsonToken postFile(std::string_view endpoint, const RequestArgs& args, const fs::path& path) const;

	SimpleJsonToken handleResult(std::string_view url, std::string_view result) const;

	//--
};

struct GitReleaseInfo
{
	std::string id;
	std::string tag;
	std::string name;
	std::string comitish;
	std::string body;	
	bool draft = false;
	bool prerelease = false;
	std::string createdAt;
	std::string publishedAt;
	std::string zipballUrl;
	std::string tarballUrl;
};

struct GitArtifactInfo
{
	std::string id;
	std::string name;
	std::string url;
	std::string state;
	uint64_t size = 0;
	std::string createdAt;
	std::string uploadedAt;
};

extern bool GitApi_ListReleases(const GitHubConfig& git, std::vector<std::string>& outReleasesTags);
extern bool GitApi_GetHighestReleaseNumber(const GitHubConfig& git, std::string_view prefix, uint32_t versionParts, uint32_t& outNumber);
extern bool GitApi_GetLatestReleaseName(const GitHubConfig& git, std::string& outReleaseName);
extern bool GitApi_GetReleaseInfoByTag(const GitHubConfig& git, std::string_view name, GitReleaseInfo& outInfo);
extern bool GitApi_GetReleaseInfoById(const GitHubConfig& git, std::string_view id, GitReleaseInfo& outInfo);
extern bool GitApi_GetAllReleaseInfos(const GitHubConfig& git, std::vector<GitReleaseInfo>& outInfos);
extern bool GitApi_CreateRelease(const GitHubConfig& git, std::string_view tag, std::string_view name, std::string_view body, bool draft, bool prerelease, std::string& outReleaseId);
extern bool GitApi_ListReleaseArtifacts(const GitHubConfig& git, std::string_view id, std::vector<GitArtifactInfo>& outArtifacts);
extern bool GitApi_PublishRelease(const GitHubConfig& git, std::string_view id);
extern bool GitApi_DeleteRelease(const GitHubConfig& git, std::string_view id);
extern bool GitApi_DeleteReleaseArtifact(const GitHubConfig& git, std::string_view id);
extern bool GitApi_UploadReleaseArtifact(const GitHubConfig& git, std::string_view id, std::string_view name, const fs::path& filePath);

//--