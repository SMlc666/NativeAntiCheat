#include "handleTextPacket.hpp"
#include "ll/api/event/Emitter.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"
#include "mc/network/ServerNetworkHandler.h"
#include "mc/network/packet/Packet.h"
#include "mc/network/packet/TextPacket.h"

namespace native_ac::event {
LL_TYPE_INSTANCE_HOOK(
    handleTextPacketEventHook,
    ll::memory::HookPriority::Highest,
    ServerNetworkHandler,
    &ServerNetworkHandler::$handle,
    void,
    NetworkIdentifier const& source,
    TextPacket const&        packet
) {
    native_ac::event::HandleTextPacketEvent event{
        const_cast<NetworkIdentifier&>(source),
        const_cast<TextPacket*>(&packet)
    };
    ll::event::EventBus::getInstance().publish(event);
    if (!event.isCancelled()) {
        origin(source, packet);
    }
}

static std::unique_ptr<ll::event::EmitterBase> emitterFactory();
class handleTextPacketEventEmitter : public ll::event::Emitter<emitterFactory, HandleTextPacketEvent> {
    ll::memory::HookRegistrar<handleTextPacketEventHook> hook;
};

static std::unique_ptr<ll::event::EmitterBase> emitterFactory() {
    return std::make_unique<handleTextPacketEventEmitter>();
}

} // namespace native_ac::event