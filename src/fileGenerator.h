#pragma once

#include <mutex>

//--

struct GeneratedFile
{
	GeneratedFile(const fs::path& path)
		: absolutePath(path)
	{}

	fs::path absolutePath;
	fs::file_time_type customtTime;

	std::stringstream content; // may be empty
};

class FileGenerator
{
public:
    GeneratedFile* createFile(const fs::path& path);

    bool saveFiles(bool print=true);

private:
    std::mutex fileLock;
    std::vector<GeneratedFile*> files; // may be empty
};

//--