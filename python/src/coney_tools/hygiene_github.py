# SPDX-License-Identifier: GPL-3.0-or-later
"""What the hygiene workflow reads and writes on GitHub, behind a small interface so tests use a fake.

`Remote` is the interface; `GhRemote` implements it with the `gh` command line (`gh api` for comments, `gh issue` for
the report issue). The only writes are to create or edit a comment and to create the report issue when it is missing;
nothing is ever deleted, and an issue's title, body, labels and state are never changed.
"""

from __future__ import annotations

import json
import re
from collections.abc import Iterable, Mapping
from typing import Any, Protocol

from coney_tools.audit_github import Runner, run_gh
from coney_tools.config import ConfigError
from coney_tools.hygiene_comments import Sticky

#: What a login in `hygiene.comments.author` may look like; it goes into a `gh --jq` filter, so nothing else is allowed.
_LOGIN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9-]*(\[bot\])?$")
#: What the first line of a comment (the marker) may look like, for the same reason.
_FIRST_LINE = re.compile(r"^<!-- [A-Za-z][A-Za-z0-9-]*(:report)? -->$")


class Remote(Protocol):
    """The operations the hygiene command needs from GitHub."""

    def stickies(self, first_line: str, author: str, number: int | None = None) -> dict[int, Sticky]:
        """The comments by `author` whose first line is `first_line`, by item; `number` limits it to one item."""
        ...

    def create_comment(self, number: int, body: str) -> None:
        """Add a comment to issue or pull request `number`."""
        ...

    def edit_comment(self, comment_id: int, body: str) -> None:
        """Replace the text of comment `comment_id`."""
        ...

    def find_report_issue(self, title: str) -> int | None:
        """The number of the open issue with exactly this title, or None."""
        ...

    def create_report_issue(self, title: str, body: str, labels: Iterable[str]) -> int:
        """Create the report issue and return its number."""
        ...


def pick_stickies(comments: Iterable[Mapping[str, Any]], first_line: str, author: str) -> dict[int, Sticky]:
    """Of GitHub's comments (`id`, `body`, `issue_url`, `user.login`), the workflow's own, by item number.

    A comment counts only when `author` wrote it and its first line is `first_line`; the oldest such comment of an item
    wins (a duplicate, if one ever appears, is left alone).
    """
    found: dict[int, Sticky] = {}
    for comment in comments:
        body = str(comment.get("body") or "")
        user = comment.get("user")
        login = user.get("login") if isinstance(user, dict) else None
        if login != author or body.split("\n", 1)[0].strip() != first_line:
            continue
        url = str(comment.get("issue_url") or "")
        tail = url.rsplit("/", 1)[-1]
        if not tail.isdigit():
            continue
        found.setdefault(int(tail), Sticky(int(comment["id"]), int(tail), body.replace("\r\n", "\n")))
    return found


class GhRemote:
    """GitHub through `gh`, for the repository `gh` finds from the checkout (or from the GH_REPO variable)."""

    def __init__(self, run: Runner = run_gh) -> None:
        self._run = run

    def stickies(self, first_line: str, author: str, number: int | None = None) -> dict[int, Sticky]:
        """The workflow's own comments with this first line; the filter runs in `gh`, so only those are downloaded."""
        if not _LOGIN.match(author) or not _FIRST_LINE.match(first_line):
            raise ConfigError("hygiene.comments: the author or the marker has a character that is not allowed")
        where = f"issues/{number}/comments" if number is not None else "issues/comments"
        select = f'.[] | select(.user.login == "{author}" and (.body | startswith("{first_line}")))'
        try:
            out = self._run(["api", f"repos/{{owner}}/{{repo}}/{where}", "--paginate", "--jq", select + " | tojson"])
            comments = [json.loads(line) for line in out.splitlines() if line.strip()]
        except (OSError, ValueError) as error:
            raise ConfigError(f"cannot read the comments with gh ({error})") from error
        return pick_stickies(comments, first_line, author)

    def create_comment(self, number: int, body: str) -> None:
        """Add a comment with `body`."""
        self._write(
            ["api", "--method", "POST", f"repos/{{owner}}/{{repo}}/issues/{number}/comments", "-f", f"body={body}"]
        )

    def edit_comment(self, comment_id: int, body: str) -> None:
        """Replace comment `comment_id`'s text with `body`."""
        self._write(
            ["api", "--method", "PATCH", f"repos/{{owner}}/{{repo}}/issues/comments/{comment_id}", "-f", f"body={body}"]
        )

    def find_report_issue(self, title: str) -> int | None:
        """The open issue whose title is exactly `title` (GitHub's search is fuzzy, so the title is compared here)."""
        try:
            out = self._run(
                [
                    "issue",
                    "list",
                    "--state",
                    "open",
                    "--search",
                    f'"{title}" in:title',
                    "--limit",
                    "50",
                    "--json",
                    "number,title",
                ]
            )
            issues = json.loads(out)
        except (OSError, ValueError) as error:
            raise ConfigError(f"cannot look for the report issue with gh ({error})") from error
        for item in issues:
            if item.get("title") == title:
                return int(item["number"])
        return None

    def create_report_issue(self, title: str, body: str, labels: Iterable[str]) -> int:
        """Create the issue and return its number (`gh issue create` prints its URL)."""
        arguments = ["issue", "create", "--title", title, "--body", body]
        for label in labels:
            arguments += ["--label", label]
        out = self._write(arguments).strip().splitlines()
        tail = out[-1].rsplit("/", 1)[-1] if out else ""
        if not tail.isdigit():
            raise ConfigError(f"gh issue create did not print an issue URL (got {out[-1:]!r})")
        return int(tail)

    def _write(self, arguments: list[str]) -> str:
        """Run a gh command that changes GitHub; ConfigError when it fails."""
        try:
            return self._run(arguments)
        except OSError as error:
            raise ConfigError(f"cannot write to GitHub with gh ({error})") from error
