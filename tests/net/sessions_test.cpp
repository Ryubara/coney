// SPDX-License-Identifier: GPL-3.0-or-later
// The server and client sessions over the loopback (docs/guides/multiplayer.md#sessions): the handshake, slots,
// refusals, leaving, timeouts counted in steps, protocol errors, and the round trip and loss through a link
// conditioner, and the remote player's pad.
#include "net/net_client.h"
#include "net/net_server.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/pad.h"
#include "net/link_conditioner.h"
#include "net/loopback_transport.h"
#include "net/protocol.h"
#include "net/transport.h"
#include "support/net_fixtures.h"

using namespace coney::net;
using coney::net::test::drain;

namespace {

constexpr std::uint64_t kStepNs = 33'333'333; // one 30 Hz step
constexpr std::uint64_t kMs = 1'000'000;

// One client machine: its wire, a conditioner in front of it, and the session.
struct Guest {
    LoopbackTransport wire;
    LinkConditioner link;
    NetClient client;

    Guest(LoopbackNetwork& network, const std::string& name, const ClientConfig& config)
        : wire(network, name), link(wire, 99), client(link, config) {}
};

// A server and any number of clients on one loopback network, stepped together.
struct Match {
    LoopbackNetwork network;
    LoopbackTransport serverWire{network, "host"};
    LinkConditioner serverLink{serverWire, 7};
    NetServer server;
    std::vector<std::unique_ptr<Guest>> guests;

    explicit Match(ServerConfig config = serverConfig()) : server(serverLink, std::move(config)) {
        REQUIRE(server.start().has_value());
    }

    // The tests' server: any free port, short timeouts so a test runs few steps.
    static ServerConfig serverConfig() {
        ServerConfig config;
        config.port = 0;
        config.maxClients = 3;
        config.serverName = "Test Server";
        config.timeoutSteps = 60;
        config.handshakeTimeoutSteps = 30;
        return config;
    }

    // Adds a client called `name` and starts it connecting.
    NetClient& join(const std::string& name) {
        ClientConfig config;
        config.playerName = name;
        config.timeoutSteps = 60;
        config.connectTimeoutSteps = 30;
        guests.push_back(std::make_unique<Guest>(network, "guest" + std::to_string(guests.size()), config));
        REQUIRE(guests.back()->client.connect("host", server.port()).has_value());
        return guests.back()->client;
    }

    // Runs `steps` fixed steps of `stepNs` each on every machine, as the main loop does.
    void run(int steps, std::uint64_t stepNs = kStepNs) {
        for (int i = 0; i < steps; ++i) {
            server.advance(stepNs);
            for (auto& guest : guests) {
                guest->client.advance(stepNs);
            }
            server.receive();
            for (auto& guest : guests) {
                guest->client.receive();
            }
            server.send();
            for (auto& guest : guests) {
                guest->client.send();
            }
        }
    }
};

// A pad with `buttons` held and the left stick at `leftX`.
coney::PadSample padWith(std::uint16_t buttons, std::uint8_t leftX = coney::pad::kStickCentre) {
    coney::PadSample pad;
    pad.connected = true;
    pad.buttons = buttons;
    pad.sticks.at(2) = leftX;
    return pad;
}

// A connection that says Hello by hand and has its Welcome, for sending packets a NetClient never would.
PeerId rawJoin(Match& match, LoopbackTransport& raw) {
    const PeerId peer = raw.connect("host", match.server.port()).value();
    (void)drain(raw);
    REQUIRE(raw.send(peer, Delivery::ReliableOrdered, encode(Hello{.version = kProtocolVersion, .playerName = "Raw"}))
                .has_value());
    match.run(1);
    const auto events = drain(raw);
    REQUIRE_FALSE(events.empty());
    REQUIRE(std::holds_alternative<Welcome>(decode(events[0].data).value()));
    return peer;
}

} // namespace

