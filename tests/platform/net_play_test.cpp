// SPDX-License-Identifier: GPL-3.0-or-later
// The game's network play (docs/guides/multiplayer.md) over the loopback: hosting, joining by address, the figures the
// Network page shows, the simulated network, leaving, the refusals, and the guest's pad on the host's port 2, two games
// in one process with no socket.
#include "platform/net_play.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/pad.h"
#include "core/pads.h"
#include "net/loopback_transport.h"
#include "net/net_id.h"
#include "net/net_status.h"
#include "net/snapshot.h"
#include "net/state_digest.h"
#include "net/transport.h"

using coney::InputSource;
using coney::PadSample;
using coney::PortSamples;
using coney::net::DigestLine;
using coney::net::DigestMatch;
using coney::net::LoopbackNetwork;
using coney::net::LoopbackTransport;
using coney::net::NetId;
using coney::net::NetRole;
using coney::net::NetSimulation;
using coney::net::SnapshotHuman;
using coney::net::SnapshotHumans;
using coney::platform::NetInput;
using coney::platform::NetPlay;

namespace {

constexpr std::uint64_t kStepNs = 33'333'333;

// Network play whose transports are loopbacks on `network` named `name` (a literal, so the lambda copies a pointer).
NetPlay::TransportFactory loopback(LoopbackNetwork& network, const char* name) {
    return [&network, name] { return std::make_unique<LoopbackTransport>(network, name); };
}

// A game's own pads: whatever the test sets, every step.
struct HeldPads final : InputSource {
    PortSamples samples{};
    PortSamples sample(std::uint64_t /*frame*/) override { return samples; }
};

// A connected pad with `buttons` held and the left stick's x at `leftX`.
PadSample padWith(std::uint16_t buttons, std::uint8_t leftX = coney::pad::kStickCentre) {
    PadSample pad;
    pad.connected = true;
    pad.buttons = buttons;
    pad.sticks.at(2) = leftX;
    return pad;
}

// Two games, a host and a guest, stepped together as their main loops would.
struct TwoGames {
    LoopbackNetwork network;
    std::vector<std::string> printed;
    NetPlay host{loopback(network, "host"), [this](std::string_view line) { printed.emplace_back(line); }};
    NetPlay guest{loopback(network, "guest"), {}};
    // Each game's own pads, and the pads its game reads: its own through network play (NetInput).
    HeldPads hostPads;
    HeldPads guestPads;
    NetInput hostInput{&hostPads, host};
    NetInput guestInput{&guestPads, guest};
    PortSamples hostSees{};  // what the host's game read in the last step
    PortSamples guestSees{}; // what the guest's game read in the last step
    std::uint64_t frame = 0;

