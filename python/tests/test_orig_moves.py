# SPDX-License-Identifier: GPL-3.0-or-later
"""The move map (`orig_review.function_moves`) reads only what a change can move, and finds what reading the whole tree
finds (issue 1386). Invented code, names and addresses only.

The oracle is the whole-tree rule: `moves` over the standing of every function of both trees. A scenario, and a
seeded run of random edits, must give the same map as it; and the work done is counted, so a body edit parses only the
files it changed."""

from __future__ import annotations

import random
from collections.abc import Mapping
from pathlib import Path

import pytest
from conventions_support import write_checkout
from git_support import git

from coney_tools import orig_review
from coney_tools.orig_scan import SourceFile, read_source

Texts = dict[str, str]


def whole_tree(before: Mapping[str, str], after: Mapping[str, str]) -> dict[tuple[str, int], orig_review.Unit]:
    """The move map as the whole-tree rule gives it: every function of both trees read, none left out."""
    found = orig_review.moves(orig_review.standings(before).values(), orig_review.standings(after).values()).moved
    return {(move.unit.path, move.unit.line): move.was for move in found}


def same_map(before: Texts, after: Texts) -> dict[tuple[str, int], orig_review.Unit]:
    """`function_moves` of the two trees; fails unless it is the whole-tree map. Returns the map."""
    expected = whole_tree(before, after)
    assert orig_review.function_moves(before, after) == expected
    return expected


HEADER = """#pragma once
namespace coney::ai {
class Brain {
  public:
    /// Thinks.
    /// @orig 0x00100010 Brain_Think (Brain.cpp)
    int think(int hunger);
    /// @coney-only a test aid
    void dump();
    int sleep(int hours);
};
} // namespace coney::ai
"""

THINK = "int Brain::think(int hunger) {\n    return hunger > 3 ? 1 : 0;\n}\n"
SLEEP = "int Brain::sleep(int hours) {\n    return hours * 2;\n}\n"

SOURCE = f"""#include "ai/brain.h"
namespace coney::ai {{
{THINK}void Brain::dump() {{}}
{SLEEP}}} // namespace coney::ai
"""

OTHER = """#include "ai/other.h"
namespace coney::ai {
/// @orig 0x00100030 Other_Run (Other.cpp)
int run(int a) {
    return a + 1;
}
} // namespace coney::ai
"""

UNRELATED = """#include "gfx/unrelated.h"
namespace coney::gfx {
// A helper nothing else names.
int clamp(int v) {
    return v < 0 ? 0 : v;
}
} // namespace coney::gfx
"""

#: A destructor, an operator and an overload of `sleep`, which a header's tag covers for the first two only.
SPECIAL = """/// @orig 0x00100040 Brain_Free (Brain.cpp)
Brain::~Brain() {
    release();
}
/// @orig 0x00100050 Brain_Eq (Brain.cpp)
bool Brain::operator==(const Brain& o) const {
    return o.id == id;
}
int Brain::sleep(int hours, int more) {
    return hours + more;
}
"""


def source_of(*definitions: str) -> str:
    """A source file of `brain.h` holding these definitions."""
    return '#include "ai/brain.h"\nnamespace coney::ai {\n' + "".join(definitions) + "} // namespace coney::ai\n"


def tree() -> Texts:
    """A small tree: a header with its source, a free function in a file of its own and a file of another folder."""
    return {
        "src/ai/brain.h": HEADER,
        "src/ai/brain.cpp": SOURCE,
        "src/ai/other.cpp": OTHER,
        "src/gfx/unrelated.cpp": UNRELATED,
    }


# --- the same map as reading everything ---------------------------------------------------------------------------


def test_an_unchanged_tree_has_no_move() -> None:
    assert same_map(tree(), tree()) == {}


def test_an_edit_that_adds_or_removes_no_function_has_no_move() -> None:
    after = tree()
    after["src/ai/brain.cpp"] = SOURCE.replace("hunger > 3", "hunger > 4")
    assert same_map(tree(), after) == {}


def test_a_function_moved_to_another_file_is_a_move() -> None:
    after = tree()
    after["src/ai/brain.cpp"] = SOURCE.replace(SLEEP, "")
    after["src/ai/brain_sleep.cpp"] = source_of(SLEEP)
    moved = same_map(tree(), after)
    assert [(path, was.coney) for (path, _), was in moved.items()] == [("src/ai/brain_sleep.cpp", "Brain::sleep")]


def test_a_definition_moved_to_another_file_keeps_the_tag_of_its_unchanged_declaration() -> None:
    """The header (unchanged) tags the definition: the move must still read it as the tag of both."""
    after = tree()
    after["src/ai/brain.cpp"] = SOURCE.replace(THINK, "")
    after["src/ai/think.cpp"] = source_of(THINK)
    moved = same_map(tree(), after)
    assert len(moved) == 1


def test_a_free_function_moved_to_another_folder_is_a_move() -> None:
    after = tree()
    del after["src/ai/other.cpp"]
    after["src/gfx/other.cpp"] = OTHER.replace("coney::ai", "coney::gfx")
    assert len(same_map(tree(), after)) == 1


