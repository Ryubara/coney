# SPDX-License-Identifier: GPL-3.0-or-later
"""The legacy marks and the touch-it rule (standards_check, standards_file) on a made-up checkout under git."""

from __future__ import annotations

from pathlib import Path

import pytest
from conventions_support import changed, real_data, write_checkout
from git_support import git

from coney_tools import standards_check as sc
from coney_tools.config import ConfigError
from coney_tools.conventions import Conventions, parse_conventions
from coney_tools.standards_file import LegacyFiles, format_legacy_files, parse_legacy_files

GOOD_SOURCE = (
    "// Adds one.\n// @orig 0x00123456 Brain_Think (Brain.cpp)\n"
    "int think() {\n    int a = 1;\n    a += 1;\n    return a;\n}\n"
)
BAD_SOURCE = GOOD_SOURCE + "\nint guess() {\n    int a = 1;\n    a += 1;\n    return a;\n}\n"
#: `BAD_SOURCE` with a line of its second function, `guess` (no comment, no tag), changed: the touch-it rule holds it.
GUESS_CHANGED = "a += 3;".join(BAD_SOURCE.rsplit("a += 1;", 1))
GOOD_TEST = 'TEST_CASE("a") {\n    // Expected: unverified, #12\n    CHECK(1 == 1);\n}\n'
BAD_TEST = 'TEST_CASE("a") {\n    CHECK(1 == 1);\n}\n'
#: `BAD_TEST` with a line of its test case changed.
CASE_CHANGED = BAD_TEST.replace("CHECK(1 == 1);", "CHECK(2 == 2);")

ENTRY = """[[function]]
address = 0x00123456
name = "Brain_Think"
page = "ai/brain.md#think"
role = "thinks"
level = "inferred"
coney = "src/ai/brain.cpp:think"
"""


def conventions(**changes: object) -> Conventions:
    """The real conventions with changes applied to the made-up checkout's (`standards.enforce` and the like)."""
    return parse_conventions(changed(*changes.items()))


def build(root: Path, files: dict[str, str], status: str = 'status = "legacy"\n') -> Path:
    """A repository on `main` holding the files given, the function database with one ported function and the real
    conventions."""
    write_checkout(root)
    git(root, "init", "-q", "-b", "main")
    (root / "research" / "functions").mkdir(parents=True)
    (root / "research" / "functions" / "ai.toml").write_text(f"# ai\n\n{ENTRY}{status}", encoding="utf-8")
    for name, text in files.items():
        (root / name).parent.mkdir(parents=True, exist_ok=True)
        (root / name).write_text(text, encoding="utf-8")
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "base")
    return root


def branch(root: Path, files: dict[str, str]) -> None:
    """Commit `files` on a branch that left main."""
    git(root, "checkout", "-q", "-b", "work")
    for name, text in files.items():
        (root / name).parent.mkdir(parents=True, exist_ok=True)
        (root / name).write_text(text, encoding="utf-8")
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "work")


def scan(root: Path, conv: Conventions | None = None) -> tuple[sc.Scan, Conventions]:
    """The scan of the checkout with the conventions it holds."""
    conv = conv or conventions()
    return sc.scan_tree(root, conv), conv


# --- the scan --------------------------------------------------------------------------------------------------------


def test_the_scan_counts_each_file_by_rule(tmp_path: Path) -> None:
    root = build(
        tmp_path, {"src/ai/brain.cpp": BAD_SOURCE, "src/ai/ok.cpp": GOOD_SOURCE, "tests/ai/a_test.cpp": BAD_TEST}
    )
    result, _ = scan(root)
    assert result.counts() == {
        "src/ai/brain.cpp": {"comment": 1, "tag": 1},
        "tests/ai/a_test.cpp": {"citation": 1},
    }
    assert (result.code_files, result.test_files) == (2, 1)


def test_the_functions_are_counted_by_status(tmp_path: Path) -> None:
    from coney_tools.research_functions import load_all

    root = build(tmp_path, {}, status="")
    counted = sc.function_counts(load_all(root, Path("research/functions")))
    assert (counted.legacy, counted.compared, counted.ported) == (0, 0, 1)
    assert counted.unmarked == ("0x00123456 Brain_Think",)


def test_mark_unmarked_gives_ported_functions_the_status_legacy(tmp_path: Path) -> None:
    root = build(tmp_path, {}, status="")
    assert sc.mark_unmarked(root, conventions()) == ["research/functions/ai.toml"]
    text = (root / "research/functions/ai.toml").read_text(encoding="utf-8")
    assert 'status = "legacy"' in text
    assert sc.mark_unmarked(root, conventions()) == []


def test_mark_unmarked_leaves_a_compared_function_alone(tmp_path: Path) -> None:
    root = build(tmp_path, {}, status='status = "compared"\n')
    assert sc.mark_unmarked(root, conventions()) == []
    assert 'status = "compared"' in (root / "research/functions/ai.toml").read_text(encoding="utf-8")


# --- the legacy file -------------------------------------------------------------------------------------------------


