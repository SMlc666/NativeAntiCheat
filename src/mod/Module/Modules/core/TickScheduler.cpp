#include "TickScheduler.hpp"
#include "ll/api/event/EventBus.h"
#include "mod/Module/ModuleManager.hpp"
#include <atomic>
#include <mod/Module/ModuleManager.hpp>
namespace native_ac {
TickSchedulerModule* TickSchedulerModule::getInstance() {
    static TickSchedulerModule instance;
    return &instance;
}

bool TickSchedulerModule::enable() {
    tickListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::LevelTickEvent>(
        [this](ll::event::LevelTickEvent&) {
            for (auto& [handle, item] : TickCallbackList) {
                if (item.mRemainingTicks <= 0) {
                    item.mRemainingTicks = item.mInterval;
                    item.mCallback();
                } else {
                    --item.mRemainingTicks;
                }
            }
        }
    );
    return true;
}

bool TickSchedulerModule::disable() {
    ll::event::EventBus::getInstance().removeListener(tickListener);
    return true;
}

bool TickSchedulerModule::load() { return true; }

void* TickSchedulerModule::emplaceTickCallback(std::function<void()> callback, size_t interval) {
    const uint64            handle = ++HandleId;
    TickSchedulerModuleImpl item{callback, interval, interval};
    TickCallbackList.emplace(handle, std::move(item));
    return reinterpret_cast<void*>(handle);
}

void TickSchedulerModule::removeTickCallback(void* handle) {
    const uint64 key = reinterpret_cast<uint64>(handle);
    TickCallbackList.erase(key);
}

} // namespace native_ac
REGISTER_MODULE(native_ac::TickSchedulerModule, native_ac::TickSchedulerModule::getInstance());