def test_a_copy_is_not_a_move() -> None:
    after = tree()
    after["src/ai/copy.cpp"] = OTHER
    assert same_map(tree(), after) == {}


def test_an_untagged_function_moves_only_under_its_own_name() -> None:
    after = tree()
    after["src/gfx/unrelated.cpp"] = UNRELATED.replace("clamp", "limit")
    assert same_map(tree(), after) == {}
    after = tree()
    del after["src/gfx/unrelated.cpp"]
    after["src/gfx/other_name.cpp"] = UNRELATED
    assert len(same_map(tree(), after)) == 1


def test_a_move_of_a_destructor_an_operator_and_an_overload() -> None:
    before = tree()
    before["src/ai/ops.cpp"] = source_of(SPECIAL)
    after = dict(before)
    del after["src/ai/ops.cpp"]
    after["src/ai/ops2.cpp"] = source_of(SPECIAL)
    assert len(same_map(before, after)) >= 2


def test_a_deleted_file_and_a_new_one_are_read_as_one_side_each() -> None:
    after = tree()
    del after["src/ai/brain.cpp"]
    after["src/ai/new.cpp"] = SOURCE.replace("Brain", "Mind")
    same_map(tree(), after)
    same_map(after, tree())


# --- seeded edits against the whole-tree rule ---------------------------------------------------------------------

_NAMES = ("think", "sleep", "run", "dump", "step", "grow")
_CLASSES = ("Brain", "Mind", "Body", "")


def _function(rng: random.Random, scope: str, name: str) -> str:
    """One definition, tagged or not, with one of a few bodies (so two functions can share code)."""
    tag = rng.choice(["", "/// @orig 0x00100010 Think (Brain.cpp)\n", "/// @coney-only a test aid\n", "// plain\n"])
    qualified = f"{scope}::{name}" if scope else name
    body = rng.choice(["return a + 1;", "return a * 2;", f"return {name}Helper(a);"])
    return f"{tag}int {qualified}(int a) {{\n    {body}\n}}\n"


def _corpus(rng: random.Random) -> Texts:
    """Six source files and two headers drawn from a few names, so overloads, copies and declarations happen."""
    texts: Texts = {}
    for index in range(6):
        folder = rng.choice(["ai", "gfx"])
        parts = ["namespace coney {\n"]
        for _ in range(rng.randint(1, 4)):
            parts.append(_function(rng, rng.choice(_CLASSES), rng.choice(_NAMES)))
        parts.append("}\n")
        texts[f"src/{folder}/file{index}.cpp"] = "".join(parts)
    for index in range(2):
        declared = "".join(
            f"{rng.choice(['/// @orig 0x00100020 Run (Brain.cpp)\n', ''])}    int {rng.choice(_NAMES)}(int a);\n"
            for _ in range(rng.randint(1, 3))
        )
        texts[f"src/ai/file{index}.h"] = f"class {rng.choice(_CLASSES[:3])} {{\n  public:\n{declared}}};\n"
    return texts


def _edit(rng: random.Random, texts: Texts) -> Texts:
    """The tree after a few of: a function moved or copied to another file, a rename, a body edit, a file added or
    removed."""
    after = dict(texts)
    for _ in range(rng.randint(1, 4)):
        paths = sorted(p for p in after if p.endswith(".cpp"))
        path = rng.choice(paths)
        action = rng.choice(["move", "rename", "body", "copy", "drop", "new"])
        if action == "new":
            definition = _function(rng, rng.choice(_CLASSES), rng.choice(_NAMES))
            after[f"src/ai/new{rng.randint(0, 99)}.cpp"] = "namespace coney {\n" + definition + "}\n"
        elif action == "drop":
            del after[path]
        elif action == "body":
            after[path] = after[path].replace("a + 1", "a + 7").replace("a * 2", "a * 3")
        elif action == "rename":
            after[path] = after[path].replace("Brain::", "Mind::").replace(" think(", " ponder(")
        else:
            target = rng.choice(paths)
            text = after[path]
            start = text.find("\n", text.find("namespace coney {")) + 1
            end = text.find("\n}\n", start)
            if end < 0:
                continue
            chunk = text[start : end + 3]
            if action == "move":
                after[path] = text[:start] + text[end + 3 :]
            after[target] = after[target].replace("namespace coney {\n", "namespace coney {\n" + chunk, 1)
    return after


@pytest.mark.parametrize("seed", range(60))
def test_random_edits_give_the_whole_tree_map(seed: int) -> None:
    rng = random.Random(seed)
    before = _corpus(rng)
    same_map(before, _edit(rng, before))


# --- the work done -------------------------------------------------------------------------------------------------


@pytest.fixture
def parsed(monkeypatch: pytest.MonkeyPatch) -> list[str]:
    """The paths `read_source` is called for, in order, during a test."""
    paths: list[str] = []

    def count(path: str, text: str) -> SourceFile:
        paths.append(path)
        return read_source(path, text)

    monkeypatch.setattr(orig_review, "read_source", count)
    return paths