def test_the_legacy_file_round_trips_and_keeps_the_order_of_the_rules() -> None:
    conv = conventions()
    legacy = LegacyFiles({"tests/a_test.cpp": {"citation": 2}, "src/ai/b.cpp": {"comment": 1, "tag": 4}})
    text = format_legacy_files(legacy)
    assert text.index("src/ai/b.cpp") < text.index("tests/a_test.cpp")
    assert parse_legacy_files(text, conv.standards, "x").files == legacy.files


@pytest.mark.parametrize(
    ("text", "message"),
    [
        ('["docs/guide.md"]\ncomment = 1\n', "neither a source file nor a test file"),
        ('["src/a.cpp"]\ncitation = 1\n', "not a rule of this file"),
        ('["tests/a_test.cpp"]\ntag = 1\n', "not a rule of this file"),
        ('["src/a.cpp"]\ncomment = 0\n', "a whole number of 1 or more"),
        ('["src/a.cpp"]\n', "lists no rule"),
        ("not toml [", "not valid TOML"),
    ],
)
def test_a_wrong_legacy_file_is_refused(text: str, message: str) -> None:
    with pytest.raises(ConfigError, match=message):
        parse_legacy_files(text, conventions().standards, "research/legacy-files.toml")


def test_init_writes_what_the_scan_finds_and_refuses_to_run_twice(tmp_path: Path) -> None:
    root = build(tmp_path, {"src/ai/brain.cpp": BAD_SOURCE})
    result, conv = scan(root)
    text = sc.initial_text(root, conv, result)
    assert '["src/ai/brain.cpp"]\ncomment = 1\ntag = 1' in text
    (root / conv.standards.legacy_file).write_text(text, encoding="utf-8")
    with pytest.raises(ConfigError, match="only shrinks"):
        sc.initial_text(root, conv, result)


def test_prune_takes_out_what_passes_lowers_counts_and_adds_nothing(tmp_path: Path) -> None:
    root = build(tmp_path, {"src/ai/brain.cpp": BAD_SOURCE, "src/ai/ok.cpp": GOOD_SOURCE, "tests/a_test.cpp": BAD_TEST})
    legacy = LegacyFiles(
        {"src/ai/brain.cpp": {"comment": 5, "tag": 1}, "src/ai/ok.cpp": {"tag": 2}, "src/ai/gone.cpp": {"tag": 1}}
    )
    (root / "research").mkdir(exist_ok=True)
    (root / "research/legacy-files.toml").write_text(format_legacy_files(legacy), encoding="utf-8")
    result, conv = scan(root)
    left = sc.pruned(root, conv, result)
    # ok.cpp passes and gone.cpp is gone; brain.cpp's count is lowered; tests/a_test.cpp is not added
    assert left.files == {"src/ai/brain.cpp": {"comment": 1, "tag": 1}}


# --- problems --------------------------------------------------------------------------------------------------------


def write_legacy(root: Path, legacy: dict[str, dict[str, int]]) -> None:
    """Write the legacy file of the made-up checkout."""
    (root / "research").mkdir(exist_ok=True)
    (root / "research/legacy-files.toml").write_text(format_legacy_files(LegacyFiles(legacy)), encoding="utf-8")


def test_a_clean_checkout_has_no_problem(tmp_path: Path) -> None:
    root = build(tmp_path, {"src/ai/brain.cpp": BAD_SOURCE})
    write_legacy(root, {"src/ai/brain.cpp": {"comment": 1, "tag": 1}})
    result, conv = scan(root)
    assert sc.problems(root, conv, result) == []


def test_a_listed_file_that_now_passes_or_is_gone_is_a_problem(tmp_path: Path) -> None:
    root = build(tmp_path, {"src/ai/ok.cpp": GOOD_SOURCE})
    write_legacy(root, {"src/ai/ok.cpp": {"tag": 1}, "src/ai/gone.cpp": {"tag": 1}})
    result, conv = scan(root)
    messages = [p.message for p in sc.problems(root, conv, result)]
    assert any("src/ai/ok.cpp now passes 'tag'" in m for m in messages)
    assert any("src/ai/gone.cpp is gone" in m for m in messages)


def test_a_ported_function_with_no_status_is_a_problem(tmp_path: Path) -> None:
    root = build(tmp_path, {}, status="")
    result, conv = scan(root)
    found = sc.problems(root, conv, result)
    assert [(p.rule, p.path) for p in found] == [("status", "research/functions")]
    assert "0x00123456 Brain_Think" in found[0].message


def test_the_marks_are_only_warnings_until_the_check_is_enforced(tmp_path: Path) -> None:
    # A listed file that now passes, a listed file that is gone and a ported function with no status.
    root = build(tmp_path, {"src/ai/ok.cpp": GOOD_SOURCE}, status="")
    write_legacy(root, {"src/ai/ok.cpp": {"tag": 1}, "src/ai/gone.cpp": {"tag": 1}})
    conv = conventions(**{"standards.enforce": False})
    found = sc.problems(root, conv, sc.scan_tree(root, conv))
    assert {p.rule for p in found} == {"legacy", "status"} and len(found) == 3
    assert not any(p.error for p in found)
    assert sum("standards prune" in p.message for p in found) == 2
    assert any('status = "compared"' in p.message and "standards mark" not in p.message for p in found)


