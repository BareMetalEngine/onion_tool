#include "common.h"
#include "aws.h"
#include "utils.h"
#include "externalLibrary.h"
#include "externalLibraryInstaller.h"

//--

ILibraryInstaller::ILibraryInstaller(PlatformType platform, const fs::path& cacheDirectory)
	: m_platform(platform)
	, m_cachePath(cacheDirectory)
{}

void ILibraryInstaller::buildCoreLibraryPath(std::string_view name, std::string_view version, std::stringstream& str) const
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

fs::path ILibraryInstaller::buildLibraryDownloadPath(std::string_view name, std::string_view version) const
{
	std::stringstream str;
	buildCoreLibraryPath(name, version, str);
	str << name;
	str << ".zip";
	return fs::path(str.str()).make_preferred();
}

fs::path ILibraryInstaller::buildLibraryUnpackPath(std::string_view name, std::string_view version) const
{
	std::stringstream str;
	buildCoreLibraryPath(name, version, str);
	str << "unpacked/";
	return fs::path(str.str()).make_preferred();
}

fs::path ILibraryInstaller::buildLibraryManifestPath(std::string_view name, std::string_view version) const
{
	std::stringstream str;
	buildCoreLibraryPath(name, version, str);
	str << "unpacked/";
	str << name;
	str << ".onion";
	return fs::path(str.str()).make_preferred();
}

//--

ExternalLibraryInstaller::ExternalLibraryInstaller(PlatformType platform, const fs::path& cacheDirectory)
	: ILibraryInstaller(platform, cacheDirectory)
{
}

bool ExternalLibraryInstaller::collect(const Commandline& cmdLine)
{
	if (!m_aws.init(cmdLine))
		return false;

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

OfflineLibraryInstaller::OfflineLibraryInstaller(PlatformType platform, const fs::path& cacheDirectory, const fs::path& offlinePath)
	: ILibraryInstaller(platform, cacheDirectory)
	, m_offlinePath(offlinePath)
{}

bool OfflineLibraryInstaller::collect(const Commandline& cmdLine)
{
	try
	{
		const std::string filePrefix = std::string("lib_");
		const std::string filePostfix = std::string("_") + std::string(NameEnumOption(m_platform)) + ".zip";

		if (fs::is_directory(m_offlinePath))
		{
			for (const auto& entry : fs::directory_iterator(m_offlinePath))
			{
				const auto name = entry.path().filename().u8string();
				std::cout << "'" << name << "'\n";

				if (BeginsWith(name, filePrefix) && EndsWith(name, filePostfix))
				{
					const auto coreName = std::string(PartBefore(PartAfter(name, filePrefix), filePostfix));
					std::cout << "Found offline library file '" << coreName << "' at " << entry.path() << "\n";

					OfflineLibrary lib;
					lib.name = coreName;
					lib.zipPath = entry.path();
					lib.version = "offline";
					m_libs[coreName] = lib;
				}
			}
		}
	}
	catch (fs::filesystem_error& e)
	{
		std::cout << "Filesystem Error: " << e.what() << "\n";
		return false;
	}

	std::cout << "Found " << m_libs.size() << " offline libraries\n";
	return true;
}

bool OfflineLibraryInstaller::install(std::string_view name, fs::path& outInstalledPath, std::string& outInstalledVersion, std::unordered_set<std::string>& outRequiredSystemPacakges) const
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
				if (fs::last_write_time(info.zipPath) > fs::last_write_time(libraryManifestPath))
				{
					std::cerr << KYEL << "[WARNING] Install directory for third-party library '" << name << "' is older than the offline zip file, library will be unpacked again\n" << RST;
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
	}

	// if we don't have the download file get it now
	const auto cacheDownloadPath = info.zipPath;
	if (!fs::is_regular_file(cacheDownloadPath))
	{
		std::cerr << KRED << "[BREAKING] Failed to find file '" << info.zipPath << "\n" << RST;
		return false;
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

std::shared_ptr<ILibraryInstaller> ILibraryInstaller::MakeLibraryInstaller(PlatformType platform, const fs::path& cacheDirectory)
{
	if (const char* str = std::getenv("ONION_OFFLINE_LIB_DIRECTORY"))
	{
		std::cout << "Using offline libraries at '" << str << "'\n";
		return std::make_shared<OfflineLibraryInstaller>(platform, cacheDirectory, fs::path(str).make_preferred());
	}

	return std::make_shared<ExternalLibraryInstaller>(platform, cacheDirectory);
}

//--