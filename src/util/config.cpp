#include "config.hpp"
#include <fstream>
#include <iostream>
#include <spdlog/spdlog.h>

nlohmann::json load_config(const std::string& config_path) {
    nlohmann::json j;
    std::ifstream ifs(config_path);
    if (!ifs.is_open()) {
        spdlog::error("Failed to open config file: {}", config_path);
        return {};
    }
    try {
        ifs >> j;
    } catch (const std::exception& e) {
        spdlog::error("Failed to parse config JSON: {}", e.what());
        return {};
    }
    return j;
}
