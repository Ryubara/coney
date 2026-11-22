# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the fix profile of `coney-tools task check` (`proof.fix`, check_fix.py): a Check on top of an earlier
passing Check of the same task runs the tests of the files changed since, falls back to the full Check when what changed
is too wide or cannot be traced, and `pr verify` accepts the fix's stamp on top of the earlier one and refuses one that
does not hold. The checks and the build are the fakes of test_task_check; the repository, its origin, the stamp log and
the build records are real."""

import json
import subprocess
from pathlib import Path
from typing import Any

import pytest
from git_support import git
from test_task_check import TASK, TESTS, World, _main_edits, make_world, start_fix

from coney_tools import check_fix, ctest_scope, pr_diff, pr_proof, proof_profiles, stamps, unit
from coney_tools.config import ConfigError
from coney_tools.conventions import load_conventions
from coney_tools.proof_profiles import pytest_scope

BRAIN = '#include "ai/brain.h"\nint think() { return %d; }\n'


@pytest.fixture
def world(tmp_path: Path) -> World:
    """The world of test_task_check with a small engine on `main`, pushed to a bare `origin` as `pr verify` needs."""
    built = make_world(tmp_path)
    git(built.root, "checkout", "-q", "main")
    built.edit("src/ai/brain.h", "#pragma once\nint think();\n", "engine")
    built.edit("src/ai/brain.cpp", BRAIN % 1, "engine")
    built.edit("src/ai/plan.cpp", '#include "ai/plan.h"\nint plan() { return 1; }\n', "engine")
    built.edit("src/ai/plan.h", "#pragma once\nint plan();\n", "engine")
    built.edit("tests/ai/brain_test.cpp", '#include "ai/brain.h"\n', "engine")
    built.edit("tests/ai/plan_test.cpp", '#include "ai/plan.h"\n', "engine")
    built.edit("src/CMakeLists.txt", "add_library(ai)\n", "engine")
    origin = tmp_path / "origin.git"
    subprocess.run(["git", "init", "-q", "--bare", "-b", "main", str(origin)], check=True)
    git(built.root, "remote", "add", "origin", str(origin))
    git(built.root, "push", "-q", "-u", "origin", "main")
    git(built.root, "remote", "set-head", "origin", "main")
    git(built.root, "checkout", "-q", "work")
    git(built.root, "rebase", "-q", "main")
    return built


def check(world: World, **more: Any) -> int:
    """`task check` against `origin/main`, as an agent runs it."""
    return world.check(base="origin/main", **more)


def full_then_edit(world: World) -> None:
    """The pull request: a CMake file (so its Check runs the whole suite) and the brain, checked in full; then a fix
    to the brain."""
    world.edit("src/CMakeLists.txt", "add_library(ai)\nadd_library(more)\n")
    world.edit("src/ai/brain.cpp", BRAIN % 2)
    assert check(world) == 0
    assert world.labels == [None] and "fix" not in world.stamp.detail
    world.edit("src/ai/brain.cpp", BRAIN % 3, "the fix")


def push_main(world: World) -> None:
    """Publish `main` as it is now to `origin` and fetch it, and rebase the branch on it."""
    git(world.root, "push", "-q", "origin", "main")
    git(world.root, "fetch", "-q", "origin")
    git(world.root, "rebase", "-q", "origin/main")


def verify(world: World) -> list[str]:
    """The refusals of `pr verify`'s Check step for the unit."""
    subject = pr_proof.load_subject(
        world.root, unit.Unit.of(world.root, TASK), TASK, "fix", conventions=load_conventions(world.root), scan=False
    )
    return [refusal.text for refusal in pr_proof.check_checked(subject)]


# --- task check picks the fix profile -------------------------------------------------------------------------------


def test_a_fix_on_a_passing_full_check_runs_only_the_tests_of_the_files_changed_since(world: World) -> None:
    full_then_edit(world)
    first = world.stamp
    assert check(world) == 0
    assert world.labels[-1] == "^#(brain_test)$"  # not the whole suite the CMake file of the branch asks for
    stamp = world.stamp
    assert stamp.detail["profile"] == "full" and stamp.detail["checks"][-3:] == [
        "msvc-ci-build",
        "ctest-full-disc",
        "clang-tidy",
    ]
    assert stamp.detail["fix"] == {
        "on": first.seq,
        "on_stamp": first.hash,
        "on_change": first.covers[stamps.CHANGE],
        "files": 1,
        "sha256": check_fix.paths_sha256(["src/ai/brain.cpp"]),
    }
    assert stamp.detail["pytest_scope"] == ["engine"]  # from the files changed since, not the whole branch
    assert any(line.startswith(f"  fix: on Check #{first.seq}") and "src/ai/brain.cpp" in line for line in world.said)
    assert verify(world) == []


