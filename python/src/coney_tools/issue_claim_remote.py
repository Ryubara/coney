# SPDX-License-Identifier: GPL-3.0-or-later
"""What claiming reads and writes on GitHub, behind a small interface so every test uses a fake.

`Remote` is the interface; `GhRemote` implements it with the `gh` command line (`gh api`, one GraphQL query for the
pull requests that close an issue). The Action runs it with the Actions token, `task create` with the signed-in
account. Nothing here decides anything: the rules are in `issue_claim_rules.py` and the loop in
`issue_claim_engine.py` (docs/guides/issues.md#claiming).
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass
from datetime import datetime
from typing import Protocol

from coney_tools.audit_github import Runner, run_gh
from coney_tools.config import ConfigError
from coney_tools.issue_claim_model import parse_time

#: What a login or a label may look like before it goes into a gh argument or a path.
_NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._\[\] -]*$")
_COMMENT_URL = re.compile(r"#issuecomment-(\d+)\s*$")


@dataclass(frozen=True)
class IssueView:
    """An issue (or pull request) as claiming sees it."""

    number: int
    title: str
    #: `open` or `closed`.
    state: str
    #: GitHub's close reason (`completed`, `not_planned`) or None.
    state_reason: str | None
    is_pr: bool
    labels: tuple[str, ...]
    assignees: tuple[str, ...]
    body: str


@dataclass(frozen=True)
class Comment:
    """A comment on an issue, with who wrote it."""

    id: int
    issue: int
    login: str
    #: GitHub's account type: `User` or `Bot`.
    user_type: str
    #: GitHub's `author_association`: OWNER, MEMBER, COLLABORATOR, CONTRIBUTOR, NONE, ...
    association: str
    body: str
    created_at: datetime


@dataclass(frozen=True)
class LinkedPr:
    """A pull request that closes the issue (`Fixes #N`)."""

    number: int
    #: `open`, `closed` (not merged) or `merged`.
    state: str
    #: The date of the head commit, a sign of life when the push event was missed; None if unknown.
    head_date: datetime | None
    #: The login of the pull request's author ("" when GitHub does not say); only the claimant's (or a maintainer's)
    #: pull request counts for the claim.
    author: str = ""
    #: A draft is still being written: it is activity, but the clock does not wait for its review.
    draft: bool = False


@dataclass(frozen=True)
class Blocker:
    """One issue or pull request a task is blocked by."""

    number: int
    #: Whether it is finished: an issue closed by a merged pull request, or a merged pull request.
    done: bool
    #: What it is now, for the refusal: `open`, `closed without a merged pull request`, `merged`.
    why: str


class Remote(Protocol):
    """The operations claiming needs from GitHub."""

    def issue(self, number: int) -> IssueView:
        """The issue or pull request `number`."""
        ...

    def comments(self, number: int) -> list[Comment]:
        """Every comment of the issue, oldest first."""
        ...

    def all_cards(self) -> list[Comment]:
        """Every comment of the repository written by the bot that starts like a claim card."""
        ...

    def all_replies(self) -> list[Comment]:
        """Every comment of the repository written by the bot that starts like a refusal reply."""
        ...

    def recent_commands(self, since: datetime) -> list[Comment]:
        """The command-shaped comments of people, written or edited since `since`, in any issue."""
        ...

    def open_with_label(self, label: str) -> list[int]:
        """The open issues (not pull requests) that carry `label`."""
        ...

    def open_assigned(self) -> list[int]:
        """The open issues (not pull requests) that have an assignee."""
        ...

    def blocker(self, number: int) -> Blocker:
        """Whether `number` is finished, for a Blocked by check."""
        ...

    def linked_prs(self, number: int) -> list[LinkedPr]:
        """The pull requests that close issue `number`."""
        ...

    def assign(self, number: int, login: str) -> tuple[str, ...]:
        """Assign `login` and return the issue's assignees afterwards (GitHub drops one it cannot assign)."""
        ...

    def unassign(self, number: int, logins: tuple[str, ...]) -> None:
        """Remove assignees."""
        ...

    def add_label(self, number: int, label: str) -> None:
        """Add a label."""
        ...

    def remove_label(self, number: int, label: str) -> None:
        """Remove a label (nothing if it is not there)."""
        ...

    def create_comment(self, number: int, body: str) -> int:
        """Add a comment as the signed-in account (the bot, in the Action) and return its id."""
        ...

    def edit_comment(self, comment_id: int, body: str) -> None:
        """Replace the text of a comment."""
        ...

    def delete_comment(self, comment_id: int) -> bool:
        """Delete a comment; False when GitHub refused (the caller carries on)."""
        ...

    def post_command(self, number: int, body: str) -> int:
        """Post a command comment as the signed-in person and return its id."""
        ...

    def current_user(self) -> str:
        """The login of the signed-in account."""
        ...

    def permission(self, login: str) -> str | None:
        """`login`'s permission here (`admin`, `write`, `read`, `none`), or None when GitHub will not say."""
        ...


