#pragma once
#include "ll/api/event/Listener.h"
#include "ll/api/event/player/PlayerConnectEvent.h"
#include "mc/platform/UUID.h"
#include "mod/Module/Module.hpp"
#include "mod/SDK/event/packet/handle/handleLoginPacket.hpp"
#include "parallel_hashmap/phmap.h"
#include <chrono>
#include <optional>
namespace native_ac {
class BanModule final : public Module {
public:
    enum class BanType {
        IP,
        NAME,
    };
    struct BanInfo {
        std::chrono::system_clock::time_point expiration_time;
        std::string                           reason;
    };

private:
    phmap::flat_hash_map<std::string, BanInfo>                          BanList;
    std::shared_ptr<ll::event::Listener<ll::event::PlayerConnectEvent>> mPlayerConnectListener{};
    std::string                                                         mBanMessage =
        "You are banned from this server. Reason: {reason} Expires: {expiration_time}"; // default ban message
    int   mListCommandPageSize = 10; // default page size for list command
    void* tickHandle           = nullptr;

public:
    static BanModule* getInstance();
    BanModule() : Module("Ban") {}
    void addBan(
        const std::string&                           realname,
        const std::string&                           reason,
        const std::chrono::system_clock::time_point& expiration_time
    );
    bool                                                 isBanned(const std::string& realname) const;
    std::optional<std::chrono::system_clock::time_point> getBanExpirationTime(const std::string& realname) const;
    bool                                                 load() override;
    bool                                                 enable() override;
    bool                                                 disable() override;
    void                                                 from_json(const nlohmann::json& json) override;
    void                                                 to_json(nlohmann::json& json) const override;
};
} // namespace native_ac