def test_the_marks_are_errors_once_the_check_is_enforced(tmp_path: Path) -> None:
    root = build(tmp_path, {"src/ai/ok.cpp": GOOD_SOURCE}, status="")
    write_legacy(root, {"src/ai/ok.cpp": {"tag": 1}, "src/ai/gone.cpp": {"tag": 1}})
    conv = conventions(**{"standards.enforce": True})
    found = sc.problems(root, conv, sc.scan_tree(root, conv))
    assert len(found) == 3 and all(p.error for p in found)


def test_nothing_fails_the_check_while_it_is_not_enforced(tmp_path: Path) -> None:
    # What the branch adds to the legacy file, a function marked legacy after the cutover and every touched file are
    # warnings too.
    files = {"src/ai/brain.cpp": BAD_SOURCE, "src/ai/other.cpp": BAD_SOURCE}
    root = build(tmp_path, files, status='status = "compared"\n')
    write_legacy(root, {"src/ai/brain.cpp": {"comment": 1}})
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "list")
    branch(
        root,
        {
            "research/legacy-files.toml": format_legacy_files(
                LegacyFiles({"src/ai/brain.cpp": {"comment": 1}, "src/ai/other.cpp": {"comment": 1}})
            ),
            "research/functions/ai.toml": f'# ai\n\n{ENTRY}status = "legacy"\n',
            "src/ai/brain.cpp": GUESS_CHANGED,
        },
    )
    conv = conventions(**{"standards.cutover": "2026-11-01", "standards.enforce": False})
    reports = sc.check(root, conv, "main")
    assert not any(r.error for r in reports)
    assert {"legacy", "status", "comment", "tag"} <= {r.rule for r in reports}
    enforced = conventions(**{"standards.cutover": "2026-11-01", "standards.enforce": True})
    assert all(r.error for r in sc.check(root, enforced, "main"))


def test_a_file_added_to_the_legacy_file_since_the_base_is_a_problem(tmp_path: Path) -> None:
    root = build(tmp_path, {"src/ai/brain.cpp": BAD_SOURCE, "src/ai/other.cpp": BAD_SOURCE})
    write_legacy(root, {"src/ai/brain.cpp": {"comment": 1}})
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "list")
    branch(
        root,
        {
            "research/legacy-files.toml": format_legacy_files(
                LegacyFiles({"src/ai/brain.cpp": {"comment": 1, "tag": 1}, "src/ai/other.cpp": {"comment": 1}})
            )
        },
    )
    result, conv = scan(root)
    messages = [p.message for p in sc.problems(root, conv, result, "main")]
    assert any("src/ai/other.cpp is added" in m for m in messages)
    assert any("src/ai/brain.cpp is listed for 'tag'" in m for m in messages)


def test_the_first_legacy_file_may_be_added_when_the_base_has_none(tmp_path: Path) -> None:
    root = build(tmp_path, {"src/ai/brain.cpp": BAD_SOURCE})
    branch(
        root,
        {
            "research/legacy-files.toml": format_legacy_files(
                LegacyFiles({"src/ai/brain.cpp": {"comment": 1, "tag": 1}})
            )
        },
    )
    result, conv = scan(root)
    assert sc.problems(root, conv, result, "main") == []


def test_a_function_marked_legacy_since_the_base_is_refused_only_while_a_cutover_is_set(tmp_path: Path) -> None:
    root = build(tmp_path, {}, status='status = "compared"\n')
    branch(root, {"research/functions/ai.toml": f'# ai\n\n{ENTRY}status = "legacy"\n'})
    conv = conventions(**{"standards.cutover": ""})
    assert sc.problems(root, conv, sc.scan_tree(root, conv), "main") == []
    after = conventions(**{"standards.cutover": "2026-11-01"})
    found = sc.problems(root, after, sc.scan_tree(root, after), "main")
    assert [p.rule for p in found] == ["status"] and "marked legacy since the cutover" in found[0].message


# --- touching --------------------------------------------------------------------------------------------------------


def test_a_touched_file_below_the_standard_is_a_warning_while_not_enforced(tmp_path: Path) -> None:
    root = build(tmp_path, {"src/ai/brain.cpp": BAD_SOURCE, "src/ai/other.cpp": BAD_SOURCE})
    branch(root, {"src/ai/brain.cpp": GUESS_CHANGED})
    conv = conventions(**{"standards.enforce": False})
    found = sc.touched(root, conv, sc.scan_tree(root, conv), "main")
    files = {(r.path, r.rule, r.error) for r in found}
    assert ("src/ai/brain.cpp", "comment", False) in files and ("src/ai/brain.cpp", "tag", False) in files
    # the file the change leaves alone is not held to the standard
    assert all(r.path != "src/ai/other.cpp" for r in found)


