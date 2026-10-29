// SPDX-License-Identifier: GPL-3.0-or-later
// The `.cpatch` reader and writer (docs/guides/script-patches.md#format). Every text here is made up for the tests.
#include "core/script_patch.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "core/sha256.h"

using coney::patch::ColumnRange;
using coney::patch::LineRange;
using coney::patch::Operation;
using coney::patch::OpKind;
using coney::patch::PatchSection;
using coney::patch::ScriptPatch;

namespace {

// A digest whose every byte is `value`, so each header hash of a fixture is told apart at a glance.
coney::Sha256Digest digestOf(std::uint8_t value) {
    coney::Sha256Digest digest{};
    digest.fill(value);
    return digest;
}

// 64 hex digits of the digest digestOf(value), as a `.cpatch` header writes it after `sha256:`.
std::string hexOf(std::uint8_t value) { return coney::toHex(digestOf(value)); }

// A header with every key, the hashes made of repeated bytes 0x11 (chunk), 0x22 (base) and 0x33 (result).
std::string header() {
    return "entry  = 0x1c2d3e4f\n"
           "name   = \"level99.lua\"\n"
           "chunk  = \"sha256:" +
           hexOf(0x11) +
           "\"\n"
           "form   = \"lua4 1\"\n"
           "base   = \"sha256:" +
           hexOf(0x22) +
           "\"\n"
           "result = \"sha256:" +
           hexOf(0x33) + "\"\n";
}

// A section with each kind of operation, built in code, to be written and read back.
PatchSection everyKindOfOperation() {
    PatchSection section;
    section.entry = 0x1c2d3e4fU;
    section.name = "level99.lua";
    section.chunk = digestOf(0x11);
    section.form = "lua4 1";
    section.base = digestOf(0x22);
    section.result = digestOf(0x33);
    section.operations.push_back(Operation{.kind = OpKind::Keep, .lines = LineRange{1, 4}});
    section.operations.push_back(Operation{.kind = OpKind::Drop, .lines = LineRange{5, 6}, .hash = digestOf(0x44)});
    section.operations.push_back(Operation{.kind = OpKind::Add, .text = "Coney.Log(\"the yard fight starts early\")"});
    section.operations.push_back(Operation{.kind = OpKind::Copy, .lines = LineRange{2, 3}});
    section.operations.push_back(Operation{
        .kind = OpKind::Rebuild,
        .lines = LineRange{7, 7},
        .pieces = {ColumnRange{0, 27}, std::string("3"), ColumnRange{29, std::nullopt}},
    });
    section.operations.push_back(Operation{.kind = OpKind::Keep, .lines = LineRange{8, std::nullopt}});
    return section;
}

} // namespace

TEST_CASE("A .cpatch reads its header and every kind of operation", "[patch]") {
    // Expected: ../guides/script-patches.md#format, Coney's own format (no counterpart in the original)
    const std::string text = "coney-script-patch 1\n"
                             "# a made-up patch\n" +
                             header() +
                             "---\n"
                             "= 1-4          # keep\n"
                             "- 5-6 sha256:" +
                             hexOf(0x44) +
                             "\n"
                             "+ Coney.Log(\"the yard fight starts early\")\n"
                             "< 2-3\n"
                             "~ 7 [0-27] \"3\" [29-]\n"
                             "= 8-\n";
    const auto patch = coney::patch::readScriptPatch(text);
    REQUIRE(patch.has_value());
    REQUIRE(patch->sections.size() == 1);
    CHECK(patch->sections[0] == everyKindOfOperation());
}

