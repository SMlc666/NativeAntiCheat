#include "ModuleManager.hpp"
#include "mod/NativeAntiCheat.h"
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <vector>


namespace native_ac {

namespace detail {
std::vector<ModuleFactory>& GetGlobalModuleRegistry() {
    static std::vector<ModuleFactory> registry;
    return registry;
}

ModuleRegistrar::ModuleRegistrar(ModuleFactory factory) { GetGlobalModuleRegistry().push_back(std::move(factory)); }
} // namespace detail

ModuleManager& ModuleManager::getInstance() {
    static std::once_flag ModuleManagerFlag;
    static ModuleManager* ModuleManagerInstance = nullptr;
    std::call_once(ModuleManagerFlag, [&]() { ModuleManagerInstance = new ModuleManager(); });
    if (!ModuleManagerInstance) {
        throw std::runtime_error("Failed to initialize ModuleManager singleton.");
    }
    return *ModuleManagerInstance;
}

Module* ModuleManager::getModuleByName(std::string_view moduleName) {
    auto it = mModuleList.find(std::string(moduleName));
    if (it != mModuleList.end()) {
        return it->second; // Now stores Module* directly
    }
#ifdef DEBUG
    NativeAntiCheat::getInstance().getSelf().getLogger().warn("Module not found: {}", moduleName);
#endif
    return nullptr;
}

bool ModuleManager::addModule(std::string_view name, Module* module) {
    if (!module) {
#ifdef DEBUG
        NativeAntiCheat::getInstance().getSelf().getLogger().error(
            "Attempted to add a null module with name: {}",
            name
        );
#endif
        return false;
    }
    auto result = mModuleList.try_emplace(std::string(name), module); // Store raw pointer, no move needed
    if (!result.second) {
#ifdef DEBUG
        NativeAntiCheat::getInstance().getSelf().getLogger().warn("Module {} already exists, not adding again.", name);
#endif
        return false;
    }
    return true;
}

bool ModuleManager::addModule(Module* module) {
    if (!module) {
#ifdef DEBUG
        NativeAntiCheat::getInstance().getSelf().getLogger().error("Attempted to add a null module.");
#endif
        return false;
    }
    // 获取模块名称前检查 module 是否有效 (已在 addModule(name, module) 中处理)
    std::string moduleNameStr = module->getName();
    return addModule(moduleNameStr, module); // Pass raw pointer, no move needed
}

void ModuleManager::forEachModule(std::function<bool(Module*)> callback) {
    for (auto& [name, module] : mModuleList) {
        if (module && !callback(module)) { // module is now Module*
            break;
        }
    }
}

void ModuleManager::registerModulesFromRegistry() {
    auto& registry = detail::GetGlobalModuleRegistry();
#ifdef DEBUG
    NativeAntiCheat::getInstance().getSelf().getLogger().info(
        "Registering {} modules from global registry...",
        registry.size()
    );
#endif
    for (const auto& factory : registry) {
        if (factory) {
            auto module = factory();
            if (module) {
                addModule(module); // module is now Module*, no move needed
            } else {
#ifdef DEBUG
                NativeAntiCheat::getInstance().getSelf().getLogger().error(
                    "A module factory returned a null module during registration."
                );
#endif
            }
        } else {
#ifdef DEBUG
            NativeAntiCheat::getInstance().getSelf().getLogger().error("Found a null factory in the module registry.");
#endif
        }
    }
#ifdef DEBUG
    NativeAntiCheat::getInstance().getSelf().getLogger().info(
        "Module registration complete. Total modules: {}",
        mModuleList.size()
    );
#endif
}

} // namespace native_ac