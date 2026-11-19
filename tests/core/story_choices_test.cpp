// SPDX-License-Identifier: GPL-3.0-or-later
// The player's story merge choices (docs/guides/studio/mission-mods.md#graph-merge): their text in `enabled.toml`, the
// groups they apply to, and the merge that takes its turns in the chosen order. Coney's own, with no counterpart in
// the original. Every mod here is synthetic.
#include "core/story_choices.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/story_graph.h"
#include "core/story_merge.h"
#include "support/fixtures.h"

using coney::story::GroupKind;
using coney::story::loadStoryChoices;
using coney::story::mergeStory;
using coney::story::parseStoryChoices;
using coney::story::saveStoryChoices;
using coney::story::StoryChoice;
using coney::story::StoryChoiceMember;
using coney::story::StoryGraph;
using coney::story::StoryGroup;
using coney::story::storyGroups;
using coney::story::StoryMerge;
using coney::story::StoryMission;
using coney::story::StoryMod;
using coney::story::withStoryChoices;

namespace {

// A mission of `id` after `anchor`.
StoryMission after(std::string id, std::string anchor) {
    StoryMission mission;
    mission.id = std::move(id);
    mission.level = "levels/a.clevel";
    mission.after = std::move(anchor);
    return mission;
}

// A mission that replaces `target`.
StoryMission replacing(std::string id, std::string target) {
    StoryMission mission;
    mission.id = std::move(id);
    mission.level = "levels/a.clevel";
    mission.replace = std::move(target);
    return mission;
}

// A story mod named `name` whose package hash is `hash`, with `missions` in manifest order.
StoryMod mod(std::string name, std::string hash, std::vector<StoryMission> missions) {
    StoryMod made{.name = std::move(name), .story = {}, .title = {}, .packageHash = std::move(hash)};
    made.story.missions = std::move(missions);
    return made;
}

// A choice of `kind` on `anchor` naming `members` (story id and package hash), in this order.
StoryChoice choice(GroupKind kind, std::string anchor, std::vector<StoryChoiceMember> members) {
    return StoryChoice{.kind = kind, .anchor = std::move(anchor), .members = std::move(members)};
}

// The merged chain's step ids from `from` to before `to` (both ids in the chain).
std::vector<std::string> idsBetween(const StoryGraph& graph, std::string_view from, std::string_view to) {
    std::vector<std::string> ids;
    bool inside = false;
    for (const auto& step : graph.steps()) {
        const std::string id = step.id();
        if (id == from) {
            inside = true;
        }
        if (id == to) {
            break;
        }
        if (inside) {
            ids.push_back(id);
        }
    }
    return ids;
}

// The two missions of one anchor, in two mods.
std::vector<StoryMod> twoOnOneAnchor() {
    return {mod("aaa", "hash-a", {after("x", "level80")}), mod("bbb", "hash-b", {after("y", "level80")})};
}

// The two mods' missions replacing one mission.
std::vector<StoryMod> twoReplacements() {
    return {mod("aaa", "hash-a", {replacing("p", "level86")}), mod("bbb", "hash-b", {replacing("q", "level86")})};
}

// The message `parseStoryChoices` refuses `text` with; empty when it reads.
std::string refusal(std::string_view text) {
    const auto read = parseStoryChoices(text);
    return read ? std::string{} : read.error().message;
}

// The text of the file `file`.
std::string textOf(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

} // namespace

TEST_CASE("story choices: the story tables of enabled.toml are read and the rest is skipped", "[story_choices]") {
    // Expected: ../guides/studio/mission-mods.md#graph-merge (the swap is kept in enabled.toml with both package
    // hashes), Coney's own design (no counterpart in the original)
    const std::string text = R"(# the mods, in load order
[[mod]]
name = "aaa"
hash = "hash-a"

[[story.choice]]
kind = "after"
anchor = "level80"
missions = ["bbb.y", "aaa.x"]
packages = ["hash-b", "hash-a"]

[[mod]]
name = "bbb"

[[story.choice]]
kind = "replace"
anchor = "level86"
missions = ["bbb.q", "aaa.p"]
packages = ["hash-b", "hash-a"]
)";
    const auto read = parseStoryChoices(text);
    REQUIRE(read.has_value());
    const std::vector<StoryChoice> expected{
        choice(GroupKind::Anchor, "level80", {{"bbb.y", "hash-b"}, {"aaa.x", "hash-a"}}),
        choice(GroupKind::Replacement, "level86", {{"bbb.q", "hash-b"}, {"aaa.p", "hash-a"}})};
    CHECK(*read == expected);
    const auto none = parseStoryChoices("[[mod]]\nname = \"aaa\"\n");
    REQUIRE(none.has_value());
    CHECK(none->empty());
}