TEST_CASE("a client joins through Hello and Welcome and gets the first slot", "[net][sessions]") {
    Match match;
    NetClient& client = match.join("Swan");
    CHECK(client.state() == ClientState::Connecting);
    match.run(3);
    REQUIRE(client.state() == ClientState::Connected);
    CHECK(client.playerSlot() == 1);
    CHECK(client.serverName() == "Test Server");
    CHECK(client.stepRateHz() == 30);
    CHECK(client.describe() == "connected to Test Server as player 2");

    const auto events = match.server.takeEvents();
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == ServerEvent::Kind::Joined);
    CHECK(events[0].slot == 1);
    CHECK(events[0].name == "Swan");
    CHECK(match.server.joinedCount() == 1);
    CHECK(match.server.takeEvents().empty());
}

TEST_CASE("slots fill in order and a client over the limit is told the server is full", "[net][sessions]") {
    Match match;
    NetClient& a = match.join("A");
    NetClient& b = match.join("B");
    NetClient& c = match.join("C");
    NetClient& d = match.join("D");
    match.run(4);
    CHECK(a.playerSlot() == 1);
    CHECK(b.playerSlot() == 2);
    CHECK(c.playerSlot() == 3);
    CHECK(d.state() == ClientState::Disconnected);
    CHECK(d.end() == ClientEnd::Rejected);
    CHECK(d.rejectReason() == RejectReason::ServerFull);
    CHECK(match.server.joinedCount() == 3);

    // A slot that frees up goes to the next joiner.
    b.disconnect();
    match.run(2);
    const auto left = match.server.takeEvents().back();
    CHECK(left.kind == ServerEvent::Kind::Left);
    CHECK(left.slot == 2);
    CHECK(left.reason == ByeReason::Quit);
    CHECK_FALSE(left.connectionLost);
    CHECK(b.end() == ClientEnd::Left);
    NetClient& e = match.join("E");
    match.run(3);
    CHECK(e.state() == ClientState::Connected);
    CHECK(e.playerSlot() == 2);
}

TEST_CASE("a bad player name is refused", "[net][sessions]") {
    Match match;
    NetClient& client = match.join("   ");
    match.run(3);
    CHECK(client.end() == ClientEnd::Rejected);
    CHECK(client.rejectReason() == RejectReason::BadName);
    CHECK(client.describe() == "refused: bad player name");
    CHECK(match.server.takeEvents().empty());
}

TEST_CASE("a Hello of another protocol version is refused with the server's version", "[net][sessions]") {
    Match match;
    LoopbackTransport raw(match.network, "old");
    const PeerId peer = raw.connect("host", match.server.port()).value();
    (void)drain(raw);
    REQUIRE(raw.send(peer, Delivery::ReliableOrdered, encode(Hello{.version = 999, .playerName = "Old"})).has_value());
    match.run(1);
    const auto events = drain(raw);
    REQUIRE(events.size() == 2);
    const auto reply = decode(events[0].data);
    REQUIRE(reply.has_value());
    const auto& reject = std::get<Reject>(*reply);
    CHECK(reject.reason == RejectReason::VersionMismatch);
    CHECK(reject.serverVersion == kProtocolVersion);
    CHECK(events[1].kind == TransportEventKind::Disconnected);
}

TEST_CASE("stopping the server tells every client", "[net][sessions]") {
    Match match;
    NetClient& client = match.join("Vermin");
    match.run(3);
    REQUIRE(client.state() == ClientState::Connected);
    match.server.stop();
    match.run(1);
    CHECK(client.end() == ClientEnd::ServerClosed);
    CHECK(client.byeReason() == ByeReason::Quit);
    CHECK_FALSE(match.server.running());
}

