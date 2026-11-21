// SPDX-License-Identifier: GPL-3.0-or-later
// Level loads, checkpoints and the loading barrier between a server and its clients over the loopback
// (docs/guides/multiplayer.md#levels): the messages and their pinned bytes, a level change, a checkpoint restart, a
// mid-level join and its full snapshot, a client that never loads dropped after the time-out, the end of a mission and
// the wait for every client's end or skip of a movie.
#include "net/level_barrier.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "net/loopback_transport.h"
#include "net/net_client.h"
#include "net/net_id.h"
#include "net/net_server.h"
#include "net/protocol.h"
#include "net/snapshot.h"
#include "net/transport.h"
#include "support/net_fixtures.h"

using coney::ErrorCode;
using namespace coney::net;
using coney::net::test::drain;

namespace {

constexpr std::uint64_t kStepNs = 33'333'333; // one 30 Hz step

// Bytes from a list of numbers, for hand-built packets.
std::vector<std::byte> bytes(std::initializer_list<int> values) {
    std::vector<std::byte> out;
    for (const int v : values) {
        out.push_back(static_cast<std::byte>(v));
    }
    return out;
}

// Encodes and decodes `message`, which must decode.
Message roundTrip(const Message& message) {
    auto decoded = decode(encode(message));
    REQUIRE(decoded.has_value());
    return *decoded;
}

// One client machine: its wire and its session; `stepping` false stands for a game busy loading, which runs no steps.
struct Guest {
    LoopbackTransport wire;
    NetClient client;
    bool stepping = true;

    Guest(LoopbackNetwork& network, const std::string& name, const ClientConfig& config)
        : wire(network, name), client(wire, config) {}
};

// A server and its clients on one loopback network, stepped together as the main loops do. Each step the server's
// game sends a snapshot of `humans` and the step's number counts on.
struct Session {
    LoopbackNetwork network;
    LoopbackTransport serverWire{network, "host"};
    NetServer server;
    std::vector<std::unique_ptr<Guest>> guests;
    std::vector<SnapshotHuman> humans;
    std::uint32_t step = 0;

    explicit Session(ServerConfig config = serverConfig()) : server(serverWire, std::move(config)) {
        REQUIRE(server.start().has_value());
    }

    // The tests' server: any free port, short time-outs so a test runs few steps.
    static ServerConfig serverConfig() {
        ServerConfig config;
        config.port = 0;
        config.maxClients = 3;
        config.serverName = "Coney Island";
        config.timeoutSteps = 60;
        config.handshakeTimeoutSteps = 30;
        config.loadTimeoutSteps = 90;
        config.movieTimeoutSteps = 45;
        return config;
    }

    // Adds a client called `name`, connects it and runs until it has joined.
    NetClient& join(const std::string& name) {
        ClientConfig config;
        config.playerName = name;
        config.timeoutSteps = 60;
        config.connectTimeoutSteps = 30;
        guests.push_back(std::make_unique<Guest>(network, "guest" + std::to_string(guests.size()), config));
        REQUIRE(guests.back()->client.connect("host", server.port()).has_value());
        run(3);
        REQUIRE(guests.back()->client.state() == ClientState::Connected);
        return guests.back()->client;
    }

    // Runs `steps` steps on every machine: receive, the server's snapshot, send.
    void run(int steps) {
        for (int i = 0; i < steps; ++i) {
            server.advance(kStepNs);
            server.receive();
            for (auto& guest : guests) {
                if (guest->stepping) {
                    guest->client.advance(kStepNs);
                    guest->client.receive();
                }
            }
            server.sendSnapshots(step, humans);
            server.send();
            for (auto& guest : guests) {
                if (guest->stepping) {
                    guest->client.send();
                }
            }
            ++step;
        }
    }
};

// `count` humans, ids 1 to `count`, each with its first field's word at `word` and the rest at 0.
std::vector<SnapshotHuman> crowd(std::size_t count, std::uint32_t word) {
    std::vector<SnapshotHuman> out;
    for (std::size_t i = 0; i < count; ++i) {
        SnapshotHuman human{.id = NetId{.slot = static_cast<std::uint16_t>(i + 1), .generation = 1}, .words = {}};
        human.words.at(0) = word;
        out.push_back(human);
    }
    return out;
}

} // namespace