    // Runs `steps` steps on both games, in the main loop's order: receive, read the pads, the step, send.
    void run(int steps) {
        for (int i = 0; i < steps; ++i) {
            host.advance(kStepNs);
            guest.advance(kStepNs);
            host.beforeStep();
            guest.beforeStep();
            hostSees = hostInput.sample(frame);
            guestSees = guestInput.sample(frame);
            ++frame;
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

TEST_CASE("a guest who leaves over a slow link is still seen to leave, not to lose the connection", "[net][net_play]") {
    // The Bye waits in the simulated link; the transport must outlive the session until it has gone, or the host
    // reads the close alone as a lost connection (as ENet did when its transport was destroyed behind the Bye).
    TwoGames games;
    games.connect();
    games.guest.setSimulation(NetSimulation{.latencyMs = 100});
    games.run(10);
    games.guest.disconnect();
    CHECK(games.guest.closingTransports() == 1);
    games.run(8);
    CHECK(logged(games.host.log(), "Guest (player 2) left"));
    CHECK_FALSE(logged(games.host.log(), "lost the connection"));
    CHECK(games.guest.closingTransports() == 0);
}

TEST_CASE("a closing transport that never closes is dropped after its steps", "[net][net_play]") {
    TwoGames games;
    games.connect();
    // A second of latency holds the guest's Bye for 30 steps, longer than the close may take.
    games.guest.setSimulation(NetSimulation{.latencyMs = 1000});
    games.guest.disconnect();
    games.run(static_cast<int>(NetPlay::kCloseSteps) - 1);
    CHECK(games.guest.closingTransports() == 1);
    games.run(1);
    CHECK(games.guest.closingTransports() == 0);
}

TEST_CASE("hosting again on the port a closing host holds closes it at once", "[net][net_play]") {
    TwoGames games;
    games.host.setPort(30002);
    REQUIRE(games.host.host());
    games.guest.setAddress("host:30002");
    REQUIRE(games.guest.join());
    games.run(4);
    REQUIRE(games.host.status().players.size() == 1);
    games.host.setSimulation(NetSimulation{.latencyMs = 100});
    games.run(10);
    games.host.disconnect();
    REQUIRE(games.host.closingTransports() == 1);
    // The closing host's socket holds the port; Host again must free it rather than fail.
    REQUIRE(games.host.host());
    CHECK(games.host.closingTransports() == 0);
    CHECK(games.host.status().port == 30002);
}

TEST_CASE("shutting down waits, on the time it is given, for the Bye to reach the other game", "[net][net_play]") {
    TwoGames games;
    games.connect();
    games.host.setSimulation(NetSimulation{.latencyMs = 100});
    games.run(10);
    int waits = 0;
    // The wait the end of a run gives: here one step of the guest's game and a step's time, with no real clock.
    games.host.shutdown([&games, &waits] {
        ++waits;
        games.guest.advance(kStepNs);
        games.guest.beforeStep();
        games.guest.afterStep();
        return kStepNs;
    });
    CHECK(games.host.closingTransports() == 0);
    CHECK(waits > 0);
    CHECK(static_cast<std::uint64_t>(waits) * kStepNs <= NetPlay::kQuitWaitNs);
    games.run(2);
    CHECK(logged(games.guest.log(), "the server closed the connection (left)"));

    // With nothing closing, it returns without waiting.
    waits = 0;
    games.host.shutdown([&waits] {
        ++waits;
        return kStepNs;
    });
    CHECK(waits == 0);
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

TEST_CASE("a guest's pad is the host's port 2, as a second pad plugged into the host would be", "[net][net_play]") {
    TwoGames games;
    games.hostPads.samples[0] = padWith(coney::pad::kCross);
    games.guestPads.samples[0] = padWith(coney::pad::kStart, 230);
    games.connect();
    games.run(2); // one step to arrive, one in the host's input buffer

    // The host's own port 1 is untouched; port 2 is the guest's port 1.
    CHECK(games.hostSees[0].buttons == coney::pad::kCross);
    CHECK(games.hostSees[1].connected);
    CHECK(games.hostSees[1].buttons == coney::pad::kStart);
    CHECK(games.hostSees[1].sticks.at(2) == 230);
    // The guest's own game reads its own pads as before.
    CHECK(games.guestSees[0].buttons == coney::pad::kStart);
    CHECK_FALSE(games.guestSees[1].connected);

    // A press in the guest's step leaves at its end, arrives in the host's next step and plays in the one after
    // (the input buffer's one step of delay).
    games.guestPads.samples[0] = padWith(coney::pad::kCircle);
    games.run(2);
    CHECK(games.hostSees[1].buttons == coney::pad::kStart);
    games.run(1);
    CHECK(games.hostSees[1].buttons == coney::pad::kCircle);
}

TEST_CASE("the host's own port 2 plays until a guest joins and again once they leave", "[net][net_play]") {
    TwoGames games;
    games.hostPads.samples[1] = padWith(coney::pad::kTriangle);
    games.guestPads.samples[0] = padWith(coney::pad::kSquare);
    games.run(1);
    CHECK(games.hostSees[1].buttons == coney::pad::kTriangle); // offline
    games.connect();
    games.run(2);
    CHECK(games.hostSees[1].buttons == coney::pad::kSquare);
    CHECK(logged(games.host.log(), "Guest joined as player 2 on pad port 2"));
    games.guest.disconnect();
    games.run(2);
    CHECK(games.hostSees[1].buttons == coney::pad::kTriangle);
}

TEST_CASE("a guest with no pad plugged in leaves the host's port 2 unplugged", "[net][net_play]") {
    TwoGames games;
    games.hostPads.samples[1] = padWith(coney::pad::kTriangle);
    games.connect(); // guestPads are all unplugged
    games.run(1);
    CHECK_FALSE(games.hostSees[1].connected);
}

TEST_CASE("a host takes one remote player, as the console has two pad ports", "[net][net_play]") {
    // Expected: docs/guides/multiplayer.md#player-two (one remote player, on pad port 2)
    TwoGames games;
    games.connect();
    NetPlay third(loopback(games.network, "third"), {});
    third.setAddress("host:" + std::to_string(games.host.status().port));
    REQUIRE(third.join());
    for (int i = 0; i < 4; ++i) {
        third.advance(kStepNs);
        third.beforeStep();
        games.run(1);
        third.afterStep();
    }
    CHECK(third.status().role == NetRole::Offline);
    CHECK(third.status().state == "refused: server full");
    CHECK(games.host.status().players.size() == 1);
}

TEST_CASE("a host sends its game's humans, and the guest's digests match what each snapshot left it holding",
          "[net][net_play]") {
    // Expected: unverified, #558 (Coney's own digest; docs/guides/multiplayer.md#digest)
    TwoGames games;
    std::vector<DigestLine> sent;
    std::vector<DigestLine> decoded;
    games.host.setSnapshotDigests([&sent](const DigestLine& line) { sent.push_back(line); });
    games.guest.setSnapshotDigests([&decoded](const DigestLine& line) { decoded.push_back(line); });
    games.connect();

    // Two humans, one walking along x a little each step: the host's game hands them over before each send.
    SnapshotHuman still{.id = NetId{.slot = 0, .generation = 1}, .words = {}, .priority = 1};
    SnapshotHuman walker{.id = NetId{.slot = 1, .generation = 1}, .words = {}, .priority = 1};
    for (int i = 0; i < 30; ++i) {
        walker.words.at(0) = 1000 + static_cast<std::uint32_t>(i);
        const std::array humans{still, walker};
        games.host.setGameHumans(humans);
        games.run(1);
    }

    // Every step after the join sent a snapshot; the guest decoded them, a step behind, each with the host's digest.
    CHECK(sent.size() >= 30);
    REQUIRE(decoded.size() >= 25);
    const DigestMatch match = coney::net::matchDigests(sent, decoded);
    CHECK(match.matched == decoded.size());
    CHECK_FALSE(match.firstMismatch.has_value());
    CHECK_FALSE(match.firstUnknown.has_value());
    CHECK(decoded.back().humanCount == 2);
    const std::array last{still, walker};
    SnapshotHumans holding;
    for (const SnapshotHuman& human : last) {
        holding.emplace(human.id, human.words);
    }
    CHECK(sent.back().humans == coney::net::humansDigest(holding));
    // Steps count from the host's first step, so a snapshot names the step its humans are from.
    CHECK(sent.back().step == games.host.steps() - 1);
}

TEST_CASE("a host with nobody joined sends nothing and writes no snapshot digest", "[net][net_play]") {
    // Expected: unverified, #558 (Coney's own digest)
    TwoGames games;
    std::vector<DigestLine> sent;
    games.host.setSnapshotDigests([&sent](const DigestLine& line) { sent.push_back(line); });
    games.host.setPort(0);
    REQUIRE(games.host.host());
    games.run(5);
    CHECK(sent.empty());
    CHECK(games.host.steps() == 5);
}
