# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared by the Conclave tests: made-up task folders, stamp logs, worktrees, claim cards and `gh` answers."""

import json
import os
from collections.abc import Callable
from datetime import UTC, datetime, timedelta
from pathlib import Path
from typing import Any

from conventions_support import REPO

from coney_tools import stamps
from coney_tools.conclave_snapshot import Sources
from coney_tools.conflict_work import Trial
from coney_tools.conventions import Conventions, load_conventions
from coney_tools.coord_cli import Sources as CoordSources
from coney_tools.issue_claim_model import Card
from coney_tools.pr_ready import Readiness
from coney_tools.task_file import TaskFile, render_task_file
from coney_tools.unit import Unit

CONVENTIONS = load_conventions(REPO)
NOW = datetime(2026, 10, 23, 14, 0, tzinfo=UTC)
HASH = "a" * 40


def ago(**delta: float) -> datetime:
    """A moment before NOW."""
    return NOW - timedelta(**delta)


def iso(moment: datetime) -> str:
    """A moment as the tools print it."""
    return moment.strftime("%Y-%m-%dT%H:%M:%SZ")


class FakeGh:
    """`gh` answering from canned JSON by its leading arguments (first match wins); records every call."""

    def __init__(self, answers: dict[tuple[str, ...], Any]) -> None:
        self.answers = answers
        self.calls: list[list[str]] = []

    def __call__(self, args: list[str]) -> str:
        self.calls.append(args)
        for key, value in self.answers.items():
            if tuple(args[: len(key)]) == key:
                if isinstance(value, Exception):
                    raise value
                return json.dumps(value)
        raise OSError(f"unexpected gh {args}")


def make_task(
    scratch: Path, task: str, issues: tuple[int, ...] = (741,), scope: tuple[str, ...] = ("src/ai/",)
) -> Path:
    """`scratch/<task>/TASK.md` for a fix task claimed 2 days ago by example-maintainer."""
    folder = scratch / task
    folder.mkdir(parents=True)
    claimed_at = ago(days=2)
    text = render_task_file(
        TaskFile(
            task=task,
            issues=issues,
            kind="fix",
            claimant="example-maintainer",
            claimed_at=claimed_at,
            scope=scope,
            sections={"Goal": "Fix the uncuff steer.\nSecond line."},
        )
    )
    (folder / "TASK.md").write_text(text, encoding="utf-8")
    # the file's age is read from its mtime, so fix it relative to the fake clock
    os.utime(folder / "TASK.md", (ago(hours=30).timestamp(),) * 2)
    return folder


def moment_at(moment: datetime) -> Callable[[], datetime]:
    """A clock that always says `moment`."""
    return lambda: moment


def stamp_steps(
    scratch: Path, task: str, steps: list[tuple[str, str]], agent: str = "engineer-2", hours_ago: float = 5
) -> None:
    """Write one stamp per (step, result) into the task's log, one minute apart, the last `hours_ago` hours ago."""
    rules = CONVENTIONS.stamps
    moment = [NOW - timedelta(hours=hours_ago, minutes=len(steps) - index - 1) for index in range(len(steps))]
    for (step, result), when in zip(steps, moment, strict=True):
        stamps.append(
            scratch / task / "stamps",
            rules=rules,
            task=task,
            step=step,
            subject="0x00501000" if step in ("studied", "compared") else "",
            result=result,
            commit=HASH,
            covers={},
            detail={},
            who={"kind": "tool", "agent": {"id": agent, "type": "engineer", "model": "m"}, "source": "hook"},
            clock=moment_at(when),
        )


#: The change hash `ready_now` gives every unit.
CHANGE = "c" * 40


def ready_now(root: Path, unit: Unit, text: Path, conventions: Conventions, now: datetime) -> Readiness:
    """A made-up answer to "would pr open pass the proof": yes, for the change `CHANGE`. The tests that weigh the
    stamps themselves use real units (`test_conclave_ready`)."""
    return Readiness(True, "", CHANGE)


#: The commit `sources` says every worktree is at.
TIP = "d" * 40


def untried(root: Path, unit: Unit, text: Path, key: str, head: str, now: datetime) -> Trial:
    """A trial rebase that could not be made: tests not about conflicts leave a conflict as a `pr-stuck`."""
    return Trial("unknown", "no trial in this test")


def card(issue: int, claimant: str = "example-maintainer", hours_idle: float = 10, **changes: Any) -> Card:
    """A held claim card idle for `hours_idle` hours."""
    base: dict[str, Any] = {
        "issue": issue,
        "claimant": claimant,
        "claimed_at": ago(hours=hours_idle + 1),
        "last_activity": ago(hours=hours_idle),
        "scope": ("src/ai/",),
    }
    base.update(changes)
    return Card(**base)


HOOKS_TOML = """
require_role = false
helpers = ["Explore", "Plan", "fork"]

[roles.coordinator]
hint = "x"

[roles.reviewer]
aliases = ["engineer", "tooling"]
"""


