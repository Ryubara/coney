// SPDX-License-Identifier: GPL-3.0-or-later
// The client game (docs/guides/client-world.md#client-game): a guest that runs no game of its own, following the
// host's levels and drawing the host's humans from the snapshots it decodes, two games in one process on the loopback
// in test mode's fixed steps, with no display (a fake scene stands for the drawing).
#include "platform/client_game.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "core/game_events.h"
#include "gamemodes/game_mode.h"
#include "gamemodes/game_mode_stack.h"
#include "hud/hud.h"
#include "hud/hud_owner.h"
#include "net/client_world.h"
#include "net/jitter_buffer.h"
#include "net/loopback_transport.h"
#include "net/net_client.h"
#include "net/net_id.h"
#include "net/player_huds.h"
#include "net/protocol.h"
#include "net/snapshot.h"
#include "platform/net_levels.h"
#include "platform/net_play.h"
#include "scripting/sound_bindings.h"

using coney::FrameTime;
using coney::GameModeStack;
using coney::RenderTime;
using coney::net::ClientWorld;
using coney::net::HumanState;
using coney::net::LoopbackNetwork;
using coney::net::LoopbackTransport;
using coney::net::NetId;
using coney::net::SnapshotHuman;
using coney::platform::ClientGame;
using coney::platform::ClientPresentation;
using coney::platform::ClientScene;
using coney::platform::LevelAt;
using coney::platform::NetLevels;
using coney::platform::NetPlay;

namespace {

constexpr std::uint64_t kStepNs = 33'333'333;

// The guest's own sound: it writes down each sound started, stopped and each track played, in the order asked.
class RecordingSound final : public coney::script::SoundHost {
  public:
    std::vector<std::string> calls;
    double next = 21.0; ///< the next handle a sound started takes

