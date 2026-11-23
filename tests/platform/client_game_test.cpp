// SPDX-License-Identifier: GPL-3.0-or-later
// The client game (docs/guides/client-world.md#client-game): a guest that runs no game of its own, following the
// host's levels and drawing the host's humans from the snapshots it decodes, two games in one process on the loopback
// in test mode's fixed steps, with no display (a fake scene stands for the drawing).
#include "platform/client_game.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <expected>
#include <format>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "camera/camera_view.h"
#include "camera/follow_camera.h"
#include "core/error.h"
#include "core/game_events.h"
#include "core/pads.h"
#include "gamemodes/game_mode.h"
#include "gamemodes/game_mode_stack.h"
#include "hud/hud.h"
#include "hud/hud_owner.h"
#include "human/locomotion.h"
#include "net/client_camera.h"
#include "net/client_world.h"
#include "net/jitter_buffer.h"
#include "net/loopback_transport.h"
#include "net/net_client.h"
#include "net/net_id.h"
#include "net/player_huds.h"
#include "net/protocol.h"
#include "net/radar_blips.h"
#include "net/snapshot.h"
#include "platform/net_levels.h"
#include "platform/net_play.h"
#include "scripting/sound_bindings.h"
#include "support/optional_value.h"

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

// What the fake scene saw of the client's camera on its last step and frame, and what the client game asked it.
struct SceneSeen {
    std::optional<NetId> followed;                      // the human the camera followed on the last step
    std::optional<coney::camera::CameraView> stepView;  // the camera's view of the newest step, seen by step()
    std::optional<coney::camera::CameraView> frameView; // the camera's view at the frame's alpha, seen by render()
    std::uint32_t cuts = 0;                             // the camera's cuts
    std::vector<std::int32_t> speedsAsked;              // the characters it was asked the speeds of, in order
    int collisionAsked = 0;                             // how often the client game asked for the collision
};

// What the client game drew: a scene that counts its steps and frames and remembers the humans it was last given.
struct FakeScene final : ClientScene {
    LevelAt level;
    int* steps;
    int* frames;
    SceneSeen* seen;
    FakeScene(LevelAt at, int* stepCount, int* frameCount, SceneSeen* seenOut)
        : level(std::move(at)), steps(stepCount), frames(frameCount), seen(seenOut) {}
    void step(const ClientWorld& /*world*/, const coney::net::ClientCamera& camera,
              const FrameTime& /*frame*/) override {
        ++*steps;
        seen->followed = camera.followed();
        seen->stepView = camera.view(1.0F);
        seen->cuts = camera.cuts();
    }
    void render(const ClientWorld& /*world*/, const coney::net::ClientCamera& camera, const RenderTime& time) override {
        ++*frames;
        seen->frameView = camera.view(time.alpha);
    }
    [[nodiscard]] const coney::raycast::CollisionMesh* collision() const override {
        ++seen->collisionAsked;
        return nullptr;
    }
    [[nodiscard]] coney::human::Speeds speedsOf(std::int32_t character) override {
        seen->speedsAsked.push_back(character);
        return coney::human::Speeds{
            .base = 1.0F, .sneak = 0.8F, .walk = 1.6F, .jog = 3.0F, .run = 4.6F, .sprint = 7.0F};
    }
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
        human.character = slot == 0 ? 7 : 0; // player 1 is character 7 (the camera asks for his gait speeds)
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

// The guest's own pad on port 1: plugged in, its right stick wherever a test sets it (at rest at first).
class GuestPad final : public coney::InputSource {
  public:
    std::uint8_t rightX = coney::net::kClientStickRest;
    coney::PortSamples sample(std::uint64_t /*frame*/) override {
        coney::PortSamples samples{};
        samples.at(0).connected = true;
        samples.at(0).sticks = {rightX, coney::net::kClientStickRest, coney::net::kClientStickRest,
                                coney::net::kClientStickRest};
        return samples;
    }
};

// A host and a guest running the client game, stepped as their main loops would step them.
struct HostAndClient {
    LoopbackNetwork network;
    NetPlay host{[this] { return std::make_unique<LoopbackTransport>(network, "host"); }, {}};
    NetPlay guest{[this] { return std::make_unique<LoopbackTransport>(network, "guest"); }, {}};
    NetLevels guestLevels{guest};
    std::vector<LevelAt> loads; // every scene the client game loaded
    int sceneSteps = 0;
    int sceneFrames = 0;
    SceneSeen seen; // what the fake scene saw of the client camera
    std::vector<std::string> printed;
    RecordingSound sound; // the guest's sound
    ClientGame client{guest,
                      [this](const LevelAt& at, coney::hud::HudOwner& /*hud*/)
                          -> std::expected<std::unique_ptr<ClientScene>, coney::Error> {
                          loads.push_back(at);
                          return std::make_unique<FakeScene>(at, &sceneSteps, &sceneFrames, &seen);
                      },
                      [](std::string_view level) { return level != "nolevel"; },
                      [this](std::string_view line) { printed.emplace_back(line); },
                      nullptr,
                      ClientPresentation{.sound = &sound, .hud = {}}};
    std::array<coney::hud::PlayerHudState, 2> hostPlayerHuds{}; // what the host's game hands over each step
    std::array<NetId, 2> hostDrivers{}; // the human each of the host's player slots drives (none at first)
    coney::hud::HudStanding hostStanding{};
    std::vector<coney::net::SnapshotBlip> hostBlips{}; // the host's radar blips, as its game hands them over each step
    GameModeStack stack;
    GuestPad pad;
    bool hostMoves = true;          // the host's game steps (its humans move) unless the barrier holds it
    std::uint32_t hostGameStep = 0; // the host's game's own steps run
    std::map<std::uint32_t, coney::net::SnapshotHumans> sent; // the humans the host handed over, by its step
    std::uint64_t frame = 0;

