// SPDX-License-Identifier: GPL-3.0-or-later
// Applying and making script patches (docs/guides/script-patches.md#apply, #make). The script is made up for the tests,
// in the shape of a mission script; it is no text of the game's.
#include "core/script_patch_apply.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include "core/script_patch.h"

using coney::patch::ColumnRange;
using coney::patch::LineRange;
using coney::patch::Operation;
using coney::patch::OpKind;
using coney::patch::PatchFault;
using coney::patch::PatchSection;

namespace {

// The base text: a made-up script of 16 lines and a final newline (so 17 lines, the last empty).
constexpr std::string_view kBase = "-- a made-up mission script for the patch tests\n"
                                   "local yard = {}\n"
                                   "function yard.start(level)\n"
                                   "    Level.SetTimer(level, 30)\n"
                                   "    Hud.Show(\"yard_start\")\n"
                                   "end\n"
                                   "\n"
                                   "function yard.fight(level)\n"
                                   "    local gang = Gang.Spawn(\"orphans\", 5)\n"
                                   "    Gang.Attack(gang, level.player)\n"
                                   "end\n"
                                   "\n"
                                   "function yard.finish(level)\n"
                                   "    Mission.Complete(level, \"the yard is ours\")\n"
                                   "end\n"
                                   "return yard\n";

// The base's lines, numbered from 1 as a patch numbers them (the vector's 0 is line 1).
std::vector<std::string_view> baseLines() { return coney::patch::splitLines(kBase); }

// What handWritten() makes of kBase, written out in full.
std::string expectedHandWritten() {
    return "-- a made-up mission script for the patch tests\n"
           "local yard = {}\n"
           "function yard.start(level)\n"
           "    Coney.Log(\"the yard fight starts early\")\n"
           "local yard = {}\n"
           "end\n"
           "\n"
           "function yard.fight(level)\n"
           "    local gang = Gang.Spawn(\"orphans\", 7)\n"
           "    Gang.Attack(gang, level.player)\n"
           "end\n"
           "\n"
           "function yard.finish(level)\n"
           "    Mission.Complete(level, \"the yard is ours\")\n"
           "end\n"
           "return yard\n";
}

// A section of kBase, built by hand: keep 1-3, drop 4 and 5 (the timer and the HUD), write two lines, copy line 2,
// rebuild line 9 with 7 gangsters instead of 5, keep the rest.
PatchSection handWritten() {
    const auto lines = baseLines();
    PatchSection section;
    section.chunk = coney::patch::textHash("a made-up chunk");
    section.form = "lua4 1";
    section.base = coney::patch::textHash(kBase);
    section.operations = {
        Operation{.kind = OpKind::Keep, .lines = LineRange{1, 3}},
        Operation{.kind = OpKind::Drop,
                  .lines = LineRange{4, 5},
                  .hash = coney::patch::linesHash(std::span(lines).subspan(3, 2))},
        Operation{.kind = OpKind::Add, .text = "    Coney.Log(\"the yard fight starts early\")"},
        Operation{.kind = OpKind::Copy, .lines = LineRange{2, 2}},
        Operation{.kind = OpKind::Keep, .lines = LineRange{6, 8}},
        Operation{.kind = OpKind::Rebuild,
                  .lines = LineRange{9, 9},
                  .pieces = {ColumnRange{0, 38}, std::string("7"), ColumnRange{40, std::nullopt}}},
        Operation{.kind = OpKind::Keep, .lines = LineRange{10, std::nullopt}},
    };
    section.result = coney::patch::textHash(expectedHandWritten());
    return section;
}

} // namespace

TEST_CASE("Applying a patch gives the text with its result hash", "[patch]") {
    // Expected: ../guides/script-patches.md#apply, Coney's own format (no counterpart in the original)
    const PatchSection section = handWritten();
    const auto result = coney::patch::applyPatch(section, kBase);
    REQUIRE(result.has_value());
    CHECK(*result == expectedHandWritten());
    CHECK(coney::patch::textHash(*result) == section.result);
}

TEST_CASE("A patch is refused for a text that is not its base", "[patch]") {
    // Expected: ../guides/script-patches.md#apply, Coney's own format (no counterpart in the original)
    std::string other(kBase);
    other[4] = 'A'; // one byte changed: another script, or another form of it
    const auto result = coney::patch::applyPatch(handWritten(), other);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().fault == PatchFault::WrongBase);
    CHECK(result.error().message.find("the patch is for another text: its base is sha256:") == 0);
}

TEST_CASE("A patch whose dropped range was damaged is refused", "[patch]") {
    // Expected: ../guides/script-patches.md#apply, Coney's own format (no counterpart in the original)
    PatchSection section = handWritten();
    section.operations[1].hash[0] ^= 0x01U;
    const auto result = coney::patch::applyPatch(section, kBase);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().fault == PatchFault::DamagedRange);
    CHECK(result.error().message == "the patch is damaged: base lines 4-5 do not have the hash it gives");
}