// Expected: unverified, #557 (Coney's own protocol; the original has no network play). The checkpoint is 16 bits as the
// original's: characters.md#level-starts, confirmed (code)
TEST_CASE("the level messages round-trip and go reliably", "[net][level]") {
    const auto load = std::get<LoadLevel>(
        roundTrip(LoadLevel{.load = 3, .level = "level34", .checkpoint = 2, .reason = LoadReason::Restart}));
    CHECK(load.load == 3);
    CHECK(load.level == "level34");
    CHECK(load.checkpoint == 2);
    CHECK(load.reason == LoadReason::Restart);
    CHECK(std::get<LevelLoaded>(roundTrip(LevelLoaded{.load = 7})).load == 7);
    const auto end = std::get<MissionEnd>(roundTrip(MissionEnd{.load = 2, .outcome = MissionOutcome::Failed}));
    CHECK(end.load == 2);
    CHECK(end.outcome == MissionOutcome::Failed);
    const auto done = std::get<MovieDone>(roundTrip(MovieDone{.movieStep = 1200, .skipped = true}));
    CHECK(done.movieStep == 1200);
    CHECK(done.skipped);
    CHECK(std::get<Bye>(roundTrip(Bye{.reason = ByeReason::LoadTimedOut})).reason == ByeReason::LoadTimedOut);
    CHECK(describe(ByeReason::LoadTimedOut) == "did not load the level in time");

    CHECK(deliveryOf(LoadLevel{}) == Delivery::ReliableOrdered);
    CHECK(deliveryOf(LevelLoaded{}) == Delivery::ReliableOrdered);
    CHECK(deliveryOf(MissionEnd{}) == Delivery::ReliableOrdered);
    CHECK(deliveryOf(MovieDone{}) == Delivery::ReliableOrdered);
}

// Expected: unverified, #557 (Coney's own protocol; the original has no network play)
TEST_CASE("the level messages' bytes are pinned", "[net][level]") {
    // LoadLevel: id 13, the load (varint), the reason (8 bits), the checkpoint (16 bits), the level as a count and its
    // bytes.
    CHECK(encode(LoadLevel{.load = 1, .level = "l2", .checkpoint = 3, .reason = LoadReason::Change}) ==
          bytes({0x0d, 0x01, 0x01, 0x03, 0x00, 0x02, 'l', '2'}));
    // LevelLoaded: id 14, the load.
    CHECK(encode(LevelLoaded{.load = 300}) == bytes({0x0e, 0xac, 0x02}));
    // MissionEnd: id 15, the load, the outcome (8 bits).
    CHECK(encode(MissionEnd{.load = 2, .outcome = MissionOutcome::Passed}) == bytes({0x0f, 0x02, 0x01}));
    // MovieDone: id 16, the movie's step (varint), skipped (1 bit, padded).
    CHECK(encode(MovieDone{.movieStep = 5, .skipped = true}) == bytes({0x10, 0x05, 0x01}));
    // Bye's new reason.
    CHECK(encode(Bye{.reason = ByeReason::LoadTimedOut}) == bytes({0x04, 0x04}));
    // Version 8 added the level messages and the Bye for a load that timed out (#557); the version itself is pinned
    // in protocol_test.cpp.
    CHECK(kProtocolVersion >= 8);
}

