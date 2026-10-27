# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools orig check-copy`: decompiler fingerprints in changed C++ lines, on invented code only."""

from pathlib import Path

import pytest
from conventions_support import write_checkout
from git_support import git

from coney_tools import orig_copy
from coney_tools.cli import main

CLEAN = """namespace coney::ai {
/// @orig 0x00100010 Brain_Think (Brain.cpp)
int Brain::think(int hunger) {
    // A shader input such as in_pos0 or a word like undefined in a comment is prose, not code.
    const char* shader = "in vec3 in_pos0; void main() { goto_label(); }";
    return hunger > kHungry ? 1 : 0;
}
} // namespace coney::ai
"""


@pytest.mark.parametrize(
    ("line", "kind"),
    [
        ("int iVar1 = 0;", "ghidra-name"),
        ("float fVar12 = speed;", "ghidra-name"),
        ("auto* puVar3 = table;", "ghidra-name"),
        ("void f(int param_1) {}", "ghidra-name"),
        ("int local_1c = 0;", "ghidra-name"),
        ("int x = DAT_003a1f20;", "ghidra-name"),
        ("FUN_00123456();", "ghidra-name"),
        ("int y = unaff_s0;", "ghidra-name"),
        ("int z = extraout_v0;", "ghidra-name"),
        ("int w = in_v1;", "ghidra-name"),
        ("int s = auStack_20[0];", "ghidra-name"),
        ("undefined4 value = 0;", "undefined-type"),
        ("undefined *p = nullptr;", "undefined-type"),
        ("int hp = *(int *)(fighter + 0x40);", "raw-offset"),
        ("float v = *(float*)((char*)self + 0x1c);", "raw-offset"),
        ("auto h = *reinterpret_cast<int*>(base + 0x10);", "raw-offset"),
        ("goto LAB_done;", "goto"),
        ("    goto done;", "goto"),
    ],
)
def test_each_fingerprint_is_found(line: str, kind: str) -> None:
    found = orig_copy.scan_text("src/ai/x.cpp", line + "\n")
    assert [f.kind for f in found][:1] == [kind]
    assert found[0].line == 1


@pytest.mark.parametrize(
    "line",
    [
        "int variable1 = 0;",
        "int localCount = 0;",
        "int params_1 = 0;",
        "auto inPos = in_pos;",
        "bool undefinedBehaviour = false;",
        "int hp = fighter.health();",
        "auto* p = reinterpret_cast<const Header*>(bytes.data());",
        "int offset = base + 0x40;",
        "int gotoCount = 0;",
    ],
)
def test_own_code_is_not_flagged(line: str) -> None:
    assert orig_copy.scan_text("src/ai/x.cpp", line + "\n") == []


def test_comments_strings_and_raw_strings_are_skipped() -> None:
    text = CLEAN + 'const char* kGlsl = R"glsl(\nin vec2 in_tex0;\nint iVar1;\ngoto x;\n)glsl";\n// FUN_00123456\n'
    text += "/* undefined4 param_1\n   local_10 */\nchar c = '\\'';\n"
    assert orig_copy.scan_text("src/platform/x.cpp", text) == []


def test_only_the_given_lines_are_checked() -> None:
    text = "int iVar1;\nint iVar2;\n"
    assert [f.line for f in orig_copy.scan_text("a.cpp", text, {2})] == [2]


def test_an_allow_marker_with_a_reason_passes_the_line() -> None:
    assert orig_copy.scan_text("a.cpp", "goto retry; // copy-check: allow the original's retry loop\n") == []
    # A marker without a reason does not count.
    assert orig_copy.scan_text("a.cpp", "goto retry; // copy-check: allow\n") != []


def test_added_lines_counts_only_new_and_changed_lines(tmp_path: Path) -> None:
    write_checkout(tmp_path)
    source = tmp_path / "src/ai/brain.cpp"
    source.parent.mkdir(parents=True)
    source.write_text("int iVar1;\nint a;\n", encoding="utf-8")
    git(tmp_path, "init", "-q", "-b", "main")
    git(tmp_path, "add", "-A")
    git(tmp_path, "commit", "-q", "-m", "base")
    # The old fingerprint stays (the backlog); a new line with one is refused, as is a new untracked file.
    source.write_text("int iVar1;\nint a;\nint uVar2;\n", encoding="utf-8")
    (tmp_path / "src/ai/new.h").write_text("int param_1;\n", encoding="utf-8")
    (tmp_path / "python").mkdir()
    (tmp_path / "python/x.cpp").write_text("int iVar9;\n", encoding="utf-8")  # outside src/
    found = orig_copy.check(tmp_path, "HEAD")
    assert [(f.path, f.line, f.text) for f in found] == [
        ("src/ai/brain.cpp", 3, "uVar2"),
        ("src/ai/new.h", 1, "param_1"),
    ]
    every = orig_copy.check(tmp_path, "HEAD", every_line=True)
    assert len(every) == 3


def test_the_command_exits_1_on_a_finding(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    write_checkout(tmp_path)
    (tmp_path / "src/ai").mkdir(parents=True)
    (tmp_path / "src/ai/brain.cpp").write_text(CLEAN, encoding="utf-8")
    git(tmp_path, "init", "-q", "-b", "main")
    git(tmp_path, "add", "-A")
    git(tmp_path, "commit", "-q", "-m", "base")
    monkeypatch.chdir(tmp_path)
    assert main(["orig", "check-copy", "--base", "HEAD"]) == 0
    (tmp_path / "src/ai/brain.cpp").write_text(CLEAN.replace("hunger >", "iVar1 >"), encoding="utf-8")
    assert main(["orig", "check-copy", "--base", "HEAD"]) == 1
