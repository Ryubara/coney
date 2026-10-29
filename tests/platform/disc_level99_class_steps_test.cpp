// SPDX-License-Identifier: GPL-3.0-or-later
// Human_Init's class steps (docs/research/human-init.md) on the disc's level99 at checkpoint 2, the scenario the
// original was read in (run r-20261029-class-size-vermin): Vermin, of class 26 that the type scale does not list and a
// Warrior the random size skips, is made at 1.0, and his power class (7) sets none of the eight switch bits.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "ai/ai_humans.h"
#include "ai/brain.h"
#include "ai/brains.h"
#include "fileio/wad.h"
#include "human/human_flags.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "warriors/created_humans.h"

namespace {

namespace flag = coney::human::flag;

// The pad: cross at 100 (the scene `l99_c2` at checkpoint 2), then nothing.
constexpr std::string_view kPad = "100 tap cross\n";
// The frame of play the humans are looked at: they are made, and nothing in the lesson has scaled them yet.
constexpr std::uint64_t kLookAt = 30;
// The eight bits the power class's switches set or clear (human-init.md#class-switches).
constexpr std::uint64_t kSwitchBits = flag::kUngroundable | flag::kHardToStun | flag::kNoEscape |
                                      flag::kBlockFromReact | flag::kReducedReact | flag::kComboStrength |
                                      flag::kIncreasedReact | flag::kIncreasedReactOff;

// The brain of the first human whose name starts with `prefix`, or null.
coney::ai::Brain* brainNamed(coney::platform::PlayLevelMode& play, const coney::CreatedHumans& humans,
                             std::string_view prefix) {
    coney::ai::Brains& brains = play.fighters().brains();
    for (const coney::HumanCreation& human : humans.all()) {
        if (!human.name.starts_with(prefix)) {
            continue;
        }
        for (std::size_t i = 0; i < brains.size(); ++i) {
            if (brains.at(i).handle() == human.handle) {
                return &brains.at(i);
            }
        }
    }
    return nullptr;
}

// Prints what the run's reads of the original are compared with: the size, the eight bits and the ped type.
void report(std::string_view name, const coney::ai::Brain& brain) {
    std::printf("  level99 checkpoint 2: %.*s scale %.2f, switch bits 0x%llx, ped type %u\n",
                static_cast<int>(name.size()), name.data(), static_cast<double>(brain.human().scale()),
                static_cast<unsigned long long>(brain.human().flags() & kSwitchBits),
                static_cast<unsigned>(brain.senses().pedType));
}

} // namespace

TEST_CASE("the disc's level99: checkpoint 2's Vermin is made at his class's size, with no switch bits",
          "[disc][story][hu_create]") {
    // Expected: human-init.md#size, confirmed (runtime), run r-20261029-class-size-vermin; #class-switches,
    // confirmed (code)
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession run(*wad, kPad);
    run.session().startAtLevel("level99", 2);
    REQUIRE(run.runUntilPlay());
    REQUIRE(run.runToPlayFrame(kLookAt));
    coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);
    const coney::CreatedHumans& humans = run.session().flow().humans();

    const coney::ai::Brain* vermin = brainNamed(*play, humans, "Vermin");
    REQUIRE(vermin != nullptr);
    report("Vermin", *vermin);
    CHECK(vermin->human().scale() == 1.0F);
    CHECK((vermin->human().flags() & kSwitchBits) == 0);
}
