# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the change hash: what a stamp is bound to instead of a commit (docs/guides/stamps.md#change)."""

import subprocess
from pathlib import Path

import pytest
from git_support import git

from coney_tools import pr_diff
from coney_tools.config import ConfigError

NINE = "".join(f"line {n}\n" for n in range(1, 10))


def commit(repo: Path, name: str, text: str | bytes, message: str = "change") -> str:
    """Write `name` (creating folders), commit it, return the new commit."""
    path = repo / name
    path.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(text, bytes):
        path.write_bytes(text)
    else:
        path.write_text(text, encoding="utf-8", newline="")
    git(repo, "add", "-A")
    git(repo, "commit", "-q", "-m", message)
    return git(repo, "rev-parse", "HEAD").strip()


@pytest.fixture
def repo(tmp_path: Path) -> Path:
    """A repository whose `main` holds a nine-line file and another file; HEAD is on the branch `work`."""
    git(tmp_path, "init", "-q", "-b", "main")
    git(tmp_path, "config", "core.autocrlf", "false")  # a CRLF test must reach the blob as written
    commit(tmp_path, "src/widget.cpp", NINE, "base")
    commit(tmp_path, "src/other.cpp", "other\n", "base 2")
    git(tmp_path, "checkout", "-q", "-b", "work")
    return tmp_path


def change(repo: Path, tip: str = "HEAD", target: str = "main") -> str:
    """The change hash of `tip` against the merge base with `target`."""
    return pr_diff.change_of(repo, target, tip).sha256


def rebase(repo: Path) -> None:
    """Rebase `work` onto `main`."""
    git(repo, "rebase", "-q", "main")


def test_the_hash_is_a_sha256_and_names_the_changed_paths(repo: Path) -> None:
    commit(repo, "src/widget.cpp", NINE.replace("line 5", "LINE 5"))
    commit(repo, "docs/new.md", "new\n")
    found = pr_diff.change_of(repo, "main", "HEAD")
    assert len(found.sha256) == 64 and int(found.sha256, 16) >= 0
    assert found.paths == ("docs/new.md", "src/widget.cpp")
    assert found.base == git(repo, "rev-parse", "main").strip()
    assert found.tip == git(repo, "rev-parse", "HEAD").strip()


def test_a_clean_rebase_onto_a_main_that_edited_other_files_keeps_the_hash(repo: Path) -> None:
    commit(repo, "src/widget.cpp", NINE.replace("line 5", "LINE 5"))
    before = change(repo)
    git(repo, "checkout", "-q", "main")
    commit(repo, "src/unrelated.cpp", "one\n", "main moves")
    git(repo, "checkout", "-q", "work")
    old_tip = git(repo, "rev-parse", "HEAD").strip()
    rebase(repo)
    assert git(repo, "rev-parse", "HEAD").strip() != old_tip  # the commit id changed ...
    assert change(repo) == before  # ... the change did not


def test_a_clean_rebase_onto_a_main_that_edited_other_lines_of_the_same_file_keeps_the_hash(repo: Path) -> None:
    commit(repo, "src/widget.cpp", NINE.replace("line 8", "LINE 8"))
    before = change(repo)
    git(repo, "checkout", "-q", "main")
    commit(repo, "src/widget.cpp", "new first line\nnew second line\n" + NINE.replace("line 1\n", ""), "main moves")
    git(repo, "checkout", "-q", "work")
    rebase(repo)
    assert change(repo) == before  # the hunk moved down the file; its lines did not change


def test_the_hash_does_not_depend_on_how_the_branch_is_cut_into_commits(repo: Path) -> None:
    commit(repo, "src/widget.cpp", NINE.replace("line 2", "LINE 2"), "first")
    commit(repo, "src/widget.cpp", NINE.replace("line 2", "LINE 2").replace("line 7", "LINE 7"), "second")
    two = change(repo)
    git(repo, "reset", "-q", "--soft", "main")
    git(repo, "commit", "-q", "-m", "squashed")
    assert change(repo) == two


def test_the_hash_differs_when_upstream_made_one_of_the_branch_hunks_a_no_op(repo: Path) -> None:
    commit(repo, "src/widget.cpp", NINE.replace("line 3", "LINE 3").replace("line 8", "LINE 8"))
    before = change(repo)
    git(repo, "checkout", "-q", "main")
    commit(repo, "src/widget.cpp", NINE.replace("line 3", "LINE 3"), "main made the same edit")
    git(repo, "checkout", "-q", "work")
    rebase(repo)
    after = change(repo)
    assert after != before  # only the line 8 hunk is left of the branch's change


