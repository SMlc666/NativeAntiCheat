#pragma once

#include "ll/api/event/Listener.h"
#include "mc/network/NetworkIdentifier.h"
#include "mc/network/packet/PlayerAuthInputPacket.h"
#include "mc/platform/UUID.h"
// Removed: #include "mod/BenchMark/Timer.hpp" - No longer seems necessary for the packet counting logic
#include "mod/Module/Module.hpp"
#include "mod/SDK/event/packet/handle/handlePlayerAuthInputPacket.hpp"
#include "parallel_hashmap/phmap.h"
#include <chrono> // Added

namespace native_ac {

// Define the data structure for tracking packets per player


class TimerModule final : public Module {
private:
    struct PlayerPacketData {
        std::chrono::steady_clock::time_point windowStartTime;
        int                                   packetCount = 0;
    };
    phmap::flat_hash_map<mce::UUID, PlayerPacketData>                             mPlayerPacketCounters{};
    std::shared_ptr<ll::event::Listener<event::HandlePlayerAuthInputPacketEvent>> mPlayerAuthInputPacketListener{};
    int                                                                           mPacketLimit = 21;

public:
    static TimerModule* getInstance(); // Add static getInstance method
    TimerModule() : Module("Timer") {}
    bool load() override;
    bool enable() override;
    bool disable() override;
    void to_json(nlohmann::json& j) const override;
    void from_json(const nlohmann::json& j) override;
};
} // namespace native_ac