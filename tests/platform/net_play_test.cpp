// SPDX-License-Identifier: GPL-3.0-or-later
// The game's network play (docs/guides/multiplayer.md) over the loopback: hosting, joining by address, the figures the
// Network page shows, the simulated network, leaving, the refusals, and the guest's pad on the host's port 2, two games
// in one process with no socket.
#include "platform/net_play.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/game_events.h"
#include "core/options.h"
#include "core/pad.h"
#include "core/pads.h"
#include "gamemodes/mission_complete_mode.h"
#include "gamemodes/sound_event_player.h"
#include "gamemodes/sound_events.h"
#include "gamemodes/start_up_flow.h"
#include "hud/hud.h"
#include "net/event_channel.h"
#include "net/loopback_transport.h"
#include "net/net_id.h"
#include "net/net_status.h"
#include "net/player_huds.h"
#include "net/radar_blips.h"
#include "net/snapshot.h"
#include "net/state_digest.h"
#include "net/transport.h"
#include "platform/net_game.h"
#include "platform/net_levels.h"
#include "scripting/sound_bindings.h"
#include "support/flow_run.h"
#include "warriors/level_table.h"

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
using coney::platform::LevelAt;
using coney::platform::LevelGame;
using coney::platform::MissionCounts;
using coney::platform::NetInput;
using coney::platform::NetLevels;
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

// A game's sound that writes down each sound started, moved and stopped, with the step it was asked in; each start
// takes the next handle from `next`.
class StepSound final : public coney::script::SoundHost {
  public:
    explicit StepSound(double first) : next(first) {}
    std::vector<std::string> calls;
    std::uint32_t step = 0; ///< the step the calls are written down for
    double next;

    double play3D(std::uint32_t sound, const std::array<float, 3>& position) override {
        calls.push_back(std::format("{}: start {:#x} at {}", step, sound, position[0]));
        return next++;
    }
    void moveSound(double /*handle*/, const std::array<float, 3>& position, float volume) override {
        calls.push_back(std::format("{}: move to {} at {}", step, position[0], volume));
    }
    void stopSound(double /*handle*/) override { calls.push_back(std::format("{}: stop", step)); }
    // What this test does not use.
    void configureMusicTrack(std::uint32_t /*track*/, float /*barMs*/, float /*volume*/) override {}
    void setInterfaceSound(int /*cue*/, std::uint32_t /*sound*/) override {}
    void allocateCharacterVoices(int /*count*/) override {}
    void setCommandSoundPercent(int /*voiceSet*/, std::uint32_t /*command*/, std::uint32_t /*percent*/) override {}
    void loadSoundBank(std::string_view /*name*/) override {}
    void setNonDuckableDuck(float /*factor*/) override {}
    void setPitchFactor(float /*factor*/) override {}
    void humanSound(const coney::script::HumanSoundCall& /*call*/) override {}
    void addAmbientSound(int /*index*/, std::uint32_t /*sound*/) override {}
    double addAmbientEmitter(const coney::script::AmbientEmitterCall& /*call*/) override { return 0.0; }
    void setAmbientEmitterPositions(std::string_view /*name*/,
                                    std::span<const std::array<float, 3>> /*points*/) override {}
    void playAmbientTrack(std::uint32_t /*sound*/) override {}
    void pauseSound(bool /*on*/) override {}
    double play2D(std::uint32_t /*sound*/) override { return 0.0; }
    void stopAmbientTrack() override {}
    void setAmbientTrackVolume(float /*volume*/) override {}
    void playMusic(std::uint32_t /*track*/, bool /*loop*/, std::string_view /*callback*/) override {}
    void stopMusic() override {}
    void setMusicVolume(float /*volume*/) override {}
    void setListener(int /*listener*/) override {}
    bool speak(const coney::script::SpeechCall& /*call*/, std::string_view /*callback*/,
               std::optional<double> /*callbackArg*/) override {
        return false;
    }
    void shutUp(double /*human*/, bool /*force*/) override {}
    std::optional<double> sayCommand(const coney::script::CommandCall& /*call*/,
                                     std::string_view /*callback*/) override {
        return std::nullopt;
    }
    void gameplayEntered() override {}
    void levelLoadStarted(int /*levelNumber*/) override {}
    void levelLoaded() override {}
    void gameplayLeft() override {}
};

// A game's levels as NetLevels sees them: a jump it takes starts that level's load at the next frame, which plays
// `loadSteps` frames later (never, while `loads` is off).
struct FakeLevels final : LevelGame {
    bool playing = false;
    std::uint64_t starts = 0;
    std::optional<LevelAt> at;
    MissionCounts missions;
    bool acceptsJumps = true;
    bool loads = true;
    int loadSteps = 5;
    std::vector<LevelAt> jumps;
    std::vector<std::string> moves; // each jump ("to <level>") and each leave ("leave"), in the order asked
    std::optional<LevelAt> pendingJump;
    int loadingLeft = -1;
    int leaves = 0; // the leaves the game took