TEST_CASE("story choices: writing replaces the choices and leaves the rest of the file as it was", "[story_choices]") {
    // Expected: ../guides/studio/mods.md#installed (the mods menu writes enabled.toml; the player edits it too),
    // Coney's own design (no counterpart in the original)
    const std::string before = R"(# my mods
[[mod]]
name = "aaa"
hash = "hash-a"

[[story.choice]]
kind = "after"
anchor = "level70"
missions = ["aaa.old", "bbb.old"]
packages = ["h1", "h2"]

[[mod]]
name = "bbb"
hash = "hash-b"
)";
    const std::vector<StoryChoice> choices{
        choice(GroupKind::Anchor, "level80", {{"bbb.y", "hash-b"}, {"aaa.x", "hash-a"}})};
    const std::string written = withStoryChoices(before, choices);
    CHECK(written.contains("# my mods\n[[mod]]\nname = \"aaa\"\nhash = \"hash-a\"\n"));
    CHECK(written.contains("[[mod]]\nname = \"bbb\"\nhash = \"hash-b\"\n"));
    CHECK_FALSE(written.contains("level70"));
    CHECK_FALSE(written.contains("aaa.old"));
    const auto read = parseStoryChoices(written);
    REQUIRE(read.has_value());
    CHECK(*read == choices);
    // Writing the same choices again changes nothing, and writing none takes the tables out again.
    CHECK(withStoryChoices(written, choices) == written);
    const std::string cleared = withStoryChoices(written, {});
    CHECK_FALSE(cleared.contains("story.choice"));
    CHECK(cleared.contains("name = \"bbb\""));
    CHECK(parseStoryChoices(cleared)->empty());
}

TEST_CASE("story choices: a quote or a backslash in an id or a hash is kept", "[story_choices]") {
    // Expected: ../guides/studio/mission-mods.md#graph-merge, Coney's own design (no counterpart in the original)
    const std::vector<StoryChoice> choices{choice(GroupKind::Anchor, "hub:3", {{"a\"b.x", "h\\1"}, {"c.y", "h\"2"}})};
    const auto read = parseStoryChoices(withStoryChoices("", choices));
    REQUIRE(read.has_value());
    CHECK(*read == choices);
}

TEST_CASE("story choices: a broken choice is refused with its line", "[story_choices]") {
    // Expected: ../guides/studio/mission-mods.md#graph-merge, Coney's own design (no counterpart in the original)
    const std::string head = "[[story.choice]]\n";
    const std::string anchor = "anchor = \"level80\"\n";
    const std::string pair = "missions = [\"a.x\", \"b.y\"]\npackages = [\"h\", \"i\"]\n";
    // Arrays of different lengths.
    const auto different =
        parseStoryChoices(head + "kind = \"after\"\n" + anchor + "missions = [\"a.x\", \"b.y\"]\npackages = [\"h\"]\n");
    REQUIRE_FALSE(different.has_value());
    CHECK(different.error().code == coney::ErrorCode::Invalid);
    // One mission, an unknown kind, a missing anchor, a line that is no key and value, and a string that does not end.
    CHECK(refusal(head + "kind = \"after\"\n" + anchor + "missions = [\"a.x\"]\npackages = [\"h\"]\n")
              .contains("at least two"));
    CHECK(refusal(head + "kind = \"sideways\"\n" + anchor + pair).contains("sideways"));
    CHECK_FALSE(refusal(head + "kind = \"after\"\n" + pair).empty());
    CHECK(refusal("\n" + head + "kind after\n").starts_with("line 3:"));
    CHECK_FALSE(refusal(head + "kind = \"after\n" + anchor + pair).empty());
    // A mission named twice leaves another out of the order: refused, with the mission.
    CHECK(refusal(head + "kind = \"after\"\n" + anchor + "missions = [\"a.x\", \"a.x\"]\npackages = [\"h\", \"h\"]\n")
              .contains("a.x"));
}