TEST_CASE("a client that hears nothing from the server times out after its steps", "[net][sessions]") {
    Match match;
    NetClient& client = match.join("Snow");
    match.run(3);
    REQUIRE(client.state() == ClientState::Connected);
    // The server's pings now all vanish; the client's still arrive, so only the client gives up.
    match.serverLink.setConditions(LinkConditions{.lossFraction = 1.0F});
    match.run(59);
    CHECK(client.state() == ClientState::Connected);
    match.run(2);
    CHECK(client.end() == ClientEnd::TimedOut);
    CHECK(client.describe() == "timed out");
}

TEST_CASE("the server drops a client it hears nothing from, after its steps", "[net][sessions]") {
    Match match;
    NetClient& client = match.join("Rembrandt");
    match.run(3);
    (void)match.server.takeEvents();
    match.guests[0]->link.setConditions(LinkConditions{.lossFraction = 1.0F});
    match.run(62);
    const auto events = match.server.takeEvents();
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == ServerEvent::Kind::Left);
    CHECK(events[0].reason == ByeReason::TimedOut);
    // The server's Bye is reliable, so the client hears why.
    CHECK(client.end() == ClientEnd::ServerClosed);
    CHECK(client.byeReason() == ByeReason::TimedOut);
}

TEST_CASE("a connection that never says Hello is closed after the handshake timeout", "[net][sessions]") {
    Match match;
    LoopbackTransport raw(match.network, "silent");
    (void)raw.connect("host", match.server.port()).value();
    match.run(30);
    CHECK(match.server.clients().size() == 1);
    match.run(2);
    CHECK(match.server.clients().empty());
    CHECK(match.server.takeEvents().empty()); // it never joined
}

TEST_CASE("a client gives up on a server that never answers its Hello", "[net][sessions]") {
    LoopbackNetwork network;
    LoopbackTransport mute(network, "host");
    REQUIRE(mute.listen(ListenConfig{.port = 0, .maxPeers = 1, .bindAddress = {}}).has_value());
    LoopbackTransport wire(network, "guest");
    ClientConfig config;
    config.connectTimeoutSteps = 10;
    NetClient client(wire, config);
    REQUIRE(client.connect("host", mute.boundPort()).has_value());
    for (int i = 0; i < 12; ++i) {
        client.advance(kStepNs);
        client.receive();
        client.send();
    }
    CHECK(client.end() == ClientEnd::TimedOut);
}

TEST_CASE("connecting where no server listens ends as unreachable", "[net][sessions]") {
    LoopbackNetwork network;
    LoopbackTransport wire(network, "guest");
    NetClient client(wire, ClientConfig{});
    REQUIRE(client.connect("nobody", kDefaultPort).has_value());
    client.receive();
    CHECK(client.end() == ClientEnd::Unreachable);
    CHECK(client.describe() == "could not reach the server");
    // A new session may start after an ended one.
    CHECK(client.connect("nobody", kDefaultPort).has_value());
}

TEST_CASE("a packet that does not decode closes the connection with a protocol-error Bye", "[net][sessions]") {
    Match match;
    LoopbackTransport raw(match.network, "noise");
    const PeerId peer = raw.connect("host", match.server.port()).value();
    (void)drain(raw);
    const std::vector<std::byte> junk{std::byte{0xee}, std::byte{0x01}};
    REQUIRE(raw.send(peer, Delivery::Unreliable, junk).has_value());
    match.run(1);
    const auto events = drain(raw);
    REQUIRE(events.size() == 2);
    CHECK(std::get<Bye>(decode(events[0].data).value()).reason == ByeReason::ProtocolError);
    CHECK(events[1].kind == TransportEventKind::Disconnected);
}

