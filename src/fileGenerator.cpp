#include "common.h"
#include "utils.h"
#include "fileGenerator.h"

//--

GeneratedFile* FileGenerator::createFile(const fs::path& path)
{
    std::lock_guard<std::mutex> lk(fileLock);

    auto file = new GeneratedFile(path);
    files.push_back(file);
    return file;
}

bool FileGenerator::saveFiles(bool print)
{
    bool valid = true;

    uint32_t numSavedFiles = 0;

    if (files.size() < 6)
    {
		for (const auto* file : files)
			valid &= SaveFileFromString(file->absolutePath, file->content.str(), false, print, & numSavedFiles, file->customtTime);
    }
    else
    {
        #pragma omp parallel for
        for (int i = 0; i < files.size(); ++i)
        {
            const auto* file = files[i];
            valid &= SaveFileFromString(file->absolutePath, file->content.str(), false, print, &numSavedFiles, file->customtTime);
        }
    }

    if (print)
        std::cout << "Saved " << numSavedFiles << " files\n";

    if (!valid)
    {
        std::cout << "Failed to save some output files, generated solution may not be valid\n";
        return false;
    }

    return true;
}

//--