// Expected: unverified, #557 (Coney's own protocol; decoding is strict, docs/guides/multiplayer.md#protocol). The
// checkpoint counts from 1: characters.md#level-starts, confirmed (code)
TEST_CASE("level messages that break their rules are refused", "[net][level]") {
    // Checkpoint 0, no level name, an unknown reason, load 0.
    CHECK(decode(bytes({0x0d, 0x01, 0x01, 0x00, 0x00, 0x02, 'l', '2'})).error().code == ErrorCode::Invalid);
    CHECK(decode(bytes({0x0d, 0x01, 0x01, 0x01, 0x00, 0x00})).error().code == ErrorCode::Invalid);
    CHECK(decode(bytes({0x0d, 0x01, 0x04, 0x01, 0x00, 0x02, 'l', '2'})).error().code == ErrorCode::Invalid);
    CHECK(decode(bytes({0x0d, 0x00, 0x01, 0x01, 0x00, 0x02, 'l', '2'})).error().code == ErrorCode::Invalid);
    CHECK(decode(bytes({0x0e, 0x00})).error().code == ErrorCode::Invalid);
    // A level name with a character no level name has: a slash, a dot (so no ".."), a space, a NUL or a control byte.
    CHECK(decode(bytes({0x0d, 0x01, 0x01, 0x01, 0x00, 0x04, 'a', '/', 'l', '2'})).error().code == ErrorCode::Invalid);
    CHECK(decode(bytes({0x0d, 0x01, 0x01, 0x01, 0x00, 0x02, '.', '.'})).error().code == ErrorCode::Invalid);
    CHECK(decode(bytes({0x0d, 0x01, 0x01, 0x01, 0x00, 0x02, 'l', ' '})).error().code == ErrorCode::Invalid);
    CHECK(decode(bytes({0x0d, 0x01, 0x01, 0x01, 0x00, 0x02, 'l', 0x00})).error().code == ErrorCode::Invalid);
    CHECK(decode(bytes({0x0d, 0x01, 0x01, 0x01, 0x00, 0x02, 'l', 0x0a})).error().code == ErrorCode::Invalid);
    // Letters, digits, '_' and '-' are a level name's characters.
    CHECK(std::get<LoadLevel>(roundTrip(LoadLevel{.level = "Level_1-x"})).level == "Level_1-x");
    // A mission end of no load, or an unknown outcome.
    CHECK(decode(bytes({0x0f, 0x00, 0x01})).error().code == ErrorCode::Invalid);
    CHECK(decode(bytes({0x0f, 0x01, 0x04})).error().code == ErrorCode::Invalid);
    // Set padding bits after a movie's skipped bit, and Bye reasons past the last.
    CHECK(decode(bytes({0x10, 0x05, 0x03})).error().code == ErrorCode::Invalid);
    CHECK(decode(bytes({0x04, 0x05})).error().code == ErrorCode::Invalid);
    // A short one.
    CHECK(decode(bytes({0x0d, 0x01, 0x01})).error().code == ErrorCode::Truncated);
}

// Expected: unverified, #557 (Coney's own barrier; network-architecture.md#story says the server waits)
TEST_CASE("a level change: the client loads the host's level and the server waits for it", "[net][level]") {
    Session session;
    NetClient& client = session.join("Swan");
    session.humans = crowd(3, 10);
    session.run(3);
    REQUIRE(client.snapshots().humans().size() == 3);
    CHECK(session.server.levelReady()); // no level yet: nothing to wait for

    const std::uint32_t load = session.server.loadLevel("level2", 1);
    CHECK(load == 1);
    CHECK_FALSE(session.server.levelReady());
    session.run(2);
    const std::optional<LoadLevel> order = client.takeLevelOrder();
    REQUIRE(order.has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access): the REQUIRE above ends the test when empty, unseen by clang-tidy
    CHECK(order->load == 1);
    CHECK(order->level == "level2");
    CHECK(order->checkpoint == 1);
    CHECK(order->reason == LoadReason::Change);
    // NOLINTEND(bugprone-unchecked-optional-access)
    CHECK_FALSE(client.takeLevelOrder().has_value()); // handed over once
    REQUIRE(client.loadingLevel().has_value());

    // While it loads it gets no snapshots: it is not in the level yet.
    const std::uint32_t before = client.snapshots().newest();
    session.humans = crowd(2, 20);
    session.run(10);
    CHECK(client.snapshots().newest() == before);
    CHECK_FALSE(session.server.levelReady());
    CHECK(session.server.loading(1));

    client.levelLoaded(load);
    CHECK_FALSE(client.loadingLevel().has_value());
    session.run(2);
    CHECK(session.server.levelReady());
    CHECK_FALSE(session.server.loading(1));
    session.run(2);
    CHECK(client.snapshots().humans() == SnapshotHumans{{NetId{.slot = 1, .generation = 1}, crowd(1, 20)[0].words},
                                                        {NetId{.slot = 2, .generation = 1}, crowd(2, 20)[1].words}});
}

