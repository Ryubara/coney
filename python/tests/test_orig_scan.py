# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the light C++ reader behind `coney-tools orig`, on small made-up sources."""

from coney_tools.orig_scan import read_source

HEADER = """// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A made-up brain. Research: docs/research/made-up.md#brain
#include <cstdint>

namespace coney::ai {

/// The brain.
/// Research: docs/research/made-up.md#class
class Brain {
  public:
    /// Thinks once.
    /// @orig 0x00100010 Brain_Think (Brain.cpp)
    void think(int when);

    /// Two original functions in one.
    /// @orig 0x00100020 Brain_Reset (unknown)
    /// @orig 0x00100030 Brain_Clear (unknown)
    void reset();

    /// An accessor.
    [[nodiscard]] int count() const { return m_count; }

  private:
    int m_count{0};
    int m_other = 3;
};

} // namespace coney::ai
"""

SOURCE = r"""// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/brain.h"

namespace coney::ai {

namespace {

// A helper with a brace in a string and a char literal: "{" '}'.
int helper(const char* text) {
    const char* s = "{ not a brace }";
    char c = '{';
    return s[0] + c + 1'000;
}

} // namespace

Brain::Brain() : m_count{1}, m_other{2} {
    auto lambda = [](int x) { return x; };
    (void)lambda;
}

void Brain::think(int when) {
    if (when > 0) {
        // The original also counts here.
        // @orig 0x00100040 Brain_Count (unknown)
        ++m_count;
    }
}

static int fileLocal() { return 2; }

const int kTable[] = {1, 2, 3};

void freeFunction() {
    const auto* raw = R"(a "{" raw string)";
    (void)raw;
}

} // namespace coney::ai
"""


def test_a_declaration_in_a_class_carries_the_tags_of_its_comment() -> None:
    source = read_source("src/ai/brain.h", HEADER)
    by_name = {f.name: f for f in source.functions}
    think = by_name["think"]
    assert (think.scope, think.body, think.in_class) == ("Brain", False, True)
    assert [(t.address, t.name, t.file) for t in think.tags] == [(0x00100010, "Brain_Think", "Brain.cpp")]
    assert [t.address for t in by_name["reset"].tags] == [0x00100020, 0x00100030]


def test_an_accessor_defined_in_its_class_is_an_inline_definition() -> None:
    count = next(f for f in read_source("src/ai/brain.h", HEADER).functions if f.name == "count")
    assert count.body and count.in_class and not count.tags


def test_member_initializers_are_not_functions() -> None:
    names = [f.name for f in read_source("src/ai/brain.h", HEADER).functions]
    assert names == ["think", "reset", "count"]


def test_the_file_and_class_comments_are_kept() -> None:
    source = read_source("src/ai/brain.h", HEADER)
    assert "docs/research/made-up.md#brain" in source.header_comment
    assert "docs/research/made-up.md#class" in source.class_comments["Brain"]


def test_definitions_in_a_source_file_are_found_with_their_scope() -> None:
    source = read_source("src/ai/brain.cpp", SOURCE)
    found = [(f.qualified, f.body, f.helper) for f in source.functions]
    assert found == [
        ("helper", True, True),
        ("Brain::Brain", True, False),
        ("Brain::think", True, False),
        ("fileLocal", True, True),
        ("freeFunction", True, False),
    ]


def test_a_tag_inside_a_body_belongs_to_the_function_it_is_in() -> None:
    source = read_source("src/ai/brain.cpp", SOURCE)
    think = next(f for f in source.functions if f.name == "think")
    assert [(t.address, t.line) for t in think.body_tags] == [(0x00100040, 25)]
    assert source.tag_owner[25] == "Brain::think"


def test_a_function_knows_how_long_it_is() -> None:
    think = next(f for f in read_source("src/ai/brain.cpp", SOURCE).functions if f.name == "think")
    assert (think.line, think.lines) == (22, 7)


def test_a_tag_on_a_class_comment_is_loose_and_names_the_class() -> None:
    text = "namespace x {\n/// @orig 0x00100050 Thing_Construct (unknown)\nclass Thing {\n};\n}\n"
    source = read_source("src/ai/thing.h", text)
    assert [t.address for t in source.loose_tags] == [0x00100050]
    assert source.tag_owner[2] == "Thing"