TEST_CASE("story choices: they are saved to the file and read back, the mods in it untouched", "[story_choices]") {
    // Expected: ../guides/studio/mission-mods.md#graph-merge (kept across a restart), Coney's own design (no
    // counterpart in the original)
    const coney::test::TempDir dir;
    const std::filesystem::path file = dir.path() / "mods" / "enabled.toml";
    // No file: no choices.
    const auto none = loadStoryChoices(file);
    REQUIRE(none.has_value());
    CHECK(none->empty());
    // Saving makes the folder and the file.
    const std::vector<StoryChoice> choices{
        choice(GroupKind::Anchor, "level80", {{"bbb.y", "hash-b"}, {"aaa.x", "hash-a"}})};
    REQUIRE(saveStoryChoices(file, choices).has_value());
    const auto read = loadStoryChoices(file);
    REQUIRE(read.has_value());
    CHECK(*read == choices);
    // A file with mods in it keeps them when the choices change.
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << "[[mod]]\nname = \"aaa\"\n";
    }
    REQUIRE(saveStoryChoices(file, choices).has_value());
    CHECK(textOf(file).starts_with("[[mod]]\nname = \"aaa\"\n"));
    CHECK(*loadStoryChoices(file) == choices);
    REQUIRE(saveStoryChoices(file, {}).has_value());
    CHECK(textOf(file) == "[[mod]]\nname = \"aaa\"\n");
    // A file that cannot be made is an Io error.
    const std::filesystem::path blocker = dir.path() / "blocker";
    std::ofstream(blocker) << "not a folder";
    const auto refused = saveStoryChoices(blocker / "enabled.toml", choices);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == coney::ErrorCode::Io);
}

TEST_CASE("story groups: missions on one anchor or one replaced mission are a group, in turn order",
          "[story_choices]") {
    // Expected: ../guides/studio/mission-mods.md#graph-merge (two missions on one anchor go in load order; two
    // replacements of one mission), Coney's own design (no counterpart in the original)
    std::vector<StoryMod> mods = twoOnOneAnchor();
    mods.push_back(mod("ccc", "hash-c", {after("z", "level87"), replacing("w", "level86")}));
    mods.push_back(mod("ddd", "hash-d", {replacing("v", "level86")}));
    const std::vector<StoryGroup> groups = storyGroups(mods);
    REQUIRE(groups.size() == 2);
    CHECK(groups[0].kind == GroupKind::Anchor);
    CHECK(groups[0].anchor == "level80");
    CHECK(groups[0].defaultOrder == std::vector<std::string>{"aaa.x", "bbb.y"});
    CHECK(groups[0].order == groups[0].defaultOrder);
    CHECK_FALSE(groups[0].chosen);
    CHECK_FALSE(groups[0].stale);
    CHECK(groups[1].kind == GroupKind::Replacement);
    CHECK(groups[1].anchor == "level86");
    CHECK(groups[1].defaultOrder == std::vector<std::string>{"ccc.w", "ddd.v"});
    // A mission alone on its anchor is no group, and a mission after a mission and one replacing it are two groups.
    CHECK(storyGroups(std::vector<StoryMod>{mod("aaa", "h", {after("x", "level80"), replacing("y", "level80")})})
              .empty());
}

TEST_CASE("story groups: a choice applies to the group it names, with the packages it was made between",
          "[story_choices]") {
    // Expected: ../guides/studio/mission-mods.md#graph-merge (kept in enabled.toml with both package hashes), Coney's
    // own design (no counterpart in the original)
    const std::vector<StoryMod> mods = twoOnOneAnchor();
    const std::vector<StoryChoice> swapped{
        choice(GroupKind::Anchor, "level80", {{"bbb.y", "hash-b"}, {"aaa.x", "hash-a"}})};
    const auto applied = storyGroups(mods, swapped);
    REQUIRE(applied.size() == 1);
    CHECK(applied[0].order == std::vector<std::string>{"bbb.y", "aaa.x"});
    CHECK(applied[0].defaultOrder == std::vector<std::string>{"aaa.x", "bbb.y"});
    CHECK(applied[0].chosen);
    CHECK_FALSE(applied[0].stale);
    // A mod whose package changed, a choice of the other kind, a choice missing one mission and a choice naming a
    // mission no mod has are ignored; the first and the last are marked stale (the player chose between other things),
    // a choice for another group is not this group's.
    const std::vector<StoryMod> updated{mod("aaa", "hash-a2", {after("x", "level80")}),
                                        mod("bbb", "hash-b", {after("y", "level80")})};
    const auto changed = storyGroups(updated, swapped);
    REQUIRE(changed.size() == 1);
    CHECK(changed[0].order == changed[0].defaultOrder);
    CHECK_FALSE(changed[0].chosen);
    CHECK(changed[0].stale);
    const std::vector<StoryChoice> otherKind{
        choice(GroupKind::Replacement, "level80", {{"bbb.y", "hash-b"}, {"aaa.x", "hash-a"}})};
    CHECK_FALSE(storyGroups(mods, otherKind)[0].chosen);
    const std::vector<StoryChoice> unknown{
        choice(GroupKind::Anchor, "level80", {{"bbb.y", "hash-b"}, {"zzz.q", "hash-z"}})};
    const auto missing = storyGroups(mods, unknown);
    CHECK_FALSE(missing[0].chosen);
    CHECK(missing[0].stale);
    const std::vector<StoryChoice> elsewhere{
        choice(GroupKind::Anchor, "level87", {{"bbb.y", "hash-b"}, {"aaa.x", "hash-a"}})};
    CHECK_FALSE(storyGroups(mods, elsewhere)[0].chosen);
    CHECK_FALSE(storyGroups(mods, elsewhere)[0].stale);
}