TEST_CASE("A patch whose result hash was damaged is refused", "[patch]") {
    // Expected: ../guides/script-patches.md#apply, Coney's own format (no counterpart in the original)
    PatchSection section = handWritten();
    section.result[31] ^= 0x80U;
    const auto result = coney::patch::applyPatch(section, kBase);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().fault == PatchFault::DamagedResult);
    CHECK(result.error().message.find("the patch is damaged: its result is sha256:") == 0);
}

TEST_CASE("A patch that does not fit its base is refused", "[patch]") {
    // Expected: ../guides/script-patches.md#apply, Coney's own format (no counterpart in the original)
    PatchSection section = handWritten();
    const auto [index, op, says] = GENERATE(table<std::size_t, Operation, std::string_view>({
        {6, Operation{.kind = OpKind::Keep, .lines = LineRange{10, 40}}, "base lines 10-40"},
        {6, Operation{.kind = OpKind::Keep, .lines = LineRange{10, 12}}, "leaves base lines 13-17"},
        {3, Operation{.kind = OpKind::Copy, .lines = LineRange{16, 18}}, "base lines 16-18"},
        {5, Operation{.kind = OpKind::Rebuild, .lines = LineRange{9, 9}, .pieces = {ColumnRange{0, 99}}},
         "columns 0-99"},
        {4, Operation{.kind = OpKind::Keep, .lines = LineRange{7, 8}}, "out of order: base line 6 comes next"},
    }));
    section.operations[index] = op;
    const auto result = coney::patch::applyPatch(section, kBase);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().fault == PatchFault::DoesNotFit);
    CAPTURE(result.error().message);
    CHECK(result.error().message.find(says) != std::string::npos);
}

TEST_CASE("A patch of copies cannot grow a text past the limit", "[patch]") {
    // Expected: ../guides/script-patches.md#limits, Coney's own format (no counterpart in the original)
    PatchSection section = handWritten();
    // 16 lines copied 20,000 times: 320,000 lines, past kMaxTextLines.
    section.operations.insert(section.operations.begin() + 1, 20'000,
                              Operation{.kind = OpKind::Copy, .lines = LineRange{1, 16}});
    const auto result = coney::patch::applyPatch(section, kBase);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().fault == PatchFault::TooLarge);
}

namespace {

// An edit of kBase that a modder might make, with a name for the test's report.
struct Edit {
    std::string_view name;
    std::string edited;
};

// kBase with `from` replaced by `to` (once).
std::string replaced(std::string_view from, std::string_view to) {
    std::string text(kBase);
    const auto at = text.find(from);
    REQUIRE(at != std::string::npos);
    text.replace(at, from.size(), to);
    return text;
}

// The edits make and apply are tried on: one of each kind of change, and all of them at once.
std::vector<Edit> edits() {
    const std::string fight = "function yard.fight(level)\n"
                              "    local gang = Gang.Spawn(\"orphans\", 5)\n"
                              "    Gang.Attack(gang, level.player)\n"
                              "end\n"
                              "\n";
    std::string moved = replaced(fight, "");
    moved.insert(moved.find("function yard.start"), fight);
    std::string everything = replaced("\"orphans\", 5)", "\"orphans\", 7)");
    everything.replace(everything.find("    Level.SetTimer(level, 30)\n"), 30, "");
    everything += "-- a line of the author's own\n";
    return {
        {"nothing changed", std::string(kBase)},
        {"one number changed", replaced("SetTimer(level, 30)", "SetTimer(level, 45)")},
        {"a line added", replaced("end\nreturn", "end\nConey.Log(\"finished\")\nreturn")},
        {"a line removed", replaced("    Hud.Show(\"yard_start\")\n", "")},
        {"a function moved", moved},
        {"a function duplicated", std::string(kBase) + fight},
        {"no final newline", std::string(kBase.substr(0, kBase.size() - 1))},
        {"everything replaced", "print(\"a whole new script\")\n"},
        {"emptied", ""},
        {"several changes", everything},
    };
}

} // namespace

TEST_CASE("A made patch applies to give the edited copy", "[patch]") {
    // Expected: ../guides/script-patches.md#make, Coney's own format (no counterpart in the original)
    const auto all = edits();
    const auto i = GENERATE_COPY(range(std::size_t{0}, all.size()));
    const Edit& edit = all[i];
    CAPTURE(edit.name);
    const auto made = coney::patch::makePatch(kBase, edit.edited, coney::patch::PatchHeader{.name = "yard.lua"});
    REQUIRE(made.has_value());
    CHECK(made->base == coney::patch::textHash(kBase));
    CHECK(made->result == coney::patch::textHash(edit.edited));
    CHECK(made->name == "yard.lua");
    CHECK(made->form == "lua4 1");
    const auto applied = coney::patch::applyPatch(*made, kBase);
    REQUIRE(applied.has_value());
    CHECK(*applied == edit.edited);
}

