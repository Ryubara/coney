# SPDX-License-Identifier: GPL-3.0-or-later
"""What a `fails-first` stamp binds: the one test's own code and registration, not whole test files (issue 1097)."""

from pathlib import Path

import pytest
from git_support import git

from coney_tools import fails_first, stamps

NAME = "Widget ready frame"
FILE = "tests/ai/widget_test.cpp"
LIST = "tests/CMakeLists.txt"

FIRST = 'TEST_CASE("Widget ready frame") {\n    REQUIRE(ready() == 7);\n}\n'
OTHER = 'TEST_CASE("Widget other") {\n    REQUIRE(other() == 1);\n}\n'
REGISTER = "add_executable(coney_tests\n    ai/widget_test.cpp\n    core/log_test.cpp\n)\n"


class Tree:
    """A repository whose commits edit one test file and the list that registers it."""

    def __init__(self, root: Path) -> None:
        self.root = root
        root.mkdir(parents=True)
        git(root, "init", "-q", "-b", "work")
        self.put(FILE, FIRST)
        self.put(LIST, REGISTER)
        self.first = self.commit("test")

    def put(self, path: str, text: str) -> None:
        """Write a file."""
        target = self.root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8", newline="\n")

    def read(self, path: str) -> str:
        """Read a file."""
        return (self.root / path).read_text(encoding="utf-8")

    def commit(self, message: str) -> str:
        """Commit everything; returns the commit."""
        git(self.root, "add", "-A")
        git(self.root, "commit", "-q", "-m", message)
        return git(self.root, "rev-parse", "HEAD").strip()

    def hash(self, commit: str = "HEAD", name: str = NAME) -> str:
        """The test's hash at `commit`."""
        return fails_first.test_sha256(self.root, commit, name, [FILE, LIST])

    def stamp(self, **covers: str) -> stamps.Stamp:
        """A passing stamp for NAME as `orig fails-first` writes it at the first commit."""
        have = {"fix_commit": self.first, fails_first.TEST_COVER: self.hash(self.first), **covers}
        return stamps.Stamp({"subject": NAME, "result": "pass", "covers": have, "detail": {"test_files": [FILE, LIST]}})

    def legacy(self) -> stamps.Stamp:
        """A passing stamp in the old form, bound to the whole files as they were at the first commit."""
        whole = fails_first.tests_sha256(self.root, self.first, [FILE, LIST])
        return stamps.Stamp(
            {
                "subject": NAME,
                "result": "pass",
                "covers": {"fix_commit": self.first, fails_first.FILES_COVER: whole},
                "detail": {"test_files": [FILE, LIST]},
            }
        )


@pytest.fixture
def tree(tmp_path: Path) -> Tree:
    """A repository with one stamped test."""
    return Tree(tmp_path / "repo")


def test_a_test_added_to_the_same_file_later_keeps_the_hash(tree: Tree) -> None:
    before = tree.hash()
    tree.put(FILE, tree.read(FILE) + "\n" + OTHER)
    tree.commit("a second test")
    assert tree.hash() == before
    assert fails_first.staleness(tree.root, "HEAD", tree.stamp()) is None


def test_a_test_added_before_the_stamped_one_keeps_the_hash(tree: Tree) -> None:
    before = tree.hash()
    tree.put(FILE, OTHER + "\n" + tree.read(FILE))
    tree.commit("a test above it")
    assert tree.hash() == before


def test_an_edit_to_the_stamped_tests_body_makes_the_stamp_stale(tree: Tree) -> None:
    tree.put(FILE, tree.read(FILE).replace("== 7", "== 8"))
    tree.commit("weaken it")
    reason = fails_first.staleness(tree.root, "HEAD", tree.stamp())
    assert reason is not None and "changed since" in reason and FILE in reason


def test_an_edit_to_another_test_in_the_file_keeps_the_stamp(tree: Tree) -> None:
    tree.put(FILE, tree.read(FILE) + OTHER)
    stamp = tree.stamp(**{fails_first.TEST_COVER: tree.hash()})
    tree.put(FILE, tree.read(FILE).replace("other() == 1", "other() == 2"))
    tree.commit("edit the other test")
    assert fails_first.staleness(tree.root, "HEAD", stamp) is None


def test_comments_and_layout_are_not_a_change_of_the_test(tree: Tree) -> None:
    before = tree.hash()
    tree.put(
        FILE,
        '// a note\nTEST_CASE( "Widget ready frame" )\n{\n  // why\n  REQUIRE( ready()   ==   7 );  /* x */\n}\n',
    )
    tree.commit("reformat")
    assert tree.hash() == before


def test_a_literal_or_a_tag_is_part_of_the_test(tree: Tree) -> None:
    before = tree.hash()
    tree.put(FILE, tree.read(FILE).replace('"Widget ready frame")', '"Widget ready frame", "[widget]")'))
    tree.commit("add a tag")
    assert tree.hash() != before


