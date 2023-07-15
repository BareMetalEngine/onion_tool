#include "common.h"
#include "utils.h"
#include "toolRelease.h"
#include "externalLibrary.h"
#include "git.h"
#include "xmlUtils.h"

//--

std::unique_ptr<VersionInfo> VersionInfo::LoadConfig(const fs::path& manifestPath)
{
	std::string txt;
	if (!LoadFileToString(manifestPath, txt))
		return nullptr;

	XMLDoc doc;
	try
	{
		doc.parse<0>((char*)txt.c_str());
	}
	catch (std::exception& e)
	{
		LogError() << "Error parsing XML '" << manifestPath << "': " << e.what();
		return nullptr;
	}

	const auto* root = doc.first_node("Version");
	if (!root)
	{
		LogError() << "Manifest XML at '" << manifestPath << "' is not a version manifest";
		return nullptr;
	}

	auto ret = std::make_unique<VersionInfo>();

	bool valid = true;
	XMLNodeIterate(root, [&valid, &ret](const XMLNode* node, std::string_view option)
		{
			// TODO: filter

			if (option == "Prefix")
				ret->prefix = XMLNodeValue(node);
			else if (option == "MajorVersion")
				valid &= XMLNodeValueIntSafe(node, &ret->majorVersion);
			else if (option == "MinorVersion")
				valid &= XMLNodeValueIntSafe(node, &ret->minorVersion);
			else
			{
				LogError() << "Unknown version manifest option '" << option << "'";
				valid = false;
			}
		});

	if (!valid)
	{
		LogError() << "There were errors parsing project manifest from '" << manifestPath;
		return nullptr;
	}


	return ret;
}

//--

ToolRelease::ToolRelease()
{}

void ToolRelease::printUsage()
{
	LogInfo() << "onion release [options]";
	LogInfo() << "";
	LogInfo() << "General options:";
	LogInfo() << "  -action=[create|discard|publish|list]";
	LogInfo() << "  -versionFile=<path to file with major version numbers, defaults to .version>";
	LogInfo() << "";
}

bool Release_GetNextVersion(GitHubConfig& git, const Commandline& cmdline, std::string& outVersion)
{
	const auto defaultVersionFile = fs::weakly_canonical(fs::path(git.path / ".version").make_preferred());

	// load version information
	auto versionInfo = std::make_unique<VersionInfo>();
	if (cmdline.has("versionFile"))
	{
		const auto versionFilePath = fs::path(cmdline.get("versionFile"));

		versionInfo = VersionInfo::LoadConfig(versionFilePath);
		if (!versionInfo)
		{
			LogError() << "Failed to load version information from " << versionFilePath;
			return false;
		}
	}
	else if (fs::is_regular_file(defaultVersionFile))
	{
		versionInfo = VersionInfo::LoadConfig(defaultVersionFile);
		if (!versionInfo)
		{
			LogError() << "Failed to load default version information from " << defaultVersionFile;
			return false;
		}
	}

	// get current release number
	uint32_t releaseNumber = 0;
	GitApi_GetHighestReleaseNumber(git, versionInfo->prefix.c_str(), 3, releaseNumber);
	LogInfo() << "Current incremental release number is: " << releaseNumber << "";

	// assemble final version
	std::stringstream txt;
	txt << versionInfo->prefix;
	txt << versionInfo->majorVersion;
	txt << ".";
	txt << versionInfo->minorVersion;
	txt << ".";
	txt << (releaseNumber+1);

	outVersion = txt.str();
	return true;
}

static fs::path Release_ReleaseTokenFilePath(const GitHubConfig& git)
{
	const auto releaseTokenFilePath = fs::weakly_canonical(fs::path(git.path / ".release_token").make_preferred());
	return releaseTokenFilePath;
}

