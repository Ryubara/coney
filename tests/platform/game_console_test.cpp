// SPDX-License-Identifier: GPL-3.0-or-later
// The game's console (docs/guides/console.md#in-the-game) over network play's loopback: the player commands' co-op rows
// of docs/guides/studio/console.md#coop, two games in one process with no socket.
#include "platform/game_console.h"

#include <algorithm>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "console/command_spec.h"
#include "console/console.h"
#include "core/options.h"
#include "gamemodes/game_mode_stack.h"
#include "net/loopback_transport.h"
#include "net/net_status.h"
#include "platform/net_play.h"

using coney::console::Access;
using coney::console::LineKind;
using coney::console::LineSource;
using coney::console::Outcome;
using coney::net::LoopbackNetwork;
using coney::net::LoopbackTransport;
using coney::net::NetRole;
using coney::platform::GameConsole;
using coney::platform::GameConsoleLinks;
using coney::platform::NetPlay;
using Strings = std::vector<std::string>;

namespace {

constexpr std::uint64_t kStepNs = 33'333'333;

// Network play whose transports are loopbacks on `network` named `name`.
NetPlay::TransportFactory loopback(LoopbackNetwork& network, const char* name) {
    return [&network, name] { return std::make_unique<LoopbackTransport>(network, name); };
}

// A test-mode run's options: no user folder is touched.
coney::Options testModeOptions() {
    coney::Options options;
    options.headless = true;
    return options;
}

// What a game's links say: a mission select with two completed missions, a frame cap setting and a volume setting.
struct GameState {
    int fpsCap = 60;
    double volume = 1.0;
    std::vector<std::pair<int, int>> started;
};

GameConsoleLinks linksOf(GameState& state) {
    GameConsoleLinks links;
    links.has = [](coney::console::Need need) {
        return need == coney::console::Need::Profile || need == coney::console::Need::Game;
    };
    links.settings.push_back(coney::platform::wholeSetting(
        "fps_cap", "Frames a second at most", 0, 1000, [&state] { return state.fpsCap; },
        [&state](int cap) { state.fpsCap = cap; }));
    links.settings.push_back(coney::platform::numberSetting(
        "volume", "The master volume", 0.0, 1.0, [&state] { return state.volume; },
        [&state](double volume) { state.volume = volume; }));
    links.completedMissions = [] {
        return std::vector<coney::console::CompletedMission>{{.mission = 1, .checkpoints = 3},
                                                             {.mission = 2, .checkpoints = 2}};
    };
    links.startMission = [&state](int mission, int checkpoint) -> std::expected<std::string, std::string> {
        state.started.emplace_back(mission, checkpoint);
        return "starting mission " + std::to_string(mission) + " at checkpoint " + std::to_string(checkpoint);
    };
    links.builtFeatures = {"mission-select"};
    return links;
}

// One game: network play, its console over the links, and the hooks the main loop would run.
struct Game {
    NetPlay net;
    GameState state;
    GameConsole console;
    coney::FrameHooks hooks;

    Game(LoopbackNetwork& network, const char* name)
        : net(loopback(network, name), {}), console(testModeOptions(), net, Access::Player) {
        console.setLinks(linksOf(state));
        hooks.beforeStep = [this] { net.beforeStep(); };
        console.addHooks(hooks);
    }

    // Enters `line` as the input script would and returns what the terminal then printed (the echo left out), after the
    // step boundary has run what the line queued.
    Strings run(std::string_view line, std::uint8_t slot = 0) {
        console.console().terminal().clear();
        (void)console.console().submit(line, LineSource::Script, slot);
        hooks.beforeStep();
        Strings printed;
        for (const auto& output : console.console().terminal().output()) {
            if (output.kind != LineKind::Echo) {
                printed.push_back(output.text);
            }
        }
        return printed;
    }
};

// Two games, a host and a guest, stepped together as their main loops would: receive, the queue, send.
struct TwoGames {
    LoopbackNetwork network;
    Game host{network, "host"};
    Game guest{network, "guest"};

    void step(int steps) {
        for (int i = 0; i < steps; ++i) {
            host.net.advance(kStepNs);
            guest.net.advance(kStepNs);
            host.net.beforeStep();
            guest.net.beforeStep();
            host.net.afterStep();
            guest.net.afterStep();
        }
    }

