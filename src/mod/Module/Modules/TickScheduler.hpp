#pragma once
#include "ll/api/base/StdInt.h"
#include "ll/api/event/Listener.h"
#include "ll/api/event/world/LevelTickEvent.h"
#include "mod/Module/Module.hpp"
#include <cstddef>
#include <unordered_map>

namespace native_ac {
class TickSchedulerModule : public Module {
private:
    struct TickSchedulerModuleImpl {
        std::function<void()> mCallback;
        size_t                mInterval;
        size_t                mRemainingTicks;
    };
    std::unordered_map<uint64, TickSchedulerModuleImpl>             TickCallbackList;
    std::atomic_uint64_t                                            HandleId{0};
    std::shared_ptr<ll::event::Listener<ll::event::LevelTickEvent>> tickListener;

public:
    TickSchedulerModule() : Module("TickScheduler") {}
    static TickSchedulerModule* getInstance();
    bool                        enable() override;
    bool                        disable() override;
    bool                        load() override;
    void*                       emplaceTickCallback(std::function<void()> callback, size_t interval);
    void                        removeTickCallback(void* handle);
};
} // namespace native_ac