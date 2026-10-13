// SPDX-License-Identifier: GPL-3.0-or-later
// The Animation player's catalogue: the anim ids' names and categories, and the clips an anim set offers, over the
// synthetic character data of support/human_fixtures.h (no game data).
#include "debug/anim_catalog.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "characters/anim_set.h"
#include "support/human_fixtures.h"

using Catch::Approx;
using coney::debug::animCategory;
using coney::debug::AnimClipChoice;
using coney::debug::animIdName;

TEST_CASE("every anim id has its scripts' name, and none past the last", "[debug][anim]") {
    CHECK(animIdName(0) == "ANIM_RUNNING_ATTACK_CHARGE");
    CHECK(animIdName(358) == "ANIM_FIGHT_IDLE");
    CHECK(animIdName(721) == "ANIM_FACE_SPEAKING_EXCITED_03");
    CHECK(animIdName(722).empty());
    for (std::uint32_t id = 0; id < coney::debug::kAnimIdCount; ++id) {
        INFO(id);
        CHECK(animIdName(id).starts_with("ANIM_"));
    }
}

TEST_CASE("an anim id's category is the first word of its name, a word's forms together", "[debug][anim]") {
    CHECK(animCategory(0) == "Running");
    CHECK(animCategory(3) == "Tackle");
    CHECK(animCategory(358) == "Fight");
    CHECK(animCategory(721) == "Face");
    // ANIM_MOUNTED_IDLE and ANIM_MOUNTING_IDLE join the mount's own ids.
    CHECK(animCategory(207) == "Mount");
    CHECK(animCategory(210) == "Mount");
    CHECK(animCategory(722) == "Other");
}

TEST_CASE("an anim set offers each id it answers with a clip, its length and rate, own or generic", "[debug][anim]") {
    // The character's own data holds the idle and the walk; the defaults hold the whole locomotion set.
    const std::vector<coney::test::LocomotionClip> all = coney::test::locomotionClips();
    std::vector<coney::test::LocomotionClip> mine;
    std::ranges::copy_if(all, std::back_inserter(mine),
                         [](const auto& clip) { return clip.id == 388 || clip.id == 408; });
    const coney::characters::CharacterData own = coney::test::locomotionData(mine);
    const coney::characters::CharacterData generic = coney::test::locomotionData(all);
    const coney::characters::AnimSet set(own, &generic);

    const std::vector<AnimClipChoice> choices = coney::debug::clipChoicesOf(set);
    REQUIRE(choices.size() == all.size());
    CHECK(std::ranges::is_sorted(choices, {}, &AnimClipChoice::id));
    const auto find = [&choices](std::uint32_t id) {
        const auto at = std::ranges::find(choices, id, &AnimClipChoice::id);
        REQUIRE(at != choices.end());
        return *at;
    };
    const AnimClipChoice idle = find(388);
    CHECK_FALSE(idle.generic);
    CHECK(idle.seconds == Approx(2.0F));
    CHECK(idle.rate == Approx(0.75F)); // no rate flag
    CHECK(idle.playSeconds() == Approx(2.0F / 0.75F));
    CHECK(idle.playSeconds(2.0F) == Approx(1.0F / 0.75F));
    CHECK(idle.clip == "synthetic");
    const AnimClipChoice walk = find(408);
    CHECK_FALSE(walk.generic);
    CHECK(walk.rate == Approx(1.0F)); // flag 0x1000
    // The run start (414) comes from the defaults, at the rate the character's own range list gives it.
    const AnimClipChoice runStart = find(414);
    CHECK(runStart.generic);
    CHECK(runStart.seconds == Approx(0.3F));

    // A set with no defaults offers only its own.
    const coney::characters::AnimSet alone(own, nullptr);
    CHECK(coney::debug::clipChoicesOf(alone).size() == 2);
}

TEST_CASE("the clips are grouped by category, the categories in alphabetical order", "[debug][anim]") {
    const std::vector<AnimClipChoice> clips{
        {.id = 358, .clip = "fight idle"}, {.id = 3, .clip = "tackle intro"}, {.id = 4, .clip = "tackle start"},
        {.id = 207, .clip = "mounted"},    {.id = 210, .clip = "mounting"},
    };
    const auto categories = coney::debug::byCategory(clips);
    REQUIRE(categories.size() == 3);
    CHECK(categories[0].name == "Fight");
    CHECK(categories[1].name == "Mount");
    CHECK(categories[1].clips.size() == 2);
    CHECK(categories[2].name == "Tackle");
    REQUIRE(categories[2].clips.size() == 2);
    CHECK(categories[2].clips[0].id == 3);
    CHECK(categories[2].clips[1].id == 4);
}
