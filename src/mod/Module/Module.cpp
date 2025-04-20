#include "Module.hpp"
#include "mod/NativeAntiCheat.h"
namespace native_ac {
std::string Module::getName() { return mName; }
bool        Module::loadE() {

    if (mIsLoaded) {
#ifdef DEBUG
        NativeAntiCheat::getInstance().getSelf().getLogger().error("override load module{}", this->getName());
#endif
        return false;
    }
    mIsLoaded = true;
    return load();
}
bool Module::enableE() {
    if (mIsEnabled) {
        return false;
    }
    mIsEnabled = true;
    return enable();
}
bool Module::disableE() {
    if (!mIsEnabled) {
        return false;
    }
    mIsEnabled = false;
    return disable();
}
void Module::to_jsonE(nlohmann::json& j) const {
    j["status"] = mShouldEnable;
    to_json(j);
}
void Module::from_jsonE(const nlohmann::json& j) {
    mShouldEnable = j.at("status").get<bool>();
    from_json(j);
}
} // namespace native_ac