def test_a_rebase_onto_a_main_that_moved_counts_only_the_branchs_own_edits(world: World) -> None:
    full_then_edit(world)
    git(world.root, "checkout", "-q", "main")
    world.edit("src/ai/plan.cpp", '#include "ai/plan.h"\nint plan() { return 7; }\n', "main moves")
    world.edit("src/CMakeLists.txt", "cmake_minimum_required(VERSION 3.28)\nadd_library(ai)\n", "main moves")
    git(world.root, "checkout", "-q", "work")
    push_main(world)
    assert check(world) == 0
    assert world.labels[-1] == "^#(brain_test)$" and world.stamp.detail["fix"]["files"] == 1
    assert verify(world) == []


def test_a_fix_after_a_fix_stands_on_the_full_check_with_everything_changed_since(world: World) -> None:
    full_then_edit(world)
    first = world.stamp
    assert check(world) == 0
    world.edit("src/ai/plan.cpp", '#include "ai/plan.h"\nint plan() { return 2; }\n', "another fix")
    assert check(world) == 0
    assert world.stamp.detail["fix"]["on"] == first.seq and world.stamp.detail["fix"]["files"] == 2
    assert world.labels[-1] == "^#(brain_test|plan_test)$"
    assert verify(world) == []


def test_a_fix_that_touches_no_cpp_file_runs_the_branchs_own_test_files(world: World) -> None:
    world.edit("tests/ai/brain_test.cpp", '#include "ai/brain.h"\nint more;\n')
    world.edit("src/CMakeLists.txt", "add_library(ai)\nadd_library(more)\n")
    assert check(world) == 0
    world.edit("docs/guides/widget.md", "# Widget\n\nFixed.\n", "the fix")
    assert check(world) == 0
    assert world.labels[-1] == "^#(brain_test)$"
    # The branch edited C++ test files, so the tests that read them run too although the fix edited only a page: the
    # guard test that scans every C++ test file was skipped this way (task 1588, review round 8).
    assert world.stamp.detail["pytest_scope"] == ["docs_only", "engine"]
    assert verify(world) == []


def test_pr_verify_refuses_a_fix_that_left_out_the_tests_that_read_what_the_branch_touched(
    world: World, monkeypatch: pytest.MonkeyPatch
) -> None:
    world.edit("tests/ai/brain_test.cpp", '#include "ai/brain.h"\nint more;\n')
    world.edit("src/CMakeLists.txt", "add_library(ai)\nadd_library(more)\n")
    assert check(world) == 0
    world.edit("docs/guides/widget.md", "# Widget\n\nFixed.\n", "the fix")
    with monkeypatch.context() as older:  # a Check by a tool that scoped a fix to the files changed since only
        older.setattr(
            proof_profiles, "fix_pytest_scope", lambda conventions, since, branch: pytest_scope(conventions, since)
        )
        assert check(world) == 0
    assert world.stamp.detail["pytest_scope"] == ["docs_only"]
    refusals = verify(world)
    assert len(refusals) == 1 and "this change needs only the tests marked as reading docs_only, engine" in refusals[0]


@pytest.mark.parametrize("name", ["tests/pictures/scenes.toml", "src/console/commands.toml", "assets/ui/font.txt"])
def test_a_fix_to_a_non_cpp_engine_file_runs_the_full_check(world: World, name: str) -> None:
    # Such a file feeds tests the include graph cannot trace: the normal scope runs the whole suite for it, so a fix
    # must not narrow it to the branch's own test files.
    world.edit("tests/ai/brain_test.cpp", '#include "ai/brain.h"\nint more;\n')
    world.edit("src/CMakeLists.txt", "add_library(ai)\nadd_library(more)\n")
    assert check(world) == 0
    world.edit(name, "x = 1\n", "the fix")
    assert check(world) == 0
    assert "fix" not in world.stamp.detail and world.labels[-1] is None
    said = next(line for line in world.said if line.startswith("  fix: "))
    assert said.startswith("  fix: not used: the C++ tests of what changed are the whole suite") and name in said
    assert verify(world) == []


