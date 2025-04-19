#pragma once

#include "ll/api/event/Event.h"
#include "mc/network/packet/Packet.h"

namespace native_ac::event {
class PacketEvent : public ll::event::Event {
    Packet* packet;

public:
    Packet* getPacket() const { return packet; }
    constexpr explicit PacketEvent(Packet* packet) : packet(packet) {}
};
} // namespace native_ac::event