#include "common.h"
#include "aws.h"
#include "utils.h"
#include "externalLibrary.h"
#include "externalLibraryInstaller.h"

//--

ExternalLibraryInstaller::ExternalLibraryInstaller(AWSConfig& aws, PlatformType platform, const fs::path& cacheDirectory)
	: m_aws(aws)
	, m_platform(platform)
	 , m_cachePath(cacheDirectory)
{}

bool ExternalLibraryInstaller::collect()
{
	std::vector<AWSLibraryInfo> libs;
	if (!AWS_S3_ListLibraries(m_aws, m_platform, libs))
	{
		std::cerr << KRED << "[BREAKING] Unable to retrieve library listing from the AWS endpoint, supporting infrastructure seems to be broken. Please use offline libraries.\n" << RST;
		return false;
	}

	std::cout << "Discovered " << libs.size() << " third-party libraries for platform '" << NameEnumOption(m_platform) << "'\n";

	for (const auto& info : libs)
		m_libs[info.name] = info;

	return true;
}

void ExternalLibraryInstaller::buildCoreLibraryPath(std::string_view name, std::string_view version, std::stringstream& str) const
{
	str << m_cachePath.u8string();
	str << "/libraries/";
	str << NameEnumOption(m_platform);
	str << "/";
	str << name;
	str << "_";
	str << version;
	str << "/";
}

fs::path ExternalLibraryInstaller::buildLibraryDownloadPath(std::string_view name, std::string_view version) const
{
	std::stringstream str;
	buildCoreLibraryPath(name, version, str);
	str << name;
	str << ".zip";
	return fs::path(str.str()).make_preferred();
}

fs::path ExternalLibraryInstaller::buildLibraryUnpackPath(std::string_view name, std::string_view version) const
{
	std::stringstream str;
	buildCoreLibraryPath(name, version, str);
	str << "unpacked/";
	return fs::path(str.str()).make_preferred();
}

fs::path ExternalLibraryInstaller::buildLibraryManifestPath(std::string_view name, std::string_view version) const
{
	std::stringstream str;
	buildCoreLibraryPath(name, version, str);
	str << "unpacked/";
	str << name;
	str << ".onion";
	return fs::path(str.str()).make_preferred();
}

bool ExternalLibraryInstaller::install(std::string_view name, fs::path& outInstalledPath, std::string& outInstalledVersion, std::unordered_set<std::string>& outRequiredSystemPacakges) const
{
	const auto it = m_libs.find(std::string(name));
	if (it == m_libs.end())
	{
		std::cerr << KRED << "[BREAKING] Required third-party library '" << name << "' not found\n" << RST;
		return false;
	}

	const auto& info = it->second;

	// check if directory exists, if so, we assume library to be installed
	const auto cacheUnpackPath = buildLibraryUnpackPath(info.name, info.version);
	if (fs::is_directory(cacheUnpackPath))
	{
		const auto libraryManifestPath = buildLibraryManifestPath(info.name, info.version);
		if (!fs::is_regular_file(libraryManifestPath))
		{
			std::cerr << KYEL << "[WARNING] Install directory for third-party library '" << name << "' exists but does not contain the manifest, library will be unpacked again\n" << RST;
			fs::remove_all(cacheUnpackPath);
		}
		else
		{
			const auto libraryManifest = ExternalLibraryManifest::Load(libraryManifestPath);
			if (!libraryManifest)
			{
				std::cerr << KYEL << "[WARNING] Install directory for third-party library '" << name << "' exists and contains the manifest but it is invalid, library will be unpacked again\n" << RST;
				fs::remove_all(cacheUnpackPath);
			}
			else
			{
                for (const auto& name : libraryManifest->additionalSystemPackages)
                    outRequiredSystemPacakges.insert(name);

				outInstalledPath = libraryManifestPath;
				outInstalledVersion = info.version;
				std::cerr << KGRN << "Found already installed directory third-party library '" << name << "' at version '" << info.version << "\n" << RST;
				return true;
			}
		}
	}

	// if we don't have the download file get it now
	const auto cacheDownloadPath = buildLibraryDownloadPath(info.name, info.version);

	// download if not there
	if (!fs::is_regular_file(cacheDownloadPath))
	{
		// make sure library directory exists
		if (!CreateDirectories(cacheDownloadPath.parent_path()))
			return false;

		// curl --silent -z onion.exe -L -O https://github.com/BareMetalEngine/onion_tool/releases/latest/download/onion.exe
		std::stringstream cmd;
		cmd << "curl --silent -z ";
		cmd << cacheDownloadPath;
		cmd << " -L -o ";
		cmd << cacheDownloadPath;
		cmd << " ";
		cmd << info.url;

		if (!RunWithArgs(cmd.str()))
		{
			std::cerr << KRED << "[BREAKING] Failed to download file from '" << info.url << "\n" << RST;
			return false;
		}

		if (!fs::is_regular_file(cacheDownloadPath))
		{
			std::cerr << KRED << "[BREAKING] Third-part library '" << info.name << "' failed to download to " << cacheDownloadPath << "\n" << RST;
			return false;
		}

		std::cerr << KGRN << "Third-part library '" << info.name << "' downloaded\n" << RST;
	}
	else
	{
		std::cerr << KGRN << "Third-part library '" << info.name << "' already downloaded\n" << RST;
	}

	// unpack
	{
		// make sure library directory exists
		if (!CreateDirectories(cacheUnpackPath))
			return false;

		std::stringstream cmd;
		cmd << "tar -xvf ";
		cmd << cacheDownloadPath;
		cmd << " -C ";
		cmd << cacheUnpackPath;

		if (!RunWithArgs(cmd.str()))
		{
			std::cerr << KRED << "[BREAKING] Third-part library '" << info.name << "' failed to unpack to " << cacheUnpackPath << "\n" << RST;
			return false;
		}

		std::cerr << KGRN << "Third-part library '" << info.name << "' unpacked\n" << RST;
	}

	// verify
	const auto libraryManifestPath = buildLibraryManifestPath(info.name, info.version);
	{
		if (!fs::is_regular_file(libraryManifestPath))
		{
			std::cerr << KRED << "[BREAKING] Install directory for third-party library '" << name << "' exists but does not contain the manifest\n" << RST;
			return false;
		}

		const auto libraryManifest = ExternalLibraryManifest::Load(libraryManifestPath);
		if (!libraryManifest)
		{
			std::cerr << KRED << "[BREAKING] Install directory for third-party library '" << name << "' exists and contains the manifest but it is invalid\n" << RST;
			return false;
		}

        for (const auto& name : libraryManifest->additionalSystemPackages)
            outRequiredSystemPacakges.insert(name);
    }

	// valid third-party library
	outInstalledPath = libraryManifestPath;
	outInstalledVersion = info.version;
	std::cerr << KGRN << "Installed third-party library '" << name << "' at version '" << info.version << "\n" << RST;
	return true;
}

//--