#include "common.h"
#include "utils.h"
#include "fileRepository.h"

#ifndef _WIN32
#ifndef _POSIX_SOURCE
#define _POSIX_SOURCE
#endif
#include <sys/stat.h>
#endif

//--

#pragma pack(push)
#pragma pack(1)
struct GluedEntryHeader
{
	static const uint32_t MAGIC = 0x46494C45; // FILE

	uint32_t magic = 0;
	uint32_t nameSize = 0;
	uint32_t contentSize = 0;
	uint32_t uncompressedSize = 0;
	fs::file_time_type timestamp;
	uint64_t crc = 0;
};

struct GluedArchiveHeader
{
	static const uint32_t MAGIC = 0x474C5545; // GLUE;

	uint32_t magic = 0;
	uint32_t offset = 0; // to first file
	uint32_t count = 0; // number of files
};
#pragma pack(pop)

//--

GluedArchive::GluedArchive()
{}

const GluedFile* GluedArchive::findFile(std::string_view localPath) const
{
	const auto& it = m_files.find(std::string(localPath));
	if (it != m_files.end())
		return &it->second;

	return nullptr;
}

std::vector<const GluedFile*> GluedArchive::findFiles(std::string_view localPrefixPath) const
{
	std::vector<const GluedFile*> ret;

	for (const auto& it : m_files)
		if (BeginsWith(it.second.name, localPrefixPath))
			ret.push_back(&it.second);

	return ret;
}

bool GluedArchive::storeFile(const std::string& name, fs::file_time_type timestamp, std::vector<uint8_t> data)
{
	if (name.empty())
	{
		LogError() << "Failed to store glue file without a name";
		return false;
	}

	if (data.empty())
	{
		LogError() << "Failed to store glue file '" << name << "' without content";
		return false;
	}

	std::vector<uint8_t> compressedData;
	if (!CompressLZ4(data, compressedData))
		return false;

	GluedFile file;
	file.name = name;
	file.timestamp = timestamp;
	file.compressedData = compressedData;
	file.uncompressedSize = (uint32_t)data.size();
	m_files[file.name] = file;

	LogInfo() << "Stored file '" << name << "' (size: " << data.size() << ", compressed: " << compressedData.size() << ")";
	return true;
}

bool GluedArchive::storeFile(const std::string& name, const fs::path& sourcePath)
{
	std::vector<uint8_t> data;
	if (!LoadFileToBuffer(sourcePath, data))
	{
		LogError() << "Failed to load content of " << sourcePath << " into a memory buffer";
		return false;
	}

	std::error_code ec;
	auto time = fs::last_write_time(sourcePath, ec);

	return storeFile(name, time, data);
}

bool GluedArchive::loadFromFile(const fs::path& path)
{
	// just load all content
	std::vector<uint8_t> buffer;
	if (!LoadFileToBuffer(path, buffer))
	{
		LogWarning() << "Failed to load content of " << path << " into a memory buffer";
		return false;
	}

	// file is smaller than the header
	if (buffer.size() < sizeof(GluedArchiveHeader))
	{
		LogWarning() << "File " << path << " is to small to host GLUE header";
		return false;
	}

	// do we have the glue "header" ?
	const auto* header = (const GluedArchiveHeader*)(buffer.data() + buffer.size() - sizeof(GluedArchiveHeader));
	if (header->magic != GluedArchiveHeader::MAGIC)
	{
		LogWarning() << "File " << path << " does not contain glued data";
		return false;
	}

	LogInfo() << "Found " << header->count << " glued file(s)";

	// extract files
	uint32_t offset = header->offset;
	for (uint32_t i = 0; i < header->count; ++i)
	{
		// invalid file
		if (offset + sizeof(GluedEntryHeader) > buffer.size())
		{
			LogWarning() << "File " << path << " has corrupted glue data, entry " << i << " header lies out of file boundary";
			return false;
		}

		// make sure it's a valid entry
		const auto* fileHeader = (const GluedEntryHeader*)(buffer.data() + offset);
		if (fileHeader->magic != GluedEntryHeader::MAGIC)
		{
			LogWarning() << "File " << path << " has corrupted glue data, entry " << i << " has invalid magic value";
			return false;
		}

		// make sure we have space for data
		const auto endOffset = offset + sizeof(GluedEntryHeader) + fileHeader->nameSize + fileHeader->contentSize + 1;
		if (endOffset > buffer.size())
		{
			LogWarning() << "File " << path << " has corrupted glue data, entry " << i << " lies out of file boundary";
			return false;
		}

		// read file name and content
		const auto* name = (const char*)buffer.data() + offset + sizeof(GluedEntryHeader);
		const auto* content = (const uint8_t*)name + fileHeader->nameSize + 1;
		//LogInfo() << "Found glued file '" << name << "' at offset " << offset << "\n";

		// calculate content CRC
		const auto crc = Crc64(content, fileHeader->contentSize);
		if (crc != fileHeader->crc)
		{
			LogWarning() << "File " << path << " has corrupted content for glued file '" << name << "' - invalid CRC";
			return false;
		}

		// duplicated file ?
		const auto nameStr = std::string(name);
		if (m_files.find(nameStr) != m_files.end())
		{
			LogWarning() << "File " << path << " has duplicated glued file '" << name << "'";
			return false;
		}

		// store entry
		GluedFile file;
		file.name = std::move(nameStr);
		file.timestamp = fileHeader->timestamp;
		file.compressedData.resize(fileHeader->contentSize);
		file.uncompressedSize = fileHeader->uncompressedSize;
		memcpy(file.compressedData.data(), content, fileHeader->contentSize);
		m_files[file.name] = file;

		// advance to new file
		offset = (uint32_t)endOffset;
	}

	// done
	return true;
}

