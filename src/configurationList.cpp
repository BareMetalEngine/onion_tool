#include "common.h"
#include "utils.h"
#include "configuration.h"
#include "configurationList.h"
#include "xmlUtils.h"

//--

ConfigurationList::ConfigurationList()
{}

ConfigurationList::~ConfigurationList()
{}

bool ConfigurationList::Compare(const Configuration& a, const Configuration& b)
{
	if (a.build != b.build) return false;
	if (a.platform != b.platform) return false;
	if (a.configuration != b.configuration) return false;
	if (a.generator != b.generator) return false;
	if (a.libs != b.libs) return false;
	if (a.modulePath != b.modulePath) return false;
	if (a.solutionPath != b.solutionPath) return false;
	if (a.deployPath != b.deployPath) return false;
	return true;
}

bool ConfigurationList::append(const Configuration& cfg)
{
	for (const auto& existing : m_configurations)
		if (Compare(cfg, existing))
			return false;

	m_configurations.push_back(cfg);
	return true;
}

void ConfigurationList::collect(PlatformType platform, std::vector<Configuration>& outConfigurations) const
{
	for (const auto& cfg : m_configurations)
		if (cfg.platform == platform)
			outConfigurations.push_back(cfg);
}

bool ConfigurationList::save(const fs::path& path)
{
	std::stringstream f;

	const auto rootDir = fs::weakly_canonical(fs::absolute(path.parent_path()));

	writeln(f, "<?xml version=\"1.0\" encoding=\"utf-8\"?>");
	writeln(f, "<ConfigurationList>");

	for (const auto& cfg : m_configurations)
	{
		writelnf(f, "  <Configuration name=\"%hs\">", cfg.mergedName().c_str());
		writelnf(f, "    <ConfigurationType>%hs</ConfigurationType>", NameEnumOption(cfg.configuration).data());
		writelnf(f, "    <PlatformType>%hs</PlatformType>", NameEnumOption(cfg.platform).data());
		writelnf(f, "    <GeneratorType>%hs</GeneratorType>", NameEnumOption(cfg.generator).data());
		writelnf(f, "    <BuildType>%hs</BuildType>", NameEnumOption(cfg.build).data());
		writelnf(f, "    <LibraryType>%hs</LibraryType>", NameEnumOption(cfg.libs).data());

		{
			const auto normalizedPath = fs::relative(cfg.modulePath, rootDir);
			writelnf(f, "    <ModulePath>%hs</ModulePath>", normalizedPath.u8string().c_str());
		}

		{
			const auto normalizedPath = fs::relative(cfg.solutionPath, rootDir);
			writelnf(f, "    <SolutionPath>%hs</SolutionPath>", normalizedPath.u8string().c_str());
		}

		{
			const auto normalizedPath = fs::relative(cfg.deployPath, rootDir);
			writelnf(f, "    <DeployPath>%hs</DeployPath>", normalizedPath.u8string().c_str());
		}

		writeln(f, "  </Configuration>");
	}

	writeln(f, "</ConfigurationList>");

	return SaveFileFromString(path, f.str(), false, false);
}

static bool ParseRelativePath(std::string_view path, const fs::path& basePath, fs::path& outPath)
{
	outPath = fs::weakly_canonical(fs::absolute((basePath / path).make_preferred()));
	return true;
}

static bool ParseConfiguration(const XMLNode* node, Configuration& cfg, const fs::path& basePath)
{
	const auto name = XMLNodeAttrbiute(node, "name");
	if (name.empty())
		return false;

	bool valid = true;
	XMLNodeIterate(node, [&valid, &cfg, basePath, name](const XMLNode* node, std::string_view option)
		{
			if (option == "BuildType")
				valid &= ParseBuildType(XMLNodeValue(node), cfg.build);
			else if (option == "ConfigurationType")
				valid &= ParseConfigurationType(XMLNodeValue(node), cfg.configuration);
			else if (option == "LibraryType")
				valid &= ParseLibraryType(XMLNodeValue(node), cfg.libs);
			else if (option == "PlatformType")
				valid &= ParsePlatformType(XMLNodeValue(node), cfg.platform);
			else if (option == "GeneratorType")
				valid &= ParseGeneratorType(XMLNodeValue(node), cfg.generator);
			else if (option == "ModulePath")
				valid &= ParseRelativePath(XMLNodeValue(node), basePath, cfg.modulePath);
			else if (option == "DeployPath")
				valid &= ParseRelativePath(XMLNodeValue(node), basePath, cfg.deployPath);
			else if (option == "SolutionPath")
				valid &= ParseRelativePath(XMLNodeValue(node), basePath, cfg.solutionPath);
			else
			{
				std::cerr << "Unknown configuration option '" << option << "' at configuration " << name << "\n";
				valid = false;
			}
		});

	return valid;
}

std::unique_ptr<ConfigurationList> ConfigurationList::Load(const fs::path& path)
{
	if (!fs::is_regular_file(path))
		return std::make_unique<ConfigurationList>();

	std::string txt;
	if (!LoadFileToString(path, txt))
		return nullptr;

	XMLDoc doc;
	try
	{
		doc.parse<0>((char*)txt.c_str());
	}
	catch (std::exception& e)
	{
		std::cerr << KYEL << "[WARNING] Error parsing XML " << path << ": " << e.what() << "\n" << RST;
		return std::make_unique<ConfigurationList>();
	}

	const auto* root = doc.first_node("ConfigurationList");
	if (!root)
	{
		std::cerr << KYEL << "[WARNING] File at " << path << " is not a module configuration file\n" << RST;
		return std::make_unique<ConfigurationList>();
	}

	auto ret = std::make_unique<ConfigurationList>();

	const auto rootPath = fs::weakly_canonical(fs::absolute(path.parent_path()));

	XMLNodeIterate(root, "Configuration", [&ret, rootPath](const XMLNode* node)
		{
			Configuration cfg;
			if (ParseConfiguration(node, cfg, rootPath))
				ret->append(cfg);
		});

	return ret;
}

//--
