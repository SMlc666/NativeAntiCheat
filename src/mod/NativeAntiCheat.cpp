#include "mod/NativeAntiCheat.h"

#include "ll/api/mod/RegisterHelper.h"

namespace native_ac {

NativeAntiCheat& NativeAntiCheat::getInstance() {
    static MyMod instance;
    return instance;
}

bool NativeAntiCheat::load() {
    getSelf().getLogger().debug("Loading...");
    // Code for loading the mod goes here.
    return true;
}

bool NativeAntiCheat::enable() {
    getSelf().getLogger().debug("Enabling...");
    // Code for enabling the mod goes here.
    return true;
}

bool NativeAntiCheatod::disable() {
    getSelf().getLogger().debug("Disabling...");
    // Code for disabling the mod goes here.
    return true;
}

} // namespace native_ac

LL_REGISTER_MOD(native_ac::NativeAntiCheat, native_ac::NativeAntiCheat::getInstance());