// Expected: unverified, #557 (Coney's own barrier; network-architecture.md#story: both players restart)
TEST_CASE("a checkpoint restart reloads the client, and a reply to the older load does not count", "[net][level]") {
    Session session;
    NetClient& client = session.join("Cleon");
    const std::uint32_t first = session.server.loadLevel("level3", 1);
    session.run(2);
    REQUIRE(client.takeLevelOrder().has_value());
    client.levelLoaded(first);
    session.run(2);
    REQUIRE(session.server.levelReady());

    // The mission fails and the host restarts at checkpoint 4 of the same level.
    session.server.endMission(MissionOutcome::Failed);
    const std::uint32_t restart = session.server.loadLevel("level3", 4);
    CHECK(restart == 2);
    session.run(2);
    const std::vector<MissionEnd> ends = client.takeMissionEnds();
    REQUIRE(ends.size() == 1);
    CHECK(ends[0].load == first);
    CHECK(ends[0].outcome == MissionOutcome::Failed);
    const std::optional<LoadLevel> order = client.takeLevelOrder();
    REQUIRE(order.has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access): the REQUIRE above ends the test when empty, unseen by clang-tidy
    CHECK(order->load == restart);
    CHECK(order->level == "level3");
    CHECK(order->checkpoint == 4);
    CHECK(order->reason == LoadReason::Restart);
    // NOLINTEND(bugprone-unchecked-optional-access)

    // A reply to the first load, late, leaves the server waiting for the restart.
    client.levelLoaded(first);
    session.run(3);
    CHECK_FALSE(session.server.levelReady());
    CHECK(client.state() == ClientState::Connected);
    client.levelLoaded(restart);
    session.run(2);
    CHECK(session.server.levelReady());
    REQUIRE(session.server.level().has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access): the REQUIRE above ends the test when empty, unseen by clang-tidy
    CHECK(session.server.level()->checkpoint == 4);
    // NOLINTEND(bugprone-unchecked-optional-access)
}

// Expected: unverified, #557 (Coney's own; network-architecture.md#authority: a player drops in mid-level)
TEST_CASE("a client joining mid-level is told the level and gets a full snapshot once loaded", "[net][level]") {
    Session session;
    NetClient& first = session.join("Ajax");
    const std::uint32_t load = session.server.loadLevel("level5", 2);
    session.run(2);
    REQUIRE(first.takeLevelOrder().has_value());
    first.levelLoaded(load);
    session.humans = crowd(5, 30);
    session.run(5);
    REQUIRE(session.server.levelReady());
    REQUIRE(first.snapshots().humans().size() == 5);

    NetClient& late = session.join("Rembrandt");
    CHECK_FALSE(session.server.levelReady()); // the newcomer has the level to load
    const std::optional<LoadLevel> order = late.takeLevelOrder();
    REQUIRE(order.has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access): the REQUIRE above ends the test when empty, unseen by clang-tidy
    CHECK(order->load == load);
    CHECK(order->level == "level5");
    CHECK(order->checkpoint == 2);
    CHECK(order->reason == LoadReason::Join);
    // NOLINTEND(bugprone-unchecked-optional-access)
    session.run(5);
    CHECK(late.snapshots().newest() == 0);                     // nothing while loading
    CHECK(first.snapshots().newestStep() + 2 == session.step); // the others play on

    late.levelLoaded(load);
    session.run(1); // the server hears it
    CHECK(session.server.levelReady());
    session.run(1); // the client decodes the first snapshot
    REQUIRE(late.snapshots().newest() != 0);
    const std::uint32_t firstSequence = late.snapshots().newest();
    const SnapshotHumans* held = late.snapshots().holding(firstSequence);
    REQUIRE(held != nullptr);
    CHECK(held->size() == 5); // in full: every human
    CHECK(session.server.clients().at(1).snapshots.holding(firstSequence) != nullptr);
}

