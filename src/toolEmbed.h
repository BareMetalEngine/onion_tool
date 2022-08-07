#pragma once

//--

class FileGenerator;
struct GeneratedFile;

//--

class ToolEmbed
{
public:
    ToolEmbed();

    int run(const Commandline& cmdline);
    bool writeFile(FileGenerator& gen, const fs::path& inputPath, std::string_view projectName, std::string_view relativePath, const fs::path& outputPath);
};

//--