def test_one_changed_line_changes_the_hash(repo: Path) -> None:
    commit(repo, "src/widget.cpp", NINE.replace("line 5", "LINE 5"))
    before = change(repo)
    commit(repo, "src/widget.cpp", NINE.replace("line 5", "LINE 6"))
    assert change(repo) != before


@pytest.mark.parametrize("edit", ["trailing space", "tab", "crlf", "no final newline"])
def test_a_whitespace_change_counts(repo: Path, edit: str) -> None:
    base = NINE.replace("line 5", "LINE 5")
    commit(repo, "src/widget.cpp", base)
    before = change(repo)
    other = {
        "trailing space": base.replace("LINE 5\n", "LINE 5 \n"),
        "tab": base.replace("LINE 5\n", "LINE 5\t\n"),
        "crlf": base.replace("LINE 5\n", "LINE 5\r\n"),
        "no final newline": base.rstrip("\n"),
    }[edit]
    commit(repo, "src/widget.cpp", other)
    assert change(repo) != before


def test_a_path_is_part_of_the_hash_and_a_file_added_then_removed_is_no_change(repo: Path) -> None:
    commit(repo, "src/a.cpp", "same\n")
    one = change(repo)
    git(repo, "mv", "src/a.cpp", "src/b.cpp")
    git(repo, "commit", "-q", "-m", "rename")
    assert change(repo) != one  # another path with the same text
    git(repo, "rm", "-q", "src/b.cpp")
    git(repo, "commit", "-q", "-m", "delete")
    assert change(repo) == change(repo, "main")  # nothing left of the change: the empty change


def test_the_empty_change_has_a_stable_hash(repo: Path) -> None:
    empty = pr_diff.change_of(repo, "main", "HEAD")
    assert empty.paths == () and len(empty.sha256) == 64
    assert empty.sha256 == pr_diff.change_of(repo, "main", "main").sha256


def test_a_mode_change_through_git_is_part_of_the_hash(repo: Path) -> None:
    before = change(repo)
    git(repo, "update-index", "--chmod=+x", "src/other.cpp")
    git(repo, "commit", "-q", "-m", "chmod")
    assert change(repo) != before


def test_a_binary_file_is_hashed_by_its_content(repo: Path) -> None:
    commit(repo, "assets/pic.bin", b"\x00\x01\x02binary")
    first = change(repo)
    commit(repo, "assets/pic.bin", b"\x00\x01\x02binary!")
    second = change(repo)
    assert first != second
    commit(repo, "assets/pic.bin", b"\x00\x01\x02binary")
    assert change(repo) == first  # back to the same content: the same change


def test_a_binary_file_survives_a_rebase_too(repo: Path) -> None:
    commit(repo, "assets/pic.bin", b"\x00\x01\x02binary")
    before = change(repo)
    git(repo, "checkout", "-q", "main")
    commit(repo, "assets/other.bin", b"\x00\x09", "main moves")
    git(repo, "checkout", "-q", "work")
    rebase(repo)
    assert change(repo) == before


def test_a_path_with_spaces_and_non_ascii_letters_works(repo: Path) -> None:
    commit(repo, "docs/a name with spaces.md", "x\n")
    commit(repo, "docs/café.md", "y\n")
    found = pr_diff.change_of(repo, "main", "HEAD")
    assert found.paths == ("docs/a name with spaces.md", "docs/café.md")


def test_a_branch_that_is_behind_main_has_the_same_change_as_before(repo: Path) -> None:
    commit(repo, "src/widget.cpp", NINE.replace("line 5", "LINE 5"))
    before = change(repo)
    git(repo, "checkout", "-q", "main")
    commit(repo, "src/unrelated.cpp", "one\n", "main moves")
    git(repo, "checkout", "-q", "work")
    assert change(repo) == before  # the merge base is the old main: no rebase needed to ask


def test_an_unknown_commit_is_a_config_error(repo: Path) -> None:
    with pytest.raises(ConfigError, match="nowhere"):
        pr_diff.change_of(repo, "main", "nowhere")


def test_the_hash_is_the_same_whatever_the_users_git_settings(repo: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    commit(repo, "src/widget.cpp", NINE.replace("line 5", "LINE 5"))
    plain = change(repo)
    subprocess.run(["git", "-C", str(repo), "config", "diff.renames", "true"], check=True)
    subprocess.run(["git", "-C", str(repo), "config", "diff.algorithm", "patience"], check=True)
    subprocess.run(["git", "-C", str(repo), "config", "core.quotepath", "true"], check=True)
    subprocess.run(["git", "-C", str(repo), "config", "diff.noprefix", "true"], check=True)
    assert change(repo) == plain
