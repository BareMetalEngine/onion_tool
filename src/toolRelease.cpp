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
		std::cerr << KRED << "[BREAKING] Error parsing XML '" << manifestPath << "': " << e.what() << "\n" << RST;
		return nullptr;
	}

	const auto* root = doc.first_node("Version");
	if (!root)
	{
		std::cerr << KRED << "[BREAKING] Manifest XML at '" << manifestPath << "' is not a version manifest\n" << RST;
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
				std::cerr << "Unknown version manifest option '" << option << "'\n";
				valid = false;
			}
		});

	if (!valid)
	{
		std::cerr << KRED << "[BREAKING] There were errors parsing project manifest from '" << manifestPath << "\n" << RST;
		return nullptr;
	}


	return ret;
}

//--

ToolRelease::ToolRelease()
{}

void ToolRelease::printUsage(const char* argv0)
{
	auto platform = DefaultPlatform();

	std::cout << KBOLD << "onion release [options]\n" << RST;
	std::cout << "\n";
	std::cout << "General options:";
	std::cout << "  -action=[create|discard|publish|list]\n";
	std::cout << "  -versionFile=<path to file with major version numbers, defaults to .version>\n";
	std::cout << "\n";
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
			std::cerr << KRED << "[BREAKING] Failed to load version information from " << versionFilePath << "\n" << RST;
			return false;
		}
	}
	else if (fs::is_regular_file(defaultVersionFile))
	{
		versionInfo = VersionInfo::LoadConfig(defaultVersionFile);
		if (!versionInfo)
		{
			std::cerr << KRED << "[BREAKING] Failed to load default version information from " << defaultVersionFile << "\n" << RST;
			return false;
		}
	}

	// get current release number
	uint32_t releaseNumber = 0;
	GitApi_GetHighestReleaseNumber(git, versionInfo->prefix.c_str(), 3, releaseNumber);
	std::cerr << "Current incremental release number is: " << releaseNumber << "\n";

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
		std::cerr << KRED << "[BREAKING] Failed to list releases\n" << RST;
		return false;
	}

	std::string activeReleaseId;
	Release_GetCurrentReleaseId(git, cmdline, activeReleaseId);

	std::cout << "Found " << infos.size() << " release(s) in branch '" << git.branch << "'\n";

	for (const auto& info : infos)
	{
		std::cout << info.id << ": " << info.name << " (" << info.tag << ")";

		if (info.draft)
			std::cout << " (DRAFT)";

		if (info.id == activeReleaseId)
			std::cout << " (ACTIVE)";

		if (!info.comitish.empty())
			std::cout << " @" << info.comitish;

		std::cout << "\n";
	}

	return true;
}

static bool Release_Create(GitHubConfig& git, const Commandline& cmdline)
{
	// we have active release, we can't stare a new one
	std::string releaseId;
	if (Release_GetCurrentReleaseId(git, cmdline, releaseId))
	{
		std::cerr << KRED << "[BREAKING] There's active release with ID '" << releaseId << "', can't start a new one. Use -discard or -publish to finish the release.\n" << RST;
		return false;
	}

	// determine next version info
	std::string releaseName;
	if (!Release_GetNextVersion(git, cmdline, releaseName))
	{
		std::cerr << KRED << "[BREAKING] Unable to determine next release tag\n" << RST;
		return false;
	}

	// create DRAFT release
	if (!GitApi_CreateRelease(git, releaseName, releaseName, "Automatic release", true, false, releaseId))
	{
		std::cerr << KRED << "[BREAKING] Failed to create GitHub release '" << releaseName << "'\n" << RST;
		return false;
	}

	// store
	const auto releaseTokenFilePath = Release_ReleaseTokenFilePath(git);
	if (!SaveFileFromString(releaseTokenFilePath, releaseId, true, false))
	{
		std::cerr << KRED << "[BREAKING] Failed to save release token to " << releaseTokenFilePath << "\n" << RST;
		return false;
	}

	// started a release
	std::cout << KGRN << "Started release process for '" << releaseName << "' at id '" << releaseId << "'\n" << RST;
	return true;
}

