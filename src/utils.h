#pragma once

//--

class Parser
{
public:
    Parser(std::string_view txt);

    //--

    inline std::string_view fullView() const { return std::string_view(m_start, m_end - m_start); }
    inline std::string_view currentView() const { return std::string_view(m_cur, m_end - m_cur); }
    inline uint32_t line() const { return m_line; }

    //--

    bool testKeyword(std::string_view keyword) const;

    void push();
    void pop();

    bool parseWhitespaces();
    bool parseTillTheEndOfTheLine(std::string_view* outIdent = nullptr);
    bool parseIdentifier(std::string_view& outIdent);
    bool parseString(std::string_view& outValue, const char* additionalDelims = "");
    bool parseStringWithScapement(std::string& outValue, const char* additionalDelims = "");
    bool parseLine(std::string_view& outValue, const char* additionalDelims = "", bool eatLeadingWhitespaces = true);
    bool parseKeyword(std::string_view keyword);
    bool parseChar(uint32_t& outChar);
    bool parseFloat(float& outValue);
    bool parseDouble(double& outValue);
    bool parseBoolean(bool& outValue);
    bool parseHex(uint64_t& outValue, uint32_t maxLength = 0, uint32_t* outValueLength = nullptr);
    bool parseInt8(char& outValue);
    bool parseInt16(short& outValue);
    bool parseInt32(int& outValue);
    bool parseInt64(int64_t& outValue);
    bool parseUint8(uint8_t& outValue);
    bool parseUint16(uint16_t& outValue);
    bool parseUint32(uint32_t& outValue);
    bool parseUint64(uint64_t& outValue);

private:
    const char* m_start;
    const char* m_cur;
    const char* m_end;
    uint32_t m_line;

    struct State
    {
        const char* cur = nullptr;
        uint32_t line = 0;
    };

    std::vector<State> m_stateStack;
};

//--

class Commandline
{
public:
    struct Arg
    {
        std::string key;
        std::string value; // last
        std::vector<std::string> values; // all
    };

    std::vector<Arg> args;
    std::vector<std::string> commands;

    const std::string& get(std::string_view name) const;
    const std::vector<std::string>& getAll(std::string_view name) const;

    std::string_view get(std::string_view name, std::string_view defaultValue) const;

    bool has(std::string_view name) const;
    bool parse(std::string_view text);
};

//--

class RequestArgs
{
public:
	RequestArgs();
	~RequestArgs();

	RequestArgs& clear();
    RequestArgs& remove(std::string_view name);
	RequestArgs& setText(std::string_view name, std::string_view value);
	RequestArgs& setNumber(std::string_view name, int value);
	RequestArgs& setBool(std::string_view name, bool value);

	void print(std::stringstream& f) const;
    void printUri(std::stringstream& f) const;
    void printHeader(std::stringstream& f) const;
    void printHeaderNames(std::stringstream& f) const;

private:
	std::unordered_map<std::string, std::string> m_arguments;
};

extern void PrintURL(std::stringstream& f, std::string_view txt);

//--

struct LogPrinter
{
    LogPrinter(int type);
    LogPrinter(const LogPrinter& other);
    LogPrinter& operator=(const LogPrinter& other);
    ~LogPrinter();

    LogPrinter& operator<<(const char* str);
    LogPrinter& operator<<(const std::string& str);
    LogPrinter& operator<<(const std::string_view& str);
    LogPrinter& operator<<(const std::filesystem::path& str);
    LogPrinter& operator<<(int val);
    LogPrinter& operator<<(uint32_t val);
    LogPrinter& operator<<(uint64_t val);
#if defined (__APPLE__)
    LogPrinter& operator<<(size_t val);
#endif
    LogPrinter& operator<<(std::error_code val);    

private:
    int m_type = 0;
};

//--

extern std::string GetExecutablePath();

//--

extern bool LoadFileToString(const fs::path& path, std::string& outText);

extern bool LoadFileToBuffer(const fs::path& path, std::vector<uint8_t>& outBuffer);

extern bool SaveFileFromString(const fs::path& path, std::string_view txt, bool force = false, bool print = true, uint32_t* outCounter=nullptr, fs::file_time_type customTime = fs::file_time_type());