// Expected: unverified, #557 (Coney's own time-out; network-architecture.md#story: a barrier with a time-out that drops
// a client who never finishes)
TEST_CASE("a client that never loads is dropped after the load time-out, and the server plays on", "[net][level]") {
    Session session;
    NetClient& slow = session.join("Vermin");
    NetClient& quick = session.join("Snow");
    const std::uint32_t load = session.server.loadLevel("level6", 1);
    session.run(2);
    REQUIRE(slow.takeLevelOrder().has_value());
    REQUIRE(quick.takeLevelOrder().has_value());
    quick.levelLoaded(load);
    (void)session.server.takeEvents();

    // The slow game is busy loading and runs no steps: silent for longer than the 60-step keep-alive time-out, which
    // does not drop a loading player; the 90-step load time-out does.
    session.guests[0]->stepping = false;
    session.run(80);
    CHECK(session.server.joinedCount() == 2);
    CHECK(session.server.takeEvents().empty());
    CHECK_FALSE(session.server.levelReady());
    session.run(15);
    CHECK(session.server.joinedCount() == 1);
    const std::vector<ServerEvent> events = session.server.takeEvents();
    REQUIRE(events.size() == 1);
    CHECK(events[0].kind == ServerEvent::Kind::Left);
    CHECK(events[0].slot == 1);
    CHECK(events[0].reason == ByeReason::LoadTimedOut);
    CHECK(session.server.levelReady());

    // When its game comes back it reads why.
    session.guests[0]->stepping = true;
    session.run(1);
    CHECK(slow.state() == ClientState::Disconnected);
    CHECK(slow.end() == ClientEnd::ServerClosed);
    CHECK(slow.byeReason() == ByeReason::LoadTimedOut);
}

// Expected: unverified, #557 (Coney's own; a reply must name a load the server ordered)
TEST_CASE("a client that says it loaded a load never ordered is dropped for a protocol error", "[net][level]") {
    Session session;
    LoopbackTransport raw(session.network, "raw");
    const PeerId peer = raw.connect("host", session.server.port()).value();
    session.run(1);
    REQUIRE(raw.send(peer, Delivery::ReliableOrdered, encode(Hello{.version = kProtocolVersion, .playerName = "Raw"}))
                .has_value());
    session.run(1);
    (void)drain(raw);
    (void)session.server.takeEvents();
    REQUIRE(raw.send(peer, Delivery::ReliableOrdered, encode(LevelLoaded{.load = 1})).has_value());
    session.run(1);
    const std::vector<ServerEvent> events = session.server.takeEvents();
    REQUIRE(events.size() == 1);
    CHECK(events[0].reason == ByeReason::ProtocolError);

    // A level order goes only from a server to a client.
    Session other;
    NetClient& client = other.join("Cochise");
    (void)client;
    LoopbackTransport raw2(other.network, "raw2");
    const PeerId peer2 = raw2.connect("host", other.server.port()).value();
    other.run(1);
    REQUIRE(raw2.send(peer2, Delivery::ReliableOrdered, encode(Hello{.version = kProtocolVersion, .playerName = "Raw"}))
                .has_value());
    other.run(1);
    (void)other.server.takeEvents();
    REQUIRE(raw2.send(peer2, Delivery::ReliableOrdered,
                      encode(LoadLevel{.load = 1, .level = "level1", .checkpoint = 1, .reason = LoadReason::Change}))
                .has_value());
    other.run(1);
    const std::vector<ServerEvent> otherEvents = other.server.takeEvents();
    REQUIRE(otherEvents.size() == 1);
    CHECK(otherEvents[0].reason == ByeReason::ProtocolError);
}

// Expected: unverified, #557 (Coney's own; network-architecture.md#story: the host resumes when its movie has ended
// and every client has said it has, or after a time-out; any player's skip ends it for both)
TEST_CASE("a movie waits for every client's end, and a client's skip ends it", "[net][level][movie]") {
    Session session;
    NetClient& a = session.join("Swan");
    NetClient& b = session.join("Mercy");
    const std::uint32_t load = session.server.loadLevel("level1", 1);
    session.run(2);
    a.levelLoaded(load);
    b.levelLoaded(load);
    session.run(2);
    REQUIRE(session.server.levelReady());

    session.server.startMovie(400);
    CHECK_FALSE(session.server.movieMayEnd());
    session.server.endHostMovie(); // the host's own copy has ended
    CHECK_FALSE(session.server.movieMayEnd());
    a.movieDone(400, false);
    session.run(2);
    CHECK_FALSE(session.server.movieMayEnd());
    CHECK_FALSE(session.server.movieSkipped());
    b.movieDone(399, false); // another movie's: not this one
    session.run(2);
    CHECK_FALSE(session.server.movieMayEnd());
    b.movieDone(400, false);
    session.run(2);
    CHECK(session.server.movieMayEnd());

    // The next movie: b skips it before the host's copy ends, which tells the host to end its own.
    session.server.startMovie(900);
    b.movieDone(900, true);
    session.run(2);
    CHECK(session.server.movieSkipped());
    CHECK_FALSE(session.server.movieMayEnd()); // the host's own copy still plays
    session.server.endHostMovie();
    CHECK_FALSE(session.server.movieMayEnd()); // a has not finished its copy
    a.movieDone(900, false);
    session.run(2);
    CHECK(session.server.movieMayEnd());
}

