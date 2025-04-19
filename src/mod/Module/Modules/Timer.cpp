#include "Timer.hpp"

#include "ll/api/event/EventBus.h"
#include "ll/api/service/Bedrock.h"
#include "mc/network/ServerNetworkHandler.h"
#include "mc/server/ServerPlayer.h"
#include "mod/Module/ModuleManager.hpp"
#include "mod/NativeAntiCheat.h"
#include "mod/SDK/event/packet/handle/handlePlayerAuthInputPacket.hpp"
#include <chrono> // Ensure chrono is included

namespace native_ac {
TimerModule* TimerModule::getInstance() {
    static TimerModule instance;
    return &instance;
}

bool TimerModule::load() { return true; }
bool TimerModule::enable() {
    mPlayerAuthInputPacketListener =
        ll::event::EventBus::getInstance().emplaceListener<event::HandlePlayerAuthInputPacketEvent>(
            [this](event::HandlePlayerAuthInputPacketEvent& event) {
                ServerPlayer* player = ll::service::getServerNetworkHandler()->_getServerPlayer(
                    event.getNetworkIdentifier(),
                    event.getPacket()->mClientSubId
                );
                if (!player) {
                    return;
                }
                mce::UUID uuid = player->getUuid();
                auto      now  = std::chrono::steady_clock::now();
                auto      it   = mPlayerPacketCounters.find(uuid);
                if (it == mPlayerPacketCounters.end()) {
                    // First packet for this player (or after a reset)
                    mPlayerPacketCounters.emplace(uuid, PlayerPacketData{now, 1});
                } else {
                    PlayerPacketData& data = it->second;
                    auto diff = std::chrono::duration_cast<std::chrono::seconds>(now - data.windowStartTime);
                    if (diff.count() >= 1) {
                        data.windowStartTime = now;
                        data.packetCount     = 1;
                    } else {
                        data.packetCount++;
                        if (data.packetCount > mPacketLimit) {
                            NativeAntiCheat::getInstance().getSelf().getLogger().warn(
                                "Timer check failed for player UUID {}: {} packets in < 1 second.",
                                uuid.asString(), // Log UUID instead of name
                                data.packetCount
                            );
                        }
                    }
                }
            }
        );
    return true;
}
bool TimerModule::disable() {
    ll::event::EventBus::getInstance().removeListener(mPlayerAuthInputPacketListener);
    mPlayerPacketCounters.clear(); // Clear the map on disable
    return true;
}
void TimerModule::to_json(nlohmann::json& j) const { j["packet_limit"] = mPacketLimit; }
void TimerModule::from_json(const nlohmann::json& j) {
    if (j.contains("packet_limit")) {
        mPacketLimit = j.at("packet_limit").get<int>();
    }
}
} // namespace native_ac

REGISTER_MODULE(native_ac::TimerModule, native_ac::TimerModule::getInstance());