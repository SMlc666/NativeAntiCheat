#include "Mute.hpp"
#include "../LevelDBService.hpp" // Include LevelDB Service
#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/command/ExecuteCommandEvent.h"
#include "mc/network/ServerNetworkHandler.h"
#include "mc/network/packet/Packet.h"
#include "mc/server/commands/Command.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/CommandSelector.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"
#include "mod/BenchMark/BenchMark.hpp"
#include "mod/Module/Module.hpp"
#include "mod/Module/ModuleManager.hpp" // Needed for REGISTER_MODULE
#include "mod/Module/Modules/core/TickScheduler.hpp"
#include "mod/NativeAntiCheat.h"
#include <algorithm>             // For std::transform
#include <cstring>               // For memcpy
#include <leveldb/write_batch.h> // Include for WriteBatch
#include <ll/api/service/Bedrock.h>
#include <spdlog/spdlog.h> // For logging errors

// Platform-specific includes for byte swapping
#ifdef _WIN32
#include <winsock2.h> // Provides htonll, ntohll (ensure linking ws2_32.lib)
// Define htobe64/be64toh based on Windows functions if not already defined elsewhere
#ifndef htobe64
#define htobe64(x) htonll(x)
#endif
#ifndef be64toh
#define be64toh(x) ntohll(x)
#endif
#else
#include <arpa/inet.h> // Provides htobe64, be64toh on POSIX systems
#endif


namespace native_ac {

// --- Anonymous Namespace for Mute LevelDB Helpers ---
namespace {

// Key Prefixes
const std::string kMutePrefix   = "m";
const std::string kNameSubtype  = "n"; // Mute only uses name subtype
const std::string kExpiryPrefix = "e";
const char        kSeparator    = ':';

// Normalize identifier (to lower case)
std::string NormalizeMuteIdentifier(const std::string& id) {
    std::string lower_id = id;
    std::transform(lower_id.begin(), lower_id.end(), lower_id.begin(), [](unsigned char c) { return std::tolower(c); });
    return lower_id;
}

// Convert time_point to milliseconds timestamp (uint64_t)
uint64_t MuteTimePointToMillis(const std::chrono::system_clock::time_point& tp) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
}

// Convert milliseconds timestamp (uint64_t) to time_point
std::chrono::system_clock::time_point MuteMillisToTimePoint(uint64_t millis) {
    return std::chrono::system_clock::time_point(std::chrono::milliseconds(millis));
}

// Convert uint64_t timestamp to 8-byte big-endian binary string
std::string MuteTimestampToBigEndianBytes(uint64_t timestamp) {
    uint64_t    timestamp_be = htobe64(timestamp);
    std::string bytes(sizeof(timestamp_be), '\0');
    std::memcpy(&bytes[0], &timestamp_be, sizeof(timestamp_be));
    return bytes;
}

// Convert 8-byte big-endian binary string back to uint64_t timestamp
uint64_t MuteBigEndianBytesToTimestamp(const std::string& bytes) {
    if (bytes.length() != sizeof(uint64_t)) {
        MuteModule::getInstance()->mLogger->error(
            "Invalid byte string length for timestamp conversion: {}",
            bytes.length()
        );
        return 0;
    }
    uint64_t timestamp_be;
    std::memcpy(&timestamp_be, bytes.data(), sizeof(timestamp_be));
    return be64toh(timestamp_be);
}

// Create primary data key: m:n:<name>
std::string CreateMutePrimaryKey(const std::string& name) {
    return kMutePrefix + kSeparator + kNameSubtype + kSeparator + NormalizeMuteIdentifier(name);
}

// Create expiration index key: e:m:n:<formatted_expiration_time>:<name>
std::string CreateMuteExpiryKey(const std::string& name, const std::chrono::system_clock::time_point& expiration) {
    uint64_t    millis     = MuteTimePointToMillis(expiration);
    std::string time_bytes = MuteTimestampToBigEndianBytes(millis);
    return kExpiryPrefix + kSeparator + kMutePrefix + kSeparator + kNameSubtype + kSeparator + time_bytes + kSeparator
         + NormalizeMuteIdentifier(name);
}

// Internal function to get MuteInfo from DB
std::optional<MuteModule::MuteInfo> GetMuteInfoFromDBInternal(const std::string& name) {
    auto& dbService = LevelDBService::GetInstance();
    if (!dbService.IsInitialized()) {
        MuteModule::getInstance()->mLogger->error("LevelDBService not initialized during internal Get.");
        return std::nullopt;
    }

    std::string     primaryKey = CreateMutePrimaryKey(name);
    std::string     valueStr;
    leveldb::Status status = dbService.Get(primaryKey, &valueStr);

    if (status.IsNotFound()) {
        return std::nullopt; // Not muted
    }
    if (!status.ok()) {
        MuteModule::getInstance()->mLogger->error("Failed to get Mute for '{}' from DB: {}", name, status.ToString());
        return std::nullopt; // Error occurred
    }

    try {
        nlohmann::json       valueJson = nlohmann::json::parse(valueStr);
        MuteModule::MuteInfo info;
        info.expiration_time = MuteMillisToTimePoint(valueJson.at("e").get<uint64_t>());
        info.reason          = valueJson.at("r").get<std::string>();
        return info;
    } catch (const nlohmann::json::parse_error& e) {
        MuteModule::getInstance()
            ->mLogger->error("Failed to parse JSON for Mute '{}' from DB: {}. Value: {}", name, e.what(), valueStr);
        return std::nullopt;
    } catch (const nlohmann::json::type_error& e) {
        MuteModule::getInstance()
            ->mLogger->error("JSON type error for Mute '{}' from DB: {}. Value: {}", name, e.what(), valueStr);
        return std::nullopt;
    } catch (const std::exception& e) {
        MuteModule::getInstance()
            ->mLogger->error("Unexpected error processing JSON for Mute '{}' from DB: {}", name, e.what());
        return std::nullopt;
    }
}

} // anonymous namespace
// --- End of Mute LevelDB Helpers ---


