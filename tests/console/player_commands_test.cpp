// SPDX-License-Identifier: GPL-3.0-or-later
#include "console/player_commands.h"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "console/catalog.h"
#include "console/command_spec.h"
#include "console/console.h"
#include "console/hosts.h"
#include "console/registry.h"
#include "core/log.h"

using coney::console::Access;
using coney::console::ArgType;
using coney::console::CompletedMission;
using coney::console::Console;
using coney::console::LineKind;
using coney::console::LineSource;
using coney::console::Need;
using coney::console::NetworkHost;
using coney::console::Outcome;
using coney::console::playerCatalog;
using coney::console::registerPlayerCommands;
using coney::console::SessionHost;
using coney::console::SessionRole;
using Strings = std::vector<std::string>;

// The release set's commands against fake hosts: docs/guides/studio/console.md#release-set (what each does),
// #access (scope) and #coop (who may run what in co-op), tests of the design's #tests.

namespace {

// A session host that records what the commands ask of it.
class FakeSession final : public SessionHost {
  public:
    bool game = true;
    bool profile = true;
    bool level = true;
    std::optional<coney::console::BuildInfo> build;
    std::optional<std::string> levelId;
    std::vector<coney::console::SettingInfo> table{{"fps_cap", "60", "Frames a second at most"},
                                                   {"show_fps", "off", "Shows the frame rate"},
                                                   {"vsync", "on", "Waits for the vertical blank"}};
    std::vector<CompletedMission> missions;
    std::string bugReportLine;
    bool canQuit = true;
    int quits = 0;
    int bugReports = 0;
    Strings setCalls;
    std::vector<std::pair<int, int>> started;
    Strings* order = nullptr; // where quit and leave write themselves, to prove their order

    [[nodiscard]] bool has(Need need) const override {
        switch (need) {
        case Need::Game:
            return game;
        case Need::Profile:
            return profile;
        case Need::Level:
            return level;
        case Need::Disc:
            return false;
        }
        return false;
    }
    [[nodiscard]] std::optional<Strings> values(ArgType type, std::span<const std::string>) const override {
        if (type == ArgType::Setting) {
            Strings names;
            for (const auto& setting : table) {
                names.push_back(setting.name);
            }
            return names;
        }
        return std::nullopt;
    }
    [[nodiscard]] std::optional<coney::console::BuildInfo> buildInfo() const override { return build; }
    [[nodiscard]] std::optional<std::string> levelName() const override { return levelId; }
    [[nodiscard]] std::vector<coney::console::SettingInfo> settings() const override { return table; }
    [[nodiscard]] std::expected<std::string, std::string> setSetting(std::string_view name,
                                                                     std::string_view value) override {
        setCalls.push_back(std::string(name) + "=" + std::string(value));
        const auto found = std::ranges::find(table, name, &coney::console::SettingInfo::name);
        if (found == table.end()) {
            return std::unexpected("no setting named \"" + std::string(name) + "\"");
        }
        if (value == "bad") {
            return std::unexpected("\"bad\" is not a value of " + std::string(name));
        }
        found->value = std::string(value);
        return found->value;
    }
    [[nodiscard]] std::string writeBugReport() override {
        ++bugReports;
        return bugReportLine;
    }
    bool requestQuit() override {
        if (order != nullptr) {
            order->push_back("quit");
        }
        ++quits;
        return canQuit;
    }
    [[nodiscard]] std::vector<CompletedMission> completedMissions() const override { return missions; }
    [[nodiscard]] std::expected<std::string, std::string> startMission(int mission, int checkpoint) override {
        started.emplace_back(mission, checkpoint);
        return "starting mission " + std::to_string(mission) + " at checkpoint " + std::to_string(checkpoint);
    }
};

// A network host with a role the test sets, that records what it is asked.
class FakeNetwork final : public NetworkHost {
  public:
    SessionRole current = SessionRole::Offline;
    Strings calls;
    Strings* order = nullptr;
    bool hostFails = false;
    bool leaveFails = false; // leave() says it could not