static uint32_t WriteToBuffer(std::vector<uint8_t>& buffer, const void* data, uint32_t size)
{
	const auto offset = buffer.size();
	buffer.resize(buffer.size() + size);
	memcpy(buffer.data() + offset, data, size);
	return (uint32_t) offset;
}

static uint32_t WriteToBuffer(std::vector<uint8_t>& buffer, std::string_view txt)
{
	const auto offset = WriteToBuffer(buffer, txt.data(), (uint32_t)txt.length());

	uint8_t zero = 0;
	WriteToBuffer(buffer, &zero, 1);

	return offset;
}

static uint32_t WriteToBuffer(std::vector<uint8_t>& buffer, const std::vector<uint8_t>& data)
{
	return WriteToBuffer(buffer, data.data(), (uint32_t)data.size());
}

bool GluedArchive::saveToFile(const fs::path& path)
{
	// just load all existing content
	std::vector<uint8_t> buffer;
	if (!LoadFileToBuffer(path, buffer))
	{
		LogWarning() << "Failed to load content of " << path << " into a memory buffer";
		return false;
	}

	// do we have the glue "header" ?
	if (buffer.size() >= sizeof(GluedArchiveHeader))
	{
		const auto* header = (const GluedArchiveHeader*)(buffer.data() + buffer.size() - sizeof(GluedArchiveHeader));
		if (header->magic == GluedArchiveHeader::MAGIC)
		{
			LogInfo() << "Found " << header->count << " already glued file(s) at offset " << header->offset;
			buffer.resize(header->offset);
		}
	}

	// gather file entries
	std::vector<const GluedFile*> filesToSave;
	for (const auto& it : m_files)
		filesToSave.push_back(&it.second);

	// sort entries by name to ensure deterministic saving
	std::sort(filesToSave.begin(), filesToSave.end(), [](const GluedFile* a, const GluedFile* b) { return a->name < b->name; });

	// write header only if we have files there
	if (!filesToSave.empty())
	{
		// store entries in the file
		const auto startOffset = (uint32_t)buffer.size();
		for (const auto* file : filesToSave)
		{
			GluedEntryHeader header;
			header.magic = GluedEntryHeader::MAGIC;
			header.crc = Crc64(file->compressedData.data(), file->compressedData.size());
			header.timestamp = file->timestamp;
			header.nameSize = (uint32_t)file->name.length();
			header.contentSize = (uint32_t)file->compressedData.size();
			header.uncompressedSize = (uint32_t)file->uncompressedSize;
			WriteToBuffer(buffer, &header, sizeof(header));
			WriteToBuffer(buffer, file->name);
			WriteToBuffer(buffer, file->compressedData);
		}

		// final info
		LogInfo() << "Stored " << filesToSave.size() << " glued file(s) at offset " << startOffset << ", total size of stored data is " << (buffer.size() - startOffset);

		// store header
		{
			GluedArchiveHeader header;
			header.magic = GluedArchiveHeader::MAGIC;
			header.offset = startOffset;
			header.count = (uint32_t)filesToSave.size();
			WriteToBuffer(buffer, &header, sizeof(header));
		}
	}

	// save buffer to file
	return SaveFileFromBuffer(path, buffer);
}

//--

FileRepository::FileRepository()
{}

FileRepository::~FileRepository()
{}

bool FileRepository::initialize(const fs::path& executablePath, const fs::path& extractedFilesPath)
{
	// if we have loose files always use them
	{
		const auto testDirectory = fs::weakly_canonical((executablePath.parent_path() / ".." / "files").make_preferred());
		const auto testFile = testDirectory / "README.md";
		if (fs::is_regular_file(testFile))
		{
			LogInfo() << "Found local files at " << testDirectory;
			m_rootFileSystemPath = testDirectory;
			return true;
		}
	}

	// if we don't have local files we better have the files glued to executable
	if (m_archive.loadFromFile(executablePath))
	{
		m_extractedFilesPath = extractedFilesPath;
		return true;
	}

	// no files found
	LogError() << "No third party files found for the Build Tool, files should either be glued to executable or in the '../files' directory WRT the binary";
	return false;
}