def test_a_fix_task_named_by_its_id_stands_on_its_parents_check(world: World) -> None:
    full_then_edit(world)
    start_fix(world.root, "757-fix", TASK)
    assert check(world, task="757-fix") == 0
    assert "fix" in world.stamp.detail and world.stamp.task == TASK


# --- and falls back to the full check --------------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("name", "text", "why"),
    [
        (
            "src/CMakeLists.txt",
            "add_library(other)\n",
            "src/CMakeLists.txt changed (proof.fix.full_when: */CMakeLists.txt)",
        ),
        ("cmake/deps.cmake", "set(X 1)\n", "cmake/deps.cmake changed (proof.fix.full_when: cmake/*)"),
        ("tests/support/check.txt", "x\n", "tests/support/check.txt changed (proof.fix.full_when: tests/support/*)"),
    ],
)
def test_a_fix_that_touches_the_build_or_a_fixture_runs_the_full_check(
    world: World, name: str, text: str, why: str
) -> None:
    full_then_edit(world)
    world.edit(name, text)
    assert check(world) == 0
    assert "fix" not in world.stamp.detail and world.labels[-1] is None
    said = next(line for line in world.said if line.startswith("  fix: "))
    assert said.startswith("  fix: not used: ") and name in said
    assert why in said
    assert verify(world) == []


def test_a_fix_of_more_files_than_the_limit_runs_the_full_check(world: World) -> None:
    full_then_edit(world)
    for number in range(load_conventions(world.root).proof.fix.max_files):
        world.edit(f"docs/guides/page{number}.md", f"# Page {number}\n")
    assert check(world) == 0
    assert "fix" not in world.stamp.detail
    assert any("files changed since, more than 10" in line for line in world.said)


def test_a_fix_to_a_widely_included_header_runs_the_full_check(world: World) -> None:
    _main_edits(world, lambda data: data["proof"]["fix"].update(max_header_includers=1))
    push_main(world)
    full_then_edit(world)
    world.edit("src/ai/brain.h", "#pragma once\nint think();\nint again();\n")
    assert check(world) == 0
    assert "fix" not in world.stamp.detail
    assert any("src/ai/brain.h is included by 2 C++ files, more than 1" in line for line in world.said)


def test_no_fix_or_a_profile_asked_for_runs_the_full_check(world: World) -> None:
    full_then_edit(world)
    assert check(world, fix=False) == 0
    assert "fix" not in world.stamp.detail and world.labels[-1] is None
    world.edit("src/ai/brain.cpp", BRAIN % 4)
    assert check(world, profile="full") == 0
    assert "fix" not in world.stamp.detail and world.labels[-1] is None


def test_a_failed_or_smaller_earlier_check_is_no_anchor(world: World) -> None:
    world.edit("python/src/coney_tools/widget.py", "x = 1\n")
    assert check(world) == 0 and world.stamp.detail["profile"] == "tooling"
    world.edit("src/ai/brain.cpp", BRAIN % 5)  # now the change needs full: the tooling Check cannot vouch for it
    world.build_tests = dict(TESTS, passed=411, failed=1)
    assert check(world) == 1
    world.edit("src/ai/brain.cpp", BRAIN % 6)
    world.build_tests = dict(TESTS)
    assert check(world) == 0
    assert "fix" not in world.stamp.detail and not any(line.startswith("  fix: ") for line in world.said)


def test_a_base_without_the_fix_rules_has_no_fix_profile(world: World) -> None:
    _main_edits(world, lambda data: data["proof"].pop("fix"))
    push_main(world)
    full_then_edit(world)
    assert check(world) == 0
    assert "fix" not in world.stamp.detail and world.labels[-1] is None


def test_an_anchor_whose_commit_is_gone_runs_the_full_check(world: World, monkeypatch: pytest.MonkeyPatch) -> None:
    full_then_edit(world)
    real = pr_diff.change_of

    def lost(root: Path, target: str, tip: str) -> Any:
        if tip == world.log.stamps[-1].commit:
            raise ConfigError(f"{tip}: not a commit of {root}")
        return real(root, target, tip)

    monkeypatch.setattr(pr_diff, "change_of", lost)
    assert check(world) == 0
    assert "fix" not in world.stamp.detail
    assert any("cannot be read" in line for line in world.said)


# --- pr verify --------------------------------------------------------------------------------------------------------


