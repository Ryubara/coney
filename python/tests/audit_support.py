# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared by the audit tests: made-up GitHub data in the shape `gh` returns, with no network."""

from datetime import UTC, datetime, timedelta
from typing import Any

from conventions_support import REPO

from coney_tools.audit import Problem
from coney_tools.audit_github import GithubContext, Snapshot, parse_snapshot
from coney_tools.conventions import Conventions, load_conventions

#: The moment every made-up snapshot was taken.
TAKEN = datetime(2026, 10, 11, 12, 0, tzinfo=UTC)
CONVENTIONS = load_conventions(REPO)
#: The real labels, as GitHub returns them.
REAL_LABELS = [
    {"name": label.name, "color": label.color, "description": label.description} for label in CONVENTIONS.labels.list
]


def stamp(days_ago: float = 1.0) -> str:
    """A GitHub timestamp `days_ago` days before the snapshot was taken."""
    return (TAKEN - timedelta(days=days_ago)).strftime("%Y-%m-%dT%H:%M:%SZ")


def task_body(
    summary: str = "Nothing checks the repository as a whole.",
    area: str = "A-tools",
    done: str = "A command reports every gap.",
    screenshots: str = "Screenshots: none (repository tooling)",
    extra: str = "",
) -> str:
    """An issue body in the task form's shape, the way `gh issue create` writes it (Screenshots at level two)."""
    return (
        f"### Summary\n\n{summary}\n\n### Area\n\n{area}\n\n### Research page\n\nNone.\n\n"
        f'### What "done" means\n\n{done}\n\n{extra}## Screenshots\n\n{screenshots}\n'
    )


def bug_body(frequency: str = "Always", log: str = "") -> str:
    """An issue body in the bug form's shape."""
    return (
        "### Coney version or commit\n\nabc123\n\n### How did you start the game?\n\nStory\n\n"
        "### Steps to reproduce\n\n1. Start\n\n### What happens\n\nIt breaks.\n\n"
        f"### How often does it happen?\n\n{frequency}\n\n### Screenshots\n\nScreenshots: none (a crash log only)\n\n"
        f"{log}### No game files\n\n- [x] I have not attached game files\n"
    )


def issue(
    number: int,
    *,
    title: str = "tools: Add a repository check",
    body: str | None = None,
    labels: tuple[str, ...] = ("task", "A-tools", "P-medium", "S-in-progress"),
    state: str = "OPEN",
    reason: str = "",
    created: float = 1.0,
    updated: float = 1.0,
) -> dict[str, Any]:
    """An issue as `gh issue list --json` returns it; the defaults describe a clean one."""
    return {
        "number": number,
        "title": title,
        "body": task_body() if body is None else body,
        "state": state,
        "stateReason": reason,
        "labels": [{"name": name} for name in labels],
        "createdAt": stamp(created),
        "updatedAt": stamp(updated),
        "url": f"https://example.invalid/issues/{number}",
    }


PR_BODY = (
    "## Summary\n\nAdds a check.\n\n## Checklist\n\nDone:\n\n- [x] the check\n\n"
    "## Testing\n\n- 3 tests passed\n\n## Screenshots\n\nScreenshots: none (tooling)\n\n"
    "## AI use\n\n- **Commits written by:** a person\n- **Model:** n/a\n"
)


def pull(
    number: int,
    *,
    title: str = "tools: Add a check",
    body: str | None = None,
    state: str = "OPEN",
    base: str = "main",
    head: str | None = None,
    draft: bool = False,
    auto_merge: bool = False,
    closes: tuple[int, ...] = (),
    updated: float = 1.0,
    merged: str | None = None,
) -> dict[str, Any]:
    """A pull request as `gh pr list --json` returns it; the defaults describe a clean open one."""
    return {
        "number": number,
        "title": title,
        "body": PR_BODY if body is None else body,
        "state": state,
        "labels": [],
        "createdAt": stamp(updated),
        "updatedAt": stamp(updated),
        "mergedAt": (merged or stamp(updated)) if state == "MERGED" else None,
        "baseRefName": base,
        "headRefName": head or f"branch-{number}",
        "isDraft": draft,
        "autoMergeRequest": {"enabledAt": stamp()} if auto_merge else None,
        "closingIssuesReferences": [{"number": n} for n in closes],
        "url": f"https://example.invalid/pull/{number}",
    }


def raw_snapshot(
    issues: list[dict[str, Any]] | None = None,
    pulls: list[dict[str, Any]] | None = None,
    labels: list[dict[str, Any]] | None = None,
    settings: dict[str, Any] | None = None,
    rulesets: list[dict[str, Any]] | None = None,
) -> dict[str, Any]:
    """The raw snapshot JSON; the labels default to the real ones and the settings and rulesets to the real rules."""
    return {
        "repo": "someone/coney",
        "taken": TAKEN.strftime("%Y-%m-%dT%H:%M:%SZ"),
        "default_branch": "main",
        "issues": issues or [],
        "pulls": pulls or [],
        "labels": REAL_LABELS if labels is None else labels,
        "settings": settings,
        "rulesets": rulesets,
        "notices": [],
    }


def snapshot(**kwargs: Any) -> Snapshot:
    """A parsed snapshot of `raw_snapshot(**kwargs)`."""
    return parse_snapshot(raw_snapshot(**kwargs))


class FakeTree:
    """The files of main, made up: pages mapped to their anchors."""

    def __init__(self, pages: dict[str, frozenset[str]]) -> None:
        self.pages = pages

    def has(self, path: str) -> bool:
        """Whether `path` is a page or a folder holding one."""
        return path in self.pages or any(name.startswith(path.rstrip("/") + "/") for name in self.pages)

    def anchors(self, path: str) -> frozenset[str] | None:
        """The anchors of a page."""
        return self.pages.get(path)


def context(conventions: Conventions | None = None, tree: FakeTree | None = None, **kwargs: Any) -> GithubContext:
    """A context over `snapshot(**kwargs)`."""
    return GithubContext(conventions or CONVENTIONS, snapshot(**kwargs), tree)


def found(function: Any, ctx: Any) -> list[Problem]:
    """What a check function yields, as a list."""
    return list(function(ctx))


def items(problems: list[Problem]) -> list[str]:
    """The items of some problems."""
    return [problem.item for problem in problems]
