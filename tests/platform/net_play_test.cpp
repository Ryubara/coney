// SPDX-License-Identifier: GPL-3.0-or-later
// The game's network play (docs/guides/multiplayer.md) over the loopback: hosting, joining by address, the figures the
// Network page shows, the simulated network, leaving, and the refusals, two games in one process with no socket.
#include "platform/net_play.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "debug/net_controls.h"
#include "net/loopback_transport.h"
#include "net/transport.h"

using coney::debug::NetRole;
using coney::debug::NetSimulation;
using coney::net::LoopbackNetwork;
using coney::net::LoopbackTransport;
using coney::platform::NetPlay;

namespace {

constexpr std::uint64_t kStepNs = 33'333'333;

// Network play whose transports are loopbacks on `network` named `name` (a literal, so the lambda copies a pointer).
NetPlay::TransportFactory loopback(LoopbackNetwork& network, const char* name) {
    return [&network, name] { return std::make_unique<LoopbackTransport>(network, name); };
}

// Two games, a host and a guest, stepped together as their main loops would.
struct TwoGames {
    LoopbackNetwork network;
    std::vector<std::string> printed;
    NetPlay host{loopback(network, "host"), [this](std::string_view line) { printed.emplace_back(line); }};
    NetPlay guest{loopback(network, "guest"), {}};

    // Runs `steps` steps on both games.
    void run(int steps) {
        for (int i = 0; i < steps; ++i) {
            host.advance(kStepNs);
            guest.advance(kStepNs);
            host.beforeStep();
            guest.beforeStep();
            host.afterStep();
            guest.afterStep();
        }
    }

    // Hosts on a free port as "Host" and joins it as "Guest".
    void connect() {
        REQUIRE(host.setPlayerName("Host"));
        REQUIRE(guest.setPlayerName("Guest"));
        host.setPort(0);
        REQUIRE(host.host());
        guest.setAddress("host:" + std::to_string(host.status().port));
        REQUIRE(guest.join());
        run(4);
    }
};

// Whether `log` has a line containing `text`.
bool logged(const std::vector<std::string>& log, std::string_view text) {
    return std::ranges::any_of(log, [text](const std::string& line) { return line.find(text) != std::string::npos; });
}

} // namespace

TEST_CASE("a game hosts and another joins it by address", "[net][net_play]") {
    TwoGames games;
    CHECK(games.host.status().role == NetRole::Offline);
    CHECK(games.host.status().state == "offline");
    games.connect();

    const auto host = games.host.status();
    CHECK(host.role == NetRole::Host);
    CHECK(host.port != 0);
    CHECK(host.state == "hosting on port " + std::to_string(host.port) + ", 1 player joined");
    REQUIRE(host.players.size() == 1);
    CHECK(host.players[0].name == "Guest");
    CHECK(host.players[0].slot == 1);
    CHECK(host.bytesReceived > 0);
    CHECK(logged(games.host.log(), "Guest joined as player 2"));
    // The host's log is printed too.
    CHECK(logged(games.printed, "net: hosting on port"));

    const auto guest = games.guest.status();
    CHECK(guest.role == NetRole::Client);
    CHECK(guest.state == "connected to Host as player 2");
    CHECK(logged(games.guest.log(), "connected to Host as player 2"));
    CHECK(guest.packetsSent > 0);
    CHECK(games.guest.summary() == "net: connected to Host as player 2; " + std::to_string(guest.bytesSent) +
                                       " B sent, " + std::to_string(guest.bytesReceived) + " B received\n");
}

TEST_CASE("the simulated network shows in the ping", "[net][net_play]") {
    TwoGames games;
    games.connect();
    games.guest.setSimulation(NetSimulation{.latencyMs = 100});
    CHECK(games.guest.simulation().latencyMs == 100);
    games.run(120);
    // 100 ms one way on what the guest sends, rounded up to whole steps of a thirtieth of a second, plus a step.
    CHECK(games.guest.status().roundTripNs >= 100'000'000);
    CHECK(games.guest.status().roundTripNs <= 200'000'000);
    CHECK(games.host.status().roundTripNs >= 100'000'000);
    games.guest.setSimulation(NetSimulation{});
}

TEST_CASE("leaving tells the host, and stopping tells the guest", "[net][net_play]") {
    TwoGames games;
    games.connect();
    games.guest.disconnect();
    CHECK(logged(games.guest.log(), "left the game"));
    games.run(2);
    CHECK(logged(games.host.log(), "Guest (player 2) left"));
    CHECK(games.host.status().players.empty());

    REQUIRE(games.guest.join());
    games.run(4);
    REQUIRE(games.host.status().players.size() == 1);
    games.host.disconnect();
    CHECK(logged(games.host.log(), "stopped hosting (1 player told)"));
    CHECK(games.host.status().role == NetRole::Offline);
    games.run(2);
    CHECK(games.guest.status().role == NetRole::Offline);
    CHECK(logged(games.guest.log(), "the server closed the connection (left)"));
}

TEST_CASE("bad names, addresses and ports are refused with a reason", "[net][net_play]") {
    LoopbackNetwork network;
    NetPlay first(loopback(network, "host"), {});
    NetPlay second(loopback(network, "host"), {});
    CHECK_FALSE(first.setPlayerName(""));
    CHECK_FALSE(first.setPlayerName(std::string(33, 'x')));
    CHECK(first.playerName() == "Player");
    CHECK(logged(first.log(), "is not a player name"));

    first.setAddress("host:notaport");
    CHECK_FALSE(first.join());
    CHECK(logged(first.log(), "has no valid port"));
    first.setAddress(":27615");
    CHECK_FALSE(first.join());

    // Two games cannot host on one port.
    first.setPort(30000);
    second.setPort(30000);
    REQUIRE(first.host());
    CHECK_FALSE(second.host());
    CHECK(logged(second.log(), "cannot host on port 30000"));
    CHECK(second.status().role == NetRole::Offline);
}
