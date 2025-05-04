#include "Ban.hpp"
#include "../LevelDBService.hpp" // Include LevelDB Service
#include "core/TickScheduler.hpp"
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
#include "mod/Module/Modules/core/TickScheduler.hpp"
#include "mod/NativeAntiCheat.h"
#include <algorithm> // For std::transform, std::reverse
#include <chrono>
#include <cstring>               // For memcpy
#include <iomanip>               // Required for put_time and get_time
#include <leveldb/write_batch.h> // Include for WriteBatch
#include <nlohmann/json.hpp>     // Required for JSON handling
#include <optional>
#include <spdlog/spdlog.h> // For logging errors
#include <sstream>         // Required for string streams


// Platform-specific includes for byte swapping
#ifdef _WIN32
#include <winsock2.h> // For byte swap functions like ntohll, htonll (might need linking ws2_32.lib)
// Provide portable implementations if standard functions aren't available
// Ensure these are defined only once, perhaps in a common header if needed elsewhere
#ifndef NATIVE_AC_BYTE_SWAP_DEFINED
#define NATIVE_AC_BYTE_SWAP_DEFINED
inline uint64_t htobe64(uint64_t host_64bits) { return htonll(host_64bits); }
inline uint64_t be64toh(uint64_t big_endian_64bits) { return ntohll(big_endian_64bits); }
#endif // NATIVE_AC_BYTE_SWAP_DEFINED
#else
#include <arpa/inet.h> // For htobe64, be64toh on POSIX
#endif


namespace native_ac {

// --- Anonymous Namespace for LevelDB Helpers ---
namespace {

// Key Prefixes (as defined in leveldb_storage_design.md)
const std::string kBanPrefix    = "b";
const std::string kMutePrefix   = "m"; // Context
const std::string kIpSubtype    = "i";
const std::string kNameSubtype  = "n";
const std::string kExpiryPrefix = "e";
const char        kSeparator    = ':';

// Normalize identifier (example: to lower case)
std::string NormalizeIdentifier(const std::string& id) {
    std::string lower_id = id;
    std::transform(lower_id.begin(), lower_id.end(), lower_id.begin(), [](unsigned char c) { return std::tolower(c); });
    return lower_id;
}

// Convert time_point to milliseconds timestamp (uint64_t)
uint64_t TimePointToMillis(const std::chrono::system_clock::time_point& tp) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
}

// Convert milliseconds timestamp (uint64_t) to time_point
std::chrono::system_clock::time_point MillisToTimePoint(uint64_t millis) {
    return std::chrono::system_clock::time_point(std::chrono::milliseconds(millis));
}

// Convert uint64_t timestamp to 8-byte big-endian binary string
std::string TimestampToBigEndianBytes(uint64_t timestamp) {
    uint64_t    timestamp_be = htobe64(timestamp);
    std::string bytes(sizeof(timestamp_be), '\0');
    std::memcpy(&bytes[0], &timestamp_be, sizeof(timestamp_be));
    return bytes;
}

// Convert 8-byte big-endian binary string back to uint64_t timestamp
uint64_t BigEndianBytesToTimestamp(const std::string& bytes) {
    if (bytes.length() != sizeof(uint64_t)) {
        BanModule::getInstance()->mLogger->error(
            "Invalid byte string length for timestamp conversion: {}",
            bytes.length()
        );
        return 0; // Or throw
    }
    uint64_t timestamp_be;
    std::memcpy(&timestamp_be, bytes.data(), sizeof(timestamp_be));
    return be64toh(timestamp_be);
}


// Create primary data key: <type_prefix>:<subtype_prefix>:<identifier>
std::string CreatePrimaryKey(const std::string& type, const std::string& subtype, const std::string& identifier) {
    return type + kSeparator + subtype + kSeparator + NormalizeIdentifier(identifier);
}

// Create expiration index key: e:<type_prefix>:<subtype_prefix>:<formatted_expiration_time>:<identifier>
std::string CreateExpiryKey(
    const std::string&                           type,
    const std::string&                           subtype,
    const std::string&                           identifier,
    const std::chrono::system_clock::time_point& expiration
) {
    uint64_t    millis     = TimePointToMillis(expiration);
    std::string time_bytes = TimestampToBigEndianBytes(millis);
    return kExpiryPrefix + kSeparator + type + kSeparator + subtype + kSeparator + time_bytes + kSeparator
         + NormalizeIdentifier(identifier);
}

// Internal function to get BanInfo from DB (used by Remove and public Getters)
// Returns optional containing BanInfo on success, nullopt if not found or error.
std::optional<BanModule::BanInfo> GetBanInfoFromDBInternal(const std::string& identifier, const std::string& subtype) {
    auto& dbService = LevelDBService::GetInstance();
    if (!dbService.IsInitialized()) {
        BanModule::getInstance()->mLogger->error("LevelDBService not initialized during internal Get.");
        return std::nullopt;
    }

    std::string     primaryKey = CreatePrimaryKey(kBanPrefix, subtype, identifier);
    std::string     valueStr;
    leveldb::Status status = dbService.Get(primaryKey, &valueStr);

    if (status.IsNotFound()) {
        return std::nullopt; // Not banned
    }
    if (!status.ok()) {
        BanModule::getInstance()->mLogger->error(
            "Failed to get {} ban for '{}' from DB: {}",
            (subtype == kIpSubtype ? "IP" : "Name"),
            identifier,
            status.ToString()
        );
        return std::nullopt; // Error occurred
    }

    try {
        nlohmann::json     valueJson = nlohmann::json::parse(valueStr);
        BanModule::BanInfo info;
        // Use .at() for required fields, .value() for optional
        info.expiration_time = MillisToTimePoint(valueJson.at("e").get<uint64_t>());
        info.reason          = valueJson.at("r").get<std::string>();
        // Example for an optional field:
        // info.admin = valueJson.value("admin", "Unknown");

        // Note: We don't check for expiration here in the raw Get function.
        // The caller (or a cleanup process) should handle expiration logic if needed.
        return info;
    } catch (const nlohmann::json::parse_error& e) {
        BanModule::getInstance()->mLogger->error(
            "Failed to parse JSON for {} ban '{}' from DB: {}. Value: {}",
            (subtype == kIpSubtype ? "IP" : "Name"),
            identifier,
            e.what(),
            valueStr
        );
        return std::nullopt;
    } catch (const nlohmann::json::type_error& e) {
        BanModule::getInstance()->mLogger->error(
            "JSON type error for {} ban '{}' from DB: {}. Value: {}",
            (subtype == kIpSubtype ? "IP" : "Name"),
            identifier,
            e.what(),
            valueStr
        );
        return std::nullopt;
    } catch (const std::exception& e) { // Catch other potential exceptions like std::out_of_range from .at()
        BanModule::getInstance()->mLogger->error(
            "Unexpected error processing JSON for {} ban '{}' from DB: {}",
            (subtype == kIpSubtype ? "IP" : "Name"),
            identifier,
            e.what()
        );
        return std::nullopt;
    }
}


} // anonymous namespace
// --- End of LevelDB Helpers ---


