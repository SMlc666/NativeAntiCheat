#pragma once
#include "ll/api/event/Listener.h"
#include "mc/network/packet/PlayerInputTick.h"
#include "mc/platform/UUID.h"
#include "mod/Module/Module.hpp"
#include "mod/SDK/event/packet/handle/handlePlayerAuthInputPacket.hpp"
#include "parallel_hashmap/phmap.h"

namespace native_ac {
class PacketVerifyModule : public Module {
private:
    bool VerifyPlayerAuthInputPacket           = true;
    bool VerifyPlayerAuthInputPacketClientTick = true;
    int  MaxPlayerInputTickDiff                = 1000;
    int  MinPlayerInputTickDiff                = 1;
    bool DisableMovePlayerPacket               = false;
    std::shared_ptr<ll::event::Listener<event::HandlePlayerAuthInputPacketEvent>> mPlayerAuthInputPacketListener{};
    phmap::flat_hash_map<mce::UUID, PlayerInputTick>                              mPlayerLastInputTick{};

public:
    static PacketVerifyModule* getInstance();
    PacketVerifyModule() : Module("PacketVerify") {}
    bool load() override;
    bool enable() override;
    bool disable() override;
};
} // namespace native_ac