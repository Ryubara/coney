# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the C++ test scope of `task check` (`proof.ctest`, ctest_scope.py): which test files a change reaches
through the `#include`s and the sources that implement the headers, and when it is the whole suite. The trees are
synthetic; one test reads the real tests/CMakeLists.txt for the labels the scope selects by."""

from pathlib import Path

import pytest
from conventions_support import REMOVE, REPO, conventions_with
from git_support import git

from coney_tools import ctest_scope
from coney_tools.config import ConfigError
from coney_tools.conventions import ConventionsError, load_conventions

CONVENTIONS = load_conventions(REPO)

#: A small engine: a leaf (the launcher), a header split over two sources (human.h: human.cpp and human_hiding.cpp,
#: which has no header of its own), a game session that reaches the human through its source only, and the tests.
TREE = {
    "src/core/assert.h": "#pragma once\n",
    "src/launcher/update_plan.h": '#include "core/assert.h"\n',
    "src/launcher/update_plan.cpp": '#include "launcher/update_plan.h"\n',
    "src/human/human.h": '#include "core/assert.h"\n',
    "src/human/human.cpp": '#include "human/human.h"\n',
    "src/human/human_hiding.cpp": '#include "human/human.h"\n#include "world/cover.h"\n',
    "src/world/cover.h": "#pragma once\n",
    "src/world/cover.cpp": '#include "world/cover.h"\n',
    "src/gamemodes/game_session.h": "#pragma once\n",
    "src/gamemodes/game_session.cpp": '#include "gamemodes/game_session.h"\n#include "human/human.h"\n',
    "tests/support/fixtures.h": '#include "core/assert.h"\n',
    "tests/launcher/update_plan_test.cpp": '#include "launcher/update_plan.h"\n',
    "tests/human/human_test.cpp": '#include "human/human.h"\n#include "support/fixtures.h"\n',
    "tests/platform/disc_story_test.cpp": '#include "gamemodes/game_session.h"\n',
    "tests/support/frame_copy_check.cpp": "int main() { return 0; }\n",
    "tests/world/cover_test.cpp": '// #include "human/human.h" is only a comment\n#include "world/cover.h"\n',
}


def scope_of(*paths: str, tree: dict[str, str] | None = None) -> ctest_scope.CtestScope:
    """The scope the real conventions give `paths` over `tree` (default TREE)."""
    return ctest_scope.ctest_scope(CONVENTIONS, paths, TREE if tree is None else tree)


def test_the_rules_are_read_from_the_conventions() -> None:
    rules = CONVENTIONS.proof.ctest
    assert rules.enabled and rules.skip == ("docs_only", "tools_only") and rules.test_suffix == "_test.cpp"
    assert rules.whole_when == ("python/tests/lua4/fixtures/",) and rules.max_files == 100 and rules.tests == "tests/"


@pytest.mark.parametrize(
    ("paths", "files"),
    [
        # a leaf reaches its own test only
        (["src/launcher/update_plan.cpp"], ["tests/launcher/update_plan_test.cpp"]),
        # a changed test file is its own scope
        (["tests/world/cover_test.cpp"], ["tests/world/cover_test.cpp"]),
        # a source with no header of its own implements the header it includes: the session reaches it through
        # game_session.cpp, which includes human.h
        (["src/human/human_hiding.cpp"], ["tests/human/human_test.cpp", "tests/platform/disc_story_test.cpp"]),
        # human_hiding.cpp includes cover.h, so whoever reaches human.h reaches cover too
        (
            ["src/world/cover.cpp"],
            ["tests/human/human_test.cpp", "tests/platform/disc_story_test.cpp", "tests/world/cover_test.cpp"],
        ),
        # a test helper reaches the tests that include it; docs and tools add nothing
        (["tests/support/fixtures.h", "docs/guides/testing.md", "python/src/a.py"], ["tests/human/human_test.cpp"]),
    ],
)
def test_a_change_needs_the_test_files_that_reach_it(paths: list[str], files: list[str]) -> None:
    scope = scope_of(*paths)
    assert not scope.whole and list(scope.files) == files


def test_the_label_selects_the_test_files_by_name() -> None:
    scope = scope_of("src/human/human.h")
    assert scope.label == "^#(disc_story_test|human_test)$"
    assert scope.text == "the tests of 2 test files the change reaches, with the disc"
    assert scope_of("CMakeLists.txt").label is None


@pytest.mark.parametrize(
    ("paths", "why"),
    [
        (["CMakeLists.txt"], "CMakeLists.txt is not a C++ file"),
        (["src/launcher/update_plan.cpp", "cmake/deps.cmake"], "cmake/deps.cmake is not a C++ file"),
        (["tests/CMakeLists.txt"], "tests/CMakeLists.txt is not a C++ file"),
        (["tests/support/scripts/chase.txt"], "chase.txt is not a C++ file"),
        (["assets/sandbox/yard.json"], "yard.json is not a C++ file"),
        (["a-file-nobody-classified.xyz"], "nobody-classified.xyz is not a C++ file"),
        (["python/tests/lua4/fixtures/a.lua"], "a.lua runs everything"),
        (["src/human/human_old.cpp"], "human_old.cpp was deleted or moved"),
        (["src/gamemodes/main.cpp"], "deleted or moved"),
        (["docs/guides/testing.md"], "the change has no C++ file"),
        # the checks outside Catch2 and the helper programs have no file label
        (["tests/support/frame_copy_check.cpp"], "frame_copy_check.cpp is a test program's source with no file label"),
    ],
)
def test_what_cannot_be_mapped_runs_the_whole_suite(paths: list[str], why: str) -> None:
    if why == "test files reach":
        tree = {**TREE, **{f"tests/core/t{i}_test.cpp": '#include "core/assert.h"\n' for i in range(101)}}
        scope = scope_of(*paths, tree=tree)
        assert scope.whole and "105 test files reach the change, more than proof.ctest.max_files 100" in scope.culprit
        return
    scope = scope_of(*paths)
    assert scope.whole and why in scope.culprit and scope.files == ()
    assert scope.text.startswith("the whole suite with the disc (")


def wide(tree: dict[str, str]) -> dict[str, str]:
    """`tree` with 101 more test files that reach the human only through the session, so a change to it is too wide."""
    return {**tree, **{f"tests/misc/t{i}_test.cpp": '#include "gamemodes/game_session.h"\n' for i in range(101)}}


def test_a_change_that_reaches_more_test_files_than_the_cap_runs_the_tests_near_it() -> None:
    # human_test includes human.h, the header human_hiding.cpp is named after; the 101 session tests are left out
    scope = scope_of("src/human/human_hiding.cpp", tree=wide(TREE))
    assert not scope.whole and scope.files == ("tests/human/human_test.cpp",)
    assert scope.narrowed == (
        "103 test files reach it, more than 100; the test files that include it directly or are named after it; "
        "CI and ci disc-suite after the merge run the rest"
    )
    assert scope.text.startswith("the tests of 1 test file, with the disc, narrowed by reach (103 test files")
    assert scope_of("src/human/human_hiding.cpp").narrowed == ""  # under the cap: every test file it reaches


def test_a_test_file_named_after_the_change_is_near_it() -> None:
    tree = {**wide(TREE), "tests/world/cover_edges_test.cpp": "int edges;\n"}
    scope = scope_of("src/world/cover.cpp", tree=tree)  # the 101 session tests reach it through human_hiding.cpp
    assert scope.files == ("tests/world/cover_edges_test.cpp", "tests/world/cover_test.cpp")


def test_with_nothing_near_the_change_its_subsystems_test_files_run() -> None:
    tree = {**TREE, "src/platform/main.cpp": '#include "gamemodes/game_session.h"\n'}
    scope = scope_of("src/platform/main.cpp", tree=tree)  # no test reaches it, none is named after it
    assert scope.files == ("tests/platform/disc_story_test.cpp",)
    assert scope.narrowed.startswith("no test file reaches it; the test files of its subsystem;")


def test_a_change_with_no_test_file_near_it_at_all_runs_the_whole_suite() -> None:
    tree = {**TREE, "src/launch/boot.cpp": "int boot;\n"}
    scope = scope_of("src/launch/boot.cpp", tree=tree)
    assert scope.whole and scope.culprit == "no test file reaches it, and no test file is near src/launch/boot.cpp"


def test_a_test_file_whose_name_a_label_cannot_hold_runs_the_whole_suite() -> None:
    tree = {**TREE, "tests/world/cover-old_test.cpp": '#include "world/cover.h"\n'}
    scope = scope_of("tests/world/cover-old_test.cpp", tree=tree)
    assert scope.whole and "a ctest label cannot hold that name" in scope.culprit


def test_an_include_is_found_beside_the_file_and_relative_paths_are_worked_out() -> None:
    tree = {
        "src/net/wire.h": "#pragma once\n",
        "src/net/snapshot.h": '#include "wire.h"\n',
        "tests/net/snapshot_test.cpp": '#include "../../src/net/snapshot.h"\n',
    }
    assert scope_of("src/net/wire.h", tree=tree).files == ("tests/net/snapshot_test.cpp",)


def test_without_a_ctest_key_every_change_runs_the_whole_suite() -> None:
    conventions = conventions_with(("proof.ctest", REMOVE))
    assert not conventions.proof.ctest.enabled
    scope = ctest_scope.ctest_scope(conventions, ["tests/world/cover_test.cpp"], TREE)
    assert scope.whole and "no proof.ctest" in scope.culprit


@pytest.mark.parametrize(
    ("key", "value", "match"),
    [
        ("skip", ["unknown"], "skip"),
        ("test_suffix", "test", "test_suffix"),
        ("max_files", 0, "max_files"),
        ("invented", 1, "invented"),
    ],
)
def test_the_ctest_rules_are_validated(key: str, value: object, match: str) -> None:
    with pytest.raises(ConventionsError, match=match):
        conventions_with((f"proof.ctest.{key}", value))


def test_a_scope_covers_one_that_needs_its_files_or_fewer() -> None:
    whole, leaf, both = (
        scope_of("CMakeLists.txt"),
        scope_of("tests/world/cover_test.cpp"),
        scope_of("src/world/cover.h"),
    )
    assert ctest_scope.covers(whole, both) and ctest_scope.covers(both, leaf)
    assert not ctest_scope.covers(leaf, both) and not ctest_scope.covers(both, whole)


def test_the_scope_is_recorded_and_read_back_from_its_stamp_form() -> None:
    for scope in (scope_of("CMakeLists.txt"), scope_of("src/world/cover.h")):
        assert ctest_scope.scope_from_record(ctest_scope.scope_record(scope)) == scope
    assert ctest_scope.scope_record(scope_of("tests/world/cover_test.cpp")) == ["tests/world/cover_test.cpp"]
    assert ctest_scope.scope_from_record(None).whole  # a stamp from before the scope was recorded ran everything
    with pytest.raises(ConventionsError, match="ctest_scope"):
        ctest_scope.scope_from_record([])


def test_the_tree_is_read_at_a_commit_not_from_the_worktree(tmp_path: Path) -> None:
    git(tmp_path, "init", "-q", "-b", "main")
    (tmp_path / "src" / "ai").mkdir(parents=True)
    (tmp_path / "src" / "ai" / "brain.h").write_text("#pragma once\n", encoding="utf-8")
    (tmp_path / "src" / "ai" / "notes.txt").write_text("not C++\n", encoding="utf-8")
    (tmp_path / "docs").mkdir()
    (tmp_path / "docs" / "a.h").write_text("outside the folders\n", encoding="utf-8")
    git(tmp_path, "add", "-A")
    git(tmp_path, "commit", "-q", "-m", "one")
    commit = git(tmp_path, "rev-parse", "HEAD").strip()
    (tmp_path / "src" / "ai" / "brain.h").write_text("edited after the commit\n", encoding="utf-8")
    tree = ctest_scope.tree_at(tmp_path, commit, ["src/", "tests/"], [".h", ".cpp"])
    assert tree == {"src/ai/brain.h": "#pragma once\n"}
    with pytest.raises(ConfigError, match="git"):
        ctest_scope.tree_at(tmp_path, "0" * 40, ["src/"], [".h"])


@pytest.mark.reads("engine")
def test_the_test_programs_tag_each_test_with_its_file_so_ctest_has_the_labels() -> None:
    text = (REPO / "tests" / "CMakeLists.txt").read_text(encoding="utf-8")
    discovered = [line for line in text.splitlines() if line.strip().startswith("catch_discover_tests(")]
    assert discovered and all("TEST_SPEC --filenames-as-tags" in line for line in discovered)
    assert all("ADD_TAGS_AS_LABELS" in line for line in discovered)
