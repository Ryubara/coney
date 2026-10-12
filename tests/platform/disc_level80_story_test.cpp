// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the second story mission's hints follow the mission from checkpoint to
// checkpoint, on the owner's path (a stale "throw a bottle" hint was seen to stay up until the Wreck'em order). The
// story is started from the menus, in the one game session every start uses (tests/support/disc_session.h), and level80
// is jumped to at checkpoints 1, 2, 3 and 4 in turn, as the debug menu does. Each start must find the hint box and the
// objective empty, whatever the last one left. An adaptive pad plays as a player would, the left stick at 70 % on the
// walks. At checkpoint 1 it gives the follow order TT_1 and TT_4 ask for. At checkpoint 3 it walks to Fox, gives the
// Hold Up order, goes up the stairs to the roof (no teleport), plays the throw lesson and gives the Wreck'em order.
// The hint box must show each text in the original's order (docs/research/scripting.md#level80-hints), keep nothing
// of a step once the next has begun, and be empty when the bottle breaks and after the order. It runs only when the
// environment variable CONEY_DISC names the disc and skips otherwise; it prints counts and keys only (LEGAL.md).
//
// **Known gap**: checkpoints 2 and 4 are only started, not played to their hints (TT_11, TT_8): the walks from their
// starts to Snow and Cowboy and to Cochise cross the second world's collision, which no route here has mapped yet.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/route_follower.h"
#include "ai/route_planner.h"
#include "animation/anim_math.h"
#include "combat/throw_aim.h"
#include "core/input_script.h"
#include "core/pad.h"
#include "core/pads.h"
#include "fileio/wad.h"
#include "gamemodes/start_up_flow.h"
#include "hud/hint_box.h"
#include "hud/hud.h"
#include "hud/messages.h"
#include "human/human.h"
#include "human/locomotion.h"
#include "human/player.h"
#include "human/script_state.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "scenes/scene_player.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "warriors/game_state.h"
#include "world_objects/flags.h"

#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"

namespace {

// The hints, objectives and scenes the mission shows, by their keys in the level's text
// (docs/research/scripting.md#level80-hints).
constexpr std::array<std::string_view, 12> kHintKeys{"TT_1",  "TT_4",  "TT_11", "TT_5", "TT_25", "TT_26",
                                                     "TT_27", "TT_28", "TT_29", "TT_7", "TT_8",  "TANDEM"};
constexpr std::array<std::string_view, 10> kObjectiveKeys{"MS_C1_1", "MS_C2_1", "MS_C2_2", "MS_C3_1", "MS_C3_1a",
                                                          "MS_C3_2", "MS_C3_3", "MS_C3_4", "MS_C4_1", "MS_C4_2"};
constexpr std::array<std::string_view, 3> kSceneNames{"l80_c5", "l80_c6", "l80_c6_b"};
// The throw lesson's texts: none may show once the bottle has broken.
constexpr std::array<std::string_view, 5> kThrowLesson{"TT_25", "TT_26", "TT_27", "TT_28", "TT_29"};

// How long any one step may take, frames (a minute of play).
constexpr int kWait = 30 * 60;
// The stick's deflection on the walks: a walk, not a run (docs/guides/testing.md#gamepad).
constexpr float kWalk = 0.7F;
// A walk that gains no 0.3 m for this many frames with the pad is stalled; it steps aside for kSidestepFrames, at most
// kSidesteps times.
constexpr int kStall = 60;
constexpr int kSidestepFrames = 30;
constexpr int kSidesteps = 6;

// Player 1's pad, set by the test each frame from what the game shows: the buttons held and both sticks in [-1, 1],
// y up, written as an input script writes them (each axis has its own dead zone, core/pad.h).
class DrivenPad final : public coney::InputSource {
  public:
    std::uint16_t buttons = 0;
    float leftX = 0.0F;
    float leftY = 0.0F;
    float rightX = 0.0F;
    float rightY = 0.0F;

    coney::PortSamples sample(std::uint64_t /*frame*/) override {
        coney::PortSamples samples{};
        coney::PadSample& pad = samples[0];
        pad.connected = true;
        pad.buttons = buttons;
        pad.sticks[0] = raw(rightX);
        pad.sticks[1] = raw(-rightY);
        pad.sticks[2] = raw(leftX);
        pad.sticks[3] = raw(-leftY);
        return samples;
    }

