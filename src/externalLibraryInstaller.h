#pragma once

#include "aws.h"

//--

struct AWSConfig;
struct Configuration;

class ExternalLibraryInstaller
{
public:
	ExternalLibraryInstaller(AWSConfig& aws, PlatformType platform, const fs::path& cacheDirectory);

	bool collect();

	bool install(std::string_view name, fs::path& outInstalledPath, std::string& outInstalledVersion) const;

private:
	AWSConfig& m_aws;
	
	PlatformType m_platform;
	fs::path m_cachePath;

	std::unordered_map<std::string, AWSLibraryInfo> m_libs;

	//--

	void buildCoreLibraryPath(std::string_view name, std::string_view version, std::stringstream& str) const;

	fs::path buildLibraryDownloadPath(std::string_view name, std::string_view version) const;
	fs::path buildLibraryUnpackPath(std::string_view name, std::string_view version) const;
	fs::path buildLibraryManifestPath(std::string_view name, std::string_view version) const;
};

//--