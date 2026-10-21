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


def test_braces_inside_a_parameter_or_initializer_list_open_no_function() -> None:
    text = (
        "namespace x {\n"
        "Tactic::Tactic() : Base(kType, {}), m_list{} {\n"
        "    run([](int v) { return v; });\n"
        "}\n"
        "void Other::go(Options options = {}) {}\n"
        "}\n"
    )
    found = [(f.qualified, f.line) for f in read_source("src/ai/t.cpp", text).functions]
    assert found == [("Tactic::Tactic", 2), ("Other::go", 5)]


def test_template_arguments_and_brace_members_do_not_make_names() -> None:
    text = (
        "namespace x {\n"
        "class C {\n"
        "    std::function<void(int)> make();\n"
        "    Foo m_f{bar(1)};\n"
        "    std::span<const int> view() const { return m_v; }\n"
        "};\n"
        "}\n"
    )
    assert [f.name for f in read_source("src/ai/c.h", text).functions] == ["make", "view"]


def test_digit_separators_are_not_character_literals() -> None:
    text = (
        "namespace x {\nconstexpr int kRate = 48'000;\nint rate() {\n    return 'a' + 0x1'0;\n}\n"
        "int next() { return 1; }\n}\n"
    )
    found = [(f.name, f.line) for f in read_source("src/audio/r.cpp", text).functions]
    assert found == [("rate", 3), ("next", 6)]


def test_a_character_literal_that_ends_a_line_keeps_the_line_count() -> None:
    text = "namespace x {\nint a() {\n    return '\n}\nint b() { return 1; }\n}\n"
    assert [(f.name, f.line) for f in read_source("src/audio/r.cpp", text).functions][-1] == ("b", 5)


def test_a_comment_after_code_on_its_line_documents_nothing_below() -> None:
    text = "namespace x {\nint a = 1; // @orig 0x00100010 Wrong_Owner (unknown)\nvoid next();\n}\n"
    source = read_source("src/ai/t.h", text)
    assert source.functions[0].tags == []
    assert [t.address for t in source.loose_tags] == [0x00100010]


def test_a_tag_in_a_block_comment_or_with_trailing_text_is_read() -> None:
    text = (
        "/** @orig 0x00100010 Brain_Think (Brain.cpp) */\nvoid think();\n"
        "/// @orig 0x00100020 Brain_Wait (unknown) and more\nvoid wait();\n"
    )
    found = [[(t.address, t.name, t.file) for t in f.tags] for f in read_source("src/ai/t.h", text).functions]
    assert found == [[(0x00100010, "Brain_Think", "Brain.cpp")], [(0x00100020, "Brain_Wait", "unknown")]]


def test_a_malformed_tag_is_reported() -> None:
    text = "/// @orig 0x0010001 Short_Address (unknown)\n/// @orig 0x00100020 NoFile\nvoid f();\n"
    assert [line for line, _ in read_source("src/ai/t.h", text).malformed] == [1, 2]


def test_a_function_knows_its_parameter_types() -> None:
    text = (
        "namespace x {\nclass P {\n    void play(int id, float volume = 1.0f);\n"
        "    void play(const Clip& clip);\n};\n}\n"
    )
    assert [f.params for f in read_source("src/audio/p.h", text).functions] == ["int,float", "const Clip&"]


def test_a_pointer_to_a_function_is_not_a_function() -> None:
    text = "void (*callback)(int, float) = nullptr;\nvoid real(int);\n"
    assert [f.name for f in read_source("src/ai/p.cpp", text).functions] == ["real"]


def test_a_member_after_an_access_label_starts_on_its_own_line() -> None:
    text = (
        "namespace x {\nclass A {\n  public:\n    /// Makes one.\n    /// More words.\n"
        "    static A create() { return A{}; }\n\n  private:\n    int m_a = 0;\n  protected: void g();\n};\n}\n"
    )
    found = [(f.name, f.line, f.lines, f.comment.count("///")) for f in read_source("src/ai/a.h", text).functions]
    assert found == [("create", 6, 1, 2), ("g", 10, 1, 0)]


def test_nested_braces_in_a_brace_initializer_keep_the_constructor() -> None:
    text = (
        "namespace x {\n"
        "A::A() : m_v{Vec{1, 2}} {}\n"
        "B::B() : m_v{{1, 2}}, m_w(3) {}\n"
        "C::C() : m_f{[this] { go(); }} {}\n"
        "void after() {}\n"
        "}\n"
    )
    found = [(f.qualified, f.line) for f in read_source("src/ai/a.cpp", text).functions]
    assert found == [("A::A", 2), ("B::B", 3), ("C::C", 4), ("after", 5)]
