# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools orig check-tags`: new gameplay functions must carry `@orig` or `@coney-only`, on a made-up git
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
    assert main(["orig", "check-tags", "--base", "HEAD"]) == 0
    add_to_brain(repo, "void freshWork() {}")
    assert main(["orig", "check-tags", "--base", "HEAD"]) == 1


def test_the_local_gate_runs_it_when_src_changed(repo: Path) -> None:
    from coney_tools import check_cli

    assert "orig" in check_cli.STEPS
    assert check_cli._step_orig(repo, "HEAD", []).skipped
    add_to_brain(repo, "void freshWork() {}")
    result = check_cli._step_orig(repo, "HEAD", ["src/ai/brain.cpp"])
    assert not result.ok and result.output[0].startswith("src/ai/brain.cpp:8: freshWork")


def commit(repo: Path, files: dict[str, str]) -> None:
    """Write files and commit them, so they are part of the base."""
    for name, text in files.items():
        (repo / name).parent.mkdir(parents=True, exist_ok=True)
        (repo / name).write_text(text, encoding="utf-8")
    git(repo, "add", "-A")
    git(repo, "commit", "-q", "-m", "more base")


@pytest.mark.parametrize("target", ["src/ai/brain_impl.cpp", "src/human/brain.cpp"])
def test_moving_a_file_with_old_untagged_functions_is_not_a_new_finding(repo: Path, target: str) -> None:
    (repo / target).parent.mkdir(parents=True, exist_ok=True)
    git(repo, "mv", "src/ai/brain.cpp", target)
    assert orig_check.check(repo, "HEAD") == []


def test_the_older_wording_does_not_mark_a_new_function(repo: Path) -> None:
    add_to_brain(
        repo, "// Like the original's targeting, but with Coney's own tie-break.\nvoid fresh() {\n    go();\n}"
    )
    assert [p.split(": ")[1].split()[0] for p in orig_check.check(repo, "HEAD")] == ["fresh"]


def test_the_older_wording_still_marks_a_function_written_before_the_check(repo: Path) -> None:
    commit(repo, {"src/ai/old.cpp": "// Coney's own tally.\nvoid tally() {\n    go();\n}\n"})
    (repo / "src/ai/old.cpp").write_text("// Coney's own tally.\nvoid tally() {\n    go();\n    go();\n}\n")
    assert orig_check.check(repo, "HEAD") == []


def test_a_new_untagged_overload_of_an_old_function_is_refused(repo: Path) -> None:
    add_to_brain(repo, "void Brain::legacy(int times) {\n    think();\n}")
    problems = orig_check.check(repo, "HEAD")
    assert len(problems) == 1 and problems[0].startswith("src/ai/brain.cpp:8: Brain::legacy")


def test_a_new_helper_is_not_excused_by_an_old_one_of_the_same_name_in_another_file(repo: Path) -> None:
    commit(repo, {"src/ai/other.cpp": "namespace {\nint numberAt() {\n    return 1;\n}\n}\n"})
    (repo / "src/ai/third.cpp").write_text("namespace {\nint numberAt() {\n    return 2;\n}\n}\n")
    git(repo, "add", "-A")
    problems = orig_check.check(repo, "HEAD")
    assert [p.split(":")[0] for p in problems] == ["src/ai/third.cpp"]


def test_an_exempt_accessor_that_grows_into_logic_is_refused(repo: Path) -> None:
    header = "struct Small {\n    int get() const { return m_v; }\n    int m_v = 0;\n};\n"
    commit(repo, {"src/ai/small.h": header})
    grown = header.replace(
        "{ return m_v; }", "{\n        if (m_v > 3) {\n            return 3;\n        }\n        return m_v;\n    }"
    )
    (repo / "src/ai/small.h").write_text(grown, encoding="utf-8")
    assert [p.split(":")[0] for p in orig_check.check(repo, "HEAD")] == ["src/ai/small.h"]


@pytest.mark.parametrize(
    "member",
    [
        "void run() { each([](int v) { use(v); use(v); }); }",
        "void set(int v) { if (v > 3) m_a = v; }",
    ],
)
def test_a_one_line_member_with_a_lambda_or_a_condition_is_not_exempt(member: str) -> None:
    from coney_tools.orig_index import is_exempt
    from coney_tools.orig_scan import read_source

    function = read_source("src/ai/s.h", f"struct S {{\n    {member}\n    int m_a = 0;\n}};\n").functions[0]
    assert not is_exempt(function)


#: A class of a Coney-only folder whose untagged members needed no marker there (src/platform/ is Coney-only).
MODE_H = """#pragma once
namespace coney {
class PlayLevelMode {
  public:
    void stepHats(int count);
};
} // namespace coney
"""

#: Its member's definition, in a file of its own that a move takes to a gameplay folder.
MODE_HATS_CPP = """#include "platform/play_level_mode.h"
namespace coney {
// Each hat.
void PlayLevelMode::stepHats(int count) {
    for (int i = 0; i < count; ++i) {
        PlayLevelMode::kick(i);
    }
}
} // namespace coney
"""