    // The host hosts and the guest joins, both from their consoles.
    void connect() {
        REQUIRE(host.net.setPlayerName("Host"));
        REQUIRE(guest.net.setPlayerName("Guest"));
        const Strings hosted = host.run("host 0");
        REQUIRE(hosted.size() == 1);
        REQUIRE(hosted[0].starts_with("hosting on port "));
        const Strings joined = guest.run("connect host:" + std::to_string(host.net.status().port));
        REQUIRE(joined.size() == 1);
        step(4);
        REQUIRE(host.net.status().role == NetRole::Host);
        REQUIRE(guest.net.status().role == NetRole::Client);
    }
};

// Whether any line of `log` contains `text`.
bool logged(const std::vector<std::string>& log, std::string_view text) {
    return std::ranges::any_of(log, [text](const std::string& line) { return line.find(text) != std::string::npos; });
}

} // namespace

TEST_CASE("a game hosts and another joins from their consoles, over the loopback") {
    // Expected: Coney's own rule, docs/guides/studio/console.md#release-set (host, connect)
    TwoGames games;
    games.connect();
    CHECK(games.host.net.status().players.size() == 1);
    CHECK(logged(games.host.net.log(), "Guest joined as player 2"));
    CHECK(games.guest.net.status().state == "connected to Host as player 2");
}

TEST_CASE("a guest's console refuses the host's commands before anything is sent") {
    // Expected: docs/guides/studio/console.md#coop (the Guest column)
    TwoGames games;
    games.connect();
    for (const std::string_view line : {"host", "connect 127.0.0.1", "mission 1"}) {
        INFO(line);
        const Strings refused = games.guest.run(line);
        REQUIRE(refused.size() == 1);
        CHECK(refused[0] == "only the host can do this in a network game");
    }
    CHECK(games.guest.state.started.empty());
    CHECK(games.guest.net.status().role == NetRole::Client);
    // The same lines on the host: the mission starts on the host's game (hosting again is refused as already hosting).
    CHECK(games.host.run("mission 1 2") == Strings{"starting mission 1 at checkpoint 2"});
    CHECK(games.host.state.started == std::vector<std::pair<int, int>>{{1, 2}});
    CHECK(games.host.run("host").size() == 1);
    CHECK(games.host.net.status().role == NetRole::Host);
}

TEST_CASE("a guest's console reads, and changes only the guest's own machine") {
    // Expected: docs/guides/studio/console.md#coop (get and set on a guest change the guest's own display and sound)
    TwoGames games;
    games.connect();
    CHECK(games.guest.run("set fps_cap 144") == Strings{"fps_cap 144 (for this session)"});
    CHECK(games.guest.state.fpsCap == 144);
    CHECK(games.host.state.fpsCap == 60);
    CHECK(games.guest.run("get fps_cap") == Strings{"fps_cap 144"});
    CHECK(games.guest.run("settings") == Strings{"fps_cap  144", "volume   1"});
    CHECK(games.guest.run("missions") == Strings{"1  checkpoints 1-3", "2  checkpoints 1-2"});
}

TEST_CASE("a guest's disconnect is the Player 2 quit, and the host plays on") {
    // Expected: docs/guides/studio/console.md#coop (disconnect on a guest)
    TwoGames games;
    games.connect();
    CHECK(games.guest.run("disconnect") == Strings{"left the network session"});
    games.step(2);
    CHECK(logged(games.host.net.log(), "Guest (player 2) left"));
    CHECK(games.host.net.status().role == NetRole::Host);
    CHECK(games.host.net.status().players.empty());
    CHECK(games.guest.net.status().role == NetRole::Offline);
    CHECK_FALSE(games.host.console.quitRequested());
}

TEST_CASE("a host's disconnect ends the session and drops the guest") {
    // Expected: docs/guides/studio/console.md#coop (disconnect on the host)
    TwoGames games;
    games.connect();
    CHECK(games.host.run("disconnect") == Strings{"left the network session"});
    CHECK(games.host.net.status().role == NetRole::Offline);
    games.step(2);
    CHECK(games.guest.net.status().role == NetRole::Offline);
    // There is nothing left to leave.
    const Strings again = games.host.run("disconnect");
    REQUIRE(again.size() == 1);
    CHECK(again[0] == "disconnect: not in a network session");
}

