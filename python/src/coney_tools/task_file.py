# SPDX-License-Identifier: GPL-3.0-or-later
"""The task file: `scratch/<task>/TASK.md`, written by `task create`, read by the tools after it and by the engineer.

A small YAML block between `---` lines holds what the tools read (`task`, `issues`, `kind`, `claimant`, `claimed_at`,
`scope`, `terms`); the sections below it are prose the coordinator prunes. The section names are fixed so a tool and a
person find the same thing in the same place (docs/guides/issues.md#task-files).
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from datetime import datetime

import yaml

from coney_tools.config import ConfigError
from coney_tools.conventions import Tasks
from coney_tools.issue_claim_model import iso_time, parse_time

#: The sections of a task file, in order.
SECTIONS = (
    "Issues",
    "Goal",
    "Done",
    "Scope",
    "Where to look",
    "Searched, not related",
    "Expected tests",
    "Decision points",
    "Related PRs and open questions",
)
#: What "Expected tests" says for work that has no test that fails first.
NO_FAILING_TEST = "no fails-first test: this task changes documentation or tools, not the game."
_HEADING = re.compile(r"^## (?P<name>.+?)\s*$", re.MULTILINE)
_KEYS = ("task", "issues", "kind", "claimant", "claimed_at", "scope", "terms")


class TaskFileError(ConfigError):
    """A task file that is missing a part or holds a wrong one."""


@dataclass(frozen=True)
class TaskFile:
    """A task file as data."""

    task: str
    issues: tuple[int, ...]
    kind: str
    claimant: str
    claimed_at: datetime
    scope: tuple[str, ...] = ()
    terms: tuple[str, ...] = ()
    #: The text of each section, by name, in file order.
    sections: dict[str, str] = field(default_factory=dict)


def render_task_file(task: TaskFile) -> str:
    """The text of a task file: the YAML block, the title, then every section in order (missing ones empty)."""
    front = {
        "task": task.task,
        "issues": list(task.issues),
        "kind": task.kind,
        "claimant": task.claimant,
        "claimed_at": iso_time(task.claimed_at),
        "scope": list(task.scope),
        "terms": list(task.terms),
    }
    block = yaml.safe_dump(front, default_flow_style=None, sort_keys=False, allow_unicode=True, width=1000).strip()
    lines = ["---", block, "---", f"# Task {task.task}", ""]
    for name in SECTIONS:
        lines += [f"## {name}", "", task.sections.get(name, "").strip() or "(nothing yet)", ""]
    return "\n".join(lines)


def parse_task_file(text: str, tasks: Tasks) -> TaskFile:
    """The task file in `text`; TaskFileError names the first problem (a missing part, an unknown kind)."""
    text = text.replace("\r\n", "\n")
    parts = text.split("\n---\n", 1)
    if not text.startswith("---\n") or len(parts) != 2:
        raise TaskFileError("the task file does not start with a --- block")
    try:
        front = yaml.safe_load(parts[0][4:])
    except yaml.YAMLError as error:
        raise TaskFileError(f"the --- block is not valid YAML ({error})") from error
    if not isinstance(front, dict):
        raise TaskFileError("the --- block is not a mapping")
    for key in _KEYS:
        if key not in front:
            raise TaskFileError(f"the --- block has no '{key}'")
    kind = str(front["kind"])
    if kind not in tasks.kinds:
        raise TaskFileError(f"the kind '{kind}' is not one of {', '.join(tasks.kinds)}")
    try:
        issues = tuple(int(item) for item in front["issues"])
        claimed_at = parse_time(str(front["claimed_at"]))
    except (ValueError, TypeError) as error:
        raise TaskFileError(f"the --- block holds a wrong value ({error})") from error
    if not issues:
        raise TaskFileError("the task covers no issue")
    body = parts[1]
    headings = list(_HEADING.finditer(body))
    sections: dict[str, str] = {}
    for index, heading in enumerate(headings):
        end = headings[index + 1].start() if index + 1 < len(headings) else len(body)
        sections[heading.group("name")] = body[heading.end() : end].strip()
    for name in SECTIONS:
        if name not in sections:
            raise TaskFileError(f"the section '## {name}' is missing")
    if list(sections)[: len(SECTIONS)] != list(SECTIONS):
        raise TaskFileError("the sections are out of order")
    return TaskFile(
        task=str(front["task"]),
        issues=issues,
        kind=kind,
        claimant=str(front["claimant"]),
        claimed_at=claimed_at,
        scope=tuple(str(item) for item in front["scope"] or ()),
        terms=tuple(str(item) for item in front["terms"] or ()),
        sections=sections,
    )


def quote_block(text: str) -> str:
    """Text from an issue, quoted line by line (`> `), so none of its lines can start a section of the task file."""
    return "\n".join(f"> {line}".rstrip() for line in text.replace("\r\n", "\n").split("\n"))


def slug(title: str) -> str:
    """A task id's words from an issue title: `gameplay: Fix the fight goal` is `fix-the-fight-goal`, 40 characters
    at most, cut at a word."""
    text = title.split(": ", 1)[1] if ": " in title else title
    words = re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")
    if len(words) <= 40:
        return words
    return words[:40].rsplit("-", 1)[0] or words[:40]