bool Release_GetCurrentReleaseId(const GitHubConfig& git, const Commandline& cmdline, std::string& outReleaseId)
{
	const auto releaseTokenFilePath = Release_ReleaseTokenFilePath(git);
	return LoadFileToString(releaseTokenFilePath, outReleaseId);
}

static bool Release_List(GitHubConfig& git, const Commandline& cmdline)
{
	std::vector<GitReleaseInfo> infos;
	if (!GitApi_GetAllReleaseInfos(git, infos))
	{
		LogError() << "Failed to list releases";
		return false;
	}

	std::string activeReleaseId;
	Release_GetCurrentReleaseId(git, cmdline, activeReleaseId);

	LogInfo() << "Found " << infos.size() << " release(s) in branch '" << git.branch << "'";

	for (const auto& info : infos)
	{
		LogInfo() << info.id << ": " << info.name << " (" << info.tag << ")";

		if (info.draft)
			LogInfo() << " (DRAFT)";

		if (info.id == activeReleaseId)
			LogInfo() << " (ACTIVE)";

		if (!info.comitish.empty())
			LogInfo() << " @" << info.comitish;

		LogInfo() << "";
	}

	return true;
}

static bool Release_Create(GitHubConfig& git, const Commandline& cmdline)
{
	// we have active release, we can't stare a new one
	std::string releaseId;
	if (Release_GetCurrentReleaseId(git, cmdline, releaseId))
	{
		LogError() << "There's active release with ID '" << releaseId << "', can't start a new one. Use -discard or -publish to finish the release.";
		return false;
	}

	// determine next version info
	std::string releaseName;
	if (!Release_GetNextVersion(git, cmdline, releaseName))
	{
		LogError() << "Unable to determine next release tag";
		return false;
	}

	// create DRAFT release
	if (!GitApi_CreateRelease(git, releaseName, releaseName, "Automatic release", true, false, releaseId))
	{
		LogError() << "Failed to create GitHub release '" << releaseName << "'";
		return false;
	}

	// store
	const auto releaseTokenFilePath = Release_ReleaseTokenFilePath(git);
	if (!SaveFileFromString(releaseTokenFilePath, releaseId, true, false))
	{
		LogError() << "Failed to save release token to " << releaseTokenFilePath;
		return false;
	}

	// started a release
	LogSuccess() << "Started release process for '" << releaseName << "' at id '" << releaseId << "'";
	return true;
}

static bool Release_Discard(GitHubConfig& git, const Commandline& cmdline)
{
	// we have active release, we can't stare a new one
	std::string releaseId;
	if (!Release_GetCurrentReleaseId(git, cmdline, releaseId))
	{
		LogError() << "There's no active release in progress";
		return false;
	}

	// get information about the release, it should be in a draft state
	GitReleaseInfo info;
	if (!GitApi_GetReleaseInfoById(git, releaseId, info))
	{
		LogError() << "ReleaseID '" << releaseId << "' stored in .release token is not a valid release";
		return false;
	}

	// release is no longer a draft
	if (!info.draft)
	{
		LogError() << "Release '" << releaseId << "' is no longer a draf release and cannot be discarded from this tool";
		return false;
	}

	// delete that release
	if (!GitApi_DeleteRelease(git, releaseId))
	{
		LogWarning() << "Release '" << releaseId << "' could not be deleted";
	}
	else
	{
		LogSuccess() << "Draft release " << releaseId << " was deleted";
	}

	// delete file on disk
	{
		std::error_code ec;
		const auto releaseTokenFilePath = Release_ReleaseTokenFilePath(git);
		if (fs::remove(releaseTokenFilePath, ec))
		{
			LogSuccess() << "Release token " << releaseTokenFilePath << " deleted";
		}
		else
		{
			LogWarning() << "Failed to delete release token " << releaseTokenFilePath;
		}
	}

	// ok
	return true;
}

