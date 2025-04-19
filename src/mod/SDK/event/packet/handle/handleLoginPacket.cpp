#include "handleLoginPacket.hpp"
#include "ll/api/event/Emitter.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"
#include "mc/network/ServerNetworkHandler.h"
namespace native_ac::event {
LL_TYPE_INSTANCE_HOOK(
    handleLoginPacketEventHook,
    ll::memory::HookPriority::Highest,
    ServerNetworkHandler,
    &ServerNetworkHandler::$handle,
    void,
    NetworkIdentifier const& source,
    LoginPacket const&       packet
) {
    native_ac::event::HandleLoginPacket event{
        const_cast<NetworkIdentifier&>(source),
        const_cast<LoginPacket*>(&packet)
    };
    ll::event::EventBus::getInstance().publish(event);
    if (!event.isCancelled()) {
        origin(source, packet);
    }
}
static std::unique_ptr<ll::event::EmitterBase> emitterFactory();
class handleLoginPacketEventEmitter : public ll::event::Emitter<emitterFactory, HandleLoginPacket> {
    ll::memory::HookRegistrar<handleLoginPacketEventHook> hook;
};

static std::unique_ptr<ll::event::EmitterBase> emitterFactory() {
    return std::make_unique<handleLoginPacketEventEmitter>();
}

} // namespace native_ac::event