// Expected: unverified, #557 (Coney's own time-out; network-architecture.md#story: or after a time-out)
TEST_CASE("a movie a client never ends lets the host go on after the movie time-out", "[net][level][movie]") {
    Session session;
    NetClient& client = session.join("Fox");
    const std::uint32_t load = session.server.loadLevel("level1", 1);
    session.run(2);
    client.levelLoaded(load);
    session.run(2);

    session.server.startMovie(60);
    session.run(100); // the host's movie plays on; the time-out counts only from its end
    CHECK_FALSE(session.server.movieMayEnd());
    session.server.endHostMovie();
    session.run(44);
    CHECK_FALSE(session.server.movieMayEnd());
    session.run(2);
    CHECK(session.server.movieMayEnd());
    CHECK(session.server.joinedCount() == 1); // waited for, not dropped
}

// Expected: unverified, #1548 (Coney's own protocol; the original has no network play): MovieBegan, id 17, is the
// movie's step (varint), sent reliably from a client, since version 10
TEST_CASE("MovieBegan's bytes are pinned, and it round-trips and goes reliably", "[net][level][movie]") {
    CHECK(encode(MovieBegan{.movieStep = 5}) == bytes({0x11, 0x05}));
    CHECK(encode(MovieBegan{.movieStep = 300}) == bytes({0x11, 0xac, 0x02}));
    CHECK(std::get<MovieBegan>(roundTrip(MovieBegan{.movieStep = 1200})).movieStep == 1200);
    CHECK(deliveryOf(MovieBegan{}) == Delivery::ReliableOrdered);
    // Short of its step, or with a byte after it, it does not decode.
    CHECK(decode(bytes({0x11})).error().code == ErrorCode::Truncated);
    CHECK_FALSE(decode(bytes({0x11, 0x05, 0x00})).has_value());
}

// Expected: unverified, #1548 (Coney's own; the host's movie waits for every guest that is watching its own copy until
// it ends, and gives up after the time-out only on a guest whose copy never began, network-architecture.md#story)
TEST_CASE("a client whose own copy of the movie plays is waited for past the movie time-out", "[net][level][movie]") {
    Session session; // the movie time-out is 45 steps
    NetClient& a = session.join("Swan");
    NetClient& b = session.join("Mercy");
    const std::uint32_t load = session.server.loadLevel("level1", 1);
    session.run(2);
    a.levelLoaded(load);
    b.levelLoaded(load);
    session.run(2);
    REQUIRE(session.server.levelReady());

    // a's own copy plays long after the host's ended; b's game never plays the movie.
    session.server.startMovie(400);
    a.movieBegan(400);
    session.run(2);
    session.server.endHostMovie();
    session.run(200);
    CHECK_FALSE(session.server.movieMayEnd()); // well past the time-out, a is still watching
    a.movieDone(400, false);
    session.run(2);
    CHECK(session.server.movieMayEnd()); // b never began: given up on after the time-out
    CHECK(session.server.joinedCount() == 2);

    // A copy that began, whose guest then goes silent and is dropped, no longer holds the host.
    session.server.startMovie(900);
    b.movieBegan(900);
    session.run(2);
    session.server.endHostMovie();
    session.run(50);
    CHECK_FALSE(session.server.movieMayEnd()); // b's copy plays; a's never began, and its time-out has passed
    session.guests[1]->stepping = false;       // b's machine goes silent: the keep-alive drops it
    session.run(70);
    CHECK(session.server.joinedCount() == 1);
    CHECK(session.server.movieMayEnd());
}