def test_a_touched_file_below_the_standard_fails_once_the_check_is_enforced(tmp_path: Path) -> None:
    root = build(tmp_path, {"src/ai/brain.cpp": BAD_SOURCE, "tests/ai/a_test.cpp": BAD_TEST})
    branch(root, {"tests/ai/a_test.cpp": CASE_CHANGED, "src/ai/brain.cpp": GUESS_CHANGED})
    conv = conventions(**{"standards.enforce": True})
    found = sc.touched(root, conv, sc.scan_tree(root, conv), "main")
    assert found and all(r.error for r in found)
    assert {r.rule for r in found} >= {"citation", "comment", "tag"}


def test_a_touched_file_that_meets_the_standard_has_no_report(tmp_path: Path) -> None:
    root = build(tmp_path, {"src/ai/brain.cpp": GOOD_SOURCE, "tests/ai/a_test.cpp": GOOD_TEST})
    branch(
        root, {"src/ai/brain.cpp": GOOD_SOURCE.replace("a += 1;", "a += 2;"), "tests/ai/a_test.cpp": GOOD_TEST + "\n"}
    )
    result, conv = scan(root)
    functions = [r for r in sc.touched(root, conv, result, "main") if r.rule != "compared"]
    assert functions == []


def test_a_touched_ported_function_must_be_compared(tmp_path: Path) -> None:
    root = build(tmp_path, {"src/ai/brain.cpp": GOOD_SOURCE})
    branch(root, {"src/ai/brain.cpp": GOOD_SOURCE.replace("a += 1;", "a += 2;")})
    conv = conventions(**{"standards.enforce": False})
    found = sc.touched(root, conv, sc.scan_tree(root, conv), "main")
    assert [(r.path, r.rule, r.error) for r in found] == [("src/ai/brain.cpp", "compared", False)]
    assert "0x00123456 Brain_Think" in found[0].message


def test_a_touched_definition_is_held_to_the_tag_on_its_declaration(tmp_path: Path) -> None:
    # Most ports carry `@orig` on the header's declaration; a change to the body in the source file still owes the
    # comparison.
    header = """#pragma once
/// A brain.
class Brain {
public:
    /// Thinks.
    /// @orig 0x00123456 Brain_Think (Brain.cpp)
    int think();
    /// Waits.
    int wait();
};
"""
    source = """#include "brain.h"
int Brain::think() {
    int a = 1;
    a += 1;
    return a;
}
int Brain::wait() {
    int b = 1;
    b += 1;
    return b;
}
"""
    root = build(tmp_path, {"src/ai/brain.h": header, "src/ai/brain.cpp": source})
    branch(root, {"src/ai/brain.cpp": source.replace("a += 1;", "a += 2;")})
    result, conv = scan(root)
    found = [r for r in sc.touched(root, conv, result, "main") if r.rule == "compared"]
    assert [(r.path, r.line) for r in found] == [("src/ai/brain.cpp", 2)]
    assert "Brain::think ports 0x00123456 Brain_Think" in found[0].message
    # a change to the other function only owes nothing
    git(root, "checkout", "-q", "main")
    git(root, "checkout", "-q", "-b", "other")
    (root / "src/ai/brain.cpp").write_text(source.replace("b += 1;", "b += 2;"), encoding="utf-8")
    git(root, "commit", "-q", "-am", "other")
    assert [r for r in sc.touched(root, conv, sc.scan_tree(root, conv), "main") if r.rule == "compared"] == []


def test_a_touched_function_is_held_to_a_tag_inside_its_body(tmp_path: Path) -> None:
    source = """// Engages the enemy.
int process() {
    // @orig 0x00123456 Brain_Think (Brain.cpp)
    int a = 1;
    a += 1;
    return a;
}
"""
    root = build(tmp_path, {"src/ai/brain.cpp": source})
    branch(root, {"src/ai/brain.cpp": source.replace("a += 1;", "a += 2;")})
    result, conv = scan(root)
    found = [r for r in sc.touched(root, conv, result, "main") if r.rule == "compared"]
    assert [(r.path, r.line) for r in found] == [("src/ai/brain.cpp", 2)]
    assert "0x00123456 Brain_Think" in found[0].message


SECOND_ENTRY = """[[function]]
address = 0x00654321
name = "Pad_PressedMask"
page = "ai/brain.md#think"
role = "reads a mask"
level = "inferred"
coney = "src/ai/pad.h:Pad::pressed"
status = "legacy"
"""