    [[nodiscard]] SessionRole role() const override { return current; }
    [[nodiscard]] std::expected<std::string, std::string> host(std::optional<int> port) override {
        calls.push_back("host " + (port ? std::to_string(*port) : std::string("default")));
        if (hostFails) {
            return std::unexpected(std::string("the port cannot be opened"));
        }
        current = SessionRole::Host;
        return "hosting on port " + std::to_string(port.value_or(27615));
    }
    [[nodiscard]] std::expected<std::string, std::string> join(std::string_view address) override {
        calls.push_back("join " + std::string(address));
        current = SessionRole::Guest;
        return "connecting to " + std::string(address);
    }
    bool leave() override {
        if (order != nullptr) {
            order->push_back("leave");
        }
        calls.emplace_back("leave");
        const bool was = current != SessionRole::Offline;
        current = SessionRole::Offline;
        return was && !leaveFails;
    }
};

// A console with the player commands over fake hosts.
struct Fixture {
    FakeSession session;
    FakeNetwork network;
    Console console;

    explicit Fixture(Access highest = Access::Player)
        : console({.highest = highest}, {.session = &session, .network = &network}) {
        const auto registered = registerPlayerCommands(console);
        REQUIRE(registered.has_value());
    }

    // Enters `line` and runs what it queued, and returns what it printed (not the echoed line), errors included.
    Strings run(std::string_view line, int slot = 0) {
        console.terminal().clear();
        (void)console.submit(line, LineSource::Script, static_cast<coney::console::PlayerSlot>(slot));
        console.runQueued();
        Strings lines;
        for (const auto& output : console.terminal().output()) {
            if (output.kind != LineKind::Echo) {
                lines.push_back(output.text);
            }
        }
        return lines;
    }
};

// Whether any line of `lines` contains `text`.
bool contains(const Strings& lines, std::string_view text) {
    return std::ranges::any_of(lines, [text](const std::string& line) { return line.find(text) != std::string::npos; });
}

// Leaves the logger as it was found, whatever a test did to it.
struct LoggerGuard {
    LoggerGuard() { coney::logging::reset(); }
    LoggerGuard(const LoggerGuard&) = delete;
    LoggerGuard& operator=(const LoggerGuard&) = delete;
    ~LoggerGuard() { coney::logging::reset(); }
};

} // namespace

TEST_CASE("the registry holds the player catalog's entries that are built, each with its handler") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#release (the registry is compared with the catalog)
    Fixture fixture;
    std::set<std::string> registered;
    for (const auto* command : fixture.console.registry().commands()) {
        registered.insert(std::string(command->spec().name));
    }
    // The catalog's entries whose feature is built: none of the features is, so every entry that waits for one is out.
    std::set<std::string> expected;
    for (const auto& spec : playerCatalog()) {
        if (spec.available.empty()) {
            expected.insert(std::string(spec.name));
        }
    }
    CHECK(registered == expected);
    // The release set of the build issue is all there.
    for (const std::string_view name : {"help", "clear", "quit", "version", "where", "settings", "get", "set",
                                        "bugreport", "host", "connect", "disconnect"}) {
        INFO(name);
        CHECK(registered.contains(std::string(name)));
    }
    // Missions wait for Coney's mission select, and the panels for their issues.
    CHECK_FALSE(registered.contains("missions"));
    CHECK_FALSE(registered.contains("mission"));
}

TEST_CASE("once a feature is built, the entries that wait for it register with their handlers") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#catalog (available)
    Fixture fixture;
    fixture.console.registry().setBuiltFeatures({"mission-select"});
    CHECK(fixture.console.registry().resolve("missions").has_value());
    CHECK(fixture.console.registry().resolve("mission").has_value());
}

TEST_CASE("help lists the commands the build has by group and nothing else") {
    // Expected: docs/guides/studio/console.md#release (help prints exactly the player catalog's available names)
    Fixture fixture;
    const Strings lines = fixture.run("help");
    std::set<std::string> listed;
    for (const std::string& line : lines) {
        if (line.starts_with("  ")) {
            const std::size_t end = line.find(' ', 2);
            listed.insert(line.substr(2, end - 2));
        }
    }
    std::set<std::string> expected;
    for (const auto* command : fixture.console.registry().commands()) {
        expected.insert(std::string(command->spec().name));
    }
    CHECK(listed == expected);
    CHECK(listed.contains("quit"));
    CHECK_FALSE(listed.contains("noclip"));
    // The heading names the console, and each group is a line of its own.
    REQUIRE_FALSE(lines.empty());
    CHECK(lines.front().starts_with("Coney console"));
    CHECK(contains(lines, "Settings"));
    CHECK(contains(lines, "Network play"));
}

