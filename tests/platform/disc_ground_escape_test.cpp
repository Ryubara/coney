// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that a downed player held down by an AI breaks out and counters
// (docs/research/combat-reactions.md#ground-escape), in a level as `coney --play-level level99` plays it
// (support/disc_session.h). A crushing special (653) from his left knocks player 1 down; once he lies in the grounded
// idle a fighter of the debug menus' spawn stands 0.8 m to his left and its brain queues its own grounded attack kind
// 14 (the circle held, command 0xe), which mounts him with the grounded mount (237-240 on the fighter, 241 on him).
// Mounted (the fighter's brain then suspended, as the runtime's puppet), the pad mashes square: each press plays the
// struggle 250 and drains the fighter's power until he breaks out with 246; once 246 has ended one square strikes the
// fighter. It runs only when the environment variable CONEY_DISC
// names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "ai/ai_humans.h"
#include "ai/attack_action.h"
#include "ai/brain.h"
#include "ai/brains.h"
#include "combat/attacks.h"
#include "combat/being_hit.h"
#include "core/pad.h"
#include "core/pads.h"
#include "debug/play_controls.h"
#include "fileio/wad.h"
#include "human/human.h"
#include "human/victim.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"

namespace {

// The frame of play the fighter is spawned on (the intro scene long skipped), and how far to the player's left, m.
constexpr std::uint64_t kSpawnAt = 300;
constexpr float kSpawnLeft = 0.8F;
// The clips (docs/references/anim-ids.md): the ground's loop, the grounded mount and its reaction, the mounted idle,
// the struggle and the break.
constexpr std::uint32_t kGroundedIdle = 196;
constexpr std::uint32_t kFirstGroundedMount = 237;
constexpr std::uint32_t kLastGroundedMount = 240;
constexpr std::uint32_t kGroundedMountReact = 241;
constexpr std::uint32_t kMountedIdle = 207;
constexpr std::uint32_t kStruggle = 250;
constexpr std::uint32_t kBreak = 246;
// The AI's grounded attack kind that presses circle held (docs/research/ai/index.md#attack-kinds).
constexpr int kMountKind = 14;
// How long each stage may take, in frames (10 s).
constexpr std::uint64_t kLimit = 300;

// Player 1's pad: square tapped every 6 frames while mashing, and one square tapped on the frame the test names. The
// intro scene is skipped with cross at 100.
class EscapeDriver final : public coney::InputSource {
  public:
    coney::PortSamples sample(std::uint64_t frame) override {
        coney::PortSamples samples{};
        coney::PadSample& pad = samples[0];
        pad.connected = true;
        ++m_samples;
        if (frame == 100) {
            pad.buttons = coney::pad::kCross;
        }
        constexpr std::uint64_t kMashEvery = 6;
        if ((mashing && m_samples % kMashEvery == 0) || m_samples == tapOn) {
            pad.buttons |= coney::pad::kSquare;
        }
        return samples;
    }

    // The samples taken so far: the next one is the next update's.
    [[nodiscard]] std::uint64_t samples() const { return m_samples; }

    bool mashing = false;
    std::uint64_t tapOn = 0;