TEST_CASE("story merge: a chosen order puts the first mission nearest the anchor", "[story_choices]") {
    // Expected: ../guides/studio/mission-mods.md#graph-merge (two missions on one anchor go in load order, which the
    // Mods screen can swap), Coney's own design (no counterpart in the original)
    const std::vector<StoryMod> mods = twoOnOneAnchor();
    const StoryMerge byLoadOrder = mergeStory(StoryGraph::original(), mods);
    CHECK(idsBetween(byLoadOrder.graph, "level80", "hub:1") ==
          std::vector<std::string>{"level80", "aaa.x", "bbb.y", "level87"});
    const std::vector<StoryChoice> swapped{
        choice(GroupKind::Anchor, "level80", {{"bbb.y", "hash-b"}, {"aaa.x", "hash-a"}})};
    const StoryMerge merge = mergeStory(StoryGraph::original(), mods, swapped);
    CHECK(merge.refusals.empty());
    CHECK(idsBetween(merge.graph, "level80", "hub:1") ==
          std::vector<std::string>{"level80", "bbb.y", "aaa.x", "level87"});
    REQUIRE(merge.groups.size() == 1);
    CHECK(merge.groups[0].chosen);
    CHECK(merge.groups[0].order == std::vector<std::string>{"bbb.y", "aaa.x"});
    // The choice is a different story: the graph's hash differs, so a co-op session sees the two apart.
    CHECK(merge.graph.hash() != byLoadOrder.graph.hash());
}

TEST_CASE("story merge: a mission anchored on a swapped one follows it", "[story_choices]") {
    // Expected: ../guides/studio/mission-mods.md#graph-merge (the next after the first and after everything placed
    // after it), Coney's own design (no counterpart in the original)
    std::vector<StoryMod> mods = twoOnOneAnchor();
    mods.push_back(mod("ccc", "hash-c", {after("z", "aaa.x")}));
    const std::vector<StoryChoice> swapped{
        choice(GroupKind::Anchor, "level80", {{"bbb.y", "hash-b"}, {"aaa.x", "hash-a"}})};
    const StoryMerge merge = mergeStory(StoryGraph::original(), mods, swapped);
    CHECK(merge.refusals.empty());
    CHECK(idsBetween(merge.graph, "level80", "hub:1") ==
          std::vector<std::string>{"level80", "bbb.y", "aaa.x", "ccc.z", "level87"});
}

TEST_CASE("story merge: three missions on one anchor take the chosen order", "[story_choices]") {
    // Expected: ../guides/studio/mission-mods.md#graph-merge, Coney's own design (no counterpart in the original)
    std::vector<StoryMod> mods = twoOnOneAnchor();
    mods.push_back(mod("ccc", "hash-c", {after("z", "level80")}));
    const std::vector<StoryChoice> chosen{
        choice(GroupKind::Anchor, "level80", {{"ccc.z", "hash-c"}, {"aaa.x", "hash-a"}, {"bbb.y", "hash-b"}})};
    const StoryMerge merge = mergeStory(StoryGraph::original(), mods, chosen);
    CHECK(idsBetween(merge.graph, "level80", "hub:1") ==
          std::vector<std::string>{"level80", "ccc.z", "aaa.x", "bbb.y", "level87"});
}

