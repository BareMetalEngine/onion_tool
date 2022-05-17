#pragma once

//--

struct Configuration;

class ConfigurationList
{
public:
    ConfigurationList();
    ~ConfigurationList();

    bool save(const fs::path& path);
    bool append(const Configuration& cfg);
    void collect(PlatformType platform, std::vector<Configuration>& outConfigurations) const;

    static std::unique_ptr<ConfigurationList> Load(const fs::path& path);

private:
    static bool Compare(const Configuration& a, const Configuration& b);

	std::vector<Configuration> m_configurations;
};

//--
