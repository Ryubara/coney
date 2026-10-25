// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the sprint's moving attacks play out when they strike a human, in a level
// as `coney --play-level level99` plays it (support/disc_session.h): a fighter of the debug menus' spawn stands ahead
// of player 1, who sprints at him (L2 held, the left stick at 98 % toward him through the camera, as a pad would) and
// presses cross (the charge, clip 0) or square (the dive, clip 1) within 3 m of him. The fighter takes the crushing
// hit and is flung; the attacker's clip plays to its end with no reaction of his own, as the original's held flags
// `0x1c16a40` keep any hit from making a charger or a diver react (docs/research/combat-reactions.md#protected-moves).
// It runs only when the environment variable CONEY_DISC names the disc and skips otherwise; it prints counts only
// (LEGAL.md).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "core/input_script.h"
#include "core/pad.h"
#include "core/pads.h"
#include "debug/play_controls.h"
#include "fileio/wad.h"
#include "human/human.h"
#include "human/player.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"

namespace {

// The frame of play the fighter is spawned on (the intro scene long skipped) and how far ahead of the player, m.
constexpr std::uint64_t kSpawnAt = 300;
constexpr float kSpawnAhead = 9.0F;
// How near the fighter the button is pressed, m: inside the charge's 6.7 m run and the dive's far range.
constexpr float kPressWithin = 3.0F;
// The stick's deflection toward the fighter: past the run's 0.95, short of full.
constexpr float kSprintStick = 0.98F;
// How long the run is watched after the spawn, in frames (6 s).
constexpr std::uint64_t kWatchFrames = 180;
// The charge (L2 + cross) and the dive (L2 + square) clips (docs/research/combat/index.md#run-attacks).
constexpr std::uint32_t kCharge = 0;
constexpr std::uint32_t kDive = 1;
// The hit reactions, light to extreme and the dying set (docs/research/combat/index.md#hit-codes).
constexpr std::uint32_t kFirstReaction = 268;
constexpr std::uint32_t kLastReaction = 315;

// Player 1's pad: nothing until the fighter stands, then L2 held and the left stick toward him at kSprintStick, and the
// attack's button held for two frames once he is within kPressWithin. The intro scene is skipped with cross at 100.
class SprintDriver final : public coney::InputSource {
  public:
    // `button` is the attack's (pad::kCross or pad::kSquare); `play` gives the play mode once the level plays.
    SprintDriver(std::uint16_t button, std::function<coney::platform::PlayLevelMode*()> play)
        : m_button(button), m_play(std::move(play)) {}

    coney::PortSamples sample(std::uint64_t frame) override {
        coney::PortSamples samples{};
        coney::PadSample& pad = samples[0];
        pad.connected = true;
        if (frame == 100) {
            pad.buttons = coney::pad::kCross;
        }
        const coney::platform::PlayLevelMode* play = m_play();
        if (play == nullptr || !m_target.has_value()) {
            return samples;
        }
        const coney::human::Player& player = play->player();
        const coney::anim::Vec3 feet = player.human().position();
        const float dx = m_target->x - feet.x;
        const float dy = m_target->y - feet.y;
        if (m_pressed < 2) {
            steer(pad, player, dx, dy);
        }
        pad.buttons |= coney::pad::kL2;
        if (m_pressed < 2 && (m_pressed > 0 || std::hypot(dx, dy) < kPressWithin)) {
            pad.buttons |= m_button;
            ++m_pressed;
        }
        return samples;
    }

    // Starts the run at the fighter standing at `at`.
    void runAt(coney::anim::Vec3 at) { m_target = at; }

  private:
    // Sets the left stick toward (dx, dy) at kSprintStick, turned by the shown camera's heading: up is the camera's
    // view along the ground, right its right.
    static void steer(coney::PadSample& pad, const coney::human::Player& player, float dx, float dy) {
        const coney::human::PlayerSnapshot& shot = player.current();
        float fx = shot.cameraTarget.x - shot.cameraEye.x;
        float fy = shot.cameraTarget.y - shot.cameraEye.y;
        const float forward = std::hypot(fx, fy);
        const float wanted = std::hypot(dx, dy);
        if (forward < 1e-4F || wanted < 1e-4F) {
            return;
        }
        fx /= forward;
        fy /= forward;
        const float up = ((dx * fx) + (dy * fy)) / wanted;
        const float right = ((dx * fy) - (dy * fx)) / wanted;
        pad.sticks[2] = raw(right * kSprintStick);
        pad.sticks[3] = raw(-up * kSprintStick);
    }

