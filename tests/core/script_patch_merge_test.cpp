// SPDX-License-Identifier: GPL-3.0-or-later
// Merging several script patches of one base (docs/guides/script-patches.md#merge; the rules:
// docs/guides/studio/mission-mods.md#two-mods). The base is made-up numbered lines, no text of the game's.
#include "core/script_patch_merge.h"

#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/script_patch.h"
#include "core/script_patch_apply.h"

using coney::patch::MergeConflict;
using coney::patch::MergeSide;
using coney::patch::PatchFault;
using coney::patch::PatchSection;

namespace {

// Ten lines `line 1 of the base` to `line 10 of the base`, each with its newline (so 11 lines, the last empty).
std::string base() {
    std::string text;
    for (int i = 1; i <= 10; ++i) {
        text += "line " + std::to_string(i) + " of the base\n";
    }
    return text;
}

// The base with lines `first` to `last` (numbered from 1) replaced by `with`, which ends in a newline or is empty.
std::string edit(const std::string& text, int first, int last, std::string_view with) {
    std::size_t from = 0;
    for (int i = 1; i < first; ++i) {
        from = text.find('\n', from) + 1;
    }
    std::size_t to = from;
    for (int i = first; i <= last; ++i) {
        to = text.find('\n', to) + 1;
    }
    std::string out = text;
    out.replace(from, to - from, with);
    return out;
}

// The base with `with` put before line `before`.
std::string insert(const std::string& text, int before, std::string_view with) {
    return edit(text, before, before - 1, with);
}

// A patch of the base made by makePatch, as a mod's would be.
PatchSection patchTo(const std::string& edited) {
    auto made = coney::patch::makePatch(base(), edited, coney::patch::PatchHeader{});
    REQUIRE(made.has_value());
    return *made;
}

// Merges the patches and returns the result, which must succeed.
coney::patch::MergeResult merge(const std::vector<PatchSection>& patches,
                                const coney::patch::ConflictChooser& choose = {}) {
    auto merged = coney::patch::mergePatches(patches, base(), choose);
    REQUIRE(merged.has_value());
    return *merged;
}

// The text the merged section makes of the base; its result hash is checked by applying it.
std::string mergedText(const coney::patch::MergeResult& result) {
    auto applied = coney::patch::applyPatch(result.merged, base());
    REQUIRE(applied.has_value());
    return *applied;
}

} // namespace

TEST_CASE("Two patches that change different lines merge", "[patch]") {
    // Expected: ../guides/script-patches.md#merge, Coney's own format (no counterpart in the original)
    const PatchSection first = patchTo(edit(base(), 2, 2, "the first mod's line 2\n"));
    const PatchSection second = patchTo(edit(base(), 8, 9, "the second mod's line\n"));
    const auto result = merge({first, second});
    CHECK(result.conflicts.empty());
    const std::string expected = edit(edit(base(), 8, 9, "the second mod's line\n"), 2, 2, "the first mod's line 2\n");
    CHECK(mergedText(result) == expected);
    CHECK(result.merged.result == coney::patch::textHash(expected));
}

TEST_CASE("Two patches that change the same line report one conflict", "[patch]") {
    // Expected: ../guides/script-patches.md#merge, Coney's own format (no counterpart in the original)
    const PatchSection first = patchTo(edit(base(), 4, 5, "the first mod's lines\n"));
    const PatchSection second = patchTo(edit(base(), 5, 6, "the second mod's lines\n"));
    const auto result = merge({first, second});
    REQUIRE(result.conflicts.size() == 1);
    CHECK(result.conflicts[0] ==
          MergeConflict{.earlier = 0, .later = 1, .earlierFirst = 4, .earlierEnd = 6, .laterFirst = 5, .laterEnd = 7});
    // Without a choice the earlier patch's change is kept.
    CHECK(mergedText(result) == edit(base(), 4, 5, "the first mod's lines\n"));
}

TEST_CASE("A conflict keeps the side the chooser picks", "[patch]") {
    // Expected: ../guides/script-patches.md#merge, Coney's own format (no counterpart in the original)
    const PatchSection first = patchTo(edit(base(), 4, 5, "the first mod's lines\n"));
    const PatchSection second = patchTo(edit(base(), 5, 6, "the second mod's lines\n"));
    const auto result = merge({first, second}, [](const MergeConflict&) { return MergeSide::Later; });
    CHECK(result.conflicts.size() == 1);
    CHECK(mergedText(result) == edit(base(), 5, 6, "the second mod's lines\n"));
}

TEST_CASE("Two patches whose ranges touch but share no line merge", "[patch]") {
    // Expected: ../guides/script-patches.md#merge, Coney's own format (no counterpart in the original)
    const PatchSection first = patchTo(edit(base(), 3, 4, "the first mod's lines\n"));
    const PatchSection second = patchTo(edit(base(), 5, 6, "the second mod's lines\n"));
    const auto result = merge({first, second});
    CHECK(result.conflicts.empty());
    CHECK(mergedText(result) == edit(base(), 3, 6, "the first mod's lines\nthe second mod's lines\n"));
}

