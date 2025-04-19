#include "PacketVerify.hpp"
#include "ll/api/base/StdInt.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/service/Bedrock.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/network/ServerNetworkHandler.h"
#include "mc/platform/UUID.h"
#include "mc/server/ServerPlayer.h"
#include "mod/Module/ModuleManager.hpp"
#include "mod/NativeAntiCheat.h"
#include "mod/SDK/event/packet/handle/handlePlayerAuthInputPacket.hpp"

namespace native_ac {
bool PacketVerifyModule::load() { return true; }
bool PacketVerifyModule::enable() {
    if (VerifyPlayerAuthInputPacket && mPlayerAuthInputPacketListener == nullptr) {
        mPlayerAuthInputPacketListener =
            ll::event::EventBus::getInstance().emplaceListener<event::HandlePlayerAuthInputPacketEvent>(
                [this](event::HandlePlayerAuthInputPacketEvent& event) {
                    auto* packet = event.getPacket();
                    auto* player = ll::service::getServerNetworkHandler()->_getServerPlayer(
                        event.getNetworkIdentifier(),
                        packet->mClientSubId
                    );
                    if (player == nullptr) {
                        return;
                    }
                    if (VerifyPlayerAuthInputPacketClientTick) {
                        mce::UUID uuid = player->getUuid();
                        if (mPlayerLastInputTick.find(uuid) == mPlayerLastInputTick.end()) {
                            mPlayerLastInputTick[uuid] = packet->mClientTick;
                        } else {
                            auto& lastInputTick = mPlayerLastInputTick[uuid];
                            if (packet->mClientTick->mValue - lastInputTick.mValue
                                > static_cast<uint64>(MaxPlayerInputTickDiff)) {}
                        }
                    }
                }
            );
    }
    return true;
}
bool PacketVerifyModule::disable() {
    if (mPlayerAuthInputPacketListener) {
        ll::event::EventBus::getInstance().removeListener(mPlayerAuthInputPacketListener);
        mPlayerAuthInputPacketListener = nullptr;
    }
    return true;
}
PacketVerifyModule* PacketVerifyModule::getInstance() {
    static PacketVerifyModule instance;
    return &instance;
}

} // namespace native_ac
REGISTER_MODULE(native_ac::PacketVerifyModule, native_ac::PacketVerifyModule::getInstance());