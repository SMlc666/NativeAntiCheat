#pragma once
#include "ll/api/event/Listener.h"
#include "mc/network/packet/PlayerInputTick.h"
#include "mc/platform/UUID.h"
#include "mod/BenchMark/Timer.hpp"
#include "mod/Module/Module.hpp"
#include "mod/SDK/event/packet/handle/handlePlayerAuthInputPacket.hpp"
#include "mod/SDK/event/packet/handle/handleTextPacket.hpp"
#include "parallel_hashmap/phmap.h"

namespace native_ac {
class PacketVerifyModule : public Module {
private:
    bool VerifyPlayerAuthInputPacket           = true;
    bool VerifyPlayerAuthInputPacketClientTick = true;
    bool VerifyTextPacket                      = true;
    int  MaxPlayerInputTickDiff                = 1000;
    int  MinPlayerInputTickDiff                = 1;
    int  AllowedTextPacketSize                 = 1000;
    int  MinTextPacketIntervalMs               = 500; // 毫秒
    bool DisableMovePlayerPacket               = false;
    std::shared_ptr<ll::event::Listener<event::HandlePlayerAuthInputPacketEvent>> mPlayerAuthInputPacketListener{};
    std::shared_ptr<ll::event::Listener<event::HandleTextPacketEvent>>            mTextPacketListener{};
    phmap::flat_hash_map<mce::UUID, PlayerInputTick>                              mPlayerLastInputTick{};
    phmap::flat_hash_map<mce::UUID, Timer>                                        mPlayerTextPacketTimer{};

public:
    static PacketVerifyModule* getInstance();
    PacketVerifyModule() : Module("PacketVerify") {}
    bool load() override;
    bool enable() override;
    bool disable() override;
};
} // namespace native_ac