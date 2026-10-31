# SPDX-License-Identifier: GPL-3.0-or-later
"""The scanners of the code and test standards (standards_scan): what each rule counts, on made-up sources."""

from __future__ import annotations

from conventions_support import REPO

from coney_tools.conventions import load_conventions
from coney_tools.orig_scan import read_source
from coney_tools.standards_scan import Finding, case_spans, cited_sections, counts, scan_code, scan_tests

STANDARDS = load_conventions(REPO).standards
GAMEPLAY = ("src/ai/",)


def code(files: dict[str, str]) -> dict[str, list[Finding]]:
    """The code scan of made-up files (path to text), with `src/ai/` the one gameplay folder."""
    return scan_code([read_source(path, text) for path, text in files.items()], GAMEPLAY)


def rules(findings: list[Finding]) -> list[str]:
    """The rule of each finding, in order."""
    return [f.rule for f in findings]


# --- comments --------------------------------------------------------------------------------------------------------


def test_a_documented_header_passes() -> None:
    header = """// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// A counter.
class Counter {
public:
    /// Adds one.
    void bump();
    /// The count so far.
    int value() const { return m_value; }

private:
    int m_value = 0;
};
"""
    assert code({"src/core/counter.h": header}) == {}


def test_a_function_with_no_comment_is_a_finding_with_its_line() -> None:
    source = """#include "counter.h"

namespace {

int twice(int x) {
    int y = x;
    y += x;
    return y;
}

}  // namespace
"""
    found = code({"src/core/counter.cpp": source})["src/core/counter.cpp"]
    assert [(f.rule, f.line) for f in found] == [("comment", 5)]
    assert "twice" in found[0].message


def test_a_definition_is_covered_by_the_comment_of_its_declaration() -> None:
    header = "#pragma once\n/// A counter.\nclass Counter {\npublic:\n    /// Adds one.\n    void bump();\n};\n"
    source = '#include "counter.h"\nvoid Counter::bump() {\n    int a = 1;\n    a += 1;\n}\n'
    assert code({"src/core/counter.h": header, "src/core/counter.cpp": source}) == {}


def test_a_private_one_statement_accessor_and_a_special_member_need_no_comment() -> None:
    header = """#pragma once
/// A counter.
class Counter {
public:
    Counter();
    ~Counter();

private:
    int value() const { return m_value; }
    void set(int v) { m_value = v; }
    int m_value = 0;
};
"""
    assert code({"src/core/counter.h": header}) == {}


def test_a_public_one_statement_accessor_needs_a_doc_comment() -> None:
    # AGENTS.md and conventions.md rule 1: every public function has a doc comment; rule 2's "the name says
    # everything" is for the other functions only.
    header = """#pragma once
/// A counter.
class Counter {
public:
    int value() const { return m_value; }
    /// Sets the count to `v`.
    void set(int v) { m_value = v; }

private:
    int m_value = 0;
};
"""
    found = code({"src/core/counter.h": header})["src/core/counter.h"]
    assert [(f.rule, f.line) for f in found] == [("comment", 5)]


def test_a_header_declaration_without_a_comment_is_a_finding() -> None:
    header = "#pragma once\n/// A counter.\nclass Counter {\npublic:\n    void bump();\n};\n"
    found = code({"src/core/counter.h": header})["src/core/counter.h"]
    assert [(f.rule, f.line) for f in found] == [("comment", 5)]


def test_a_type_a_header_declares_needs_a_comment_and_a_source_file_does_not() -> None:
    header = "#pragma once\nstruct Bare {\n    int x;\n};\n"
    source = "struct Local {\n    int x;\n};\n"
    found = code({"src/core/bare.h": header, "src/core/local.cpp": source})
    assert [(f.rule, f.line) for f in found["src/core/bare.h"]] == [("type-doc", 2)]
    assert "src/core/local.cpp" not in found