TEST_CASE("a guest's quit leaves the host's game as the Player 2 quit does, then ends the guest's program") {
    // Expected: docs/guides/studio/console.md#coop (quit on a guest)
    TwoGames games;
    games.connect();
    CHECK(games.guest.run("quit") == Strings{"quitting"});
    CHECK(games.guest.console.quitRequested());
    // The guest's frame loop ends at its next frame.
    CHECK_FALSE(games.guest.hooks.beginFrame());
    games.step(2);
    CHECK(logged(games.host.net.log(), "Guest (player 2) left"));
    // The host plays on.
    CHECK(games.host.net.status().role == NetRole::Host);
    CHECK_FALSE(games.host.console.quitRequested());
    CHECK(games.host.hooks.beginFrame());
}

TEST_CASE("the host's quit ends its game, and with it the session the guest was in") {
    // Expected: docs/guides/studio/console.md#coop (quit on the host)
    TwoGames games;
    games.connect();
    CHECK(games.host.run("quit") == Strings{"quitting"});
    CHECK_FALSE(games.host.hooks.beginFrame());
    games.step(2);
    CHECK(games.guest.net.status().role == NetRole::Offline);
    CHECK(logged(games.guest.net.log(), "the server closed the connection (left)"));
}

TEST_CASE("a game that is not in a session hosts again after a disconnect, and refuses to host twice") {
    // Expected: docs/guides/studio/console.md#coop (host and connect are for an offline game)
    LoopbackNetwork network;
    Game game(network, "host");
    CHECK(game.run("host 0")[0].starts_with("hosting on port "));
    const Strings twice = game.run("host 0");
    REQUIRE(twice.size() == 1);
    CHECK(twice[0] == "host: already in a network session; disconnect first");
    CHECK(game.run("disconnect") == Strings{"left the network session"});
    CHECK(game.run("host 0")[0].starts_with("hosting on port "));
}

TEST_CASE("connect says why a game cannot be joined") {
    // Expected: docs/guides/studio/console.md#release-set (connect)
    LoopbackNetwork network;
    Game game(network, "guest");
    const Strings refused = game.run("connect host:notaport");
    REQUIRE(refused.size() == 1);
    CHECK(refused[0].starts_with("connect: "));
    CHECK(refused[0].find("no valid port") != std::string::npos);
    CHECK(game.net.status().role == NetRole::Offline);
}

TEST_CASE("a setting changed at the console writes no file") {
    // Expected: docs/guides/studio/console.md#terminal (what the console writes: a setting lasts for the session)
    // The run happens in a folder of its own, so any file a command wrote shows.
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "coney-console-writes-nothing";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    const std::filesystem::path before = std::filesystem::current_path();
    std::filesystem::current_path(folder);
    {
        LoopbackNetwork network;
        Game game(network, "host");
        CHECK(game.run("set fps_cap 30") == Strings{"fps_cap 30 (for this session)"});
        CHECK(game.run("set volume 0.5") == Strings{"volume 0.5 (for this session)"});
        CHECK(game.state.fpsCap == 30);
        CHECK(game.state.volume == 0.5);
    }
    std::filesystem::current_path(before);
    CHECK(std::filesystem::is_empty(folder));
    // The user's own folders are left alone too: a test-mode console keeps no history.
    std::filesystem::remove_all(folder);
}

TEST_CASE("a typed line is ignored in test mode, and a scripted one runs") {
    // Expected: docs/guides/console.md#test-mode
    LoopbackNetwork network;
    Game game(network, "host");
    CHECK(game.console.console().submit("quit", LineSource::Typed) == Outcome::Ignored);
    CHECK_FALSE(game.console.quitRequested());
    CHECK(game.console.console().submit("quit", LineSource::Script) == Outcome::Queued);
}

TEST_CASE("the release console has no developer command and its mission commands wait for the mission select") {
    // Expected: docs/guides/studio/console.md#release (noclip is unknown; mission is left out until it is built)
    LoopbackNetwork network;
    Game game(network, "host");
    const Strings unknown = game.run("noclip");
    REQUIRE(unknown.size() == 1);
    CHECK(unknown[0] == "unknown command \"noclip\"; help lists the commands");
    // This game's links built the mission select, so its `mission` starts one.
    CHECK(game.run("mission 1 2") == Strings{"starting mission 1 at checkpoint 2"});
    // A game without those links has no `mission`.
    game.console.setLinks({});
    const Strings none = game.run("mission 1");
    REQUIRE(none.size() == 1);
    CHECK(none[0].starts_with("unknown command \"mission\""));
}
