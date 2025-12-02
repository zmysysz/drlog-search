#pragma once
#include <string>
#include <nlohmann/json.hpp>

// Load JSON config from file. On error returns empty json object.
nlohmann::json load_config(const std::string& config_path);
