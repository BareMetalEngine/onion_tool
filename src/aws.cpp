#include "common.h"
#include "utils.h"
#include "json.h"
#include "aws.h"
#include "xmlUtils.h"
#include <assert.h>

//--

AWSConfig::AWSConfig()
{
}

bool AWSConfig::init(const Commandline& cmdLine, const fs::path& referencePath)
{
	{
		secret = cmdLine.get("awsSecret");
		if (secret.empty())
		{
			secret = GetSecret(referencePath);
			if (secret.empty())
			{
				std::cerr << KRED << "[BREAKING] Unable to retrieve AWS secret\n" << RST;
				return false;
			}
		}
	}

	{
		key = cmdLine.get("awsKey");
		if (key.empty())
		{
			key = GetKey(referencePath);
			if (key.empty())
			{
				std::cerr << KRED << "[BREAKING] Unable to retrieve AWS key\n" << RST;
				return false;
			}
		}
	}

	return true;
}

std::string_view AWSConfig::endpoint(AWSEndpoint type) const
{
	switch (type)
	{
		case AWSEndpoint::LIBS: return "https://bare-metal-libs.s3.eu-central-1.amazonaws.com/";
		default: break;
	}

	return "";
}

std::string AWSConfig::GetSecret(const fs::path& referencePath)
{
	std::string token;
	{
		auto searchPath = fs::current_path();
		while (fs::is_directory(searchPath))
		{
			if (LoadFileToString(searchPath / ".awssecret", token))
				return token;

			const auto old = searchPath;
			searchPath = searchPath.parent_path();
			if (old == searchPath)
				break;
		}
	}

	if (!referencePath.empty())
	{
		if (LoadFileToString(referencePath / ".awssecret", token))
			return token;
	}

	const char* str = std::getenv("ONION_AWS_SECRET");
	if (str && *str)
		return str;

	std::cerr << KRED << "[BREAKING] Failed to retrieve AWS secret\n" << RST;
	return "";
}

std::string AWSConfig::GetKey(const fs::path& referencePath)
{
	std::string token;
	{
		auto searchPath = fs::current_path();
		while (fs::is_directory(searchPath))
		{
			if (LoadFileToString(searchPath / ".awskey", token))
				return token;

			const auto old = searchPath;
			searchPath = searchPath.parent_path();
			if (old == searchPath)
				break;
		}
	}

	if (!referencePath.empty())
	{
		if (LoadFileToString(referencePath / ".awskey", token))
			return token;
	}

	const char* str = std::getenv("ONION_AWS_KEY");
	if (str && *str)
		return str;

	std::cerr << KRED << "[BREAKING] Failed to retrieve AWS key\n" << RST;
	return "";
}

//--

/*SimpleJsonToken GitHubConfig::handleResult(std::string_view url, std::string_view result) const
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
}*/

bool AWSConfig::get(AWSEndpoint type, std::string path, const RequestArgs& args, std::string& outResult) const
{
	std::stringstream txt;
	txt << "curl --silent ";
	//txt << "-H \"Accept: application/vnd.github.v3+json\" ";
	//txt << "-u \"" << token << "\"
	txt << "\"";
	txt << endpoint(type);
	txt << path;
	args.print(txt);
	txt << "\"";

	std::stringstream result;
	if (!RunWithArgsAndCaptureOutput(txt.str(), result))
	{
		std::cerr << KRED << "[BREAKING] AWS request failed: " << path << ": " << result.str() << "\n" << RST;
		return false;
	}

	//return handleResult(url, result.str());
	outResult = result.str();
	return true;
}

//--

static std::string BuildLibraryPrefixForPlatform(PlatformType platform)
{
	std::stringstream str;
	str << "libraries/";
	str << NameEnumOption(platform);
	str << "/";
	return str.str();
}

static std::string BuildLibraryDownloadURL(const AWSConfig& aws, std::string_view key)
{
	std::stringstream str;
	str << aws.endpoint(AWSEndpoint::LIBS);
	str << key;
	return str.str();
}

