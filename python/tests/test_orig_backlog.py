# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools orig check-tags --backlog`: the untagged functions that arrived in a gameplay folder by a
move, found in a made-up git history."""

from pathlib import Path

import pytest
from conventions_support import write_checkout
from git_support import git

from coney_tools import orig_backlog
from coney_tools.cli import main

#: A platform header (src/platform/ is Coney-only: nothing there needs a tag) with an untagged member, a tagged one, a
#: marked one and an accessor.
HATS_H = """#pragma once
namespace coney {
class PlayLevelMode {
  public:
    void stepHats(int count);
    /// @orig 0x00100030 Mode_StepCaps (Mode.cpp)
    void stepCaps(int count);
    /// @coney-only a test aid
    void dumpHats(int count);
    int hats() const { return m_hats; }
  private:
    int m_hats = 0;
};
} // namespace coney
"""

HATS_CPP = """#include "platform/play_level_mode.h"
namespace coney {
// Each hat.
void PlayLevelMode::stepHats(int count) {
    for (int i = 0; i < count; ++i) {
        PlayLevelMode::kick(i);
    }
}
void PlayLevelMode::stepCaps(int count) {
    for (int i = 0; i < count; ++i) {
        PlayLevelMode::tip(i);
    }
}
void PlayLevelMode::dumpHats(int count) {
    for (int i = 0; i < count; ++i) {
        PlayLevelMode::print(i);
    }
}
} // namespace coney
"""

BRAIN_CPP = """namespace coney::ai {
// Untagged since the start.
void legacyThink() {
    go();
    go();
}
} // namespace coney::ai
"""


def commit(repo: Path, title: str) -> str:
    """Commit everything; the new commit's id."""
    git(repo, "add", "-A")
    git(repo, "commit", "-q", "-m", title)
    return git(repo, "rev-parse", "HEAD").strip()


def write(repo: Path, files: dict[str, str]) -> None:
    """Write files (creating their folders)."""
    for name, text in files.items():
        (repo / name).parent.mkdir(parents=True, exist_ok=True)
        (repo / name).write_text(text, encoding="utf-8")


def move(repo: Path, source: str, target: str, rename: tuple[str, str] | None = None) -> None:
    """`git mv` a file, then (when `rename` is given) rewrite a name in it."""
    (repo / target).parent.mkdir(parents=True, exist_ok=True)
    git(repo, "mv", source, target)
    if rename:
        path = repo / target
        path.write_text(path.read_text(encoding="utf-8").replace(*rename), encoding="utf-8")


@pytest.fixture
def repo(tmp_path: Path) -> Path:
    """A history: a base with an old untagged gameplay function, the platform files, an unrelated change."""
    write_checkout(tmp_path)
    git(tmp_path, "init", "-q", "-b", "main")
    write(tmp_path, {"src/ai/brain.cpp": BRAIN_CPP})
    commit(tmp_path, "core: Add the brain")
    write(tmp_path, {"src/platform/play_level_mode.h": HATS_H, "src/platform/hats.cpp": HATS_CPP})
    commit(tmp_path, "platform: Add the hats")
    write(tmp_path, {"src/ai/note.txt": "x"})
    commit(tmp_path, "docs: Touch something else")
    return tmp_path


def names(found: list[orig_backlog.MovedFunction]) -> list[str]:
    """The qualified names of what was found."""
    return sorted(f.coney for f in found)


def test_a_history_without_a_move_has_no_backlog(repo: Path) -> None:
    assert orig_backlog.find(repo) == []


def test_an_untagged_function_moved_into_a_gameplay_folder_is_listed_with_its_commit(repo: Path) -> None:
    move(repo, "src/platform/hats.cpp", "src/gamemodes/hats.cpp")
    moved = commit(repo, "core: Move the hats into gamemodes")
    found = orig_backlog.find(repo)
    assert names(found) == ["PlayLevelMode::stepHats"]
    assert found[0].commit == moved
    assert found[0].title == "core: Move the hats into gamemodes"
    assert found[0].moved_from.startswith("src/platform/hats.cpp:")
    assert found[0].where.startswith("src/gamemodes/hats.cpp:")
    assert found[0].lines == 5