def _restamp(world: World, edit: Any) -> None:
    """Append a copy of the newest stamp with `edit` applied to its detail: a stamp a tool wrote wrong."""
    newest = world.stamp
    detail = json.loads(json.dumps(newest.detail))
    edit(detail)
    stamps.append(
        unit.Unit.of(world.root, TASK).stamps,
        rules=load_conventions(world.root).stamps,
        task=TASK,
        step=newest.step,
        subject=newest.subject,
        result=newest.result,
        commit=newest.commit,
        covers=dict(newest.covers),
        detail=detail,
        who=dict(newest.who),
        clock=world.clock,
    )


def test_pr_verify_refuses_a_fix_whose_files_are_not_those_changed_since(world: World) -> None:
    full_then_edit(world)
    assert check(world) == 0
    _restamp(world, lambda detail: detail["fix"].update(files=0))
    assert verify(world) == ["checked: the files changed since Check #1 are not those the fix ran for; run task check"]


def test_pr_verify_refuses_a_fix_on_a_stamp_that_is_not_in_the_log_or_was_changed(world: World) -> None:
    full_then_edit(world)
    assert check(world) == 0
    _restamp(world, lambda detail: detail["fix"].update(on_stamp="0" * 64))
    assert verify(world) == ["checked: checked stamp: fix names stamp #1, whose hash differs"]
    _restamp(world, lambda detail: detail["fix"].update(on=99))
    assert verify(world) == ["checked: checked stamp: fix names stamp #99, which is not in the log"]


def test_pr_verify_refuses_a_fix_the_bases_rules_no_longer_allow(world: World) -> None:
    full_then_edit(world)
    assert check(world) == 0
    _main_edits(world, lambda data: data["proof"]["fix"].update(full_when=["src/ai/*"]))
    push_main(world)
    assert verify(world) == [
        "checked: Check ran as a fix on Check #1, but src/ai/brain.cpp changed (proof.fix.full_when: src/ai/*); "
        "run task check --no-fix"
    ]


def test_pr_verify_refuses_a_fix_whose_tests_do_not_hold_those_of_the_files_changed_since(world: World) -> None:
    full_then_edit(world)
    assert check(world) == 0
    plan_only = ["tests/ai/plan_test.cpp"]
    ctest_scope.write_files(
        unit.Unit.of(world.root, TASK).dir / ctest_scope.FOLDER, ctest_scope.CtestScope(False, tuple(plan_only))
    )
    _restamp(
        world,
        lambda detail: detail.update(ctest_scope={"files": 1, "sha256": ctest_scope.files_sha256(plan_only)}),
    )
    [refusal] = verify(world)
    assert "this change needs the tests of 1 test file the change reaches" in refusal


def test_pr_verify_refuses_a_fix_on_a_check_whose_build_record_is_gone(world: World) -> None:
    full_then_edit(world)
    first = world.stamp
    assert check(world) == 0
    record = unit.Unit.of(world.root, TASK).dir / "build-runs" / f"{first.covers['build_record_sha256']}.json"
    record.unlink()
    [refusal] = verify(world)
    assert refusal.startswith("checked: Check ran as a fix on Check #1, whose build record cannot be read")


# --- the rules on their own ------------------------------------------------------------------------------------------


def test_a_headers_includers_count_those_that_reach_it_through_other_headers() -> None:
    tree = {
        "src/core/types.h": "#pragma once\n",
        "src/ai/brain.h": '#include "core/types.h"\n',
        "src/ai/brain.cpp": '#include "ai/brain.h"\n',
        "tests/ai/brain_test.cpp": '#include "ai/brain.h"\n',
        "src/ai/plan.cpp": "int plan;\n",
    }
    counted = check_fix.includers(tree, ("src/", "tests/"), ["src/core/types.h", "src/ai/brain.h"])
    assert counted == {"src/core/types.h": 3, "src/ai/brain.h": 2}


def test_the_files_changed_since_are_checked_against_the_rules_in_order(tmp_path: Path) -> None:
    conventions = load_conventions(make_world(tmp_path).root)
    assert check_fix.why_full(conventions, ["src/ai/brain.cpp"], {}) == ""
    assert check_fix.why_full(conventions, ["CMakePresets.json"], {}) == (
        "CMakePresets.json changed (proof.fix.full_when: CMakePresets.json)"
    )
    many = [f"docs/page{n}.md" for n in range(11)]
    assert check_fix.why_full(conventions, many, {}) == "11 files changed since, more than 10"