def test_braces_in_a_string_do_not_end_the_body_early(tree: Tree) -> None:
    tree.put(FILE, 'TEST_CASE("Widget ready frame") {\n    log("}");\n    REQUIRE(ready() == 7);\n}\n' + OTHER)
    tree.commit("a brace in a string")
    first = tree.hash()
    tree.put(FILE, tree.read(FILE).replace("== 7", "== 9"))
    tree.commit("edit after the string")
    assert tree.hash() != first


def test_a_renamed_test_is_stale(tree: Tree) -> None:
    tree.put(FILE, tree.read(FILE).replace("Widget ready frame", "Widget ready"))
    tree.commit("rename")
    assert fails_first.staleness(tree.root, "HEAD", tree.stamp()) is not None


def test_a_removed_test_is_stale(tree: Tree) -> None:
    tree.put(FILE, OTHER)
    tree.commit("remove it")
    assert fails_first.staleness(tree.root, "HEAD", tree.stamp()) is not None


def test_a_test_file_added_to_the_list_later_keeps_the_hash(tree: Tree) -> None:
    before = tree.hash()
    tree.put(LIST, tree.read(LIST).replace("    core/log_test.cpp", "    ai/brain_test.cpp\n    core/log_test.cpp"))
    tree.commit("register another file")
    assert tree.hash() == before


def test_a_test_file_taken_out_of_the_list_is_stale(tree: Tree) -> None:
    tree.put(LIST, tree.read(LIST).replace("    ai/widget_test.cpp\n", ""))
    tree.commit("unregister it")
    assert fails_first.staleness(tree.root, "HEAD", tree.stamp()) is not None


def test_a_file_in_a_folder_of_its_own_is_registered_by_that_folders_list(tree: Tree) -> None:
    tree.put("tests/ai/CMakeLists.txt", "target_sources(coney_tests PRIVATE widget_test.cpp other_test.cpp)\n")
    tree.commit("a folder list")
    before = tree.hash()
    tree.put("tests/ai/CMakeLists.txt", "target_sources(coney_tests PRIVATE widget_test.cpp)\n")
    tree.commit("change the folder list")
    assert tree.hash() != before


def test_a_name_that_is_a_prefix_of_another_file_is_not_its_registration(tree: Tree) -> None:
    before = tree.hash()
    tree.put(
        LIST, tree.read(LIST).replace("    core/log_test.cpp", "    core/ai/widget_test.cpp.in\n    core/log_test.cpp")
    )
    tree.commit("a lookalike")
    assert tree.hash() == before


def test_an_old_stamp_stays_valid_while_the_files_are_as_they_were(tree: Tree) -> None:
    assert fails_first.staleness(tree.root, "HEAD", tree.legacy()) is None


def test_an_old_stamp_stays_valid_when_a_test_was_added_to_its_file_later(tree: Tree) -> None:
    stamp = tree.legacy()
    tree.put(FILE, tree.read(FILE) + OTHER)
    tree.commit("a second test")
    assert fails_first.staleness(tree.root, "HEAD", stamp) is None


def test_an_old_stamp_is_stale_when_its_tests_body_changed(tree: Tree) -> None:
    stamp = tree.legacy()
    tree.put(FILE, tree.read(FILE).replace("== 7", "== 8") + OTHER)
    tree.commit("weaken it")
    reason = fails_first.staleness(tree.root, "HEAD", stamp)
    assert reason == f"its test file changed since ({FILE}, {LIST})"


def test_an_old_stamp_whose_hash_is_not_the_files_at_its_fix_commit_is_stale(tree: Tree) -> None:
    stamp = tree.legacy()
    forged = stamps.Stamp({**stamp.data, "covers": {**stamp.covers, fails_first.FILES_COVER: "0" * 64}})
    tree.put(FILE, tree.read(FILE) + OTHER)
    tree.commit("a second test")
    assert fails_first.staleness(tree.root, "HEAD", forged) is not None


def test_an_old_stamp_whose_fix_commit_is_gone_falls_back_to_the_files(tree: Tree) -> None:
    stamp = tree.legacy()
    gone = stamps.Stamp({**stamp.data, "covers": {**stamp.covers, "fix_commit": "f" * 40}})
    tree.put(FILE, tree.read(FILE) + OTHER)
    tree.commit("a second test")
    assert fails_first.staleness(tree.root, "HEAD", gone) is not None


def test_declared_bodies_gives_each_declaration_of_a_name() -> None:
    bodies = fails_first.declared_bodies('TEST_CASE("a") { f(); }\nTEST_CASE("a") { g(); }\nTEST_CASE("b") { h(); }\n')
    assert sorted(bodies) == ["a", "b"] and len(bodies["a"]) == 2 and "g()" in bodies["a"][1]