TEST_CASE("the round trip through added latency is measured on both ends", "[net][sessions]") {
    Match match;
    NetClient& client = match.join("Cochise");
    // 40 ms each way, stepped in 10 ms frames so the measurement is fine-grained.
    match.serverLink.setConditions(LinkConditions{.latencyMs = 40});
    match.guests[0]->link.setConditions(LinkConditions{.latencyMs = 40});
    match.run(200, 10 * kMs);
    REQUIRE(client.state() == ClientState::Connected);
    CHECK(client.quality().roundTripNs() >= 80 * kMs);
    CHECK(client.quality().roundTripNs() <= 100 * kMs);
    CHECK(client.quality().lossFraction() == 0.0F);
    const auto& seen = match.server.clients().at(0).quality;
    CHECK(seen.roundTripNs() >= 80 * kMs);
    CHECK(seen.roundTripNs() <= 100 * kMs);
}

TEST_CASE("the handshake survives a lossy link, and the loss shows in the figures", "[net][sessions]") {
    Match match;
    NetClient& client = match.join("Ajax");
    // Half of what the client sends unreliably is lost; reliable messages are only delayed.
    match.guests[0]->link.setConditions(LinkConditions{.latencyMs = 20, .jitterMs = 20, .lossFraction = 0.5F});
    match.run(400);
    REQUIRE(client.state() == ClientState::Connected);
    CHECK(client.quality().lossFraction() > 0.25F);
    CHECK(client.quality().lossFraction() < 0.75F);
    CHECK(client.quality().pingsSent() > 50);
}

TEST_CASE("a joined client's pad reaches the server every step, in the slot it holds", "[net][sessions][pad]") {
    Match match;
    CHECK_FALSE(match.server.remotePad(1).has_value()); // nobody in the slot
    NetClient& client = match.join("Cleon");
    match.run(3);
    REQUIRE(client.state() == ClientState::Connected);

    // A step's pad leaves at the end of that step, arrives at the start of the server's next and plays one step
    // after that (InputBufferConfig::delaySteps).
    client.setPad(padWith(coney::pad::kStart, 200));
    match.run(2);
    CHECK(match.server.remotePad(1).value_or(coney::PadSample{}).buttons == 0);
    match.run(1);
    const auto pad = match.server.remotePad(1);
    REQUIRE(pad.has_value());
    CHECK(pad.value_or(coney::PadSample{}).connected);
    CHECK(pad.value_or(coney::PadSample{}).buttons == coney::pad::kStart);
    CHECK(pad.value_or(coney::PadSample{}).sticks.at(2) == 200);
    CHECK_FALSE(match.server.remotePad(2).has_value());

    // The next step's pad replaces it.
    client.setPad(padWith(coney::pad::kCross));
    match.run(3);
    CHECK(match.server.remotePad(1).value_or(coney::PadSample{}).buttons == coney::pad::kCross);

    // Gone with the player.
    client.disconnect();
    match.run(1);
    CHECK_FALSE(match.server.remotePad(1).has_value());
}

TEST_CASE("a joined player's pad reads unplugged until their first pad input arrives", "[net][sessions][pad]") {
    Match match;
    LoopbackTransport raw(match.network, "raw");
    (void)rawJoin(match, raw);
    const auto pad = match.server.remotePad(1);
    REQUIRE(pad.has_value());
    CHECK_FALSE(pad.value_or(coney::PadSample{}).connected);
}

TEST_CASE("input commands that come late, out of order or twice each play once on their own step",
          "[net][sessions][pad]") {
    Match match;
    LoopbackTransport raw(match.network, "raw");
    const PeerId peer = rawJoin(match, raw);
    const auto sendCommands = [&](std::uint32_t newest, std::vector<coney::PadSample> pads) {
        REQUIRE(
            raw.send(peer, Delivery::Unreliable, encode(InputCommands{.newestStep = newest, .pads = std::move(pads)}))
                .has_value());
    };
    const auto played = [&] { return match.server.remotePad(1).value_or(coney::PadSample{}).buttons; };
    sendCommands(0, {padWith(coney::pad::kSquare)});
    match.run(1);
    CHECK_FALSE(match.server.remotePad(1).value_or(coney::PadSample{}).connected); // the delay filling
    sendCommands(2, {padWith(coney::pad::kTriangle)});                             // step 2 overtakes step 1
    sendCommands(1, {padWith(coney::pad::kCircle)});
    match.run(1);
    CHECK(played() == coney::pad::kSquare);
    sendCommands(2, {padWith(coney::pad::kCross)}); // step 2 again: the first copy stands
    sendCommands(3, {padWith(coney::pad::kR1)});
    match.run(1);
    CHECK(played() == coney::pad::kCircle);
    match.run(1);
    CHECK(played() == coney::pad::kTriangle);
    match.run(1);
    CHECK(played() == coney::pad::kR1);
    const InputBufferStats& stats = match.server.clients().at(0).input.stats();
    CHECK(stats.applied == 4);
    CHECK(stats.missed == 0);
}