    [[nodiscard]] bool inPlay() const override { return playing; }
    [[nodiscard]] std::uint64_t levelStarts() const override { return starts; }
    [[nodiscard]] std::optional<LevelAt> currentLevel() const override { return at; }
    std::function<MissionCounts()> counts; // when set, the mission ends instead of `missions` (a real game's)
    [[nodiscard]] MissionCounts missionEnds() const override { return counts ? counts() : missions; }
    bool jumpToLevel(std::string_view level, int checkpoint) override {
        if (!acceptsJumps) {
            return false;
        }
        jumps.push_back(LevelAt{.level = std::string(level), .checkpoint = checkpoint});
        moves.push_back("to " + std::string(level));
        pendingJump = jumps.back();
        return true;
    }
    // The game goes to the front end between two frames: the level and any jump asked for before are gone. Nothing
    // to leave before a level starts or after a leave.
    bool leaveLevel() override {
        if (!at) {
            return false;
        }
        moves.emplace_back("leave");
        ++leaves;
        pendingJump.reset();
        at.reset();
        playing = false;
        loadingLeft = -1;
        return true;
    }
    // The game starts loading `where` (gameplay's level load), out of play until it has loaded.
    void start(const LevelAt& where) {
        ++starts;
        at = where;
        playing = false;
        loadingLeft = loads ? loadSteps : -1;
    }
    // Between two frames: a jump asked for starts its load; a load under way counts down to play.
    void frame() {
        if (pendingJump) {
            const LevelAt where = *pendingJump;
            pendingJump.reset();
            start(where);
            return;
        }
        if (loadingLeft > 0) {
            --loadingLeft;
            playing = loadingLeft == 0;
        }
    }
};

// Two games with their levels, stepped as the main loop steps them: each frame the games' jumps and loads move on,
// then network play receives, NetLevels looks, the host's step gate asks NetLevels, and network play sends.
struct LevelGames {
    TwoGames games;
    FakeLevels hostGame;
    FakeLevels guestGame;
    NetLevels hostLevels{games.host};
    NetLevels guestLevels{games.guest};
    int held = 0; // the host's steps the gate held