// Expected: unverified, #1548 (Coney's own; the barrier's bookkeeping for a copy that began)
TEST_CASE("a player whose copy began is waited for to its end, one whose copy never began only to the time-out",
          "[net][level][movie]") {
    LevelBarrier barrier(LevelBarrierConfig{.loadTimeoutSteps = 50, .movieTimeoutSteps = 3});
    barrier.playerJoined(1);
    barrier.playerJoined(2);
    barrier.load("level7", 1);
    REQUIRE(barrier.loaded(1, 1) == LoadReply::Accepted);
    REQUIRE(barrier.loaded(2, 1) == LoadReply::Accepted);

    barrier.startMovie(10);
    barrier.movieBegan(1, 9);  // another movie's begin: nothing changes
    barrier.movieBegan(3, 10); // a player who is not watching: nothing changes
    barrier.endHostMovie();
    for (int i = 0; i < 10; ++i) {
        CHECK(barrier.step().empty());
    }
    CHECK(barrier.movieMayEnd()); // neither began: both given up on at the time-out

    barrier.startMovie(20);
    barrier.movieBegan(1, 20);
    barrier.endHostMovie();
    for (int i = 0; i < 10; ++i) {
        CHECK(barrier.step().empty());
    }
    CHECK_FALSE(barrier.movieMayEnd()); // slot 1's copy plays: waited for, whatever the time-out
    barrier.movieDone(1, 20, false);
    CHECK(barrier.movieMayEnd()); // slot 2's never began

    // A player who leaves with its copy playing is no longer waited for.
    barrier.startMovie(30);
    barrier.movieBegan(2, 30);
    barrier.endHostMovie();
    for (int i = 0; i < 10; ++i) {
        CHECK(barrier.step().empty());
    }
    CHECK_FALSE(barrier.movieMayEnd());
    barrier.playerLeft(2);
    CHECK(barrier.movieMayEnd());
}

// Expected: unverified, #557 (Coney's own; the barrier's bookkeeping alone)
TEST_CASE("the barrier counts only joined players, and one that leaves is no longer waited for", "[net][level]") {
    LevelBarrier barrier(LevelBarrierConfig{.loadTimeoutSteps = 5, .movieTimeoutSteps = 3});
    CHECK(barrier.ready());
    barrier.playerJoined(1);
    CHECK(barrier.ready()); // no level: nothing to load
    const LoadLevel& order = barrier.load("level7", 2);
    CHECK(order.reason == LoadReason::Change);
    CHECK_FALSE(barrier.ready());
    barrier.playerJoined(2);
    CHECK(barrier.joinOrder().reason == LoadReason::Join);
    CHECK(barrier.loaded(1, 1) == LoadReply::Accepted);
    CHECK_FALSE(barrier.ready());
    barrier.playerLeft(2);
    CHECK(barrier.ready());
    CHECK(barrier.loaded(1, 2) == LoadReply::Invalid);
    CHECK(barrier.loaded(3, 1) == LoadReply::Invalid); // not joined

    // A restart of the same level, then the time-out.
    CHECK(barrier.load("level7", 3).reason == LoadReason::Restart);
    CHECK(barrier.loaded(1, 1) == LoadReply::Late);
    for (int i = 0; i < 5; ++i) {
        CHECK(barrier.step().empty());
    }
    CHECK(barrier.step() == std::vector<std::uint8_t>{1});
}

// Expected: unverified, #1530 (Coney's own name rule, docs/guides/multiplayer.md#levels; a story mod's mission level is
// a path in its package, docs/guides/studio/mission-mods.md#story-manifest)
TEST_CASE("a level the host's game names by a path goes on the wire as its file name, or not at all", "[net][level]") {
    CHECK(coney::net::levelNameOf("level99") == std::optional<std::string>("level99"));
    CHECK(coney::net::levelNameOf("levels/alley.clevel") == std::optional<std::string>("alley"));
    CHECK(coney::net::levelNameOf(R"(mods\yard-brawl\levels\payin.clevel)") == std::optional<std::string>("payin"));
    CHECK_FALSE(coney::net::levelNameOf("levels/two words.clevel").has_value());
    CHECK_FALSE(coney::net::levelNameOf("levels/").has_value());
    CHECK_FALSE(coney::net::levelNameOf("").has_value());
    CHECK_FALSE(coney::net::levelNameOf(std::string(33, 'a')).has_value());
}