extern bool SaveFileFromBuffer(const fs::path& path, const std::vector<uint8_t>& buffer, bool force = false, bool print = true, uint32_t* outCounter = nullptr, fs::file_time_type customTime = fs::file_time_type());

//--

extern uint64_t Crc64(uint64_t crc, const uint8_t* s, uint64_t l);

extern uint64_t Crc64(const uint8_t* s, uint64_t l);

//--

extern bool CompressLZ4(const void* data, uint32_t size, std::vector<uint8_t>& outBuffer);
extern bool CompressLZ4(const std::vector<uint8_t>& uncompressedData, std::vector<uint8_t>& outBuffer);

extern bool DecompressLZ4(const void* data, uint32_t size, std::vector<uint8_t>& outBuffer);
extern bool DecompressLZ4(const std::vector<uint8_t>& compresedData, std::vector<uint8_t>& outBuffer);

//--

extern bool RunWithArgs(std::string_view cmd, int* outCode = nullptr);

extern bool RunWithArgsInDirectory(const fs::path& dir, std::string_view cmd, int* outCode = nullptr);

extern bool RunWithArgsAndCaptureOutput(std::string_view cmd, std::stringstream& outStr, int* outCode = nullptr);

extern bool RunWithArgsInDirectoryAndCaptureOutput(const fs::path& dir, std::string_view cmd, std::stringstream& outStr, int* outCode = nullptr);

extern bool RunWithArgsAndCaptureOutputIntoLines(std::string_view cmd, std::vector<std::string>& outLines, int* outCode = nullptr);

extern bool CheckVersion(std::string_view app, std::string_view prefix, std::string_view postfix, std::string_view minVersion);

extern bool OpenDefaultFileEditor(const fs::path& path);

//--

extern bool EndsWith(std::string_view txt, std::string_view end);

extern bool BeginsWith(std::string_view txt, std::string_view end);

extern std::string_view PartBefore(std::string_view txt, std::string_view end);

extern std::string_view PartAfter(std::string_view txt, std::string_view end);

extern std::string_view PartBeforeLast(std::string_view txt, std::string_view end);

extern std::string_view PartAfterLast(std::string_view txt, std::string_view end, bool fullOnEmpty=false);

extern void TokenizeIntoParts(std::string_view txt, std::vector<std::string_view>& outOptions);

extern std::pair<std::string_view, std::string_view> SplitIntoKeyValue(std::string_view txt); // Key=Value

extern std::string MakeGenericPathEx(const fs::path& path);

extern std::string MakeGenericPath(std::string_view txt);

extern std::string MakeSymbolName(std::string_view txt);

extern std::string ToUpper(std::string_view txt);

extern std::string ToLower(std::string_view txt);

extern void writeln(std::stringstream& s, std::string_view txt);

extern void writelnf(std::stringstream& s, const char* txt, ...);

extern void SplitString(std::string_view txt, std::string_view delim, std::vector<std::string_view>& outParts);

extern bool SplitString(std::string_view txt, std::string_view delim, std::string_view& outLeft, std::string_view& outRight);

extern std::string GuidFromText(std::string_view txt);

extern bool IsFileSourceNewer(const fs::path& source, const fs::path& target);

extern bool CreateDirectories(const fs::path& path);

extern bool CopyNewerFile(const fs::path& source, const fs::path& target, bool* outActuallyCopied = nullptr);

extern bool CopyFile(const fs::path& source, const fs::path& target);

extern bool CopyNewerFilesRecursive(const fs::path& sourceDir, const fs::path& targetDir, uint32_t* outActuallyCopied = nullptr);

extern bool CopyFilesRecursive(const fs::path& sourceDir, const fs::path& targetDir, uint32_t* outActuallyCopied = nullptr);

extern std::string ReplaceAll(std::string_view txt, std::string_view what, std::string_view replacement);

extern std::string_view Trim(std::string_view txt);

extern std::string_view TrimQuotes(std::string_view txt);

extern std::string EscapeArgument(std::string_view txt);

extern std::string GetCurrentWeeklyTimestamp(); // 2205 - 5th week of 2022

extern LogPrinter LogInfo();

extern LogPrinter LogWarning();

extern LogPrinter LogError();

extern LogPrinter LogSuccess();

//--

