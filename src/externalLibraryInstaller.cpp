#include "common.h"
#include "aws.h"
#include "utils.h"
#include "externalLibrary.h"
#include "externalLibraryInstaller.h"

//--

ILibrarySource::~ILibrarySource()
{}

//--

LibrarySourceAWSEndpoint::LibrarySourceAWSEndpoint()
	: m_aws(false)
{}

LibrarySourceAWSEndpoint::~LibrarySourceAWSEndpoint()
{}

bool LibrarySourceAWSEndpoint::init(PlatformType platform, const std::string& endpointAddress)
{
	Commandline cmdLine;
	if (!m_aws.init(cmdLine))
		return false;

	m_aws.endpoint = endpointAddress;

	std::vector<AWSLibraryInfo> libs;
	if (!AWS_S3_ListLibraries(m_aws, platform, libs))
	{
		LogError() << "Unable to retrieve library listing from the AWS endpoint '" << endpointAddress << "'";
		return false;
	}

	std::stringstream str;
	str << NameEnumOption(platform);

	LogInfo() << "Discovered " << libs.size() << " third-party libraries for platform '" << str.str() << "' at endpoint '" << endpointAddress << "'";

	for (const auto& info : libs)
		m_libs[info.name] = info;

	return true;
}

bool LibrarySourceAWSEndpoint::install(const LibraryInstaller& installer, std::string_view name, fs::path* outInstalledPath, std::string* outInstalledVersion, std::unordered_set<std::string>* outRequiredSystemPacakges) const
{
	const auto it = m_libs.find(std::string(name));
	if (it == m_libs.end())
		return false;

	const auto& info = it->second;

	// check if directory exists, if so, we assume library to be installed
	const auto cacheUnpackPath = installer.buildLibraryUnpackPath(info.name, info.version);
	if (fs::is_directory(cacheUnpackPath))
	{
		const auto libraryManifestPath = installer.buildLibraryManifestPath(info.name, info.version);
		if (!fs::is_regular_file(libraryManifestPath))
		{
			LogWarning() << "Install directory for third-party library '" << name << "' exists but does not contain the manifest, library will be unpacked again";
			fs::remove_all(cacheUnpackPath);
		}
		else
		{
			const auto libraryManifest = ExternalLibraryManifest::Load(libraryManifestPath);
			if (!libraryManifest)
			{
				LogWarning() << "Install directory for third-party library '" << name << "' exists and contains the manifest but it is invalid, library will be unpacked again";
				fs::remove_all(cacheUnpackPath);
			}
			else
			{
				libraryManifest->collectAdditionalSystemPackages(installer.platformType(), outRequiredSystemPacakges);

				*outInstalledPath = libraryManifestPath;
				*outInstalledVersion = info.version;
				LogSuccess() << "Found already installed directory third-party library '" << name << "' at version '" << info.version;
				return true;
			}
		}
	}

	// if we don't have the download file get it now
	const auto cacheDownloadPath = installer.buildLibraryDownloadPath(info.name, info.version);

	// download if not there
	if (!fs::is_regular_file(cacheDownloadPath))
	{
		// make sure library directory exists
		if (!CreateDirectories(cacheDownloadPath.parent_path()))
			return false;

		// curl --silent -z onion.exe -L -O https://...
		std::stringstream cmd;
		cmd << "curl --silent -z ";
		cmd << cacheDownloadPath;
		cmd << " -L -o ";
		cmd << cacheDownloadPath;
		cmd << " ";
		cmd << info.url;

		if (!RunWithArgs(cmd.str()))
		{
			LogError() << "Failed to download file from '" << info.url;
			return false;
		}

		if (!fs::is_regular_file(cacheDownloadPath))
		{
			LogError() << "Third-part library '" << info.name << "' failed to download to " << cacheDownloadPath;
			return false;
		}

		LogSuccess() << "Third-part library '" << info.name << "' downloaded";
	}
	else
	{
		LogSuccess() << "Third-part library '" << info.name << "' already downloaded";
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
			LogError() << "Third-part library '" << info.name << "' failed to unpack to " << cacheUnpackPath;
			return false;
		}

		LogSuccess() << "Third-part library '" << info.name << "' unpacked";
	}

	// verify
	const auto libraryManifestPath = installer.buildLibraryManifestPath(info.name, info.version);
	{
		if (!fs::is_regular_file(libraryManifestPath))
		{
			LogError() << "Install directory for third-party library '" << name << "' exists but does not contain the manifest";
			return false;
		}

		const auto libraryManifest = ExternalLibraryManifest::Load(libraryManifestPath);
		if (!libraryManifest)
		{
			LogError() << "Install directory for third-party library '" << name << "' exists and contains the manifest but it is invalid";
			return false;
		}

		libraryManifest->collectAdditionalSystemPackages(installer.platformType(), outRequiredSystemPacakges);
	}

	// valid third-party library
	*outInstalledPath = libraryManifestPath;
	*outInstalledVersion = info.version;
	LogSuccess() << "Installed third-party library '" << name << "' from external endpoint at version '" << info.version;
	return true;
}