// --- MuteModule Implementation ---
struct MuteCommand {
    CommandSelector<Player> mPlayer;
    int                     mMuteSeconds{};
    std::string             mReason;
};
struct UnMuteCommand {
    CommandSelector<Player> mPlayer;
};
struct ListMuteCommand {
    int mPage = 0;
};
MuteModule* MuteModule::getInstance() {
    static MuteModule instance;
    return &instance;
}

MuteModule::MuteModule() : Module("Mute") {}
bool MuteModule::isMuted(const std::string& name) {
    // Query LevelDB directly
    auto muteInfoOpt = GetMuteFromDB(name);
    if (muteInfoOpt.has_value()) {
        // Check if the mute found in DB is expired
        return muteInfoOpt->expiration_time > std::chrono::system_clock::now();
    }
    return false; // Not found in DB
}
// TODO: Implement MuteModule methods here
bool MuteModule::load() { return true; }
bool MuteModule::enable() {
    {
        std::lock_guard<std::mutex> lock(muteListMutex);
        MuteList.clear();
    }

    auto& mutecmd = ll::command::CommandRegistrar::getInstance()
                        .getOrCreateCommand("mute", "Mute player", CommandPermissionLevel::GameDirectors);
    cmdListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::ExecutingCommandEvent>(
        [this](ll::event::ExecutingCommandEvent& event) {
            std::lock_guard<std::mutex> lock(muteListMutex);
            auto*                       entity = event.commandContext().mOrigin->getEntity();
            if (entity && entity->isType(ActorType::Player)) {
                auto* player = static_cast<Player*>(entity);
                if (event.commandContext().mCommand.find("say") != std::string::npos
                    || event.commandContext().mCommand.find("msg") != std::string::npos
                    || event.commandContext().mCommand.find("tell") != std::string::npos
                    || event.commandContext().mCommand.find("w") != std::string::npos
                    || event.commandContext().mCommand.find("me") != std::string::npos) {
                    if (isMuted(player->getRealName())) {
                        player->sendMessage(fmt::format(
                            "You are muted. Reason: {}",
                            MuteList[player->getRealName()].reason.empty() ? "Not specified"
                                                                           : MuteList[player->getRealName()].reason
                        ));
                        event.cancel();
                    }
                }
            }
        }
    );
    chatListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::PlayerChatEvent>(
        [this](ll::event::PlayerChatEvent& event) {
            std::lock_guard<std::mutex> lock(muteListMutex);
            if (isMuted(event.self().getRealName())) {
                event.self().sendMessage(fmt::format(
                    "You are muted. Reason: {}",
                    MuteList[event.self().getRealName()].reason.empty() ? "Not specified"
                                                                        : MuteList[event.self().getRealName()].reason
                ));
                event.cancel();
            }
        }
    );
    tickHandle = TickSchedulerModule::getInstance()->emplaceTickCallback(
        [this]() {
            std::vector<std::string> namesToRemove;
            auto                     now = std::chrono::system_clock::now();

            // First pass: Collect expired mutes without holding the lock for long
            {
                std::lock_guard<std::mutex> lock(muteListMutex);
                for (const auto& [name, info] : MuteList) {
                    if (info.expiration_time <= now) {
                        namesToRemove.push_back(name);
                    }
                }
            } // Lock released here

            // Second pass: Remove collected names and notify players
            if (!namesToRemove.empty()) {
                std::lock_guard<std::mutex> lock(muteListMutex);
                for (const auto& name : namesToRemove) {
                    // Remove from LevelDB first
                    leveldb::Status status = RemoveMuteFromDB(name);
                    if (!status.ok() && !status.IsNotFound()) {
                        MuteModule::getInstance()->mLogger->warn(
                            "[MuteModule Tick] Failed to remove expired mute for {} from LevelDB: {}",
                            name,
                            status.ToString()
                        );
                    }
                    // Then remove from memory
                    if (MuteList.erase(name)) { // Check if erase was successful (element existed)
                        Player* player = ll::service::getLevel()->getPlayer(name);
                        if (player) {
                            player->sendMessage("You are no longer muted");
                        }
                    }
                }
            }
        },
        100
    );
    mutecmd.overload<MuteCommand>()
        .text("add")
        .required("mPlayer")
        .required("mMuteSeconds")
        .optional("mReason")
        .execute([this](CommandOrigin const& origin, CommandOutput& output, MuteCommand const& param, Command const&) {
            std::lock_guard<std::mutex> lock(muteListMutex);
            if (!param.mPlayer.results(origin).size()) {
                output.error("No player found");
                return;
            }
            if (param.mMuteSeconds <= 0) {
                output.error("Invalid mute duration");
                return;
            }
            for (auto const& player : param.mPlayer.results(origin)) {
                auto        now            = std::chrono::system_clock::now();
                auto        expirationTime = now + std::chrono::seconds(param.mMuteSeconds);
                std::string realName       = player->getRealName();
                std::string reason         = param.mReason.empty() ? "Not specified" : param.mReason;

                // Update memory cache
                MuteList[realName] = {expirationTime, reason};

                // Add to LevelDB
                leveldb::Status status = AddMuteToDB(realName, expirationTime, reason);
                if (!status.ok()) {
                    MuteModule::getInstance()->mLogger->error(
                        "[MuteModule] Failed to add Mute for {} to LevelDB: {}",
                        realName,
                        status.ToString()
                    );
                    output.error(fmt::format("Failed to save mute for {} to database.", realName));
                    // Optionally remove from memory cache if DB write fails?
                    MuteList.erase(realName);
                    continue; // Skip messaging the player if save failed
                }

#ifdef DEBUG
                output.success(
                    "{} has been muted for {} seconds until {}",
                    player->getRealName(),
                    param.mMuteSeconds,
                    // Format expirationTime nicely if needed, requires <chrono> formatting or a library
                    "future time" // Placeholder for formatted time
                );
#endif
                player->sendMessage(
                    fmt::format("You have been muted for {} seconds. Reason: {}", param.mMuteSeconds, reason)
                );
            }
        });
    mutecmd.overload<ListMuteCommand>().text("list").optional("mPage").execute(
        [this](CommandOrigin const&, CommandOutput& output, ListMuteCommand const& param, Command const&) {
            std::lock_guard<std::mutex> lock(muteListMutex);
            if (MuteList.empty()) {
                output.error("No active mutes found");
                return;
            }

            const size_t pageCount = (MuteList.size() + ListCommandPageSize - 1) / ListCommandPageSize;
            if (param.mPage < 0 || static_cast<size_t>(param.mPage) >= pageCount) {
                output.error("Invalid page number (0-" + std::to_string(pageCount - 1) + ")");
                return;
            }

            // Calculate page range
            const auto startIdx = param.mPage * ListCommandPageSize;
            const auto endIdx   = std::min(startIdx + ListCommandPageSize, static_cast<int>(MuteList.size()));

            // Get current time for remaining duration calculation
            const auto now = std::chrono::system_clock::now();

            // Header
            output.success(
                "Mute List (Page " + std::to_string(param.mPage + 1) + "/" + std::to_string(pageCount) + ")"
            );
// Iterate through the page items
#ifdef DEBUG
            {
                BenchMark bench([&output](Timer& timer) {
                    output.success("Output Mute using time: {}", timer.elapsed());
                });
#endif
                auto it = MuteList.begin();
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
            output.success("Total mutes: {} ", MuteList.size());
#endif
        }
    );
    mutecmd.overload<UnMuteCommand>().text("remove").required("mPlayer").execute(
        [this](CommandOrigin const& origin, CommandOutput& output, UnMuteCommand const& param, Command const&) {
            std::lock_guard<std::mutex> lock(muteListMutex);
            if (!param.mPlayer.results(origin).size()) {
                output.error("No player found");
                return;
            }
            for (auto const& player : param.mPlayer.results(origin)) {
                std::string realName = player->getRealName();

                // Remove from LevelDB first
                leveldb::Status status = RemoveMuteFromDB(realName);
                if (!status.ok() && !status.IsNotFound()) {
                    mLogger->error("Failed to remove Mute for {} from LevelDB: {}", realName, status.ToString());
                    output.error(fmt::format("Failed to remove mute for {} from database.", realName));
                    // Continue to attempt memory removal? Yes.
                }

                // Remove from memory cache
                if (MuteList.erase(realName)) {
                    output.success(fmt::format("Unmuted {}.", realName));
                    player->sendMessage("You have been unmuted.");
                } else {
                    // If not found in memory, double-check DB wasn't the only place it existed
                    if (status.ok() || status.IsNotFound()) { // If DB removal succeeded or it wasn't there anyway
                        output.success(fmt::format("{} was not muted.", realName));
                        player->sendMessage("You were not muted.");
                    }
                    // If DB removal failed, we already sent an error message.
                }
            }
        }
    );
    return true;
}

bool MuteModule::disable() {
    ll::event::EventBus::getInstance().removeListener(cmdListener);
    ll::event::EventBus::getInstance().removeListener(chatListener);
    TickSchedulerModule::getInstance()->removeTickCallback(tickHandle);
    return true;
}

// --- JSON Serialization ---

void MuteModule::to_json(nlohmann::json& j) const {
    // Mute list is now saved to LevelDB, no longer to JSON.
    // Only save configuration options.
    j["ListCommandPageSize"] = ListCommandPageSize;
}

void MuteModule::from_json(const nlohmann::json& j) {
    // Mute list is now loaded from LevelDB in enable(), no longer from JSON.
    // Only load configuration options.
    if (j.contains("ListCommandPageSize")) {
        if (j.at("ListCommandPageSize").is_number_integer()) {
            ListCommandPageSize = j["ListCommandPageSize"].get<int>();
        } else {
            NativeAntiCheat::getInstance().getSelf().getLogger().warn(
                "Invalid ListCommandPageSize value in config, using default."
            );
            ListCommandPageSize = 10; // Reset to default
        }
    } else {
        NativeAntiCheat::getInstance().getSelf().getLogger().warn(
            "ListCommandPageSize field missing from config, using default."
        );
        ListCommandPageSize = 10; // Use default
    }

    // Clear the in-memory list here as well, in case of config reloads after enable().
    // Loading from DB should happen in enable().
    std::lock_guard<std::mutex> lock(muteListMutex);
    MuteList.clear();
}
// --- Static LevelDB DAO Function Implementations ---

leveldb::Status MuteModule::AddMuteToDB(
    const std::string&                           name,
    const std::chrono::system_clock::time_point& expiration_time,
    const std::string&                           reason
) {
    auto& dbService = LevelDBService::GetInstance();
    if (!dbService.IsInitialized()) {
        MuteModule::getInstance()->mLogger->error("LevelDBService not initialized for AddMuteToDB.");
        return leveldb::Status::IOError("LevelDBService not initialized");
    }

    std::string primaryKey = CreateMutePrimaryKey(name);
    std::string expiryKey  = CreateMuteExpiryKey(name, expiration_time);

    nlohmann::json valueJson;
    valueJson["e"]       = MuteTimePointToMillis(expiration_time);
    valueJson["r"]       = reason;
    std::string valueStr = valueJson.dump();

    leveldb::WriteBatch batch;
    batch.Put(primaryKey, valueStr);
    batch.Put(expiryKey, ""); // Empty value for expiry key

    leveldb::WriteOptions writeOptions;
    // writeOptions.sync = true; // Optional

    leveldb::Status status = dbService.Write(writeOptions, &batch);
    if (!status.ok()) {
        MuteModule::getInstance()->mLogger->error("Failed to add Mute for '{}' to DB: {}", name, status.ToString());
    } else {
        MuteModule::getInstance()->mLogger->debug("Added Mute for '{}' to DB.", name);
    }
    return status;
}

std::optional<MuteModule::MuteInfo> MuteModule::GetMuteFromDB(const std::string& name) {
    return GetMuteInfoFromDBInternal(name);
}

leveldb::Status MuteModule::RemoveMuteFromDB(const std::string& name) {
    auto& dbService = LevelDBService::GetInstance();
    if (!dbService.IsInitialized()) {
        MuteModule::getInstance()->mLogger->error("LevelDBService not initialized for RemoveMuteFromDB.");
        return leveldb::Status::IOError("LevelDBService not initialized");
    }

    // 1. Get mute info to find the expiration time for the expiry key
    std::optional<MuteInfo> muteInfoOpt = GetMuteInfoFromDBInternal(name);

    if (!muteInfoOpt.has_value()) {
        // Check if it really doesn't exist or if Get failed previously
        std::string     primaryKeyCheck = CreateMutePrimaryKey(name);
        std::string     valueStrCheck;
        leveldb::Status getStatus = dbService.Get(primaryKeyCheck, &valueStrCheck);
        if (getStatus.IsNotFound()) {
            MuteModule::getInstance()->mLogger->debug("Mute for '{}' not found in DB, removal skipped.", name);
            return leveldb::Status::OK(); // Already gone is success
        }
        MuteModule::getInstance()->mLogger->warn(
            "Mute info for '{}' disappeared or Get failed before removal from DB. Status: {}",
            name,
            getStatus.ToString()
        );
        return getStatus.ok() ? leveldb::Status::NotFound("Mute info disappeared before removal from DB") : getStatus;
    }

    // 2. Construct keys
    std::string primaryKey = CreateMutePrimaryKey(name);
    std::string expiryKey  = CreateMuteExpiryKey(name, muteInfoOpt.value().expiration_time);

    // 3. Prepare batch delete
    leveldb::WriteBatch batch;
    batch.Delete(primaryKey);
    batch.Delete(expiryKey);

    leveldb::WriteOptions writeOptions;
    // writeOptions.sync = true;

    leveldb::Status status = dbService.Write(writeOptions, &batch);
    if (!status.ok()) {
        MuteModule::getInstance()
            ->mLogger->error("Failed to remove Mute for '{}' from DB: {}", name, status.ToString());
    } else {
        MuteModule::getInstance()->mLogger->debug("Removed Mute for '{}' from DB.", name);
    }
    return status;
}

// --- End of Static LevelDB DAO Function Implementations ---
} // namespace native_ac
REGISTER_MODULE(native_ac::MuteModule, native_ac::MuteModule::getInstance());
