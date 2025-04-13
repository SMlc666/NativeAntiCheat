#include "ConfigHelper.hpp"
#include "mod/Module/Module.hpp"
#include <fmt/format.h>
#include <fstream>
#include <nlohmann/json.hpp>

namespace native_ac {
std::optional<std::string> ConfigHelper::LoadConfig(const std::string& config_file, ModuleManager& module_manager) {
    std::ifstream file(config_file);
    if (!file.is_open()) {
        return fmt::format("Failed to open config file: {}", config_file);
    }
    nlohmann::json config_json;
    try {
        config_json = nlohmann::json::parse(file);
    } catch (const nlohmann::json::parse_error& e) {
        return fmt::format("Failed to parse config file: {}. Error message: {}", config_file, e.what());
    }
    try {
        module_manager.forEachModule([&](Module* module) {
            try {
                module->from_jsonE(config_json[module->getName()]);
            } catch (const nlohmann::json::exception& e) {
                throw fmt::format(
                    "Failed to parse config for module: {}. Error message: {}",
                    module->getName(),
                    e.what()
                );
            }
            return true;
        });
    } catch (const std::exception& e) {
        return fmt::format("Failed to load config file: {}. Error message: {}", config_file, e.what());
    }
    return std::nullopt;
}
std::optional<std::string> ConfigHelper::SaveConfig(const std::string& config_file, ModuleManager& module_manager) {
    nlohmann::json config_json;
    module_manager.forEachModule([&](Module* module) {
        module->to_jsonE(config_json[module->getName()]);
        return true;
    });
    std::ofstream file(config_file);
    if (!file.is_open()) {
        return fmt::format("Failed to open config file: {}", config_file);
    }
    try {
        file << config_json.dump(4);
    } catch (const std::exception& e) {
        return fmt::format("Failed to save config file: {}. Error message: {}", config_file, e.what());
    }
    return std::nullopt;
}
} // namespace native_ac