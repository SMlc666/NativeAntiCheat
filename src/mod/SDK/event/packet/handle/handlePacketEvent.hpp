#pragma once
#include "ll/api/base/Macro.h"
#include "ll/api/event/Cancellable.h"
#include "mc/network/NetworkIdentifier.h"
#include "mod/SDK/event/packet/PacketEvent.hpp"

namespace native_ac::event {
class HandlePacketEvent : public ll::event::Cancellable<PacketEvent> {
    NetworkIdentifier& networkIdentifier;

public:
    constexpr explicit HandlePacketEvent(NetworkIdentifier& networkIdentifier, Packet* packet)
    : ll::event::Cancellable<PacketEvent>(packet),
      networkIdentifier(networkIdentifier) {}
    NetworkIdentifier& getNetworkIdentifier() { return networkIdentifier; }
};
} // namespace native_ac::event