    // The raw byte for `value` in [-1, 1] as an input script writes it (pad::stickValue reads it back).
    static std::uint8_t raw(float value) {
        return coney::stickByteFromPercent(static_cast<int>(std::lround(std::clamp(value, -1.0F, 1.0F) * 100.0F)));
    }

    std::uint16_t m_button;
    std::function<coney::platform::PlayLevelMode*()> m_play;
    std::optional<coney::anim::Vec3> m_target;
    int m_pressed = 0;
};

// What a run left: the player's clip each frame after the spawn, his health and the fighter's hits.
struct Run {
    std::vector<std::uint32_t> clips;
    int healthBefore = 0;
    int healthAfter = 0;
    int fighterHits = 0;
};

// Plays level99 checkpoint 1 with a fighter spawned ahead and the sprint attack by `button`.
Run runSprintAttack(const coney::io::Wad& wad, std::uint16_t button) {
    coney::test::DiscSession* session = nullptr;
    SprintDriver driver(button, [&session]() -> coney::platform::PlayLevelMode* {
        return session != nullptr ? session->session().play() : nullptr;
    });
    coney::test::DiscSession run(wad, driver);
    session = &run;
    run.session().startAtLevel("level99", 1);
    REQUIRE(run.runUntilPlay());
    REQUIRE(run.runToPlayFrame(kSpawnAt));
    coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);
    const coney::anim::Vec3 at = coney::debug::spotAhead(play->playerFeet(), play->playerHeadingDegrees(), kSpawnAhead);
    const std::size_t before = play->fighterCount();
    REQUIRE(play->spawnFighter(at, play->playerHeadingDegrees() + 180.0F));
    REQUIRE(play->fighterCount() == before + 1);
    const coney::human::Human& fighter = *play->fighters().humans().back().human;
    driver.runAt(fighter.position());
    Run result;
    const coney::human::Human& human = play->player().human();
    result.healthBefore = human.health().value();
    const int hitsBefore = fighter.fighter().hitsTaken();
    for (std::uint64_t i = 0; i < kWatchFrames; ++i) {
        run.run(1);
        result.clips.push_back(human.animator().animId());
    }
    result.healthAfter = human.health().value();
    result.fighterHits = fighter.fighter().hitsTaken() - hitsBefore;
    return result;
}

// The first frame playing `clip`, or -1.
int firstOf(const std::vector<std::uint32_t>& clips, std::uint32_t clip) {
    const auto found = std::ranges::find(clips, clip);
    return found == clips.end() ? -1 : static_cast<int>(found - clips.begin());
}

// The frames `clip` plays from `from` on, unbroken.
int lengthFrom(const std::vector<std::uint32_t>& clips, std::uint32_t clip, int from) {
    int length = 0;
    for (auto k = static_cast<std::size_t>(from); k < clips.size() && clips[k] == clip; ++k) {
        ++length;
    }
    return length;
}

// Whether `clips` plays a hit reaction from `from` on.
bool reactsFrom(const std::vector<std::uint32_t>& clips, int from) {
    return std::any_of(clips.begin() + from, clips.end(),
                       [](std::uint32_t clip) { return clip >= kFirstReaction && clip <= kLastReaction; });
}

} // namespace

TEST_CASE("the disc's level99: a sprint charge or dive into a human plays out with no reaction of the attacker's",
          "[disc][story][combat]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    struct Case {
        std::string_view name;
        std::uint16_t button;
        std::uint32_t clip;
        int minimumFrames; // the clip's length on the disc, less a few frames of fade
    };
    for (const Case& c :
         {Case{"charge", coney::pad::kCross, kCharge, 24}, Case{"dive", coney::pad::kSquare, kDive, 55}}) {
        INFO("L2 + " << (c.button == coney::pad::kCross ? "cross" : "square") << ": the " << c.name);
        const Run run = runSprintAttack(*wad, c.button);
        const int start = firstOf(run.clips, c.clip);
        const int length = start >= 0 ? lengthFrom(run.clips, c.clip, start) : 0;
        std::printf("sprint %s: starts %d frames after the spawn, plays %d frames, fighter hit %d times, player health "
                    "%d -> %d, reaction %s\n",
                    std::string(c.name).c_str(), start, length, run.fighterHits, run.healthBefore, run.healthAfter,
                    start >= 0 && reactsFrom(run.clips, start) ? "yes" : "no");
        REQUIRE(start >= 0);
        CHECK(run.fighterHits >= 1);
        CHECK(length >= c.minimumFrames);
        CHECK_FALSE(reactsFrom(run.clips, start));
    }
}
