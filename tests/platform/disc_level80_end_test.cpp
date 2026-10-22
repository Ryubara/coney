// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the second story mission (`level80`) ends after its last fight
// (docs/research/scripting.md#level80, steps 4.1 to 4.4; issue 239: "nothing happens after the Destroyers"):
// checkpoint 4 played in the game session as `coney --play-level level80 --checkpoint 4` plays it
// (support/disc_session.h), driven by a pad that walks player 1 into Cochise and Swan's volume with the left stick at
// 70 %, skips their scene with cross, gives the attack order the hint asks for (R2 held, the right stick up-left at
// 95 %), then fights: the left stick at 50 % toward the nearest Destroyer still standing and square tapped while one is
// within 2.5 m. The fight is not the subject: after its time any Destroyer still standing is knocked out by the test
// (his health taken, as the fight's last blow takes it), so the test always reaches the end the script waits for; the
// pad then keeps swinging until MS_C4_2 is ticked off and 5 s more, as a player does. The last one down must complete
// MS_C4_2 and play l80_c3, l80_c3_a and l80_c3_b, whose end launches the mission's
// completion. It runs only when the environment variable CONEY_DISC names the disc and skips otherwise; it prints
// counts only (LEGAL.md).

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
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/gangs.h"
#include "core/input_script.h"
#include "core/pad.h"
#include "core/pads.h"
#include "fileio/wad.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/mission_complete_mode.h"
#include "hud/hint_box.h"
#include "hud/hud.h"
#include "human/combatant.h"
#include "human/human.h"
#include "human/player.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "scenes/scene_player.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "world_objects/flags.h"
#include "world_objects/volume_boxes.h"