TEST_CASE("a lost packet's steps come in the next one and play late, none missed", "[net][sessions][pad]") {
    Match match;
    NetClient& client = match.join("Rembrandt");
    match.run(3);
    REQUIRE(client.state() == ClientState::Connected);
    client.setPad(padWith(coney::pad::kL1));
    match.run(3);
    // Two steps of the client's packets lost; the next repeats their steps (ClientConfig::inputSteps is 4).
    match.guests[0]->link.setConditions(LinkConditions{.lossFraction = 1.0F});
    client.setPad(padWith(coney::pad::kR2));
    match.run(2);
    match.guests[0]->link.setConditions(LinkConditions{});
    match.run(6);
    const InputBufferStats& stats = match.server.clients().at(0).input.stats();
    CHECK(stats.missed == 0);
    CHECK(stats.skipped == 0);
    // The one step of delay covered the first lost step; the buffer waited one step for the second, and plays a step
    // later than before from then on.
    CHECK(stats.stalled == 1);
    CHECK(match.server.clients().at(0).input.lead() == 2);
    CHECK(match.server.remotePad(1).value_or(coney::PadSample{}).buttons == coney::pad::kR2);
}

TEST_CASE("at 5% loss with jitter every input command plays, in order, none lost", "[net][sessions][pad]") {
    Match match;
    NetClient& client = match.join("Cochise");
    match.run(3);
    REQUIRE(client.state() == ClientState::Connected);
    match.guests[0]->link.setConditions(LinkConditions{.latencyMs = 20, .jitterMs = 20, .lossFraction = 0.05F});

    // A different pad each step (buttons 1, 2, 3, ...), so each step's command is told apart from its neighbours.
    constexpr std::uint16_t kSteps = 300;
    std::vector<std::uint16_t> played;
    for (std::uint16_t i = 1; i <= kSteps; ++i) {
        client.setPad(padWith(i));
        match.run(1);
        played.push_back(match.server.remotePad(1).value_or(coney::PadSample{}).buttons);
    }
    // The server plays a few steps behind the client: run on until the last steps have played too.
    match.guests[0]->link.setConditions(LinkConditions{});
    for (int i = 0; i < 8; ++i) {
        match.run(1);
        played.push_back(match.server.remotePad(1).value_or(coney::PadSample{}).buttons);
    }

    // A stall holds the step before, so the server plays a step twice in a row; apart from that, every step the
    // client sent plays once, in order, with none skipped.
    std::vector<std::uint16_t> distinct;
    for (const std::uint16_t buttons : played) {
        if (buttons != 0 && (distinct.empty() || distinct.back() != buttons)) {
            distinct.push_back(buttons);
        }
    }
    REQUIRE(distinct.size() == kSteps);
    for (std::uint16_t i = 0; i < kSteps; ++i) {
        CHECK(distinct.at(i) == i + 1);
    }
    const InputBufferStats& stats = match.server.clients().at(0).input.stats();
    CHECK(stats.missed == 0);
    CHECK(stats.skipped == 0);
}

