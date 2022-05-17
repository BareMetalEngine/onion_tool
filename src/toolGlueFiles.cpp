#include "common.h"
#include "utils.h"
#include "toolGlueFiles.h"
#include "fileRepository.h"

//--

struct FileForPacking
{
	fs::path source;
	std::string local;
	fs::file_time_type timestamp;
};

static bool CollectFilesForPacking(const fs::path& sourcePath, const std::string& localPath, std::vector<FileForPacking>& outFiles)
{
	bool valid = true;

	try
	{
		if (fs::is_directory(sourcePath))
		{
			for (const auto& entry : fs::directory_iterator(sourcePath))
			{
				const auto name = entry.path().filename().u8string();

				if (entry.is_directory())
				{
					const auto subPath = localPath.empty() ? (name) : (localPath + "/" + name);
					valid &= CollectFilesForPacking(entry.path(), subPath, outFiles);
				}
				else if (entry.is_regular_file())
				{
					FileForPacking file;
					file.source = entry.path();
					file.local = localPath.empty() ? (name) : (localPath + "/" + name);
					file.timestamp = fs::last_write_time(entry.path());
					outFiles.push_back(file);
				}
			}
		}
	}
	catch (fs::filesystem_error& e)
	{
		std::cout << "File system Error: " << e.what() << "\n";
		valid = false;
	}

	return valid;
}

//--

static bool GlueFile_List(const fs::path& path)
{
	GluedArchive archive;

	if (!archive.loadFromFile(path))
	{
		std::cerr << KRED << "[BREAKING] Failed to load glued files from " << path << "\n" << RST;
		return false;
	}

	std::cout << "There are " << KGRN << archive.files().size() << RST << " file(s) in the archive\n";

	uint32_t index = 0;
	for (const auto& it : archive.files())
	{
		const auto& file = it.second;

		const auto ratio = (file.uncompressedSize / (float)file.compressedData.size()) * 100.0f;
		std::cout << "[" << index << "]: " << file.name << " (size: " << file.uncompressedSize << ", compression ration: " << ratio << "%" << ")\n";
		index += 1;
	}

	return true;
}

static bool GlueFile_Clear(const fs::path& path)
{
	GluedArchive archive;

	if (!archive.saveToFile(path))
	{
		std::cerr << KRED << "[BREAKING] Failed to update glued archive\n";
		return false;
	}

	return true;
}

static bool GlueFile_Pack(const fs::path& path, const Commandline& cmdline)
{
	GluedArchive archive;

	//--

	const auto append = cmdline.has("append");
	if (append)
		archive.loadFromFile(path);

	//--

	fs::path sourceDirectory;
	{
		const auto str = cmdline.get("source");
		if (str.empty())
		{
			std::cerr << KRED << "[BREAKING] Missing source directory (-source)\n" << RST;
			return false;
		}

		sourceDirectory = fs::absolute(str).make_preferred();
		if (!fs::is_directory(sourceDirectory))
		{
			std::cerr << KRED << "[BREAKING] Source directory at " << sourceDirectory << " does not exist\n" << RST;
			return false;
		}
	}

	std::string prefix = cmdline.get("prefix");

	//--

	std::vector<FileForPacking> filesForPacking;
	if (!CollectFilesForPacking(sourceDirectory, prefix, filesForPacking))
	{
		std::cerr << KRED << "[BREAKING] Failed to collect content for packing from directory " << sourceDirectory << "\n" << RST;
		return false;
	}

	//--

	bool valid = true;
	for (const auto& file : filesForPacking)
		valid &= archive.storeFile(file.local, file.source);

	if (!valid)
	{
		std::cerr << KRED << "[BREAKING] Failed to load all files for gluing\n";
		return false;
	}

	if (!archive.saveToFile(path))
	{
		std::cerr << KRED << "[BREAKING] Failed to update glued archive\n";
		return false;
	}

	return true;
}

static bool GlueFile_Unpack(const fs::path& path, const Commandline& cmdline)
{
	GluedArchive archive;

	if (!archive.loadFromFile(path))
	{
		std::cerr << KRED << "[BREAKING] Failed to load glued files from " << path << "\n" << RST;
		return false;
	}

	//--

	fs::path targetDirectory;
	{
		const auto str = cmdline.get("target");
		if (str.empty())
		{
			std::cerr << KRED << "[BREAKING] Missing target directory (-target)\n" << RST;
			return false;
		}

		targetDirectory = fs::absolute(str).make_preferred();
		if (!CreateDirectories(targetDirectory))
		{
			std::cerr << KRED << "[BREAKING] Could not create target directory at " << targetDirectory << "\n" << RST;
			return false;
		}
	}

	//--

	const bool force = cmdline.has("force");
	const auto& files = archive.findFiles("");

	//--

	bool valid = true;
	uint32_t saved = 0;
	for (const auto* file : files)
	{
		const auto targetPath = fs::weakly_canonical(targetDirectory / file->name);

		std::vector<uint8_t> decompresedContent;
		decompresedContent.resize(file->uncompressedSize);

		if (DecompressLZ4(file->compressedData, decompresedContent))
		{
			valid &= SaveFileFromBuffer(targetPath, decompresedContent, force, false, &saved, file->timestamp);
		}
	}

	if (!valid)
	{
		std::cerr << KRED << "[BREAKING] Failed to extract all files\n";
		return false;
	}

	return true;
}

//--

ToolGlueFiles::ToolGlueFiles()
{}

int ToolGlueFiles::run(const char* argv0, const Commandline& cmdline)
{
	//--

	fs::path targetFilePath;
	{
		const auto str = cmdline.get("file");
		if (str.empty())
		{
			std::cerr << KRED << "[BREAKING] Missing target file path (-file)\n" << RST;
			return 1;
		}

		targetFilePath = fs::absolute(str).make_preferred();
		if (!fs::is_regular_file(targetFilePath))
		{
			std::cerr << KRED << "[BREAKING] File at " << targetFilePath << " does not exist\n" << RST;
			return 1;
		}
	}

	//--

	const auto action = cmdline.get("action");
	if (action == "list")
	{
		if (!GlueFile_List(targetFilePath))
			return 1;
	}
	else if (action == "clear")
	{
		if (!GlueFile_Clear(targetFilePath))
			return 1;
	}
	else if (action == "pack")
	{
		if (!GlueFile_Pack(targetFilePath, cmdline))
			return 1;
	}
	else if (action == "unpack")
	{
		if (!GlueFile_Unpack(targetFilePath, cmdline))
			return 1;
	}
	else
	{
		std::cerr << KRED << "[BREAKING] Unknown glue action '" << action << "'\n" << RST;
		return 1;
	}

	return 0;
}

//--
