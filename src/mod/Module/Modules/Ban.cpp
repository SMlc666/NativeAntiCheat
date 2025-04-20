#include "Ban.hpp"
#include "TickScheduler.hpp"
#include "fmt/core.h"
#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/service/Bedrock.h"
#include "mc/network/ServerNetworkHandler.h"
#include "mc/server/commands/Command.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/CommandSelector.h"
#include "mod/BenchMark/BenchMark.hpp"
#include "mod/Module/ModuleManager.hpp"
#include "mod/Module/Modules/TickScheduler.hpp"
#include "mod/NativeAntiCheat.h"
#include <chrono>
#include <iomanip>           // Required for put_time and get_time
#include <nlohmann/json.hpp> // Required for JSON handling
#include <optional>
#include <sstream> // Required for string streams

namespace native_ac {
BanModule* BanModule::getInstance() {
    static BanModule instance;
    return &instance;
}
struct BanCommand {
    bool                    mIP;
    CommandSelector<Player> mPlayerSelector;
    int                     mBanSeconds;
    std::string             mReason;
};
struct RemoveBanCommand {
    std::string mRealNameOrIP;
};
struct ListBanCommand {
    int mPage = 0;
};
bool BanModule::load() { return true; }
bool BanModule::enable() {
    mPlayerConnectListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::PlayerConnectEvent>(
        [this](ll::event::PlayerConnectEvent& event) {
            auto banInfo = BanList.find(event.self().getRealName());
            if (banInfo == BanList.end()) {
                return;
            }
            ll::service::getServerNetworkHandler()->disconnectClient(
                event.networkIdentifier(),
                Connection::DisconnectFailReason::Kicked,
                fmt::v10::format(
                    fmt::runtime(mBanMessage),
                    fmt::arg("reason", banInfo->second.reason),
                    fmt::arg("expiration_time", banInfo->second.expiration_time)
                ),
                std::nullopt,
                false
            );
            event.cancel();
        }
    );
    tickHandle = TickSchedulerModule::getInstance()->emplaceTickCallback(
        [this]() {
            std::vector<std::string> namesToRemove;
            auto                     now = std::chrono::system_clock::now();
            {
                for (const auto& [name, info] : BanList) {
                    if (info.expiration_time <= now) {
                        namesToRemove.push_back(name);
                    }
                }
            }

            if (!namesToRemove.empty()) {
                for (const auto& name : namesToRemove) {
                    BanList.erase(name);
                }
            }
        },
        100
    );
    auto& bancmd = ll::command::CommandRegistrar::getInstance().getOrCreateCommand(
        "ban",
        "Ban a player from the server",
        CommandPermissionLevel::GameDirectors
    );
    bancmd.overload<RemoveBanCommand>()
        .required("mRealNameOrIP")
        .execute([this](CommandOrigin const&, CommandOutput& output, RemoveBanCommand const& param, Command const&) {
            auto&& banInfo = BanList.find(param.mRealNameOrIP);
            if (banInfo == BanList.end()) {
                output.error("Player not found or not banned.");
                return;
            }
            BanList.erase(banInfo);
            output.success("Player unbanned.");
        });
    bancmd.overload<ListBanCommand>().optional("mPage").execute(
        [this](CommandOrigin const&, CommandOutput& output, ListBanCommand const& param, Command const&) {
            if (BanList.empty()) {
                output.error("No active mutes found");
                return;
            }
            const size_t pageCount = (BanList.size() + mListCommandPageSize - 1) / mListCommandPageSize;
            if (param.mPage < 0 || static_cast<size_t>(param.mPage) >= pageCount) {
                output.error("Invalid page number (0-" + std::to_string(pageCount - 1) + ")");
                return;
            }
            // Calculate page range
            const auto startIdx = param.mPage * mListCommandPageSize;
            const auto endIdx   = std::min(startIdx + mListCommandPageSize, static_cast<int>(BanList.size()));

            // Get current time for remaining duration calculation
            const auto now = std::chrono::system_clock::now();

            // Header
            output.success("Ban List (Page " + std::to_string(param.mPage + 1) + "/" + std::to_string(pageCount) + ")");
// Iterate through the page items
#ifdef DEBUG
            {
                BenchMark bench([&output](Timer& timer) {
                    output.success("Output Ban using time: {}", timer.elapsed());
                });
#endif
                auto it = BanList.begin();
                std::advance(it, startIdx);
                for (int i = startIdx; i < endIdx; ++i, ++it) {
                    const auto& [name, info] = *it;
                    const auto remaining = std::chrono::duration_cast<std::chrono::seconds>(info.expiration_time - now);

                    std::string timeStr;
                    if (remaining.count() > 86400) {
                        timeStr = std::to_string(remaining.count() / 86400) + "d";
                    } else if (remaining.count() > 3600) {
                        timeStr = std::to_string(remaining.count() / 3600) + "h";
                    } else if (remaining.count() > 60) {
                        timeStr = std::to_string(remaining.count() / 60) + "m";
                    } else {
                        timeStr = std::to_string(remaining.count()) + "s";
                    }
                    output.success(
                        "{} - Remaining: {} - Reason: {}",
                        name,
                        timeStr,
                        info.reason.empty() ? "Not specified" : info.reason
                    );
                }

#ifdef DEBUG
            }
            output.success("Total Bans: {} ", BanList.size());
#endif
        }
    );
    bancmd.overload<BanCommand>()
        .text("add")
        .required("mIP") // 同时封禁名称和IP
        .required("mPlayerSelector")
        .required("mBanSeconds")
        .optional("mReason")
        .execute([this](CommandOrigin const& origin, CommandOutput& output, BanCommand const& param, Command const&) {
            if (param.mPlayerSelector.results(origin).empty()) {
                output.error("No players found matching selector.");
                return;
            }
            for (auto&& player : param.mPlayerSelector.results(origin)) {
                BanInfo banInfo{
                    std::chrono::system_clock::now() + std::chrono::seconds(param.mBanSeconds),
                    param.mReason
                };
                BanList[player->getRealName()] = banInfo;
                if (param.mIP) {
                    BanList[player->getNetworkIdentifier().getAddress()] = banInfo;
                }
                output.success(
                    "Banned {} for {} seconds for reason: {}",
                    player->getRealName(),
                    param.mBanSeconds,
                    param.mReason
                );
                ll::service::getServerNetworkHandler()->disconnectClient(
                    player->getNetworkIdentifier(),
                    Connection::DisconnectFailReason::Kicked,
                    fmt::v10::format(
                        fmt::runtime(mBanMessage),
                        fmt::arg("reason", param.mReason),
                        fmt::arg(
                            "expiration_time",
                            std::chrono::system_clock::now() + std::chrono::seconds(param.mBanSeconds)
                        )
                    ),
                    std::nullopt,
                    false
                );
            }
        });
    return true;
}
bool BanModule::disable() {
    ll::event::EventBus::getInstance().removeListener(mPlayerConnectListener);
    TickSchedulerModule::getInstance()->removeTickCallback(tickHandle);
    return true;
}
void BanModule::addBan(
    const std::string&                           realname,
    const std::string&                           reason,
    const std::chrono::system_clock::time_point& expiration_time
) {
    BanList.emplace(realname, BanInfo{expiration_time, reason});
}
bool BanModule::isBanned(const std::string& realname) const { return BanList.find(realname) != BanList.end(); }
std::optional<std::chrono::system_clock::time_point> BanModule::getBanExpirationTime(const std::string& realname
) const {
    auto&& banInfo = BanList.find(realname);
    if (banInfo == BanList.end()) {
        return std::nullopt;
    }
    return banInfo->second.expiration_time;
}
void BanModule::from_json(const nlohmann::json& json) {
    // Load BanMessage (existing logic)
    if (json.contains("BanMessage")) {
        if (json.at("BanMessage").is_string()) {
            mBanMessage = json["BanMessage"].get<std::string>();
        } else {
            NativeAntiCheat::getInstance().getSelf().getLogger().error("BanMessage must be a string");
        }
    } else {
        NativeAntiCheat::getInstance().getSelf().getLogger().warn("BanMessage field missing from config.");
    }
    if (json.contains("ListCommandPageSize")) {
        if (json.at("ListCommandPageSize").is_number_integer()) {
            mListCommandPageSize = json["ListCommandPageSize"].get<int>();
        } else {
            NativeAntiCheat::getInstance().getSelf().getLogger().error("ListCommandPageSize must be an integer");
        }
    } else {
        NativeAntiCheat::getInstance().getSelf().getLogger().warn("ListCommandPageSize field missing from config.");
    }
    // Load BanList
    if (json.contains("BanList") && json.at("BanList").is_object()) {
        BanList.clear(); // Clear existing bans before loading
        const auto& banListJson = json.at("BanList");
        for (auto it = banListJson.items().begin(); it != banListJson.items().end(); ++it) {
            const std::string& realname    = it.key();
            const auto&        banInfoJson = it.value();

            if (banInfoJson.is_object() && banInfoJson.contains("reason") && banInfoJson.at("reason").is_string()
                && banInfoJson.contains("expiration_time") && banInfoJson.at("expiration_time").is_string()) {
                try {
                    std::string reason  = banInfoJson.at("reason").get<std::string>();
                    std::string timeStr = banInfoJson.at("expiration_time").get<std::string>();

                    std::tm            tm = {};
                    std::istringstream iss(timeStr);
                    iss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
                    if (iss.fail()) {
                        NativeAntiCheat::getInstance().getSelf().getLogger().error(
                            "Failed to parse expiration time string for {}: {}",
                            realname,
                            timeStr
                        );
                        continue; // Skip this entry
                    }
                    std::chrono::system_clock::time_point expiration_time =
                        std::chrono::system_clock::from_time_t(std::mktime(&tm));

                    BanList.emplace(realname, BanInfo{expiration_time, reason});
                } catch (const nlohmann::json::exception& e) {
                    NativeAntiCheat::getInstance().getSelf().getLogger().error(
                        "JSON parsing error for ban entry {}: {}",
                        realname,
                        e.what()
                    );
                } catch (...) {
                    NativeAntiCheat::getInstance().getSelf().getLogger().error(
                        "Unknown error parsing ban entry for {}",
                        realname
                    );
                }

            } else {
                NativeAntiCheat::getInstance().getSelf().getLogger().error(
                    "Invalid BanList entry format for {}",
                    realname
                );
            }
        }
    } else if (json.contains("BanList")) {
        NativeAntiCheat::getInstance().getSelf().getLogger().error("BanList field exists but is not a JSON object.");
    }
}
void BanModule::to_json(nlohmann::json& json) const {
    // Save BanMessage (existing logic)
    json["BanMessage"]          = mBanMessage;
    json["ListCommandPageSize"] = mListCommandPageSize;
    // Save BanList
    nlohmann::json banListJson = nlohmann::json::object();
    for (const auto& [realname, banInfo] : BanList) {
        std::time_t t = std::chrono::system_clock::to_time_t(banInfo.expiration_time);
        std::tm     tm;
        localtime_s(&tm, &t); // Use localtime_s for thread-safe local time representation
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");

        nlohmann::json banInfoJson;
        banInfoJson["reason"]          = banInfo.reason;
        banInfoJson["expiration_time"] = oss.str();
        banListJson[realname]          = banInfoJson;
    }
    json["BanList"] = banListJson;
}
} // namespace native_ac
REGISTER_MODULE(native_ac::BanModule, native_ac::BanModule::getInstance());