#pragma once

#include <memory>
#include <string>

//--

class SimpleJson;
typedef std::shared_ptr<SimpleJson> SimpleJsonPtr;

struct SimpleJsonToken;
class RequestArgs;

class Commandline;

//--

enum class AWSEndpoint : uint8_t
{
	LIBS,
};

struct AWSConfig
{
	std::string secret;
	std::string key;

	AWSConfig();

	bool init(const Commandline& cmdLine);

	std::string_view endpoint(AWSEndpoint type) const;

	//--

	bool get(AWSEndpoint type, std::string path, const RequestArgs& args, std::string& outResult) const; // NO AUTHORIZATION

	//--

	static std::string GetSecret();
	static std::string GetKey();

};


extern void AWS_S3_PrintEndpoint(std::stringstream& str, std::string_view bucket, std::string_view region);
extern void AWS_S3_PrintObjectPath(std::stringstream& str, std::string_view bucket, std::string_view region, std::string_view key);

extern std::string AWS_S3_MakeEndpoint(std::string_view bucket, std::string_view region);
extern std::string AWS_S3_MakeObjectPath(std::string_view bucket, std::string_view region, std::string_view key);

extern bool AWS_S3_UploadFile(const AWSConfig& aws, const fs::path& file, std::string_view bucket, std::string_view region, std::string_view key);

//--

struct AWSLibraryInfo
{
	std::string name; // just the file name (zlib)
	std::string version; // ETag
	std::string url; // download url (full, with endpoint name)
};

extern bool AWS_S3_ListLibraries(const AWSConfig& aws, PlatformType platform, std::vector<AWSLibraryInfo>& outLibraries);
extern bool AWS_S3_UploadLibrary(const AWSConfig& aws, const fs::path& file, PlatformType platform, std::string_view name);

//--