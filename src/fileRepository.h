#pragma once

#include <vector>
#include <unordered_map>

//--

struct GluedFile
{
	std::string name;
	std::vector<uint8_t> compressedData;
	fs::file_time_type timestamp;
	uint32_t uncompressedSize = 0;
	//uint64_t crc = 0;
};

class GluedArchive
{
public:
	GluedArchive();

	bool storeFile(const std::string& name, fs::file_time_type timestamp, std::vector<uint8_t> data);
	bool storeFile(const std::string& name, const fs::path& sourcePath);

	bool loadFromFile(const fs::path& path);
	bool saveToFile(const fs::path& path);

	inline const std::unordered_map<std::string, GluedFile>& files() const { return m_files; }

	const GluedFile* findFile(std::string_view localPath) const;
	std::vector<const GluedFile*> findFiles(std::string_view localPrefixPath) const;

private:
	std::unordered_map<std::string, GluedFile> m_files;
};

//--

class FileRepository
{
public:
	FileRepository();
	~FileRepository();

	bool initialize(const fs::path& executablePath, const fs::path& extractedFilesPath);
	bool resolveFilePath(std::string_view localPath, fs::path& outActualPath);
	bool resolveDirectoryPath(std::string_view localPath, fs::path& outActualPath);

private:
	GluedArchive m_archive;

	fs::path m_rootFileSystemPath;
	fs::path m_extractedFilesPath;

	bool extractLocalFile(const GluedFile* file, fs::path& outActualPath);
};

//--