    double play2D(std::uint32_t sound) override {
        calls.push_back(std::format("2d {:#x}", sound));
        return next++;
    }
    void stopSound(double handle) override { calls.push_back(std::format("stop {}", handle)); }
    void playMusic(std::uint32_t track, bool /*loop*/, std::string_view /*callback*/) override {
        calls.push_back(std::format("music {:#x}", track));
    }
    // What these tests do not use.
    void configureMusicTrack(std::uint32_t /*track*/, float /*barMs*/, float /*volume*/) override {}
    void setInterfaceSound(int /*cue*/, std::uint32_t /*sound*/) override {}
    void allocateCharacterVoices(int /*count*/) override {}
    void setCommandSoundPercent(int /*voiceSet*/, std::uint32_t /*command*/, std::uint32_t /*percent*/) override {}
    void loadSoundBank(std::string_view /*name*/) override {}
    void setNonDuckableDuck(float /*factor*/) override {}
    void setPitchFactor(float /*factor*/) override {}
    void addAmbientSound(int /*index*/, std::uint32_t /*sound*/) override {}
    double addAmbientEmitter(const coney::script::AmbientEmitterCall& /*call*/) override { return 0.0; }
    void setAmbientEmitterPositions(std::string_view /*name*/,
                                    std::span<const std::array<float, 3>> /*points*/) override {}
    void playAmbientTrack(std::uint32_t /*sound*/) override {}
    void pauseSound(bool /*on*/) override {}
    void stopAmbientTrack() override {}
    void setAmbientTrackVolume(float /*volume*/) override {}
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

// What the client game drew: a scene that counts its steps and frames and remembers the humans it was last given.
struct FakeScene final : ClientScene {
    LevelAt level;
    int* steps;
    int* frames;
    FakeScene(LevelAt at, int* stepCount, int* frameCount)
        : level(std::move(at)), steps(stepCount), frames(frameCount) {}
    void step(const ClientWorld& /*world*/, const FrameTime& /*frame*/) override { ++*steps; }
    void render(const ClientWorld& /*world*/, const RenderTime& /*time*/) override { ++*frames; }
};

// The host's three humans at host step `step`: one standing, one walking along x, one turning, so every step moves.
std::vector<SnapshotHuman> hostHumans(std::uint32_t step) {
    std::vector<SnapshotHuman> humans;
    for (std::uint16_t slot = 0; slot < 3; ++slot) {
        HumanState human;
        // NOLINTNEXTLINE(bugprone-narrowing-conversions): slot is 0 to 2, exact as a float
        human.position = coney::anim::Vec3{10.0F * slot, 5.0F, 0.0F};
        human.health = 150;
        human.healthMax = 150;
        human.grounded = true;
        human.layers[0] = coney::net::HumanAnimLayer{.clip = 388, .playhead = 0.0F, .gait = 0.0F, .fade = 1.0F};
        if (slot == 1) {
            human.position.x += 0.1F * static_cast<float>(step);
        } else if (slot == 2) {
            human.heading = 0.05F * static_cast<float>(step % 100);
        }
        humans.push_back(SnapshotHuman{
            .id = NetId{.slot = slot, .generation = 1}, .words = coney::net::humanWords(human), .priority = 1});
    }
    return humans;
}

// A host and a guest running the client game, stepped as their main loops would step them.
struct HostAndClient {
    LoopbackNetwork network;
    NetPlay host{[this] { return std::make_unique<LoopbackTransport>(network, "host"); }, {}};
    NetPlay guest{[this] { return std::make_unique<LoopbackTransport>(network, "guest"); }, {}};
    NetLevels guestLevels{guest};
    std::vector<LevelAt> loads; // every scene the client game loaded
    int sceneSteps = 0;
    int sceneFrames = 0;
    std::vector<std::string> printed;
    RecordingSound sound; // the guest's sound
    ClientGame client{guest,
                      [this](const LevelAt& at, coney::hud::HudOwner& /*hud*/)
                          -> std::expected<std::unique_ptr<ClientScene>, coney::Error> {
                          loads.push_back(at);
                          return std::make_unique<FakeScene>(at, &sceneSteps, &sceneFrames);
                      },
                      [](std::string_view level) { return level != "nolevel"; },
                      [this](std::string_view line) { printed.emplace_back(line); },
                      nullptr,
                      ClientPresentation{.sound = &sound, .hud = {}}};
    std::array<coney::hud::PlayerHudState, 2> hostPlayerHuds{}; // what the host's game hands over each step
    coney::hud::HudStanding hostStanding{};
    GameModeStack stack;
    bool hostMoves = true;          // the host's game steps (its humans move) unless the barrier holds it
    std::uint32_t hostGameStep = 0; // the host's game's own steps run
    std::map<std::uint32_t, coney::net::SnapshotHumans> sent; // the humans the host handed over, by its step
    std::uint64_t frame = 0;

    HostAndClient() { guestLevels.setGame(&client); }

    // Runs `steps` steps on both games: receive, NetLevels, the host's game (unless held) and the client game, send.
    void run(int steps) {
        for (int i = 0; i < steps; ++i) {
            host.advance(kStepNs);
            guest.advance(kStepNs);
            host.beforeStep();
            guest.beforeStep();
            guestLevels.beforeStep();
            if (hostMoves && !host.holdsStep()) {
                ++hostGameStep;
            }
            const std::vector<SnapshotHuman> humans = hostHumans(hostGameStep);
            coney::net::SnapshotHumans holding;
            for (const SnapshotHuman& human : humans) {
                holding.emplace(human.id, human.words);
            }
            sent[host.steps()] = holding;
            host.setGameHumans(humans);
            const coney::net::SnapshotHudStanding standing = coney::net::snapshotHudStanding(hostStanding);
            host.setGameHuds(coney::net::snapshotHuds(hostPlayerHuds), standing.elements, standing.texts);
            (void)client.update(stack, FrameTime{.index = frame, .seconds = 1.0 / 30.0});
            client.render(RenderTime{});
            ++frame;
            host.afterStep();
            guest.afterStep();
        }
    }

