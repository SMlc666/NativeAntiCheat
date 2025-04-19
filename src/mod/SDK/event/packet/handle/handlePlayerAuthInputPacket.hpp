#pragma once

#include "handlePacketEvent.hpp"
#include "ll/api/base/Macro.h"
#include "mc/network/packet/PlayerAuthInputPacket.h"


namespace native_ac::event {
class HandlePlayerAuthInputPacketEvent final : public HandlePacketEvent {
public:
    constexpr explicit HandlePlayerAuthInputPacketEvent(
        NetworkIdentifier&     networkIdentifier,
        PlayerAuthInputPacket* packet
    )
    : HandlePacketEvent(networkIdentifier, packet) {}
    PlayerAuthInputPacket* getPacket() const { return static_cast<PlayerAuthInputPacket*>(PacketEvent::getPacket()); };
};
} // namespace native_ac::event