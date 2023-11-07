#include "common.h"
#include "utils.h"
#include "configuration.h"
#include "configurationInteractive.h"

//--

Configuration::Configuration()
{
	platform = DefaultPlatform();

    if (platform == PlatformType::Windows)
    {
        generator = GeneratorType::VisualStudio22;
        linking = LinkingType::Shared;
    }
    else
    {
        generator = GeneratorType::CMake;
        linking = LinkingType::Static;
    }
}

std::string Configuration::mergedName() const
{
    std::string ret;
    ret.reserve(100);
    ret += NameEnumOption(platform);
    ret += ".";
    ret += NameEnumOption(generator);
    ret += ".";
    ret += NameEnumOption(linking);

    if (flagDevBuild)
        ret += ".dev";

    return ret;
}

fs::path Configuration::platformConfigurationFile() const
{
	const auto fileName = std::string(NameEnumOption(platform)) + ".config";
	return (tempPath / fileName).make_preferred(); // Z:\projects\core\.temp\windows.config
}

bool Configuration::save(const fs::path& path) const
{
	return SaveFileFromString(path, mergedName());
}

bool Configuration::load(const fs::path& path)
{
	std::string str;
	if (!LoadFileToString(path, str))
		return false;

	std::vector<std::string_view> parts;
	SplitString(str, ".", parts);
	if (parts.size() < 4)
		return false;

	bool valid = ParsePlatformType(parts[0], platform);
	valid &= ParseGeneratorType(parts[1], generator);
	valid &= ParseLinkingType(parts[2], linking);

    //if (parts.size() == 5 && parts[4] == "shipment")
      //  flagShipmentBuild = true;

	return valid;
}

bool Configuration::Parse(const Commandline& cmd, Configuration& cfg)
{
    cfg.executablePath = GetExecutablePath();
	if (!fs::is_regular_file(cfg.executablePath))
	{
		LogInfo() << "Invalid local executable name: " << cfg.executablePath;
		return false;
	}

	if (!ParseOptions(cmd, cfg)) {
		LogError() << "Invalid/incomplete configuration";
		return 1;
	}

	/*if (cmd.has("interactive"))
		if (!RunInteractiveConfig(cfg))
			return false;*/

	if (!ParsePaths(cmd, cfg)) {
		LogError() << "Invalid/incomplete configuration";
		return 1;
	}

	LogInfo() << "Configuration: '" << cfg.mergedName() << "'";
    return true;
}

bool Configuration::ParseOptions(const Commandline& cmd, Configuration& cfg)
{
    // -config=windows[.nodev]
    // -config=windows.static[.nodev]
    // -config=windows.static.cmake[.nodev]

    bool hasGeneratorType = false;

    const auto& buildString = cmd.get("config");
    if (!buildString.empty())
    {
        std::vector<std::string_view> buildParts;
        SplitString(buildString, ".", buildParts);

        bool hasPlatform = false;
        bool hasConfigType = false;
        bool hasLibsType = false;

        for (const auto& part : buildParts)
        {
            if (ParsePlatformType(part, cfg.platform))
            {
                if (hasPlatform)
                {
                    LogError() << "Platform was already defined in build configuration string";
                    return false;
                }

                hasPlatform = true;
            }
            else if (ParseLinkingType(part, cfg.linking))
            {
                if (hasLibsType)
                {
                    LogError() << "Library type was already defined in build configuration string";
                    return false;
                }

                hasLibsType = true;
            }
            else if (ParseGeneratorType(part, cfg.generator))
            {
                if (hasGeneratorType)
                {
                    LogError() << "Generator type was already defined in build configuration string";
                    return false;
                }

                hasGeneratorType = true;
            }
            else if (part == "nodev")
            {
                cfg.flagDevBuild = false;
            }
            else
            {
                LogError() << "Invalid build configuration token '" << part << "' that does not match any platform, generator, library type or other shit";
                return false;
            }
        }
    }

    // assign some defaults
    if (!hasGeneratorType)
    {
        if (cfg.platform == PlatformType::Windows || cfg.platform == PlatformType::UWP)
            cfg.generator = GeneratorType::VisualStudio22;
        else if (cfg.platform == PlatformType::Prospero || cfg.platform == PlatformType::Scarlett)
            cfg.generator = GeneratorType::VisualStudio22;
        else
            cfg.generator = GeneratorType::CMake;
    }

    // when using the CMake generator we can't really generate reflection and other shit at runtime :(
    if (cfg.generator == GeneratorType::CMake || cmd.has("static"))
    {
        LogInfo() << "Enabled static content generation";
        cfg.flagStaticBuild = true;
    }

    return true;
}

bool Configuration::ParsePaths(const Commandline& cmd, Configuration& cfg)
{
    // module path
	{
		const auto& str = cmd.get("module");
		if (str.empty())
		{
            auto testPath = fs::weakly_canonical(fs::absolute((fs::current_path() / "build.xml").make_preferred()));
			if (!fs::is_regular_file(testPath))
			{
				LogError() << "Build tool run in a directory without \"build.xml\", did you run the configure command?";
				return false;
			}
			else
			{
                cfg.moduleFilePath = testPath;
                cfg.moduleDirPath = testPath.parent_path();
			}
		}
		else
		{
            auto testPath = fs::weakly_canonical(fs::absolute(str).make_preferred());
			if (!fs::is_regular_file(testPath))
			{
                LogError() << "Specified module path '" << str << " does not exist";
				return false;
			}

			cfg.moduleFilePath = testPath;
            cfg.moduleDirPath = testPath.parent_path();
		}
	}

    // temp directory
    {
		const auto& str = cmd.get("tempPath");
		if (str.empty())
			cfg.tempPath = (cfg.moduleDirPath / ".temp").make_preferred();
		else
			cfg.tempPath = fs::weakly_canonical(fs::absolute(fs::path(str).make_preferred()));

		std::error_code ec;
		if (!fs::is_directory(cfg.tempPath, ec))
		{
			if (!fs::create_directories(cfg.tempPath, ec))
			{
				LogError() << "Failed to create temp directory " << cfg.tempPath;
				return false;
			}
		}
    }

    // cache directory
	{
		const auto& str = cmd.get("cachePath");
		if (str.empty())
			cfg.cachePath = (cfg.moduleDirPath / ".cache").make_preferred();
		else
			cfg.cachePath = fs::weakly_canonical(fs::absolute(fs::path(str).make_preferred()));

		std::error_code ec;
		if (!fs::is_directory(cfg.cachePath, ec))
		{
			if (!fs::create_directories(cfg.cachePath, ec))
			{
				LogError() << "Failed to create temp directory " << cfg.cachePath;
				return false;
			}
		}
	}

    // derived paths
    cfg.derivedConfigurationPathBase = (cfg.tempPath / cfg.mergedName()).make_preferred();
    cfg.derivedBinaryPathBase = (cfg.derivedConfigurationPathBase / "bin").make_preferred();
    cfg.derivedSolutionPathBase = (cfg.derivedConfigurationPathBase / "build").make_preferred();

    return true;
}

//--
