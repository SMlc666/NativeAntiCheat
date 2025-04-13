#include "mod/NativeAntiCheat.h"

#include "gsl/util"
#include "ll/api/io/LogLevel.h"
#include "ll/api/mod/RegisterHelper.h"
#include "mod/BenchMark/BenchMark.hpp"
#include "mod/Module/ModuleManager.hpp"
#include "mod/Module/Modules/Mute.hpp" // Include MuteModule header

namespace native_ac {
NativeAntiCheat& NativeAntiCheat::getInstance() {
    static NativeAntiCheat instance;
    return instance;
}

bool NativeAntiCheat::load() {
#ifdef DEBUG
    getSelf().getLogger().setLevel(ll::io::LogLevel::Debug);
    getSelf().getLogger().debug("Loading NativeAntiCheat...");
    BenchMark loadBench([this](Timer& time) {
        getSelf().getLogger().debug("Load NativeAntiCheat using time: {}", time.elapsed());
    });
#endif
    native_ac::ModuleManager::getInstance().registerModulesFromRegistry();
    ModuleManager::getInstance().forEachModule([this](Module* module) {
#ifdef DEBUG
        BenchMark loadModuleBench([this, &module](Timer& time) {
            getSelf().getLogger().debug("Load Module :{} using time: {}", module->getName(), time.elapsed());
        });
        getSelf().getLogger().debug("load Module :{}", module->getName());
#endif
        module->load();
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
    // Code for disabling the mod goes here.
    return true;
}

} // namespace native_ac

LL_REGISTER_MOD(native_ac::NativeAntiCheat, native_ac::NativeAntiCheat::getInstance());
