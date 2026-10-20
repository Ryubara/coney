// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc the counter from the rise (docs/research/combat-reactions.md#ground-escape), in
// a level as `coney --play-level level99` plays it (support/disc_session.h). First the clip the player's set plays for
// anim 199: its events 0x25 and 0x48 by frame (counts only), and the updates of the rise on which Coney fires its 0x25
// events before the recovery, which must be the runtime's k = 15-17. Then the player's path: a crushing special (653)
// from his left knocks player 1 down, a fighter of the debug menus' spawn stands 0.8 m to his left as he lies, the pad
// mashes square every 4 frames while he lies in 196, as the runtime's scenario did, and one square is tapped so that
// it reaches him on update k of his rise. At k = 16 he counters with one of 617-620 and the fighter takes the hit; at k
// = 18 nothing plays. It runs only when the environment variable CONEY_DISC names the disc and skips otherwise; it
// prints counts only (LEGAL.md).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/brains.h"
#include "animation/anim_clip.h"
#include "animation/anim_task.h"
#include "combat/attacks.h"
#include "combat/being_hit.h"
#include "core/pad.h"
#include "core/pads.h"
#include "debug/play_controls.h"
#include "fileio/wad.h"
#include "human/human.h"
#include "human/player.h"
#include "human/victim.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"

