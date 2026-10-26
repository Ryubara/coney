// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that a punch or a kick takes the victim's health when it reaches him, not when
// it starts: the level plays as `coney --play-level level99` plays it (support/disc_session.h), a fighter of the debug
// menus' spawn stands 1.0 m ahead of player 1 facing him, and the player attacks with the pad (square, square, cross:
// S1, SS2 and the kick SSX3; cross alone: X1; cross held and square: the special 653). The update each attack's
// damage shows, counted from the first update its clip plays, must be the update the original showed it in the same
// set-up, and never before the attack's strike shapes are on: the original deals a free attack's damage only from the
// strike test, when a shape touches the victim, and shows the attack's pose from its first update, with no fade from
// the pose before it (docs/research/combat-moves.md#timing). It runs only when the environment variable CONEY_DISC
// names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

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

// The frame of play the fighter is spawned on (the intro scene long skipped), how far ahead of the player, m, and how
// many frames he is left to stand before the first press.
constexpr std::uint64_t kSpawnAt = 300;
constexpr float kSpawnAhead = 1.0F;
constexpr std::uint64_t kSettleFrames = 10;
// How long each scenario is watched after the first press, in frames.
constexpr int kWatchFrames = 50;

// One button held from `at` frames after the first press for `frames` frames.
struct Press {
    std::uint64_t at;
    std::uint64_t frames;
    std::uint16_t buttons;
};

// Player 1's pad: cross at 100 skips the intro scene, then the scenario's presses once start() names the first frame.
class PressDriver final : public coney::InputSource {
  public:
    explicit PressDriver(std::vector<Press> presses) : m_presses(std::move(presses)) {}

    coney::PortSamples sample(std::uint64_t frame) override {
        coney::PortSamples samples{};
        coney::PadSample& pad = samples[0];
        pad.connected = true;
        if (frame == 100) {
            pad.buttons = coney::pad::kCross;
        }
        if (!m_start) {
            return samples;
        }
        for (const Press& press : m_presses) {
            if (frame >= *m_start + press.at && frame < *m_start + press.at + press.frames) {
                pad.buttons |= press.buttons;
            }
        }
        return samples;
    }

    // The presses count from frame `frame` of the pad script.
    void start(std::uint64_t frame) { m_start = frame; }

  private:
    std::vector<Press> m_presses;
    std::optional<std::uint64_t> m_start;
};

// One attack of a scenario: its clip, and the update the original showed its damage in, from the clip's first.
struct Attack {
    std::uint32_t clip;
    int contact;
};

// What a scenario showed for one attack: the updates (from its clip's first) its shapes came on and its damage
// showed, -1 for never.
struct Seen {
    int shapesOn = -1;
    int damage = -1;
};

// Plays level99 checkpoint 1 with a fighter 1.0 m ahead, presses `presses`, and returns what each of `attacks` showed.
std::vector<Seen> play(const coney::io::Wad& wad, std::vector<Press> presses, const std::vector<Attack>& attacks) {
    PressDriver driver(std::move(presses));
    coney::test::DiscSession run(wad, driver);
    run.session().startAtLevel("level99", 1);
    REQUIRE(run.runUntilPlay());
    REQUIRE(run.runToPlayFrame(kSpawnAt));
    coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);
    const coney::anim::Vec3 at = coney::debug::spotAhead(play->playerFeet(), play->playerHeadingDegrees(), kSpawnAhead);
    REQUIRE(play->spawnFighter(at, play->playerHeadingDegrees() + 180.0F));
    const coney::human::Human& fighter = *play->fighters().humans().back().human;
    const coney::human::Human& player = play->player().human();
    run.run(kSettleFrames);
    driver.start(run.playFrames());

    // Each update: which attack's clip plays, when it began, and whether the fighter's health dropped.
    std::vector<Seen> seen(attacks.size());
    std::optional<std::size_t> current;
    int clipStart = 0;
    int health = fighter.health().value();
    for (int k = 0; k < kWatchFrames; ++k) {
        run.run(1);
        const std::uint32_t clip = player.animator().animId();
        for (std::size_t i = 0; i < attacks.size(); ++i) {
            if (attacks[i].clip == clip && current != i) {
                current = i;
                clipStart = k;
            }
        }
        if (current) {
            Seen& now = seen[*current];
            if (now.shapesOn < 0 && player.strikeShapes().anyOn()) {
                now.shapesOn = k - clipStart;
            }
            if (now.damage < 0 && fighter.health().value() < health) {
                now.damage = k - clipStart;
            }
        }
        health = fighter.health().value();
    }
    return seen;
}

// Checks each attack's damage against the original's update, and that it came once its shapes were on.
void check(std::string_view name, const std::vector<Attack>& attacks, const std::vector<Seen>& seen) {
    for (std::size_t i = 0; i < attacks.size(); ++i) {
        std::printf("%s: clip %u shapes on at update %d, damage at %d (the original's %d)\n", std::string(name).c_str(),
                    attacks[i].clip, seen[i].shapesOn, seen[i].damage, attacks[i].contact);
        INFO(name << ", clip " << attacks[i].clip);
        REQUIRE(seen[i].damage >= 0);
        CHECK(seen[i].shapesOn >= 0);
        CHECK(seen[i].damage >= seen[i].shapesOn);
        CHECK(seen[i].damage == attacks[i].contact);
    }
}

} // namespace

TEST_CASE("the disc's level99: a punch or kick's damage shows when it reaches the victim, as in the original",
          "[disc][story][combat]") {
    // Expected: combat-moves.md#runtime, confirmed (runtime)
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    using coney::pad::kCross;
    using coney::pad::kSquare;
    // Square, then square and cross in each window: S1 (12), SS2 (16), the kick SSX3 (17).
    const std::vector<Attack> chain{{12, 2}, {16, 4}, {17, 7}};
    check("square, square, cross", chain, play(*wad, {{0, 2, kSquare}, {8, 2, kSquare}, {16, 2, kCross}}, chain));
    // Cross tapped: X1 (11) on the release.
    const std::vector<Attack> cross{{11, 8}};
    check("cross", cross, play(*wad, {{0, 2, kCross}}, cross));
    // Cross held and square pressed: the special 653.
    const std::vector<Attack> special{{653, 7}};
    check("cross + square", special, play(*wad, {{0, 6, kCross}, {2, 2, kSquare}}, special));
}
