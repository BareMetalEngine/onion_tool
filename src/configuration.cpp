#include "common.h"
#include "utils.h"
#include "configuration.h"

//--

Configuration::Configuration()
{
	build = BuildType::Development;
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
    ret += NameEnumOption(build);
    ret += ".";
    ret += NameEnumOption(libs);
    ret += ".";
    ret += NameEnumOption(configuration);
    return ret;
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
    if (parts.size() != 5)
        return false;

    bool valid = ParsePlatformType(parts[0], platform);
    valid &= ParseGeneratorType(parts[1], generator);
    valid &= ParseBuildType(parts[2], build);
    valid &= ParseLibraryType(parts[3], libs);
    valid &= ParseConfigurationType(parts[4], configuration);
    return valid;
}

bool Configuration::parseOptions(const char* path, const Commandline& cmd)
{
    builderExecutablePath = fs::absolute(path);
    if (!fs::is_regular_file(builderExecutablePath))
    {
        std::cout << "Invalid local executable name: " << builderExecutablePath << "\n";
        return false;
    }

    /*builderEnvPath = builderExecutablePath.parent_path().parent_path();
    //std::cout << "EnvPath: " << builderEnvPath << "\n";

    if (!fs::is_directory(builderEnvPath / "vs"))
    {
        std::cout << "BuildTool is run from invalid directory and does not have required local files (vs folder is missing)\n";
        return false;
    }

    if (!fs::is_directory(builderEnvPath / "cmake"))
    {
        std::cout << "BuildTool is run from invalid directory and does not have required local files (cmake folder is missing)\n";
        return false;
    }*/

    {
        const auto& str = cmd.get("build");
        if (!str.empty())
        {
            if (!ParseBuildType(str, this->build))
            {
                std::cout << "Invalid build type '" << str << "'specified\n";
                return false;
            }
        }
    }

    {
        const auto& str = cmd.get("platform");
        if (!str.empty())
        {
            if (!ParsePlatformType(str, this->platform))
            {
                std::cout << "Invalid platform type '" << str << "'specified\n";
                return false;
            }
        }
    }

    {
        const auto& str = cmd.get("generator");
        if (str.empty())
        {
            if (this->platform == PlatformType::Windows || this->platform == PlatformType::UWP)
                this->generator = GeneratorType::VisualStudio22;
            else if (this->platform == PlatformType::Prospero || this->platform == PlatformType::Scarlett)
                this->generator = GeneratorType::VisualStudio22;
            else
                this->generator = GeneratorType::CMake;
        }
        else if (!ParseGeneratorType(str, this->generator))
        {
            std::cout << "Invalid generator type '" << str << "'specified\n";
            return false;
        }
    }

    {
        const auto& str = cmd.get("libs");
        if (!str.empty())
        {
            if (!ParseLibraryType(str, this->libs))
            {
                std::cout << "Invalid library type '" << str << "'specified\n";
                return false;
            }
        }
    }

    {
        const auto& str = cmd.get("config");
        if (!str.empty())
        {
            if (!ParseConfigurationType(str, this->configuration))
            {
                std::cout << "Invalid configuration type '" << str << "'specified\n";
                return false;
            }
        }
    }

    //force = cmd.has("force");
    if (generator == GeneratorType::CMake || cmd.has("static"))
        staticBuild = true;

    return true;
}

bool Configuration::parsePaths(const char* executable, const Commandline& cmd)
{
    //--

	{
		const auto& str = cmd.get("module");
		if (str.empty())
		{
			auto testPath = fs::current_path() / CONFIGURATION_NAME;
			if (!fs::is_regular_file(testPath))
			{
				std::cerr << KRED << "[BREAKING] Onion build tool run in a directory without \"" << CONFIGURATION_NAME << "\", did you run the configure command?\n" << RST;
				return false;
			}
			else
			{
				modulePath = fs::weakly_canonical(fs::absolute(fs::current_path()));
			}
		}
		else
		{
            auto testPath = fs::path(str) / CONFIGURATION_NAME;
			if (!fs::is_regular_file(testPath))
			{
                std::cerr << KRED << "[BREAKING] Specified module path '" << str << "' does not contain \"" << CONFIGURATION_NAME << "\" that is required to recognize directory as Onion Module\n" << RST;
				return false;
			}

			modulePath = fs::weakly_canonical(fs::absolute(fs::path(str).make_preferred()));
		}
	}

    fs::path buildPath = modulePath;

    {
		const auto& str = cmd.get("buildDir");
        if (!str.empty())
            buildPath = str;
    }

    {
        const auto& str = cmd.get("deployDir");
        if (str.empty())
        {
            //std::cout << "No deploy directory specified, using default one\n";

            std::string solutionPartialPath = ".bin/";
            solutionPartialPath += mergedName();

            this->deployPath = buildPath / solutionPartialPath;
        }
        else
        {
            this->deployPath = fs::weakly_canonical(fs::absolute(fs::path(str).make_preferred()));
        }

        this->deployPath.make_preferred();

        std::error_code ec;
        if (!fs::is_directory(deployPath, ec))
        {
            if (!fs::create_directories(deployPath, ec))
            {
                std::cerr << KRED << "[BREAKING] Failed to create deploy directory " << deployPath << "\n" << RST;
                return false;
            }
        }
    }

    {
        const auto& str = cmd.get("outDir");
        if (str.empty())
        {
            //std::cout << "No output directory specified, using default one\n";

            std::string solutionPartialPath = ".temp/";
            this->tempPath = buildPath / solutionPartialPath;

			solutionPartialPath += mergedName();
            this->solutionPath = buildPath / solutionPartialPath;
        }
        else
        {
            this->solutionPath = fs::weakly_canonical(fs::absolute(fs::path(str).make_preferred()));
            this->tempPath = this->solutionPath;
        }

        std::error_code ec;
        if (!fs::is_directory(solutionPath, ec))
        {
            if (!fs::create_directories(solutionPath, ec))
            {
                std::cerr << KRED << "[BREAKING] Failed to create solution directory " << solutionPath << "\n" << RST;
                return false;
            }
        }
    }

	/*{
		auto str = cmd.get("thirdPartyDir");
        if (str.empty())
        {
            const auto path = modulePath / ".thirdparty";
            std::ifstream file(path);
            if (!file.fail())
            {
                if (std::getline(file, str))
                    localLibraryPath = str;
            }
        }
        else
        {
            localLibraryPath = str;
        }

        if (localLibraryPath.empty())
        {
            std::cout << "Failed to determine path to third-party libraries.\n";
            std::cout << "Run 'generate.py' or manually specify path to folder with third-party libraries in .thridparty file\n";
            return false;
        }
	}*/

    solutionPath = solutionPath.make_preferred();
    deployPath = deployPath.make_preferred();
    modulePath = modulePath.make_preferred();

	std::cout << "Module path: " << modulePath << "\n";
	std::cout << "Solution path: " << solutionPath << "\n";
	std::cout << "Deploy path: " << deployPath << "\n";

    return true;
}

//--