def test_an_overload_is_held_only_to_the_tags_of_its_own_declaration(tmp_path: Path) -> None:
    # Two overloads with different tags: touching `pressed()` owes only its own address.
    header = """#pragma once
/// A pad.
class Pad {
public:
    /// Held now.
    /// @orig 0x00123456 Brain_Think (Brain.cpp)
    bool pressed() const;
    /// Held under a mask.
    /// @orig 0x00654321 Pad_PressedMask (Pad.cpp)
    bool pressed(int mask) const { return (m_bits & mask) != 0; }

private:
    int m_bits = 0;
};
"""
    source = """#include "pad.h"
bool Pad::pressed() const {
    int a = m_bits;
    return a != 0;
}
"""
    status = f'status = "legacy"\n\n{SECOND_ENTRY}'
    root = build(tmp_path, {"src/ai/pad.h": header, "src/ai/pad.cpp": source}, status=status)
    branch(root, {"src/ai/pad.cpp": source.replace("a != 0", "a > 0")})
    result, conv = scan(root)
    found = [r for r in sc.touched(root, conv, result, "main") if r.rule == "compared"]
    assert len(found) == 1 and "0x00123456" in found[0].message
    # Touch the other overload too (pad.cpp's change stays on the branch): each overload owes its own address, and
    # neither owes its sibling's, so there is no third report.
    (root / "src/ai/pad.h").write_text(header.replace("!= 0; }", "> 0; }"), encoding="utf-8")
    git(root, "commit", "-q", "-am", "mask")
    found = [r for r in sc.touched(root, conv, sc.scan_tree(root, conv), "main") if r.rule == "compared"]
    assert sorted(r.message.split()[2] for r in found) == ["0x00123456", "0x00654321"]


def test_an_untagged_overload_owes_nothing_for_its_tagged_sibling(tmp_path: Path) -> None:
    header = """#pragma once
/// A batch of sprites.
class Batch {
public:
    /// Adds a sprite.
    /// @orig 0x00123456 Brain_Think (Brain.cpp)
    void add(int sprite);
    /// Adds a sprite at a depth (Coney's own overload).
    void add(int sprite, float depth);
};
"""
    source = """#include "batch.h"
void Batch::add(int sprite) {
    int a = sprite;
    a += 1;
}
void Batch::add(int sprite, float depth) {
    int b = sprite;
    b += static_cast<int>(depth);
}
"""
    root = build(tmp_path, {"src/ai/batch.h": header, "src/ai/batch.cpp": source})
    branch(root, {"src/ai/batch.cpp": source.replace("b += ", "b -= ")})
    result, conv = scan(root)
    assert [r for r in sc.touched(root, conv, result, "main") if r.rule == "compared"] == []


def test_a_ported_function_the_change_does_not_touch_is_left_alone(tmp_path: Path) -> None:
    other = "\n// Somewhere else.\nint other() {\n    int a = 1;\n    a += 1;\n    return a;\n}\n"
    root = build(tmp_path, {"src/ai/brain.cpp": GOOD_SOURCE + other})
    branch(root, {"src/ai/brain.cpp": GOOD_SOURCE + other.replace("a += 1;", "a += 3;")})
    result, conv = scan(root)
    assert [r for r in sc.touched(root, conv, result, "main") if r.rule == "compared"] == []


def test_a_touched_function_that_is_compared_owes_nothing(tmp_path: Path) -> None:
    root = build(tmp_path, {"src/ai/brain.cpp": GOOD_SOURCE}, status='status = "compared"\n')
    branch(root, {"src/ai/brain.cpp": GOOD_SOURCE.replace("a += 1;", "a += 2;")})
    result, conv = scan(root)
    assert [r for r in sc.touched(root, conv, result, "main") if r.rule == "compared"] == []


def test_a_new_file_holds_all_its_ported_functions(tmp_path: Path) -> None:
    root = build(tmp_path, {})
    branch(root, {"src/ai/brain.cpp": GOOD_SOURCE})
    result, conv = scan(root)
    assert [r.rule for r in sc.touched(root, conv, result, "main")] == ["compared"]


# --- the touch-it rule's reach: the functions, types and test cases a change touched --------------------------------

#: Two functions below the standard (no comment, no tag) in one gameplay file.
TWO_BAD = (
    "int guess() {\n    int a = 1;\n    a += 1;\n    return a;\n}\n\n"
    "int other() {\n    int b = 1;\n    b += 1;\n    return b;\n}\n"
)


def held_and_listed(root: Path) -> tuple[set[tuple[str, int]], set[tuple[str, int]]]:
    """The (rule, line) the touch-it rule holds the branch to, and those it only lists (`info`)."""
    conv = conventions(**{"standards.enforce": True})
    found = sc.touched(root, conv, sc.scan_tree(root, conv), "main")
    held = {(r.rule, r.line) for r in found if not r.info}
    assert all(r.error for r in found if not r.info) and not any(r.error for r in found if r.info)
    return held, {(r.rule, r.line) for r in found if r.info}


def test_a_changed_function_of_an_old_file_is_held_and_the_one_beside_it_is_only_listed(tmp_path: Path) -> None:
    root = build(tmp_path, {"src/ai/brain.cpp": TWO_BAD})
    branch(root, {"src/ai/brain.cpp": TWO_BAD.replace("a += 1;", "a += 2;")})
    held, listed = held_and_listed(root)
    assert held == {("comment", 1), ("tag", 1)}
    assert listed == {("comment", 7), ("tag", 7)}


