#pragma once

#include "ll/api/mod/NativeMod.h"

namespace native_ac {

class NativeAntiCheat {

public:
    static NativeAntiCheat& getInstance();

    NativeAntiCheat() : mSelf(*ll::mod::NativeMod::current()) {}

    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }

    /// @return True if the mod is loaded successfully.
    bool load();

    /// @return True if the mod is enabled successfully.
    bool enable();

    /// @return True if the mod is disabled successfully.
    bool disable();

    // TODO: Implement this method if you need to unload the mod.
    // /// @return True if the mod is unloaded successfully.
    // bool unload();

private:
    std::string         mConfigPath;
    ll::mod::NativeMod& mSelf;
};

} // namespace native_ac