  private:
    std::uint64_t m_samples = 0;
};

// The player's brain among the session's brains.
coney::ai::Brain* playerBrain(coney::test::DiscSession& run) {
    const coney::ai::Brains* found = run.session().gameplay().brains();
    if (found == nullptr) {
        return nullptr;
    }
    auto& brains = const_cast<coney::ai::Brains&>(*found);
    for (std::size_t i = 0; i < brains.size(); ++i) {
        if (brains.at(i).type() == coney::ai::BrainType::Player) {
            return &brains.at(i);
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("the disc's level99: knocked down and mounted by a fighter, the player struggles out and counters",
          "[disc][combat]") {
    // Expected: combat-reactions.md#ground-escape, confirmed (runtime)
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    EscapeDriver driver;
    coney::test::DiscSession run(*wad, driver);
    run.session().startAtLevel("level99", 1);
    REQUIRE(run.runUntilPlay());
    REQUIRE(run.runToPlayFrame(kSpawnAt));
    coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);
    coney::ai::Brain* player = playerBrain(run);
    REQUIRE(player != nullptr);
    coney::human::Human& human = player->human();

    // 1. The crushing special from his left: down and stunned (docs/research/combat-reactions.md#reactions).
    coney::human::IncomingHit hit;
    hit.damage = 50;
    hit.attackAnim = 653;
    hit.code = 0x39;
    hit.flags = 0x400;
    hit.attacker = coney::debug::spotAhead(play->playerFeet(), play->playerHeadingDegrees() + 90.0F, kSpawnLeft);
    human.takeHit(hit);
    REQUIRE(run.runUntil([&] { return human.animator().animId() == kGroundedIdle; }, kLimit));

    // 2. The fighter at his left as he lies, facing him; its brain takes him as its target and queues its grounded
    // attack, the circle held.
    const float heading = play->playerHeadingDegrees();
    const coney::anim::Vec3 at = coney::debug::spotAhead(play->playerFeet(), heading + 90.0F, kSpawnLeft);
    REQUIRE(play->spawnFighter(at, heading - 90.0F));
    coney::human::Human& fighter = *play->fighters().humans().back().human;
    coney::ai::Brain* brain = play->fighters().brainOf(fighter);
    REQUIRE(brain != nullptr);
    brain->setTarget(player);
    REQUIRE(brain->queueAction(std::make_unique<coney::ai::AttackAction>(kMountKind, 0)));
    std::uint32_t mountClip = 0;
    bool reacted = false;
    const bool mounted = run.runUntil(
        [&] {
            const std::uint32_t clip = fighter.animator().animId();
            if (clip >= kFirstGroundedMount && clip <= kLastGroundedMount) {
                mountClip = clip;
            }
            reacted = reacted || human.animator().animId() == kGroundedMountReact;
            return human.animator().animId() == kMountedIdle;
        },
        kLimit);
    std::printf("ground escape: mounted %d with %u, reaction 241 %d, player state %d\n", mounted ? 1 : 0, mountClip,
                reacted ? 1 : 0, static_cast<int>(human.state()));
    REQUIRE(mounted);
    CHECK(mountClip != 0);
    CHECK(reacted);
    CHECK(human.state() == coney::human::TargetState::Mounted);

    // 3. The fighter's brain is suspended (BrSuspend), as the runtime's puppet acted only on the commands it was given:
    // its own mount strikes and its pick-up to the hold would close his input. The mash: each square struggles (250),
    // draining the fighter's power, until he breaks out (246).
    brain->setSuspended(true);
    const int fighterHealth = fighter.fighter().health().value();
    int struggles = 0;
    std::uint32_t lastClip = human.animator().animId();
    driver.mashing = true;
    const bool broke = run.runUntil(
        [&] {
            const std::uint32_t clip = human.animator().animId();
            if (clip == kStruggle && lastClip != kStruggle) {
                ++struggles;
            }
            lastClip = clip;
            return clip == kBreak;
        },
        kLimit);
    driver.mashing = false;
    const int struggleDamage = fighterHealth - fighter.fighter().health().value();
    std::printf("ground escape: broke out %d after %d struggles, the fighter lost %d health\n", broke ? 1 : 0,
                struggles, struggleDamage);
    REQUIRE(broke);
    CHECK(struggles > 0);
    CHECK(struggleDamage > 0);

    // 4. Free once 246 ends: one square counters on the fighter.
    REQUIRE(run.runUntil([&] { return human.animator().animId() != kBreak; }, kLimit));
    CHECK(human.state() == coney::human::TargetState::Standing);
    const int beforeCounter = fighter.fighter().health().value();
    driver.tapOn = driver.samples() + 1;
    run.run(30);
    const int counterDamage = beforeCounter - fighter.fighter().health().value();
    std::printf("ground escape: the counter's square took %d health\n", counterDamage);
    CHECK(counterDamage > 0);
}