static bool Release_Discard(GitHubConfig& git, const Commandline& cmdline)
{
	// we have active release, we can't stare a new one
	std::string releaseId;
	if (!Release_GetCurrentReleaseId(git, cmdline, releaseId))
	{
		std::cerr << KRED << "[BREAKING] There's no active release in progress\n" << RST;
		return false;
	}

	// get information about the release, it should be in a draft state
	GitReleaseInfo info;
	if (!GitApi_GetReleaseInfoById(git, releaseId, info))
	{
		std::cerr << KRED << "[BREAKING] ReleaseID '" << releaseId << "' stored in .release token is not a valid release\n" << RST;
		return false;
	}

	// release is no longer a draft
	if (!info.draft)
	{
		std::cerr << KRED << "[BREAKING] Release '" << releaseId << "' is no longer a draf release and cannot be discarded from this tool\n" << RST;
		return false;
	}

	// delete that release
	if (!GitApi_DeleteRelease(git, releaseId))
	{
		std::cerr << KYEL << "[WARNING] Release '" << releaseId << "' could not be deleted\n" << RST;
	}
	else
	{
		std::cout << KGRN << "Draft release " << releaseId << " was deleted\n" << RST;
	}

	// delete file on disk
	{
		std::error_code ec;
		const auto releaseTokenFilePath = Release_ReleaseTokenFilePath(git);
		if (fs::remove(releaseTokenFilePath, ec))
		{
			std::cout << KGRN << "Release token " << releaseTokenFilePath << " deleted\n" << RST;
		}
		else
		{
			std::cerr << KYEL << "[WARNING] Failed to delete release token " << releaseTokenFilePath << "\n" << RST;
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
		std::cerr << KRED << "[BREAKING] There's no active release in progress\n" << RST;
		return false;
	}

	// get information about the release, it should be in a draft state
	GitReleaseInfo info;
	if (!GitApi_GetReleaseInfoById(git, releaseId, info))
	{
		std::cerr << KRED << "[BREAKING] ReleaseID '" << releaseId << "' stored in .release token is not a valid release\n" << RST;
		return false;
	}

	// release is no longer a draft
	if (!info.draft)
	{
		std::cerr << KRED << "[BREAKING] Release '" << releaseId << "' is no longer a draf release and cannot be discarded from this tool\n" << RST;
		return false;
	}

	// delete that release
	if (!GitApi_PublishRelease(git, releaseId))
	{
		std::cerr << KYEL << "[WARNING] Release '" << releaseId << "' could not be published\n" << RST;
	}
	else
	{
		std::cout << KGRN << "Draft release " << releaseId << " was published!\n" << RST;
	}

	// delete file on disk
	{
		std::error_code ec;
		const auto releaseTokenFilePath = Release_ReleaseTokenFilePath(git);
		if (fs::remove(releaseTokenFilePath, ec))
		{
			std::cout << KGRN << "Release token " << releaseTokenFilePath << " deleted\n" << RST;
		}
		else
		{
			std::cerr << KYEL << "[WARNING] Failed to delete release token " << releaseTokenFilePath << "\n" << RST;
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
		std::cerr << KRED << "[BREAKING] There's no active release in progress\n" << RST;
		return false;
	}

	// get path to file to publish
	const auto filePath = fs::weakly_canonical(cmdline.get("file"));
	if (!fs::is_regular_file(filePath))
	{
		std::cerr << KRED << "[BREAKING] File " << filePath << " does not exist, there's nothing to publish" << RST;
		return false;
	}

	// asset file name
	const auto assetFileName = filePath.filename().u8string();

	// list all current artifacts of the release
	std::vector<GitArtifactInfo> artifacts;
	if (!GitApi_ListReleaseArtifacts(git, releaseId, artifacts))
	{
		std::cerr << KRED << "[BREAKING] Failed to list git artifacts for release '" << releaseId << "\n" << RST;
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
			std::cout << "Github Release Asset for '" << assetFileName << "' in release '" << releaseId << "' already found at ID " << matchingAssetID << "\n";
			return false;
		}
	}

	// push asset
	{
		if (!GitApi_UploadReleaseArtifact(git, releaseId, assetFileName, filePath))
		{
			std::cerr << KRED << "[BREAKING] Upload failed" << RST;
			return false;
		}
	}

	return true;
}

int ToolRelease::run(const char* argv0, const Commandline& cmdline)
{
	const auto builderExecutablePath = fs::absolute(argv0);
	if (!fs::is_regular_file(builderExecutablePath))
	{
		std::cerr << KRED << "[BREAKING] Invalid local executable name: " << builderExecutablePath << "\n" << RST;
		return 1;
	}

	const auto path = fs::absolute(fs::path(cmdline.get("releasePath", ".")).make_preferred());
	const auto action = cmdline.get("action", "");

	GitHubConfig git;
	if (!git.init(cmdline))
	{
		std::cerr << KRED << "[BREAKING] Failed to initialize GitHub helper\n" << RST;
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
		std::cerr << KRED << "[BREAKING] Unknown release action '" << action << "'\n" << RST;
		return 1;
	}

	return 0;
}

//--