TEST_CASE("A made patch holds no line of the base", "[patch]") {
    // Expected: ../guides/script-patches.md#make, Coney's own format (no counterpart in the original)
    const auto all = edits();
    const auto i = GENERATE_COPY(range(std::size_t{0}, all.size()));
    const Edit& edit = all[i];
    CAPTURE(edit.name);
    const auto made = coney::patch::makePatch(kBase, edit.edited, coney::patch::PatchHeader{});
    REQUIRE(made.has_value());
    coney::patch::ScriptPatch patch;
    patch.sections.push_back(*made);
    const std::string written = coney::patch::writeScriptPatch(patch);
    // Every base line with words in it (not `end`, a blank or a lone brace) is absent from the file, whole or as the
    // body of a quoted piece.
    for (const std::string_view line : baseLines()) {
        if (line.size() < 8) {
            continue;
        }
        CAPTURE(line);
        CHECK(written.find(line) == std::string::npos);
    }
}

TEST_CASE("A moved block becomes a copy and a changed number a rebuild", "[patch]") {
    // Expected: ../guides/script-patches.md#make, Coney's own format (no counterpart in the original)
    const auto all = edits();
    const auto moved = coney::patch::makePatch(kBase, all[4].edited, coney::patch::PatchHeader{});
    REQUIRE(moved.has_value());
    const auto counts = coney::patch::countOperations(*moved);
    CHECK(counts.copied == 5);
    CHECK(counts.dropped == 5);
    CHECK(counts.added == 0);
    CHECK(counts.rebuilt == 0);

    const auto number = coney::patch::makePatch(kBase, all[1].edited, coney::patch::PatchHeader{});
    REQUIRE(number.has_value());
    REQUIRE(number->operations.size() == 3);
    CHECK(number->operations[0] == Operation{.kind = OpKind::Keep, .lines = LineRange{1, 3}});
    CHECK(number->operations[1] == Operation{.kind = OpKind::Rebuild,
                                             .lines = LineRange{4, 4},
                                             .pieces = {ColumnRange{0, 25}, std::string("45)")}});
    CHECK(number->operations[2] == Operation{.kind = OpKind::Keep, .lines = LineRange{5, std::nullopt}});
}

TEST_CASE("A patch between equal texts keeps every line", "[patch]") {
    // Expected: ../guides/script-patches.md#make, Coney's own format (no counterpart in the original)
    const auto made = coney::patch::makePatch(kBase, kBase, coney::patch::PatchHeader{});
    REQUIRE(made.has_value());
    REQUIRE(made->operations.size() == 1);
    CHECK(made->operations[0] == Operation{.kind = OpKind::Keep, .lines = LineRange{1, std::nullopt}});
    CHECK(made->result == made->base);
}

TEST_CASE("Making a patch of a text past the limit is refused", "[patch]") {
    // Expected: ../guides/script-patches.md#limits, Coney's own format (no counterpart in the original)
    const std::string huge(coney::patch::kMaxTextBytes + 1, '\n');
    const auto made = coney::patch::makePatch(kBase, huge, coney::patch::PatchHeader{});
    REQUIRE_FALSE(made.has_value());
    CHECK(made.error().fault == PatchFault::TooLarge);
}

TEST_CASE("A moved line that was also changed is built from its base line", "[patch]") {
    // Expected: ../guides/script-patches.md#make, Coney's own format (no counterpart in the original)
    // The fight function moved above the start, and its gang of 5 made 7 on the way.
    std::string edited = edits()[4].edited;
    const auto at = edited.find("\"orphans\", 5)");
    REQUIRE(at != std::string::npos);
    edited[at + 11] = '7';
    const auto made = coney::patch::makePatch(kBase, edited, coney::patch::PatchHeader{});
    REQUIRE(made.has_value());
    const auto counts = coney::patch::countOperations(*made);
    CHECK(counts.added == 0);
    CHECK(counts.borrowed == 1);
    const auto applied = coney::patch::applyPatch(*made, kBase);
    REQUIRE(applied.has_value());
    CHECK(*applied == edited);
    coney::patch::ScriptPatch patch;
    patch.sections.push_back(*made);
    CHECK(coney::patch::writeScriptPatch(patch).find("Gang.Spawn") == std::string::npos);
}

TEST_CASE("A line built from a base line the text does not have is refused", "[patch]") {
    // Expected: ../guides/script-patches.md#apply, Coney's own format (no counterpart in the original)
    PatchSection section = handWritten();
    section.operations.insert(
        section.operations.begin() + 1,
        Operation{.kind = OpKind::Borrow, .lines = LineRange{40, 40}, .pieces = {ColumnRange{0, 2}}});
    const auto result = coney::patch::applyPatch(section, kBase);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().fault == PatchFault::DoesNotFit);
    CHECK(result.error().message == "the patch builds a line from base line 40, but the text has 17 lines");
}