    // Hosts on a free port and joins it.
    void connect() {
        host.setPort(0);
        REQUIRE(host.host());
        guest.setAddress("host:" + std::to_string(host.status().port));
        REQUIRE(guest.join());
        run(4);
    }
};

// The newest step the host has sent a snapshot of.
std::uint32_t newestSent(const HostAndClient& g) { return g.host.steps() - 1; }

} // namespace

// Expected: unverified, #1603 (Coney's own: the delay is JitterConfig::delaySteps, docs/guides/client-world.md)
TEST_CASE("client game: the guest loads the host's level and draws the host's humans as the host had them, within the "
          "buffer's delay",
          "[net][net_play][client_game]") {
    HostAndClient g;
    g.host.loadLevel("level99", 1);
    g.connect();
    g.run(6);

    // The host's order loaded the level's scene once, and the host no longer holds for the guest.
    REQUIRE(g.loads == std::vector<LevelAt>{{.level = "level99", .checkpoint = 1}});
    CHECK(g.client.inPlay());
    CHECK_FALSE(g.host.holdsStep());
    REQUIRE(g.client.world() != nullptr);

    // Settled, every step the client draws a whole step of the host's, it draws the host's humans of that step, every
    // field's word equal; and it draws the host's game no further behind than the buffer's delay and the link's step.
    g.run(60);
    const std::uint32_t delay = coney::net::JitterConfig{}.delaySteps;
    int compared = 0;
    std::uint32_t mostBehind = 0;
    for (int i = 0; i < 200; ++i) {
        g.run(1);
        const ClientWorld& world = *g.client.world();
        const std::optional<std::uint64_t> time = world.shownTime();
        REQUIRE(time.has_value());
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above ends the test when empty
        const std::uint32_t shown = static_cast<std::uint32_t>(*time / coney::net::kTimeUnitsPerStep);
        mostBehind = std::max(mostBehind, newestSent(g) - shown);
        CHECK(newestSent(g) - shown <= delay + 1);
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above ends the test when empty
        if (*time % coney::net::kTimeUnitsPerStep != 0) {
            continue;
        }
        const auto expected = g.sent.find(shown);
        REQUIRE(expected != g.sent.end());
        REQUIRE(world.humans().size() == expected->second.size());
        for (const auto& [id, words] : expected->second) {
            const auto drawn = world.humans().find(id);
            REQUIRE(drawn != world.humans().end());
            CHECK(coney::net::humanWords(drawn->second) == words);
        }
        ++compared;
    }
    CHECK(compared >= 190);
    CHECK(mostBehind >= delay);
    CHECK(g.sceneSteps > 200);
    CHECK(g.sceneFrames > 200);
}

// Expected: unverified, #1603 (Coney's own: docs/guides/client-world.md#client-game)
TEST_CASE("client game: a new level gets a new world, a quit leaves the level, and a host's movie waits for no copy on "
          "the guest",
          "[net][net_play][client_game]") {
    HostAndClient g;
    g.host.loadLevel("level99", 1);
    g.connect();
    g.run(40);
    REQUIRE(g.client.world() != nullptr);
    CHECK_FALSE(g.client.world()->humans().empty());

    // The host goes on to another level: its scene loads, and its world starts with none of the last level's humans.
    g.hostMoves = false;
    g.host.loadLevel("level34", 2);
    g.run(2);
    REQUIRE(g.loads.size() == 2);
    CHECK(g.loads.back() == LevelAt{.level = "level34", .checkpoint = 2});
    REQUIRE(g.client.world() != nullptr);
    CHECK(g.client.world()->humans().empty());
    g.hostMoves = true;
    g.run(30);
    CHECK_FALSE(g.host.holdsStep());
    CHECK_FALSE(g.client.world()->humans().empty());

    // A movie on the host: the guest plays none, so the host goes on once its own copy ends, long before the time-out.
    const std::uint32_t movie = g.host.startMovie();
    g.host.gameEvents().raise(coney::GameEvent{.kind = coney::GameEventKind::MovieStart, .value = 1, .text = "L34_IN"});
    g.run(3);
    const std::optional<coney::net::HostMovie> told = g.guest.hostMovie();
    REQUIRE(told.has_value());
    CHECK(told->step == movie); // NOLINT(bugprone-unchecked-optional-access): the REQUIRE above ends the test
    g.host.endHostMovie();
    int waited = 0;
    while (!g.host.movieMayEnd() && waited < 30) {
        g.run(1);
        ++waited;
    }
    CHECK(g.host.movieMayEnd());
    CHECK(waited < 30);
    CHECK_FALSE(g.host.movieSkipped());

    // The host quits: the guest leaves the level and draws none.
    g.host.endMission(coney::net::MissionOutcome::Quit);
    g.run(3);
    CHECK_FALSE(g.client.inPlay());
    CHECK(g.client.world() == nullptr);
    CHECK(g.client.scene() == nullptr);

    // A level this disc cannot load is refused, and nothing is loaded.
    CHECK_FALSE(g.client.jumpToLevel("nolevel", 1));
}

// Expected: unverified, #1614 (Coney's own: events are presented when the playback reaches the host's step)
TEST_CASE("client game: an event the host raises is presented on the guest once, when its playback reaches the host's "
          "step",
          "[net][net_play][client_game][events]") {
    HostAndClient g;
    g.host.loadLevel("level99", 1);
    g.connect();
    g.run(40);
    REQUIRE(g.client.world() != nullptr);
    const std::uint64_t presentedBefore = g.client.eventsPresented();

    // Raised during the host's next step: a sound the guest plays, a hint and a message for its HUD, and the music.
    const std::uint32_t eventStep = g.host.steps();
    g.host.gameEvents().raise(coney::GameEvent{.kind = coney::GameEventKind::Sound2D, .id = 0x99, .value = 4});
    g.host.gameEvents().raise(
        coney::GameEvent{.kind = coney::GameEventKind::Hint, .id = coney::kHintQueued, .value = 2, .text = "Press X"});
    g.host.gameEvents().raise(
        coney::GameEvent{.kind = coney::GameEventKind::HudMessage, .id = 5, .value = 0, .text = "Run!"});
    g.host.gameEvents().raise(coney::GameEvent{.kind = coney::GameEventKind::MusicStart, .id = 0x77, .value = 1});

    // Until the playback reaches the host's step the guest presents none of them; from it, all, in the order raised.
    bool hudChecked = false;
    for (int i = 0; i < 40; ++i) {
        g.run(1);
        const std::optional<std::uint64_t> time = g.client.world()->shownTime();
        REQUIRE(time.has_value());
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above ends the test when empty
        const std::uint32_t shown = static_cast<std::uint32_t>(*time / coney::net::kTimeUnitsPerStep);
        if (g.client.eventsPresented() == presentedBefore) {
            CHECK(shown < eventStep);
        } else {
            CHECK(shown >= eventStep);
            CHECK(g.client.eventsPresented() == presentedBefore + 4);
            // On the HUD the step they are presented in: the hint queued, the message up.
            if (!hudChecked) {
                hudChecked = true;
                REQUIRE(g.client.hud() != nullptr);
                CHECK(g.client.hud()->hints().contains("Press X"));
                REQUIRE(g.client.hud()->centredAnnouncement().has_value());
                // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above ends the test when empty
                const std::string announced = g.client.hud()->centredAnnouncement()->text;
                CHECK(announced == "Run!");
            }
        }
    }
    CHECK(hudChecked);
    CHECK(g.client.eventsPresented() == presentedBefore + 4);
    // Each exactly once, on this guest's own sound and HUD.
    CHECK(g.sound.calls == std::vector<std::string>{"2d 0x99", "music 0x77"});
    // The host's own game is unchanged: its events were taken as before, none left held for it.
    CHECK(g.host.takeArrivedEvents(g.host.steps()).empty());
}

// Expected: unverified, #1614 (Coney's own: the guest's HUD is its own slot's, docs/guides/client-world.md#client-game)
TEST_CASE("client game: the HUD is the guest's own player slot's, from the host's snapshots",
          "[net][net_play][client_game][hud]") {
    HostAndClient g;
    g.host.loadLevel("level99", 1);
    // The host's player and the guest's: different rage and money, and the host's panel attached with a prompt.
    g.hostPlayerHuds[0].panel.rage = 20;
    g.hostPlayerHuds[0].panel.money = 7;
    g.hostPlayerHuds[1].panel.rage = 50;
    g.hostPlayerHuds[1].panel.money = 99;
    g.hostStanding.players[0].panel.attached = true;
    g.hostStanding.players[1].panel.attached = true;
    g.hostStanding.players[1].panel.banner = 0x33;
    g.hostStanding.players[0].prompt = "Host prompt";
    g.hostStanding.players[1].prompt = "Guest prompt";
    g.connect();
    g.run(40);
    REQUIRE(g.client.hud() != nullptr);
    const coney::hud::Hud& hud = *g.client.hud();

    // The guest is the host's remote player 1; its panel is the one player the guest's HUD has, panel 0.
    CHECK(g.guest.playerSlot() == 1);
    CHECK(hud.panel(0).values().rage == 50);
    CHECK(hud.panel(0).values().money == 99);
    CHECK(hud.panel(0).flags().attached);
    CHECK(hud.panel(0).flags().banner == 0x33);
    CHECK(hud.actionPrompt(0) == "Guest prompt");
    // The host's player is nobody's panel here: panel 1 stays as an unattached HUD has it.
    CHECK(hud.panel(1).values().rage == 0);
    CHECK_FALSE(hud.panel(1).flags().attached);

    // What the host's HUD holds for everyone follows it, a step later each time: its settings and a counter panel.
    g.hostStanding.shared.visible = false;
    g.hostStanding.shared.counters[0].used = true;
    g.hostStanding.shared.counters[0].label = "Tags";
    g.hostPlayerHuds[1].panel.rage = 60;
    g.run(40);
    CHECK_FALSE(hud.visible());
    CHECK(hud.counterPanels().panels().at(0).label == "Tags");
    CHECK(hud.panel(0).values().rage == 60);
}

// Expected: unverified, #1614 (Coney's own: a level's HUD is the level's, docs/guides/client-world.md#client-game)
TEST_CASE("client game: a new level gets a new HUD, and the guest's events stop with a quit",
          "[net][net_play][client_game][hud][events]") {
    HostAndClient g;
    g.host.loadLevel("level99", 1);
    g.connect();
    g.run(40);
    g.host.gameEvents().raise(coney::GameEvent{.kind = coney::GameEventKind::HudMessage, .id = 5, .text = "Run!"});
    g.run(40);
    REQUIRE(g.client.hud() != nullptr);
    CHECK(g.client.hud()->centredAnnouncement().has_value());

    // The host goes on to another level: its HUD starts as the new level's, with none of the last one's messages.
    g.hostMoves = false;
    g.host.loadLevel("level34", 2);
    g.run(2);
    REQUIRE(g.client.hud() != nullptr);
    CHECK_FALSE(g.client.hud()->centredAnnouncement().has_value());

    // The host quits: the guest has no level, no HUD, and nothing it still plays.
    g.hostMoves = true;
    g.run(30);
    g.host.endMission(coney::net::MissionOutcome::Quit);
    g.run(3);
    CHECK(g.client.hud() == nullptr);
}