def moved_base(repo: Path, marker: str = "") -> None:
    """Commit the platform class and its member (with `marker` above the definition) as part of the base."""
    commit(
        repo,
        {
            "src/platform/play_level_mode.h": MODE_H,
            "src/platform/play_level_hats.cpp": MODE_HATS_CPP.replace("// Each hat.\n", f"// Each hat.{marker}\n"),
        },
    )


def move_to_gamemodes(repo: Path, body: str = "", marker: str = "") -> None:
    """Move the member into a renamed class of src/gamemodes/ with `git mv`, its body as it was (or with `body` added),
    and the declaration into the new class's header (with `marker` on the definition's comment)."""
    (repo / "src/gamemodes").mkdir(exist_ok=True)
    git(repo, "mv", "src/platform/play_level_hats.cpp", "src/gamemodes/play_level_hats.cpp")
    moved = MODE_HATS_CPP.replace("PlayLevelMode", "PlayLevelSimulation").replace(
        "platform/play_level_mode.h", "gamemodes/play_level_simulation.h"
    )
    moved = moved.replace("// Each hat.\n", f"// Each hat.{marker}\n")
    if body:
        moved = moved.replace("    }\n}", f"    }}\n    {body}\n}}")
    (repo / "src/gamemodes/play_level_hats.cpp").write_text(moved, encoding="utf-8")
    (repo / "src/gamemodes/play_level_simulation.h").write_text(
        MODE_H.replace("PlayLevelMode", "PlayLevelSimulation"), encoding="utf-8"
    )
    (repo / "src/platform/play_level_mode.h").write_text(
        MODE_H.replace("    void stepHats(int count);\n", ""), encoding="utf-8"
    )
    git(repo, "add", "-A")


def test_a_function_moved_unchanged_into_a_gameplay_folder_and_a_renamed_class_keeps_its_standing(repo: Path) -> None:
    moved_base(repo)
    move_to_gamemodes(repo)
    assert orig_check.check(repo, "HEAD") == []


def test_a_moved_function_whose_body_changed_is_new_work(repo: Path) -> None:
    moved_base(repo)
    move_to_gamemodes(repo, body="kick(count);")
    problems = orig_check.check(repo, "HEAD")
    assert [p.split(": ")[1].split()[0] for p in problems] == ["PlayLevelSimulation::stepHats"]


def test_a_moved_function_that_lost_its_marker_is_refused(repo: Path) -> None:
    moved_base(repo, marker=" @coney-only the level's own loop")
    move_to_gamemodes(repo)
    assert [p.split(": ")[1].split()[0] for p in orig_check.check(repo, "HEAD")] == ["PlayLevelSimulation::stepHats"]


def test_a_moved_function_that_was_also_renamed_is_new_work(repo: Path) -> None:
    moved_base(repo)
    move_to_gamemodes(repo)
    for name in ("src/gamemodes/play_level_hats.cpp", "src/gamemodes/play_level_simulation.h"):
        path = repo / name
        path.write_text(path.read_text(encoding="utf-8").replace("stepHats", "stepCaps"), encoding="utf-8")
    assert [p.split(": ")[1].split()[0] for p in orig_check.check(repo, "HEAD")] == ["PlayLevelSimulation::stepCaps"]


def test_a_copy_into_a_gameplay_folder_is_new_work(repo: Path) -> None:
    moved_base(repo)
    (repo / "src/gamemodes").mkdir(exist_ok=True)
    (repo / "src/gamemodes/play_level_hats.cpp").write_text(
        MODE_HATS_CPP.replace("PlayLevelMode", "PlayLevelSimulation"), encoding="utf-8"
    )
    (repo / "src/gamemodes/play_level_simulation.h").write_text(
        MODE_H.replace("PlayLevelMode", "PlayLevelSimulation"), encoding="utf-8"
    )
    git(repo, "add", "-A")
    assert [p.split(": ")[1].split()[0] for p in orig_check.check(repo, "HEAD")] == ["PlayLevelSimulation::stepHats"]


def test_a_moved_function_that_lost_its_orig_tag_is_refused(repo: Path) -> None:
    moved_base(repo, marker="\n// @orig 0x00100030 Mode_StepHats (Mode.cpp)")
    move_to_gamemodes(repo)
    assert [p.split(": ")[1].split()[0] for p in orig_check.check(repo, "HEAD")] == ["PlayLevelSimulation::stepHats"]


def test_a_moved_function_whose_parameters_changed_with_its_class_is_new_work(repo: Path) -> None:
    moved_base(repo)
    move_to_gamemodes(repo)
    for name in ("src/gamemodes/play_level_hats.cpp", "src/gamemodes/play_level_simulation.h"):
        path = repo / name
        path.write_text(path.read_text(encoding="utf-8").replace("int count", "long count"), encoding="utf-8")
    assert [p.split(": ")[1].split()[0] for p in orig_check.check(repo, "HEAD")] == ["PlayLevelSimulation::stepHats"]