TEST_CASE("A written .cpatch reads back equal", "[patch]") {
    // Expected: ../guides/script-patches.md#format, Coney's own format (no counterpart in the original)
    PatchSection section = everyKindOfOperation();
    // Lines that need a quote to survive: a tab inside is fine as it is, but a trailing space, a carriage return, a
    // quote at the start, a backslash and an empty line must come back byte for byte.
    for (const std::string_view line :
         {"\tindented with a tab", "ends with a space ", "ends with a return\r", "\"starts with a quote",
          "a \\ backslash and a \"quote\"", "", "a bell \x07 inside"}) {
        section.operations.insert(section.operations.end() - 1,
                                  Operation{.kind = OpKind::Add, .text = std::string(line)});
    }
    section.operations.insert(section.operations.end() - 1,
                              Operation{.kind = OpKind::Rebuild,
                                        .lines = LineRange{8, 8},
                                        .pieces = {std::string("\"q\" \\ \t"), ColumnRange{4, 4}}});
    section.operations.back().lines.first = 9;
    ScriptPatch patch;
    patch.sections.push_back(section);
    PatchSection second = section;
    second.entry.reset();
    second.name.reset();
    second.chunk = digestOf(0x55);
    second.base = digestOf(0x66);
    patch.sections.push_back(second);

    const std::string written = coney::patch::writeScriptPatch(patch);
    CHECK(written.starts_with("coney-script-patch 1\n"));
    const auto read = coney::patch::readScriptPatch(written);
    REQUIRE(read.has_value());
    CHECK(*read == patch);
    // Writing what was read gives the same file, so a patch passed through Coney does not change.
    CHECK(coney::patch::writeScriptPatch(*read) == written);
}

TEST_CASE("A .cpatch with two sections finds the one for a base or a chunk", "[patch]") {
    // Expected: ../guides/script-patches.md#sections, Coney's own format (no counterpart in the original)
    ScriptPatch patch;
    patch.sections.push_back(everyKindOfOperation());
    PatchSection other = everyKindOfOperation();
    other.chunk = digestOf(0x77);
    other.base = digestOf(0x88);
    patch.sections.push_back(other);
    CHECK(patch.sectionForBase(digestOf(0x22)) == &patch.sections[0]);
    CHECK(patch.sectionForBase(digestOf(0x88)) == &patch.sections[1]);
    CHECK(patch.sectionForChunk(digestOf(0x77)) == &patch.sections[1]);
    CHECK(patch.sectionForBase(digestOf(0x99)) == nullptr);
    CHECK(patch.sectionForChunk(digestOf(0x22)) == nullptr);
}

TEST_CASE("A .cpatch that breaks the format is refused with the line at fault", "[patch]") {
    // Expected: ../guides/script-patches.md#format, Coney's own format (no counterpart in the original)
    struct Bad {
        std::string text;
        std::string_view says; // part of the message
    };
    const std::string start = "coney-script-patch 1\n" + header() + "---\n";
    const auto [text, says] = GENERATE_COPY(values<Bad>({
        {"coney-script-patch 2\n" + header() + "---\n= 1-\n", "version 2"},
        {"diff --git a b\n", "line 1"},
        {"coney-script-patch 1\nchunk = \"sha256:" + hexOf(0x11) + "\"\n---\n= 1-\n",
         "line 3: the section's header has no form"},
        {"coney-script-patch 1\n" + header() + "form = \"lua4 1\"\n---\n= 1-\n", "line 8: form given twice"},
        {"coney-script-patch 1\n" + header() + "colour = \"red\"\n---\n= 1-\n", "line 8: unknown key"},
        {start + "= 1-3\n= 5-\n", "line 10"},
        {start + "- 1-3\n", "line 9"},
        {start + "- 1-3 sha256:abc\n", "line 9"},
        {start + "= 3-1\n", "line 9"},
        {start + "- 1- sha256:" + hexOf(0x44) + "\n", "line 9"},
        {start + "< 0-2\n", "line 9"},
        {start + "+\"unterminated\n", "line 9"},
        {start + "~ 1 [5-2]\n", "line 9"},
        {start + "~ 1 [0-3] [2-]\n", "line 9"},
        {start + "? 1\n", "line 9"},
        {start + "= 1-\n= 5-6\n", "line 10"},
        {"coney-script-patch 1\n" + header(), "ends inside"},
    }));
    CAPTURE(text);
    const auto patch = coney::patch::readScriptPatch(text);
    REQUIRE_FALSE(patch.has_value());
    CAPTURE(patch.error().message);
    CHECK(patch.error().message.find(says) != std::string::npos);
}

