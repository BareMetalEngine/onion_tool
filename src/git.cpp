#include "common.h"
#include "utils.h"
#include "json.h"
#include "git.h"

//--

GitHubConfig::GitHubConfig()
{
	path = fs::current_path();
}

bool GitHubConfig::init(const Commandline& cmdLine)
{
	fs::path initPath;

	if (cmdLine.has("gitPath"))
	{
		initPath = fs::weakly_canonical(fs::path(cmdLine.get("gitPath")).make_preferred());
		if (!fs::is_directory(path / ".git"))
		{
			std::cerr << KRED << "[BREAKING] Specified path " << initPath << " is not a Git repository\n" << RST;
			return false;
		}
	}
	else
	{
		initPath = FindRepoPath();
		if (initPath.empty())
		{
			std::cerr << KRED << "[BREAKING] Unable to find git repository in current or parent directories\n" << RST;
			return false;
		}
	}

	return init(initPath, cmdLine);
}

bool GitHubConfig::init(const fs::path& initPath, const Commandline& cmdLine)
{
	path = initPath;

	token = GetPublicToken(path);
	if (token.empty())
		return false;

	branch = GetBranchName(path);
	if (branch.empty())
		return false;

	head = GetHeadRef(path);
	user = GetRemoteUser(path);
	repo = GetRemoteRepo(path);
	remote = GetRemote(path);

	std::cout << "[GIT] Path: " << path << "\n";
	std::cout << "[GIT] Token: '" << token << "'\n";
	std::cout << "[GIT] Branch: '" << branch << "'\n";
	std::cout << "[GIT] Head: '" << head << "'\n";
	std::cout << "[GIT] Remote: '" << remote << "'\n";
	std::cout << "[GIT] User: '" << user << "'\n";
	std::cout << "[GIT] Repo: '" << repo << "'\n";

	return true;
}

std::string GitHubConfig::endpoint(std::string_view name) const
{
	std::stringstream txt;
	txt << "https://api.github.com/repos/";
	txt << user;
	txt << "/";
	txt << repo;
	txt << "/";
	txt << name;
	return txt.str();
}

std::string GitHubConfig::uploadEndpoint(std::string_view name) const
{
	std::stringstream txt;
	txt << "https://uploads.github.com/repos/";
	txt << user;
	txt << "/";
	txt << repo;
	txt << "/";
	txt << name;
	return txt.str();
}

fs::path GitHubConfig::FindRepoPath()
{
	auto searchPath = fs::current_path();
	while (fs::is_directory(searchPath))
	{
		if (fs::is_directory(searchPath / ".git"))
			return searchPath;

		const auto old = searchPath;
		searchPath = searchPath.parent_path();
		if (old == searchPath)
			break;
	}

	return fs::path();
}

std::string GitHubConfig::GetPublicToken(const fs::path& path)
{
	std::string token;
	{
		auto searchPath = fs::current_path();
		while (fs::is_directory(searchPath))
		{
			if (LoadFileToString(searchPath / ".gittoken", token))
				return token;

			const auto old = searchPath;
			searchPath = searchPath.parent_path();
			if (old == searchPath)
				break;
		}
	}

	if (!path.empty())
	{
		if (LoadFileToString(path / ".gittoken", token))
			return token;
	}

	const char* str = std::getenv("ONION_GIT_PUBLIC_TOKEN");
	if (str && *str)
		return str;

	std::cerr << KRED << "[BREAKING] Failed to retrieve GitHub access token\n" << RST;
	return "";
}

std::string GitHubConfig::GetBranchName(const fs::path& path)
{
	std::stringstream txt;
	if (!RunWithArgsInDirectoryAndCaptureOutput(path, "git branch --show-current", txt))
	{
		std::cerr << KRED << "[BREAKING] Failed to retrieve GitHub branch name\n" << RST;
		return "";
	}

	return std::string(Trim(txt.str()));
}

std::string GitHubConfig::GetHeadRef(const fs::path& path)
{
	std::stringstream hash;
	if (!RunWithArgsInDirectoryAndCaptureOutput(path, "git rev-parse --verify HEAD", hash))
	{
		std::cerr << KRED << "[BREAKING] Failed to fetch root hash from GitHub\n" << RST;
		return "";
	}

	return std::string(Trim(hash.str()));
}

std::string GitHubConfig::GetRemote(const fs::path& path)
{
	std::stringstream txt;
	if (!RunWithArgsInDirectoryAndCaptureOutput(path, "git config --get remote.origin.url", txt))
	{
		std::cerr << KRED << "[BREAKING] Failed to retrieve GitHub remote\n" << RST;
		return "";
	}

	return std::string(Trim(txt.str()));
}