def test_a_change_that_brings_its_function_up_passes_whatever_the_rest_of_the_file_holds(tmp_path: Path) -> None:
    root = build(tmp_path, {"src/ai/brain.cpp": TWO_BAD})
    fixed = "// Guesses.\n// @coney-only a test helper\n" + TWO_BAD.replace("a += 1;", "a += 2;")
    branch(root, {"src/ai/brain.cpp": fixed})
    held, listed = held_and_listed(root)
    assert held == set() and listed == {("comment", 9), ("tag", 9)}


def test_a_new_file_is_held_whole(tmp_path: Path) -> None:
    root = build(tmp_path, {})
    branch(root, {"src/ai/brain.cpp": TWO_BAD})
    held, listed = held_and_listed(root)
    assert held == {("comment", 1), ("tag", 1)} and listed == set()
    report = next(r for r in sc.touched(root, conventions(), scan(root)[0], "main") if r.rule == "comment")
    assert "2 findings in a new file" in report.message


def test_a_type_is_held_when_the_change_touches_its_body(tmp_path: Path) -> None:
    header = "#pragma once\n\nstruct Plain {\n    int a = 0;\n};\n\nstruct Other {\n    int b = 0;\n};\n"
    root = build(tmp_path, {"src/ai/brain.h": header})
    branch(root, {"src/ai/brain.h": header.replace("int b = 0;", "int b = 1;")})
    held, listed = held_and_listed(root)
    assert held == {("type-doc", 7)} and listed == {("type-doc", 3)}


def test_a_changed_test_case_is_held_and_the_one_beside_it_is_only_listed(tmp_path: Path) -> None:
    two = BAD_TEST + "\n" + BAD_TEST.replace('"a"', '"b"')
    root = build(tmp_path, {"tests/ai/a_test.cpp": two})
    branch(root, {"tests/ai/a_test.cpp": two.replace("CHECK(1 == 1);\n}\n\nTEST", "CHECK(2 == 2);\n}\n\nTEST")})
    held, listed = held_and_listed(root)
    assert held == {("citation", 1)} and listed == {("citation", 5)}


def test_a_test_case_with_a_comment_above_its_macro_is_held_when_its_body_changes(tmp_path: Path) -> None:
    # the case's span starts at the comment block, the finding is at the macro line below it
    case = "// Not a citation.\n" + BAD_TEST
    root = build(tmp_path, {"tests/ai/a_test.cpp": case})
    branch(root, {"tests/ai/a_test.cpp": case.replace("CHECK(1 == 1);", "CHECK(2 == 2);")})
    held, listed = held_and_listed(root)
    assert held == {("citation", 2)} and listed == set()


def test_a_touched_test_helper_with_no_test_case_has_nothing_to_hold(tmp_path: Path) -> None:
    # a helper header under tests/ holds no test case; touching it once crashed the touch-it check
    helper = "#pragma once\n\ninline int helper() { return 1; }\n"
    root = build(tmp_path, {"tests/support/helper.h": helper})
    branch(root, {"tests/support/helper.h": helper.replace("return 1", "return 2")})
    held, listed = held_and_listed(root)
    assert not [item for item in held | listed if item[0] == "citation"]


def test_the_check_command_lists_what_lies_outside_the_change_in_one_line_or_each_with_cleanup(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    from coney_tools import standards_cli

    root = build(tmp_path, {"src/ai/brain.cpp": TWO_BAD}, status='status = "compared"\n')
    (root / "coney.local.example.toml").write_text("", encoding="utf-8")
    fixed = "// Guesses.\n// @coney-only a test helper\n" + TWO_BAD.replace("a += 1;", "a += 2;")
    branch(root, {"src/ai/brain.cpp": fixed})
    assert standards_cli.run_check(root, conventions(), "main") == 0
    shown = capsys.readouterr().out
    assert "src/ai/brain.cpp:1: info: touch-it: 2 findings outside what the change touched (comment 1, tag 1)" in shown
    assert standards_cli.run_check(root, conventions(), "main", cleanup=True) == 0
    shown = capsys.readouterr().out
    assert "src/ai/brain.cpp:9: info: comment:" in shown and "src/ai/brain.cpp:9: info: tag:" in shown


def test_relevant_names_the_changes_that_can_move_the_marks() -> None:
    conv = conventions()
    assert sc.relevant(["src/ai/brain.cpp"], conv)
    assert sc.relevant(["tests/ai/a_test.cpp"], conv)
    assert sc.relevant(["research/functions/ai.toml"], conv)
    assert sc.relevant(["research/legacy-files.toml"], conv)
    assert sc.relevant([".github/conventions.yml"], conv)
    assert not sc.relevant(["docs/guides/writing-docs.md", "python/tests/test_x.py", "src/ai/CMakeLists.txt"], conv)


def test_the_real_data_describes_a_checkout_that_exists() -> None:
    # the standards of the real conventions point at folders and a database the checks read
    conv = parse_conventions(real_data())
    assert conv.standards.legacy_file.endswith(".toml") and conv.research.files.functions == "research/functions"


# --- the cutover: the real conventions enforce ------------------------------------------------------------------------


def test_the_real_conventions_enforce_the_standards_from_the_cutover_day() -> None:
    standards = parse_conventions(real_data()).standards
    assert standards.enforce is True
    assert standards.cutover is not None and standards.cutover.date().isoformat() == "2026-10-25"


def test_on_the_real_rules_a_touched_legacy_file_left_below_the_standard_is_an_error(tmp_path: Path) -> None:
    # Three files below the standard are listed; the change touches two of them and the third is left alone.
    files = {"src/ai/brain.cpp": BAD_SOURCE, "src/ai/other.cpp": BAD_SOURCE, "tests/ai/a_test.cpp": BAD_TEST}
    root = build(tmp_path, files)
    both = {"comment": 1, "tag": 1}
    write_legacy(root, {"src/ai/brain.cpp": both, "src/ai/other.cpp": both, "tests/ai/a_test.cpp": {"citation": 1}})
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "list")
    # a line of the ported function changes, so it is owed its comparison too
    branch(root, {"src/ai/brain.cpp": BAD_SOURCE.replace("a += 1;", "a += 2;"), "tests/ai/a_test.cpp": CASE_CHANGED})
    reports = sc.check(root, conventions(), "main")
    assert reports and all(r.error for r in reports)
    assert {(r.path, r.rule) for r in reports} == {
        ("src/ai/brain.cpp", "comment"),
        ("src/ai/brain.cpp", "tag"),
        ("src/ai/brain.cpp", "compared"),
        ("tests/ai/a_test.cpp", "citation"),
    }