TEST_CASE("A .cpatch past its limits is refused", "[patch]") {
    // Expected: ../guides/script-patches.md#limits, Coney's own format (no counterpart in the original)
    SECTION("a file larger than kMaxPatchBytes") {
        std::string text = "coney-script-patch 1\n" + header() + "---\n";
        text.append(coney::patch::kMaxPatchBytes, '#');
        const auto patch = coney::patch::readScriptPatch(text);
        REQUIRE_FALSE(patch.has_value());
        CHECK(patch.error().message.find("larger than") != std::string::npos);
    }
    SECTION("more sections than kMaxSections") {
        std::string text = "coney-script-patch 1\n";
        for (std::size_t i = 0; i <= coney::patch::kMaxSections; ++i) {
            text += header() + "---\n= 1-\n";
        }
        const auto patch = coney::patch::readScriptPatch(text);
        REQUIRE_FALSE(patch.has_value());
        CHECK(patch.error().message.find("sections") != std::string::npos);
    }
}

TEST_CASE("A text's lines keep a final newline as an empty last line", "[patch]") {
    // Expected: ../guides/script-patches.md#lines, Coney's own format (no counterpart in the original)
    using V = std::vector<std::string_view>;
    CHECK(coney::patch::splitLines("a\nb\n") == V{"a", "b", ""});
    CHECK(coney::patch::splitLines("a\nb") == V{"a", "b"});
    CHECK(coney::patch::splitLines("") == V{""});
    CHECK(coney::patch::splitLines("a\r\n") == V{"a\r", ""});
}

TEST_CASE("A dropped range's hash covers each line and its newline", "[patch]") {
    // Expected: ../guides/script-patches.md#lines, Coney's own format (no counterpart in the original)
    const std::vector<std::string_view> lines{"one", "two"};
    coney::Sha256 expected;
    expected.update(std::string_view("one\ntwo\n"));
    CHECK(coney::patch::linesHash(lines) == expected.finish());
    CHECK(coney::patch::hashText(digestOf(0xab)) == "sha256:" + hexOf(0xab));
}

TEST_CASE("A .cpatch reads and writes a line built from another base line", "[patch]") {
    // Expected: ../guides/script-patches.md#operations, Coney's own format (no counterpart in the original)
    const std::string text = "coney-script-patch 1\n" + header() +
                             "---\n"
                             "= 1-8\n"
                             "^ 3 [0-8] \"!\" [10-]   # base line 3, changed, written after line 8\n"
                             "= 9-\n"
                             "^ 2 [0-]\n";
    const auto patch = coney::patch::readScriptPatch(text);
    REQUIRE(patch.has_value());
    const auto& ops = patch->sections.at(0).operations;
    REQUIRE(ops.size() == 4);
    CHECK(ops[1] == Operation{.kind = OpKind::Borrow,
                              .lines = LineRange{3, 3},
                              .pieces = {ColumnRange{0, 8}, std::string("!"), ColumnRange{10, std::nullopt}}});
    // It consumes nothing: the keep after it goes on from line 9, and one may follow a keep to the end.
    CHECK(ops[3] ==
          Operation{.kind = OpKind::Borrow, .lines = LineRange{2, 2}, .pieces = {ColumnRange{0, std::nullopt}}});
    const std::string written = coney::patch::writeScriptPatch(*patch);
    CHECK(written.find("\n^ 3 [0-8] \"!\" [10-]\n") != std::string::npos);
    const auto again = coney::patch::readScriptPatch(written);
    REQUIRE(again.has_value());
    CHECK(*again == *patch);

    const auto range = coney::patch::readScriptPatch("coney-script-patch 1\n" + header() + "---\n^ 3-4 [0-]\n");
    REQUIRE_FALSE(range.has_value());
    CHECK(range.error().message.find("line 9") != std::string::npos);
}