//--

LibrarySourcePhysicalPackedDirectory::LibrarySourcePhysicalPackedDirectory()
{}

LibrarySourcePhysicalPackedDirectory::~LibrarySourcePhysicalPackedDirectory()
{}

bool LibrarySourcePhysicalPackedDirectory::init(PlatformType platform, const fs::path& path)
{
	try
	{
		const std::string filePrefix = std::string("lib_");
		const std::string filePostfix = std::string("_") + std::string(NameEnumOption(platform)) + ".zip";

		if (fs::is_directory(path))
		{
			for (const auto& entry : fs::directory_iterator(path))
			{
				const auto name = entry.path().filename().u8string();
		
				if (BeginsWith(name, filePrefix) && EndsWith(name, filePostfix))
				{
					const auto coreName = std::string(PartBefore(PartAfter(name, filePrefix), filePostfix));
					LogInfo() << "Found external packed library file '" << coreName << "' at " << entry.path();

					OfflineLibrary lib;
					lib.name = coreName;
					lib.zipPath = entry.path();
					lib.version = "offline_packed";
					m_libs[coreName] = lib;
				}
			}
		}
	}
	catch (fs::filesystem_error& e)
	{
		LogError() << "Filesystem Error: " << e.what();
		return false;
	}

	LogInfo() << "Found " << m_libs.size() << " packed offline libraries at " << path;
	return true;
}

bool LibrarySourcePhysicalPackedDirectory::install(const LibraryInstaller& installer, std::string_view name, fs::path* outInstalledPath, std::string* outInstalledVersion, std::unordered_set<std::string>* outRequiredSystemPacakges) const
{
	const auto it = m_libs.find(std::string(name));
	if (it == m_libs.end())
		return false;

	const auto& info = it->second;

	// check if directory exists, if so, we assume library to be installed
	const auto cacheUnpackPath = installer.buildLibraryUnpackPath(info.name, info.version);
	if (fs::is_directory(cacheUnpackPath))
	{
		const auto libraryManifestPath = installer.buildLibraryManifestPath(info.name, info.version);
		if (!fs::is_regular_file(libraryManifestPath))
		{
			LogWarning() << "Install directory for third-party library '" << name << "' exists but does not contain the manifest, library will be unpacked again";
			fs::remove_all(cacheUnpackPath);
		}
		else
		{
			const auto libraryManifest = ExternalLibraryManifest::Load(libraryManifestPath);
			if (!libraryManifest)
			{
				LogWarning() << "Install directory for third-party library '" << name << "' exists and contains the manifest but it is invalid, library will be unpacked again";
				fs::remove_all(cacheUnpackPath);
			}
			else
			{
				if (fs::last_write_time(info.zipPath) > fs::last_write_time(libraryManifestPath))
				{
					LogWarning() << "Install directory for third-party library '" << name << "' is older than the offline zip file, library will be unpacked again";
					fs::remove_all(cacheUnpackPath);
				}
				else
				{
					libraryManifest->collectAdditionalSystemPackages(installer.platformType(), outRequiredSystemPacakges);

					*outInstalledPath = libraryManifestPath;
					*outInstalledVersion = info.version;
					LogSuccess() << "Found already installed directory third-party library '" << name << "' at version '" << info.version;
					return true;
				}
			}
		}
	}

	// if we don't have the download file get it now
	const auto cacheDownloadPath = info.zipPath;
	if (!fs::is_regular_file(cacheDownloadPath))
	{
		LogError() << "Failed to find file '" << info.zipPath;
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
			LogError() << "Third-part library '" << info.name << "' failed to unpack to " << cacheUnpackPath;
			return false;
		}

		LogSuccess() << "Third-part library '" << info.name << "' unpacked";
	}

	// verify
	const auto libraryManifestPath = installer.buildLibraryManifestPath(info.name, info.version);
	{
		if (!fs::is_regular_file(libraryManifestPath))
		{
			LogError() << "Install directory for third-party library '" << name << "' exists but does not contain the manifest";
			return false;
		}

		const auto libraryManifest = ExternalLibraryManifest::Load(libraryManifestPath);
		if (!libraryManifest)
		{
			LogError() << "Install directory for third-party library '" << name << "' exists and contains the manifest but it is invalid";
			return false;
		}

		libraryManifest->collectAdditionalSystemPackages(installer.platformType(), outRequiredSystemPacakges);
	}

	// valid third-party library
	*outInstalledPath = libraryManifestPath;
	*outInstalledVersion = info.version;
	LogSuccess() << "Installed third-party library '" << name << "' at version '" << info.version;
	return true;
}

//--

LibrarySourcePhysicalLooseDirectory::LibrarySourcePhysicalLooseDirectory()
{}