TEST_CASE("help of a command gives its usage, what it does and its arguments") {
    // Expected: docs/guides/studio/console.md#release-set (help [command])
    Fixture fixture;
    fixture.console.registry().setBuiltFeatures({"mission-select"});
    const Strings lines = fixture.run("help mission");
    REQUIRE(lines.size() >= 4);
    CHECK(lines[0] == "mission <mission> [checkpoint]");
    CHECK(lines[1].starts_with("  Starts a mission you have completed"));
    CHECK(contains(lines, "<mission>"));
    CHECK(contains(lines, "A completed mission's number in the story"));
}

TEST_CASE("a developer command is unknown in the release build and marked in a developer one") {
    // Expected: docs/guides/studio/console.md#access
    Fixture release;
    const Strings refused = release.run("noclip");
    REQUIRE(refused.size() == 1);
    CHECK(refused[0] == "unknown command \"noclip\"; help lists the commands");

    Fixture developer(Access::Developer);
    const coney::console::CommandSpec spec{
        .name = "noclip", .group = "Player", .access = Access::Developer, .summary = "Walks through walls."};
    REQUIRE(developer.console.registry().addBuiltin(
        spec, [](coney::console::CommandContext&) -> std::expected<void, std::string> { return {}; }));
    CHECK(contains(developer.run("help"), "noclip [dev]"));
    CHECK(contains(release.run("help"), "[dev]") == false);
}

TEST_CASE("help of a command that is not there is refused, naming the commands that are") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#terminal (an error says what to type instead)
    Fixture fixture;
    const Strings lines = fixture.run("help wher");
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].find("\"wher\" is not one of") != std::string::npos);
    CHECK(lines[0].find("where") != std::string::npos);
    CHECK(lines[0].find("usage: help [command]") != std::string::npos);
}

TEST_CASE("clear empties the terminal's output") {
    // Expected: docs/guides/studio/console.md#release-set (clear)
    Fixture fixture;
    fixture.console.terminal().print("an earlier line");
    CHECK(fixture.console.submit("clear", LineSource::Script) == Outcome::Ran);
    CHECK(fixture.console.terminal().output().empty());
}

TEST_CASE("version prints Coney's version, the build, the disc's region and the renderer") {
    // Expected: docs/guides/studio/console.md#release-set (version)
    Fixture fixture;
    fixture.session.build = coney::console::BuildInfo{
        .version = "0.1.0", .build = "release build", .discRegion = "NTSC-U", .renderer = "gl3"};
    CHECK(fixture.run("version") == Strings{"Coney 0.1.0 (release build)", "disc: NTSC-U; renderer: gl3"});
    // A run with no disc says so.
    fixture.session.build->discRegion.clear();
    CHECK(fixture.run("version") == Strings{"Coney 0.1.0 (release build)", "disc: none; renderer: gl3"});
}

TEST_CASE("version without a game session still names Coney") {
    // Expected: docs/guides/studio/console.md#release-set (version)
    Fixture fixture;
    const Strings lines = fixture.run("version");
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].starts_with("Coney "));
}

TEST_CASE("where reads the level, the checkpoint, the step and the play time as the run log stamps them") {
    // Expected: docs/guides/run-log.md#reading (the stamp: step, play time, checkpoint)
    const LoggerGuard logger;
    Fixture fixture;
    fixture.session.levelId = "level99";
    coney::logging::setStep(5400);
    coney::logging::setCheckpoint(2);
    CHECK(fixture.run("where") == Strings{"level level99, checkpoint 2, step 5400 (03:00.0)"});
    // Between levels there is no level and no checkpoint.
    fixture.session.levelId.reset();
    coney::logging::clearCheckpoint();
    CHECK(fixture.run("where") == Strings{"no level in play, step 5400 (03:00.0)"});
}

TEST_CASE("where needs a game in play") {
    // Expected: docs/guides/studio/console.md#catalog (needs)
    Fixture fixture;
    fixture.session.game = false;
    const Strings lines = fixture.run("where");
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].find("needs a game in play") != std::string::npos);
}

TEST_CASE("settings lists each setting with its value, aligned") {
    // Expected: docs/guides/studio/console.md#release-set (settings)
    Fixture fixture;
    CHECK(fixture.run("settings") == Strings{"fps_cap   60", "show_fps  off", "vsync     on"});
}