  private:
    // The byte for `value` as an input script's `stick` line writes it.
    static std::uint8_t raw(float value) {
        return coney::stickByteFromPercent(static_cast<int>(std::lround(std::clamp(value, -1.0F, 1.0F) * 100.0F)));
    }
};

// What the run showed, in order: each hint, objective and scene when it appears, by its key (a text the keys do not
// name is skipped).
struct Shown {
    std::vector<std::string> order;
    std::string hint;          // the key of the hint showing, empty when none or unnamed
    std::string objective;     // the key of objective slot 0's line
    std::string hintText;      // the hint box's text last frame
    std::string objectiveText; // objective slot 0's text last frame
    std::vector<std::string> scenes;

    // Records this frame's hint, objective line and scenes.
    void watch(coney::platform::GameSession& session) {
        coney::StartUpFlow& flow = session.flow();
        const coney::hud::Hud& hud = flow.hud();
        const std::optional<coney::hud::Hint>& showing = hud.hints().showing();
        if (std::string text = showing ? showing->text : std::string(); text != hintText) {
            hintText = std::move(text);
            hint = hintText.empty() ? std::string() : keyOf(flow, hintText, kHintKeys);
            if (!hint.empty()) {
                order.push_back(hint);
            }
        }
        const std::optional<coney::hud::ChecklistLine>& line = hud.checklist().slots[0];
        if (std::string text = line ? line->text : std::string(); text != objectiveText) {
            objectiveText = std::move(text);
            if (std::string key = objectiveText.empty() ? std::string() : keyOf(flow, objectiveText, kObjectiveKeys);
                !key.empty()) {
                objective = key;
                order.push_back(std::move(key));
            }
        }
        if (coney::scenes::SceneSystem* system = flow.context().scenes; system != nullptr) {
            for (const std::string_view name : kSceneNames) {
                const std::optional<std::uint32_t> id = system->idOf(name);
                if (id && system->state(*id) == coney::scenes::SceneState::Playing &&
                    std::ranges::find(scenes, name) == scenes.end()) {
                    scenes.emplace_back(name);
                    order.emplace_back(name);
                }
            }
        }
    }

    // The key among `keys` whose text the level's scripts hold, as a global or a field of a global table.
    static std::string keyOf(coney::StartUpFlow& flow, std::string_view text, std::span<const std::string_view> keys) {
        const auto find = [text, keys](const coney::script::Table& table, const auto& self, int depth) -> std::string {
            coney::script::Value key;
            while (const auto entry = table.next(key)) {
                key = entry->first;
                const std::optional<std::string_view> name = entry->first.string();
                if (const std::optional<std::string_view> value = entry->second.string();
                    name && value && *value == text && std::ranges::find(keys, *name) != keys.end()) {
                    return std::string(*name);
                }
                if (entry->second.type() == coney::script::Value::Type::Table && depth > 0) {
                    if (std::string found = self(*entry->second.table(), self, depth - 1); !found.empty()) {
                        return found;
                    }
                }
            }
            return {};
        };
        return find(flow.scripts().vm().globals(), find, 1);
    }
};

// The run: the session, its pad and what it showed, stepped one frame at a time.
struct Run {
    coney::test::DiscSession& session;
    DrivenPad& pad;
    Shown shown;
    coney::anim::Vec3 lastAim{};                   // where the last walk steered, for the failure dump
    std::pair<float, float> lastStick{0.0F, 0.0F}; // its last stick

    // The play mode (null outside play), the start-up flow and the HUD.
    [[nodiscard]] coney::platform::PlayLevelMode* play() { return session.session().play(); }
    [[nodiscard]] coney::StartUpFlow& flow() { return session.session().flow(); }
    [[nodiscard]] coney::hud::Hud& hud() { return flow().hud(); }
    // The route planner of the level in play (null without one).
    [[nodiscard]] coney::ai::RoutePlanner* routePlanner() {
        const coney::ai::Brains* brains = session.session().gameplay().brains();
        return brains != nullptr ? brains->planner() : nullptr;
    }

    // Steps until `done` holds, at most `frames` frames, recording what shows each frame.
    bool until(const std::function<bool()>& done, int frames = kWait) {
        return session.runUntil(
            [&] {
                shown.watch(session.session());
                return done();
            },
            static_cast<std::uint64_t>(frames));
    }
    // Steps `frames` frames.
    void wait(int frames) {
        static_cast<void>(until([] { return false; }, frames));
    }