std::string GitHubConfig::GetRemoteUser(const fs::path& path)
{
	const auto remote = GetRemote(path);
	auto name = PartAfter(remote, "github.com/");
	if (name.empty())
		name = PartAfter(remote, "git@github.com:");
	name = PartBefore(name, "/");
	return std::string(name);
}

std::string GitHubConfig::GetRemoteRepo(const fs::path& path)
{
	const auto remote = GetRemote(path);
	auto name = PartAfterLast(remote, "/");
	name = PartBefore(name, ".git");
	return std::string(name);
}

//--

SimpleJsonToken GitHubConfig::handleResult(std::string_view url, std::string_view result) const
{
	if (result.empty())
		return SimpleJson::Object();

	auto ret = SimpleJsonToken(SimpleJson::Parse(result));
	if (!ret)
	{
		std::cerr << KRED << "[BREAKING] GitHub API request returned invalid JSON: " << url << "\n" << RST;
		return nullptr;
	}

	if (const auto errors = ret["errors"])
	{
		for (const auto err : errors.values())
		{
			if (const auto code = err["code"])
			{
				std::cout << KRED << "GitHub API error: " << code.str() << "\n" << RST;
				return nullptr;
			}
		}
	}
	else if (const auto message = ret["message"])
	{
		std::cout << KRED << "GitHub API error message: " << message.str() << "\n" << RST;
		return nullptr;
	}

	std::cout << KGRN << "GitHub API request " << url << " returned valid JSON\n" << RST;
	return ret;
}

SimpleJsonToken GitHubConfig::get(std::string_view endpointName, const RequestArgs& args) const
{
	const auto url = endpoint(endpointName);

	std::stringstream txt;
	txt << "curl --silent ";
	txt << "-H \"Accept: application/vnd.github.v3+json\" ";
	txt << "-u \"" << token << "\" \"";
	txt << url;
	args.print(txt);
	txt << "\"";

	std::stringstream result;
	if (!RunWithArgsAndCaptureOutput(txt.str(), result))
	{
		std::cerr << KRED << "[BREAKING] GitHub API request failed: " << url << ": " << result.str() << "\n" << RST;
		return nullptr;
	}

	return handleResult(url, result.str());
}

SimpleJsonToken GitHubConfig::del(std::string_view endpointName, const RequestArgs& args) const
{
	const auto url = endpoint(endpointName);

	std::stringstream txt;
	txt << "curl --silent ";
	txt << "-X DELETE ";
	txt << "-H \"Accept: application/vnd.github.v3+json\" ";
	txt << "-u \"" << token << "\" \"";
	txt << url;
	args.print(txt);
	txt << "\"";

	std::stringstream result;
	if (!RunWithArgsAndCaptureOutput(txt.str(), result))
	{
		std::cerr << KRED << "[BREAKING] GitHub API request failed: " << url << ": " << result.str() << "\n" << RST;
		return nullptr;
	}

	return handleResult(url, result.str());
}

SimpleJsonToken GitHubConfig::post(std::string_view endpointName, const SimpleJson& json) const
{
	const auto url = endpoint(endpointName);

	std::stringstream txt;
	txt << "curl --silent ";
	txt << "-X POST ";
	txt << "-H \"Accept: application/vnd.github.v3+json\" ";
	txt << "-u \"" << token << "\" ";
	txt << "-d " << EscapeArgument(json.toString()) << " ";
	txt << url;

	std::stringstream result;
	if (!RunWithArgsAndCaptureOutput(txt.str(), result))
	{
		std::cerr << KRED << "[BREAKING] GitHub API request failed: " << url << ": " << result.str() << "\n" << RST;
		return nullptr;
	}

	return handleResult(url, result.str());
}

SimpleJsonToken GitHubConfig::patch(std::string_view endpointName, const SimpleJson& json) const
{
	const auto url = endpoint(endpointName);

	std::stringstream txt;
	txt << "curl --silent ";
	txt << "-X PATCH ";
	txt << "-H \"Accept: application/vnd.github.v3+json\" ";
	txt << "-u \"" << token << "\" ";
	txt << "-d " << EscapeArgument(json.toString()) << " ";
	txt << url;

	std::stringstream result;
	if (!RunWithArgsAndCaptureOutput(txt.str(), result))
	{
		std::cerr << KRED << "[BREAKING] GitHub API request failed: " << url << ": " << result.str() << "\n" << RST;
		return nullptr;
	}

	return handleResult(url, result.str());
}


