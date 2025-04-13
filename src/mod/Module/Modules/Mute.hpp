#pragma once
#include "ll/api/event/Listener.h"
#include "ll/api/event/command/ExecuteCommandEvent.h"
#include "ll/api/event/player/PlayerChatEvent.h"
#include "mod/Module/Module.hpp"
#include "parallel_hashmap/phmap.h"
#include <chrono>
#include <mutex>
#include <nlohmann/json_fwd.hpp> // Forward declaration for json
#include <string>

namespace native_ac {

class MuteModule : public Module {
public:
    struct MuteInfo {
        std::chrono::system_clock::time_point expiration_time;  // 禁言到期时间点
        int                                   duration_seconds; // 禁言持续秒数
        std::string                           reason;           // 禁言原因
    };

private:
    std::shared_ptr<ll::event::Listener<ll::event::ExecutingCommandEvent>> cmdListener;
    std::shared_ptr<ll::event::Listener<ll::event::PlayerChatEvent>>       chatListener;
    phmap::flat_hash_map<std::string, MuteInfo>                            MuteList;
    void                                                                   cleanMuteList();
    bool                                                                   cleanMuteListRunnning = false;
    mutable std::mutex muteListMutex; // Marked mutable to allow locking in const methods like to_json

public:
    bool isMuted(const std::string& name);
    MuteModule();
    bool load() override;
    bool enable() override;
    bool disable() override;

protected:
    void from_json(const nlohmann::json& j) override;
    void to_json(nlohmann::json& j) const override;
};
} // namespace native_ac