// --- BanModule Implementation ---
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
    // Since LevelDBService doesn't provide iterators, we cannot load all bans at startup.
    // We will rely on querying LevelDB directly when checking bans.
    // Clear in-memory lists on enable to ensure they don't contain stale data from previous runs.
    ipBanList.clear();
    nameBanList.clear();
    BanModule::getInstance()->mLogger->info("Initialized. Ban checks will query LevelDB directly.");
    mPlayerConnectListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::PlayerConnectEvent>(
        [this](ll::event::PlayerConnectEvent& event) {
            const auto& player     = event.self();
            const auto& playerName = player.getRealName();
            // 确保 getAddress() 返回的类型与 BanList 的键类型匹配
            const auto& playerIp = player.getNetworkIdentifier().getAddress();

            // Check for name ban first
            auto nameBanInfo = getNameBanInfo(playerName);
            if (nameBanInfo.has_value()) {
                // Found an active name ban
                ll::service::getServerNetworkHandler()->disconnectClient(
                    event.networkIdentifier(),
                    Connection::DisconnectFailReason::Kicked,
                    fmt::v10::format(
                        fmt::runtime(mBanMessage),
                        fmt::arg("reason", nameBanInfo->reason),
                        fmt::arg("expiration_time", nameBanInfo->expiration_time) // Consider formatting time
                    ),
                    std::nullopt,
                    false
                );
                event.cancel();
                return; // Player is name-banned, no need to check IP
            }

            // If no active name ban, check for IP ban
            auto ipBanInfo = getIpBanInfo(playerIp);
            if (ipBanInfo.has_value()) {
                // Found an active IP ban
                ll::service::getServerNetworkHandler()->disconnectClient(
                    event.networkIdentifier(),
                    Connection::DisconnectFailReason::Kicked,
                    fmt::v10::format(
                        fmt::runtime(mBanMessage),
                        fmt::arg("reason", ipBanInfo->reason),
                        fmt::arg("expiration_time", ipBanInfo->expiration_time) // Consider formatting time
                    ),
                    std::nullopt,
                    false
                );
                event.cancel();
                return; // Player is IP-banned
            }
            // Player is not banned by name or IP
        }
    );
    tickHandle = TickSchedulerModule::getInstance()->emplaceTickCallback(
        [this]() {
            auto                     now = std::chrono::system_clock::now();
            std::vector<std::string> expiredNameKeys;
            std::vector<std::string> expiredIpKeys;

            // Identify expired keys from in-memory maps
            for (const auto& [name, info] : nameBanList) {
                if (info.expiration_time <= now) {
                    expiredNameKeys.push_back(name);
                }
            }
            for (const auto& [ip, info] : ipBanList) {
                if (info.expiration_time <= now) {
                    expiredIpKeys.push_back(ip);
                }
            }

            // Remove expired entries from both LevelDB and in-memory maps
            for (const auto& name : expiredNameKeys) {
                // Remove from DB first
                leveldb::Status status = RemoveNameBanFromDB(name);
                if (!status.ok() && !status.IsNotFound()) {
                    BanModule::getInstance()->mLogger->warn(
                        "Failed to remove expired name ban for {} from LevelDB: {}",
                        name,
                        status.ToString()
                    );
                    // Continue to remove from memory even if DB fails? Yes, to avoid repeated attempts.
                }
                // Remove from memory
                nameBanList.erase(name);
                // BanModule::getInstance()->mLogger->debug("Removed expired name ban for {}", name); // Optional
                // logging
            }

            for (const auto& ip : expiredIpKeys) {
                // Remove from DB first
                leveldb::Status status = RemoveIpBanFromDB(ip);
                if (!status.ok() && !status.IsNotFound()) {
                    BanModule::getInstance()->mLogger->warn(
                        "[BanModule Tick] Failed to remove expired IP ban for {} from LevelDB: {}",
                        ip,
                        status.ToString()
                    );
                }
                // Remove from memory
                ipBanList.erase(ip);
                // BanModule::getInstance()->mLogger->debug("Removed expired IP ban for {}", ip); // Optional logging
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
        .text("remove")
        .required("mRealNameOrIP")
        .execute([this](CommandOrigin const&, CommandOutput& output, RemoveBanCommand const& param, Command const&) {
            bool removedName = removeNameBan(param.mRealNameOrIP);
            bool removedIp   = removeIpBan(param.mRealNameOrIP);
            if (removedName || removedIp) {
                output.success(fmt::format(
                    "Removed ban for '{}'. Name removed: {}, IP removed: {}",
                    param.mRealNameOrIP,
                    removedName,
                    removedIp
                ));
            } else {
                output.error(fmt::format("No active ban found for '{}'.", param.mRealNameOrIP));
            }
        });
    // Helper lambda to format and output ban list entries
    auto outputBanList = [&](CommandOutput& output, const auto& banMap, const std::string& type, int page) {
        if (banMap.empty()) {
            // Don't output error if the list is just empty, only if page is invalid for non-empty list
            // output.success(fmt::format("No active {} bans found.", type));
            return false; // Indicate list was empty or processed (in this case, empty)
        }

        const size_t totalSize = banMap.size();
        const size_t pageCount = (totalSize + mListCommandPageSize - 1) / mListCommandPageSize;

        if (page < 0 || static_cast<size_t>(page) >= pageCount) {
            output.error(
                "Invalid page number for {} bans (0-{}). Total items: {}",
                type,
                pageCount > 0 ? pageCount - 1 : 0,
                totalSize
            );
            return true; // Indicate list was not empty but page invalid
        }

        // Calculate page range
        const auto startIdx = static_cast<size_t>(page) * mListCommandPageSize;
        const auto endIdx   = std::min(startIdx + mListCommandPageSize, totalSize);

        const auto now = std::chrono::system_clock::now();

        // Header
        output.success("--- {} Ban List (Page {}/{}, Total: {}) ---", type, page + 1, pageCount, totalSize);

        auto it = banMap.begin();
        std::advance(it, startIdx);
        int displayedCount = 0;
        for (size_t i = startIdx; i < endIdx; ++i, ++it) {
            const auto& [key, info] = *it; // key is name or IP
            const auto remaining    = std::chrono::duration_cast<std::chrono::seconds>(info.expiration_time - now);

            // Skip expired bans that might not have been cleaned up yet by the tick handler
            if (remaining.count() <= 0) continue;
            displayedCount++;

            std::string timeStr;
            if (remaining.count() > 86400 * 365 * 10) { // ~10 years, treat as permanent
                timeStr = "Permanent";
            } else if (remaining.count() > 86400) {
                timeStr = fmt::format("{:.1f}d", static_cast<double>(remaining.count()) / 86400.0);
            } else if (remaining.count() > 3600) {
                timeStr = fmt::format("{:.1f}h", static_cast<double>(remaining.count()) / 3600.0);
            } else if (remaining.count() > 60) {
                timeStr = fmt::format("{:.1f}m", static_cast<double>(remaining.count()) / 60.0);
            } else {
                timeStr = std::to_string(remaining.count()) + "s";
            }
            output.success(
                "{} - Remaining: {} - Reason: {}",
                key,
                timeStr,
                info.reason.empty() ? "Not specified" : info.reason
            );
        }
        if (displayedCount == 0 && totalSize > 0) {
            output.success("(No active {} bans on this page, likely expired)", type);
        }
        // output.success(fmt::format("Total Active {} Bans: {}", type, totalSize)); // Moved total to header
        return true; // Indicate list was processed
    };

    bancmd.overload<ListBanCommand>().text("list").optional("mPage").execute(
        [this,
         outputBanList](CommandOrigin const&, CommandOutput& output, ListBanCommand const& param, Command const&) {
            if (nameBanList.empty() && ipBanList.empty()) {
                output.error("No active bans found (neither name nor IP).");
                return;
            }
            if (!nameBanList.empty()) {
                outputBanList(output, nameBanList, "Name", param.mPage);
            }
            if (!ipBanList.empty()) {
                outputBanList(output, ipBanList, "IP", param.mPage);
            }
        }
    );
    bancmd.overload<BanCommand>()
        .text("add")
        .required("mIP") // 同时封禁名称和IP
        .required("mPlayerSelector")
        .required("mBanSeconds")
        .optional("mReason")
        .execute([this](CommandOrigin const& origin, CommandOutput& output, BanCommand const& param, Command const&) {
            auto results = param.mPlayerSelector.results(origin);
            if (results.empty()) {
                output.error("No players found matching selector.");
                return;
            }
            // Calculate expiration time once
            auto expiration_time = (param.mBanSeconds <= 0) // Treat 0 or negative as permanent (or very long time)
                                     ? std::chrono::system_clock::now() + std::chrono::years(100)
                                     : std::chrono::system_clock::now() + std::chrono::seconds(param.mBanSeconds);
            std::string reason   = param.mReason.empty() ? "Not specified" : param.mReason;


            for (auto* player : results) { // Use pointer directly
                if (!player) continue;     // Safety check

                // Always ban by name
                addNameBan(player->getRealName(), reason, expiration_time);
                output.success(
                    "Name-Banned {} for {} seconds. Reason: {}",
                    player->getRealName(),
                    param.mBanSeconds <= 0 ? "Permanent"
                                           : std::to_string(param.mBanSeconds), // Display "Permanent" for 0 seconds
                    reason
                );

                // Optionally ban by IP
                if (param.mIP) {
                    std::string ipAddress = player->getNetworkIdentifier().getAddress();
                    addIpBan(ipAddress, reason, expiration_time);
                    output.success(
                        "IP-Banned {} ({}) for {} seconds. Reason: {}",
                        player->getRealName(),
                        ipAddress,
                        param.mBanSeconds <= 0 ? "Permanent" : std::to_string(param.mBanSeconds),
                        reason
                    );
                }

                // Disconnect the player
                ll::service::getServerNetworkHandler()->disconnectClient(
                    player->getNetworkIdentifier(),
                    Connection::DisconnectFailReason::Kicked,
                    fmt::v10::format(
                        fmt::runtime(mBanMessage),
                        fmt::arg("reason", reason),
                        fmt::arg("expiration_time", expiration_time) // Use calculated time, consider formatting
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
// --- Implementation of new Ban functions ---

// IP Ban Functions
void BanModule::addIpBan(
    const std::string&                           ip,
    const std::string&                           reason,
    const std::chrono::system_clock::time_point& expiration_time
) {
    ipBanList.emplace(ip, BanInfo{expiration_time, reason});
    // Add to LevelDB
    leveldb::Status status = AddIpBanToDB(ip, expiration_time, reason);
    if (!status.ok()) {
        BanModule::getInstance()->mLogger->error("Failed to add IP ban for {} to LevelDB: {}", ip, status.ToString());
        // Consider logging or handling the error more robustly
    }
}

bool BanModule::removeIpBan(const std::string& ip) {
    // Remove from LevelDB first
    leveldb::Status status = RemoveIpBanFromDB(ip);
    if (!status.ok() && !status.IsNotFound()) { // Log error unless it was simply not found
        BanModule::getInstance()
            ->mLogger->error("Failed to remove IP ban for {} from LevelDB: {}", ip, status.ToString());
        // Decide if we should prevent in-memory removal? For now, just log and proceed.
    }
    // Remove from memory and return status based on memory removal
    return ipBanList.erase(ip) > 0;
}

bool BanModule::isIpBanned(const std::string& ip) const {
    // Query LevelDB directly
    auto banInfoOpt = GetIpBanFromDB(ip);
    if (banInfoOpt.has_value()) {
        // Check if the ban found in DB is expired
        return banInfoOpt->expiration_time > std::chrono::system_clock::now();
    }
    return false; // Not found in DB
}

std::optional<BanModule::BanInfo> BanModule::getIpBanInfo(const std::string& ip) const {
    // Query LevelDB directly
    auto banInfoOpt = GetIpBanFromDB(ip);
    if (banInfoOpt.has_value()) {
        // Check if the ban found in DB is expired
        if (banInfoOpt->expiration_time > std::chrono::system_clock::now()) {
            return banInfoOpt; // Return the info if not expired
        }
    }
    return std::nullopt; // Not found or expired
}


// Name Ban Functions
void BanModule::addNameBan(
    const std::string&                           name,
    const std::string&                           reason,
    const std::chrono::system_clock::time_point& expiration_time
) {
    // Update memory cache (still useful for /ban list and potentially quick checks if needed later)
    nameBanList.emplace(name, BanInfo{expiration_time, reason});
    // Add to LevelDB
    leveldb::Status status = AddNameBanToDB(name, expiration_time, reason);
    if (!status.ok()) {
        BanModule::getInstance()
            ->mLogger->error("Failed to add Name ban for {} to LevelDB: {}", name, status.ToString());
        // Consider logging or handling the error more robustly
    }
}

bool BanModule::removeNameBan(const std::string& name) {
    // Remove from LevelDB first
    leveldb::Status status = RemoveNameBanFromDB(name);
    if (!status.ok() && !status.IsNotFound()) {
        BanModule::getInstance()
            ->mLogger->error("Failed to remove Name ban for {} from LevelDB: {}", name, status.ToString());
        // Decide if we should prevent in-memory removal? For now, just log and proceed.
    }
    // Remove from memory and return status based on memory removal
    return nameBanList.erase(name) > 0;
}

bool BanModule::isNameBanned(const std::string& name) const {
    // Query LevelDB directly
    auto banInfoOpt = GetNameBanFromDB(name);
    if (banInfoOpt.has_value()) {
        // Check if the ban found in DB is expired
        return banInfoOpt->expiration_time > std::chrono::system_clock::now();
    }
    return false; // Not found in DB
}

std::optional<BanModule::BanInfo> BanModule::getNameBanInfo(const std::string& name) const {
    // Query LevelDB directly
    auto banInfoOpt = GetNameBanFromDB(name);
    if (banInfoOpt.has_value()) {
        // Check if the ban found in DB is expired
        if (banInfoOpt->expiration_time > std::chrono::system_clock::now()) {
            return banInfoOpt; // Return the info if not expired
        }
    }
    return std::nullopt; // Not found or expired
}

// --- End of new Ban function implementations ---


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
    // Ban lists are now loaded from LevelDB in enable(), no longer from JSON.
    // Clear lists here to ensure a clean state if from_json is called after enable (e.g., config reload)
    ipBanList.clear();
    nameBanList.clear();
    // Note: A config reload command would need to trigger the LevelDB loading logic again.
    // For now, we assume loading happens only once at enable().
} // End of from_json function

void BanModule::to_json(nlohmann::json& json) const {
    // Save BanMessage (existing logic)
    json["BanMessage"]          = mBanMessage;
    json["ListCommandPageSize"] = mListCommandPageSize;

    // Ban lists are now saved to LevelDB, no longer to JSON.
}


// --- Static LevelDB DAO Function Implementations ---

leveldb::Status BanModule::AddIpBanToDB(
    const std::string&                           ip,
    const std::chrono::system_clock::time_point& expiration_time,
    const std::string&                           reason
) {
    auto& dbService = LevelDBService::GetInstance();
    if (!dbService.IsInitialized()) {
        BanModule::getInstance()->mLogger->error("LevelDBService not initialized for AddIpBanToDB.");
        return leveldb::Status::IOError("LevelDBService not initialized");
    }

    std::string primaryKey = CreatePrimaryKey(kBanPrefix, kIpSubtype, ip);
    std::string expiryKey  = CreateExpiryKey(kBanPrefix, kIpSubtype, ip, expiration_time);

    nlohmann::json valueJson;
    valueJson["e"]       = TimePointToMillis(expiration_time);
    valueJson["r"]       = reason;
    std::string valueStr = valueJson.dump();

    leveldb::WriteBatch batch;
    batch.Put(primaryKey, valueStr);
    batch.Put(expiryKey, ""); // Empty value for expiry key

    leveldb::WriteOptions writeOptions;
    // writeOptions.sync = true; // Optional: for higher durability

    leveldb::Status status = dbService.Write(writeOptions, &batch);
    if (!status.ok()) {
        BanModule::getInstance()->mLogger->error("Failed to add IP ban for '{}' to DB: {}", ip, status.ToString());
    } else {
        BanModule::getInstance()->mLogger->debug("Added IP ban for '{}' to DB.", ip); // Debug log on success
    }
    return status;
}

leveldb::Status BanModule::AddNameBanToDB(
    const std::string&                           name,
    const std::chrono::system_clock::time_point& expiration_time,
    const std::string&                           reason
) {
    auto& dbService = LevelDBService::GetInstance();
    if (!dbService.IsInitialized()) {
        BanModule::getInstance()->mLogger->error("LevelDBService not initialized for AddNameBanToDB.");
        return leveldb::Status::IOError("LevelDBService not initialized");
    }

    std::string primaryKey = CreatePrimaryKey(kBanPrefix, kNameSubtype, name);
    std::string expiryKey  = CreateExpiryKey(kBanPrefix, kNameSubtype, name, expiration_time);

    nlohmann::json valueJson;
    valueJson["e"]       = TimePointToMillis(expiration_time);
    valueJson["r"]       = reason;
    std::string valueStr = valueJson.dump();

    leveldb::WriteBatch batch;
    batch.Put(primaryKey, valueStr);
    batch.Put(expiryKey, "");

    leveldb::WriteOptions writeOptions;
    // writeOptions.sync = true;

    leveldb::Status status = dbService.Write(writeOptions, &batch);
    if (!status.ok()) {
        BanModule::getInstance()->mLogger->error("Failed to add Name ban for '{}' to DB: {}", name, status.ToString());
    } else {
        BanModule::getInstance()->mLogger->debug("Added Name ban for '{}' to DB.", name); // Debug log on success
    }
    return status;
}

std::optional<BanModule::BanInfo> BanModule::GetIpBanFromDB(const std::string& ip) {
    // Use the internal helper which already handles DB check and JSON parsing
    return GetBanInfoFromDBInternal(ip, kIpSubtype);
}

std::optional<BanModule::BanInfo> BanModule::GetNameBanFromDB(const std::string& name) {
    // Use the internal helper
    return GetBanInfoFromDBInternal(name, kNameSubtype);
}

leveldb::Status BanModule::RemoveIpBanFromDB(const std::string& ip) {
    auto& dbService = LevelDBService::GetInstance();
    if (!dbService.IsInitialized()) {
        BanModule::getInstance()->mLogger->error("LevelDBService not initialized for RemoveIpBanFromDB.");
        return leveldb::Status::IOError("LevelDBService not initialized");
    }

    // 1. Get ban info to find the expiration time for the expiry key
    std::optional<BanInfo> banInfoOpt = GetBanInfoFromDBInternal(ip, kIpSubtype);

    // If not found, nothing to remove (or error already logged by GetBanInfoFromDBInternal)
    if (!banInfoOpt.has_value()) {
        // Check if it really doesn't exist or if Get failed previously
        std::string     primaryKeyCheck = CreatePrimaryKey(kBanPrefix, kIpSubtype, ip);
        std::string     valueStrCheck;
        leveldb::Status getStatus = dbService.Get(primaryKeyCheck, &valueStrCheck); // Check existence directly
        if (getStatus.IsNotFound()) {
            BanModule::getInstance()->mLogger->debug("IP ban for '{}' not found in DB, removal skipped.", ip);
            return leveldb::Status::OK(); // Already gone is considered success for removal
        }
        // If Get failed for other reasons, GetBanInfoFromDBInternal should have logged it.
        // Return the original getStatus if it wasn't NotFound (error occurred during Get)
        BanModule::getInstance()->mLogger->warn(
            "IP ban info for '{}' disappeared or Get failed before removal from DB. Status: {}",
            ip,
            getStatus.ToString()
        );
        return getStatus.ok() ? leveldb::Status::NotFound("IP Ban info disappeared before removal from DB") : getStatus;
    }

    // 2. Construct keys using the retrieved expiration time
    std::string primaryKey = CreatePrimaryKey(kBanPrefix, kIpSubtype, ip);
    std::string expiryKey  = CreateExpiryKey(kBanPrefix, kIpSubtype, ip, banInfoOpt.value().expiration_time);

    // 3. Prepare batch delete
    leveldb::WriteBatch batch;
    batch.Delete(primaryKey);
    batch.Delete(expiryKey);

    leveldb::WriteOptions writeOptions;
    // writeOptions.sync = true;

    leveldb::Status status = dbService.Write(writeOptions, &batch);
    if (!status.ok()) {
        BanModule::getInstance()->mLogger->error("Failed to remove IP ban for '{}' from DB: {}", ip, status.ToString());
    } else {
        BanModule::getInstance()->mLogger->debug("Removed IP ban for '{}' from DB.", ip); // Debug log on success
    }
    return status;
}

leveldb::Status BanModule::RemoveNameBanFromDB(const std::string& name) {
    auto& dbService = LevelDBService::GetInstance();
    if (!dbService.IsInitialized()) {
        BanModule::getInstance()->mLogger->error("LevelDBService not initialized for RemoveNameBanFromDB.");
        return leveldb::Status::IOError("LevelDBService not initialized");
    }

    // 1. Get ban info
    std::optional<BanInfo> banInfoOpt = GetBanInfoFromDBInternal(name, kNameSubtype);

    if (!banInfoOpt.has_value()) {
        std::string     primaryKeyCheck = CreatePrimaryKey(kBanPrefix, kNameSubtype, name);
        std::string     valueStrCheck;
        leveldb::Status getStatus = dbService.Get(primaryKeyCheck, &valueStrCheck);
        if (getStatus.IsNotFound()) {
            BanModule::getInstance()->mLogger->debug("Name ban for '{}' not found in DB, removal skipped.", name);
            return leveldb::Status::OK(); // Already gone
        }
        BanModule::getInstance()->mLogger->warn(
            "Name ban info for '{}' disappeared or Get failed before removal from DB. Status: {}",
            name,
            getStatus.ToString()
        );
        return getStatus.ok() ? leveldb::Status::NotFound("Name Ban info disappeared before removal from DB")
                              : getStatus;
    }

    // 2. Construct keys
    std::string primaryKey = CreatePrimaryKey(kBanPrefix, kNameSubtype, name);
    std::string expiryKey  = CreateExpiryKey(kBanPrefix, kNameSubtype, name, banInfoOpt.value().expiration_time);

    // 3. Prepare batch delete
    leveldb::WriteBatch batch;
    batch.Delete(primaryKey);
    batch.Delete(expiryKey);

    leveldb::WriteOptions writeOptions;
    // writeOptions.sync = true;

    leveldb::Status status = dbService.Write(writeOptions, &batch);
    if (!status.ok()) {
        BanModule::getInstance()
            ->mLogger->error("Failed to remove Name ban for '{}' from DB: {}", name, status.ToString());
    } else {
        BanModule::getInstance()->mLogger->debug("Removed Name ban for '{}' from DB.", name); // Debug log on success
    }
    return status;
}

// --- End of Static LevelDB DAO Function Implementations ---


} // namespace native_ac
REGISTER_MODULE(native_ac::BanModule, native_ac::BanModule::getInstance());