static bool Release_Publish(GitHubConfig& git, const Commandline& cmdline)
{
	// we have active release, we can't stare a new one
	std::string releaseId;
	if (!Release_GetCurrentReleaseId(git, cmdline, releaseId))
	{
		LogError() << "There's no active release in progress";
		return false;
	}

	// get information about the release, it should be in a draft state
	GitReleaseInfo info;
	if (!GitApi_GetReleaseInfoById(git, releaseId, info))
	{
		LogError() << "ReleaseID '" << releaseId << "' stored in .release token is not a valid release";
		return false;
	}

	// release is no longer a draft
	if (!info.draft)
	{
		LogError() << "Release '" << releaseId << "' is no longer a draf release and cannot be discarded from this tool";
		return false;
	}

	// delete that release
	if (!GitApi_PublishRelease(git, releaseId))
	{
		LogWarning() << "Release '" << releaseId << "' could not be published";
	}
	else
	{
		LogSuccess() << "Draft release " << releaseId << " was published!";
	}

	// delete file on disk
	{
		std::error_code ec;
		const auto releaseTokenFilePath = Release_ReleaseTokenFilePath(git);
		if (fs::remove(releaseTokenFilePath, ec))
		{
			LogSuccess() << "Release token " << releaseTokenFilePath << " deleted";
		}
		else
		{
			LogWarning() << "Failed to delete release token " << releaseTokenFilePath;
		}
	}

	// ok
	return true;
}

static bool Release_AddArtifact(GitHubConfig& git, const Commandline& cmdline)
{
	// we have active release, we can't stare a new one
	std::string releaseId;
	if (!Release_GetCurrentReleaseId(git, cmdline, releaseId))
	{
		LogError() << "There's no active release in progress";
		return false;
	}

	// get path to file to publish
	const auto filePath = fs::weakly_canonical(cmdline.get("file"));
	if (!fs::is_regular_file(filePath))
	{
		LogError() << "File " << filePath << " does not exist, there's nothing to publish" << RST;
		return false;
	}

	// asset file name
	const auto assetFileName = filePath.filename().u8string();

	// list all current artifacts of the release
	std::vector<GitArtifactInfo> artifacts;
	if (!GitApi_ListReleaseArtifacts(git, releaseId, artifacts))
	{
		LogError() << "Failed to list git artifacts for release '" << releaseId;
		return false;
	}

	// check if we have existing deployment of such file
	{
		std::string matchingAssetID;
		for (const auto& info : artifacts)
		{
			if (info.name == assetFileName)
			{
				matchingAssetID = info.id;
				break;
			}
		}

		if (!matchingAssetID.empty())
		{
			LogWarning() << "Github Release Asset for '" << assetFileName << "' in release '" << releaseId << "' already found at ID " << matchingAssetID;
			return false;
		}
	}

	// push asset
	{
		if (!GitApi_UploadReleaseArtifact(git, releaseId, assetFileName, filePath))
		{
			LogError() << "Upload failed" << RST;
			return false;
		}
	}

	return true;
}

int ToolRelease::run(const Commandline& cmdline)
{
	const auto path = fs::absolute(fs::path(cmdline.get("releasePath", ".")).make_preferred());
	const auto action = cmdline.get("action", "");

	GitHubConfig git;
	if (!git.init(cmdline))
	{
		LogError() << "Failed to initialize GitHub helper";
		return 1;
	}

	// run action
	if (action == "create")
	{
		if (!Release_Create(git, cmdline))
			return 1;
	}
	else if (action == "discard")
	{
		if (!Release_Discard(git, cmdline))
			return 1;
	}
	else if (action == "publish")
	{
		if (!Release_Publish(git, cmdline))
			return 1;
	}
	else if (action == "list")
	{
		if (!Release_List(git, cmdline))
			return 1;
	}
	else if (action == "add")
	{
		if (!Release_AddArtifact(git, cmdline))
			return 1;
	}
	else
	{
		LogError() << "Unknown release action '" << action << "'";
		return 1;
	}

	return 0;
}

//--