# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools orig check`: new gameplay functions must carry `@orig` or `@coney-only`, on a made-up git
checkout whose base commit already holds some untagged code."""

from pathlib import Path

import pytest
from conventions_support import write_checkout
from git_support import git

from coney_tools import orig_check
from coney_tools.cli import main

BRAIN_H = """#pragma once
namespace coney::ai {
class Brain {
  public:
    /// @orig 0x00100010 Brain_Think (Brain.cpp)
    void think();
    void legacy();
};
} // namespace coney::ai
"""

BRAIN_CPP = """#include "ai/brain.h"
namespace coney::ai {
void Brain::think() {}
// Untagged since before the check existed.
void Brain::legacy() {
    think();
}
} // namespace coney::ai
"""


@pytest.fixture
def repo(tmp_path: Path) -> Path:
    """A git checkout with the real conventions and a base commit holding a tagged and an untagged function."""
    write_checkout(tmp_path)
    for name, text in {"src/ai/brain.h": BRAIN_H, "src/ai/brain.cpp": BRAIN_CPP}.items():
        (tmp_path / name).parent.mkdir(parents=True, exist_ok=True)
        (tmp_path / name).write_text(text, encoding="utf-8")
    git(tmp_path, "init", "-q", "-b", "main")
    git(tmp_path, "add", "-A")
    git(tmp_path, "commit", "-q", "-m", "base")
    return tmp_path


def add_to_brain(repo: Path, code: str) -> None:
    """Append code inside the namespace of brain.cpp, in the working tree."""
    path = repo / "src/ai/brain.cpp"
    text = path.read_text(encoding="utf-8")
    path.write_text(text.replace("} // namespace coney::ai", code + "\n} // namespace coney::ai"), encoding="utf-8")


def test_an_unchanged_checkout_passes(repo: Path) -> None:
    assert orig_check.check(repo, "HEAD") == []


def test_editing_an_old_untagged_function_is_not_a_new_finding(repo: Path) -> None:
    add_to_brain(repo, "")
    (repo / "src/ai/brain.cpp").write_text(BRAIN_CPP.replace("think();", "think();\n    think();"), encoding="utf-8")
    assert orig_check.check(repo, "HEAD") == []


def test_a_new_untagged_function_is_refused_with_what_to_add(repo: Path) -> None:
    add_to_brain(repo, "void freshWork() {\n    Brain b;\n}")
    problems = orig_check.check(repo, "HEAD")
    assert len(problems) == 1
    assert problems[0].startswith("src/ai/brain.cpp:8: freshWork has no @orig and no @coney-only")
    assert "@orig 0x<address> <Name> (<File>.cpp)" in problems[0] and "@coney-only <reason>" in problems[0]


def test_a_new_private_helper_needs_a_tag_or_marker_too(repo: Path) -> None:
    add_to_brain(repo, "namespace {\nint helper() {\n    return 1;\n}\n} // namespace")
    assert [p.split(":")[2].strip().split()[0] for p in orig_check.check(repo, "HEAD")] == ["helper"]


def test_tagged_marked_and_exempt_new_functions_pass(repo: Path) -> None:
    add_to_brain(
        repo,
        "/// @orig 0x00100020 Brain_Rest (unknown)\nvoid rest() {}\n"
        "/// @coney-only a test aid\nvoid testAid() {}\n"
        "struct Small {\n    Small() {}\n    int get() const { return 1; }\n};",
    )
    assert orig_check.check(repo, "HEAD") == []


def test_a_function_that_loses_its_tag_is_refused(repo: Path) -> None:
    (repo / "src/ai/brain.h").write_text(BRAIN_H.replace("    /// @orig 0x00100010 Brain_Think (Brain.cpp)\n", ""))
    problems = orig_check.check(repo, "HEAD")
    assert [p.split(":")[0] for p in problems] == ["src/ai/brain.cpp"]
    assert "Brain::think" in problems[0]


def test_coney_only_folders_need_nothing(repo: Path) -> None:
    (repo / "src/debug").mkdir()
    (repo / "src/debug/menu.cpp").write_text("namespace coney {\nvoid menu() {}\n}\n", encoding="utf-8")
    assert orig_check.check(repo, "HEAD") == []


def test_a_folder_in_neither_list_is_refused(repo: Path) -> None:
    (repo / "src/newthing").mkdir()
    (repo / "src/newthing/a.cpp").write_text("// @coney-only made up\nvoid a() {}\n", encoding="utf-8")
    problems = orig_check.check(repo, "HEAD")
    assert problems == [
        "src/newthing/: in neither paths.gameplay nor paths.coney_only of .github/conventions.yml; add it to one"
    ]


def test_the_command_exits_1_on_a_finding(repo: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.chdir(repo)
    assert main(["orig", "check", "--base", "HEAD"]) == 0
    add_to_brain(repo, "void freshWork() {}")
    assert main(["orig", "check", "--base", "HEAD"]) == 1


def test_the_local_gate_runs_it_when_src_changed(repo: Path) -> None:
    from coney_tools import check_cli

    assert "orig" in check_cli.STEPS
    assert check_cli._step_orig(repo, "HEAD", []).skipped
    add_to_brain(repo, "void freshWork() {}")
    result = check_cli._step_orig(repo, "HEAD", ["src/ai/brain.cpp"])
    assert not result.ok and result.output[0].startswith("src/ai/brain.cpp:8: freshWork")