TEST_CASE("An insertion at the edge of another patch's change goes on its own side", "[patch]") {
    // Expected: ../guides/script-patches.md#merge, Coney's own format (no counterpart in the original)
    const PatchSection replaced = patchTo(edit(base(), 4, 5, "replaced 4 and 5\n"));
    const PatchSection before = patchTo(insert(base(), 4, "put before line 4\n"));
    const PatchSection after = patchTo(insert(base(), 6, "put before line 6\n"));
    const auto result = merge({replaced, before, after});
    CHECK(result.conflicts.empty());
    CHECK(mergedText(result) == edit(base(), 4, 5, "put before line 4\nreplaced 4 and 5\nput before line 6\n"));
}

TEST_CASE("An identical change made by both patches is applied once", "[patch]") {
    // Expected: ../guides/script-patches.md#merge, Coney's own format (no counterpart in the original)
    const std::string edited = edit(base(), 7, 7, "the same new line 7\n");
    const auto result = merge({patchTo(edited), patchTo(edited)});
    CHECK(result.conflicts.empty());
    CHECK(mergedText(result) == edited);
}

TEST_CASE("Two different insertions at the same place are a conflict", "[patch]") {
    // Expected: ../guides/script-patches.md#merge, Coney's own format (no counterpart in the original)
    const PatchSection first = patchTo(insert(base(), 3, "the first mod's insertion\n"));
    const PatchSection second = patchTo(insert(base(), 3, "the second mod's insertion\n"));
    const auto result = merge({first, second});
    REQUIRE(result.conflicts.size() == 1);
    CHECK(result.conflicts[0].earlierFirst == 3);
    CHECK(result.conflicts[0].earlierEnd == 3);
    CHECK(result.conflicts[0].laterFirst == 3);
    CHECK(result.conflicts[0].laterEnd == 3);
}

TEST_CASE("An insertion inside another patch's changed range is a conflict", "[patch]") {
    // Expected: ../guides/script-patches.md#merge, Coney's own format (no counterpart in the original)
    const PatchSection first = patchTo(edit(base(), 3, 6, "four lines become one\n"));
    const PatchSection second = patchTo(insert(base(), 5, "put before line 5\n"));
    CHECK(merge({first, second}).conflicts.size() == 1);
}

TEST_CASE("Three patches merge one at a time in load order", "[patch]") {
    // Expected: ../guides/script-patches.md#merge, Coney's own format (no counterpart in the original)
    const PatchSection first = patchTo(edit(base(), 1, 1, "first\n"));
    const PatchSection second = patchTo(edit(base(), 1, 2, "second\n"));
    const PatchSection third = patchTo(edit(base(), 10, 10, "third\n"));
    const auto result = merge({first, second, third});
    REQUIRE(result.conflicts.size() == 1);
    CHECK(result.conflicts[0].earlier == 0);
    CHECK(result.conflicts[0].later == 1);
    CHECK(mergedText(result) == edit(edit(base(), 10, 10, "third\n"), 1, 1, "first\n"));
}

TEST_CASE("Patches of different bases do not merge", "[patch]") {
    // Expected: ../guides/script-patches.md#merge, Coney's own format (no counterpart in the original)
    const PatchSection first = patchTo(edit(base(), 2, 2, "two\n"));
    auto other = coney::patch::makePatch("another text\n", "another text, edited\n", coney::patch::PatchHeader{});
    REQUIRE(other.has_value());
    const auto merged = coney::patch::mergePatches(std::vector<PatchSection>{first, *other}, base());
    REQUIRE_FALSE(merged.has_value());
    CHECK(merged.error().fault == PatchFault::WrongBase);
}

TEST_CASE("A damaged patch is refused before it is merged", "[patch]") {
    // Expected: ../guides/script-patches.md#merge, Coney's own format (no counterpart in the original)
    PatchSection damaged = patchTo(edit(base(), 2, 2, "two\n"));
    damaged.result[0] ^= 0x01U;
    const auto merged =
        coney::patch::mergePatches(std::vector<PatchSection>{patchTo(edit(base(), 8, 8, "eight\n")), damaged}, base());
    REQUIRE_FALSE(merged.has_value());
    CHECK(merged.error().fault == PatchFault::DamagedResult);
}

TEST_CASE("A line built from a base line counts as the line it writes when merging", "[patch]") {
    // Expected: ../guides/script-patches.md#merge, Coney's own format (no counterpart in the original)
    // Both patches move line 2 below line 9, changed the same way, and so make the same change.
    std::string edited = edit(base(), 2, 2, "");
    edited = insert(edited, 9, "line 2 of the base, moved\n");
    const auto result = merge({patchTo(edited), patchTo(edited)});
    CHECK(result.conflicts.empty());
    CHECK(mergedText(result) == edited);
    CHECK(coney::patch::countOperations(result.merged).borrowed == 1);
}