def test_an_override_is_documented_by_the_declaration_it_overrides() -> None:
    # The base declaration is often in another folder; the rule is that a function whose declaration carries the doc
    # comment needs none (conventions.md rule 2), so an `override` or `final` of a virtual needs no comment of its own.
    base = (
        "#pragma once\n/// A sink.\nclass Sink {\npublic:\n"
        "    /// Sends `text`.\n    virtual void send(int text) = 0;\n};\n"
    )
    derived = (
        '#pragma once\n#include "sink.h"\n/// A socket sink.\nclass Socket : public Sink {\npublic:\n'
        "    void send(int text) override;\n    void flush(int mode) final;\n};\n"
    )
    source = (
        '#include "socket.h"\nvoid Socket::send(int text) {\n    int a = text;\n    a += 1;\n}\n'
        "void Socket::flush(int mode) {\n    int a = mode;\n    a += 1;\n}\n"
    )
    files = {"src/core/sink.h": base, "src/net/socket.h": derived, "src/net/socket.cpp": source}
    assert code(files) == {}


def test_a_function_that_only_looks_like_an_override_still_needs_a_comment() -> None:
    header = "#pragma once\n/// A socket.\nclass Socket {\npublic:\n    void send(int text);\n};\n"
    assert rules(code({"src/net/socket.h": header})["src/net/socket.h"]) == ["comment"]


def test_a_one_statement_forwarder_needs_a_comment_only_where_it_is_public() -> None:
    # In a header it is public (rule 1); in a source file or an anonymous namespace its name says everything (rule 2).
    header = """#pragma once
/// Writes `text`.
void write(int level, int text);
inline void warn(int text) { write(1, text); }
namespace {
inline void info(int text) { write(2, text); }
}
"""
    source = """#include "log.h"
void note(int text) { write(4, text); }
static void detail(int text) {
    int a = text;
    write(3, a);
}
"""
    found = code({"src/core/log.h": header, "src/core/log.cpp": source})
    assert [(f.rule, f.line) for f in found["src/core/log.h"]] == [("comment", 4)]
    assert [(f.rule, f.line) for f in found["src/core/log.cpp"]] == [("comment", 3)]


def test_only_public_types_need_a_doc_comment() -> None:
    header = """#pragma once
/// A schedule.
class Schedule {
public:
    struct Visible {
        int x;
    };

private:
    struct Entry {
        int x;
    };
    class Deeper {
    public:
        struct Inside {
            int y;
        };
    };
    int m_n = 0;
};

namespace {
struct Local {
    int x;
};
}  // namespace

struct Plain {
    int x;
};
"""
    found = code({"src/core/schedule.h": header})["src/core/schedule.h"]
    named = sorted(f.message.split()[0] for f in found if f.rule == "type-doc")
    assert named == ["Plain", "Schedule::Visible"]


# --- the form of a citation ------------------------------------------------------------------------------------------


def test_a_citation_reads_with_or_without_the_research_folder() -> None:
    rule = STANDARDS.tests.citation
    with_folder = cited_sections("// Expected: research/combat/grabs.md#grab, confirmed (code)", rule)
    assert with_folder == [("combat/grabs.md", "grab")]
    assert cited_sections("    // Expected: combat/grabs.md#grab, inferred", rule) == [("combat/grabs.md", "grab")]
    in_docs = cited_sections("// Expected: docs/research/combat/grabs.md#grab, inferred", rule)
    assert in_docs == [("combat/grabs.md", "grab")]
    assert cited_sections("// Expected: unverified, #12", rule) == []
    assert cited_sections("// combat/grabs.md#grab is not after the marker", rule) == []


# --- tags ------------------------------------------------------------------------------------------------------------

BODY = "{\n    int a = 1;\n    a += 1;\n    return a;\n}\n"


def test_a_gameplay_function_needs_a_tag_or_a_marker() -> None:
    source = (
        "// Tagged.\n// @orig 0x00123456 Brain_Think (Brain.cpp)\nint think() " + BODY + "\n"
        "// Coney's own.\n// @coney-only a debug view\nint peek() " + BODY + "\n"
        "// Not either.\nint guess() " + BODY
    )
    found = code({"src/ai/brain.cpp": source})["src/ai/brain.cpp"]
    assert [(f.rule, f.line) for f in found] == [("tag", 18)]
    assert "guess" in found[0].message