def _safe(value: str, what: str) -> str:
    """`value` when it is fit for a gh argument; ConfigError otherwise."""
    if not _NAME.match(value):
        raise ConfigError(f"{what} {value!r} has a character that is not allowed")
    return value


def _comment_from(item: dict[str, object], issue: int | None = None) -> Comment:
    """A `Comment` from one item of GitHub's comments listing."""
    user = item.get("user")
    login = str(user.get("login", "")) if isinstance(user, dict) else ""
    kind = str(user.get("type", "")) if isinstance(user, dict) else ""
    number = issue
    if number is None:
        tail = str(item.get("issue_url", "")).rsplit("/", 1)[-1]
        number = int(tail) if tail.isdigit() else 0
    return Comment(
        id=int(str(item["id"])),
        issue=number,
        login=login,
        user_type=kind,
        association=str(item.get("author_association", "NONE")),
        body=str(item.get("body") or "").replace("\r\n", "\n"),
        created_at=parse_time(str(item["created_at"])),
    )


class GhRemote:
    """GitHub through `gh`, for the repository `gh` finds from the checkout (or from the GH_REPO variable)."""

    def __init__(self, bot_login: str, card_marker: str, reply_marker: str, run: Runner = run_gh) -> None:
        self._run = run
        self._bot = _safe(bot_login, "the bot login")
        self._card_first = f"<!-- {card_marker} -->"
        self._reply_first = f"<!-- {reply_marker}:"

    # -- reading

    def _json(self, arguments: list[str]) -> object:
        """Run a gh command and parse its output as JSON; ConfigError when it fails."""
        try:
            return json.loads(self._run(arguments))
        except (OSError, ValueError) as error:
            raise ConfigError(f"cannot read from GitHub with gh ({error})") from error

    def _lines(self, arguments: list[str]) -> list[dict[str, object]]:
        """Run a paginated gh command whose `--jq` prints one JSON object per line."""
        try:
            out = self._run(arguments)
            return [json.loads(line) for line in out.splitlines() if line.strip()]
        except (OSError, ValueError) as error:
            raise ConfigError(f"cannot read from GitHub with gh ({error})") from error

    def issue(self, number: int) -> IssueView:
        """The issue or pull request `number`."""
        data = self._json(["api", f"repos/{{owner}}/{{repo}}/issues/{number}"])
        if not isinstance(data, dict):
            raise ConfigError(f"gh returned something other than an issue for #{number}")
        labels = data.get("labels") or []
        assignees = data.get("assignees") or []
        return IssueView(
            number=number,
            title=str(data.get("title") or ""),
            state=str(data.get("state") or "open"),
            state_reason=str(data["state_reason"]) if data.get("state_reason") else None,
            is_pr="pull_request" in data,
            labels=tuple(str(item["name"]) if isinstance(item, dict) else str(item) for item in labels),
            assignees=tuple(str(item["login"]) for item in assignees if isinstance(item, dict)),
            body=str(data.get("body") or "").replace("\r\n", "\n"),
        )

    def comments(self, number: int) -> list[Comment]:
        """Every comment of the issue, oldest first."""
        items = self._lines(
            ["api", f"repos/{{owner}}/{{repo}}/issues/{number}/comments", "--paginate", "--jq", ".[] | tojson"]
        )
        return [_comment_from(item, number) for item in items]

    def _bot_comments(self, prefix: str) -> list[Comment]:
        """The bot's comments of the whole repository whose text starts with `prefix`."""
        select = f'.[] | select(.user.login == "{self._bot}" and (.body | startswith("{prefix}"))) | tojson'
        path = "repos/{owner}/{repo}/issues/comments?per_page=100"
        items = self._lines(["api", path, "--paginate", "--jq", select])
        return [_comment_from(item) for item in items]

    def all_cards(self) -> list[Comment]:
        """Every claim card of the repository."""
        return self._bot_comments(self._card_first)

    def all_replies(self) -> list[Comment]:
        """Every refusal reply of the repository."""
        return self._bot_comments(self._reply_first)

    def recent_commands(self, since: datetime) -> list[Comment]:
        """The command-shaped comments of people updated since `since`."""
        stamp = since.strftime("%Y-%m-%dT%H:%M:%SZ")
        select = (
            '.[] | select(.user.type != "Bot" and (.body | test("^\\\\s*/(claim|unclaim|progress)(\\\\s|$)"))) | tojson'
        )
        path = f"repos/{{owner}}/{{repo}}/issues/comments?since={stamp}&per_page=100"
        return [_comment_from(item) for item in self._lines(["api", path, "--paginate", "--jq", select])]

    def _numbers(self, query: str) -> list[int]:
        """The numbers of the issues (not pull requests) a listing query returns."""
        select = '.[] | select(has("pull_request") | not) | .number'
        path = f"repos/{{owner}}/{{repo}}/issues?state=open&per_page=100&{query}"
        try:
            out = self._run(["api", path, "--paginate", "--jq", select])
        except OSError as error:
            raise ConfigError(f"cannot read from GitHub with gh ({error})") from error
        return [int(line) for line in out.split() if line.isdigit()]

    def open_with_label(self, label: str) -> list[int]:
        """The open issues that carry `label`."""
        return self._numbers(f"labels={_safe(label, 'the label')}")

    def open_assigned(self) -> list[int]:
        """The open issues that have an assignee."""
        return self._numbers("assignee=*")

    def blocker(self, number: int) -> Blocker:
        """Finished means an issue closed as completed by a merged pull request, or a merged pull request."""
        item = self.issue(number)
        if item.is_pr:
            data = self._json(["api", f"repos/{{owner}}/{{repo}}/pulls/{number}"])
            merged = isinstance(data, dict) and bool(data.get("merged_at"))
            return Blocker(number, merged, "merged" if merged else f"a pull request that is {item.state}, not merged")
        if item.state != "closed":
            return Blocker(number, False, "open")
        if item.state_reason == "not_planned":
            return Blocker(number, False, "closed as not planned")
        merged = any(pr.state == "merged" for pr in self.linked_prs(number))
        return Blocker(
            number, merged, "closed by a merged pull request" if merged else "closed without a merged pull request"
        )

    def linked_prs(self, number: int) -> list[LinkedPr]:
        """The pull requests that close issue `number`, with the date of each head commit."""
        query = (
            "query($owner:String!,$name:String!,$number:Int!){repository(owner:$owner,name:$name){issue(number:$number){"
            "closedByPullRequestsReferences(first:10,includeClosedPrs:true){nodes{number state merged isDraft "
            "author{login} commits(last:1){nodes{commit{committedDate}}}}}}}}"
        )
        data = self._json(
            [
                "api",
                "graphql",
                "-F",
                "owner={owner}",
                "-F",
                "name={repo}",
                "-F",
                f"number={number}",
                "-f",
                f"query={query}",
            ]
        )
        try:
            issue = data["data"]["repository"]["issue"]  # type: ignore[index]
            nodes = issue["closedByPullRequestsReferences"]["nodes"]
        except (KeyError, TypeError):
            return []
        found: list[LinkedPr] = []
        for node in nodes or []:
            state = "merged" if node.get("merged") else str(node.get("state", "OPEN")).lower()
            commits = (node.get("commits") or {}).get("nodes") or []
            date = commits[0]["commit"].get("committedDate") if commits else None
            author = (node.get("author") or {}).get("login", "")
            draft = bool(node.get("isDraft"))
            found.append(LinkedPr(int(node["number"]), state, parse_time(date) if date else None, str(author), draft))
        return found

    # -- writing

    def _write(self, arguments: list[str]) -> str:
        """Run a gh command that changes GitHub; ConfigError when it fails."""
        try:
            return self._run(arguments)
        except OSError as error:
            raise ConfigError(f"cannot write to GitHub with gh ({error})") from error

    def assign(self, number: int, login: str) -> tuple[str, ...]:
        """Assign `login` and return the assignees GitHub reports."""
        out = self._write(
            [
                "api",
                "--method",
                "POST",
                f"repos/{{owner}}/{{repo}}/issues/{number}/assignees",
                "-f",
                f"assignees[]={_safe(login, 'the login')}",
            ]
        )
        try:
            return tuple(str(item["login"]) for item in json.loads(out).get("assignees", []))
        except (ValueError, AttributeError, KeyError, TypeError):
            return ()

    def unassign(self, number: int, logins: tuple[str, ...]) -> None:
        """Remove the assignees."""
        if not logins:
            return
        command = ["api", "--method", "DELETE", f"repos/{{owner}}/{{repo}}/issues/{number}/assignees"]
        for login in logins:
            command += ["-f", f"assignees[]={_safe(login, 'the login')}"]
        self._write(command)

    def add_label(self, number: int, label: str) -> None:
        """Add `label`."""
        self._write(
            [
                "api",
                "--method",
                "POST",
                f"repos/{{owner}}/{{repo}}/issues/{number}/labels",
                "-f",
                f"labels[]={_safe(label, 'the label')}",
            ]
        )

    def remove_label(self, number: int, label: str) -> None:
        """Remove `label`; a label that is not on the issue is not an error."""
        try:
            self._run(
                [
                    "api",
                    "--method",
                    "DELETE",
                    f"repos/{{owner}}/{{repo}}/issues/{number}/labels/{_safe(label, 'the label')}",
                ]
            )
        except OSError:
            return

    def create_comment(self, number: int, body: str) -> int:
        """Add a comment and return its id."""
        out = self._write(
            [
                "api",
                "--method",
                "POST",
                f"repos/{{owner}}/{{repo}}/issues/{number}/comments",
                "-f",
                f"body={body}",
                "--jq",
                ".id",
            ]
        )
        if not out.strip().isdigit():
            raise ConfigError(f"gh did not return the id of the new comment on #{number}")
        return int(out.strip())

    def edit_comment(self, comment_id: int, body: str) -> None:
        """Replace the comment's text."""
        self._write(
            ["api", "--method", "PATCH", f"repos/{{owner}}/{{repo}}/issues/comments/{comment_id}", "-f", f"body={body}"]
        )

    def delete_comment(self, comment_id: int) -> bool:
        """Delete the comment; False when GitHub refuses (a token that may not delete another's comment)."""
        try:
            self._run(["api", "--method", "DELETE", f"repos/{{owner}}/{{repo}}/issues/comments/{comment_id}"])
        except OSError:
            return False
        return True

    def post_command(self, number: int, body: str) -> int:
        """Post `body` with `gh issue comment` (`task create`'s `/claim`) and return the new comment's id."""
        out = self._write(["issue", "comment", str(number), "--body", body]).strip()
        found = _COMMENT_URL.search(out)
        if found is None:
            raise ConfigError(f"gh issue comment did not print the new comment's address (got {out[-80:]!r})")
        return int(found.group(1))

    def current_user(self) -> str:
        """The login of the account gh is signed in as."""
        out = self._write(["api", "user", "--jq", ".login"]).strip()
        return _safe(out, "the signed-in login")

    def permission(self, login: str) -> str | None:
        """`login`'s permission here; `none` when GitHub answers not found, None when it will not say."""
        try:
            out = self._run(
                [
                    "api",
                    f"repos/{{owner}}/{{repo}}/collaborators/{_safe(login, 'the login')}/permission",
                    "--jq",
                    ".permission",
                ]
            )
        except OSError as error:
            return "none" if "404" in str(error) or "Not Found" in str(error) else None
        return out.strip() or None
