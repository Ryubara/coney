// SPDX-License-Identifier: GPL-3.0-or-later

// A check against the player's own disc: Rembrandt's Anim Range List decodes, every attack combat starts has a damage
// in it, and the grab and tackle search ranges come out as the research reads them at runtime
// (docs/research/combat/index.md). The damage the research measured at runtime (S1 17, SS2 36, ...) is not what the
// file holds: the character class's damage table overrides it when the human is made (combat.md, Coney's
// implementation), so this test checks only that the file's values are there. A second case reads the speeds of
// Rembrandt's hold walks (86, 88), which the grab's walk takes (docs/research/combat-grabs.md#hold-walk). It runs only
// when the environment variable CONEY_DISC names the disc and skips otherwise. It prints counts only (LEGAL.md).

#include <cstddef>
#include <cstdio>
#include <utility>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "characters/character_assets.h"
#include "characters/character_list.h"
#include "combat/anim_ids.h"
#include "combat/anim_ranges.h"
#include "combat/grab.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "human/player.h"

using Catch::Approx;
using namespace coney::combat;

TEST_CASE("Rembrandt's Anim Range List gives every attack a damage and the researched grab ranges", "[disc][combat]") {
    // Expected: combat-grabs.md#grab, confirmed (code) (the grab's and the tackle's search ranges, read from the disc)
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    auto list = coney::characters::loadCharacterList(*wad);
    REQUIRE(list.has_value());
    coney::chunk::ChunkHandlerTable table = coney::chunk::ChunkHandlerTable::withDefaults();
    coney::characters::addCharacterDataHandlers(table);
    auto assets = coney::characters::loadCharacterAssets(*wad, *list, table, "warr_re_cv");
    REQUIRE(assets.has_value());

    auto ranges = AnimRangeList::parse(assets->data.rangeList());
    REQUIRE(ranges.has_value());
    CHECK(ranges->size() == 722);

    // Every attack the player's combat starts has a damage of its own.
    int attacks = 0;
    for (const int id : {anim_id::kRunningAttackCharge,
                         anim_id::kRunningAttackDive,
                         anim_id::kAttackX1,
                         anim_id::kAttackS1,
                         anim_id::kAttackXX2,
                         anim_id::kAttackXS2,
                         anim_id::kAttackSX2,
                         anim_id::kAttackSS2,
                         anim_id::kAttackSSX3,
                         anim_id::kAttackSSS3,
                         anim_id::kAttackFromWalk,
                         anim_id::kAttackFromRun,
                         anim_id::kSnapRight,
                         anim_id::kSnapLeft,
                         anim_id::kSnapBack,
                         anim_id::kGrabComboStrike1,
                         anim_id::kGrabComboStrike2,
                         anim_id::kGrabComboStrike3,
                         anim_id::kGrabPower1Strike1,
                         anim_id::kGrabPower2Strike1,
                         anim_id::kGrabFrontStrike,
                         anim_id::kThrow1Front,
                         anim_id::kThrow2Front,
                         anim_id::kGroundedStrike1,
                         anim_id::kMountingStrike}) {
        CHECK(ranges->damage(static_cast<std::size_t>(id)) > 0);
        ++attacks;
    }

    // The grab searches within anim 70's far range × 1.25, the tackle within anim 3's: 3.12 m and 3.75 m at runtime.
    CHECK(grabSearchRange(*ranges, GrabKind::Grab, 1.25F) == Approx(3.12F).margin(0.01F));
    CHECK(grabSearchRange(*ranges, GrabKind::Tackle, 1.25F) == Approx(3.75F).margin(0.01F));

    int withDamage = 0;
    for (std::size_t id = 0; id < ranges->size(); ++id) {
        withDamage += ranges->damage(id) != 0 ? 1 : 0;
    }
    std::printf("  combat: %zu Anim Range List records, %d with damage, %d attacks checked\n", ranges->size(),
                withDamage, attacks);
}

TEST_CASE("Rembrandt's hold walks move at the speeds the original's grab walks at", "[disc][combat]") {
    // Expected: combat-grabs.md#hold-walk, confirmed (runtime), run r-20261128-grab-hold-walk (the player's speed read
    // 1.1250 m/s in the front hold walk and 1.2273 m/s in the rear one: record +0x170, which the grab's movement styles
    // fill from 86 and 88)
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    coney::chunk::ChunkHandlerTable table = coney::chunk::ChunkHandlerTable::withDefaults();
    coney::characters::addCharacterDataHandlers(table);
    auto character = coney::human::PlayerCharacter::load(*wad, table, coney::human::kPlayerModel);
    REQUIRE(character.has_value());
    const float front = (*character)->anims().speed(86);
    const float rear = (*character)->anims().speed(88);
    std::printf("  hold walks: front 86 at %.3f m/s, rear 88 at %.3f m/s\n", static_cast<double>(front),
                static_cast<double>(rear));
    CHECK(front == Approx(1.125F).margin(0.001F));
    CHECK(rear == Approx(1.2273F).margin(0.001F));
}