def github_answers() -> dict[tuple[str, ...], Any]:
    """`gh` answers: the account, the issues, one open and one merged pull request, the ruleset."""
    return {
        ("api", "user"): {"login": "example-maintainer"},
        ("issue", "list"): [
            {
                "number": 741,
                "title": "gameplay: Fix the uncuff steer",
                "labels": [{"name": "S-in-progress"}, {"name": "P-high"}],
                "assignees": [{"login": "example-maintainer"}],
            },
            {"number": 790, "title": "core: Fix the crash", "labels": [{"name": "P-critical"}], "assignees": []},
            {"number": 5, "title": "docs: Add a page", "labels": [], "assignees": []},
        ],
        ("pr", "list", "--state", "merged"): [
            {"number": 799, "headRefName": "add-task-start", "mergedAt": iso(ago(minutes=4))}
        ],
        # every pull request of a branch, for the release queue: a unit is queued while its branch has none
        ("pr", "list", "--state", "all"): [
            {"number": 812, "headRefName": "fix-frame-812", "state": "OPEN", "closedAt": None},
            {"number": 799, "headRefName": "add-task-start", "state": "MERGED", "closedAt": iso(ago(minutes=4))},
        ],
        ("pr", "list", "--state", "open"): [
            {
                "number": 812,
                "headRefName": "fix-frame-812",
                "baseRefName": "main",
                "isDraft": False,
                "mergeable": "MERGEABLE",
                "autoMergeRequest": None,
                "headRefOid": "b" * 40,
                "statusCheckRollup": [
                    {
                        "__typename": "CheckRun",
                        "name": "build-ok",
                        "status": "COMPLETED",
                        "conclusion": "FAILURE",
                        "startedAt": iso(ago(hours=1)),
                        "completedAt": iso(ago(minutes=50)),
                    }
                ],
                "updatedAt": iso(ago(hours=1)),
            }
        ],
        ("api",): {
            "rules": [
                {"type": "required_status_checks", "parameters": {"required_status_checks": [{"context": "build-ok"}]}}
            ]
        },
    }


class Layout:
    """A made-up workspace: scratch folders, worktrees and the checkout with hooks.toml."""

    def __init__(self, root: Path) -> None:
        self.root = root
        self.scratch = root / "scratch"
        self.trees = root / "worktrees"
        self.checkout = root / "repos" / "coney"
        (self.checkout / ".claude" / "hooks").mkdir(parents=True)
        (self.checkout / ".claude" / "hooks" / "hooks.toml").write_text(HOOKS_TOML, encoding="utf-8")
        self.scratch.mkdir()

    def worktree(self, name: str) -> Path:
        """A worktree folder named `name`."""
        (self.trees / name).mkdir(parents=True, exist_ok=True)
        return self.trees / name


def make_layout(root: Path) -> Layout:
    """Two units: `fix-frame-812` (failed compare, building) and `fix-new` (no stamps), each with a worktree."""
    made = Layout(root)
    made.worktree("fix-frame-812")
    made.worktree("fix-new")
    make_task(made.scratch, "fix-frame-812", (741,), ("src/platform/frame.cpp", "src/ai/"))
    stamp_steps(
        made.scratch,
        "fix-frame-812",
        [("assigned", "pass"), ("claimed", "pass"), ("studied", "pass"), ("fails-first", "pass"), ("compared", "fail")],
    )
    make_task(made.scratch, "fix-new", (803,), ("src/ai/ai_goal.cpp",))
    return made


def sources(made: Layout, gh: FakeGh | None = None, plain: tuple[str, ...] = (), **changes: Any) -> Sources:
    """Sources over the made-up layout; `plain` names more worktrees of branches that have no task file."""
    trees = made.trees
    lanes = {
        "main": {
            "slots": [
                {
                    "slot": 1,
                    "stale": False,
                    "claim": {
                        "slot": 1,
                        "agent": "engineer-2",
                        "worktree": str(trees / "fix-frame-812"),
                        "preset": "dev",
                        "step": "build",
                        "pid": 4,
                        "started": iso(ago(minutes=3)),
                        "lane": "main",
                    },
                }
            ],
            "queue": [
                {
                    "position": 1,
                    "stale": False,
                    "name": "t1",
                    "agent": "x",
                    "worktree": "fix-new",
                    "preset": "dev",
                    "pid": 5,
                    "priority": False,
                    "time": 0.0,
                }
            ],
        }
    }
    coord: dict[str, Any] = {
        "main": made.checkout,
        "scratch": made.scratch,
        "now": lambda: NOW,
        "gh": gh or FakeGh({}),
        "worktrees": lambda: [
            (trees / "fix-frame-812", "fix-frame-812"),
            (trees / "fix-new", "fix-new"),
            *((trees / name, name) for name in plain),
        ],
        "remote_branches": lambda: set(),
        "pcsx2": lambda: [{"copy": "pcsx2", "state": "free", "claim": None, "running": False}],
        "builds": lambda: lanes,
        "build_result": lambda path: None,
        "health": lambda pulls, required, now: {
            "stuck": [
                {
                    "number": 812,
                    "branch": "fix-frame-812",
                    "cause": "checks-failed",
                    "kind": "code",
                    "detail": "build-ok (code)",
                    "who": "author",
                    "checks": ["build-ok"],
                }
            ],
            "other": [],
            "notices": [],
        },
    }
    base: dict[str, Any] = {
        "conventions": CONVENTIONS,
        "checkout": made.checkout,
        "coord": CoordSources(**coord),
        "free_bytes": lambda: 61 * 1024**3,
        "claims": lambda: [card(741), card(770, "contrib-a", hours_idle=50, scope=("assets/",))],
        "ready": ready_now,
        "trial": untried,
        "tip_of": lambda root: TIP,
    }
    base.update(changes)
    return Sources(**base)


def unit(report: dict[str, Any], task: str) -> dict[str, Any]:
    """One unit of the report by task."""
    found: dict[str, Any] = next(u for u in report["units"]["units"] if u["task"] == task)
    return found
