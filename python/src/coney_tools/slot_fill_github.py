# SPDX-License-Identifier: GPL-3.0-or-later
"""The GitHub reads of `task slots --fill-check` (`slot_fill.Sources` over `gh`), bounded and briefly cached.

The check runs at the end of every coordinator turn (the Stop hook), so it has to be quick and must never hang: each
`gh` call is killed after `tasks.start.fill.github_seconds`, the reads together may take `budget_seconds`, and the
first read that fails ends the reading (every later one fails at once). A normal run makes a handful of calls, one
batched GraphQL query for each kind of read (the claim cards of every assigned issue, the states of every blocker the
ready issues name, the pull requests), never one call for each issue: the budget is for a GitHub that is slow, not
for the number of issues. The answers are kept in the scratch folder for `cache_seconds`, and dropped as soon as a task
file or unit record changed since (a claim just made or a unit just started must show). Every failure is a
`ConfigError`, which the check turns into a pass with a warning.
"""

from __future__ import annotations

import json
import subprocess
import time
from collections.abc import Callable, Collection
from datetime import datetime
from pathlib import Path
from typing import Any

from coney_tools import pr_health, task_slots
from coney_tools.audit_github import Runner
from coney_tools.config import ConfigError
from coney_tools.conventions import Conventions
from coney_tools.issue_claim_model import Card, blocked_by_numbers, iso_time, parse_card, parse_time, render_card
from coney_tools.issue_claim_pass import latest_held, stored_cards
from coney_tools.issue_claim_remote import Comment, GhRemote, Remote
from coney_tools.issue_form import section_heading, section_text
from coney_tools.slot_fill import FailedPull, IssueInfo

#: The cache file in the scratch folder.
CACHE_FILE = "slot-fill-cache.json"
#: Issues and pull requests read at most.
LIMIT = 200
#: The fields read of a pull request: `pr_health`'s, the head commit and the issues it closes.
PULL_FIELDS = f"{pr_health.PULL_FIELDS},headRefOid,closingIssuesReferences"
#: Blocker numbers asked in one GraphQL query (each is an alias of the query, and a query has a cost limit).
BLOCKERS_PER_QUERY = 100
#: Assigned issues, and comments of one issue, read in one query (GitHub's page size); more is read another way or
#: refused, never silently cut.
PAGE = 100

#: The claim cards: the open issues assigned to this account (`search` takes `@me`) and each one's first comments.
CARDS_QUERY = (
    "query($search:String!){search(type:ISSUE,first:100,query:$search){pageInfo{hasNextPage} nodes{... on Issue{number "
    "comments(first:100){pageInfo{hasNextPage} nodes{databaseId author{login __typename} authorAssociation createdAt "
    "body}}}}}}"
)

#: The finished-ness of a blocker: an issue's state and the pull requests that close it, or a pull request's merge.
BLOCKER_FIELDS = (
    "__typename ... on Issue{state stateReason closedByPullRequestsReferences(first:10,includeClosedPrs:true)"
    "{nodes{merged baseRepository{nameWithOwner}}}} ... on PullRequest{merged}"
)


class GhFailed(OSError):
    """A `gh` call that exited with an error; `output` is what it printed (GraphQL prints a partial answer)."""

    def __init__(self, message: str, output: str = "") -> None:
        super().__init__(message)
        self.output = output


class BudgetSpent(OSError):
    """The reads together have taken longer than the budget: no further `gh` call starts."""


def bounded_runner(seconds: float) -> Runner:
    """A `gh` runner (read only) that kills a call after `seconds`; OSError when it fails or takes longer."""

    def run(arguments: list[str]) -> str:
        """Run `gh` with the arguments and return its output."""
        try:
            done = subprocess.run(
                ["gh", *arguments], capture_output=True, text=True, encoding="utf-8", timeout=seconds, check=False
            )
        except (OSError, subprocess.SubprocessError) as error:
            raise OSError(f"gh could not run: {error}") from error
        if done.returncode != 0:
            raise GhFailed(
                f"gh {' '.join(arguments[:2])} failed: {done.stderr.strip() or done.stdout.strip()}", done.stdout
            )
        return done.stdout

    return run