TEST_CASE("the server tells a client how early its input comes, and the client's clock follows",
          "[net][sessions][pad]") {
    ServerConfig config = Match::serverConfig();
    config.input.slackSteps = 10; // keep a long stall's delay rather than skipping it
    Match match(config);
    NetClient& client = match.join("Vermin");
    match.run(3);
    REQUIRE(client.state() == ClientState::Connected);
    CHECK(client.clockSpeedPercent() == 100);

    // All well: the lead is the delay the server wants, and the clock runs at its own speed.
    match.run(12);
    REQUIRE(client.inputTiming().has_value());
    CHECK(client.inputTiming().value_or(InputTiming{}).lead == 1);
    CHECK(client.inputTiming().value_or(InputTiming{}).target == 1);
    CHECK(client.clockSpeedPercent() == 100);

    // The client's packets stop coming: its commands are late, so it should step faster.
    match.guests[0]->link.setConditions(LinkConditions{.lossFraction = 1.0F});
    match.run(8);
    CHECK(client.inputTiming().value_or(InputTiming{}).lead <= 0);
    CHECK(client.clockSpeedPercent() == 102);

    // They come again, with the stall's delay kept: they now wait longer than needed, so it should step slower.
    match.guests[0]->link.setConditions(LinkConditions{});
    match.run(12);
    CHECK(client.inputTiming().value_or(InputTiming{}).lead > 2);
    CHECK(client.clockSpeedPercent() == 98);
}

TEST_CASE("a client sends 1 to 8 steps of input commands", "[net][sessions][pad]") {
    Match match;
    for (const std::uint32_t steps : {0U, 9U}) {
        LoopbackTransport wire(match.network, "bad" + std::to_string(steps));
        NetClient client(wire, ClientConfig{.inputSteps = steps});
        const auto connected = client.connect("host", match.server.port());
        REQUIRE_FALSE(connected.has_value());
        CHECK(connected.error().code == coney::ErrorCode::InvalidArgument);
    }
}

TEST_CASE("a pad input before the handshake is a protocol error", "[net][sessions][pad]") {
    Match match;
    LoopbackTransport raw(match.network, "eager");
    const PeerId peer = raw.connect("host", match.server.port()).value();
    (void)drain(raw);
    REQUIRE(
        raw.send(peer, Delivery::Unreliable, encode(InputCommands{.newestStep = 0, .pads = {padWith(0)}})).has_value());
    match.run(1);
    const auto events = drain(raw);
    REQUIRE(events.size() == 2);
    CHECK(std::get<Bye>(decode(events[0].data).value()).reason == ByeReason::ProtocolError);
    CHECK(match.server.clients().empty());
}

TEST_CASE("a pad that goes quiet is let go: held buttons released, sticks centred", "[net][sessions][pad]") {
    ServerConfig config = Match::serverConfig();
    config.input.holdSteps = 5;
    Match match(config);
    NetClient& client = match.join("Fox");
    match.run(3);
    client.setPad(padWith(coney::pad::kR1, 255));
    match.run(3);
    REQUIRE(match.server.remotePad(1).value_or(coney::PadSample{}).buttons == coney::pad::kR1);

    // Nothing the client sends from now on arrives (the link has not timed out yet); the pad already on its way
    // arrives in the first step.
    match.guests[0]->link.setConditions(LinkConditions{.lossFraction = 1.0F});
    match.run(1);
    match.run(5);
    CHECK(match.server.remotePad(1).value_or(coney::PadSample{}).buttons ==
          coney::pad::kR1); // held through a short gap
    match.run(1);
    const auto quiet = match.server.remotePad(1);
    REQUIRE(quiet.has_value());
    CHECK(quiet.value_or(coney::PadSample{}).connected);
    CHECK(quiet.value_or(coney::PadSample{}).buttons == 0);
    CHECK(quiet.value_or(coney::PadSample{}).sticks == coney::PadSample{}.sticks);
    CHECK(quiet.value_or(coney::PadSample{}).pressure == coney::PadSample{}.pressure);
}