LibrarySourcePhysicalLooseDirectory::~LibrarySourcePhysicalLooseDirectory()
{}

bool LibrarySourcePhysicalLooseDirectory::init(PlatformType platform, const fs::path& path)
{
	bool valid = true;

	try
	{
		if (fs::is_directory(path))
		{
			for (const auto& entry : fs::directory_iterator(path))
			{
				if (!entry.is_directory())
					continue;

				const auto name = entry.path().filename().u8string();

				const auto manifestPath = (path / name / "library.xml").make_preferred();

				if (fs::is_regular_file(manifestPath))
				{
					const auto manifest = ExternalLibraryManifest::Load(manifestPath);
					if (manifest)
					{
						LogInfo() << "Found external loose library file '" << manifest->name << "' at " << manifestPath;

						OfflineLibrary lib;
						lib.name = manifest->name;
						lib.manifestPath = manifestPath;
						lib.version = "offline_loose";
						m_libs[manifest->name] = lib;
					}
					else
					{
						valid = false;
					}
				}
			}
		}
	}
	catch (fs::filesystem_error& e)
	{
		LogError() << "Filesystem Error: " << e.what();
		return false;
	}

	LogInfo() << "Found " << m_libs.size() << " packed loose libraries at " << path;
	return true;
}

bool LibrarySourcePhysicalLooseDirectory::install(const LibraryInstaller& installer, std::string_view name, fs::path* outInstalledPath, std::string* outInstalledVersion, std::unordered_set<std::string>* outRequiredSystemPacakges) const
{
	const auto it = m_libs.find(std::string(name));
	if (it == m_libs.end())
		return false;

	const auto& info = it->second;

	// check if directory exists, if so, we assume library to be installed
	if (!fs::is_regular_file(info.manifestPath))
	{
		LogError() << "Install directory for third-party library '" << name << "' exists but does not contain the manifest";
		return false;
	}

	const auto libraryManifest = ExternalLibraryManifest::Load(info.manifestPath);
	if (!libraryManifest)
	{
		LogError() << "Install directory for third-party library '" << name << "' exists and contains the manifest but it is invalid";
		return false;
	}

	libraryManifest->collectAdditionalSystemPackages(installer.platformType(), outRequiredSystemPacakges);

	*outInstalledPath = info.manifestPath;
	*outInstalledVersion = info.version;
	LogSuccess() << "Found external third-party library '" << name << "' at version '" << info.version << " at " << info.manifestPath;
	return true;
}

//--

LibraryInstaller::LibraryInstaller(PlatformType platform, const fs::path& cacheDirectory)
	: m_platform(platform)
	, m_cachePath(cacheDirectory)
{}

void LibraryInstaller::buildCoreLibraryPath(std::string_view name, std::string_view version, std::stringstream& str) const
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

fs::path LibraryInstaller::buildLibraryDownloadPath(std::string_view name, std::string_view version) const
{
	std::stringstream str;
	buildCoreLibraryPath(name, version, str);
	str << name;
	str << ".zip";
	return fs::path(str.str()).make_preferred();
}

fs::path LibraryInstaller::buildLibraryUnpackPath(std::string_view name, std::string_view version) const
{
	std::stringstream str;
	buildCoreLibraryPath(name, version, str);
	str << "unpacked/";
	return fs::path(str.str()).make_preferred();
}

fs::path LibraryInstaller::buildLibraryManifestPath(std::string_view name, std::string_view version) const
{
	std::stringstream str;
	buildCoreLibraryPath(name, version, str);
	str << "unpacked/";
	str << name;
	str << ".onion";
	return fs::path(str.str()).make_preferred();
}

//--

bool LibraryInstaller::installOnlineAWSEndpointSource(const std::string& endpointAddress)
{
	auto source = std::make_unique<LibrarySourceAWSEndpoint>();
	if (!source->init(m_platform, endpointAddress))
		return false;

	m_sources.emplace_back(std::move(source));
	return true;
}

bool LibraryInstaller::installOfflinePackedDirectory(const fs::path& localLibraryDirectory)
{
	auto source = std::make_unique<LibrarySourcePhysicalPackedDirectory>();
	if (!source->init(m_platform, localLibraryDirectory))
		return false;

	m_sources.emplace_back(std::move(source));
	return true;
}

bool LibraryInstaller::installOfflineLooseDirectory(const fs::path& localLibraryDirectory)
{
	auto source = std::make_unique<LibrarySourcePhysicalLooseDirectory>();
	if (!source->init(m_platform, localLibraryDirectory))
		return false;

	m_sources.emplace_back(std::move(source));
	return true;
}

bool LibraryInstaller::install(std::string_view name, fs::path* outInstalledPath, std::string* outInstalledVersion, std::unordered_set<std::string>* outRequiredSystemPacakges) const
{
	for (const auto& source : m_sources)
		if (source->install(*this, name, outInstalledPath, outInstalledVersion, outRequiredSystemPacakges))
			return true;

	return false;
}

//--