def test_a_function_written_new_in_a_gameplay_folder_is_not_a_move(repo: Path) -> None:
    write(repo, {"src/ai/fresh.cpp": "namespace coney::ai {\nvoid fresh() {\n    go();\n    go();\n}\n}\n"})
    commit(repo, "core: Add fresh")
    assert orig_backlog.find(repo) == []


def test_a_function_that_was_always_in_a_gameplay_folder_is_not_listed(repo: Path) -> None:
    move(repo, "src/platform/hats.cpp", "src/gamemodes/hats.cpp")
    commit(repo, "core: Move the hats")
    assert "legacyThink" not in names(orig_backlog.find(repo))


def test_a_move_between_gameplay_folders_is_not_an_arrival(repo: Path) -> None:
    move(repo, "src/ai/brain.cpp", "src/human/brain.cpp")
    commit(repo, "core: Move the brain")
    assert orig_backlog.find(repo) == []


def test_a_function_moved_on_inside_gameplay_keeps_the_commit_that_brought_it_in(repo: Path) -> None:
    move(repo, "src/platform/hats.cpp", "src/gamemodes/hats.cpp")
    arrived = commit(repo, "core: Move the hats into gamemodes")
    move(repo, "src/gamemodes/hats.cpp", "src/human/hats.cpp")
    commit(repo, "core: Move the hats on to human")
    found = orig_backlog.find(repo)
    assert [f.commit for f in found] == [arrived]
    assert found[0].where.startswith("src/human/hats.cpp:")
    assert found[0].moved_from.startswith("src/platform/hats.cpp:")


def test_a_moved_and_renamed_function_is_found_under_its_new_name(repo: Path) -> None:
    rename = ("PlayLevelMode", "PlayLevelSimulation")
    move(repo, "src/platform/hats.cpp", "src/gamemodes/hats.cpp", rename)
    move(repo, "src/platform/play_level_mode.h", "src/gamemodes/play_level_simulation.h", rename)
    commit(repo, "core: Rework the hats")
    assert names(orig_backlog.find(repo)) == ["PlayLevelSimulation::stepHats"]


def test_a_function_moved_and_changed_in_one_commit_is_new_work_not_a_move(repo: Path) -> None:
    move(repo, "src/platform/hats.cpp", "src/gamemodes/hats.cpp")
    path = repo / "src/gamemodes/hats.cpp"
    path.write_text(
        path.read_text(encoding="utf-8").replace("kick(i);", "kick(i);\n        kick(i);"), encoding="utf-8"
    )
    commit(repo, "core: Move and change the hats")
    assert orig_backlog.find(repo) == []


def test_a_body_replaced_later_under_the_same_key_is_credited_to_the_earlier_move(repo: Path) -> None:
    # The documented limit: the search follows the key, so the later rewrite does not end it.
    move(repo, "src/platform/hats.cpp", "src/gamemodes/hats.cpp")
    moved = commit(repo, "core: Move the hats")
    path = repo / "src/gamemodes/hats.cpp"
    path.write_text(path.read_text(encoding="utf-8").replace("kick(i);", "spin(i); spin(i);"), encoding="utf-8")
    commit(repo, "core: Rewrite the hats")
    assert [f.commit for f in orig_backlog.find(repo)] == [moved]


def test_a_function_deleted_and_written_again_is_not_credited_to_the_old_move(repo: Path) -> None:
    move(repo, "src/platform/hats.cpp", "src/gamemodes/hats.cpp")
    commit(repo, "core: Move the hats")
    path = repo / "src/gamemodes/hats.cpp"
    text = path.read_text(encoding="utf-8")
    path.write_text(text.replace("void PlayLevelMode::stepHats", "void PlayLevelMode::stepHatsGone"), encoding="utf-8")
    commit(repo, "core: Drop the hats")
    path.write_text(text, encoding="utf-8")
    commit(repo, "core: Write the hats again")
    assert orig_backlog.find(repo) == []