SimpleJsonToken GitHubConfig::postFile(std::string_view endpointName, const RequestArgs& args, const fs::path& path) const
{
	const auto url = uploadEndpoint(endpointName);

	std::stringstream txt;
	txt << "curl --silent ";
	txt << "-X POST ";
	txt << "-H \"Accept: application/vnd.github.v3+json\" ";
	txt << "-H \"Content-Type: application/zip\" ";
	txt << "-u \"" << token << "\" \"";
	txt << url;
	args.print(txt);
	txt << "\" ";
	txt << "--data-binary @\"" << EscapeArgument(path.u8string()) << "\" ";

	std::stringstream result;
	if (!RunWithArgsAndCaptureOutput(txt.str(), result))
	{
		std::cerr << KRED << "[BREAKING] GitHub API request failed: " << url << ": " << result.str() << "\n" << RST;
		return nullptr;
	}

	return handleResult(url, result.str());
}

//--

bool GitApi_ListReleases(const GitHubConfig& git, std::vector<std::string>& outReleases)
{
	int pageIndex = 1;
	for (;;++pageIndex)
	{
		RequestArgs args;
		args.setNumber("per_page", 100);
		args.setNumber("page", pageIndex);

		const auto result = git.get("tags", args);
		if (!result)
		{
			std::cerr << KRED << "[BREAKING] GitHub API failed to get list of releases\n" << RST;
			return false;
		}

		if (result.values().empty())
		{
			std::cout << "Got empty result at page " << pageIndex << ", collected " << outReleases.size() << " release tags so far\n";
			break;
		}

		for (const auto& tag : result.values())
			if (const auto name = tag["tag_name"])
				outReleases.push_back(name.str());
	}

	return true;
}

bool GitApi_CopyReleaseInfo(const SimpleJsonToken& tag, GitReleaseInfo& outInfo)
{
	if (!tag["tag_name"] || !tag["id"])
	{
		std::cerr << KRED << "[BREAKING] GitHub API got invalid release data\n" << RST;
		return false;
	}

	outInfo.id = tag["id"].str();
	outInfo.name = tag["name"].str();
	outInfo.tag = tag["tag_name"].str();
	outInfo.body = tag["body"].str();
	outInfo.comitish = tag["target_commitish"].str();
	outInfo.createdAt = tag["created_at"].str();
	outInfo.publishedAt = tag["published_at"].str();
	outInfo.draft = (tag["draft"].str() == "true");
	outInfo.prerelease = (tag["prerelease"].str() == "true");
	outInfo.zipballUrl = (tag["zipball_url"].str() == "true");
	outInfo.tarballUrl = (tag["tarball_url"].str() == "true");
	return true;
}

bool GitApi_GetAllReleaseInfos(const GitHubConfig& git, std::vector<GitReleaseInfo>& outInfos)
{
	int pageIndex = 1;
	for (;; ++pageIndex)
	{
		RequestArgs args;
		args.setNumber("per_page", 100);
		args.setNumber("page", pageIndex);

		const auto result = git.get("releases", args);
		if (!result)
		{
			std::cerr << KRED << "[BREAKING] GitHub API failed to get list of releases\n" << RST;
			return false;
		}

		if (result.values().empty())
			break;

		for (const auto& tag : result.values())
		{
			if (const auto name = tag["tag_name"])
			{
				GitReleaseInfo outInfo;
				if (!GitApi_CopyReleaseInfo(tag, outInfo))
					return false;

				outInfos.push_back(outInfo);
			}
		}
	}

	return true;
}

bool GitApi_GetHighestReleaseNumber(const GitHubConfig& git, std::string_view prefix, uint32_t versionParts, uint32_t& outNumber)
{
	int pageIndex = 1;
	for (;; ++pageIndex)
	{
		RequestArgs args;
		args.setNumber("per_page", 100);
		args.setNumber("page", pageIndex);

		const auto result = git.get("tags", args);
		if (!result)
		{
			std::cerr << KRED << "[BREAKING] GitHub API failed to get list of releases\n" << RST;
			return false;
		}

		if (result.values().empty())
			break;
		
		for (const auto& tag : result.values())
		{
			if (const auto name = tag["name"])
			{
				if (BeginsWith(name.str(), prefix))
				{
					std::vector<std::string_view> parts;
					SplitString(name.str().c_str() + prefix.size(), ".", parts);

					if (parts.size() == versionParts)
					{
						uint32_t number = 0;
						if (1 == sscanf_s(std::string(parts[versionParts-1]).c_str(), "%u", &number))
						{
							if (number > outNumber)
							{
								outNumber = number;
							}
						}
					}
				}
			}
		}
	}

	return true;
}

