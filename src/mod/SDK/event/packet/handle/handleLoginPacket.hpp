#pragma once

#include "mc/network/packet/LoginPacket.h"
#include "mod/SDK/event/packet/handle/handlePacketEvent.hpp"
namespace native_ac::event {
class HandleLoginPacket final : public HandlePacketEvent {
public:
    constexpr explicit HandleLoginPacket(NetworkIdentifier& networkIdentifier, LoginPacket* packet)
    : HandlePacketEvent(networkIdentifier, packet) {};
    LoginPacket* getPacket() const { return static_cast<LoginPacket*>(PacketEvent::getPacket()); };
};
} // namespace native_ac::event