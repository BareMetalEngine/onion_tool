#include "common.h"
#include "utils.h"
#include "configuration.h"
#include "configurationInteractive.h"

//--

Configuration::Configuration()
{
    configuration = ConfigurationType::Release;
	platform = DefaultPlatform();

    if (platform == PlatformType::Windows)
    {
        generator = GeneratorType::VisualStudio22;
        libs = LibraryType::Shared;
    }
    else
    {
        generator = GeneratorType::CMake;
		libs = LibraryType::Static;
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
    ret += NameEnumOption(libs);
    ret += ".";
    ret += NameEnumOption(configuration);

    if (flagShipmentBuild)
        ret += ".shipment";

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
	valid &= ParseLibraryType(parts[2], libs);
	valid &= ParseConfigurationType(parts[3], configuration);

    if (parts.size() == 5 && parts[4] == "shipment")
        flagShipmentBuild = true;

	return valid;
}

bool Configuration::Parse(const Commandline& cmd, Configuration& cfg)
{
    cfg.executablePath = GetExecutablePath();
	if (!fs::is_regular_file(cfg.executablePath))
	{
		std::cout << "Invalid local executable name: " << cfg.executablePath << "\n";
		return false;
	}

	if (!ParseOptions(cmd, cfg)) {
		std::cerr << KRED << "[BREAKING] Invalid/incomplete configuration\n" << RST;
		return 1;
	}

	/*if (cmd.has("interactive"))
		if (!RunInteractiveConfig(cfg))
			return false;*/

	if (!ParsePaths(cmd, cfg)) {
		std::cerr << KRED << "[BREAKING] Invalid/incomplete configuration\n" << RST;
		return 1;
	}

	std::cout << "Configuration: '" << cfg.mergedName() << "'\n";
    return true;
}

bool Configuration::ParseOptions(const Commandline& cmd, Configuration& cfg)
{
    // -build=windows
    // -build=windows.release
    // -build=windows.release.static
    // -build=windows.release.static.shipment
    // -build=windows.release.static.shipment

    bool hasGeneratorType = false;

    const auto& buildString = cmd.get("build");
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
                    std::cerr << KRED << "[BREAKING] Platform was already defined in build configuration string\n" << RST;
                    return false;
                }

                hasPlatform = true;
            }
            else if (ParseLibraryType(part, cfg.libs))
            {
                if (hasLibsType)
                {
                    std::cerr << KRED << "[BREAKING] Library type was already defined in build configuration string\n" << RST;
                    return false;
                }

                hasLibsType = true;
            }
            else if (ParseConfigurationType(part, cfg.configuration))
            {
                if (hasConfigType)
                {
                    std::cerr << KRED << "[BREAKING] Configuration type was already defined in build configuration string\n" << RST;
                    return false;
                }

                hasConfigType = true;
            }
            else if (ParseGeneratorType(part, cfg.generator))
            {
                if (hasGeneratorType)
                {
                    std::cerr << KRED << "[BREAKING] Generator type was already defined in build configuration string\n" << RST;
                    return false;
                }

                hasGeneratorType = true;
            }
            else if (part == "shipment")
            {
                if (cfg.flagShipmentBuild)
                {
                    std::cerr << KRED << "[BREAKING] Shipment flag was already defined in build configuration string\n" << RST;
                    return false;
                }

                cfg.flagShipmentBuild = true;
            }
            else
            {
                std::cerr << KRED << "[BREAKING] Invalid build configuration token '" << part << "' that does not match any platform, generator, library type or other shit\n" << RST;
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
        std::cout << "Enabled static content generation\n";
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
				std::cerr << KRED << "[BREAKING] Build tool run in a directory without \"build.xml\", did you run the configure command?\n" << RST;
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
                std::cerr << KRED << "[BREAKING] Specified module path '" << str << " does not exist\n" << RST;
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
				std::cerr << KRED << "[BREAKING] Failed to create temp directory " << cfg.tempPath << "\n" << RST;
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
				std::cerr << KRED << "[BREAKING] Failed to create temp directory " << cfg.cachePath << "\n" << RST;
				return false;
			}
		}
	}

    // derived paths
    cfg.derivedConfigurationPath = (cfg.tempPath / cfg.mergedName()).make_preferred();
    cfg.derivedBinaryPath = (cfg.derivedConfigurationPath / "bin").make_preferred();
    cfg.derivedSolutionPath = (cfg.derivedConfigurationPath / "build").make_preferred();

    return true;
}

//--
