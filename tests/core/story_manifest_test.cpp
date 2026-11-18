// SPDX-License-Identifier: GPL-3.0-or-later
// The manifest's `[story]` section (docs/guides/studio/mission-mods.md#story-manifest): Coney's own format, with no
// counterpart in the original. Every manifest here is synthetic.
#include "core/story_manifest.h"

#include <optional>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

using coney::story::parseStorySection;
using coney::story::StoryMission;
using coney::story::StorySection;
using coney::story::validMissionId;

namespace {

// The design's example section, with a [project] table and a later table around it.
constexpr std::string_view kExample = R"(# coney-project.toml
[project]
name = "yard-brawl"
version = "0.3.0"

[[story.missions]]
id     = "alley"                   # becomes "yard-brawl.alley"
title  = "Alley Cats"
level  = "levels/alley.clevel"
script = "missions/alley.mission"
after  = "level80"
before = "level87"
area   = "Coney"
label  = 2

[[story.missions]]
id      = "docks"
level   = "levels/docks.clevel"
after   = "hub:6"
chapter = "hub/docks-return.lua"

[[story.missions]]
id      = "payin"
level   = "levels/payin.clevel"
replace = "level86"

[load]
autostart = false
)";

// The message of a refused section, empty when it was read.
std::string errorOf(std::string_view text) {
    const auto read = parseStorySection(text);
    return read ? std::string{} : read.error().message;
}

} // namespace

TEST_CASE("story manifest: the design's example reads every field of its three missions", "[story_manifest]") {
    // Expected: ../guides/studio/mission-mods.md#story-manifest, Coney's own format (no counterpart in the original)
    const auto read = parseStorySection(kExample);
    REQUIRE(read.has_value());
    REQUIRE(read->has_value());
    // The REQUIRE above is the check; value_or reads the section without an unchecked optional access.
    const auto section = read.value().value_or(StorySection{});
    const auto& missions = section.missions;
    REQUIRE(missions.size() == 3);
    CHECK(missions[0] == StoryMission{.id = "alley",
                                      .title = "Alley Cats",
                                      .level = "levels/alley.clevel",
                                      .script = "missions/alley.mission",
                                      .after = "level80",
                                      .replace = "",
                                      .before = "level87",
                                      .area = "Coney",
                                      .label = 2,
                                      .chapter = ""});
    CHECK(missions[1].after == "hub:6");
    CHECK(missions[1].chapter == "hub/docks-return.lua");
    CHECK_FALSE(missions[1].label.has_value());
    CHECK(missions[2].replace == "level86");
    CHECK(missions[2].after.empty());
}

TEST_CASE("story manifest: a [story] table makes a story mod, missions or none; no section, none", "[story_manifest]") {
    // Expected: ../guides/studio/mission-mods.md#story-manifest, Coney's own format (no counterpart in the original)
    const auto story = parseStorySection("[project]\nname = \"yard\"\n[story]\n");
    REQUIRE(story.has_value());
    REQUIRE(story->has_value());
    // The REQUIRE above is the check; value_or reads the section without an unchecked optional access.
    CHECK(story.value().value_or(StorySection{}).missions.empty());
    const auto none = parseStorySection("[project]\nname = \"yard\"\n\n[play]\nlevel = \"project:/levels/a.layout\"\n");
    REQUIRE(none.has_value());
    CHECK_FALSE(none->has_value());
}

TEST_CASE("story manifest: a mission names exactly one of after and replace", "[story_manifest]") {
    // Expected: ../guides/studio/mission-mods.md#story-manifest, Coney's own format (no counterpart in the original)
    CHECK(errorOf("[[story.missions]]\nid = \"a\"\nlevel = \"l\"\nafter = \"level80\"\nreplace = \"level86\"\n") ==
          "story mission 'a': names both after and replace; a mission names exactly one");
    CHECK(errorOf("[[story.missions]]\nid = \"a\"\nlevel = \"l\"\n") ==
          "story mission 'a': names neither after nor replace; a mission names exactly one");
}