TEST_CASE("get reads one setting and refuses one that is not there") {
    // Expected: docs/guides/studio/console.md#release-set (get)
    Fixture fixture;
    CHECK(fixture.run("get vsync") == Strings{"vsync on"});
    const Strings refused = fixture.run("get fov");
    REQUIRE(refused.size() == 1);
    CHECK(refused[0].find("\"fov\" is not one of fps_cap, show_fps, vsync") != std::string::npos);
}

TEST_CASE("set changes a setting for this session, between steps, and says so") {
    // Expected: docs/guides/studio/console.md#release-set (set) and #terminal (a change waits for the boundary)
    Fixture fixture;
    fixture.console.terminal().clear();
    CHECK(fixture.console.submit("set fps_cap 30", LineSource::Script) == Outcome::Queued);
    CHECK(fixture.session.setCalls.empty());
    CHECK(fixture.console.runQueued() == 1);
    CHECK(fixture.session.setCalls == Strings{"fps_cap=30"});
    CHECK(fixture.console.terminal().output().back().text == "fps_cap 30 (for this session)");
    CHECK(fixture.run("get fps_cap") == Strings{"fps_cap 30"});
}

TEST_CASE("set reports a value the setting does not take, and changes nothing") {
    // Expected: docs/guides/studio/console.md#release-set (set)
    Fixture fixture;
    const Strings lines = fixture.run("set vsync bad");
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "set: \"bad\" is not a value of vsync");
    CHECK(fixture.run("get vsync") == Strings{"vsync on"});
}

TEST_CASE("bugreport writes the run log's ring through the host and prints its line") {
    // Expected: docs/guides/run-log.md#bug-reports
    Fixture fixture;
    fixture.session.bugReportLine = "bug report: the last 12 events written to coney.log";
    CHECK(fixture.run("bugreport") == Strings{"bug report: the last 12 events written to coney.log"});
    CHECK(fixture.session.bugReports == 1);
}

TEST_CASE("bugreport without a host log file dumps the ring through the logger") {
    // Expected: docs/guides/run-log.md#bug-reports
    const LoggerGuard logger;
    Strings dumped;
    coney::logging::setDumpSink([&dumped](std::string_view line) { dumped.emplace_back(line); });
    coney::logging::info(coney::logging::Category::Engine, "something happened");
    Fixture fixture;
    const Strings lines = fixture.run("bugreport");
    CHECK(contains(dumped, "something happened"));
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].starts_with("bug report: the last "));
}

TEST_CASE("quit asks the session to end through its normal exit") {
    // Expected: docs/guides/studio/console.md#release-set (quit)
    Fixture fixture;
    fixture.console.terminal().clear();
    // It is a change, so it waits for the step boundary.
    CHECK(fixture.console.submit("quit", LineSource::Script) == Outcome::Queued);
    CHECK(fixture.session.quits == 0);
    fixture.console.runQueued();
    CHECK(fixture.session.quits == 1);
    CHECK(fixture.console.terminal().output().back().text == "quitting");
}

TEST_CASE("quit says so when the run cannot be ended from the console") {
    // Expected: docs/guides/studio/console.md#release-set (quit)
    Fixture fixture;
    fixture.session.canQuit = false;
    const Strings lines = fixture.run("quit");
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].find("cannot") != std::string::npos);
}

TEST_CASE("a guest's quit leaves the host's game as the Player 2 quit does, and ends the program") {
    // Expected: docs/guides/studio/console.md#coop (quit on a guest)
    Fixture fixture;
    Strings order;
    fixture.session.order = &order;
    fixture.network.order = &order;
    fixture.network.current = SessionRole::Guest;
    fixture.run("quit");
    CHECK(order == Strings{"quit", "leave"});
}

TEST_CASE("a host's quit ends the session with the game") {
    // Expected: docs/guides/studio/console.md#coop (quit on the host)
    Fixture fixture;
    Strings order;
    fixture.session.order = &order;
    fixture.network.order = &order;
    fixture.network.current = SessionRole::Host;
    fixture.run("quit");
    CHECK(order == Strings{"quit", "leave"});
}