extern std::string_view NameEnumOption(ConfigurationType type);
extern std::string_view NameEnumOption(SolutionType type);
extern std::string_view NameEnumOption(PlatformType type);
extern std::string_view NameEnumOption(GeneratorType type);

//extern bool ParseConfigurationType(std::string_view txt, ConfigurationType& outType);
extern bool ParseLinkingType(std::string_view txt, SolutionType& outType);
extern bool ParsePlatformType(std::string_view txt, PlatformType& outType);
extern bool ParseGeneratorType(std::string_view txt, GeneratorType& outType);

extern PlatformType DefaultPlatform();
extern std::string_view DefaultPlatformStr();

extern bool MatchesPlatform(PlatformType platform, std::string_view view);
extern bool MatchesSolutionType(SolutionType type, std::string_view view);

template< typename T >
struct PrintEnumOptions
{
    int m_defaultValue;

    PrintEnumOptions(T val)
        : m_defaultValue((int)val)
    {}

    friend std::ostream& operator<<(std::ostream& f, const PrintEnumOptions<T>& val)
    {
        f << "[";

        bool hadValue = false;
        for (int i = 0; i < (int)(T::MAX); ++i)
        {
            const auto valueName = NameEnumOption((T)i);
            if (!valueName.empty())
            {
                if (hadValue)
                    f << "|";

                if (val.m_defaultValue == i)
                {
#ifdef _WIN32
                    f << "*" << valueName;
#else
                    f << KGRN << valueName << RST;
#endif
                }
                else
                {
                    f << valueName;
                }

                hadValue = true;
            }
        }

        f << "]";
        return f;
    }
};

//--

template< typename T >
bool PushBackUnique(std::vector<T>& ar, const T& data)
{
    auto it = std::find(ar.begin(), ar.end(), data);
    if (it != ar.end())
        return false;

    ar.push_back(data);
    return true;
}

template< typename T >
bool Contains(const std::vector<T>& ar, const T& data)
{
	auto it = std::find(ar.begin(), ar.end(), data);
    return (it != ar.end());
}

template< typename K, typename V >
bool Contains(const std::unordered_map<K,V>& ar, const K& data)
{
    auto it = ar.find(data);
	return (it != ar.end());
}

template< typename K, typename V >
V Find(const std::unordered_map<K, V>& ar, const K& data, const V& defaultData = V())
{
	auto it = ar.find(data);
    if (it != ar.end())
        return it->second;
    return defaultData;
}

template< typename K >
bool Remove(std::vector<K>& ar, const K& data)
{
	auto it = std::find(ar.begin(), ar.end(), data);
    if (it != ar.end())
    {
        ar.erase(it);
        return true;
    }
	return false;
}

//--

typedef struct {
	uint64_t length;
	uint32_t state[8];
	uint32_t curlen;
	uint8_t buf[64];
} Sha256Context;

#define SHA256_HASH_SIZE (256 / 8)

typedef struct {
	uint8_t bytes[SHA256_HASH_SIZE];
} SHA256_HASH;

extern void Sha256Initialise(Sha256Context* Context);
extern void Sha256Update(Sha256Context* Context, void const* Buffer, uint64_t BufferSize);
extern void Sha256Finalise(Sha256Context* Context, SHA256_HASH* Digest);
extern void Sha256Calculate(void const* Buffer, uint64_t BufferSize, SHA256_HASH* Digest);

extern std::string Sha256OfText(std::string_view data);
extern bool Sha256OfFile(const fs::path& path, std::string& outHashString);

//--

// Amazon HMAC-SHA256 
// Returns the number of bytes written to `out`
extern size_t hmac_sha256(const void* key, const size_t keylen, const void* data, const size_t datalen, void* out,	const size_t outlen);

extern std::string hmac_sha256_str(std::string_view key, std::string_view payload);

extern std::string hmac_sha256_binstr(std::string_view key, std::string_view payload);

extern std::string BytesToHexString(const uint8_t* data, uint64_t length);

extern std::string BytesToHexString(const std::vector<uint8_t>& bytes);

extern std::string BytesToHexString(const std::string& bytes);

extern void BytesToHexString(std::stringstream& str, const uint8_t* data, uint64_t length);

extern void BytesToHexString(std::stringstream& str, const std::vector<uint8_t>& bytes);


//--