TEST_CASE("story merge: a choice for changed packages is ignored", "[story_choices]") {
    // Expected: ../guides/studio/mission-mods.md#two-mods (the choice is kept with both package hashes), Coney's own
    // design (no counterpart in the original)
    const std::vector<StoryMod> updated{mod("aaa", "hash-a2", {after("x", "level80")}),
                                        mod("bbb", "hash-b", {after("y", "level80")})};
    const std::vector<StoryChoice> swapped{
        choice(GroupKind::Anchor, "level80", {{"bbb.y", "hash-b"}, {"aaa.x", "hash-a"}})};
    const StoryMerge merge = mergeStory(StoryGraph::original(), updated, swapped);
    CHECK(idsBetween(merge.graph, "level80", "hub:1") ==
          std::vector<std::string>{"level80", "aaa.x", "bbb.y", "level87"});
    CHECK(merge.graph.hash() == mergeStory(StoryGraph::original(), updated).graph.hash());
    REQUIRE(merge.groups.size() == 1);
    CHECK(merge.groups[0].stale);
}

TEST_CASE("story merge: a chosen replacement takes the place and the other is refused", "[story_choices]") {
    // Expected: ../guides/studio/mission-mods.md#graph-merge (two replacements of one mission: the first in load order
    // takes the place until the player chooses), Coney's own design (no counterpart in the original)
    const std::vector<StoryMod> mods = twoReplacements();
    const StoryMerge byLoadOrder = mergeStory(StoryGraph::original(), mods);
    CHECK(idsBetween(byLoadOrder.graph, "hub:6", "hub:7") == std::vector<std::string>{"hub:6", "aaa.p", "level93"});
    REQUIRE(byLoadOrder.refusals.size() == 1);
    CHECK(byLoadOrder.refusals[0].message == "bbb.q: 'level86' is already replaced by 'aaa.p'");
    const std::vector<StoryChoice> chosen{
        choice(GroupKind::Replacement, "level86", {{"bbb.q", "hash-b"}, {"aaa.p", "hash-a"}})};
    const StoryMerge merge = mergeStory(StoryGraph::original(), mods, chosen);
    CHECK(idsBetween(merge.graph, "hub:6", "hub:7") == std::vector<std::string>{"hub:6", "bbb.q", "level93"});
    REQUIRE(merge.refusals.size() == 1);
    CHECK(merge.refusals[0].message == "aaa.p: 'level86' is already replaced by 'bbb.q'");
    CHECK(merge.placed.size() == 1);
    CHECK(merge.placed[0].id == "bbb.q");
}

TEST_CASE("story choices: a file with CRLF line endings keeps them in the tables written", "[story_choices]") {
    // Expected: ../guides/studio/mods.md#installed (the player edits enabled.toml in any editor), Coney's own design
    // (no counterpart in the original)
    const std::vector<StoryChoice> choices{
        choice(GroupKind::Anchor, "level80", {{"bbb.y", "hash-b"}, {"aaa.x", "hash-a"}})};
    const std::string crlf = "[[mod]]\r\nname = \"aaa\"\r\n";
    const std::string written = withStoryChoices(crlf, choices);
    CHECK(written.starts_with(crlf));
    // Every line ends in CRLF: no lone LF anywhere.
    for (std::size_t i = 0; i < written.size(); ++i) {
        if (written[i] == '\n') {
            REQUIRE(i > 0);
            CHECK(written[i - 1] == '\r');
        }
    }
    CHECK(*parseStoryChoices(written) == choices);
    // Writing them again changes nothing, and writing none gives the file back.
    CHECK(withStoryChoices(written, choices) == written);
    CHECK(withStoryChoices(written, {}) == crlf);
    // A file with LF endings still gets LF.
    CHECK_FALSE(withStoryChoices("[[mod]]\nname = \"aaa\"\n", choices).contains('\r'));
}

TEST_CASE("story groups: a choice that names one mission twice is ignored and the other mission is not lost",
          "[story_choices]") {
    // Expected: ../guides/studio/mission-mods.md#graph-merge (a choice names exactly the group's missions), Coney's
    // own design (no counterpart in the original)
    const std::vector<StoryMod> mods = twoOnOneAnchor();
    const std::vector<StoryChoice> repeated{
        choice(GroupKind::Anchor, "level80", {{"aaa.x", "hash-a"}, {"aaa.x", "hash-a"}})};
    const auto groups = storyGroups(mods, repeated);
    REQUIRE(groups.size() == 1);
    CHECK_FALSE(groups[0].chosen);
    CHECK(groups[0].stale);
    CHECK(groups[0].order == groups[0].defaultOrder);
    const StoryMerge merged = mergeStory(StoryGraph::original(), mods, repeated);
    CHECK(idsBetween(merged.graph, "level80", "hub:1") ==
          std::vector<std::string>{"level80", "aaa.x", "bbb.y", "level87"});
    CHECK(merged.refusals.empty());
}
