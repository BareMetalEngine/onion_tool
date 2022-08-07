#include "common.h"
#include "utils.h"
#include "toolEmbed.h"
#include "fileGenerator.h"

//--

ToolEmbed::ToolEmbed()
{}

static const char* WriteBufer(char*& ptr, std::string_view view)
{
	auto* start = ptr;
	memcpy(ptr, view.data(), view.length());
	ptr[view.length()] = 0;
	ptr += view.length() + 1;
	return start;
}

static bool IsValidPrintableChar(uint8_t ch)
{
	if (ch >= 32 && ch <= 127)
		return true;
	return false;
}

static const std::string_view* MakeStringTokenTable()
{
	static std::string_view theTable[256];
	static char theBuffer[8192];

	char* buffWriter = theBuffer;

	for (uint32_t i = 0; i < 256; ++i)
	{
		//int a = i % 8;
		//int b = (i / 8) % 8;
		//int c = (i / 64) % 8;
		//theTable[i] = WriteBufer(buffWriter, TempString("\\{}{}{}", c, b, a));

		/*if (IsValidPrintableChar(i))
		{
			char local[10];
			local[0] = i;
			local[1] = 0;
			theTable[i] = WriteBufer(buffWriter, local);
		}
		else*/
		{
			char local[10];
			sprintf_s(local, sizeof(local), "\\x%02X", i);
			theTable[i] = WriteBufer(buffWriter, local);
		}
	}

	return theTable;
}

static void PrintDataTableByte(std::stringstream& f, const char* name, const void* data, uint32_t dataSize)
{
	writelnf(f, "const unsigned char* %hs = (const unsigned char*)", name);

	static const auto* HexTokens = MakeStringTokenTable();

	const auto* ptr = (const uint8_t*)data;
	const auto* ptrEnd = ptr + dataSize;
	bool separatorNeeded = false;
	while (ptr < ptrEnd)
	{
		f << "\"";

		int lineLength = 4096;
		while (ptr < ptrEnd && lineLength--)
			f << HexTokens[*ptr++];

		if (ptr < ptrEnd)
			f << "\"\n";
		else
			f << "\";";
	}
}

template <typename TP>
std::time_t to_time_t(TP tp)
{
	using namespace std::chrono;
	auto sctp = time_point_cast<system_clock::duration>(tp - TP::clock::now()
		+ system_clock::now());
	return system_clock::to_time_t(sctp);
}

bool ToolEmbed::writeFile(FileGenerator& gen, const fs::path& inputPath, std::string_view projectName, std::string_view relativePath, const fs::path& outputPath)
{
	// load content
	std::vector<uint8_t> data;
	if (!LoadFileToBuffer(inputPath, data))
	{
		std::cout << "[BREKAING] Failed to load content of " << inputPath << "\n";
		return false;
	}

	// create file
	auto* file = gen.createFile(outputPath);
	file->customtTime = fs::last_write_time(inputPath);

	// timestamp ?
	const auto timeStamp = to_time_t(file->customtTime);

	// header
	auto& f = file->content;
	writeln(f, "/***");
	writeln(f, "* Onion Embedded File");
	writeln(f, "* Auto generated, do not modify - add stuff to public.h instead");
	writeln(f, "***/");
	writeln(f, "");

	// global include
	/*{
		const auto includePath = (m_envPath / "tools" / "embed" / "file.h").make_preferred();
		writelnf(f, "#include <%hs>", includePath.u8string().c_str());
		writeln(f, "");
	}*/

	// symbol name
	const auto symbolPrefix = std::string("EMBED_") + std::string(projectName) + "_";
	const auto symbolCoreName = symbolPrefix + MakeSymbolName(relativePath);
	const auto symbolData = symbolCoreName + "_DATA";

	const auto safeSourcePath = ReplaceAll(inputPath.u8string().c_str(), "\\", "\\\\");
	const auto safeRelativePath = ReplaceAll(relativePath, "\\", "/");

    writelnf(f, "#include <stdint.h>");
	writelnf(f, "const char* %hs_PATH = \"%hs/%hs\";", symbolCoreName.c_str(), std::string(projectName).c_str(), safeRelativePath.c_str());
	writelnf(f, "const char* %hs_SPATH = \"%hs\";", symbolCoreName.c_str(), safeSourcePath.c_str());
	writelnf(f, "extern const unsigned int %hs_SIZE = %u;", symbolCoreName.c_str(), (uint32_t)data.size());
	writelnf(f, "extern const uint64_t %hs_CRC = 0x%016llX;", symbolCoreName.c_str(), Crc64(data.data(), data.size()));
	writelnf(f, "extern const uint64_t %hs_TS = %llu;", symbolCoreName.c_str(), timeStamp);
	PrintDataTableByte(f, symbolData.c_str(), data.data(), (uint32_t)data.size());

	// 
	// 
	// data

	return true;
}

int ToolEmbed::run(const Commandline& cmdline)
{
	const auto nologo = cmdline.has("nologo");

    std::string sourceFilePath = cmdline.get("source");
    if (sourceFilePath.empty())
    {
        std::cout << "Embed file list must be specified by -source\n";
        return 1;
	}

	std::string projectName = cmdline.get("project");
	if (projectName.empty())
	{
		std::cout << "Reflection project name must be specified by -project\n";
		return 1;
	}

	std::string outputFilePath = cmdline.get("output");
	if (outputFilePath.empty())
	{
		std::cout << "Reflection output file path must be specified by -output\n";
		return 1;
	}
	//std::cout << "OutputPath: \"" << outputFilePath << "\"\n";

	std::string relativeFilePath = ReplaceAll(cmdline.get("relative"), "\\", "/");
	if (relativeFilePath.empty())
	{
		std::cout << "Embed relative file name must be specified by -relative\n";
		return 1;
	}
	//std::cout << "RelativePath: \"" << relativeFilePath << "\"\n";

    FileGenerator files;
	if (!writeFile(files, sourceFilePath, projectName, relativeFilePath, outputFilePath))
		return 1;

    if (!files.saveFiles(!nologo))
        return 2;

	return 0;
}

//--