def newest_change(scratch: Path, conventions: Conventions) -> float:
    """The latest modification time (seconds) of a task file or unit record in the scratch folder; 0 when there is none.

    A cache older than that predates a claim or a start the check must see."""
    newest = 0.0
    if not scratch.is_dir():
        return newest
    for folder in scratch.iterdir():
        for name in (conventions.tasks.file, conventions.tasks.start.unit_file, task_slots.RESERVATION_FILE):
            try:
                newest = max(newest, (folder / name).stat().st_mtime)
            except OSError:
                continue
    return newest


def _only_not_found(error: GhFailed) -> bool:
    """Whether a failed GraphQL call is only about numbers GitHub does not know: its output is an answer with data and
    nothing wrong but `NOT_FOUND`, which the caller reads per number."""
    try:
        answer = json.loads(error.output)
    except ValueError:
        return False
    if not isinstance(answer, dict) or not isinstance(answer.get("data"), dict):
        return False
    problems = answer.get("errors") or [None]
    return all(isinstance(item, dict) and item.get("type") == "NOT_FOUND" for item in problems)


def _is_finished(node: Any, here: str) -> bool | None:
    """Whether a blocker is finished, from its GraphQL node (None: GitHub has no such number). A pull request is
    finished when merged. An issue is, when closed as completed (not "not planned") with a merged pull request of
    this repository (`here`; a pull request of another repository that names the issue does not count)."""
    if not isinstance(node, dict):
        return None
    if node.get("__typename") == "PullRequest":
        return bool(node.get("merged"))
    if node.get("state") != "CLOSED" or node.get("stateReason") == "NOT_PLANNED":
        return False
    for pull in (node.get("closedByPullRequestsReferences") or {}).get("nodes") or []:
        base = str((pull.get("baseRepository") or {}).get("nameWithOwner") or "")
        if pull.get("merged") and (not here or base.lower() == here):
            return True
    return False