bool GitApi_GetLatestReleaseName(const GitHubConfig& git, std::string& outReleaseName)
{
	const auto result = git.get("releases/latest");
	if (!result)
		return false;

	if (const auto name = result["tag_name"])
	{
		outReleaseName = name.str();
		return true;
	}

	std::cout << "No latest release\n";
	return false;
}

bool GitApi_GetReleaseInfoByTag(const GitHubConfig& git, std::string_view releasename, GitReleaseInfo& outInfo)
{
	std::stringstream url;
	url << "releases/tags/";
	url << releasename;

	const auto result = git.get(url.str());
	if (!GitApi_CopyReleaseInfo(result, outInfo))
		return false;

	return true;
}

bool GitApi_GetReleaseInfoById(const GitHubConfig& git, std::string_view id, GitReleaseInfo& outInfo)
{
	std::stringstream url;
	url << "releases/";
	url << id;

	const auto result = git.get(url.str());
	if (!GitApi_CopyReleaseInfo(result, outInfo))
		return false;

	return true;
}

bool GitApi_CreateRelease(const GitHubConfig& git, std::string_view tag, std::string_view name, std::string_view body, bool draft, bool prerelease, std::string& outReleaseId)
{
	SimpleJson json(SimpleJsonValueType::Object);
	json.set("tag_name", tag);
	json.set("target_commitish", git.branch);
	json.set("name", name);
	json.set("body", body);
	json.set("draft", draft ? "true" : "false");
	json.set("prerelease", prerelease ? "true" : "false");
	json.set("generate_release_notes", "false");

	const auto result = git.post("releases", json);
	if (!result)
	{
		std::cerr << KRED << "[BREAKING] GitHub API failed to create release\n" << RST;
		return false;
	}

	const auto id = result["id"];
	if (!id)
	{
		std::cerr << KRED << "[BREAKING] GitHub API failed to create release (invalid response)\n" << RST;
		return false;
	}

	outReleaseId = id.str();
	std::cout << KGRN << "GitHub release created at ID " << id << "\n" << RST;
	return true;
}

bool GitApi_ListReleaseArtifacts(const GitHubConfig& git, std::string_view id, std::vector<GitArtifactInfo>& outArtifacts)
{
	std::stringstream url;
	url << "releases/";
	url << id;
	url << "/assets";

	const auto result = git.get(url.str());
	if (!result)
		return false;

	for (const auto& artifact : result.values())
	{
		GitArtifactInfo info;
		info.id = artifact["id"].str();
		info.name = artifact["name"].str();
		info.state = artifact["state"].str();
		info.createdAt = artifact["created_at"].str();
		info.uploadedAt = artifact["updated_at"].str();
		info.size = atoi(artifact["size"].str().c_str());
		info.url = artifact["url"].str();

		if (!info.id.empty() && !info.name.empty())
			outArtifacts.push_back(info);
	}

	return true;
}

bool GitApi_PublishRelease(const GitHubConfig& git, std::string_view id)
{
	std::stringstream url;
	url << "releases/";
	url << id;

	SimpleJson json(SimpleJsonValueType::Object);
	json.set("draft", "false");

	const auto result = git.patch(url.str(), json);
	if (!result)
		return false;

	return true;
}

bool GitApi_DeleteRelease(const GitHubConfig& git, std::string_view id)
{
	std::stringstream url;
	url << "releases/";
	url << id;

	const auto result = git.del(url.str());
	if (!result)
		return false;

	return true;
}

bool GitApi_DeleteReleaseArtifact(const GitHubConfig& git, std::string_view id)
{
	std::stringstream url;
	url << "releases/assets/";
	url << id;

	const auto result = git.del(url.str());
	if (!result)
		return false;

	return true;
}

bool GitApi_UploadReleaseArtifact(const GitHubConfig& git, std::string_view id, std::string_view name, const fs::path& filePath)
{
	std::stringstream url;
	url << "releases/";
	url << id;
	url << "/assets";

	RequestArgs args;
	args.setText("name", name);

	const auto result = git.postFile(url.str(), args, filePath);
	if (!result)
		return false;

	{
		const auto id = result["id"];
		const auto size = result["size"];
		if (!id || !size)
		{
			std::cerr << KRED << "[BREAKING] GitHub API failed to verify asset upload (invalid response)\n" << RST;
			return false;
		}

		std::cout << "Asset '" << name << "' upload as ID " << id.str() << " (size: " << size.str() << ")\n";
	}

	return true;
}

//--
