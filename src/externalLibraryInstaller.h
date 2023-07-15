#pragma once

#include "aws.h"

//--

struct AWSConfig;
struct Configuration;
class LibraryInstaller;

class ILibrarySource
{
public:
	virtual ~ILibrarySource();
	virtual bool install(const LibraryInstaller& installer, std::string_view name, fs::path* outInstalledPath, std::string* outInstalledVersion, std::unordered_set<std::string>* outRequiredSystemPacakges) const = 0;
};

class LibrarySourceAWSEndpoint : public ILibrarySource
{
public:
	LibrarySourceAWSEndpoint();
	virtual ~LibrarySourceAWSEndpoint();

	bool init(PlatformType platform, const std::string& endpoint);

	virtual bool install(const LibraryInstaller& installer, std::string_view name, fs::path* outInstalledPath, std::string* outInstalledVersion, std::unordered_set<std::string>* outRequiredSystemPacakges) const override;

private:
	AWSConfig m_aws;
	std::unordered_map<std::string, AWSLibraryInfo> m_libs;
};

class LibrarySourcePhysicalPackedDirectory : public ILibrarySource
{
public:
	LibrarySourcePhysicalPackedDirectory();
	virtual ~LibrarySourcePhysicalPackedDirectory();

	bool init(PlatformType platform, const fs::path& path);

	virtual bool install(const LibraryInstaller& installer, std::string_view name, fs::path* outInstalledPath, std::string* outInstalledVersion, std::unordered_set<std::string>* outRequiredSystemPacakges) const override;

private:
	struct OfflineLibrary
	{
		std::string name;
		std::string version;
		fs::path zipPath;
	};

	std::unordered_map<std::string, OfflineLibrary> m_libs;
};

class LibrarySourcePhysicalLooseDirectory : public ILibrarySource
{
public:
	LibrarySourcePhysicalLooseDirectory();
	virtual ~LibrarySourcePhysicalLooseDirectory();

	bool init(PlatformType platform, const fs::path& path);

	virtual bool install(const LibraryInstaller& installer, std::string_view name, fs::path* outInstalledPath, std::string* outInstalledVersion, std::unordered_set<std::string>* outRequiredSystemPacakges) const override;

private:
	struct OfflineLibrary
	{
		std::string name;
		std::string version;
		fs::path manifestPath;
	};

	std::unordered_map<std::string, OfflineLibrary> m_libs;
};

//--

class LibraryInstaller
{
public:
	LibraryInstaller(PlatformType platform, const fs::path& cacheDirectory);

	inline PlatformType platformType() const { return m_platform; }

	bool installOnlineAWSEndpointSource(const std::string& endpointAddress);
	bool installOfflinePackedDirectory(const fs::path& localLibraryDirectory);
	bool installOfflineLooseDirectory(const fs::path& localLibraryDirectory);

	fs::path buildLibraryDownloadPath(std::string_view name, std::string_view version) const;
	fs::path buildLibraryUnpackPath(std::string_view name, std::string_view version) const;
	fs::path buildLibraryManifestPath(std::string_view name, std::string_view version) const;

	bool install(std::string_view name, fs::path* outInstalledPath, std::string* outInstalledVersion, std::unordered_set<std::string>* outRequiredSystemPacakges) const;

protected:
	PlatformType m_platform;
	fs::path m_cachePath;

	std::vector<std::unique_ptr<ILibrarySource>> m_sources;

	void buildCoreLibraryPath(std::string_view name, std::string_view version, std::stringstream& str) const;
};

//--