    HostAndClient() {
        guestLevels.setGame(&client);
        stack.setInput(&pad);
    }

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
            host.setGameHuds(coney::net::snapshotHuds(hostPlayerHuds, hostDrivers), standing.elements, standing.texts,
                             hostBlips);
            stack.samplePads(frame);
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

// Expected: unverified, #1614 (Coney's own: a level's events are the level's, docs/guides/client-world.md#client-game)
TEST_CASE("client game: an event the host raised just before a level change is not presented in the next level",
          "[net][net_play][client_game][hud][events]") {
    HostAndClient g;
    g.host.loadLevel("level99", 1);
    g.connect();
    g.run(40);
    REQUIRE(g.client.hud() != nullptr);
    const std::uint64_t presentedBefore = g.client.eventsPresented();
    const std::size_t loadsBefore = g.loads.size();

    // The host raises a message and, a step later, goes on to another level: the guest's playback has not reached
    // the message's step when the new level loads.
    g.host.gameEvents().raise(coney::GameEvent{.kind = coney::GameEventKind::HudMessage, .id = 5, .text = "Run!"});
    g.run(1);
    g.host.loadLevel("level34", 2);
    g.run(60);

    // The new level loaded, and its HUD never showed the last level's message.
    REQUIRE(g.loads.size() == loadsBefore + 1);
    REQUIRE(g.client.hud() != nullptr);
    CHECK_FALSE(g.client.hud()->centredAnnouncement().has_value());
    CHECK(g.client.eventsPresented() == presentedBefore);
}

namespace {

// The camera's view direction on the ground, as an angle (radians, anticlockwise from +x seen from above).
float yawOf(const coney::camera::CameraView& view) {
    const coney::anim::Vec3 forward = coney::camera::viewForward(view);
    return std::atan2(forward.y, forward.x);
}

// The newest step's view of the camera the fake scene was given; a failed check and the default view when none.
coney::camera::CameraView stepViewOf(const HostAndClient& g) {
    CHECK(g.seen.stepView.has_value());
    return g.seen.stepView.value_or(coney::camera::CameraView{});
}

} // namespace

// Expected: unverified, #1333 (Coney's own: docs/guides/client-world.md#client-camera; the host's player 1 is the
// human with the lowest network id, whose character is 7 in these snapshots)
TEST_CASE("client game: the guest's own camera follows the host's player 1 from the snapshots, behind him at the "
          "follow camera's distance",
          "[net][net_play][client_game][client_camera]") {
    HostAndClient g;
    g.host.loadLevel("level99", 1);
    g.connect();
    g.run(60);
    REQUIRE(g.client.world() != nullptr);

    // The camera follows the lowest id, not the human walking beside him, from the first step the world showed him, and
    // the scene was asked his character's speeds and the level's collision for it.
    const NetId player1{.slot = 0, .generation = 1};
    CHECK(g.seen.followed == std::optional<NetId>{player1});
    CHECK(g.client.camera().followed() == std::optional<NetId>{player1});
    CHECK(g.seen.cuts == 1);
    REQUIRE_FALSE(g.seen.speedsAsked.empty());
    CHECK(std::ranges::all_of(g.seen.speedsAsked, [](std::int32_t character) { return character == 7; }));
    CHECK(g.seen.collisionAsked > 0);

    // He stands at (0, 5, 0) facing +y: the camera is behind him (lower y), above his feet, within the leash band.
    const coney::camera::CameraView view = stepViewOf(g);
    const coney::camera::FollowCamera* follow = g.client.camera().follow();
    REQUIRE(follow != nullptr);
    CHECK(view.position.y < 5.0F);
    CHECK(view.position.x == Catch::Approx(0.0F).margin(0.1));
    CHECK(view.position.z > 0.0F);
    const float away = std::hypot(view.position.x - 0.0F, view.position.y - 5.0F);
    CHECK(away >= follow->bandNear() * 0.8F);
    CHECK(away <= follow->bandFar() + 0.5F);
    CHECK(yawOf(view) == Catch::Approx(std::numbers::pi_v<float> / 2.0F).margin(0.05));

    // A frame draws the camera at its own alpha: at 1, the newest step's view exactly.
    REQUIRE(g.seen.frameView.has_value());
    CHECK(g.seen.frameView.value_or(coney::camera::CameraView{}).position.y == view.position.y);
}

// Expected: unverified, #1654 (Coney's own: docs/guides/client-world.md#client-camera; the guest holds player slot 1,
// and the host's snapshots say which human that slot drives)
TEST_CASE("client game: the guest's camera follows the human its player slot drives, and cuts to another when the host "
          "gives the slot another",
          "[net][net_play][client_game][client_camera]") {
    HostAndClient g;
    g.host.loadLevel("level99", 1);
    // The host's slot 0 drives its player 1 (id 0) and the guest's slot 1 the human walking beside him (id 1).
    const NetId player1{.slot = 0, .generation = 1};
    const NetId walker{.slot = 1, .generation = 1};
    const NetId turner{.slot = 2, .generation = 1};
    g.hostDrivers = {player1, walker};
    g.connect();
    g.run(60);
    REQUIRE(g.client.world() != nullptr);
    REQUIRE(g.guest.playerSlot() == 1);
    CHECK(g.seen.followed == std::optional<NetId>{walker});
    CHECK(g.client.camera().followed() == std::optional<NetId>{walker});
    CHECK(g.seen.cuts == 1);
    // The camera is made behind him, where he walks: well along x from the host's player 1, who stands at x 0.
    CHECK(stepViewOf(g).position.x > 8.0F);

    // The host gives the slot another human: the camera is made behind him, once.
    g.hostDrivers = {player1, turner};
    g.run(60);
    CHECK(g.client.camera().followed() == std::optional<NetId>{turner});
    CHECK(g.seen.followed == std::optional<NetId>{turner});
    CHECK(g.seen.cuts == 2);

    // The slot drives none (its player is gone): the guest watches the host's player 1 again.
    g.hostDrivers = {player1, NetId{}};
    g.run(60);
    CHECK(g.client.camera().followed() == std::optional<NetId>{player1});
    CHECK(g.seen.cuts == 3);
}

// Expected: unverified, #1333 (Coney's own: the guest's own right stick turns its own view, as the right stick turns
// the follow camera, docs/research/camera.md#right-stick; the host's game never reads it)
TEST_CASE("client game: the guest's own right stick turns the guest's own camera and nothing the host reads",
          "[net][net_play][client_game][client_camera]") {
    HostAndClient g;
    g.host.loadLevel("level99", 1);
    g.connect();
    g.run(60);
    const float before = yawOf(stepViewOf(g));
    CHECK(before == Catch::Approx(std::numbers::pi_v<float> / 2.0F).margin(0.05));

    // The stick pushed left for half a second turns the view anticlockwise, 0.94 radians as measured.
    g.pad.rightX = 0;
    g.run(15);
    CHECK(std::remainder(yawOf(stepViewOf(g)) - before, 2.0F * std::numbers::pi_v<float>) > 0.8F);

    // He did not move: the host's snapshots still have him where he stood.
    REQUIRE(g.client.world() != nullptr);
    const NetId player1{.slot = 0, .generation = 1};
    const auto shown = g.client.world()->humans().find(player1);
    REQUIRE(shown != g.client.world()->humans().end());
    CHECK(shown->second.position.y == Catch::Approx(5.0F).margin(0.01));
}

// Expected: unverified, #1333 (Coney's own: a camera is the level's, made afresh for each load like its world)
TEST_CASE("client game: a new level has a new camera that follows nobody until its world shows a human, and a quit "
          "leaves none",
          "[net][net_play][client_game][client_camera]") {
    HostAndClient g;
    g.host.loadLevel("level99", 1);
    g.connect();
    g.run(60);
    REQUIRE(g.client.camera().followed().has_value());
    CHECK(g.client.camera().cuts() == 1);

    g.hostMoves = false;
    g.host.loadLevel("level34", 2);
    g.run(2);
    REQUIRE(g.loads.size() == 2);
    CHECK_FALSE(g.client.camera().followed().has_value());
    CHECK(g.client.camera().cuts() == 0);
    CHECK_FALSE(g.client.camera().view(1.0F).has_value());

    // The new level's first human: the camera is made behind him, once.
    g.hostMoves = true;
    g.run(40);
    CHECK(g.client.camera().followed().has_value());
    CHECK(g.client.camera().cuts() == 1);

    // The host quits: no level, and no camera view.
    g.host.endMission(coney::net::MissionOutcome::Quit);
    g.run(3);
    CHECK_FALSE(g.client.inPlay());
    CHECK_FALSE(g.client.camera().followed().has_value());
}

// Expected: unverified, #1537 (Coney's own: the guest's radar draws the host's blips,
// docs/guides/snapshots.md#hud-blips)
TEST_CASE("client game: the host's radar blips are the guest's HUD's, a human's by its id and a fixed one by its place",
          "[net][net_play][client_game][hud][radar]") {
    HostAndClient g;
    g.host.loadLevel("level99", 1);
    // The host's blips: a mission objective at a fixed place, and a human's blip that follows the host's walking human
    // (id slot 1) by his id, whatever script handle the host's blip is keyed by.
    std::map<double, coney::hud::RadarBlip> blips;
    blips.emplace(41.0, coney::hud::RadarBlip{.type = 10, .icon = 27, .place = coney::anim::Vec3{3.0F, 40.0F, 5.0F}});
    blips.emplace(77.0, coney::hud::RadarBlip{.type = 7, .icon = 365, .scale = 0.56F, .dotShown = true});
    coney::net::BlipSlots slots;
    const NetId walker{.slot = 1, .generation = 1};
    g.hostBlips =
        coney::net::snapshotBlips(blips, slots, [walker](double handle) { return handle == 77.0 ? walker : NetId{}; });
    g.connect();
    g.run(40);
    REQUIRE(g.client.hud() != nullptr);
    REQUIRE(g.client.world() != nullptr);
    const coney::hud::Hud& hud = *g.client.hud();

    // The guest's HUD holds both, keyed by their slots, with what the host's blips carry.
    REQUIRE(hud.radar().blips.size() == 2);
    const coney::hud::RadarBlip& objective = hud.radar().blips.at(0.0);
    const coney::hud::RadarBlip& human = hud.radar().blips.at(1.0);
    CHECK(objective.type == 10);
    CHECK(objective.icon == 27);
    CHECK(human.type == 7);
    CHECK(human.icon == 365);
    CHECK(human.dotShown);
    CHECK(human.follows == coney::net::netIdNumber(walker));

    // The guest's radar is given the view of the human its slot follows, so its blips are placed on it: the objective
    // where the host had it, the human's blip where the guest's world shows the human now.
    CHECK(hud.radar().view.known);
    const coney::net::NetId followedId = g.client.camera().followed().value_or(coney::net::NetId{});
    REQUIRE(followedId.valid());
    const auto followed = g.client.world()->humans().find(followedId);
    REQUIRE(followed != g.client.world()->humans().end());
    CHECK(hud.radar().view.position == followed->second.position);
    CHECK(hud.radar().view.heading == followed->second.heading);
    CHECK(hud.radar().view.speed == 0.0F);
    REQUIRE(objective.place.has_value());
    CHECK(coney::test::got(objective.place).x == Catch::Approx(3.0F).margin(0.07F));
    CHECK(coney::test::got(objective.place).y == Catch::Approx(40.0F).margin(0.07F));
    const auto walking = g.client.world()->humans().find(walker);
    REQUIRE(walking != g.client.world()->humans().end());
    REQUIRE(human.place.has_value());
    CHECK(coney::test::got(human.place) == walking->second.position);

    // The human walks on, and so does the blip; a human the guest's world does not hold leaves a blip unplaced.
    const coney::anim::Vec3 before = coney::test::got(human.place);
    g.run(10);
    CHECK(coney::test::got(human.place).x > before.x);
    blips.at(77.0).type = 7;
    const NetId absent{.slot = 40, .generation = 1};
    g.hostBlips =
        coney::net::snapshotBlips(blips, slots, [absent](double handle) { return handle == 77.0 ? absent : NetId{}; });
    g.run(10);
    CHECK_FALSE(hud.radar().blips.at(1.0).place.has_value());
}