TEST_CASE("a quit the run cannot do leaves the network session alone") {
    // Expected: docs/guides/studio/console.md#coop (quit on a guest); Coney's own rule: nothing is left for a quit that
    // then fails
    Fixture fixture;
    fixture.session.canQuit = false;
    fixture.network.current = SessionRole::Guest;
    const Strings lines = fixture.run("quit");
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].find("cannot") != std::string::npos);
    CHECK(fixture.network.calls.empty());
    CHECK(fixture.network.current == SessionRole::Guest);
}

TEST_CASE("a quit says so when the session could not be left, and ends the game all the same") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#release-set (quit)
    Fixture fixture;
    fixture.network.current = SessionRole::Guest;
    fixture.network.leaveFails = true;
    const Strings lines = fixture.run("quit");
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "quitting");
    CHECK(lines[1] == "the network session could not be left cleanly");
    CHECK(fixture.session.quits == 1);
}

TEST_CASE("an offline quit does not touch network play") {
    // Expected: docs/guides/studio/console.md#coop (quit offline)
    Fixture fixture;
    fixture.run("quit");
    CHECK(fixture.network.calls.empty());
    CHECK(fixture.session.quits == 1);
}

TEST_CASE("host starts a network game, on a port when given one") {
    // Expected: docs/guides/studio/console.md#release-set (host)
    Fixture fixture;
    CHECK(fixture.run("host 27615") == Strings{"hosting on port 27615"});
    CHECK(fixture.network.calls == Strings{"host 27615"});
    Fixture other;
    CHECK(other.run("host") == Strings{"hosting on port 27615"});
    CHECK(other.network.calls == Strings{"host default"});
}

TEST_CASE("host refuses a port out of range and reports a port that cannot be opened") {
    // Expected: docs/guides/studio/console.md#release-set (host)
    Fixture fixture;
    const Strings range = fixture.run("host 70000");
    REQUIRE(range.size() == 1);
    CHECK(range[0].find("0 to 65535") != std::string::npos);
    CHECK(fixture.network.calls.empty());
    fixture.network.hostFails = true;
    const Strings failed = fixture.run("host 27615");
    REQUIRE(failed.size() == 1);
    CHECK(failed[0] == "host: the port cannot be opened");
}

TEST_CASE("connect joins a game by its address") {
    // Expected: docs/guides/studio/console.md#release-set (connect)
    Fixture fixture;
    CHECK(fixture.run("connect 192.168.1.20:27615") == Strings{"connecting to 192.168.1.20:27615"});
    CHECK(fixture.network.calls == Strings{"join 192.168.1.20:27615"});
}

TEST_CASE("host and connect refuse while this game is in a network session already") {
    // Expected: docs/guides/studio/console.md#coop (host and connect are for an offline game)
    Fixture fixture;
    fixture.network.current = SessionRole::Host;
    const Strings hosted = fixture.run("host");
    REQUIRE(hosted.size() == 1);
    CHECK(hosted[0].find("disconnect first") != std::string::npos);
    const Strings joined = fixture.run("connect 10.0.0.2");
    REQUIRE(joined.size() == 1);
    CHECK(joined[0].find("disconnect first") != std::string::npos);
    CHECK(fixture.network.calls.empty());
}

TEST_CASE("disconnect leaves the session, on a host and on a guest") {
    // Expected: docs/guides/studio/console.md#coop (disconnect)
    for (const SessionRole role : {SessionRole::Host, SessionRole::Guest}) {
        Fixture fixture;
        fixture.network.current = role;
        CHECK(fixture.run("disconnect") == Strings{"left the network session"});
        CHECK(fixture.network.current == SessionRole::Offline);
    }
}

TEST_CASE("disconnect says so when there is no session") {
    // Expected: docs/guides/studio/console.md#coop (disconnect)
    Fixture fixture;
    const Strings lines = fixture.run("disconnect");
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "disconnect: not in a network session");
}

TEST_CASE("a guest's console may read, change its own settings and leave, and refuses the host's commands") {
    // Expected: docs/guides/studio/console.md#coop (the Guest column)
    Fixture fixture;
    fixture.network.current = SessionRole::Guest;
    fixture.console.registry().setBuiltFeatures({"mission-select"});
    // Readings and the guest's own machine.
    CHECK(fixture.run("settings").size() == 3);
    CHECK(fixture.run("set vsync off") == Strings{"vsync off (for this session)"});
    CHECK(fixture.run("version").size() == 1);
    CHECK(fixture.run("missions") == Strings{"no completed missions"});
    // The host's commands, refused on the guest before anything is sent.
    for (const std::string_view line : {"host", "connect 10.0.0.2", "mission 1"}) {
        INFO(line);
        const Strings refused = fixture.run(line);
        REQUIRE(refused.size() == 1);
        CHECK(refused[0] == "only the host can do this in a network game");
    }
    CHECK(fixture.network.calls.empty());
    CHECK(fixture.session.started.empty());
    // A developer command is not registered at all.
    CHECK(contains(fixture.run("noclip"), "unknown command"));
    // Leaving is the guest's.
    CHECK(fixture.run("disconnect") == Strings{"left the network session"});
}