class GhSources:
    """`slot_fill.Sources` over `gh`: the ready issues, this account's claims, blockers and failed pull requests."""

    def __init__(
        self,
        conventions: Conventions,
        scratch: Path,
        run: Runner,
        now: Callable[[], datetime],
        clock: Callable[[], float] = time.monotonic,
        changed: Callable[[], float] | None = None,
    ) -> None:
        self._conventions = conventions
        self._scratch = scratch
        self._run = run
        self._now = now
        self._clock = clock
        self._changed = changed or (lambda: newest_change(scratch, conventions))
        self._started: float | None = None
        self._stop = ""
        self._spent = False
        self._calls = 0
        #: The ready issues last read, whose blockers are read together; the states of blockers read so far
        #: (None: GitHub has no such number).
        self._ready: list[IssueInfo] = []
        self._states: dict[int, bool | None] = {}
        rules = conventions.claims
        self._remote: Remote = GhRemote(rules.bot_login, rules.card_marker, rules.reply_marker, self._gh)
        self._over = task_slots.pulls_over(self._remote)
        self._blocked_heading = section_heading(conventions, "blocked-by")

    # -- the bounded call

    def _gh(self, arguments: list[str], partial: Callable[[GhFailed], bool] | None = None) -> str:
        """One `gh` call within the budget; the first failure ends all reading.

        A failed call whose output `partial` accepts (GraphQL prints the answer it has, and exits with an error for a
        number it does not know) returns that output instead, and ends nothing."""
        fill = self._conventions.tasks.start.fill
        if self._started is None:
            self._started = self._clock()
        elif not self._stop and self._clock() - self._started >= fill.budget_seconds:
            self._spent = True
            plural = "" if self._calls == 1 else "s"
            self._stop = (
                f"the reads together took longer than the {fill.budget_seconds} s allowed for all of them "
                f"({self._calls} gh call{plural} made)"
            )
        if self._stop:
            raise BudgetSpent(self._stop) if self._spent else OSError(self._stop)
        self._calls += 1
        try:
            return self._run(arguments)
        except GhFailed as error:
            if partial is not None and partial(error):
                return error.output
            self._stop = f"an earlier read failed ({error})"
            raise
        except OSError as error:
            self._stop = f"an earlier read failed ({error})"
            raise

    def _failure(self, error: Exception) -> ConfigError:
        """A read that failed as a `ConfigError`: a spent budget is not a fault of `gh`, so it says only that."""
        if isinstance(error, ConfigError):
            return ConfigError(self._stop) if self._spent else error
        if isinstance(error, BudgetSpent):
            return ConfigError(str(error))
        return ConfigError(f"cannot read GitHub with gh ({error})")

    def _json(self, arguments: list[str]) -> Any:
        """A `gh` command's output as JSON; ConfigError when it cannot be read."""
        try:
            return json.loads(self._gh(arguments))
        except (OSError, ValueError) as error:
            raise self._failure(error) from error

    def _graphql(self, query: str, variables: dict[str, str], partial: Callable[[GhFailed], bool] | None = None) -> Any:
        """The `data` of a GraphQL query (typed variables: gh fills in `{owner}` and `{repo}`)."""
        arguments = ["api", "graphql"]
        for name, value in variables.items():
            arguments += ["-F", f"{name}={value}"]
        arguments += ["-f", f"query={query}"]
        try:
            answer = json.loads(self._gh(arguments, partial))
        except (OSError, ValueError) as error:
            raise self._failure(error) from error
        data = answer.get("data") if isinstance(answer, dict) else None
        if not isinstance(data, dict):
            raise ConfigError("gh api graphql did not return data")
        return data

    # -- the cache

    def _cached(self, key: str, load: Callable[[], Any], accept: Callable[[Any], bool] | None = None) -> Any:
        """`load()`, or the answer kept for `key` while it is younger than `cache_seconds` and no task file changed
        since (and `accept` takes it: the answer to a wider question may not hold the answer to this one). A cache that
        cannot be read or written costs only the read."""
        path = self._scratch / CACHE_FILE
        now = self._now()
        try:
            kept = json.loads(path.read_text(encoding="utf-8"))
            if not isinstance(kept, dict):
                kept = {}
        except (OSError, ValueError):
            kept = {}
        entry = kept.get(key)
        if isinstance(entry, dict):
            try:
                at = parse_time(str(entry["at"]))
                fresh = (now - at).total_seconds() < self._conventions.tasks.start.fill.cache_seconds
                if fresh and at.timestamp() >= self._changed() and (accept is None or accept(entry["data"])):
                    return entry["data"]
            except (KeyError, ValueError, TypeError):
                pass
        data = load()
        kept[key] = {"at": iso_time(now), "data": data}
        try:
            self._scratch.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps(kept), encoding="utf-8", newline="\n")
        except OSError:
            pass
        return data

    # -- Sources

    @staticmethod
    def _issues(data: Any) -> list[IssueInfo]:
        """The issues in `gh issue list --json number,title,labels,assignees,body`."""
        if not isinstance(data, list):
            raise ConfigError("gh issue list did not return a list")
        found: list[IssueInfo] = []
        for item in data:
            try:
                found.append(
                    IssueInfo(
                        number=int(item["number"]),
                        title=str(item.get("title") or ""),
                        labels=tuple(str(label["name"]) for label in item.get("labels") or []),
                        assignees=tuple(str(who["login"]) for who in item.get("assignees") or []),
                        body=str(item.get("body") or ""),
                    )
                )
            except (KeyError, TypeError, ValueError) as error:
                raise ConfigError(f"gh issue list gave an issue that cannot be read ({error})") from error
        return found

    def _trimmed(self, data: Any) -> Any:
        """The issues of a `gh issue list` answer with each body cut to its `Blocked by` section: the rest is never
        read, and the answer is kept in the cache."""
        heading = self._blocked_heading
        for item in data if isinstance(data, list) else []:
            if isinstance(item, dict):
                text = section_text(str(item.get("body") or ""), heading) if heading else None
                item["body"] = f"### {heading}" + chr(10) * 2 + text if text else ""
        return data

    def ready(self) -> list[IssueInfo]:
        """The open issues labelled `claims.claimable_status`."""
        status = self._conventions.claims.claimable_status
        fields = "number,title,labels,assignees,body"
        arguments = ["issue", "list", "--state", "open", "--label", status, "--limit", str(LIMIT), "--json", fields]
        self._ready = self._issues(self._cached("ready", lambda: self._trimmed(self._json(arguments))))
        return self._ready

    def claimed(self) -> list[Card]:
        """The held claim cards of the open issues assigned to this account.

        The cards are the claim Action's own record, and the only place that knows a claim is still held when its issue
        was paused (`S-on-hold`, `S-blocked`, `S-needs-info` take the working label off), so the limit is counted from
        them, never from labels. One query reads the assigned issues with their comments; an issue with more comments
        than one query holds is read on its own. The cards are kept in the cache as the text of their claim card."""
        rules = self._conventions.claims

        def load() -> list[str]:
            """The text of the held claim card of each assigned issue that has one."""
            data = self._graphql(CARDS_QUERY, {"search": "repo:{owner}/{repo} is:issue is:open assignee:@me"})
            found = data.get("search")
            if not isinstance(found, dict) or not isinstance(found.get("nodes"), list):
                raise ConfigError("gh api graphql did not return the assigned issues")
            if (found.get("pageInfo") or {}).get("hasNextPage"):
                raise ConfigError(f"more than {PAGE} issues are assigned to this account; they do not fit one query")
            texts: list[str] = []
            for node in found["nodes"]:
                if not isinstance(node, dict) or "number" not in node:
                    continue  # a search hit that is not an issue
                number = int(node["number"])
                comments = node.get("comments") or {}
                if (comments.get("pageInfo") or {}).get("hasNextPage"):
                    try:
                        card = task_slots.held_card(self._remote, number, self._conventions)
                    except ConfigError as error:
                        raise self._failure(error) from error
                else:
                    card = self._held(number, comments.get("nodes") or [])
                if card is not None:
                    texts.append(render_card(card, rules))
            return texts

        texts = self._cached("claimed", load)
        if not isinstance(texts, list):
            raise ConfigError("the kept claim cards cannot be read")
        cards = [parse_card(str(text), rules) for text in texts]
        return [card for card in cards if card is not None]

    def _held(self, number: int, nodes: list[Any]) -> Card | None:
        """The held claim card among an issue's comments as the query prints them."""
        comments: list[Comment] = []
        for node in nodes:
            try:
                author = node.get("author") or {}
                bot = author.get("__typename") == "Bot"
                comments.append(
                    Comment(
                        id=int(node["databaseId"]),
                        issue=number,
                        # REST prints a bot's login with the suffix the card rules compare against.
                        login=str(author.get("login") or "") + ("[bot]" if bot else ""),
                        user_type="Bot" if bot else str(author.get("__typename") or ""),
                        association=str(node.get("authorAssociation") or "NONE"),
                        body=str(node.get("body") or "").replace("\r\n", "\n"),
                        created_at=parse_time(str(node["createdAt"])),
                    )
                )
            except (AttributeError, KeyError, TypeError, ValueError) as error:
                raise ConfigError(f"a comment of #{number} cannot be read ({error})") from error
        found = latest_held(stored_cards(comments, self._conventions.claims))
        return found.card if found is not None else None

    def _named(self, issue: IssueInfo) -> list[int]:
        """The numbers in `issue`'s `Blocked by` section."""
        if self._blocked_heading is None:
            return []
        return blocked_by_numbers(section_text(issue.body, self._blocked_heading) or "")

    def blockers(self, issue: IssueInfo) -> list[int]:
        """The issues in `issue`'s `Blocked by` section that are not finished.

        The first call reads the states of the blockers of every ready issue last read, not only this one's, in as few
        queries as hold them: the check asks for the issues one at a time, and a read for each would be the budget."""
        named = self._named(issue)
        if not named:
            return []
        if not all(number in self._states for number in named):
            wanted = set(named)
            for other in self._ready:
                wanted.update(self._named(other))
            self._states.update(self._blocker_states(sorted(wanted)))
        missing = [number for number in named if self._states[number] is None]
        if missing:
            raise ConfigError(f"{', '.join(f'#{number}' for number in missing)} was not found on GitHub")
        return [number for number in named if not self._states[number]]

    def _blocker_states(self, numbers: list[int]) -> dict[int, bool | None]:
        """Whether each of `numbers` is finished (None: GitHub has no such issue or pull request), kept in the cache."""

        def load() -> dict[str, bool | None]:
            """The states, in queries of `BLOCKERS_PER_QUERY` numbers each."""
            found: dict[str, bool | None] = {}
            for start in range(0, len(numbers), BLOCKERS_PER_QUERY):
                states = self._finished(numbers[start : start + BLOCKERS_PER_QUERY])
                found.update({str(number): done for number, done in states.items()})
            return found

        def covers(kept: Any) -> bool:
            """Whether the kept states hold every number asked."""
            return isinstance(kept, dict) and all(str(number) in kept for number in numbers)

        data = self._cached("blockers", load, covers)
        if not covers(data):
            raise ConfigError("the kept blocker states cannot be read")
        return {number: data[str(number)] for number in numbers}

    def _finished(self, numbers: list[int]) -> dict[int, bool | None]:
        """One query for whether each of `numbers` is finished: a pull request that is merged, or an issue closed as
        completed by a merged pull request of this repository. None for a number GitHub does not know."""
        aliases = " ".join(f"b{n}:issueOrPullRequest(number:{n}){{{BLOCKER_FIELDS}}}" for n in numbers)
        query = f"query($owner:String!,$name:String!){{repository(owner:$owner,name:$name){{nameWithOwner {aliases}}}}}"
        data = self._graphql(query, {"owner": "{owner}", "name": "{repo}"}, _only_not_found)
        repository = data.get("repository")
        if not isinstance(repository, dict):
            raise ConfigError("gh api graphql did not return the repository")
        here = str(repository.get("nameWithOwner") or "").lower()
        return {number: _is_finished(repository.get(f"b{number}"), here) for number in numbers}

    def pulls_over(self, issues: Collection[int]) -> bool:
        """Whether the pull request that closes one of `issues` is merged or closed (`task_slots.pulls_over`); False
        when GitHub cannot be read, so a fix task is kept. Not cached: it is asked of the few fix tasks that have no
        unit record."""
        return self._over(issues)

    def failed_pulls(self) -> list[FailedPull]:
        """This account's open, non-draft pull requests with a failed required check."""
        default = self._conventions.repo.default_branch

        def load() -> dict[str, Any]:
            """The pull requests and the required checks of the default branch."""
            pulls = self._json(
                ["pr", "list", "--state", "open", "--author", "@me", "--limit", str(LIMIT), "--json", PULL_FIELDS]
            )
            rules = self._json(["api", f"repos/{{owner}}/{{repo}}/rules/branches/{default}"])
            return {"pulls": pulls, "required": list(pr_health.required_from_rules(rules))}

        data = self._cached("pulls", load)
        pulls = data.get("pulls") if isinstance(data, dict) else None
        if not isinstance(pulls, list):
            raise ConfigError("gh pr list did not return a list")
        required = [str(name) for name in data.get("required", [])]
        found: list[FailedPull] = []
        for pull in pulls:
            if not isinstance(pull, dict) or pull.get("isDraft"):
                continue
            failing = tuple(
                sorted(
                    check.name
                    for check in pr_health.checks_of(pull.get("statusCheckRollup"))
                    if check.name in required and check.state == "failed"
                )
            )
            if not failing:
                continue
            closes = tuple(
                int(ref["number"]) for ref in pull.get("closingIssuesReferences") or [] if isinstance(ref, dict)
            )
            found.append(
                FailedPull(
                    number=int(pull["number"]),
                    branch=str(pull.get("headRefName") or ""),
                    checks=failing,
                    closes=closes,
                    head=str(pull.get("headRefOid") or ""),
                )
            )
        return sorted(found, key=lambda item: item.number)