def test_the_older_wording_is_not_a_marker_for_the_standard() -> None:
    source = "// Coney's own helper.\nint peek() " + BODY
    found = code({"src/ai/brain.cpp": source})["src/ai/brain.cpp"]
    assert rules(found) == ["tag"] and "older" in found[0].message


def test_a_folder_that_is_not_gameplay_needs_no_tag() -> None:
    source = "// Not tagged.\nint guess() " + BODY
    assert code({"src/core/guess.cpp": source}) == {}


def test_counts_group_findings_by_rule() -> None:
    found = [Finding("a.cpp", 1, "comment", "x"), Finding("a.cpp", 5, "tag", "y"), Finding("a.cpp", 9, "comment", "z")]
    assert counts(found) == {"comment": 2, "tag": 1}


# --- tests -----------------------------------------------------------------------------------------------------------


def scan_text(text: str) -> list[Finding]:
    """The test scan of one made-up file."""
    return scan_tests("tests/ai/brain_test.cpp", text, STANDARDS.tests)


def test_a_cited_test_case_passes() -> None:
    text = (
        'TEST_CASE("a") {\n    // Expected: combat/grabs.md#grab, confirmed (code)\n    CHECK(1 == 1);\n}\n'
        'TEST_CASE("b") {\n    // Expected: research/combat/grabs.md#grab, inferred\n    CHECK(2 == 2);\n}\n'
        'TEST_CASE("c") {\n    // Expected: unverified, #812\n    CHECK(3 == 3);\n}\n'
    )
    assert scan_text(text) == []


def test_a_test_case_with_no_citation_is_a_finding_at_its_line() -> None:
    text = (
        'TEST_CASE("cited") {\n    // Expected: combat/grabs.md#grab, inferred\n    CHECK(1 == 1);\n}\n\n'
        'TEST_CASE("bare") {\n    CHECK(2 == 2);\n}\n'
    )
    found = scan_text(text)
    assert [(f.rule, f.line) for f in found] == [("citation", 6)]
    assert "bare" in found[0].message


def test_a_citation_in_the_comment_above_a_case_belongs_to_that_case() -> None:
    text = (
        'TEST_CASE("first") {\n    CHECK(1 == 1);\n}\n\n'
        "// What the second proves.\n// Expected: combat/grabs.md#grab, inferred\n"
        'TEST_CASE("second") {\n    CHECK(2 == 2);\n}\n'
    )
    assert [(f.rule, f.line) for f in scan_text(text)] == [("citation", 1)]


def test_a_citation_that_names_no_page_section_and_no_issue_does_not_count() -> None:
    text = 'TEST_CASE("a") {\n    // Expected: whatever the game does\n    CHECK(1 == 1);\n}\n'
    assert rules(scan_text(text)) == ["citation"]


def test_the_other_case_macros_count() -> None:
    text = 'TEST_CASE_METHOD(Fixture, "a") {\n}\nTEMPLATE_TEST_CASE("b", "", int) {\n}\nSCENARIO("c") {\n}\n'
    assert [f.line for f in scan_text(text)] == [1, 3, 5]


def test_a_file_with_no_case_has_no_finding() -> None:
    assert scan_text("// helpers only\nint helper() { return 1; }\n") == []


def test_a_file_with_no_case_has_no_case_span() -> None:
    # a helper header under tests/ holds no macro, so it holds no span (it once zipped zero starts with one end)
    assert case_spans("// helpers only\nint helper() { return 1; }\n", STANDARDS.tests) == []
    assert case_spans("", STANDARDS.tests) == []


def test_the_spans_of_a_file_with_cases_run_from_each_comment_block_to_the_next() -> None:
    text = 'TEST_CASE("a") {\n}\n// Note.\nTEST_CASE("b") {\n}\n'
    assert case_spans(text, STANDARDS.tests) == [range(1, 3), range(3, 6)]
