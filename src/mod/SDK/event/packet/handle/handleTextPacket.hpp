#pragma once

#include "handlePacketEvent.hpp"
#include "ll/api/base/Macro.h"
#include "mc/network/packet/TextPacket.h"
#include "mod/SDK/event/packet/PacketEvent.hpp"


namespace native_ac::event {
class HandleTextPacketEvent final : public HandlePacketEvent {
public:
    constexpr explicit HandleTextPacketEvent(NetworkIdentifier& networkIdentifier, TextPacket* packet)
    : HandlePacketEvent(networkIdentifier, packet) {}
    TextPacket* getPacket() const { return static_cast<TextPacket*>(PacketEvent::getPacket()); };
};
} // namespace native_ac::event