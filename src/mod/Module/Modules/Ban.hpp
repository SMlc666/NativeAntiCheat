#pragma once
#include "ll/api/event/Listener.h"
#include "ll/api/event/player/PlayerConnectEvent.h"
#include "mc/platform/UUID.h"
#include "mod/Module/Module.hpp"
#include "mod/SDK/event/packet/handle/handleLoginPacket.hpp"
#include "parallel_hashmap/phmap.h"
#include <chrono>
#include <optional>
#include <leveldb/status.h> // Include for LevelDB status

namespace native_ac {
class BanModule final : public Module {
public:
    // BanInfo struct remains the same, storing expiration and reason
    struct BanInfo {
        std::chrono::system_clock::time_point expiration_time;
        std::string                           reason;
        // Consider adding admin field if needed in the future
    };

private:
    // Use separate maps for IP and Name bans
    phmap::flat_hash_map<std::string, BanInfo>                          ipBanList;
    phmap::flat_hash_map<std::string, BanInfo>                          nameBanList;
    std::shared_ptr<ll::event::Listener<ll::event::PlayerConnectEvent>> mPlayerConnectListener{};
    std::string                                                         mBanMessage =
        "You are banned from this server. Reason: {reason} Expires: {expiration_time}"; // default ban message
    int   mListCommandPageSize = 10; // default page size for list command
    void* tickHandle           = nullptr;

public:
    static BanModule* getInstance();
    BanModule() : Module("Ban") {}
    // Functions specific to IP bans
    void addIpBan(
        const std::string&                           ip,
        const std::string&                           reason,
        const std::chrono::system_clock::time_point& expiration_time
    );
    bool removeIpBan(const std::string& ip);
    bool isIpBanned(const std::string& ip) const;
    std::optional<BanInfo> getIpBanInfo(const std::string& ip) const; // Return full info

    // Functions specific to Name bans
    void addNameBan(
        const std::string&                           name,
        const std::string&                           reason,
        const std::chrono::system_clock::time_point& expiration_time
    );
    bool removeNameBan(const std::string& name);
    bool isNameBanned(const std::string& name) const;
    std::optional<BanInfo> getNameBanInfo(const std::string& name) const; // Return full info

    // General module functions
    bool load() override;
    bool enable() override;
    bool                                                 disable() override;
    void                                                 from_json(const nlohmann::json& json) override;
    void                                                 to_json(nlohmann::json& json) const override;

    // --- Static LevelDB DAO Functions ---
    // These functions interact directly with the LevelDB database.
    // They do NOT modify the in-memory ban lists (ipBanList, nameBanList).

    // Add IP Ban to LevelDB
    static leveldb::Status AddIpBanToDB(const std::string& ip, const std::chrono::system_clock::time_point& expiration_time, const std::string& reason);

    // Add Name Ban to LevelDB
    static leveldb::Status AddNameBanToDB(const std::string& name, const std::chrono::system_clock::time_point& expiration_time, const std::string& reason);

    // Get IP Ban from LevelDB
    static std::optional<BanModule::BanInfo> GetIpBanFromDB(const std::string& ip);

    // Get Name Ban from LevelDB
    static std::optional<BanModule::BanInfo> GetNameBanFromDB(const std::string& name);

    // Remove IP Ban from LevelDB
    static leveldb::Status RemoveIpBanFromDB(const std::string& ip);

    // Remove Name Ban from LevelDB
    static leveldb::Status RemoveNameBanFromDB(const std::string& name);

    // Optional: Load all bans from DB (not implemented per requirements)
    // static std::vector<BanInfo> LoadAllBansFromDB();
}; // End of class BanModule

} // namespace native_ac