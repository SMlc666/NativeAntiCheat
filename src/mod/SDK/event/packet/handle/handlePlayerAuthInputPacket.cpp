#include "handlePlayerAuthInputPacket.hpp"
#include "ll/api/event/Emitter.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"
#include "mc/network/ServerNetworkHandler.h"
namespace native_ac::event {
LL_TYPE_INSTANCE_HOOK(
    handlePlayerAuthInputPacketEventHook,
    ll::memory::HookPriority::Highest,
    ServerNetworkHandler,
    &ServerNetworkHandler::$handle,
    void,
    NetworkIdentifier const&     source,
    PlayerAuthInputPacket const& packet
) {
    native_ac::event::HandlePlayerAuthInputPacketEvent event{
        const_cast<NetworkIdentifier&>(source),
        const_cast<PlayerAuthInputPacket*>(&packet)
    };
    ll::event::EventBus::getInstance().publish(event);
    if (!event.isCancelled()) {
        origin(source, packet);
    }
}
static std::unique_ptr<ll::event::EmitterBase> emitterFactory();
class handlePlayerAuthInputPacketEventEmitter
: public ll::event::Emitter<emitterFactory, HandlePlayerAuthInputPacketEvent> {
    ll::memory::HookRegistrar<handlePlayerAuthInputPacketEventHook> hook;
};

static std::unique_ptr<ll::event::EmitterBase> emitterFactory() {
    return std::make_unique<handlePlayerAuthInputPacketEventEmitter>();
}

} // namespace native_ac::event