TEST_CASE("story manifest: a mission needs a valid id, unique in the mod, and a level", "[story_manifest]") {
    // Expected: ../guides/studio/mission-mods.md#story-manifest, Coney's own format (no counterpart in the original)
    CHECK(errorOf("[[story.missions]]\nlevel = \"l\"\nafter = \"level80\"\n") ==
          "story mission 1: id \"\" is missing or not lower case letters, digits and '-'");
    CHECK(errorOf("[[story.missions]]\nid = \"Alley.Cats\"\nlevel = \"l\"\nafter = \"level80\"\n") ==
          "story mission 1: id \"Alley.Cats\" is missing or not lower case letters, digits and '-'");
    CHECK(errorOf("[[story.missions]]\nid = \"a\"\nafter = \"level80\"\n") == "story mission 'a': needs a level");
    CHECK(errorOf("[[story.missions]]\nid = \"a\"\nlevel = \"l\"\nafter = \"level80\"\n"
                  "[[story.missions]]\nid = \"a\"\nlevel = \"m\"\nafter = \"level87\"\n") ==
          "story mission 'a': the id is used twice");
}

TEST_CASE("story manifest: a chapter script needs a hub chapter anchor", "[story_manifest]") {
    // Expected: ../guides/studio/mission-mods.md#anchors, Coney's own format (no counterpart in the original)
    CHECK(errorOf("[[story.missions]]\nid = \"a\"\nlevel = \"l\"\nafter = \"level80\"\nchapter = \"hub/a.lua\"\n") ==
          "story mission 'a': a chapter script needs `after = \"hub:<n>\"`");
}

TEST_CASE("story manifest: a broken line is refused with its line number", "[story_manifest]") {
    // Expected: ../guides/studio/mission-mods.md#story-manifest, Coney's own format (no counterpart in the original)
    CHECK(errorOf("[[story.missions]]\nid = \"a\nlevel = \"l\"\n") == "line 2: a string needs its closing quote");
    CHECK(errorOf("[[story.missions]]\nid = \"a\"\nlabel = two\n") == "line 3: label must be a whole number");
    CHECK(errorOf("[[story.missions]]\nid = \"a\"\njust words\n") == "line 3: expected `key = value`");
    CHECK(errorOf("[story.missions]\nid = \"a\"\n") ==
          "line 1: story missions are `[[story.missions]]`, one per mission");
}

TEST_CASE("story manifest: keys and tables it does not know are skipped", "[story_manifest]") {
    // Expected: ../guides/studio/mission-mods.md#story-manifest, Coney's own format (no counterpart in the original)
    const auto read = parseStorySection("[[scripts.client]]\nid = \"not-a-mission\"\n"
                                        "[[story.missions]]\nid = \"a\"\nlevel = \"l\"\nafter = \"level80\"\n"
                                        "colour = \"red\"\ntags = [\"x\"]\n");
    REQUIRE(read.has_value());
    REQUIRE(read->has_value());
    // The REQUIRE above is the check; value_or reads the section without an unchecked optional access.
    const auto section = read.value().value_or(StorySection{});
    REQUIRE(section.missions.size() == 1);
    CHECK(section.missions[0].id == "a");
}

TEST_CASE("story manifest: mission ids are lower case letters, digits and dashes", "[story_manifest]") {
    // Expected: ../guides/studio/mission-mods.md#graph, Coney's own format (no counterpart in the original)
    CHECK(validMissionId("alley"));
    CHECK(validMissionId("yard-2"));
    CHECK_FALSE(validMissionId(""));
    CHECK_FALSE(validMissionId("-alley"));
    CHECK_FALSE(validMissionId("yard.alley"));
    CHECK_FALSE(validMissionId("hub:1"));
    CHECK_FALSE(validMissionId(std::string(65, 'a')));
}