static std::vector<uint8_t> FixupLineEndingsToLinuxOnes(std::vector<uint8_t> data)
{
	std::vector<uint8_t> ret;
	ret.reserve(data.size());

	const auto* ptr = data.data();
	const auto* ptrEnd = ptr + data.size();
	while (ptr < ptrEnd)
	{
		auto ch = *ptr++;

		if (ch == 13 && (ptr < ptrEnd) && *ptr == 10)
			ch = *ptr++;

		ret.push_back(ch);
	}

	return ret;
}

bool FileRepository::extractLocalFile(const GluedFile* file, fs::path& outActualPath)
{
	// no extraction supported
	if (m_extractedFilesPath.empty())
	{
		LogError() << "No extraction folder setup";
		return false;
	}

	// output file path
	const auto targetFilePath = (m_extractedFilesPath / file->name).make_preferred();

	// if file exists check the date, if it's up to date don't extract it
	if (fs::is_regular_file(targetFilePath))
	{
		std::error_code ec;
		const auto fileTime = fs::last_write_time(targetFilePath, ec);
		if (fileTime == file->timestamp)
		{
			//LogInfo() << "Skipping file '" << file->name << "' as it's up to date";
			outActualPath = targetFilePath;
			return true;
		}
		else
		{
			//LogInfo() << "Not skipping file '" << file->name << "' as it's NOT up to date";
		}
	}
	else
	{
		//LogInfo() << "Not skipping file '" << file->name << "' as it's NOT extracted yet";
	}

	// decompress file content
	std::vector<uint8_t> decompresedContent;
	decompresedContent.resize(file->uncompressedSize);
	if (!DecompressLZ4(file->compressedData, decompresedContent))
	{
		LogError() << "Failed to decompress files '" << file->name << "'";
		return false;
	}

	// fixup line endings...
	{
		const auto fileExtension = targetFilePath.extension().u8string();
		if (fileExtension == ".sh")
			decompresedContent = FixupLineEndingsToLinuxOnes(decompresedContent);
	}

	// write data to the file
	uint32_t saved = 0;
	if (!SaveFileFromBuffer(targetFilePath, decompresedContent, false, false, &saved, file->timestamp))
	{
		LogError() << "Failed to save extracted data for file '" << file->name << "'";
		return false;
	}

#ifndef _WIN32
	{
		// HASH: add the perms to the .sh and executable files...
		const auto fileExtension = targetFilePath.extension().u8string();
		if (fileExtension == ".sh" || fileExtension == "")
		{
            LogWarning() << "Setting +x on file '" << file->name << "'";

			const auto mode = S_IRWXG | S_IRWXO | S_IRWXU;
			if (0 != chmod(targetFilePath.u8string().c_str(), mode))
			{
				LogError() << "Failed to make file executable '" << file->name << "'";
				return false;
			}
		}
	}
#endif

	// saved
	outActualPath = targetFilePath;
	LogInfo() << "Extracted glued file '" << file->name << "'";
	return true;
}

bool FileRepository::resolveDirectoryPath(std::string_view localPath, fs::path& outActualPath)
{
	// use packed data
	if (m_rootFileSystemPath.empty())
	{
		// extract all files that begin with the prefix
		const auto files = m_archive.findFiles(localPath);

		bool valid = true;
		fs::path tempPath;
		for (const auto* it : files)
			valid &= extractLocalFile(it, tempPath);

		outActualPath = (m_extractedFilesPath / localPath).make_preferred();
		return valid;
	}

	// use local path
	else
	{
		const auto fullPath = (m_rootFileSystemPath / localPath).make_preferred();
		if (fs::is_directory(fullPath))
		{
			outActualPath = fullPath;
			return true;
		}
	}

	// unable to resolve file
	LogError() << "Unable to resolve path to third party directory '" << localPath << "'";
	return false;
}

bool FileRepository::resolveFilePath(std::string_view localPath, fs::path& outActualPath)
{
	// use packed data
	if (m_rootFileSystemPath.empty())
	{
		// extract all files that begin with the prefix
		if (const auto* file = m_archive.findFile(localPath))
		{
			return extractLocalFile(file, outActualPath);
		}
		else
		{
			LogError() << "Packed file '" << localPath << "' not found";
			return false;
		}
	}

	// use local path
	else
	{
		const auto fullPath = (m_rootFileSystemPath / localPath).make_preferred();
		if (fs::is_regular_file(fullPath))
		{
			outActualPath = fullPath;
			return true;
		}
	}

	// unable to resolve file
	LogError() << "Unable to resolve path to third party file '" << localPath << "'";
	return false;
}