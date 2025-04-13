#include "Mute.hpp"
#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/command/ExecuteCommandEvent.h"
#include "ll/api/service/Bedrock.h"
#include "mc/network/NetEventCallback.h"
#include "mc/network/ServerNetworkHandler.h"
#include "mc/network/packet/Packet.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/CommandSelector.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"
#include "mod/Module/Module.hpp"
#include "mod/Module/ModuleManager.hpp" // Needed for REGISTER_MODULE
#include <nlohmann/json.hpp>
#include <thread>
#include <vector> // For collecting names to remove


namespace native_ac {
struct MuteCommand {
    CommandSelector<Player> mPlayer;
    int                     mMuteSeconds{};
    std::string             mReason;
};
struct UnMuteCommand {
    CommandSelector<Player> mPlayer;
};
MuteModule::MuteModule() : Module("Mute") {}
bool MuteModule::isMuted(const std::string& name) {
    auto it = MuteList.find(name);
    if (it == MuteList.end()) {
        return false;
    }
    // Check if the expiration time is in the future
    return it->second.expiration_time > std::chrono::system_clock::now();
}
// TODO: Implement MuteModule methods here
bool MuteModule::load() { return true; }
void MuteModule::cleanMuteList() {
    // Note: The loop control with cleanMuteListRunnning might need refinement for instant shutdown.
    // Consider using a condition variable if more precise control is needed.
    while (cleanMuteListRunnning) {
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
                if (MuteList.erase(name)) { // Check if erase was successful (element existed)
                    Player* player = ll::service::getLevel()->getPlayer(name);
                    if (player) {
                        player->sendMessage("You are no longer muted");
                    }
                }
            }
        }

        // Sleep only if the loop should continue
        if (cleanMuteListRunnning) {
            std::this_thread::sleep_for(std::chrono::seconds(5)); // Check every 5 seconds
        }
    }
}
bool MuteModule::enable() {
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
                event.cancel();
            }
        }
    );
    cleanMuteListRunnning = true;
    std::thread([this] { cleanMuteList(); }).detach();
    mutecmd.overload<MuteCommand>()
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
                auto now                        = std::chrono::system_clock::now();
                auto expirationTime             = now + std::chrono::seconds(param.mMuteSeconds);
                MuteList[player->getRealName()] = {expirationTime, param.mMuteSeconds, param.mReason};
#ifdef DEBUG
                output.success(
                    "{} has been muted for {} seconds until {}",
                    player->getRealName(),
                    param.mMuteSeconds,
                    // Format expirationTime nicely if needed, requires <chrono> formatting or a library
                    "future time" // Placeholder for formatted time
                );
#endif
                player->sendMessage(fmt::format(
                    "You have been muted for {} seconds. Reason: {}",
                    param.mMuteSeconds,
                    param.mReason.empty() ? "Not specified" : param.mReason
                ));
            }
        });
    auto& unmutecmd = ll::command::CommandRegistrar::getInstance()
                          .getOrCreateCommand("unmute", "Unmute player", CommandPermissionLevel::GameDirectors);
    unmutecmd.overload<UnMuteCommand>().required("mPlayer").execute(
        [this](CommandOrigin const& origin, CommandOutput& output, UnMuteCommand const& param, Command const&) {
            std::lock_guard<std::mutex> lock(muteListMutex);
            if (!param.mPlayer.results(origin).size()) {
                output.error("No player found");
                return;
            }
            for (auto const& player : param.mPlayer.results(origin)) {
                if (!MuteList.erase(player->getRealName())) {
#ifdef DEBUG
                    output.error("{} is not muted", player->getRealName());
#endif
                    player->sendMessage("You are not muted");
                }
            }
        }
    );
    return true;
}

bool MuteModule::disable() {
    ll::event::EventBus::getInstance().removeListener(cmdListener);
    ll::event::EventBus::getInstance().removeListener(chatListener);
    cleanMuteListRunnning = false; // Signal the cleaning thread to stop
    // Note: Consider joining the thread here if cleanMuteList isn't detached and needs graceful shutdown.
    return true;
}

// --- JSON Serialization ---

void MuteModule::to_json(nlohmann::json& j) const {
    std::lock_guard<std::mutex> lock(muteListMutex);
    nlohmann::json              muteListData = nlohmann::json::object();
    for (const auto& [name, info] : MuteList) {
        // Only save mutes that haven't expired yet
        if (info.expiration_time > std::chrono::system_clock::now()) {
            nlohmann::json muteInfoJson;
            muteInfoJson["expiration_timestamp_ms"] =
                std::chrono::duration_cast<std::chrono::milliseconds>(info.expiration_time.time_since_epoch()).count();
            muteInfoJson["duration_seconds"] = info.duration_seconds;
            muteInfoJson["reason"]           = info.reason;
            muteListData[name]               = muteInfoJson;
        }
    }
    j["MuteList"] = muteListData; // Store under a key for clarity
}

void MuteModule::from_json(const nlohmann::json& j) {
    if (!j.contains("MuteList") || !j["MuteList"].is_object()) {
        // Log error or handle missing/invalid data
        return;
    }
    std::lock_guard<std::mutex> lock(muteListMutex);
    MuteList.clear(); // Clear existing data before loading
    const auto& muteListData = j["MuteList"];
    auto        now          = std::chrono::system_clock::now();
    for (auto it = muteListData.begin(); it != muteListData.end(); ++it) {
        const std::string& name         = it.key();
        const auto&        muteInfoJson = it.value();
        if (!muteInfoJson.is_object() || !muteInfoJson.contains("expiration_timestamp_ms")
            || !muteInfoJson.contains("duration_seconds") || !muteInfoJson.contains("reason")) {
            // Log error for invalid entry format
            continue;
        }
        long long   expirationMs   = muteInfoJson["expiration_timestamp_ms"].get<long long>();
        int         durationSecs   = muteInfoJson["duration_seconds"].get<int>();
        std::string reason         = muteInfoJson["reason"].get<std::string>();
        auto        expirationTime = std::chrono::system_clock::time_point(std::chrono::milliseconds(expirationMs));
        // Only load mutes that haven't expired yet
        if (expirationTime > now) {
            MuteList[name] = {expirationTime, durationSecs, reason};
        }
    }
}

} // namespace native_ac
REGISTER_MODULE(native_ac::MuteModule);