bool AWS_S3_ListLibraries(const AWSConfig& aws, PlatformType platform, std::vector<AWSLibraryInfo>& outFiles)
{
	RequestArgs args;

	std::string text;
	if (!aws.get(AWSEndpoint::LIBS, "", args, text))
	{
		std::cerr << KRED << "[BREAKING] AWS failed to get list of objects in bucket\n" << RST;
		return false;
	}

	XMLDoc doc;
	try
	{
		doc.parse<0>((char*)text.c_str());
	}
	catch (std::exception& e)
	{
		std::cout << KRED << "[BREAKING] Error parsing returned XML from AWS: " << e.what() << "\n" << RST;
		return false;
	}

	const auto* root = doc.first_node("ListBucketResult");
	if (!root)
	{
		std::cout << "AWS returned XML at is not a valid result for ListBucketResult \n";
		return false;
	}

	if (!root->first_node("Contents"))
	{
		std::cout << "AWS returned XML does not contain any files\n";
		return false;
	}

	const auto prefix = BuildLibraryPrefixForPlatform(platform);	

	XMLNodeIterate(root, "Contents", [&outFiles, &prefix, &aws](const XMLNode* node)
		{
			const auto key = XMLChildNodeValue(node, "Key");
			const auto tag = XMLChildNodeValue(node, "ETag");
			const auto size = XMLChildNodeValue(node, "Size");
			if (key.empty() || tag.empty())
				return;

			if (size.empty() || size == "0")
				return;

			if (!BeginsWith(key, prefix))
				return;

			const auto name = fs::path(key).stem().u8string();
			if (name.empty())
				return;

			AWSLibraryInfo info;
			info.name = name;
			info.version = TrimQuotes(tag);
			info.url = BuildLibraryDownloadURL(aws, key);
			outFiles.push_back(info);
		});

	return true;
}