def test_on_the_real_rules_a_touched_legacy_file_brought_up_to_the_standard_passes(tmp_path: Path) -> None:
    root = build(
        tmp_path, {"src/ai/brain.cpp": BAD_SOURCE, "tests/ai/a_test.cpp": BAD_TEST}, status='status = "compared"\n'
    )
    write_legacy(root, {"src/ai/brain.cpp": {"comment": 1, "tag": 1}, "tests/ai/a_test.cpp": {"citation": 1}})
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "list")
    # the change fixes both files and takes them out of the legacy file, as `standards prune` does
    branch(root, {"src/ai/brain.cpp": GOOD_SOURCE, "tests/ai/a_test.cpp": GOOD_TEST})
    write_legacy(root, {})
    assert sc.check(root, conventions(), "main") == []


def test_on_the_real_rules_a_branch_may_not_add_to_the_legacy_file_or_mark_a_function_legacy(tmp_path: Path) -> None:
    root = build(
        tmp_path, {"src/ai/brain.cpp": GOOD_SOURCE, "src/ai/new.cpp": BAD_SOURCE}, status='status = "compared"\n'
    )
    write_legacy(root, {})
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "empty list")
    branch(
        root,
        {
            "research/legacy-files.toml": format_legacy_files(
                LegacyFiles({"src/ai/new.cpp": {"comment": 1, "tag": 1}})
            ),
            "research/functions/ai.toml": f'# ai\n\n{ENTRY}status = "legacy"\n',
        },
    )
    reports = sc.check(root, conventions(), "main")
    assert all(r.error for r in reports)
    messages = [r.message for r in reports]
    assert any("src/ai/new.cpp is added" in m for m in messages)
    assert any("marked legacy since the cutover" in m for m in messages)


# --- a research link that follows a page move ------------------------------------------------------------------------

PAGE = "# The AI\n\n" + "".join(f"Line {n} of the page.\n" for n in range(12)) + "\n## Brain {#brain}\n\nThinks.\n"
OLD_REF = "docs/research/ai.md#brain"
NEW_REF = "docs/research/ai/index.md#brain"
#: A source file with a comment above its first statement that cites the page, and a function below the standard.
LINKED_SOURCE = GOOD_SOURCE.replace("    int a = 1;", "    int a = 1;  // REF") + (
    "\nint guess() {\n    int a = 1;\n    a += 1;\n    return a;\n}\n"
)
LINKED_TEST = BAD_TEST.replace("    CHECK", "    // {ref}\n    CHECK")


def move_page_and_edit(root: Path, files: dict[str, str]) -> None:
    """Commit on a branch that moves the AI page to its folder and writes `files`."""
    git(root, "checkout", "-q", "-b", "work")
    (root / "docs/research/ai").mkdir()
    git(root, "mv", "docs/research/ai.md", "docs/research/ai/index.md")
    for name, text in files.items():
        (root / name).write_text(text, encoding="utf-8")
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "work")


def linked(root: Path) -> list[sc.Report]:
    """What the touch-it rule reports for the branch, on the real conventions."""
    conv = conventions(**{"standards.enforce": True})
    return sc.touched(root, conv, sc.scan_tree(root, conv), "main")