    // Whether player 1 has the pad.
    bool padded() { return play() != nullptr && play()->player().padControlled(); }
    // Whether `key` is the hint showing and the player can read it: the HUD drawn and the box not hidden behind a
    // message (after a scene the HUD comes back a few frames after the script set the next text).
    bool readable(std::string_view key) { return shown.hint == key && hud().visible() && !hud().hintsHidden(); }

    // Taps cross every second until player 1 has the pad (a scene skipped, as a player does).
    bool skipToPad() {
        int frame = 0;
        const bool ok = until([&] {
            if (frame > 0 && padded()) {
                return true;
            }
            pad.buttons = (frame++ % 30 == 0) ? coney::pad::kCross : 0;
            return false;
        });
        pad.buttons = 0;
        return ok;
    }

    // Walks player 1 to `to` with the left stick at `deflection`, read against the camera shown, until `done`. He
    // follows the way the level's path data gives (the route planner the AI walks by, as a player who knows the
    // streets), or the straight line when it is walkable. False when `done` never held or he could not make way
    // even after stepping aside.
    bool walkTo(coney::anim::Vec3 to, const std::function<bool()>& done, float deflection = kWalk, bool routed = true) {
        std::optional<coney::ai::RouteFollower> follower;
        bool planned = false;
        coney::anim::Vec3 last{};
        int stalled = 0;
        int sidesteps = 0;
        int sidestepFrames = 0;
        static_cast<void>(until([&] {
            if (done()) {
                return true;
            }
            coney::platform::PlayLevelMode* mode = play();
            if (mode == nullptr || !mode->player().padControlled()) {
                return false;
            }
            const coney::human::Human& human = mode->player().human();
            const coney::anim::Vec3 feet = human.position();
            coney::ai::RoutePlanner* planner = routePlanner();
            if (!planned && planner != nullptr && routed) {
                planned = true;
                last = feet;
                if (auto plan = planner->request(feet, to); plan && plan->route) {
                    follower.emplace(*planner, std::move(*plan->route), feet, to);
                }
            }
            // Stalled: no 0.3 m moved in kStall frames. Something stands in the way (a passer-by, a car): step round
            // it, to one side then the other.
            if (std::hypot(feet.x - last.x, feet.y - last.y) >= 0.3F) {
                last = feet;
                stalled = 0;
            } else if (++stalled > kStall) {
                if (++sidesteps > kSidesteps) {
                    return true;
                }
                stalled = 0;
                sidestepFrames = kSidestepFrames;
            }
            const coney::anim::Vec3 aim =
                follower && planner != nullptr ? follower->waypoint(*planner, feet, human.speed()) : to;
            auto [x, y] = stickTowards(*mode, aim, deflection);
            if (sidestepFrames > 0) {
                --sidestepFrames;
                const float side = sidesteps % 2 == 1 ? 1.0F : -1.0F;
                const float sideX = side * y;
                const float sideY = -side * x;
                x = sideX;
                y = sideY;
            }
            pad.leftX = x;
            lastAim = aim;
            lastStick = {x, y};
            pad.leftY = y;
            return false;
        }));
        pad.leftX = 0.0F;
        pad.leftY = 0.0F;
        return done();
    }

    // Gives a Warrior order: R2 held, the right stick at (x, y) while it is held, R2 let go.
    void order(float x, float y) {
        pad.buttons = coney::pad::kR2;
        wait(2);
        pad.rightX = x;
        pad.rightY = y;
        wait(6);
        pad.buttons = 0;
        wait(1);
        pad.rightX = 0.0F;
        pad.rightY = 0.0F;
        wait(5);
    }

    // A flag's place, by its name as the level gave it (kept to its first 15 characters).
    coney::anim::Vec3 flagAt(std::string_view name) {
        const std::string_view kept = name.substr(0, 15);
        const std::vector<coney::world_objects::WorldFlag>& all = flow().flags().all();
        const auto flag =
            std::ranges::find_if(all, [kept](const coney::world_objects::WorldFlag& f) { return f.name == kept; });
        REQUIRE(flag != all.end());
        return coney::anim::Vec3{flag->position[0], flag->position[1], flag->position[2]};
    }

