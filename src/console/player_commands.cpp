// SPDX-License-Identifier: GPL-3.0-or-later
#include "console/player_commands.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "console/command_spec.h"
#include "core/log.h"
#include "core/log_record.h"

// @coney-only the Coney console's player commands (docs/guides/console.md#player-commands); the original has no
// console.

namespace coney::console {

namespace {

// A handler's result: failed, with one line the terminal shows as the command's failure.
using Result = std::expected<void, std::string>;

// The whole number `text` spells; nothing when it spells none or does not fit an int.
std::optional<int> parseInt(std::string_view text) {
    int value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

// The heading `help` lists a command under.
std::string groupOf(const CommandSpec& spec) {
    return spec.group.empty() ? std::string("Mod commands") : std::string(spec.group);
}

// What `help` marks a command with after its name: the debug tools' commands and a mod's.
std::string markOf(const Command& command) {
    if (command.origin == Origin::Mod) {
        return std::format(" [{}]", command.mod);
    }
    return command.spec().access == Access::Developer ? " [dev]" : "";
}

// `help`: every present command by group, or one command's usage, what it does and its arguments.
Result help(Console& console, CommandContext& context) {
    const Registry& registry = console.registry();
    const std::string_view wanted = context.args.get("command");
    if (!wanted.empty()) {
        const auto id = registry.resolve(wanted);
        if (!id) {
            return std::unexpected(id.error().message);
        }
        const Command& command = *registry.find(*id);
        context.print(usageLine(command.spec(), wanted));
        context.print(std::format("  {}", command.spec().summary));
        std::size_t widest = 0;
        for (const ArgSpec& arg : command.spec().args) {
            widest = std::max(widest, arg.name.size() + 2);
        }
        for (const ArgSpec& arg : command.spec().args) {
            const std::string name =
                std::format("{}{}{}", arg.optional ? "[" : "<", arg.name, arg.optional ? "]" : ">");
            context.print(std::format("  {:<{}}  {}", name, widest, arg.help));
        }
        return {};
    }
    context.print(std::format("{}: the commands this build has (help <command> says more about one)", kConsoleBanner));
    // The groups in the order their first command registered, the commands in registration order inside each.
    const std::vector<const Command*> commands = registry.commands();
    std::vector<std::string> groups;
    for (const Command* command : commands) {
        const std::string group = groupOf(command->spec());
        if (std::ranges::find(groups, group) == groups.end()) {
            groups.push_back(group);
        }
    }
    std::size_t widest = 0;
    for (const Command* command : commands) {
        widest = std::max(widest, command->spec().name.size() + markOf(*command).size());
    }
    for (const std::string& group : groups) {
        context.print(group);
        for (const Command* command : commands) {
            if (groupOf(command->spec()) != group) {
                continue;
            }
            const std::string name = std::format("{}{}", command->spec().name, markOf(*command));
            context.print(std::format("  {:<{}}  {}", name, widest, command->spec().summary));
        }
    }
    return {};
}

// `version`: Coney's version and build, the disc's region and the renderer, as the session host knows them.
Result version(Console& console, CommandContext& context) {
    const SessionHost* session = console.hosts().session;
    const std::optional<BuildInfo> info = session != nullptr ? session->buildInfo() : std::nullopt;
    if (!info) {
        context.print("Coney (this run gives no version information)");
        return {};
    }
    context.print(std::format("Coney {} ({})", info->version, info->build));
    context.print(
        std::format("disc: {}; renderer: {}", info->discRegion.empty() ? "none" : info->discRegion, info->renderer));
    return {};
}

// `where`: the level, the checkpoint, the step and the play time, as the run log's stamp reads them.
Result where(Console& console, CommandContext& context) {
    const SessionHost* session = console.hosts().session;
    const std::optional<std::string> level = session != nullptr ? session->levelName() : std::nullopt;
    const std::uint64_t step = logging::step();
    const std::string time = logging::playTime(step);
    if (!level) {
        context.print(std::format("no level in play, step {} ({})", step, time));
        return {};
    }
    const int checkpoint = logging::checkpoint();
    const std::string cp = checkpoint == logging::kNoCheckpoint ? std::string("none") : std::to_string(checkpoint);
    context.print(std::format("level {}, checkpoint {}, step {} ({})", *level, cp, step, time));
    return {};
}

// `bugreport`: the run log's last 30 s into the log file, through the host that owns the file; with none, through the
// logger's own dump.
Result bugReport(Console& console, CommandContext& context) {
    SessionHost* session = console.hosts().session;
    std::string line = session != nullptr ? session->writeBugReport() : std::string();
    if (line.empty()) {
        line = std::format("bug report: the last {} events written to the log", logging::dump("bug report"));
    }
    context.print(line);
    return {};
}

// `quit`: a guest leaves the host's game first, as the pause menu's Player 2 quit does, then the game ends through its
// normal exit; a host's session ends with the game.
Result quit(Console& console, CommandContext& context) {
    SessionHost* session = console.hosts().session;
    if (session == nullptr) {
        return std::unexpected("there is no game to end");
    }
    NetworkHost* network = console.hosts().network;
    if (network != nullptr && network->role() != SessionRole::Offline) {
        network->leave();
    }
    if (!session->requestQuit()) {
        return std::unexpected("this run cannot be ended from the console");
    }
    context.print("quitting");
    return {};
}

// `settings`: each setting with its value, the values in a column.
Result settings(Console& console, CommandContext& context) {
    const SessionHost* session = console.hosts().session;
    const std::vector<SettingInfo> all = session != nullptr ? session->settings() : std::vector<SettingInfo>();
    if (all.empty()) {
        return std::unexpected("this run has no settings to show");
    }
    std::size_t widest = 0;
    for (const SettingInfo& setting : all) {
        widest = std::max(widest, setting.name.size());
    }
    for (const SettingInfo& setting : all) {
        context.print(std::format("{:<{}}  {}", setting.name, widest, setting.value));
    }
    return {};
}

// `get <setting>`: one setting's value.
Result get(Console& console, CommandContext& context) {
    const SessionHost* session = console.hosts().session;
    const std::vector<SettingInfo> all = session != nullptr ? session->settings() : std::vector<SettingInfo>();
    const std::string_view name = context.args.get("setting");
    const auto found = std::ranges::find(all, name, &SettingInfo::name);
    if (found == all.end()) {
        return std::unexpected(std::format(R"(no setting named "{}"; settings lists them)", name));
    }
    context.print(std::format("{} {}", found->name, found->value));
    return {};
}

// `set <setting> <value>`: changes a setting for this session through the host; nothing is written to a file.
Result set(Console& console, CommandContext& context) {
    SessionHost* session = console.hosts().session;
    if (session == nullptr) {
        return std::unexpected("this run has no settings to change");
    }
    const std::string_view name = context.args.get("setting");
    const auto changed = session->setSetting(name, context.args.get("value"));
    if (!changed) {
        return std::unexpected(changed.error());
    }
    context.print(std::format("{} {} (for this session)", name, *changed));
    return {};
}

// How a completed mission reads: `3  checkpoints 1-4`.
std::string missionLine(const CompletedMission& mission) {
    if (mission.checkpoints <= 1) {
        return std::format("{}  checkpoint 1", mission.mission);
    }
    return std::format("{}  checkpoints 1-{}", mission.mission, mission.checkpoints);
}

// `missions`: what the active profile has completed, with the checkpoints of each.
Result missions(Console& console, CommandContext& context) {
    const SessionHost* session = console.hosts().session;
    const std::vector<CompletedMission> done =
        session != nullptr ? session->completedMissions() : std::vector<CompletedMission>();
    if (done.empty()) {
        context.print("no completed missions");
        return {};
    }
    for (const CompletedMission& mission : done) {
        context.print(missionLine(mission));
    }
    return {};
}

// `mission <n> [checkpoint]`: starts a completed mission at one of its checkpoints, as the mission select does. The
// profile may have changed since the line was checked, so its list is read again here.
Result mission(Console& console, CommandContext& context) {
    SessionHost* session = console.hosts().session;
    if (session == nullptr) {
        return std::unexpected("there is no game to start a mission in");
    }
    const std::optional<int> number = parseInt(context.args.get("mission"));
    const std::optional<int> checkpoint = parseInt(context.args.get("checkpoint"));
    if (!number || !checkpoint) {
        return std::unexpected("a mission and a checkpoint are whole numbers");
    }
    const std::vector<CompletedMission> done = session->completedMissions();
    const auto found = std::ranges::find(done, *number, &CompletedMission::mission);
    if (found == done.end()) {
        return std::unexpected(std::format("mission {} is not one the profile has completed", *number));
    }
    if (*checkpoint < 1 || *checkpoint > found->checkpoints) {
        return std::unexpected(
            found->checkpoints <= 1
                ? std::format("mission {} has only checkpoint 1, not {}", *number, *checkpoint)
                : std::format("mission {} has checkpoints 1 to {}, not {}", *number, found->checkpoints, *checkpoint));
    }
    const auto started = session->startMission(*number, *checkpoint);
    if (!started) {
        return std::unexpected(started.error());
    }
    context.print(*started);
    return {};
}

// `host [port]`: hosts a network game from an offline one.
Result host(Console& console, CommandContext& context) {
    NetworkHost* network = console.hosts().network;
    if (network == nullptr) {
        return std::unexpected("network play is not available in this run");
    }
    if (network->role() != SessionRole::Offline) {
        return std::unexpected("already in a network session; disconnect first");
    }
    std::optional<int> port;
    if (const std::string_view text = context.args.get("port"); !text.empty()) {
        port = parseInt(text);
        if (!port || *port < 0 || *port > 65535) {
            return std::unexpected("a port is a whole number from 0 to 65535");
        }
    }
    const auto hosted = network->host(port);
    if (!hosted) {
        return std::unexpected(hosted.error());
    }
    context.print(*hosted);
    return {};
}

// `connect <address>`: joins a network game from an offline one.
Result connect(Console& console, CommandContext& context) {
    NetworkHost* network = console.hosts().network;
    if (network == nullptr) {
        return std::unexpected("network play is not available in this run");
    }
    if (network->role() != SessionRole::Offline) {
        return std::unexpected("already in a network session; disconnect first");
    }
    const auto joined = network->join(context.args.get("address"));
    if (!joined) {
        return std::unexpected(joined.error());
    }
    context.print(*joined);
    return {};
}

// `disconnect`: leaves the session, a guest as the Player 2 quit and the host by ending it.
Result disconnect(Console& console, CommandContext& context) {
    NetworkHost* network = console.hosts().network;
    if (network == nullptr || !network->leave()) {
        return std::unexpected("not in a network session");
    }
    context.print("left the network session");
    return {};
}

} // namespace

std::vector<BuiltinHandler> playerHandlers(Console& console) {
    // Each handler gets the console by reference; the console owns the registry that owns the handlers.
    const auto bind = [&console](Result (*run)(Console&, CommandContext&)) -> Handler {
        return [&console, run](CommandContext& context) { return run(console, context); };
    };
    std::vector<BuiltinHandler> handlers;
    handlers.push_back({"help", bind(help)});
    handlers.push_back({"clear", [&console](CommandContext&) -> Result {
                            console.terminal().clear();
                            return {};
                        }});
    handlers.push_back({"version", bind(version)});
    handlers.push_back({"quit", bind(quit)});
    handlers.push_back({"where", bind(where)});
    handlers.push_back({"bugreport", bind(bugReport)});
    handlers.push_back({"settings", bind(settings)});
    handlers.push_back({"get", bind(get)});
    handlers.push_back({"set", bind(set)});
    handlers.push_back({"missions", bind(missions)});
    handlers.push_back({"mission", bind(mission)});
    handlers.push_back({"host", bind(host)});
    handlers.push_back({"connect", bind(connect)});
    handlers.push_back({"disconnect", bind(disconnect)});
    return handlers;
}

std::expected<std::size_t, Error> registerPlayerCommands(Console& console) {
    const std::vector<BuiltinHandler> handlers = playerHandlers(console);
    return registerBuiltins(console.registry(), playerCatalog(), handlers);
}

} // namespace coney::console