namespace {

// How long one step may take: 60 s of frames.
constexpr std::uint64_t kWait = 1800;
// How long the pad fights before the test knocks out whoever still stands: 90 s of frames.
constexpr std::uint64_t kFightFrames = 2700;
// The fight's walk (a partial push, under the run's 0.95), and the reach within which square is tapped, m.
constexpr float kFightStick = 0.5F;
constexpr float kFightRange = 2.5F;
// The hints, objectives and scenes the end shows, by their keys in the level's text and the scene list
// (docs/research/scripting.md#level80, steps 4.1 to 4.4).
constexpr std::array<std::string_view, 2> kHintKeys{"TT_8", "TANDEM"};
constexpr std::array<std::string_view, 2> kObjectiveKeys{"MS_C4_1", "MS_C4_2"};
constexpr std::array<std::string_view, 4> kSceneNames{"l80_c7", "l80_c3", "l80_c3_a", "l80_c3_b"};

// Player 1's pad, set by the test each frame: the buttons held and the two sticks in [-1, 1] with y up.
class DrivenPad final : public coney::InputSource {
  public:
    std::uint16_t buttons = 0;
    float rightX = 0.0F;
    float rightY = 0.0F;
    float leftX = 0.0F;
    float leftY = 0.0F;

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
    // The raw byte for `value` as an input script writes it, past each axis's dead zone (core/pad.h).
    static std::uint8_t raw(float value) {
        return coney::stickByteFromPercent(static_cast<int>(std::lround(std::clamp(value, -1.0F, 1.0F) * 100.0F)));
    }
};

// The left stick, in [-1, 1] at `deflection`, that walks player 1 toward `to` under the camera shown: the stick is
// read against the camera's forward, x to its right.
std::array<float, 2> stickTowards(const coney::platform::PlayLevelMode& play, coney::anim::Vec3 to, float deflection) {
    const coney::anim::Vec3 way = coney::anim::subtract(to, play.player().human().position());
    coney::anim::Vec3 forward = coney::anim::subtract(play.cameraTarget(), play.cameraEye());
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

// The key among `keys` whose text a global of the level's scripts, or a field of one, holds; empty for none.
std::string keyOf(coney::script::LuaVm& vm, std::string_view text, std::span<const std::string_view> keys) {
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
    return find(vm.globals(), find, 1);
}

// What the run showed, in order: each hint, objective and scene when it first appears, by its key.
struct Shown {
    std::vector<std::string> order;
    std::string hint;
    std::string objective;
    std::string hintText;
    std::string objectiveText;

    // Records this frame's hint, objective line and playing scenes.
    void watch(coney::platform::GameSession& session) {
        coney::StartUpFlow& flow = session.flow();
        const coney::hud::Hud& hud = flow.hud();
        coney::script::LuaVm& vm = flow.scripts().vm();
        const std::optional<coney::hud::Hint>& showing = hud.hints().showing();
        if (const std::string text = showing ? showing->text : std::string(); text != hintText) {
            hintText = text;
            hint = text.empty() ? std::string() : keyOf(vm, text, kHintKeys);
            add(hint);
        }
        const std::optional<coney::hud::ChecklistLine>& line = hud.checklist().slots[0];
        if (const std::string text = line ? line->text : std::string(); text != objectiveText) {
            objectiveText = text;
            if (std::string key = text.empty() ? std::string() : keyOf(vm, text, kObjectiveKeys); !key.empty()) {
                objective = key;
                add(key);
            }
        }
        if (coney::scenes::SceneSystem* system = flow.context().scenes; system != nullptr) {
            for (const std::string_view name : kSceneNames) {
                const std::optional<std::uint32_t> id = system->idOf(name);
                if (id && system->state(*id) == coney::scenes::SceneState::Playing) {
                    add(std::string(name));
                }
            }
        }
    }

    // `key` added to the order the first time it shows.
    void add(const std::string& key) {
        if (!key.empty() && std::ranges::find(order, key) == order.end()) {
            order.push_back(key);
        }
    }
};

} // namespace

TEST_CASE("the disc's level80 checkpoint 4: the last Destroyer down ends the mission", "[disc][story][level80-end]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    DrivenPad pad;
    coney::test::DiscSession run(*wad, pad);
    coney::platform::GameSession& session = run.session();
    Shown shown;
    const auto step = [&](std::uint64_t frames) {
        for (std::uint64_t i = 0; i < frames; ++i) {
            run.run(1);
            shown.watch(session);
        }
    };
    const auto until = [&](const std::function<bool()>& done, std::uint64_t limit) {
        for (std::uint64_t i = 0; i < limit && !done(); ++i) {
            step(1);
        }
        return done();
    };
    const auto padded = [&session] { return session.play() != nullptr && session.play()->player().padControlled(); };
    // Taps cross every second until player 1 has the pad again (a scene skipped).
    const auto skipScene = [&] {
        constexpr std::uint64_t kTapEvery = 30;
        for (std::uint64_t i = 0; i < kWait; i += kTapEvery) {
            if (i > 0 && padded()) {
                return true;
            }
            pad.buttons = coney::pad::kCross;
            step(1);
            pad.buttons = 0;
            step(kTapEvery - 1);
        }
        return padded();
    };
    // A flag's place, by the name of the scripts' global holding its handle.
    const auto flagAt = [&session](std::string_view name) {
        const std::optional<double> handle = session.flow().scripts().vm().global(std::string(name)).number();
        REQUIRE(handle.has_value());
        const coney::world_objects::WorldFlag* flag = session.flow().flags().find(handle.value_or(0.0));
        REQUIRE(flag != nullptr);
        return coney::anim::Vec3{flag->position[0], flag->position[1], flag->position[2]};
    };
    // The Destroyers: the members of every gang that is player 1's gang's enemy (two gangs at this checkpoint).
    const auto destroyers = [&session] {
        std::vector<coney::ai::Brain*> found;
        const coney::ai::Brains* brains = session.gameplay().brains();
        if (brains == nullptr) {
            return found;
        }
        // The test knocks some out through the brains gameplay owns.
        auto& owned = const_cast<coney::ai::Brains&>(*brains);
        const coney::ai::Gang* own = nullptr;
        for (std::size_t i = 0; i < owned.size(); ++i) {
            if (owned.at(i).type() == coney::ai::BrainType::Player) {
                own = owned.at(i).gang();
            }
        }
        for (std::size_t i = 0; i < owned.size(); ++i) {
            coney::ai::Brain& brain = owned.at(i);
            if (brain.gang() != nullptr && own != nullptr && brain.gang() != own &&
                coney::ai::Gangs::enemies(own, brain.gang())) {
                found.push_back(&brain);
            }
        }
        return found;
    };
    const auto standing = [](const coney::ai::Brain* brain) {
        return !brain->human().fighter().health().depleted() &&
               brain->human().state() != coney::human::TargetState::Grounded;
    };
    // On a failure: what showed, the Destroyers' state and the log.
    const auto dump = [&] {
        for (const std::string& key : shown.order) {
            UNSCOPED_INFO("shown " << key);
        }
        for (const coney::ai::Brain* brain : destroyers()) {
            UNSCOPED_INFO("destroyer gang " << brain->gang()->id() << " health "
                                            << brain->human().fighter().health().value() << " standing "
                                            << standing(brain) << " suspended " << brain->suspended());
        }
        UNSCOPED_INFO("checkpoint " << session.flow().state().checkPoint << " script errors "
                                    << session.flow().scripts().errors());
        for (const std::string& line : run.log()) {
            UNSCOPED_INFO(line);
        }
    };

    // 4.1: the chapter fades in; player 1 walks into Cochise and Swan's volume and their scene (l80_c7) is skipped.
    session.startAtLevel("level80", 4);
    REQUIRE(run.runUntilPlay());
    REQUIRE(until(padded, kWait));
    REQUIRE(until([&shown] { return shown.objective == "MS_C4_1"; }, kWait));
    step(30);
    // Teleported to 3 m short of the meeting's volume, as the reference run teleported to each objective, then walked
    // to its middle.
    const std::optional<double> volume =
        session.flow().scripts().vm().global(std::string("vCochiseSwanTrigger")).number();
    REQUIRE(volume.has_value());
    REQUIRE(session.flow().context().boxes != nullptr);
    const coney::world_objects::VolumeBox* box = session.flow().context().boxes->find(volume.value_or(0.0));
    REQUIRE(box != nullptr);
    const coney::anim::Vec3 meeting{(box->low[0] + box->high[0]) / 2.0F, (box->low[1] + box->high[1]) / 2.0F,
                                    box->low[2]};
    const coney::anim::Vec3 flag = flagAt("fCochiseSwan");
    session.play()->teleportPlayer(coney::world_objects::Placement{
        .position = {flag.x, std::max(box->low[1], box->high[1]) + 1.0F, flag.z}, .headingDegrees = 180.0F});
    step(5);
    const bool met = until(
        [&] {
            if (!padded()) {
                return true;
            }
            const auto [x, y] = stickTowards(*session.play(), meeting, 0.7F);
            pad.leftX = x;
            pad.leftY = y;
            return false;
        },
        kWait);
    pad.leftX = 0.0F;
    pad.leftY = 0.0F;
    if (!met) {
        const coney::anim::Vec3 at = session.play()->player().human().position();
        UNSCOPED_INFO("flag z " << flag.z << " player at " << at.x << " " << at.y << " " << at.z << " box "
                                << box->low[0] << " " << box->low[1] << " " << box->low[2] << " to " << box->high[0]
                                << " " << box->high[1] << " " << box->high[2] << " kind " << box->kind << " enabled "
                                << box->enabled << " turn " << box->turn[0] << " " << box->turn[1]);
        dump();
    }
    REQUIRE(met);
    REQUIRE(skipScene());

    // 4.2: TT_8 asks for the attack order: R2 held, the right stick up-left, R2 let go. The order is given as soon as
    // the hint is up, whether or not a scroll-in message covers its box: Cochise and Swan's own fight may end, and they
    // join the crew, before the box shows (inferred: their fight goals end at their deadline,
    // docs/research/ai.md#fight).
    const bool asked = until([&] { return shown.hint == "TT_8" && session.flow().hud().visible(); }, kWait);
    if (!asked) {
        dump();
    }
    REQUIRE(asked);
    pad.buttons = coney::pad::kR2;
    step(2);
    pad.rightX = -0.67F;
    pad.rightY = 0.67F;
    step(6);
    pad.buttons = 0;
    step(1);
    pad.rightX = 0.0F;
    pad.rightY = 0.0F;
    const bool ordered = until([&shown] { return shown.objective == "MS_C4_2"; }, kWait);
    if (!ordered) {
        dump();
    }
    REQUIRE(ordered);
    const std::size_t foes = destroyers().size();
    CHECK(foes == 12);

    // The fight through the pad: toward the nearest Destroyer standing, square while one is in reach.
    std::uint64_t frame = 0;
    const auto anyStanding = [&] { return std::ranges::any_of(destroyers(), standing); };
    until(
        [&] {
            ++frame;
            pad.buttons = 0;
            pad.leftX = 0.0F;
            pad.leftY = 0.0F;
            if (frame > kFightFrames || !anyStanding() || !padded()) {
                return true;
            }
            const coney::anim::Vec3 feet = session.play()->player().human().position();
            std::optional<coney::anim::Vec3> nearest;
            for (const coney::ai::Brain* brain : destroyers()) {
                const coney::anim::Vec3 at = brain->human().position();
                if (standing(brain) && (!nearest || std::hypot(at.x - feet.x, at.y - feet.y) <
                                                        std::hypot(nearest->x - feet.x, nearest->y - feet.y))) {
                    nearest = at;
                }
            }
            if (nearest && std::hypot(nearest->x - feet.x, nearest->y - feet.y) > kFightRange) {
                const auto [x, y] = stickTowards(*session.play(), *nearest, kFightStick);
                pad.leftX = x;
                pad.leftY = y;
            } else if (frame % 10 == 0) {
                pad.buttons = coney::pad::kSquare;
            }
            return false;
        },
        kFightFrames + 1);
    pad.buttons = 0;
    pad.leftX = 0.0F;
    pad.leftY = 0.0F;
    int byPad = 0;
    for (const coney::ai::Brain* brain : destroyers()) {
        byPad += standing(brain) ? 0 : 1;
    }
    // The rest knocked out by the test, one every half second, as the fight's last blows would.
    for (coney::ai::Brain* brain : destroyers()) {
        if (standing(brain)) {
            brain->human().fighter().health().set(0);
            step(15);
        }
    }
    // Square kept up after the last blow, until MS_C4_2 is ticked off and for 5 s more, as a player still swinging
    // does: each swing's grunt is a vocal line over Cleon's closing line (HuSpeakNI with the callback FadeOut). Issue
    // 239: Coney's grunt stopped that line and dropped its callback, so the fade and the closing scenes never came.
    const auto ticked = [&session] {
        const std::optional<coney::hud::ChecklistLine>& line = session.flow().hud().checklist().slots[0];
        return line.has_value() && line->marked;
    };
    constexpr std::uint64_t kSwingAfter = 150;
    std::uint64_t swing = 0;
    std::uint64_t swingsAfter = 0;
    while (swing < kWait && swingsAfter < kSwingAfter) {
        pad.buttons = swing % 10 == 0 ? coney::pad::kSquare : 0;
        step(1);
        ++swing;
        swingsAfter += ticked() ? 1 : 0;
    }
    pad.buttons = 0;
    CHECK(ticked());

    // 4.4: the last one down completes MS_C4_2; the three closing scenes play (each skipped) and the last one's end
    // launches the mission's completion.
    const std::uint64_t launchesBefore = session.flow().missionComplete().launches();
    const auto ended = [&] { return session.flow().missionComplete().launches() > launchesBefore; };
    const bool complete = until(
        [&] {
            if (ended()) {
                return true;
            }
            // Cross every second skips whichever closing scene plays.
            pad.buttons = run.frames() % 30 == 0 ? coney::pad::kCross : 0;
            return false;
        },
        kWait * 2);
    pad.buttons = 0;
    if (!complete) {
        dump();
    }
    CHECK(complete);
    std::vector<std::string> seen;
    for (const std::string& key : shown.order) {
        if (!key.starts_with("TT_") && key != "TANDEM") {
            seen.push_back(key);
        }
    }
    const std::vector<std::string> expected{"MS_C4_1", "l80_c7", "MS_C4_2", "l80_c3", "l80_c3_a", "l80_c3_b"};
    CHECK(seen == expected);
    CHECK(session.flow().scripts().errors() == 0);
    std::printf("  level80 checkpoint 4: %zu Destroyers, %d down by the pad; %zu objectives and scenes in order, "
                "mission complete %d\n",
                foes, byPad, seen.size(), complete ? 1 : 0);
}