namespace {

// The frame of play the fighter is spawned on (the intro scene long skipped), and how far to the player's left, m.
constexpr std::uint64_t kSpawnAt = 300;
constexpr float kSpawnLeft = 0.8F;
// The rise, the ground's loop and the counters (docs/references/anim-ids.md).
constexpr std::uint32_t kRise = 199;
constexpr std::uint32_t kGroundedIdle = 196;
constexpr int kFirstCounter = 617;
constexpr int kLastCounter = 620;
// How long the knockdown and the rise may take, in frames (10 s).
constexpr std::uint64_t kLimit = 300;

// Player 1's pad: square tapped every 4 frames while he lies in the ground's loop (the mash), and one square tapped on
// the frame the test names; nothing else. The intro scene is skipped with cross at 100.
class RiseDriver final : public coney::InputSource {
  public:
    coney::PortSamples sample(std::uint64_t frame) override {
        coney::PortSamples samples{};
        coney::PadSample& pad = samples[0];
        pad.connected = true;
        ++m_samples;
        if (frame == 100) {
            pad.buttons = coney::pad::kCross;
        }
        constexpr std::uint64_t kMashEvery = 4;
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

// What one run left.
struct Run {
    int counterK = -1;         // the update of the rise the counter started on, or -1
    int counter = -1;          // which of 617-620
    int fighterHealthLost = 0; // the fighter's health the counter took
    bool rose = false;         // the rise was reached
    float distanceAtPress = 0; // the fighter's distance from him at the press, m
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

// Plays level99 checkpoint 1: a crushing special from the player's left knocks him down; once he lies in 196 a fighter
// is spawned 0.8 m to his left (the knockdown's root motion carries him away from where the hit came from), he
// mashes, and square reaches him on update `k` of his rise.
Run riseCounterAt(const coney::io::Wad& wad, int k) {
    RiseDriver driver;
    coney::test::DiscSession run(wad, driver);
    run.session().startAtLevel("level99", 1);
    REQUIRE(run.runUntilPlay());
    REQUIRE(run.runToPlayFrame(kSpawnAt));
    coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);
    coney::ai::Brain* player = playerBrain(run);
    REQUIRE(player != nullptr);
    coney::human::Human& human = player->human();
    // The crushing special from his left: down and stunned (docs/research/combat-reactions.md#reactions).
    coney::human::IncomingHit hit;
    hit.damage = 50;
    hit.attackAnim = 653;
    hit.code = 0x39;
    hit.flags = 0x400;
    hit.attacker = coney::debug::spotAhead(play->playerFeet(), play->playerHeadingDegrees() + 90.0F, kSpawnLeft);
    human.takeHit(hit);
    Run result;
    if (!run.runUntil([&] { return human.animator().animId() == kGroundedIdle; }, kLimit)) {
        return result;
    }
    // The fighter at his left as he lies, facing him.
    const float heading = play->playerHeadingDegrees();
    const coney::anim::Vec3 at = coney::debug::spotAhead(play->playerFeet(), heading + 90.0F, kSpawnLeft);
    REQUIRE(play->spawnFighter(at, heading - 90.0F));
    const coney::human::Human& fighter = *play->fighters().humans().back().human;
    // The mash while he lies in 196, until the rise starts (k = 0).
    const bool rose = run.runUntil(
        [&] {
            driver.mashing = human.animator().animId() == kGroundedIdle;
            return human.animator().animId() == kRise;
        },
        kLimit);
    driver.mashing = false;
    if (!rose) {
        return result;
    }
    result.rose = true;
    // The sample after this one is k = 1's.
    driver.tapOn = driver.samples() + static_cast<std::uint64_t>(k);
    const int healthBefore = fighter.health().value();
    for (int update = 1; update <= 40; ++update) {
        run.run(1);
        const auto clip = static_cast<int>(human.animator().animId());
        if (update == k) {
            result.distanceAtPress =
                std::hypot(fighter.position().x - human.position().x, fighter.position().y - human.position().y);
        }
        if (result.counterK < 0 && clip >= kFirstCounter && clip <= kLastCounter) {
            result.counterK = update;
            result.counter = clip;
        }
    }
    result.fighterHealthLost = healthBefore - fighter.health().value();
    return result;
}

} // namespace

TEST_CASE("the disc's rise carries the counter's events, and Coney's window from it is the runtime's k = 15-17",
          "[disc][combat]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    RiseDriver driver;
    coney::test::DiscSession run(*wad, driver);
    run.session().startAtLevel("level99", 1);
    REQUIRE(run.runUntilPlay());
    coney::ai::Brain* player = playerBrain(run);
    REQUIRE(player != nullptr);
    const coney::human::HumanAnimator& animator = player->human().animator();
    const coney::anim::AnimClip* clip = animator.clip(kRise);
    REQUIRE(clip != nullptr);
    // The events by frame: 0x25 on 11-18, 0x48 on 14.
    std::vector<int> counterFrames;
    std::vector<int> recoveryFrames;
    for (const coney::anim::ClipEvent& event : clip->events) {
        if (event.type == coney::combat::kDuckCounterEvent) {
            counterFrames.push_back(event.frame);
        } else if (event.type == coney::anim::kEventRecovery) {
            recoveryFrames.push_back(event.frame);
        }
    }
    // The updates of the rise on which a 0x25 fires before the recovery has: the clip's time at update k is
    // k × rate / 30 s, as the task stack advances it.
    const float rate = player->human().anims().rate(kRise);
    std::vector<int> window;
    bool recovered = false;
    for (int k = 1; k <= 30 && !recovered; ++k) {
        const float before = static_cast<float>(k - 1) * rate / 30.0F;
        const float after = static_cast<float>(k) * rate / 30.0F;
        recovered = coney::combat::eventBetween(*clip, coney::anim::kEventRecovery, before, after);
        if (!recovered && coney::combat::eventBetween(*clip, coney::combat::kDuckCounterEvent, before, after)) {
            window.push_back(k);
        }
    }
    std::printf("rise 199: %.2f s at rate %.2f, %zu events, 0x25 on %zu frames (%d-%d), 0x48 on %zu (%d); window k = "
                "%d-%d (%zu updates)\n",
                clip->duration, rate, clip->events.size(), counterFrames.size(),
                counterFrames.empty() ? -1 : counterFrames.front(), counterFrames.empty() ? -1 : counterFrames.back(),
                recoveryFrames.size(), recoveryFrames.empty() ? -1 : recoveryFrames.front(),
                window.empty() ? -1 : window.front(), window.empty() ? -1 : window.back(), window.size());
    CHECK(counterFrames == std::vector<int>{11, 12, 13, 14, 15, 16, 17, 18});
    CHECK(recoveryFrames == std::vector<int>{14});
    CHECK(window == std::vector<int>{15, 16, 17});
}

TEST_CASE("the disc's level99: knocked down beside a fighter, square at k = 16 of the rise counters; at 18 it does not",
          "[disc][combat]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    const Run inWindow = riseCounterAt(*wad, 16);
    std::printf("rise counter, square at k = 16: rose %d, fighter %.2f m away, counter %d at k = %d, fighter lost %d "
                "health\n",
                inWindow.rose ? 1 : 0, inWindow.distanceAtPress, inWindow.counter, inWindow.counterK,
                inWindow.fighterHealthLost);
    REQUIRE(inWindow.rose);
    CHECK(inWindow.counterK == 16);
    CHECK(inWindow.fighterHealthLost == 50);
    const Run late = riseCounterAt(*wad, 18);
    std::printf("rise counter, square at k = 18: rose %d, counter %d at k = %d\n", late.rose ? 1 : 0, late.counter,
                late.counterK);
    REQUIRE(late.rose);
    CHECK(late.counterK == -1);
}
