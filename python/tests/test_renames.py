# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.renames: the old command and option names still run their command, with a notice."""

import argparse
from pathlib import Path

import pytest

from coney_tools import renames
from coney_tools.cli import _build_parser, main
from coney_tools.renames import COMMANDS, OPTIONS, rewrite


def _commands(parser: argparse.ArgumentParser, prefix: tuple[str, ...] = ()) -> set[tuple[str, ...]]:
    """Every command path the parser knows, groups included."""
    found: set[tuple[str, ...]] = set()
    for action in parser._actions:
        if isinstance(action, argparse._SubParsersAction):
            for name, sub in action.choices.items():
                found.add((*prefix, name))
                found |= _commands(sub, (*prefix, name))
    return found


def _options(parser: argparse.ArgumentParser, path: tuple[str, ...]) -> set[str]:
    """The option strings of the command at `path`."""
    for word in path:
        sub = next(a for a in parser._actions if isinstance(a, argparse._SubParsersAction))
        parser = sub.choices[word]
    return {option for action in parser._actions for option in action.option_strings}


KNOWN = _commands(_build_parser())


@pytest.mark.parametrize("rename", COMMANDS, ids=lambda r: " ".join(r.old))
def test_every_new_command_exists_and_no_old_one_does(rename: renames.Rename) -> None:
    """The new words name a command of the parser (options it adds aside), and the old ones none any more, so the
    rewrite can never hide a command that still exists."""
    words = tuple(word for word in rename.new if not word.startswith("-"))
    assert words in KNOWN
    assert rename.old not in KNOWN


@pytest.mark.parametrize("option", OPTIONS, ids=lambda o: o.old)
def test_every_new_option_exists_and_no_old_one_does(option: renames.OptionRename) -> None:
    """A renamed option's new name is an option of its command and the old one is not."""
    known = _options(_build_parser(), option.command)
    assert option.old not in known
    assert option.new is None or option.new in known


def test_a_new_command_line_is_unchanged() -> None:
    """Typing the new names prints no notice and changes nothing."""
    argv = ["ci", "run", "--base", "origin/main", "--only", "repo"]
    assert rewrite(argv) == (argv, [])


def test_an_old_command_is_rewritten_with_one_notice() -> None:
    """`check changed --base X` becomes `ci run --base X`, and the notice names both."""
    argv, notices = rewrite(["check", "changed", "--base", "HEAD^1"])
    assert argv == ["ci", "run", "--base", "HEAD^1"]
    assert notices == ["`check changed` is renamed to `ci run`; the old name stops working at the cutover"]


def test_the_longest_old_name_wins() -> None:
    """`natives coney` is its own row; `natives render` falls back to the group's row."""
    assert rewrite(["natives", "coney", "--check"])[0] == ["bindings", "sync-coney", "--check"]
    assert rewrite(["natives", "render"])[0] == ["bindings", "render"]
    assert rewrite(["natives", "--help"])[0] == ["bindings", "--help"]


def test_a_rename_may_add_an_option() -> None:
    """`natives missions DISC` is `bindings scan-usage --story DISC`, and parses as one."""
    argv, _ = rewrite(["natives", "missions", "H:/", "--check"])
    assert argv == ["bindings", "scan-usage", "--story", "H:/", "--check"]
    args = _build_parser().parse_args(argv)
    assert args.story and not args.first_mission and args.disc == "H:/" and args.check


def test_a_word_after_an_option_is_not_a_command() -> None:
    """Only the leading words are a command: `issue check --title draft` is not `issue draft`."""
    argv = ["issue", "check", "--title", "draft"]
    assert rewrite(argv) == (argv, [])


def test_a_prefix_of_a_word_is_not_a_match() -> None:
    """`repo check-title` is its own row, not `repo check` followed by `-title`."""
    assert rewrite(["repo", "check-title", "-"])[0] == ["pr", "check-title", "-"]
    assert rewrite(["repo", "check"])[0] == ["repo", "check-conventions"]


def test_an_old_option_is_renamed_in_both_spellings() -> None:
    """`--shell-hours 3` and `--shell-hours=3` both become `--stale-wait-hours`."""
    argv, notices = rewrite(["workspace", "clean", "--shell-hours", "3", "--apply"])
    assert argv == ["workspace", "clean", "--stale-wait-hours", "3", "--apply"]
    assert notices == [
        "`workspace clean --shell-hours` is renamed to `--stale-wait-hours`; the old name stops working at the cutover"
    ]
    assert rewrite(["workspace", "clean", "--shell-hours=3"])[0] == ["workspace", "clean", "--stale-wait-hours=3"]


def test_an_option_of_another_command_is_left_alone() -> None:
    """`--xbox` is renamed for `extract` only; after `--` nothing is an option."""
    argv = ["xbox", "info", "--xbox"]
    assert rewrite(argv) == (argv, [])
    assert rewrite(["extract", "out", "--", "--xbox"])[0] == ["extract", "out", "--", "--xbox"]
    assert rewrite(["extract", "out", "--xbox", "x"])[0] == ["extract", "out", "--xbox-disc", "x"]


def test_an_option_that_is_the_default_now_is_dropped() -> None:
    """`clips clean --dry-run` is what `clips clean` does now: the option goes, with a notice."""
    argv, notices = rewrite(["clips", "clean", "--dry-run", "--older-than", "5"])
    assert argv == ["clips", "clean", "--older-than", "5"]
    assert notices == ["`clips clean --dry-run` is the default now; the option goes at the cutover"]


def test_main_runs_an_old_name_and_prints_the_notice(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """Through `main`, an old name runs the new command and says so once on stderr."""
    (tmp_path / "coney.local.example.toml").write_text("", encoding="utf-8")
    (tmp_path / "coney.local.toml").write_text("", encoding="utf-8")
    monkeypatch.chdir(tmp_path)
    monkeypatch.setattr(renames, "COMMANDS", (renames.Rename(("config", "list"), ("config", "show")),))
    assert main(["config", "list"]) == 0
    captured = capsys.readouterr()
    assert "game_dir" in captured.out
    assert (
        captured.err
        == "coney-tools: `config list` is renamed to `config show`; the old name stops working at the cutover\n"
    )


def test_main_parses_an_old_name_into_the_new_command() -> None:
    """Each kind of move parses: a command into another group, a group into another, a subcommand into a group."""
    cases = {
        ("repo", "pr-health"): ("pr", "list-stuck"),
        ("original", "study", "0x00100000"): ("orig", "study"),
        ("pcsx2", "record", "s.toml", "--out", "o.csv"): ("trace", "original"),
        ("pcsx2", "prepare-state", "a.p2s", "b.p2s"): ("pcsx2", "states"),
        ("progress", "ghidra", "--check"): ("ghidra", None),
        ("repo", "review", "PR.md", "--worktree", "w"): ("legacy", "pr-review"),
    }
    for old, (group, command) in cases.items():
        args = _build_parser().parse_args(rewrite(list(old))[0])
        assert args.group == group
        if command is not None:
            assert args.command == command