def test_a_file_whose_only_change_is_a_comment_link_following_a_moved_page_is_untouched(tmp_path: Path) -> None:
    root = build(
        tmp_path,
        {
            "docs/research/ai.md": PAGE,
            "src/ai/brain.cpp": LINKED_SOURCE.replace("REF", OLD_REF),
            "tests/ai/a_test.cpp": LINKED_TEST.replace("REF", OLD_REF),
        },
    )
    move_page_and_edit(
        root,
        {
            "src/ai/brain.cpp": LINKED_SOURCE.replace("REF", NEW_REF),
            "tests/ai/a_test.cpp": LINKED_TEST.replace("REF", NEW_REF),
        },
    )
    # neither file owes the standard it is below, and the ported function the comment sits in owes no comparison
    assert linked(root) == []


def test_a_comment_link_move_with_another_edit_in_the_file_touches_it(tmp_path: Path) -> None:
    root = build(tmp_path, {"docs/research/ai.md": PAGE, "src/ai/brain.cpp": LINKED_SOURCE.replace("REF", OLD_REF)})
    edited = LINKED_SOURCE.replace("REF", NEW_REF).replace("a += 1;", "a += 2;", 1)
    move_page_and_edit(root, {"src/ai/brain.cpp": edited})
    found = linked(root)
    assert {r.rule for r in found} == {"comment", "tag", "compared"}
    assert all(r.path == "src/ai/brain.cpp" for r in found)


def test_a_comment_link_move_with_another_edit_on_its_line_touches_the_file(tmp_path: Path) -> None:
    root = build(tmp_path, {"docs/research/ai.md": PAGE, "src/ai/brain.cpp": LINKED_SOURCE.replace("REF", OLD_REF)})
    move_page_and_edit(root, {"src/ai/brain.cpp": LINKED_SOURCE.replace("REF", NEW_REF + ", and more")})
    assert {r.rule for r in linked(root)} == {"comment", "tag", "compared"}


def test_a_comment_link_rewrite_with_no_matching_move_touches_the_file(tmp_path: Path) -> None:
    root = build(
        tmp_path,
        {
            "docs/research/ai.md": PAGE,
            "docs/research/stays.md": "# Stays\n\n## Other {#other}\n\nText.\n",
            "src/ai/brain.cpp": LINKED_SOURCE.replace("REF", "docs/research/stays.md"),
        },
    )
    # the page it cites stayed, though another page moved in the same change
    move_page_and_edit(root, {"src/ai/brain.cpp": LINKED_SOURCE.replace("REF", "docs/research/stays.md#other")})
    assert {r.rule for r in linked(root)} == {"comment", "tag", "compared"}


def test_a_comment_link_rewrite_to_a_page_that_does_not_exist_touches_the_file(tmp_path: Path) -> None:
    root = build(tmp_path, {"docs/research/ai.md": PAGE, "src/ai/brain.cpp": LINKED_SOURCE.replace("REF", OLD_REF)})
    move_page_and_edit(root, {"src/ai/brain.cpp": LINKED_SOURCE.replace("REF", "docs/research/ai/brain.md#brain")})
    assert {r.rule for r in linked(root)} == {"comment", "tag", "compared"}


def test_a_link_move_inside_a_string_that_looks_like_a_comment_touches_the_file(tmp_path: Path) -> None:
    code = GOOD_SOURCE.replace("    int a = 1;", '    auto s = "// REF";')
    root = build(tmp_path, {"docs/research/ai.md": PAGE, "src/ai/brain.cpp": code.replace("REF", OLD_REF)})
    move_page_and_edit(root, {"src/ai/brain.cpp": code.replace("REF", NEW_REF)})
    assert {r.rule for r in linked(root)} == {"compared"}


def test_a_form_feed_in_the_file_does_not_shift_the_lines_of_a_link_move_in_a_string(tmp_path: Path) -> None:
    # git counts newlines only: a line split at the form feed would put the string under the comment line above it
    pad = "    // " + "padding " * 8 + "\n"
    code = GOOD_SOURCE.replace("// Adds one.", "// Adds one.\f// and more.")
    code = code.replace("    int a = 1;", pad + '    auto s = "REF";')
    root = build(tmp_path, {"docs/research/ai.md": PAGE, "src/ai/brain.cpp": code.replace("REF", OLD_REF)})
    move_page_and_edit(root, {"src/ai/brain.cpp": code.replace("REF", NEW_REF)})
    assert {r.rule for r in linked(root)} == {"compared"}


def test_an_xbox_tag_owes_no_ps2_comparison(tmp_path: Path) -> None:
    # The research database holds PS2 addresses; an Xbox address that happens to equal one is not that function.
    xbox = GOOD_SOURCE.replace("@orig 0x00123456", "@orig xbox 0x00123456")
    root = build(tmp_path, {"src/ai/brain.cpp": xbox})
    branch(root, {"src/ai/brain.cpp": xbox.replace("a += 1;", "a += 2;")})
    result, conv = scan(root)
    assert [r for r in sc.touched(root, conv, result, "main") if r.rule == "compared"] == []
