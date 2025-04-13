#pragma once
#include "mod/Module/ModuleManager.hpp"
#include <optional>
#include <string>
namespace native_ac {
class ConfigHelper {
public:
    static std::optional<std::string> /*error message*/
    LoadConfig(const std::string& config_file, ModuleManager& module_manager);
    static std::optional<std::string> /*error message*/
    SaveConfig(const std::string& config_file, ModuleManager& module_manager);
};
} // namespace native_ac