    // On a failure: where player 1 stands, what showed and the session's log.
    void dump() {
        if (play() != nullptr) {
            const coney::anim::Vec3 at = play()->player().human().position();
            const coney::anim::Vec3 eye = play()->cameraEye();
            const coney::anim::Vec3 target = play()->cameraTarget();
            UNSCOPED_INFO("camera eye " << eye.x << " " << eye.y << " " << eye.z << " target " << target.x << " "
                                        << target.y << " " << target.z << "; stick " << lastStick.first << " "
                                        << lastStick.second << " aim " << lastAim.x << " " << lastAim.y << "; anim "
                                        << play()->player().human().animator().animId() << " speed "
                                        << play()->player().human().speed());
            UNSCOPED_INFO("player at " << at.x << " " << at.y << " " << at.z);
            // Who stands within 3 m: a walk that stalls is often blocked by a human.
            if (const coney::ai::Brains* brains = session.session().gameplay().brains(); brains != nullptr) {
                for (std::size_t i = 0; i < brains->size(); ++i) {
                    const coney::anim::Vec3 other = brains->at(i).human().position();
                    if (std::hypot(other.x - at.x, other.y - at.y) < 3.0F) {
                        UNSCOPED_INFO("human near at " << other.x << " " << other.y);
                    }
                }
            }
        }
        for (const std::string& key : shown.order) {
            UNSCOPED_INFO("shown " << key);
        }
        UNSCOPED_INFO("checkpoint " << flow().state().checkPoint << ", script errors " << flow().scripts().errors());
        for (const std::string& line : session.log()) {
            UNSCOPED_INFO(line);
        }
    }

  private:
    // The left stick, in [-1, 1] at `deflection`, that walks player 1 towards `to` under the camera shown: the stick
    // is read against the camera's forward, x to its right.
    static std::pair<float, float> stickTowards(const coney::platform::PlayLevelMode& mode, coney::anim::Vec3 to,
                                                float deflection) {
        const coney::anim::Vec3 way = coney::anim::subtract(to, mode.player().human().position());
        coney::anim::Vec3 forward = coney::anim::subtract(mode.cameraTarget(), mode.cameraEye());
        const float length = std::hypot(forward.x, forward.y);
        const float along = std::hypot(way.x, way.y);
        if (length <= 0.0F || along <= 0.0F) {
            return {0.0F, 0.0F};
        }
        forward = {forward.x / length, forward.y / length, 0.0F};
        const coney::anim::Vec3 right{forward.y, -forward.x, 0.0F};
        return {deflection * ((way.x * right.x) + (way.y * right.y)) / along,
                deflection * ((way.x * forward.x) + (way.y * forward.y)) / along};
    }
};

// The stick value in [-1, 1] that reads as `steps` past the throw aim's dead zone (sign kept), so the aim turns or
// pitches by steps x 0.000589 rad a frame.
float aimStick(int steps) {
    constexpr int kLess = 25;
    if (steps == 0) {
        return 0.0F;
    }
    const int offset = (steps > 0 ? 1 : -1) * (std::abs(steps) + kLess);
    return static_cast<float>(offset) / 127.5F;
}

// The hint keys of `order` from index `from` on.
std::vector<std::string> hintsFrom(const std::vector<std::string>& order, std::size_t from) {
    std::vector<std::string> hints;
    for (std::size_t i = from; i < order.size(); ++i) {
        if (order[i].starts_with("TT_")) {
            hints.push_back(order[i]);
        }
    }
    return hints;
}

} // namespace