TEST_CASE("missions lists the completed missions with their checkpoints") {
    // Expected: docs/guides/studio/console.md#release-set (missions)
    Fixture fixture;
    fixture.console.registry().setBuiltFeatures({"mission-select"});
    fixture.session.missions = {{.mission = 1, .checkpoints = 3}, {.mission = 2, .checkpoints = 1}};
    CHECK(fixture.run("missions") == Strings{"1  checkpoints 1-3", "2  checkpoint 1"});
    fixture.session.missions.clear();
    CHECK(fixture.run("missions") == Strings{"no completed missions"});
}

TEST_CASE("missions needs an active profile") {
    // Expected: docs/guides/studio/console.md#catalog (needs)
    Fixture fixture;
    fixture.console.registry().setBuiltFeatures({"mission-select"});
    fixture.session.profile = false;
    const Strings lines = fixture.run("missions");
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].find("needs an active profile") != std::string::npos);
}

TEST_CASE("mission starts a completed mission at a checkpoint, the first by default") {
    // Expected: docs/guides/studio/console.md#release-set (mission), as the mission select starts one
    Fixture fixture;
    fixture.console.registry().setBuiltFeatures({"mission-select"});
    fixture.session.missions = {{.mission = 1, .checkpoints = 3}, {.mission = 3, .checkpoints = 2}};
    CHECK(fixture.run("mission 3 2") == Strings{"starting mission 3 at checkpoint 2"});
    CHECK(fixture.run("mission 1") == Strings{"starting mission 1 at checkpoint 1"});
    CHECK(fixture.session.started == std::vector<std::pair<int, int>>{{3, 2}, {1, 1}});
}

TEST_CASE("mission refuses a mission the profile has not completed and a checkpoint it does not have") {
    // Expected: docs/guides/studio/console.md#tests ("mission refuses a mission the profile has not completed")
    Fixture fixture;
    fixture.console.registry().setBuiltFeatures({"mission-select"});
    fixture.session.missions = {{.mission = 1, .checkpoints = 3}};
    const Strings notDone = fixture.run("mission 2");
    REQUIRE(notDone.size() == 1);
    CHECK(notDone[0] == "mission: mission 2 is not one the profile has completed");
    const Strings noCheckpoint = fixture.run("mission 1 4");
    REQUIRE(noCheckpoint.size() == 1);
    CHECK(noCheckpoint[0] == "mission: mission 1 has checkpoints 1 to 3, not 4");
    CHECK(fixture.session.started.empty());
}

TEST_CASE("mission needs a game and a profile") {
    // Expected: docs/guides/studio/console.md#catalog (needs)
    Fixture fixture;
    fixture.console.registry().setBuiltFeatures({"mission-select"});
    fixture.session.missions = {{.mission = 1, .checkpoints = 1}};
    fixture.session.profile = false;
    CHECK(contains(fixture.run("mission 1"), "needs an active profile"));
    fixture.session.profile = true;
    fixture.session.game = false;
    CHECK(contains(fixture.run("mission 1"), "needs a game in play"));
    CHECK(fixture.session.started.empty());
}

TEST_CASE("the commands that need a host say so when there is none") {
    // Expected: docs/guides/studio/console.md#boundary (each host may be absent)
    Console console({}, {});
    REQUIRE(registerPlayerCommands(console).has_value());
    for (const std::string_view line :
         {"quit", "bugreport", "host", "connect x", "disconnect", "settings", "version"}) {
        INFO(line);
        console.terminal().clear();
        (void)console.submit(line, LineSource::Script);
        console.runQueued();
        // It answered with a line of its own: the command is known, and says what it lacks.
        REQUIRE(console.terminal().output().size() >= 2);
        CHECK(console.terminal().output().back().text.find("unknown command") == std::string::npos);
    }
}
