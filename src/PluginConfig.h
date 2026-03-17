#ifndef PLUGINCONFIG_H
#define PLUGINCONFIG_H

// --- C++ Standard ---
#include <string>

struct PluginConfig {
    // spdlog
    std::string log_path;
    std::string log_file;
    int max_files;
    int flush_interval_sec;
    std::string pattern;
    std::string level;
    std::string flush_level;
    bool truncate;

    // curl
    std::string base_url;
	std::string airport_endpoint;
	std::string version_endpoint;
    int timeout_sec;
    bool verify_ssl;
    std::string user_agent;
    std::string header_accept;
    std::string header_content_type;

    //Colours
    COLORREF TAG_GREEN = RGB(0, 190, 0);
    COLORREF TAG_YELLOW = RGB(241, 121, 0);
    COLORREF TAG_RED = RGB(190, 0, 0);

};

bool InitialiseConfig(const std::string& filepath, PluginConfig& plugin_config);


#endif //PLUGINCONFIG_H