TEST_CASE("the disc's level80 from the story: the hints at each checkpoint, walked to the roof, none carried over",
          "[disc][story][level80][hints]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    DrivenPad pad;
    coney::test::DiscSession session(*wad, pad);
    Run run{.session = session, .pad = pad, .shown = {}};
    // The story from the menus; each checkpoint is then reached by the debug menus' jump, as a player can.
    session.session().startStory();
    REQUIRE(session.runUntil([&run] { return run.flow().state().levels.find("level80").has_value(); },
                             coney::test::DiscSession::kPlayLimit));
    // Jumps to `checkpoint` and plays until player 1 has the pad; the box must start empty, whatever showed before.
    // Returns where the run's record stood before the jump.
    const auto jumpTo = [&](int checkpoint) {
        const std::size_t before = run.shown.order.size();
        REQUIRE(session.session().jumpToLevel("level80", checkpoint));
        REQUIRE(run.until([&run] { return !run.session.session().inPlay(); }, 600));
        REQUIRE(session.runUntilPlay());
        CHECK_FALSE(run.hud().hints().showing().has_value());
        CHECK(run.hud().hints().queued().empty());
        // The checkpoint may set its own objective before its first frame of play; none of another may stand.
        if (const auto& line = run.hud().checklist().slots[0]; line) {
            const std::string key = Shown::keyOf(run.flow(), line->text, kObjectiveKeys);
            CHECK(key.starts_with("MS_C" + std::to_string(checkpoint) + "_"));
        }
        REQUIRE(run.until([&run] { return run.padded(); }));
        return before;
    };

    // Checkpoint 1: TT_1, then TT_4, ask for the follow order (R2, the right stick up); the order clears the text.
    const std::size_t at1 = jumpTo(1);
    REQUIRE(run.until([&] { return run.readable("TT_4"); }));
    run.order(0.0F, 0.95F);
    CHECK(run.flow().state().characters.lastWarriorCommand.at(0) == 0);
    CHECK(run.flow().state().story.commandCallback.empty());
    REQUIRE(run.until([&] { return !run.hud().hints().showing().has_value(); }, 60));
    CHECK(run.hud().hints().queued().empty());
    CHECK(hintsFrom(run.shown.order, at1) == std::vector<std::string>{"TT_1", "TT_4"});

    // Checkpoint 2: its objective, and none of checkpoint 1's texts.
    const std::size_t at2 = jumpTo(2);
    REQUIRE(run.until([&] { return run.shown.objective == "MS_C2_1"; }));
    CHECK(hintsFrom(run.shown.order, at2).empty());

    // Checkpoint 3, the owner's path: into Fox's box through his gate; his scene plays and is skipped.
    const std::size_t at3 = jumpTo(3);
    REQUIRE(run.until([&] { return run.shown.objective == "MS_C3_1"; }));
    const coney::anim::Vec3 fox = run.flagAt("fFoxTrigger");
    // **Known gap**: the walk along the street to his gate stalls at (-225.5, 241) and at (-204.7, 245.5), where no
    // collision triangle, door, object or human stands, so he is put at the gate (as the analyst's reference run and
    // the checkpoint 3 test put him at each objective) and walks in from there.
    const coney::anim::Vec3 gate = run.flagAt("fFoxGate");
    run.play()->teleportPlayer(
        coney::world_objects::Placement{.position = {gate.x, gate.y, gate.z}, .headingDegrees = 0.0F});
    run.wait(5);
    const bool atFox = run.walkTo(fox, [&] { return !run.padded(); }, kWalk, false);
    if (!atFox) {
        run.dump();
    }
    REQUIRE(atFox);
    REQUIRE(run.skipToPad());

    // 3.2: TT_5 asks for the Hold Up order: R2 held, the right stick down at 95 %.
    REQUIRE(run.until([&] { return run.readable("TT_5"); }));
    run.order(0.0F, -0.95F);
    CHECK(run.flow().state().characters.lastWarriorCommand.at(0) == 3);
    REQUIRE(run.until([&] { return run.shown.objective == "MS_C3_1a"; }));

    // 3.3: up to the roof by the level's streets and stairs (no teleport) and toward the beer bottles until the lesson
    // starts; the script walks him to his spot. The lesson is on the roof, at z 5.8.
    const coney::anim::Vec3 bottles = run.flagAt("fBottles");
    const bool inLesson = run.walkTo(bottles, [&] { return run.readable("TT_25"); });
    if (!inLesson) {
        run.dump();
    }
    REQUIRE(inLesson);
    CHECK(run.play()->player().human().position().z > 5.0F);
    REQUIRE(run.until([&] { return run.readable("TT_26"); }));
    run.pad.buttons = coney::pad::kTriangle;
    run.wait(1);
    run.pad.buttons = 0;
    REQUIRE(run.until([&] { return run.readable("TT_27"); }));
    run.pad.buttons = coney::pad::kL1;
    run.wait(1);
    run.pad.buttons = 0;
    REQUIRE(run.until([&] { return run.readable("TT_28"); }));
    REQUIRE(run.play()->throwAim().active());
    // The stick brings the arc onto the marker: it turns him to face it, then pitches the aim down a few steps a
    // frame until the arc meets it (HuIsAimingAt).
    const coney::anim::Vec3 target = run.flagAt("fBottleTarget");
    const bool aimed = run.until([&] {
        const coney::platform::PlayLevelMode& mode = *run.play();
        if (mode.player().human().script().aimedObject != 0) {
            run.pad.leftX = 0.0F;
            run.pad.leftY = 0.0F;
            return true;
        }
        const coney::combat::ThrowAimState& aim = mode.throwAim();
        const float off = coney::human::wrapAngle(
            coney::human::headingOf(coney::anim::subtract(target, aim.releasePoint())) - aim.heading());
        constexpr int kMostSteps = 60;
        constexpr float kFacing = 0.02F;
        constexpr int kPitchSteps = 4;
        const int turn =
            std::clamp(static_cast<int>(std::lround(-off / coney::combat::kAimRate)), -kMostSteps, kMostSteps);
        run.pad.leftX = aimStick(turn);
        run.pad.leftY = std::abs(off) < kFacing ? -aimStick(kPitchSteps) : 0.0F;
        return false;
    });
    if (!aimed) {
        run.dump();
    }
    REQUIRE(aimed);
    REQUIRE(run.until([&] { return run.readable("TT_29"); }));
    // Cross throws; the bottle breaks by the marker and Fox's second scene plays. The break clears the lesson's text.
    run.pad.buttons = coney::pad::kCross;
    run.wait(1);
    run.pad.buttons = 0;
    const bool broke =
        run.until([&] { return std::ranges::find(run.shown.scenes, "l80_c6_b") != run.shown.scenes.end(); });
    if (!broke) {
        run.dump();
    }
    REQUIRE(broke);
    CHECK_FALSE(run.hud().hints().showing().has_value());
    CHECK(run.hud().hints().queued().empty());
    const std::size_t afterBottle = run.shown.order.size();
    REQUIRE(run.skipToPad());

    // 3.4: TT_7 asks for the Wreck'em order (R2, the right stick down-left), and it is the next hint shown; no text
    // of the throw lesson comes back meanwhile, and the order clears it.
    const bool wreckHint = run.until([&] { return run.readable("TT_7"); });
    if (!wreckHint) {
        run.dump();
    }
    REQUIRE(wreckHint);
    CHECK(hintsFrom(run.shown.order, afterBottle) == std::vector<std::string>{"TT_7"});
    int lessonFrames = 0;
    run.until(
        [&] {
            lessonFrames += std::ranges::find(kThrowLesson, run.shown.hint) != kThrowLesson.end() ? 1 : 0;
            return false;
        },
        30);
    CHECK(lessonFrames == 0);
    run.order(-0.67F, -0.67F);
    REQUIRE(run.until([&] { return run.shown.objective == "MS_C3_3"; }));
    CHECK_FALSE(run.hud().hints().showing().has_value());
    CHECK(run.hud().hints().queued().empty());

    // Checkpoint 3 in order, as the analyst's run showed it; each counted the first time.
    const std::vector<std::string> expected{"MS_C3_1", "l80_c6", "TT_5",  "MS_C3_1a", "TT_25", "MS_C3_2", "TT_26",
                                            "TT_27",   "TT_28",  "TT_29", "l80_c6_b", "TT_7",  "MS_C3_3"};
    std::vector<std::string> seen;
    for (std::size_t i = at3; i < run.shown.order.size(); ++i) {
        const std::string& key = run.shown.order[i];
        if (std::ranges::find(expected, key) != expected.end() && std::ranges::find(seen, key) == seen.end()) {
            seen.push_back(key);
        }
    }
    if (seen != expected) {
        run.dump();
    }
    CHECK(seen == expected);

    // Checkpoint 4: its objective; nothing of checkpoint 3 (the store's objective, the throw lesson) carried over.
    const std::size_t at4 = jumpTo(4);
    REQUIRE(run.until([&] { return run.shown.objective == "MS_C4_1"; }));
    CHECK(hintsFrom(run.shown.order, at4).empty());
    CHECK(run.flow().scripts().errors() == 0);
    std::printf("  level80 from the story: checkpoints 1-4 jumped to, checkpoint 3 walked to the roof: %zu of %zu "
                "in order, %zu shown in all\n",
                seen.size(), expected.size(), run.shown.order.size());
}
