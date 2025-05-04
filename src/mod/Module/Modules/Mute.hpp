#pragma once
#include "ll/api/event/Listener.h"
#include "ll/api/event/command/ExecuteCommandEvent.h"
#include "ll/api/event/player/PlayerChatEvent.h"
#include "ll/api/event/world/LevelTickEvent.h"
#include "mod/Module/Module.hpp"
#include "parallel_hashmap/phmap.h"
#include <chrono>
#include <mutex>
#include <nlohmann/json_fwd.hpp> // Forward declaration for json
#include <string>
#include <optional>           // For optional return values
#include <leveldb/status.h>   // For LevelDB status

namespace native_ac {

class MuteModule final : public Module {
public:
    struct MuteInfo {
        std::chrono::system_clock::time_point expiration_time; // 禁言到期时间点
        std::string                           reason;          // 禁言原因
    };

private:
    std::shared_ptr<ll::event::Listener<ll::event::ExecutingCommandEvent>> cmdListener;
    std::shared_ptr<ll::event::Listener<ll::event::PlayerChatEvent>>       chatListener;
    std::shared_ptr<ll::event::Listener<ll::event::LevelTickEvent>>        tickListener;
    phmap::flat_hash_map<std::string, MuteInfo>                            MuteList;
    mutable std::mutex muteListMutex; // Marked mutable to allow locking in const methods like to_json
    int                ListCommandPageSize = 10;
    void*              tickHandle          = nullptr;

public:
    static MuteModule* getInstance(); // Add static getInstance method
    bool               isMuted(const std::string& name);
    MuteModule();
    bool load() override;
    bool enable() override;
    bool disable() override;

protected:
    void from_json(const nlohmann::json& j) override;
    void to_json(nlohmann::json& j) const override;

public: // Public static DAO functions
    // --- Static LevelDB DAO Functions ---
    // These functions interact directly with the LevelDB database.
    // They do NOT modify the in-memory mute list (MuteList).

    // Add Mute to LevelDB
    static leveldb::Status AddMuteToDB(const std::string& name, const std::chrono::system_clock::time_point& expiration_time, const std::string& reason);

    // Get Mute from LevelDB
    static std::optional<MuteModule::MuteInfo> GetMuteFromDB(const std::string& name);

    // Remove Mute from LevelDB
    static leveldb::Status RemoveMuteFromDB(const std::string& name);

    // Optional: Load all mutes from DB (not implemented per requirements)
    // static std::vector<MuteInfo> LoadAllMutesFromDB();
};
} // namespace native_ac