    LevelGames() {
        hostGame.loadSteps = 3;
        hostLevels.setGame(&hostGame);
        guestLevels.setGame(&guestGame);
    }
    // Runs `steps` frames of one step each on both games.
    void run(int steps) {
        for (int i = 0; i < steps; ++i) {
            hostGame.frame();
            guestGame.frame();
            games.host.advance(kStepNs);
            games.guest.advance(kStepNs);
            games.host.beforeStep();
            games.guest.beforeStep();
            hostLevels.beforeStep();
            guestLevels.beforeStep();
            held += hostLevels.holdsStep() ? 1 : 0;
            games.host.afterStep();
            games.guest.afterStep();
        }
    }
    // The host's game in play in `level` at `checkpoint`, as after its own load.
    void hostPlays(const std::string& level, int checkpoint) {
        hostGame.start(LevelAt{.level = level, .checkpoint = checkpoint});
        hostGame.playing = true;
        hostGame.loadingLeft = -1;
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

// Expected: frontend.md#pad-record, confirmed (code)
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

TEST_CASE("a host sends its players' HUDs with its snapshots, and the guest holds each slot's as the host had it",
          "[net][net_play][hud]") {
    // Expected: unverified, #1447 (Coney's own snapshots; docs/guides/snapshots.md#hud-state)
    TwoGames games;
    CHECK(games.guest.arrivedHuds().empty()); // not joined
    games.connect();
    // Two players' HUDs, the second's money rising each step: the host's game hands them over before each send.
    std::array<coney::hud::PlayerHudState, 2> slots{};
    slots[0].panel.rage = 20;
    slots[1].panel.rage = 50;
    for (int i = 0; i < 20; ++i) {
        slots[1].panel.money = 10 + i;
        games.host.setGameHuds(coney::net::snapshotHuds(slots));
        games.run(1);
    }
    // The guest decodes each step's snapshot at the start of the next: it holds the step before last's HUDs.
    const coney::net::SnapshotHuds held = games.guest.arrivedHuds();
    REQUIRE(held.size() == 2);
    CHECK(coney::net::playerHudState(held.at(0)) == slots[0]);
    slots[1].panel.money = 10 + 18;
    CHECK(coney::net::playerHudState(held.at(1)) == slots[1]);
    CHECK(games.host.arrivedHuds().empty()); // a host draws its own
}

TEST_CASE("a host sends its HUD's standing state with its snapshots, and the guest holds it as the host had it",
          "[net][net_play][hud]") {
    // Expected: unverified, #1524 (Coney's own snapshots; docs/guides/snapshots.md#hud-standing)
    TwoGames games;
    CHECK(games.guest.arrivedHudStanding() == coney::hud::HudStanding{}); // not joined
    games.connect();
    // A counter panel counting up each step and player 1's prompt: the host's game hands them over before each send.
    coney::hud::HudStanding standing;
    standing.players[0].prompt = "Press X";
    standing.shared.counters[0].used = true;
    standing.shared.counters[0].label = "Tags";
    for (int i = 0; i < 20; ++i) {
        standing.shared.counters[0].value = static_cast<float>(i);
        const coney::net::SnapshotHudStanding parts = coney::net::snapshotHudStanding(standing);
        games.host.setGameHuds({}, parts.elements, parts.texts);
        games.run(1);
    }
    // The guest decodes each step's snapshot at the start of the next: it holds the step before last's state.
    standing.shared.counters[0].value = 18.0F;
    CHECK(games.guest.arrivedHudStanding() == standing);
    CHECK(games.host.arrivedHudStanding() == coney::hud::HudStanding{}); // a host draws its own
}

TEST_CASE("a host sends its radar's blips with its snapshots, and the guest holds them by slot",
          "[net][net_play][hud][radar]") {
    // Expected: unverified, #1537 (Coney's own snapshots; docs/guides/snapshots.md#hud-blips)
    TwoGames games;
    games.connect();
    std::map<double, coney::hud::RadarBlip> blips;
    blips.emplace(7.0, coney::hud::RadarBlip{.type = 10, .icon = 27, .place = coney::anim::Vec3{3.0F, 4.0F, 5.0F}});
    blips.emplace(9.0, coney::hud::RadarBlip{.type = 7, .icon = 365});
    coney::net::BlipSlots slots;
    const coney::net::NetId human{.slot = 4, .generation = 1};
    const auto handOver = [&] {
        const auto taken = coney::net::snapshotBlips(
            blips, slots, [&](double handle) { return handle == 9.0 ? human : coney::net::NetId{}; });
        games.host.setGameHuds({}, {}, {}, taken);
    };
    for (int i = 0; i < 10; ++i) {
        handOver();
        games.run(1);
    }
    coney::hud::HudStanding held = games.guest.arrivedHudStanding();
    REQUIRE(held.shared.blips.size() == 2);
    CHECK(held.shared.blips.at(0.0).type == 10);
    CHECK(held.shared.blips.at(0.0).icon == 27);
    REQUIRE(held.shared.blips.at(0.0).fixedPlace.has_value());
    CHECK(held.shared.blips.at(1.0).follows == coney::net::netIdNumber(human));
    CHECK(games.host.arrivedHudStanding().shared.blips.empty()); // a host draws its own
    // A blip the host's script deletes is gone from the guest's, and the blips alone changed nothing else.
    blips.erase(7.0);
    for (int i = 0; i < 10; ++i) {
        handOver();
        games.run(1);
    }
    held = games.guest.arrivedHudStanding();
    REQUIRE(held.shared.blips.size() == 1);
    CHECK(held.shared.blips.contains(1.0));
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

TEST_CASE("the game's humans are captured only while a session or a proof needs them", "[net][net_play]") {
    // Expected: unverified, #558 (Coney's own: offline play keeps no network ids)
    TwoGames games;
    auto opened = coney::platform::NetGame::open(coney::Options{});
    REQUIRE(opened.has_value());
    coney::platform::NetGame& game = **opened;
    CHECK_FALSE(game.humans().capturing());
    game.afterStep(games.host);
    CHECK_FALSE(game.humans().capturing());
    // Hosting: from the next step on, the host's snapshots need them.
    games.host.setPort(0);
    REQUIRE(games.host.host());
    game.afterStep(games.host);
    CHECK(game.humans().capturing());
    // The session over, nobody can name them any more.
    games.host.disconnect();
    games.run(NetPlay::kCloseSteps + 1);
    game.afterStep(games.host);
    CHECK_FALSE(game.humans().capturing());

    // A run that hosts from the start captures from its first step.
    coney::Options hosting;
    hosting.netHost = coney::NetEndpoint{};
    auto hostOpened = coney::platform::NetGame::open(hosting);
    REQUIRE(hostOpened.has_value());
    CHECK((**hostOpened).humans().capturing());
}

// Expected: unverified, #547 (Coney's own network play; the original has none)
TEST_CASE("a host's game events reach the joined guest once each, at the host's step", "[net][net_play][events]") {
    TwoGames games;
    // Offline the game's events are not kept.
    games.host.gameEvents().raise(coney::GameEvent{.kind = coney::GameEventKind::Sound2D, .id = 1});
    CHECK(games.host.gameEvents().size() == 0);
    games.connect();
    CHECK(games.host.gameEvents().recording());
    CHECK_FALSE(games.guest.gameEvents().recording());
    // Raised during the host's next step: a sound and an objective.
    const std::uint32_t step = games.host.steps();
    games.host.gameEvents().raise(coney::GameEvent{.kind = coney::GameEventKind::Sound2D, .id = 0x99});
    games.host.gameEvents().raise(
        coney::GameEvent{.kind = coney::GameEventKind::Objective, .id = 0, .extra = 8000, .text = "Go"});
    games.run(3);
    CHECK(games.guest.takeArrivedEvents(step - 1).empty());
    const std::vector<coney::net::ArrivedEvent> arrived = games.guest.takeArrivedEvents(step);
    REQUIRE(arrived.size() == 2);
    CHECK(arrived[0].step == step);
    CHECK(arrived[0].event.kind == coney::GameEventKind::Sound2D);
    CHECK(arrived[0].event.id == 0x99);
    CHECK(arrived[1].event.kind == coney::GameEventKind::Objective);
    CHECK(arrived[1].event.text == "Go");
    CHECK(games.guest.takeArrivedEvents(games.host.steps()).empty());
    // Leaving stops the recording.
    games.host.disconnect();
    CHECK_FALSE(games.host.gameEvents().recording());
    CHECK(games.host.takeArrivedEvents(100).empty());
}

// Expected: unverified, #1473 (Coney's own network play; the original has none)
TEST_CASE("a sound the host starts, moves and stops is started, moved and stopped once on the guest at those steps",
          "[net][net_play][events]") {
    TwoGames games;
    games.connect();
    // The host's game sound behind its events (as GameSession wires it), and the guest's own sound with the player
    // that applies the host's events to it. Their handles differ: the host's from 100, the guest's from 500.
    StepSound hostSound(100.0);
    coney::SoundEvents seam(&hostSound, &games.host.gameEvents());
    StepSound guestSound(500.0);
    coney::SoundEventPlayer player(&guestSound);
    // The guest applies the host's events step by step, at the host's step each happened in.
    std::uint32_t shown = 0;
    const auto guestCatchesUp = [&] {
        for (; shown < games.host.steps(); ++shown) {
            guestSound.step = shown;
            for (const coney::net::ArrivedEvent& arrived : games.guest.takeArrivedEvents(shown)) {
                player.apply(coney::net::toGameEvent(arrived.event));
            }
        }
    };
    guestCatchesUp();
    // A radio's track: started, followed where it is (nothing to send), halved for a scene, then stopped; one host
    // step for each call, with a step between.
    double track = 0.0;
    const std::array<std::function<void()>, 4> calls{
        [&] { track = seam.play3D(0x42, {3.0F, 0.0F, 0.0F}); },
        [&] { seam.moveSound(track, {3.0F, 0.0F, 0.0F}, 1.0F); },
        [&] { seam.moveSound(track, {5.0F, 0.0F, 0.0F}, 0.5F); },
        [&] { seam.stopSound(track); },
    };
    for (const std::function<void()>& call : calls) {
        hostSound.step = games.host.steps();
        call();
        games.run(1);
        games.run(1);
    }
    games.run(4);
    guestCatchesUp();

    CHECK(hostSound.calls.size() == 4);
    // The host's move to where it already plays sends nothing; everything else plays once on the guest, at the
    // step the host made it in, on the guest's own handle.
    const std::vector<std::string> expected{hostSound.calls[0], hostSound.calls[2], hostSound.calls[3]};
    CHECK(guestSound.calls == expected);
    CHECK(player.instances() == 0);
}

// Expected: unverified, #557 (Coney's own barrier; network-architecture.md#story: the server waits until every client
// has loaded before the first step)
TEST_CASE("a host's level reaches the guest, and the host holds its steps until the guest has loaded it",
          "[net][net_play][level]") {
    TwoGames games;
    games.connect();
    CHECK_FALSE(games.host.holdsStep());

    games.host.loadLevel("level2", 3);
    CHECK(games.host.holdsStep());
    games.run(2);
    const std::optional<coney::net::LoadLevel> order = games.guest.takeLevelOrder();
    REQUIRE(order.has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access): the REQUIRE above ends the test when empty, unseen by clang-tidy
    CHECK(order->level == "level2");
    CHECK(order->checkpoint == 3);
    CHECK_FALSE(games.guest.holdsStep()); // a guest holds nothing: it is not the server
    games.run(5);
    CHECK(games.host.holdsStep());

    games.guest.levelLoaded(order->load);
    // NOLINTEND(bugprone-unchecked-optional-access)
    games.run(2);
    CHECK_FALSE(games.host.holdsStep());
    const std::vector<std::string> log = games.host.log();
    CHECK(std::ranges::find(log, "Guest is loading level2 at checkpoint 3") != log.end());
    CHECK(std::ranges::find(log, "Guest has loaded level2") != log.end());
}

// Expected: unverified, #557 (Coney's own; network-architecture.md#authority: a player drops in mid-level)
TEST_CASE("a guest who joins a host already in a level is told the level on joining", "[net][net_play][level]") {
    TwoGames games;
    games.host.loadLevel("level99", 1); // offline: nobody to wait for
    CHECK_FALSE(games.host.holdsStep());
    games.connect();
    CHECK(games.host.holdsStep());
    const std::optional<coney::net::LoadLevel> order = games.guest.takeLevelOrder();
    REQUIRE(order.has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access): the REQUIRE above ends the test when empty, unseen by clang-tidy
    CHECK(order->level == "level99");
    CHECK(order->reason == coney::net::LoadReason::Join);
    games.guest.levelLoaded(order->load);
    // NOLINTEND(bugprone-unchecked-optional-access)
    games.run(2);
    CHECK_FALSE(games.host.holdsStep());

    // The host's mission ends; the guest hears how.
    games.host.endMission(coney::net::MissionOutcome::Passed);
    games.run(2);
    const std::vector<coney::net::MissionEnd> ends = games.guest.takeMissionEnds();
    REQUIRE(ends.size() == 1);
    CHECK(ends[0].outcome == coney::net::MissionOutcome::Passed);
}

// Expected: unverified, #557 (Coney's own; network-architecture.md#story: a movie waits for every client's end or skip)
TEST_CASE("a host's movie waits for the guest's end, and the guest's skip ends it", "[net][net_play][level][movie]") {
    TwoGames games;
    games.host.loadLevel("level1", 1);
    games.connect();
    // NOLINTBEGIN(bugprone-unchecked-optional-access): an empty order throws, which fails the test
    games.guest.levelLoaded(games.guest.takeLevelOrder().value().load);
    // NOLINTEND(bugprone-unchecked-optional-access)
    games.run(2);

    const std::uint32_t movie = games.host.startMovie();
    CHECK_FALSE(games.host.movieMayEnd()); // the host's own copy still plays
    games.host.endHostMovie();
    CHECK_FALSE(games.host.movieMayEnd());
    games.guest.movieDone(movie, true);
    games.run(2);
    CHECK(games.host.movieSkipped());
    CHECK(games.host.movieMayEnd());
}

// Expected: unverified, #1530 (Coney's own; network-architecture.md#story: the guest's game loads the host's level, and
// the server waits for every client before the level's first step)
TEST_CASE("the guest's game loads the host's new level, and the host holds its first frame of play until it has",
          "[net][net_play][level]") {
    LevelGames g;
    g.hostPlays("level1", 1);
    g.games.connect();
    // Joined from the front end, the guest is sent to level1; it loads.
    g.run(12);
    CHECK_FALSE(g.hostLevels.holdsStep());
    CHECK(g.guestGame.jumps == std::vector<LevelAt>{{.level = "level1", .checkpoint = 1}});

    // The host's game goes on to level2: its load is not held, its first frame of play is.
    g.hostGame.loadSteps = 2;
    g.guestGame.loadSteps = 20;
    g.hostGame.start(LevelAt{.level = "level2", .checkpoint = 1});
    g.held = 0;
    g.run(6);
    CHECK(g.guestGame.jumps.back() == LevelAt{.level = "level2", .checkpoint = 1});
    CHECK(g.hostGame.inPlay());
    CHECK(g.hostLevels.holdsStep());
    CHECK(g.held > 0);
    g.run(25);
    CHECK(g.guestGame.inPlay());
    CHECK_FALSE(g.hostLevels.holdsStep());
    CHECK(logged(g.games.host.log(), "Guest has loaded level2"));
    CHECK(logged(g.games.guest.log(), "in the host's level level2"));
}

// Expected: unverified, #1530 (Coney's own; docs/guides/multiplayer.md#levels: a checkpoint restart reloads the guest)
TEST_CASE("a checkpoint restart on the host reloads the guest's game at that checkpoint", "[net][net_play][level]") {
    LevelGames g;
    g.hostPlays("level2", 1);
    g.games.connect();
    g.run(12);
    REQUIRE(g.guestGame.jumps.size() == 1);
    g.hostGame.start(LevelAt{.level = "level2", .checkpoint = 2});
    g.run(15);
    CHECK(g.guestGame.jumps.size() == 2);
    CHECK(g.guestGame.jumps.back() == LevelAt{.level = "level2", .checkpoint = 2});
    CHECK(g.guestGame.inPlay());
    CHECK_FALSE(g.hostLevels.holdsStep());
}

// Expected: unverified, #1530 (Coney's own; network-architecture.md#authority: a player drops in mid-level)
TEST_CASE("a guest who joins mid-level is sent to the host's level, or answers at once when it plays it already",
          "[net][net_play][level]") {
    // From the front end: a jump, and the host waits at its frame of play until the guest plays.
    LevelGames fromMenus;
    fromMenus.hostPlays("level99", 1);
    fromMenus.run(1); // offline: network play keeps the level for the guests who join
    fromMenus.games.connect();
    fromMenus.run(2);
    CHECK(fromMenus.hostLevels.holdsStep());
    fromMenus.run(10);
    CHECK(fromMenus.guestGame.jumps == std::vector<LevelAt>{{.level = "level99", .checkpoint = 1}});
    CHECK_FALSE(fromMenus.hostLevels.holdsStep());

    // Already playing level99 at checkpoint 1 (started there itself): no second load.
    LevelGames already;
    already.hostPlays("level99", 1);
    already.guestGame.start(LevelAt{.level = "level99", .checkpoint = 1});
    already.guestGame.playing = true;
    already.guestGame.loadingLeft = -1;
    already.run(1);
    already.games.connect();
    already.run(3);
    CHECK(already.guestGame.jumps.empty());
    CHECK_FALSE(already.hostLevels.holdsStep());
    CHECK(logged(already.games.host.log(), "Guest has loaded level99"));
}

// Expected: unverified, #1530 (Coney's own; docs/guides/multiplayer.md#levels: a player still loading after the load
// time-out, 1,800 steps, is dropped and the host goes on)
TEST_CASE("the host holds for a guest that never loads only until the load time-out drops it",
          "[net][net_play][level]") {
    LevelGames g;
    g.guestGame.acceptsJumps = false;
    g.hostPlays("level1", 1);
    g.games.connect();
    g.run(100);
    CHECK(g.hostLevels.holdsStep());
    CHECK(logged(g.games.guest.log(), "cannot load the host's level level1 yet"));
    g.run(1800);
    CHECK_FALSE(g.hostLevels.holdsStep());
    CHECK(g.games.host.status().players.empty());
    CHECK(g.held >= 1700);
}

// Expected: unverified, #1530 (Coney's own; docs/guides/multiplayer.md#levels: MissionEnd)
TEST_CASE("the guest hears each of the host's mission ends, and a guest's own are not sent", "[net][net_play][level]") {
    LevelGames g;
    g.hostPlays("level1", 1);
    g.games.connect();
    g.run(12);
    g.hostGame.missions.passed = 1;
    g.run(3);
    g.hostGame.missions.failed = 1;
    g.guestGame.missions.passed = 4; // the guest's own game: nothing is sent
    g.run(3);
    const std::vector<std::string> log = g.games.guest.log();
    CHECK(std::ranges::count(log, std::string("the host's mission ended: passed")) == 1);
    CHECK(std::ranges::count(log, std::string("the host's mission ended: failed")) == 1);
}

// Expected: unverified, #1549 (Coney's own; docs/guides/multiplayer.md#levels: MissionEnd, a quit)
TEST_CASE(
    "a host's quit from the pause menu reaches the guest as a mission end, and the guest leaves the level with it",
    "[net][net_play][level]") {
    LevelGames g;
    g.hostPlays("level1", 1);
    g.games.connect();
    g.run(12);
    REQUIRE(g.guestGame.inPlay());
    REQUIRE(g.guestGame.leaves == 0);

    // The host's game counts the quit as it counts a mission-failed screen.
    g.hostGame.missions.quit = 1;
    g.run(3);
    CHECK(logged(g.games.guest.log(), "the host's mission ended: quit"));
    CHECK(g.guestGame.leaves == 1);
    CHECK_FALSE(g.guestGame.inPlay());

    // Each quit is told once, and the guest's own quits are not sent.
    g.run(10);
    CHECK(std::ranges::count(g.games.guest.log(), std::string("the host's mission ended: quit")) == 1);
    CHECK(g.guestGame.leaves == 1);
    g.guestGame.missions.quit = 3;
    g.run(3);
    CHECK(std::ranges::count(g.games.guest.log(), std::string("the host's mission ended: quit")) == 1);
}

namespace {

// A scripted host and guest: the host's mission ends are a real start-up flow's (coney::test::FlowRun, its pad
// played from `script`), over which `launch` opens a screen two frames in; both games step together for `frames`
// frames. Both games stay for the checks.
struct ScreenQuit {
    LevelGames g;
    coney::test::FlowRun host;

    ScreenQuit(std::string_view script, const std::function<void(coney::StartUpFlow&)>& launch, int frames)
        : host(script) {
        g.hostPlays("level99", 1);
        g.games.connect();
        g.run(12);
        REQUIRE(g.guestGame.inPlay());
        g.hostGame.counts = [this] {
            return MissionCounts{.passed = host.flow->missionComplete().launches(),
                                 .failed = host.flow->missionFailures(),
                                 .quit = host.flow->missionQuits()};
        };
        host.frames(2);
        launch(*host.flow);
        for (int frame = 0; frame < frames; ++frame) {
            host.frames(1);
            g.run(1);
        }
    }
    // How many times the guest heard of the host's mission ending with `outcome`.
    [[nodiscard]] std::ptrdiff_t heard(std::string_view outcome) const {
        return std::ranges::count(g.games.guest.log(), std::format("the host's mission ended: {}", outcome));
    }
};

} // namespace

// Expected: pause.md#the-mission-failed-screen, confirmed (code): MissionFailed_Toggle (0x00155408) leaves the level
// for `menu` on quit (+0x7a0), as PauseMenu_Toggle (0x00154f28) does on the pause menu's quit to the main menu
TEST_CASE("a host's Yes to Quit on the mission-failed screen reaches the guest as a quit, and the guest leaves with it",
          "[net][net_play][level]") {
    // Down, right to Quit, cross, left to Yes, cross.
    ScreenQuit run(
        "15 tap down\n20 tap right\n25 tap cross\n35 tap left\n40 tap cross\n",
        [](coney::StartUpFlow& flow) { flow.launchMissionFailed("Busted"); }, 60);
    CHECK(run.heard("failed") == 1);
    CHECK(run.heard("quit") == 1);
    CHECK(run.g.guestGame.leaves == 1);
    CHECK_FALSE(run.g.guestGame.inPlay());
}

// Expected: pause.md#the-mission-failed-screen, confirmed (code): in `level95` MissionFailed_Toggle (0x00155408)
// calls `runNextMission(0)` on To Hangout (+0x7ac), as PauseMenu_Toggle does on the quit to the hangout
TEST_CASE("a host's To Hangout on the mission-failed screen reaches the guest as a quit", "[net][net_play][level]") {
    ScreenQuit run(
        "15 tap cross\n",
        [](coney::StartUpFlow& flow) {
            coney::LevelRecord level = *flow.state().levels.at(1);
            level.number = 95;
            (void)flow.state().levels.set(level);
            flow.launchMissionFailed("Busted");
        },
        40);
    CHECK(run.heard("quit") == 1);
    CHECK(run.g.guestGame.leaves == 1);
}

// Expected: rumble.md#result-screen, confirmed (code): RumbleResult_Toggle (0x00155648) leaves the arena on Quit
// (+0x7ac) and on Rumble menu (+0x7a8), and reloads it on Replay (+0x7a4)
TEST_CASE("a host's Quit or Rumble menu on the Rumble result screen reaches the guest as a quit, and Replay does not",
          "[net][net_play][level][rumble]") {
    const auto launch = [](coney::StartUpFlow& flow) { flow.launchRumbleWin("Player 1", ""); };
    // The choices take input 255 steps after the screen opens; Quit and Rumble menu are behind more.
    ScreenQuit quit("275 tap right\n285 tap cross\n295 tap right\n305 tap cross\n", launch, 330);
    CHECK(quit.heard("quit") == 1);
    CHECK(quit.g.guestGame.leaves == 1);
    ScreenQuit menu("275 tap right\n285 tap cross\n295 tap cross\n", launch, 330);
    CHECK(menu.heard("quit") == 1);
    CHECK(menu.g.guestGame.leaves == 1);
    ScreenQuit replay("275 tap cross\n", launch, 330);
    CHECK(replay.heard("quit") == 0);
    CHECK(replay.g.guestGame.leaves == 0);
}

// Expected: pause.md#an-game-over, confirmed (code): ANGameOver_Toggle (0x001557f8) calls `runNextMission(0)` on Quit
// (+0x7a8), as PauseMenu_Toggle does on the quit to the hangout
TEST_CASE("a host's Quit on the Armies of the Night game-over screen reaches the guest as a quit",
          "[net][net_play][level][armies]") {
    // No credit: Start begins the end phase, whose choices take input after a 60-step fade.
    ScreenQuit run(
        "12 tap start\n90 tap right\n100 tap cross\n", [](coney::StartUpFlow& flow) { flow.launchArmiesGameOver(); },
        120);
    CHECK(run.heard("quit") == 1);
    CHECK(run.g.guestGame.leaves == 1);
}

// Expected: unverified, #1549 (Coney's own; docs/guides/multiplayer.md#levels: a quit to the hangout is a quit and then
// a load)
TEST_CASE("a quit that goes on to another level takes the guest out first, then to that level",
          "[net][net_play][level]") {
    LevelGames g;
    g.hostPlays("level1", 1);
    g.games.connect();
    g.run(12);
    REQUIRE(g.guestGame.moves == std::vector<std::string>{"to level1"});

    // Both in one step of the host: the quit, and the load of the hangout that follows it.
    g.hostGame.missions.quit = 1;
    g.hostGame.start(LevelAt{.level = "level80", .checkpoint = 1});
    g.run(15);
    CHECK(g.guestGame.moves == std::vector<std::string>{"to level1", "leave", "to level80"});
    CHECK(g.guestGame.inPlay());
    CHECK_FALSE(g.hostLevels.holdsStep());
}

// Expected: unverified, #1549 (Coney's own; docs/guides/multiplayer.md#levels: the load time-out drops a guest that
// never answers)
TEST_CASE("a guest that was still loading the host's level when the host quit is not dropped for it",
          "[net][net_play][level]") {
    LevelGames g;
    g.guestGame.loads = false; // its load never plays
    g.hostPlays("level1", 1);
    g.games.connect();
    g.run(12);
    REQUIRE(g.games.host.status().players.size() == 1);
    REQUIRE_FALSE(g.guestGame.inPlay());

    g.hostGame.missions.quit = 1;
    g.run(1900); // past the 1,800-step load time-out
    CHECK(g.guestGame.leaves == 1);
    CHECK(g.games.host.status().players.size() == 1);
}

// Expected: unverified, #1549 (Coney's own; docs/guides/multiplayer.md#levels: a quit ends an order of the level left,
// taken or not yet taken)
TEST_CASE("a quit drops the host's order of that level that the guest has not taken yet", "[net][net_play][level]") {
    LevelGames g;
    g.hostPlays("level1", 1);
    g.hostLevels.beforeStep(); // the host's level is told, so a guest that joins is ordered to it
    g.games.connect();         // network play only: the guest's NetLevels has not taken the order
    REQUIRE(g.guestGame.moves.empty());

    // The quit reaches the guest's client with the order still there, in the same step of the guest.
    g.hostGame.missions.quit = 1;
    g.hostLevels.beforeStep();
    g.games.run(3);
    REQUIRE(g.guestGame.moves.empty());
    g.run(15);
    CHECK(g.guestGame.moves.empty());
    CHECK_FALSE(g.guestGame.inPlay());
    CHECK(logged(g.games.guest.log(), "the host's mission ended: quit"));
    CHECK(g.games.host.status().players.size() == 1);
}

// Expected: unverified, #1530 (Coney's own; the LoadLevel name rule, docs/guides/multiplayer.md#levels)
TEST_CASE("a mod's mission level path goes to the guest as its file name, and a level with no such name is not sent",
          "[net][net_play][level]") {
    LevelGames g;
    g.games.connect();
    g.hostPlays("levels/alley.clevel", 1);
    g.run(3);
    REQUIRE_FALSE(g.guestGame.jumps.empty());
    CHECK(g.guestGame.jumps.front().level == "alley");

    g.hostPlays("levels/two words.clevel", 1);
    g.run(3);
    CHECK(g.guestGame.jumps.size() == 1);
    CHECK(logged(g.games.host.log(), "level levels/two words.clevel has no network name"));
}

// Expected: unverified, #1530 (Coney's own; network-architecture.md#story: a movie waits for every client's end or
// skip, and a client's skip ends the host's)
TEST_CASE("the host's movie waits for the guest's own copy to end, and the guest's skip ends the host's",
          "[net][net_play][level][movie]") {
    LevelGames g;
    g.hostPlays("level1", 1);
    g.games.connect();
    g.run(12);
    const coney::movies::MovieSync host = g.hostLevels.movieSync();
    const coney::movies::MovieSync guest = g.guestLevels.movieSync();

    // The host's movie starts in a step: its MovieStart event and the wait.
    g.games.host.gameEvents().raise(
        coney::GameEvent{.kind = coney::GameEventKind::MovieStart, .value = 1, .text = "L1_OUT"});
    host.started("L1_OUT");
    g.run(3);
    host.ended("L1_OUT", false);
    CHECK_FALSE(host.mayGoOn());
    // The guest's copy ends; the host goes on.
    guest.ended("L1_OUT", false);
    g.run(3);
    CHECK(host.mayGoOn());
    CHECK_FALSE(host.skippedElsewhere());

    // A second movie, which the guest skips before the host's ends: the host's copy is to end as a skip.
    g.games.host.gameEvents().raise(
        coney::GameEvent{.kind = coney::GameEventKind::MovieStart, .value = 1, .text = "L2_IN"});
    host.started("L2_IN");
    g.run(3);
    guest.ended("L2_IN", true);
    g.run(3);
    CHECK(host.skippedElsewhere());
    host.ended("L2_IN", true);
    CHECK(host.mayGoOn());
}

// Expected: unverified, #1548 (Coney's own; network-architecture.md#story: a movie waits for every client's end or
// skip, and the movie time-out is the fallback for a guest that never plays it, not a limit on one that does)
TEST_CASE("the host's movie waits for a guest whose own copy plays on past the movie time-out",
          "[net][net_play][level][movie]") {
    LevelGames g;
    g.hostPlays("level1", 1);
    g.games.connect();
    g.run(12);
    const coney::movies::MovieSync host = g.hostLevels.movieSync();
    const coney::movies::MovieSync guest = g.guestLevels.movieSync();
    const auto hostStarts = [&g, &host](const char* name) {
        g.games.host.gameEvents().raise(
            coney::GameEvent{.kind = coney::GameEventKind::MovieStart, .value = 1, .text = name});
        host.started(name);
    };

    // The host's movie ends first; the guest's own copy started a few steps after it and plays for 400 steps more,
    // longer than the 300-step movie time-out.
    hostStarts("L1_OUT");
    g.run(3);
    guest.started("L1_OUT");
    g.run(3);
    host.ended("L1_OUT", false);
    g.run(400);
    CHECK_FALSE(host.mayGoOn());
    guest.ended("L1_OUT", false);
    g.run(3);
    CHECK(host.mayGoOn());

    // A guest whose copy began before the host's movie started (its game is a step ahead) is waited for the same.
    guest.started("L2_IN");
    g.run(3);
    hostStarts("L2_IN");
    g.run(3);
    host.ended("L2_IN", false);
    g.run(400);
    CHECK_FALSE(host.mayGoOn());
    guest.ended("L2_IN", true);
    g.run(3);
    CHECK(host.mayGoOn());
    CHECK(host.skippedElsewhere());

    // A guest whose game never plays the movie holds the host only to the time-out.
    hostStarts("L3_IN");
    g.run(3);
    host.ended("L3_IN", false);
    g.run(290);
    CHECK_FALSE(host.mayGoOn());
    g.run(20);
    CHECK(host.mayGoOn());
}

// Expected: unverified, #1548 (Coney's own; docs/guides/multiplayer.md#levels: a quit drops the guest's movie of the
// level left, begun or ended, so the host's later movie is not answered from it)
TEST_CASE("a host's quit drops a guest's begun movie, so the host's next movie waits for it only to the time-out",
          "[net][net_play][level][movie]") {
    LevelGames g;
    g.hostPlays("level1", 1);
    g.games.connect();
    g.run(12);
    const coney::movies::MovieSync host = g.hostLevels.movieSync();
    const coney::movies::MovieSync guest = g.guestLevels.movieSync();

    // The guest's own copy of a movie begins before the host has started any: the begin waits for the host's start.
    guest.started("L1_OUT");
    g.run(3);
    // The host quits the level; the guest's game leaves it, and a movie of the level left is not answered.
    g.hostGame.missions.quit = 1;
    g.run(3);
    REQUIRE(g.guestGame.leaves == 1);

    // The host's own movie of the same name: no copy of the guest's plays, so only the time-out lets the host go on.
    g.games.host.gameEvents().raise(
        coney::GameEvent{.kind = coney::GameEventKind::MovieStart, .value = 1, .text = "L1_OUT"});
    host.started("L1_OUT");
    g.run(3);
    host.ended("L1_OUT", false);
    g.run(50);
    CHECK_FALSE(host.mayGoOn());
    g.run(260);
    CHECK(host.mayGoOn()); // not held for ever by the begin of a copy that was dropped
}

// Expected: unverified, #1530 (Coney's own; a guest's MovieDone names the host's movie by its MovieStart's step)
TEST_CASE("a guest knows the host's newest movie by the step its MovieStart came in", "[net][net_play][level][movie]") {
    LevelGames g;
    g.hostPlays("level1", 1);
    g.games.connect();
    g.run(12);
    CHECK_FALSE(g.games.guest.hostMovie().has_value());
    const std::uint32_t step = g.games.host.startMovie();
    g.games.host.gameEvents().raise(
        coney::GameEvent{.kind = coney::GameEventKind::MovieStart, .value = 1, .text = "L1_OUT"});
    g.run(3);
    const std::optional<coney::net::HostMovie> movie = g.games.guest.hostMovie();
    REQUIRE(movie.has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access): the REQUIRE above ends the test when empty, unseen by clang-tidy
    CHECK(movie->step == step);
    CHECK(movie->name == "L1_OUT");
    // NOLINTEND(bugprone-unchecked-optional-access)
    CHECK_FALSE(g.games.host.hostMovie().has_value()); // a host has no host's movie
}
