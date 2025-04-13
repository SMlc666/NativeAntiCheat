#pragma once

#include "mod/Module/Module.hpp"
#include "mod/Module/ModuleManager.hpp"
namespace native_ac {
class MuteModule : public Module {
public:
    MuteModule();
    bool load() override;
    bool enable() override;
    bool disable() override;
};
} // namespace native_ac