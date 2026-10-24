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
GOOD_TEST = 'TEST_CASE("a") {\n    // Expected: unverified, #12\n    CHECK(1 == 1);\n}\n'
BAD_TEST = 'TEST_CASE("a") {\n    CHECK(1 == 1);\n}\n'

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
    result, conv = scan(root)
    found = sc.problems(root, conv, result)
    assert {p.rule for p in found} == {"legacy", "status"} and len(found) == 3
    assert not any(p.error for p in found)
    assert sum("standards prune" in p.message for p in found) == 2
    assert any("standards mark" in p.message for p in found)


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
            "src/ai/brain.cpp": BAD_SOURCE + "// x\n",
        },
    )
    conv = conventions(**{"standards.cutover": "2026-11-01"})
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


def test_a_function_marked_legacy_since_the_base_is_refused_only_after_the_cutover(tmp_path: Path) -> None:
    root = build(tmp_path, {}, status='status = "compared"\n')
    branch(root, {"research/functions/ai.toml": f'# ai\n\n{ENTRY}status = "legacy"\n'})
    before, conv = scan(root)
    assert sc.problems(root, conv, before, "main") == []
    after = conventions(**{"standards.cutover": "2026-11-01"})
    found = sc.problems(root, after, sc.scan_tree(root, after), "main")
    assert [p.rule for p in found] == ["status"] and "marked legacy since the cutover" in found[0].message


# --- touching --------------------------------------------------------------------------------------------------------


def test_a_touched_file_below_the_standard_is_a_warning_before_the_cutover(tmp_path: Path) -> None:
    root = build(tmp_path, {"src/ai/brain.cpp": BAD_SOURCE, "src/ai/other.cpp": BAD_SOURCE})
    branch(root, {"src/ai/brain.cpp": BAD_SOURCE + "\n// More.\n"})
    result, conv = scan(root)
    found = sc.touched(root, conv, result, "main")
    files = {(r.path, r.rule, r.error) for r in found}
    assert ("src/ai/brain.cpp", "comment", False) in files and ("src/ai/brain.cpp", "tag", False) in files
    # the file the change leaves alone is not held to the standard
    assert all(r.path != "src/ai/other.cpp" for r in found)


def test_a_touched_file_below_the_standard_fails_once_the_check_is_enforced(tmp_path: Path) -> None:
    root = build(tmp_path, {"src/ai/brain.cpp": BAD_SOURCE, "tests/ai/a_test.cpp": BAD_TEST})
    branch(root, {"tests/ai/a_test.cpp": BAD_TEST + "// x\n", "src/ai/brain.cpp": BAD_SOURCE + "// y\n"})
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
    result, conv = scan(root)
    found = sc.touched(root, conv, result, "main")
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