def _edit_hats_after_the_move(repo: Path, old: str, new: str, title: str) -> str:
    """Move the hats into gamemodes, then change the moved file in a later commit; the move's commit id."""
    move(repo, "src/platform/hats.cpp", "src/gamemodes/hats.cpp")
    moved = commit(repo, "core: Move the hats")
    path = repo / "src/gamemodes/hats.cpp"
    path.write_text(path.read_text(encoding="utf-8").replace(old, new), encoding="utf-8")
    commit(repo, title)
    return moved


def test_a_later_call_to_a_moved_function_does_not_drop_it(repo: Path) -> None:
    moved = _edit_hats_after_the_move(
        repo,
        "PlayLevelMode::print(i);",
        "PlayLevelMode::print(i);\n        PlayLevelMode::stepHats(1);",
        "core: Call it",
    )
    assert [f.commit for f in orig_backlog.find(repo)] == [moved]


def test_a_later_comment_naming_a_moved_function_does_not_drop_it(repo: Path) -> None:
    moved = _edit_hats_after_the_move(repo, "// Each hat.", "// Each hat.\n// See stepHats.", "core: Say so")
    assert [f.commit for f in orig_backlog.find(repo)] == [moved]


def test_a_later_overload_of_a_moved_function_does_not_drop_it(repo: Path) -> None:
    overload = "void PlayLevelMode::stepHats(double scale) {\n    kick(scale);\n    kick(scale);\n}\n"
    end = "} // namespace coney"
    moved = _edit_hats_after_the_move(repo, end, overload + end, "core: Overload it")
    found = orig_backlog.find(repo)
    assert [(f.coney, f.commit) for f in found] == [("PlayLevelMode::stepHats", moved)]


def test_a_moved_function_tagged_later_leaves_the_list(repo: Path) -> None:
    move(repo, "src/platform/hats.cpp", "src/gamemodes/hats.cpp")
    commit(repo, "core: Move the hats")
    path = repo / "src/gamemodes/hats.cpp"
    path.write_text(path.read_text(encoding="utf-8").replace("// Each hat.", "// @coney-only counts hats"))
    commit(repo, "core: Mark the hats")
    assert orig_backlog.find(repo) == []


def test_since_leaves_out_the_moves_before_it(repo: Path) -> None:
    move(repo, "src/platform/hats.cpp", "src/gamemodes/hats.cpp")
    moved = commit(repo, "core: Move the hats")
    write(repo, {"src/ai/note.txt": "y"})
    commit(repo, "docs: Touch it again")
    assert orig_backlog.find(repo, since="HEAD~1") == []
    assert [f.commit for f in orig_backlog.find(repo, since="HEAD~2")] == [moved]


def test_the_command_lists_the_function_and_exits_0(
    repo: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(repo)
    move(repo, "src/platform/hats.cpp", "src/gamemodes/hats.cpp")
    moved = commit(repo, "core: Move the hats")
    assert main(["orig", "check-tags", "--backlog"]) == 0
    out = capsys.readouterr().out
    assert "PlayLevelMode::stepHats (5 lines) moved from src/platform/hats.cpp:" in out
    assert f"in {moved[:12]} core: Move the hats" in out
    assert "1 untagged function(s) moved into gameplay folders" in out


def test_the_command_writes_a_tsv(
    repo: Path, monkeypatch: pytest.MonkeyPatch, tmp_path_factory: pytest.TempPathFactory
) -> None:
    monkeypatch.chdir(repo)
    move(repo, "src/platform/hats.cpp", "src/gamemodes/hats.cpp")
    moved = commit(repo, "core: Move the hats")
    out = tmp_path_factory.mktemp("out") / "moved.tsv"
    assert main(["orig", "check-tags", "--backlog", "--out", str(out)]) == 0
    rows = [line.split("\t") for line in out.read_text(encoding="utf-8").splitlines()]
    assert rows[0][:2] == ["where", "function"]
    assert rows[1][1] == "PlayLevelMode::stepHats" and rows[1][4] == moved


def test_since_and_out_need_backlog(repo: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.chdir(repo)
    with pytest.raises(SystemExit):
        main(["orig", "check-tags", "--since", "HEAD~1"])