def test_an_edit_that_adds_or_removes_no_function_parses_only_the_files_it_changed(parsed: list[str]) -> None:
    after = tree()
    after["src/ai/brain.cpp"] = SOURCE.replace("hunger > 3", "hunger > 4")
    assert orig_review.function_moves(tree(), after) == {}
    assert sorted(parsed) == ["src/ai/brain.cpp", "src/ai/brain.cpp"]


def test_a_tree_that_did_not_change_parses_nothing(parsed: list[str]) -> None:
    assert orig_review.function_moves(tree(), tree()) == {}
    assert parsed == []


def test_a_move_parses_the_files_that_name_the_function_and_not_the_others(parsed: list[str]) -> None:
    after = tree()
    del after["src/ai/other.cpp"]
    after["src/ai/runner.cpp"] = OTHER
    assert len(orig_review.function_moves(tree(), after)) == 1
    # The header, the other source of its folder and the file of another folder say nothing of `run`.
    assert "src/gfx/unrelated.cpp" not in parsed
    assert "src/ai/brain.h" not in parsed
    assert "src/ai/brain.cpp" not in parsed


def test_a_move_reads_the_declaration_in_a_file_that_did_not_change(parsed: list[str]) -> None:
    after = tree()
    after["src/ai/brain.cpp"] = SOURCE.replace(SLEEP, "")
    after["src/ai/brain_sleep.cpp"] = source_of(SLEEP)
    moved = orig_review.function_moves(tree(), after)
    read = list(parsed)
    assert moved == whole_tree(tree(), after)
    assert "src/ai/brain.h" in read
    assert "src/gfx/unrelated.cpp" not in read


# --- the commit's files, with the text of the working tree reused --------------------------------------------------


@pytest.fixture
def repo(tmp_path: Path) -> Path:
    """A checkout on `main` holding the small tree."""
    root = tmp_path / "repos" / "coney"
    root.mkdir(parents=True)
    write_checkout(root)
    for name, text in tree().items():
        (root / name).parent.mkdir(parents=True, exist_ok=True)
        (root / name).write_text(text, encoding="utf-8", newline="\n")
    git(root, "init", "-q", "-b", "main")
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "base")
    return root


def spy_on_blob_reads(monkeypatch: pytest.MonkeyPatch) -> list[bytes]:
    """The requests `files_at` makes to `git cat-file --batch`, one entry for each."""
    fetched: list[bytes] = []
    inner = orig_review._git

    def spy(root: Path, *args: str, stdin: bytes | None = None) -> bytes:
        if args[:1] == ("cat-file",):
            fetched.append(stdin or b"")
        return inner(root, *args, stdin=stdin)

    monkeypatch.setattr(orig_review, "_git", spy)
    return fetched


def test_the_files_at_a_commit_reuse_the_text_that_is_the_same_blob(
    repo: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    (repo / "src/ai/brain.cpp").write_text(SOURCE.replace("hunger > 3", "hunger > 4"), encoding="utf-8", newline="\n")
    (repo / "src/ai/added.cpp").write_text("int added() { return 1; }\n", encoding="utf-8", newline="\n")
    fetched = spy_on_blob_reads(monkeypatch)
    shared = orig_review.files_at(repo, "HEAD", known=orig_review.texts_now(repo))
    # Only brain.cpp is read from git: the other three files are what the commit holds.
    assert len(fetched) == 1 and fetched[0].count(b"\n") == 1
    assert shared == orig_review.files_at(repo, "HEAD")
    assert shared["src/ai/brain.cpp"] == SOURCE


def test_a_commit_known_in_full_reads_no_blob(repo: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    fetched = spy_on_blob_reads(monkeypatch)
    assert orig_review.files_at(repo, "HEAD", known=orig_review.texts_now(repo)) == tree()
    assert fetched == []


def test_moves_since_gives_the_whole_tree_map_for_a_working_tree_move(repo: Path) -> None:
    since = git(repo, "rev-parse", "HEAD").strip()
    (repo / "src/ai/other.cpp").unlink()
    (repo / "src/ai/runner.cpp").write_text(OTHER, encoding="utf-8", newline="\n")
    moved = orig_review.moves_since(repo, since)
    assert moved == whole_tree(orig_review.files_at(repo, since), orig_review.texts_now(repo))
    assert len(moved) == 1


def test_the_known_text_does_not_limit_the_paths_the_base_is_read_for(repo: Path) -> None:
    (repo / "src/ai/other.cpp").unlink()
    # `known` is the disk's text, not a list of the files wanted: a file the disk lacks is still read from the commit.
    assert "src/ai/other.cpp" in orig_review.files_at(repo, "HEAD", known=orig_review.texts_now(repo))
    assert list(orig_review.files_at(repo, "HEAD", {"src/ai/other.cpp"}, orig_review.texts_now(repo))) == [
        "src/ai/other.cpp"
    ]
