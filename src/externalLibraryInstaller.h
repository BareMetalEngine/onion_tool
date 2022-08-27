#pragma once

#include "aws.h"

//--

struct AWSConfig;
struct Configuration;

class ILibraryInstaller
{
public:
	ILibraryInstaller(PlatformType platform, const fs::path& cacheDirectory);
	virtual ~ILibraryInstaller() {};

	virtual bool collect(const Commandline& cmdLine) = 0;
	virtual bool install(std::string_view name, fs::path& outInstalledPath, std::string& outInstalledVersion, std::unordered_set<std::string>& outRequiredSystemPacakges) const = 0;

	static std::shared_ptr<ILibraryInstaller> MakeLibraryInstaller(PlatformType platform, const fs::path& cacheDirectory);

protected:
	PlatformType m_platform;
	fs::path m_cachePath;

	void buildCoreLibraryPath(std::string_view name, std::string_view version, std::stringstream& str) const;

	fs::path buildLibraryDownloadPath(std::string_view name, std::string_view version) const;
	fs::path buildLibraryUnpackPath(std::string_view name, std::string_view version) const;
	fs::path buildLibraryManifestPath(std::string_view name, std::string_view version) const;
};

class ExternalLibraryInstaller : public ILibraryInstaller
{
public:
	ExternalLibraryInstaller(PlatformType platform, const fs::path& cacheDirectory);

	virtual bool collect(const Commandline& cmdLine) override final;
	virtual bool install(std::string_view name, fs::path& outInstalledPath, std::string& outInstalledVersion, std::unordered_set<std::string>& outRequiredSystemPacakges) const override final;

private:
	AWSConfig m_aws;
	

	std::unordered_map<std::string, AWSLibraryInfo> m_libs;

	//--
};

class OfflineLibraryInstaller : public ILibraryInstaller
{
public:
	OfflineLibraryInstaller(PlatformType platform, const fs::path& cacheDirectory, const fs::path& offlinePath);

	virtual bool collect(const Commandline& cmdLine) override final;
	virtual bool install(std::string_view name, fs::path& outInstalledPath, std::string& outInstalledVersion, std::unordered_set<std::string>& outRequiredSystemPacakges) const override final;

private:
	fs::path m_offlinePath;

	struct OfflineLibrary
	{
		std::string name;
		std::string version;
		fs::path zipPath;
	};

	std::unordered_map<std::string, OfflineLibrary> m_libs;
};

//--