#include "Mute.hpp"
#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/command/ExecuteCommandEvent.h"
#include "ll/api/event/world/LevelTickEvent.h"
#include "ll/api/service/Bedrock.h"
#include "mc/network/NetEventCallback.h"
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
#include "mod/Module/Modules/TickScheduler.hpp"
#include "mod/NativeAntiCheat.h"
#include <iomanip> // Required for std::put_time, std::get_time
#include <nlohmann/json.hpp>
#include <sstream> // Required for std::ostringstream, std::istringstream
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
struct ListMuteCommand {
    int mPage = 0;
};
MuteModule* MuteModule::getInstance() {
    static MuteModule instance;
    return &instance;
}

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
                auto now                        = std::chrono::system_clock::now();
                auto expirationTime             = now + std::chrono::seconds(param.mMuteSeconds);
                MuteList[player->getRealName()] = {expirationTime, param.mReason}; // Removed duration_seconds
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
                if (!MuteList.erase(player->getRealName())) {
                    output.success("{} is not muted", player->getRealName());
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
    TickSchedulerModule::getInstance()->removeTickCallback(tickHandle);
    return true;
}

// --- JSON Serialization ---

void MuteModule::to_json(nlohmann::json& j) const {
    std::lock_guard<std::mutex> lock(muteListMutex);
    nlohmann::json              muteListData = nlohmann::json::object();
    const char*                 fmt          = "%Y-%m-%d %H:%M:%S"; // 定义格式化字符串

    for (const auto& [name, info] : MuteList) {
        // 只保存尚未过期的禁言记录
        if (info.expiration_time > std::chrono::system_clock::now()) {
            nlohmann::json muteInfoJson;

            // 将 time_point 转换为 time_t，然后转换为 tm (本地时间)
            std::time_t expiration_tt = std::chrono::system_clock::to_time_t(info.expiration_time); // 获取 time_t
            std::tm     expiration_tm;                   // 声明 tm 结构体变量
            localtime_s(&expiration_tm, &expiration_tt); // 使用 localtime_s

            // 将时间格式化为字符串
            std::ostringstream oss;
            oss << std::put_time(&expiration_tm, fmt);
            muteInfoJson["expiration_time_str"] = oss.str(); // 存储格式化后的时间字符串
            // Removed serialization of duration_seconds
            muteInfoJson["reason"] = info.reason;
            muteListData[name]     = muteInfoJson;
        }
    }
    j["MuteList"]            = muteListData;        // 将禁言列表数据存储在 "MuteList" 键下，以提高清晰度
    j["ListCommandPageSize"] = ListCommandPageSize; // 存储禁言列表分页大小
}

void MuteModule::from_json(const nlohmann::json& j) {
    if (!j.contains("MuteList") || !j["MuteList"].is_object()) {
        NativeAntiCheat::getInstance().getSelf().getLogger().error("JSON data missing MuteList or invalid format.");
        return;
    }
    if (j.contains("ListCommandPageSize")) {
        if (j.at("ListCommandPageSize").is_number_integer()) {
            ListCommandPageSize = j["ListCommandPageSize"].get<int>();
        } else {
            NativeAntiCheat::getInstance().getSelf().getLogger().warn(
                "Invalid ListCommandPageSize value, using default."
            );
        }
    } else {
        NativeAntiCheat::getInstance().getSelf().getLogger().warn("ListCommandPageSize not found, using default.");
    }
    std::lock_guard<std::mutex> lock(muteListMutex);
    MuteList.clear(); // 加载前清除现有数据
    const auto& muteListData = j["MuteList"];
    auto        now          = std::chrono::system_clock::now();
    const char* fmt          = "%Y-%m-%d %H:%M:%S"; // 定义格式化字符串

    for (auto it = muteListData.begin(); it != muteListData.end(); ++it) {
        const std::string& name         = it.key();
        const auto&        muteInfoJson = it.value();

        // 检查是否为新的字符串格式以及是否包含必要的字段
        // Check if it's the new format (without duration_seconds)
        if (muteInfoJson.is_object() && muteInfoJson.contains("expiration_time_str") && muteInfoJson.contains("reason")
            && !muteInfoJson.contains("duration_seconds")) {
            std::string expirationStr = muteInfoJson["expiration_time_str"].get<std::string>();
            // int         durationSecs  = muteInfoJson["duration_seconds"].get<int>(); // Removed
            std::string reason = muteInfoJson["reason"].get<std::string>();

            std::tm            expiration_tm = {};
            std::istringstream iss(expirationStr);
            iss >> std::get_time(&expiration_tm, fmt); // 解析字符串

            if (iss.fail()) {
                // 记录时间格式无效的错误
                // 可以在此处添加日志记录: logger.error("解析用户 {} 的过期时间字符串 '{}' 失败", name,
                // expirationStr);
                NativeAntiCheat::getInstance().getSelf().getLogger().error(
                    "Failed to parse expiration time string for user {}: {}",
                    name,
                    expirationStr
                );
                continue;
            }

            // 将 tm 转换回 time_t，然后转换为 time_point
            std::time_t expiration_tt = std::mktime(&expiration_tm);
            if (expiration_tt == -1) {
                // 记录时间转换无效的错误
                // 可以在此处添加日志记录: logger.error("将用户 {} 解析后的时间转换为 time_t 失败", name);
                NativeAntiCheat::getInstance().getSelf().getLogger().error(
                    "Failed to convert parsed expiration time for user {} to time_t",
                    name
                );
                continue;
            }
            auto expirationTime = std::chrono::system_clock::from_time_t(expiration_tt);

            // 只加载尚未过期的禁言记录
            if (expirationTime > now) {
                MuteList[name] = {expirationTime, reason}; // Removed durationSecs
            }
        } else {
            // 记录条目格式无效或缺少必要字段的错误
            // Removed handling for old format with duration_seconds
            NativeAntiCheat::getInstance().getSelf().getLogger().warn(
                "Skipping invalid or incomplete mute entry for key '{}' (expected format without duration_seconds)",
                name
            );
            continue;
        }
    }
}

} // namespace native_ac
REGISTER_MODULE(native_ac::MuteModule, native_ac::MuteModule::getInstance());