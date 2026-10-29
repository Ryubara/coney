# SPDX-License-Identifier: GPL-3.0-or-later
"""Every command a message of the tools tells the reader to run exists (#1094).

`task review` and `pr open` refuse with the command to run again, and an engineer follows it to the letter. A refusal
that names a command the line does not have (a renamed one, a typo) costs a round. The helpers are in
command_support.py; the refusals built from pieces (task_review, pr_proof) are held to the same rule in their own
tests, and this module scans every message the tools and the hooks can print.
"""

import ast
from pathlib import Path

import pytest
from command_support import command_problem, commands_in, groups, unknown_commands

SOURCES = Path(__file__).resolve().parents[2]
#: The code that prints messages: the tools and the hooks' scripts.
FOLDERS = (SOURCES / "python" / "src" / "coney_tools", SOURCES / ".claude" / "hooks")
#: Messages that name an old command on purpose: the table of renames and the pages about it.
OLD_NAMES_ON_PURPOSE = {"renames.py"}


def test_a_command_that_exists_is_found_and_accepted() -> None:
    text = 'run `orig fails-first --none "<reason>"` or `task review T --record`, then `build --agent`'
    assert commands_in(text) == ['orig fails-first --none "<reason>"', "task review T --record", "build --agent"]
    assert unknown_commands(text) == []


@pytest.mark.parametrize(
    ("text", "why"),
    [
        ("run `original fails-first`", "an old name"),
        ("run `check changed`", "an old name"),
        ("run `orig fail-first`", "no command `fail-first`"),
        ("run `task nothing`", "no command `nothing`"),
        ("run `task review --recorded`", "no option --recorded"),
    ],
)
def test_a_command_that_does_not_exist_is_reported(text: str, why: str) -> None:
    found = unknown_commands(text)
    assert len(found) == 1 and why in found[0], found


def test_a_span_that_is_not_a_command_is_left_alone() -> None:
    text = "see `xbox 0x000bd3c0`, `src/ai/brain.cpp`, `TODO:`, `the pairs` and `pr` or `docs check`"
    assert [c for c in commands_in(text)] == ["docs check"]
    assert command_problem("docs check") is None


def _spans_of(path: Path) -> list[tuple[int, str]]:
    """Every string a file holds, as (line, text): an f-string with a hole for each formatted value."""
    tree = ast.parse(path.read_text(encoding="utf-8"))
    found: list[tuple[int, str]] = []
    inside: set[int] = set()  # the pieces of an f-string are read whole, with it, not one by one
    for node in ast.walk(tree):
        if isinstance(node, ast.JoinedStr):
            parts = []
            for part in node.values:
                inside.add(id(part))
                parts.append(str(part.value) if isinstance(part, ast.Constant) else "{}")
            found.append((node.lineno, "".join(parts)))
        elif isinstance(node, ast.Constant) and isinstance(node.value, str) and id(node) not in inside:
            found.append((node.lineno, node.value))
    return found


def test_every_command_a_message_names_exists() -> None:
    problems: list[str] = []
    for folder in FOLDERS:
        for path in sorted(folder.glob("*.py")):
            if path.name in OLD_NAMES_ON_PURPOSE:
                continue
            for line, text in _spans_of(path):
                problems += [f"{path.name}:{line}: {problem}" for problem in unknown_commands(text)]
    assert problems == []


#: What a refusal's "redo" may be besides a command: the person who has to act.
PEOPLE = {"engineer", "coordinator", "none"}


def _redo_texts(path: Path) -> list[tuple[int, str]]:
    """The redo (third argument) of every `Refusal(...)` a file builds that is written out, with a hole for each
    formatted value; one computed by a call is not here (its pieces are `pr_proof._REDO`, tested below)."""
    tree = ast.parse(path.read_text(encoding="utf-8"))
    found: list[tuple[int, str]] = []
    for node in ast.walk(tree):
        if not (isinstance(node, ast.Call) and getattr(node.func, "id", getattr(node.func, "attr", "")) == "Refusal"):
            continue
        redo = node.args[2] if len(node.args) > 2 else next((k.value for k in node.keywords if k.arg == "redo"), None)
        if isinstance(redo, ast.Constant) and isinstance(redo.value, str):
            found.append((node.lineno, redo.value))
        elif isinstance(redo, ast.JoinedStr):
            found.append(
                (
                    node.lineno,
                    "".join(str(p.value) if isinstance(p, ast.Constant) else "0x00501000" for p in redo.values),
                )
            )
    return found


def _redo_problem(redo: str) -> str | None:
    """Why `redo` is neither a person nor a command that exists (a leading `engineer, then ` is read past)."""
    text = redo.removeprefix("engineer, then ")
    if text in PEOPLE:
        return None
    if text.split()[0] not in groups():
        return f"{redo!r}: neither a person nor a command of the command line"
    return command_problem(text)


def test_every_redo_a_refusal_gives_is_a_person_or_a_command_that_exists() -> None:
    problems: list[str] = []
    seen = 0
    for path in sorted(FOLDERS[0].glob("*.py")):
        for line, redo in _redo_texts(path):
            seen += 1
            if (problem := _redo_problem(redo)) is not None:
                problems.append(f"{path.name}:{line}: {problem}")
    assert seen > 30  # the scan reads the refusals of pr_proof, pr_text and pr_ready, not nothing
    assert problems == []


def test_the_command_to_redo_each_stage_exists() -> None:
    from coney_tools import pr_proof

    for step, redo in pr_proof._REDO.items():
        assert _redo_problem(redo.format(subject="0x00501000")) is None, (step, redo)
