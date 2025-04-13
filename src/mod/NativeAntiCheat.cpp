#include "mod/NativeAntiCheat.h"

#include "Module/ConfigHelper.hpp"
#include "ll/api/io/LogLevel.h"
#include "ll/api/mod/RegisterHelper.h"
#include "mod/BenchMark/BenchMark.hpp"
#include "mod/Module/ModuleManager.hpp"
#include <optional>

namespace native_ac {
NativeAntiCheat& NativeAntiCheat::getInstance() {
    static NativeAntiCheat instance;
    return instance;
}

bool NativeAntiCheat::load() {
    mConfigPath = getSelf().getConfigDir().string() + "/config.json";
#ifdef DEBUG
    getSelf().getLogger().setLevel(ll::io::LogLevel::Debug);
    getSelf().getLogger().debug("Loading NativeAntiCheat...");
    BenchMark loadBench([this](Timer& time) {
        getSelf().getLogger().debug("Load NativeAntiCheat using time: {}", time.elapsed());
    });
    getSelf().getLogger().debug("mConfigPath: {}", mConfigPath);
#endif
    native_ac::ModuleManager::getInstance().registerModulesFromRegistry();
    if (!std::filesystem::exists(mConfigPath)) {
        getSelf().getLogger().info("no config file found, using default config");
#ifdef DEBUG
        getSelf().getLogger().debug(
            "Creating config directory if not exists: {}",
            std::filesystem::path(mConfigPath).parent_path().string()
        );
#endif
        std::filesystem::create_directories(std::filesystem::path(mConfigPath).parent_path()); // 确保目录存在
#ifdef DEBUG
        getSelf().getLogger().debug("Config directory created or already exists.");
#endif
        std::optional<std::string> config_result =
            ConfigHelper::SaveConfig(mConfigPath, native_ac::ModuleManager::getInstance());
        if (config_result.has_value()) {
            getSelf().getLogger().error("Failed to save config: {}", config_result.value());
            return false;
        }
    }
    std::optional<std::string> config_result =
        ConfigHelper::LoadConfig(mConfigPath, native_ac::ModuleManager::getInstance());
    if (config_result.has_value()) {
        getSelf().getLogger().error("Failed to load config: {}", config_result.value());
        return false;
    }
    ModuleManager::getInstance().forEachModule([this](Module* module) {
#ifdef DEBUG
        BenchMark loadModuleBench([this, &module](Timer& time) {
            getSelf().getLogger().debug("Load Module :{} using time: {}", module->getName(), time.elapsed());
        });
        getSelf().getLogger().debug("load Module :{}", module->getName());
#endif
        if (!module->loadE()) {
            getSelf().getLogger().error("Failed to load Module :{}", module->getName());
            return false;
        }
        return true;
    });
    return true;
}

bool NativeAntiCheat::enable() {
#ifdef DEBUG
    getSelf().getLogger().debug("Enabling NativeAntiCheat...");
    BenchMark loadModuleBench([this](Timer& time) {
        getSelf().getLogger().debug("Enabling NativeAntiCheat using time: {}", time.elapsed());
    });
#endif
    ModuleManager::getInstance().forEachModule([this](Module* module) {
        if (module->shouldEnable()) {
#ifdef DEBUG
            NativeAntiCheat::getInstance().getSelf().getLogger().debug("Enabling module: {}", module->getName());
            BenchMark bench_mark([&module](Timer& timer) {
                NativeAntiCheat::getInstance().getSelf().getLogger().debug(
                    "Time elapsed for enabling module: {}: {}",
                    module->getName(),
                    timer.elapsed()
                );
            });
#endif
            if (!module->enableE()) {
                getSelf().getLogger().error("Failed to enable module: {}", module->getName());
                return true;
            }
        }
        return true;
    });
    // Code for enabling the mod goes here.
    return true;
}

bool NativeAntiCheat::disable() {
#ifdef DEBUG
    getSelf().getLogger().debug("Disabling NativeAntiCheat...");
    BenchMark loadModuleBench([this](Timer& time) {
        getSelf().getLogger().debug("Enabling NativeAntiCheat using time: {}", time.elapsed());
    });
#endif
    ModuleManager::getInstance().forEachModule([this](Module* module) {
        if (module->isEnaled()) {
#ifdef DEBUG
            NativeAntiCheat::getInstance().getSelf().getLogger().debug("Disabling module: {}", module->getName());
            BenchMark bench_mark([&module](Timer& timer) {
                NativeAntiCheat::getInstance().getSelf().getLogger().debug(
                    "Time elapsed for disabling module: {}: {}",
                    module->getName(),
                    timer.elapsed()
                );
            });
#endif
            if (!module->disableE()) {
                getSelf().getLogger().error("Failed to disable module: {}", module->getName());
                return true;
            }
        }
        return true;
    });
    std::optional<std::string> config_result = ConfigHelper::SaveConfig(mConfigPath, ModuleManager::getInstance());
    if (config_result.has_value()) {
        getSelf().getLogger().error("Failed to save config: {}", config_result.value());
        return false;
    }
    // Code for disabling the mod goes here.
    return true;
}

} // namespace native_ac

LL_REGISTER_MOD(native_ac::NativeAntiCheat, native_ac::NativeAntiCheat::getInstance());