static const char* DAY_OF_WEEK_NAME[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char* MONTH_NAME[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

struct AWSCanonicalDateTime
{
	tm today;

	AWSCanonicalDateTime()
	{
		time_t now;
		time(&now);
#ifdef _WIN32
		::gmtime_s(&today, &now);
#else
        ::gmtime_r(&now, &today);
#endif
	}

	AWSCanonicalDateTime(const AWSCanonicalDateTime& other)
	{
		memcpy(&today, &other.today, sizeof(today));
	}

	AWSCanonicalDateTime& operator=(const AWSCanonicalDateTime& other)
	{
		memcpy(&today, &other.today, sizeof(today));
		return *this;
	}

	std::string dateStringHTML() const
	{
		const char* dayOfWeekName = DAY_OF_WEEK_NAME[today.tm_wday];
		const char* monthName = MONTH_NAME[today.tm_mon];

		char buffer[256];
		sprintf_s(buffer, sizeof(buffer), "%s, %02d %s %04d %02d:%02d:%02d GMT",
			dayOfWeekName,
			today.tm_mday, monthName, today.tm_year + 1900,
			today.tm_hour, today.tm_min, today.tm_sec);

		return buffer;
	}

	std::string dateString() const
	{
		char buffer[256];
		sprintf_s(buffer, sizeof(buffer), "%04d%02d%02d",
			today.tm_year + 1900, today.tm_mon + 1, today.tm_mday);
		return buffer;
	}

	std::string dateTimeString() const
	{
		char buffer[256];
		sprintf_s(buffer, sizeof(buffer), "%04d%02d%02dT%02d%02d%02dZ",
			today.tm_year + 1900, today.tm_mon + 1, today.tm_mday,
			today.tm_hour, today.tm_min, today.tm_sec);

		return buffer;
	}
};

static void ExtractHostFromURI(std::string_view uri, std::string& outHost, std::string& outUri)
{
	auto prefix = PartAfter(uri, "://");
	outHost = PartBefore(prefix, "/");
	outUri = std::string("/") + std::string(PartAfter(prefix, "/"));
}

class AWSCannonicalRequest
{
public:
	AWSCannonicalRequest(std::string_view method, const AWSCanonicalDateTime& dt, std::string_view url, std::string_view contentHash)
		: m_uri(url)
		, m_dateTime(dt)
		, m_method(method)
		, m_hashedPayload(contentHash)
	{
		ExtractHostFromURI(url, m_host, m_uri);

		//m_headers.setText("Date", m_dateTime.dateStringHTML());
		m_headers.setText("Host", m_host);
		m_headers.setText("x-amz-date", m_dateTime.dateTimeString());
		m_headers.setText("x-amz-content-sha256", m_hashedPayload);
	}

	AWSCannonicalRequest& setHeader(std::string_view key, std::string_view value)
	{
		m_headers.setText(key, value);
		return *this;
	}
	
	void printSignedHeaders(std::stringstream& str) const
	{
		m_headers.printHeaderNames(str);
	}

	// https://docs.aws.amazon.com/AmazonS3/latest/API/sig-v4-header-based-auth.html
	std::string print() const
	{
		std::stringstream str;
		
		// <HTTPMethod>\n
		str << m_method << "\n";

		// <CanonicalURI>\n
		PrintURL(str, m_uri);
		str << "\n";

		// <CanonicalQueryString>\n
		m_query.printUri(str);
		str << "\n";

		// <CanonicalHeaders>\n
		m_headers.printHeader(str);
		str << "\n";

		// <SignedHeaders>\n
		m_headers.printHeaderNames(str);
		str << "\n";


		// <HashedPayload>
		str << m_hashedPayload;

		return str.str();
	}

	inline const AWSCanonicalDateTime& dateTime() const
	{
		return m_dateTime;
	}

	inline std::string_view contentHash() const
	{
		return m_hashedPayload;
	}

	inline std::string_view host() const
	{
		return m_host;
	}

	inline std::string method() const
	{
		return m_method;
	}

private:
	std::string m_method;
	std::string m_uri;
	std::string m_host;
	RequestArgs m_query;
	RequestArgs m_headers;
	std::string m_hashedPayload;
	AWSCanonicalDateTime m_dateTime;
};



// https://docs.aws.amazon.com/AmazonS3/latest/API/sig-v4-header-based-auth.html
std::string AWS_StringToSign(const AWSCannonicalRequest& req, std::string_view region)
{
	std::stringstream str;
	str << "AWS4-HMAC-SHA256" << "\n";
	str << req.dateTime().dateTimeString() << "\n";
	str << req.dateTime().dateString() << "/" << region << "/s3/aws4_request\n";

	const auto cannonicalHeader = req.print();
	str << Sha256OfText(cannonicalHeader);

	return str.str();
}

std::string AWS_SigingKey(const AWSCannonicalRequest& req, std::string_view region, std::string_view secret)
{
	const auto awsKey = std::string("AWS4") + std::string(secret);

	const auto t0 = hmac_sha256_binstr(awsKey, req.dateTime().dateString());
	const auto t1 = hmac_sha256_binstr(t0, region);
	const auto t2 = hmac_sha256_binstr(t1, "s3");
	const auto t3 = hmac_sha256_binstr(t2, "aws4_request");
	
	return t3;
}

void AWS_S3_PrintEndpoint(std::stringstream& str, std::string_view bucket, std::string_view region)
{
	str << "http://";
	str << bucket;
	str << ".s3.";
	str << region;
	str << ".amazonaws.com/";
}

void AWS_S3_PrintObjectPath(std::stringstream& str, std::string_view bucket, std::string_view region, std::string_view key)
{
	AWS_S3_PrintEndpoint(str, bucket, region);
	str << key;
}

std::string AWS_S3_MakeEndpoint(std::string_view bucket, std::string_view region)
{
	std::stringstream str;
	AWS_S3_PrintEndpoint(str, bucket, region);
	return str.str();
}

std::string AWS_S3_MakeObjectPath(std::string_view bucket, std::string_view region, std::string_view key)
{
	std::stringstream str;
	AWS_S3_PrintObjectPath(str, bucket, region, key);
	return str.str();
}

std::string AWS_MakeAuthoridzationString(std::string_view region, const AWSCannonicalRequest& req, const AWSConfig& config)
{
	std::stringstream str;
	str << "AWS4-HMAC-SHA256 ";

	str << "Credential=" << config.key;
	str << "/" << req.dateTime().dateString();
	str << "/" << region;
	str << "/s3/aws4_request";

	str << ",";
	str << "SignedHeaders=";
	req.printSignedHeaders(str);

	str << ",";
	str << "Signature=";

	/*{
		const auto awsKey = std::string("AWS4") + std::string("wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY");

		const auto t0 = hmac_sha256_binstr(awsKey, "20150830");
		const auto t1 = hmac_sha256_binstr(t0, "us-east-1");
		const auto t2 = hmac_sha256_binstr(t1, "iam");
		const auto t3 = hmac_sha256_binstr(t2, "aws4_request");

		const auto hex = BytesToHexString(t3);
		assert(hex == "c4afb1cc5771d871763a393e44b703571b55cc28424d1a5e86da6ed3c154a4b9");

		const auto stringToSign = "AWS4-HMAC-SHA256\n20150830T123600Z\n20150830/us-east-1/iam/aws4_request\nf536975d06c0309214f805bb90ccff089219ecd68b2577efef23edd43b7e1a59";
			
		const auto result = hmac_sha256_str(t3, stringToSign);
		assert(result == "5d672d79c15b13162d9279b0855cfba6789a8edb4c82c400e06b5924a6f2b5d7");
	}*/
	
	const auto stringToSign = AWS_StringToSign(req, region);
	const auto signingKey = AWS_SigingKey(req, region, config.secret);

	/*{
		const auto stringToSignLocal = "AWS4-HMAC-SHA256\n20130524T000000Z\n20130524/us-east-1/s3/aws4_request\n9e0e90d9c76de8fa5b200d8c849cd5b8dc7a3be3951ddb7f6a76b4158342019d";
		assert(stringToSign == stringToSignLocal);
		const auto awsKey = std::string("AWS4") + std::string("wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY");		

		int cmd = strcmp(stringToSign.c_str(), stringToSignLocal);
		std::cout << cmd << "\n";

		const auto t0 = hmac_sha256_binstr(awsKey, "20130524");
		const auto t1 = hmac_sha256_binstr(t0, "us-east-1");
		const auto t2 = hmac_sha256_binstr(t1, "s3");
		const auto t3 = hmac_sha256_binstr(t2, "aws4_request");

		const auto hex = BytesToHexString(t3);
		//assert(hex == "c4afb1cc5771d871763a393e44b703571b55cc28424d1a5e86da6ed3c154a4b9");

		const auto signedString = hmac_sha256_str(t3, stringToSign);
		assert(signedString == "98ad721746da40c64f1a55b78f14c238d841ea1380cd77a1b5971af0ece108bd");
	}*/

	const auto signedString = hmac_sha256_str(signingKey, stringToSign);
	str << signedString;

	return str.str();
}

void AWS_PrintCurlHeaders(std::stringstream& str, std::string_view region, const AWSCannonicalRequest& req, const AWSConfig& config)
{
	const auto authorization = AWS_MakeAuthoridzationString(region, req, config);
	str << "-X " << req.method() << " ";
	str << "-H \"Host: " << req.host() << "\" ";
	str << "-H \"x-amz-date: " << req.dateTime().dateTimeString() << "\" ";
	str << "-H \"x-amz-content-sha256: " << req.contentHash() << "\" ";
	str << "-H \"Authorization: " << authorization << "\" ";
}

bool AWS_HandleResult(std::string_view url, const std::string& result)
{
	if (result.empty())
		return true;

	XMLDoc doc;
	try
	{
		doc.parse<0>((char*)result.c_str());
	}
	catch (std::exception& e)
	{
		std::cout << KRED << "[BREAKING] Error parsing result XML from " << url << ": " << e.what() << "\n" << RST;
		return false;
	}

	if (const auto* root = doc.first_node("Error"))
	{
		const auto code = XMLChildNodeValue(root, "Code");

		std::cout << KRED << "[BREAKING] AWS error at " << url << ": " << code << "\n" << RST;
		return false;
	}

	return true;
}

bool AWS_S3_UploadFile(const AWSConfig& aws, const fs::path& filePath, std::string_view bucket, std::string_view region, std::string_view key)
{
	//--

#if 0
	{
		const auto content = "Welcome to Amazon S3.";

		AWSConfig aws;
		aws.key = "AKIAIOSFODNN7EXAMPLE";
		aws.secret = "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY";

		const auto url = "http://examplebucket.s3.amazonaws.com/test$file.text";
		const auto contentHash = Sha256OfText(content);
		assert(contentHash == "44ce7dd67c959e0d3524ffac1771dfbba87d2b6b4b4e99e42034a8b803f8b072");
		const auto region = "us-east-1";

		AWSCanonicalDateTime dt;
		memset(&dt, 0, sizeof(dt));
		dt.today.tm_wday = 5;
		dt.today.tm_year = 2013 - 1900;
		dt.today.tm_mon = 4; // may
		dt.today.tm_mday = 24;

		AWSCannonicalRequest req("PUT", dt, url, contentHash);
		req.setHeader("x-amz-storage-class", "REDUCED_REDUNDANCY");

		std::stringstream txt;
		AWS_PrintCurlHeaders(txt, region, req, aws);
	}

	//--

	{
		AWSConfig aws;
		aws.key = "AKIAIOSFODNN7EXAMPLE";
		aws.secret = "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY";

		const auto url = "http://examplebucket.s3.amazonaws.com/test.txt";
		const auto contentHash = Sha256OfText("");
		assert(contentHash == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  ");
		const auto region = "us-east-1";

		AWSCanonicalDateTime dt;
		memset(&dt, 0, sizeof(dt));
		dt.today.tm_wday = 5;
		dt.today.tm_year = 2013 - 1900;
		dt.today.tm_mon = 4; // may
		dt.today.tm_mday = 24;

		AWSCannonicalRequest req("GET", dt, url, contentHash);
		req.setHeader("range", "bytes=0-9");

		std::stringstream txt;
		AWS_PrintCurlHeaders(txt, region, req, aws);
	}
#endif

	//--

	std::string contentHash;
	if (!Sha256OfFile(filePath, contentHash))
	{
		std::cerr << KRED << "[BREAKING] Failed to calculate SHA256 of " << filePath << "\n" << RST;
		return false;
	}

	const auto url = AWS_S3_MakeObjectPath(bucket, region, key);

	AWSCanonicalDateTime dt;
	AWSCannonicalRequest req("PUT", dt, url, contentHash);

	std::stringstream txt;
	txt << "curl --retry 3 --retry-delay 10 ";
	txt << "-T \"" << EscapeArgument(filePath.u8string()) << "\" ";
	AWS_PrintCurlHeaders(txt, region, req, aws);
	if (filePath.extension() == ".zip")
		txt << "-H \"Content-Type: application/zip\" ";
	txt << "\"";
	txt << url;
	txt << "\" ";

	std::stringstream result;
	if (!RunWithArgsAndCaptureOutput(txt.str(), result))
	{
		std::cerr << KRED << "[BREAKING] AWS request failed: " << url << ": " << result.str() << "\n" << RST;
		return false;
	}

	if (!AWS_HandleResult(url, result.str()))
		return false;

	//outResult = result.str();
	return true;
}

bool AWS_S3_UploadLibrary(const AWSConfig& aws, const fs::path& file, PlatformType platform, std::string_view name)
{
	std::stringstream objectName;
	objectName << "libraries/";
	objectName << NameEnumOption(platform);
	objectName << "/";
	objectName << name;
	objectName << ".zip";

	return AWS_S3_UploadFile(aws, file, "bare-metal-libs", "eu-central-1", objectName.str());
}

//--
