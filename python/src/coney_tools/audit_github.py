# SPDX-License-Identifier: GPL-3.0-or-later
"""The `github` checks of `coney-tools repo audit`: issues, pull requests, labels and settings against the conventions.

A run reads GitHub once, with `gh` and read-only commands, into a `Snapshot` (the raw JSON `gh` returns, which
`--save-snapshot` writes and `--snapshot` reads back); the checks are pure functions of that snapshot, so the tests feed
them made-up JSON and never touch the network. Nothing here edits an issue, a pull request or a label: fixing a finding
is the coordinator's job (the /repo-hygiene skill), the audit only reports it.
"""

from __future__ import annotations

import json
import re
import subprocess
from collections.abc import Callable, Iterator, Mapping, Sequence
from dataclasses import dataclass, field
from datetime import UTC, datetime, timedelta
from pathlib import Path
from typing import Any, Protocol

from coney_tools import audit_text
from coney_tools.audit import Problem, check
from coney_tools.config import ConfigError
from coney_tools.conventions import Conventions, IssueForm
from coney_tools.repo_checks import (
    NUMBER_REFERENCE,
    check_body,
    check_title,
    remaining_items,
    screenshots_problems,
    uncited_remaining,
)

#: How many issues and pull requests one fetch asks for; a repository with more says so in a notice.
FETCH_LIMIT = 3000
#: A `Fixes #12` line of a description (GitHub's closing keywords).
_CLOSING = re.compile(r"^\s*(?:fix(?:es|ed)?|close[sd]?|resolve[sd]?)\s+#(?P<number>\d+)", re.IGNORECASE | re.MULTILINE)
#: The From: provenance line of an issue, which may be bold.
_PROVENANCE = re.compile(r"^[ \t]*\**From:", re.MULTILINE)


# --- the snapshot -------------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class Issue:
    """One issue as the audit needs it."""

    number: int
    title: str
    body: str
    open: bool
    #: Closed as not planned or as a duplicate: its unfinished items are moot.
    dropped: bool
    labels: tuple[str, ...]
    created: datetime
    updated: datetime
    url: str


@dataclass(frozen=True)
class Pull:
    """One pull request as the audit needs it."""

    number: int
    title: str
    body: str
    #: `open`, `closed` (without merging) or `merged`.
    state: str
    labels: tuple[str, ...]
    created: datetime
    updated: datetime
    base: str
    head: str
    draft: bool
    auto_merge: bool
    #: The issues GitHub links to it as the ones it closes.
    closes: tuple[int, ...]
    url: str


@dataclass(frozen=True)
class RemoteLabel:
    """A label as GitHub holds it."""

    name: str
    color: str
    description: str


@dataclass(frozen=True)
class Snapshot:
    """Everything the `github` checks read, taken at one moment."""

    repo: str
    taken: datetime
    default_branch: str
    issues: tuple[Issue, ...]
    pulls: tuple[Pull, ...]
    labels: tuple[RemoteLabel, ...]
    #: The repository settings (`gh api repos/...`), or None when they could not be read.
    settings: Mapping[str, Any] | None
    #: The repository's rulesets in full, or None when they could not be read (no admin rights, no network).
    rulesets: tuple[Mapping[str, Any], ...] | None
    #: Things the fetch could not read or may have cut short, for the report's notices.
    notices: tuple[str, ...] = ()
    _issue_index: dict[int, Issue] = field(default_factory=dict, repr=False, compare=False)
    _pull_index: dict[int, Pull] = field(default_factory=dict, repr=False, compare=False)

    def __post_init__(self) -> None:
        self._issue_index.update({issue.number: issue for issue in self.issues})
        self._pull_index.update({pull.number: pull for pull in self.pulls})

    def issue(self, number: int) -> Issue | None:
        """The issue numbered `number`, or None (it may be a pull request)."""
        return self._issue_index.get(number)

    def pull(self, number: int) -> Pull | None:
        """The pull request numbered `number`, or None."""
        return self._pull_index.get(number)

    def open_issues(self) -> list[Issue]:
        """The open issues, by number."""
        return sorted((issue for issue in self.issues if issue.open), key=lambda issue: issue.number)

    def pulls_in(self, state: str) -> list[Pull]:
        """The pull requests in the state (`open`, `closed`, `merged`), by number."""
        return sorted((pull for pull in self.pulls if pull.state == state), key=lambda pull: pull.number)


def _time(value: object) -> datetime:
    """A GitHub timestamp (`2026-10-12T02:45:30Z`) as an aware datetime; the epoch when absent."""
    if not isinstance(value, str) or not value:
        return datetime.fromtimestamp(0, UTC)
    return datetime.fromisoformat(value.replace("Z", "+00:00"))


def _names(items: object) -> tuple[str, ...]:
    """The names of a list of label objects."""
    return tuple(str(item["name"]) for item in items) if isinstance(items, list) else ()


def parse_snapshot(data: Mapping[str, Any]) -> Snapshot:
    """The snapshot of the raw JSON `fetch_raw` returns (or `--save-snapshot` wrote); ConfigError when malformed."""
    try:
        issues = tuple(
            Issue(
                number=int(item["number"]),
                title=str(item.get("title", "")),
                body=str(item.get("body") or ""),
                open=str(item["state"]).upper() == "OPEN",
                dropped=str(item.get("stateReason", "")).upper() in ("NOT_PLANNED", "DUPLICATE"),
                labels=_names(item.get("labels")),
                created=_time(item.get("createdAt")),
                updated=_time(item.get("updatedAt")),
                url=str(item.get("url", "")),
            )
            for item in data["issues"]
        )
        pulls = tuple(
            Pull(
                number=int(item["number"]),
                title=str(item.get("title", "")),
                body=str(item.get("body") or ""),
                state=str(item["state"]).lower(),
                labels=_names(item.get("labels")),
                created=_time(item.get("createdAt")),
                updated=_time(item.get("updatedAt")),
                base=str(item.get("baseRefName", "")),
                head=str(item.get("headRefName", "")),
                draft=bool(item.get("isDraft", False)),
                auto_merge=bool(item.get("autoMergeRequest")),
                closes=tuple(int(ref["number"]) for ref in item.get("closingIssuesReferences") or []),
                url=str(item.get("url", "")),
            )
            for item in data["pulls"]
        )
        labels = tuple(
            RemoteLabel(str(item["name"]), str(item.get("color", "")).lower(), str(item.get("description") or ""))
            for item in data["labels"]
        )
        rulesets = data.get("rulesets")
        return Snapshot(
            repo=str(data["repo"]),
            taken=_time(data["taken"]),
            default_branch=str(data.get("default_branch", "main")),
            issues=issues,
            pulls=pulls,
            labels=labels,
            settings=data.get("settings"),
            rulesets=None if rulesets is None else tuple(rulesets),
            notices=tuple(str(notice) for notice in data.get("notices", [])),
        )
    except (KeyError, TypeError, ValueError) as error:
        raise ConfigError(f"the GitHub snapshot is malformed ({type(error).__name__}: {error})") from error


#: Runs a command and returns its standard output; raises OSError with the reason when it fails.
Runner = Callable[[list[str]], str]


def run_gh(arguments: list[str]) -> str:
    """Run a `gh` command (read only) and return its output; OSError with its message when it fails or is missing."""
    try:
        done = subprocess.run(
            ["gh", *arguments], capture_output=True, text=True, encoding="utf-8", timeout=300, check=False
        )
    except (OSError, subprocess.SubprocessError) as error:
        raise OSError(f"gh could not run: {error}") from error
    if done.returncode != 0:
        raise OSError(f"gh {' '.join(arguments[:2])} failed: {done.stderr.strip() or done.stdout.strip()}")
    return done.stdout


ISSUE_FIELDS = "number,title,body,state,stateReason,labels,createdAt,updatedAt,url"
PULL_FIELDS = (
    "number,title,body,state,labels,createdAt,updatedAt,mergedAt,baseRefName,headRefName,isDraft,"
    "autoMergeRequest,closingIssuesReferences,url"
)


def fetch_raw(run: Runner = run_gh, now: datetime | None = None) -> dict[str, Any]:
    """Read the repository's issues, pull requests, labels and settings from GitHub into the raw snapshot JSON.

    Every command is a read. The issues, pull requests and labels are required (OSError when `gh` cannot read them);
    the settings and rulesets need admin rights, so when they cannot be read they are left out and a notice says so.
    """
    repo = json.loads(run(["repo", "view", "--json", "nameWithOwner,defaultBranchRef"]))
    name = str(repo["nameWithOwner"])
    notices: list[str] = []
    issues = json.loads(run(["issue", "list", "--state", "all", "--limit", str(FETCH_LIMIT), "--json", ISSUE_FIELDS]))
    pulls = json.loads(run(["pr", "list", "--state", "all", "--limit", str(FETCH_LIMIT), "--json", PULL_FIELDS]))
    labels = json.loads(run(["label", "list", "--limit", "500", "--json", "name,color,description"]))
    for noun, items in (("issues", issues), ("pull requests", pulls)):
        if len(items) >= FETCH_LIMIT:
            notices.append(f"{len(items)} {noun} fetched: the limit, so older ones may be missing")
    settings: Any = None
    rulesets: Any = None
    try:
        settings = json.loads(run(["api", f"repos/{name}"]))
        listed = json.loads(run(["api", f"repos/{name}/rulesets"]))
        rulesets = [json.loads(run(["api", f"repos/{name}/rulesets/{item['id']}"])) for item in listed]
    except (OSError, ValueError, KeyError, TypeError) as error:
        notices.append(f"settings or rulesets not read ({error}); gh.ruleset did not run")
        rulesets = None
    taken = now or datetime.now(UTC)
    return {
        "repo": name,
        "taken": taken.strftime("%Y-%m-%dT%H:%M:%SZ"),
        "default_branch": (repo.get("defaultBranchRef") or {}).get("name", "main"),
        "issues": issues,
        "pulls": pulls,
        "labels": labels,
        "settings": settings,
        "rulesets": rulesets,
        "notices": notices,
    }


# --- the files on main --------------------------------------------------------------------------------------------


class MainTree(Protocol):
    """The files of the default branch, for the check that a link in an issue leads somewhere."""

    def has(self, path: str) -> bool:
        """Whether the file or folder `path` exists."""
        ...

    def anchors(self, path: str) -> frozenset[str] | None:
        """The anchors of the Markdown page `path`; None when it cannot be read."""
        ...


class GitTree:
    """The tree of a git ref, read with `git ls-tree` and `git show` in a checkout."""

    def __init__(self, root: Path, ref: str, names: Sequence[str]) -> None:
        self._root = root
        self._ref = ref
        self._files = frozenset(names)
        self._folders = frozenset(
            "/".join(name.split("/")[:depth]) for name in names for depth in range(1, name.count("/") + 1)
        )
        self._cache: dict[str, frozenset[str] | None] = {}

    def has(self, path: str) -> bool:
        """Whether `path` is a file or a folder of the ref."""
        return path.rstrip("/") in self._files or path.rstrip("/") in self._folders

    def anchors(self, path: str) -> frozenset[str] | None:
        """The anchors of the page `path` at the ref (cached)."""
        if path not in self._cache:
            try:
                done = subprocess.run(
                    ["git", "show", f"{self._ref}:{path}"],
                    cwd=self._root,
                    capture_output=True,
                    text=True,
                    encoding="utf-8",
                    errors="replace",
                    timeout=60,
                    check=False,
                )
            except (OSError, subprocess.SubprocessError):
                done = None
            self._cache[path] = (
                audit_text.anchors_of(done.stdout) if done is not None and done.returncode == 0 else None
            )
        return self._cache[path]


def git_tree(root: Path, ref: str) -> GitTree | None:
    """The tree of `ref` (`origin/main`) in the checkout at `root`; None when git or the ref is not there."""
    try:
        done = subprocess.run(
            ["git", "ls-tree", "-r", "--name-only", "-z", ref],
            cwd=root,
            capture_output=True,
            timeout=120,
            check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    if done.returncode != 0:
        return None
    return GitTree(root, ref, [name for name in done.stdout.decode("utf-8", errors="replace").split("\0") if name])


# --- the context --------------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class GithubContext:
    """What a `github` check reads: the conventions, the snapshot and the files on the default branch."""

    conventions: Conventions
    snapshot: Snapshot
    #: The default branch's files, or None when they could not be read (the link check then does not run).
    tree: MainTree | None = None
    #: The checkout, which holds the issue forms the `issue.form` check reads; None skips that check.
    root: Path | None = None

    @property
    def now(self) -> datetime:
        """The moment the snapshot was taken: ages are counted from it, so a saved snapshot audits the same."""
        return self.snapshot.taken


def _days(since: datetime, now: datetime) -> int:
    """Whole days from `since` to `now`."""
    return (now - since) // timedelta(days=1)


def _problem(item: Issue | Pull, message: str) -> Problem:
    """A problem about an issue or a pull request: item `#N`, its link and the title in the message."""
    return Problem(f"#{item.number}", f"{message} ({_short(item.title)})", item.url)


def _short(text: str, limit: int = 60) -> str:
    """`text` cut to `limit` characters."""
    return text if len(text) <= limit else text[: limit - 3] + "..."


# --- issues: sections, placeholders, screenshots ------------------------------------------------------------------


def form_of(issue: Issue, conventions: Conventions) -> IssueForm:
    """The form an issue follows: the one most of its headings belong to, else the form the command line uses."""
    headings = {" ".join(section.heading.casefold().split()) for section in audit_text.sections(issue.body)}
    fallback = conventions.issues.forms[conventions.issues.cli_uses_form]
    best, best_score = fallback, 0
    for form in conventions.issues.forms.values():
        score = sum(1 for section in form.sections if " ".join(section.heading.casefold().split()) in headings)
        if score > best_score:
            best, best_score = form, score
    return best


@check("issue.sections")
def issue_sections(context: GithubContext) -> Iterator[Problem]:
    """An open issue has every required section of its form (`##` or `###` headings)."""
    for issue in context.snapshot.open_issues():
        form = form_of(issue, context.conventions)
        found = audit_text.sections(issue.body)
        missing = [
            section.heading
            for section in form.sections
            if section.required and audit_text.find_section(found, section.heading) is None
        ]
        if not missing:
            continue
        known = {" ".join(section.heading.casefold().split()) for section in form.sections}
        instead = [section.heading for section in found if " ".join(section.heading.casefold().split()) not in known]
        hint = f"; it has {', '.join(repr(heading) for heading in instead[:4])} instead" if instead else ""
        yield _problem(
            issue, f"no section {', '.join(repr(heading) for heading in missing)} of the {form.name} form{hint}"
        )


@check("issue.placeholder")
def issue_placeholder(context: GithubContext) -> Iterator[Problem]:
    """A required section of an open issue holds an answer, not nothing or a placeholder like `x`."""
    placeholders = context.conventions.issues.placeholders
    for issue in context.snapshot.open_issues():
        form = form_of(issue, context.conventions)
        found = audit_text.sections(issue.body)
        empty = []
        for section in form.sections:
            if not section.required or section.id == "screenshots":
                continue
            body = audit_text.find_section(found, section.heading)
            if body is not None and audit_text.is_placeholder(body.text, placeholders):
                said = audit_text.answer(body.text).strip()
                empty.append(f"{section.heading!r} is {'empty' if not said else repr(said)}")
        if empty:
            yield _problem(issue, f"{'; '.join(empty)}: write the real answer")


@check("issue.screenshots")
def issue_screenshots(context: GithubContext) -> Iterator[Problem]:
    """The Screenshots section of an open issue holds an uploaded image or `Screenshots: none (reason)`."""
    rules = context.conventions
    for issue in context.snapshot.open_issues():
        form = form_of(issue, rules)
        heading = next((section.heading for section in form.sections if section.id == "screenshots"), None)
        section = audit_text.find_section(audit_text.sections(issue.body), heading) if heading else None
        if heading is None or section is None:
            continue  # a missing section is issue.sections' finding
        for problem in screenshots_problems(
            section.text, heading, rules.issues.screenshots_none, True, rules.pull_requests.placeholders
        ):
            yield _problem(issue, problem)


# --- issues: labels -----------------------------------------------------------------------------------------------


def _label_problems(labels: Sequence[str], is_open: bool, conventions: Conventions) -> list[str]:
    """What is wrong with an issue's labels: counts per family and unknown labels (a closed one: unknown ones only)."""
    rules = conventions.issues.labels
    known = set(conventions.labels.names())
    families: dict[str, list[str]] = {"type": [], "area": [], "priority": [], "status": [], "other": []}
    problems = [f"label {name!r} is not in labels.list" for name in labels if name not in known]
    for name in labels:
        if name in known:
            families[conventions.labels.family(name)].append(name)
    if not is_open:
        return problems  # a closed issue keeps its history labels; a status left on it is issue.closed-status's finding
    triaged = "S-needs-triage" not in families["status"]
    for family, rule in (
        ("type", rules.type),
        ("area", rules.area),
        ("priority", rules.priority),
        ("status", rules.status),
    ):
        have = families[family]
        names = " or ".join(conventions.labels.names(family)[:3]) + (
            "..." if len(conventions.labels.names(family)) > 3 else ""
        )
        needs_one = rule in ("exactly-one", "at-least-one") or (rule == "exactly-one-after-triage" and triaged)
        at_most_one = rule in ("exactly-one", "exactly-one-after-triage")
        if needs_one and not have:
            problems.append(f"no {family} label ({names})")
        elif at_most_one and len(have) > 1:
            problems.append(f"{len(have)} {family} labels ({', '.join(have)}); one is expected")
    return problems


@check("issue.labels")
def issue_labels(context: GithubContext) -> Iterator[Problem]:
    """Open issues carry the labels `issues.labels` asks for; every issue uses known labels."""
    for issue in sorted(context.snapshot.issues, key=lambda item: item.number):
        problems = _label_problems(issue.labels, issue.open, context.conventions)
        if problems:
            yield _problem(issue, "; ".join(problems))


@check("issue.closed-status")
def issue_closed_status(context: GithubContext) -> Iterator[Problem]:
    """A closed issue carries no `S-` label: a status describes open work (the issue-status workflow removes it)."""
    if context.conventions.issues.labels.closed_keeps_status:
        return
    labels = context.conventions.labels
    for issue in sorted(context.snapshot.issues, key=lambda item: item.number):
        left = [name for name in issue.labels if labels.family(name) == "status"] if not issue.open else []
        if left:
            yield _problem(
                issue, f"closed but still labelled {', '.join(left)}: remove it (the close reason is the final state)"
            )


# --- issues: wording, leftovers, links ------------------------------------------------------------------------------


@check("issue.owner-word")
def issue_owner_word(context: GithubContext) -> Iterator[Problem]:
    """An open issue calls the person who directs the project 'user', not 'owner'."""
    wording = context.conventions.repo.wording
    for issue in context.snapshot.open_issues():
        uses = audit_text.word_uses(f"{issue.title}\n{issue.body}", wording.avoid)
        if uses:
            yield _problem(issue, f"says {wording.avoid[0]!r} {len(uses)} time(s), say {wording.person!r}: {uses[0]}")


@check("issue.closed-unchecked")
def issue_closed_unchecked(context: GithubContext) -> Iterator[Problem]:
    """A closed issue has no unchecked item without a follow-up issue number (unless it was dropped)."""
    for issue in sorted(context.snapshot.issues, key=lambda item: item.number):
        if issue.open or issue.dropped or {"duplicate", "wontfix"} & set(issue.labels):
            continue
        loose = [item for item in audit_text.unchecked_items(issue.body) if not audit_text.cites_number(item)]
        if loose:
            shown = "; ".join(repr(_short(item, 50)) for item in loose[:3])
            more = f" and {len(loose) - 3} more" if len(loose) > 3 else ""
            yield _problem(
                issue, f"closed with {len(loose)} unchecked item(s) naming no follow-up issue: {shown}{more}"
            )


@check("issue.stale-status")
def issue_stale_status(context: GithubContext) -> Iterator[Problem]:
    """An open issue's text calls a pull request open, merged or closed as it is now (`#55 (open)`)."""
    snapshot = context.snapshot
    for issue in snapshot.open_issues():
        wrong = []
        for number, claimed in audit_text.state_claims(issue.body):
            pull = snapshot.pull(number)
            if pull is None:
                continue
            actual = pull.state
            if claimed == actual or (claimed == "closed" and actual == "merged"):
                continue
            wrong.append(f"#{number} is called {claimed} but is {actual}")
        if wrong:
            yield _problem(issue, "; ".join(wrong))


@check("issue.links")
def issue_links(context: GithubContext) -> Iterator[Problem]:
    """An open issue links the docs and research pages it names, and each link leads to a page (and anchor) on main."""
    tree = context.tree
    default = context.snapshot.default_branch
    for issue in context.snapshot.open_issues():
        problems = [
            f"names {path} as plain text: write [{path}](../blob/{default}/{path})"
            for path in audit_text.plain_doc_paths(issue.body)
        ]
        problems += [
            f"link target {target!r} does not work from an issue page: use ../blob/{default}/<path>"
            for target in audit_text.broken_relative_links(issue.body)
        ]
        if tree is not None:
            for link in audit_text.repo_links(issue.body):
                if link.ref != default:
                    problems.append(f"links {link.path} on {link.ref!r}, not {default!r}")
                elif not tree.has(link.path):
                    problems.append(f"links {link.path}, which is not on {default}")
                elif link.anchor and link.path.endswith(".md"):
                    anchors = tree.anchors(link.path)
                    if anchors is not None and link.anchor not in anchors:
                        problems.append(f"links {link.path}#{link.anchor}, which has no such section on {default}")
        if problems:
            shown = "; ".join(problems[:3])
            yield _problem(issue, shown + (f"; and {len(problems) - 3} more" if len(problems) > 3 else ""))


@check("issue.provenance")
def issue_provenance(context: GithubContext) -> Iterator[Problem]:
    """An open issue that a pull request's Remaining list points at (a leftover) says where it came from (`From:`)."""
    snapshot = context.snapshot
    rules = context.conventions.pull_requests
    leftovers: dict[int, int] = {}
    for pull in snapshot.pulls:
        for item in remaining_items(pull.body, rules):
            for number in NUMBER_REFERENCE.findall(item):
                leftovers.setdefault(int(number[1:]), pull.number)
    line = context.conventions.issues.provenance.line
    for issue in snapshot.open_issues():
        source = leftovers.get(issue.number)
        if source is not None and not _PROVENANCE.search(audit_text.clean(issue.body)):
            yield _problem(issue, f"is a leftover of #{source} but has no '{line}' line saying so")


#: What `issue.form` leaves to the rules above (content, screenshots, links, wording, statuses): it reports the shape.
_COVERED_ELSEWHERE = frozenset({"empty", "screenshots", "links", "wording", "status"})


@check("issue.form")
def issue_form_shape(context: GithubContext) -> Iterator[Problem]:
    """An open issue has its form's exact shape: `###` headings with the form's names in its order, no extra
    sections, and a dropdown value from the form's options (`coney-tools issue check` runs the whole check)."""
    # Imported here: issue_form uses this module's label check, so a top-level import would be circular.
    from coney_tools import issue_form

    if context.root is None:
        return
    specs: dict[str, issue_form.FormSpec] = {}
    options = issue_form.CheckOptions(skip=_COVERED_ELSEWHERE)
    for issue in context.snapshot.open_issues():
        name = issue_form.detect_form(issue.title, issue.body, context.conventions)
        if name not in specs:
            specs[name] = issue_form.load_form(context.root, context.conventions, name)
        found = issue_form.check_issue(issue.title, issue.body, specs[name], context.conventions, options).problems
        if found:
            shown = "; ".join(problem.message for problem in found[:3])
            yield _problem(issue, shown + (f"; and {len(found) - 3} more" if len(found) > 3 else ""))


# --- issues: ages -------------------------------------------------------------------------------------------------


def _labelled(context: GithubContext, label: str) -> list[Issue]:
    """The open issues carrying `label`."""
    return [issue for issue in context.snapshot.open_issues() if label in issue.labels]


@check("issue.triage-age")
def issue_triage_age(context: GithubContext) -> Iterator[Problem]:
    """An issue has not waited in S-needs-triage longer than `needs_triage_days` since it was opened."""
    limit = context.conventions.hygiene.thresholds.needs_triage_days
    for issue in _labelled(context, "S-needs-triage"):
        waited = _days(issue.created, context.now)
        if waited > limit:
            yield _problem(issue, f"in S-needs-triage for {waited} days (limit {limit}): triage it")


@check("issue.info-age")
def issue_info_age(context: GithubContext) -> Iterator[Problem]:
    """An S-needs-info issue has had activity within `needs_info_days`."""
    limit = context.conventions.hygiene.thresholds.needs_info_days
    for issue in _labelled(context, "S-needs-info"):
        idle = _days(issue.updated, context.now)
        if idle > limit:
            yield _problem(issue, f"waiting for information with no activity for {idle} days (limit {limit})")


@check("issue.progress-idle")
def issue_progress_idle(context: GithubContext) -> Iterator[Problem]:
    """An S-in-progress issue has had activity within `in_progress_idle_days`."""
    limit = context.conventions.hygiene.thresholds.in_progress_idle_days
    for issue in _labelled(context, "S-in-progress"):
        idle = _days(issue.updated, context.now)
        if idle > limit:
            yield _problem(issue, f"in progress with no activity for {idle} days (limit {limit})")


@check("issue.review-without-pr")
def issue_review_without_pr(context: GithubContext) -> Iterator[Problem]:
    """An S-pending-review issue is named by an open pull request (its `Fixes #N` or a mention)."""
    mentioned: set[int] = set()
    for pull in context.snapshot.pulls_in("open"):
        mentioned.update(pull.closes)
        mentioned.update(int(match[1:]) for match in NUMBER_REFERENCE.findall(audit_text.clean(pull.body)))
    for issue in _labelled(context, "S-pending-review"):
        if issue.number not in mentioned:
            yield _problem(issue, "is S-pending-review but no open pull request names it")


# --- pull requests ------------------------------------------------------------------------------------------------


@check("pr.title")
def pr_title(context: GithubContext) -> Iterator[Problem]:
    """An open pull request's title follows the commit title rules (it becomes the squash commit's title)."""
    for pull in context.snapshot.pulls_in("open"):
        problems = check_title(pull.title, context.conventions.commits.title)
        if problems:
            yield _problem(pull, "; ".join(problems))


@check("pr.body")
def pr_body(context: GithubContext) -> Iterator[Problem]:
    """An open pull request's description passes the template check (sections, no local paths, images uploaded)."""
    for pull in context.snapshot.pulls_in("open"):
        problems = check_body(pull.body, context.conventions.pull_requests)
        if problems:
            shown = "; ".join(problems[:3]) + (f"; and {len(problems) - 3} more" if len(problems) > 3 else "")
            yield _problem(pull, shown)


@check("pr.owner-word")
def pr_owner_word(context: GithubContext) -> Iterator[Problem]:
    """An open pull request calls the person who directs the project 'user', not 'owner'."""
    wording = context.conventions.repo.wording
    for pull in context.snapshot.pulls_in("open"):
        uses = audit_text.word_uses(f"{pull.title}\n{pull.body}", wording.avoid)
        if uses:
            yield _problem(pull, f"says {wording.avoid[0]!r} {len(uses)} time(s), say {wording.person!r}: {uses[0]}")


@check("pr.remaining-uncited")
def pr_remaining_uncited(context: GithubContext) -> Iterator[Problem]:
    """A merged pull request's unchecked Remaining items each name an issue (so the leftover is tracked)."""
    for pull in context.snapshot.pulls_in("merged"):
        loose = uncited_remaining(pull.body, context.conventions.pull_requests)
        if loose:
            shown = "; ".join(repr(_short(item, 50)) for item in loose[:3])
            yield _problem(pull, f"merged with {len(loose)} Remaining item(s) naming no issue: {shown}")


@check("pr.idle")
def pr_idle(context: GithubContext) -> Iterator[Problem]:
    """An open pull request has had activity within `pull_request_idle_days`."""
    limit = context.conventions.hygiene.thresholds.pull_request_idle_days
    for pull in context.snapshot.pulls_in("open"):
        idle = _days(pull.updated, context.now)
        if idle > limit:
            yield _problem(pull, f"open with no activity for {idle} days (limit {limit})")


def _depends_pattern(line: str) -> re.Pattern[str]:
    """The `Depends on #N` line as a pattern (`#N` stands for any number)."""
    words = r"\s+".join(re.escape(word) for word in line.replace("#N", "").split())
    return re.compile(rf"^[ \t]*{words}\s+#\d+", re.IGNORECASE | re.MULTILINE)


@check("pr.stacked-depends")
def pr_stacked_depends(context: GithubContext) -> Iterator[Problem]:
    """An open pull request whose base is not the default branch says which pull request it depends on."""
    stacked = context.conventions.pull_requests.stacked
    pattern = _depends_pattern(stacked.depends_line)
    for pull in context.snapshot.pulls_in("open"):
        if pull.base != context.snapshot.default_branch and not pattern.search(audit_text.clean(pull.body)):
            yield _problem(pull, f"is based on {pull.base!r} but has no '{stacked.depends_line}' line")


@check("pr.stacked-automerge")
def pr_stacked_automerge(context: GithubContext) -> Iterator[Problem]:
    """Auto-merge is off on a pull request whose base is not the default branch (it would merge into its base)."""
    if context.conventions.pull_requests.stacked.auto_merge_on_non_default_base:
        return
    for pull in context.snapshot.pulls_in("open"):
        if pull.auto_merge and pull.base != context.snapshot.default_branch:
            yield _problem(pull, f"has auto-merge on but is based on {pull.base!r}: it would not reach main")


@check("pr.base-merged")
def pr_base_merged(context: GithubContext) -> Iterator[Problem]:
    """An open pull request is not based on a branch whose own pull request has merged."""
    merged_heads = {pull.head: pull for pull in context.snapshot.pulls_in("merged")}
    for pull in context.snapshot.pulls_in("open"):
        base = merged_heads.get(pull.base)
        if base is not None and pull.base != context.snapshot.default_branch:
            after = context.conventions.pull_requests.stacked.after_base_merges
            yield _problem(pull, f"is based on {pull.base!r}, which #{base.number} merged: {after}, then retarget it")


@check("pr.merged-into-base")
def pr_merged_into_base(context: GithubContext) -> Iterator[Problem]:
    """A pull request merged into another branch did not close the issues it fixes (GitHub closes only on main)."""
    if context.conventions.pull_requests.stacked.merge_into_base:
        return
    snapshot = context.snapshot
    for pull in snapshot.pulls_in("merged"):
        if pull.base == snapshot.default_branch:
            continue
        numbers = set(pull.closes) | {
            int(match.group("number")) for match in _CLOSING.finditer(audit_text.clean(pull.body))
        }
        stuck = sorted(number for number in numbers if (issue := snapshot.issue(number)) is not None and issue.open)
        if stuck:
            yield _problem(
                pull,
                f"was merged into {pull.base!r}, not {snapshot.default_branch!r}, so "
                f"{', '.join(f'#{n}' for n in stuck)} stay open: close them by hand or merge the base",
            )


# --- labels, settings and the ruleset -----------------------------------------------------------------------------


@check("gh.labels")
def labels_match(context: GithubContext) -> Iterator[Problem]:
    """The labels on GitHub are the conventions' `labels.list`: same names, colours and descriptions."""
    wanted = {label.name.casefold(): label for label in context.conventions.labels.list}
    have = {label.name.casefold(): label for label in context.snapshot.labels}
    for key, label in wanted.items():
        remote = have.get(key)
        if remote is None:
            yield Problem(f"label {label.name}", "is in labels.list but not on GitHub")
            continue
        differences = []
        if remote.name != label.name:
            differences.append(f"name is {remote.name!r}")
        if remote.color != label.color:
            differences.append(f"colour is {remote.color}, the conventions say {label.color}")
        if remote.description != label.description:
            differences.append(f"description is {remote.description!r}, the conventions say {label.description!r}")
        if differences:
            yield Problem(f"label {label.name}", "; ".join(differences))
    for key, remote in have.items():
        if key not in wanted:
            yield Problem(f"label {remote.name}", "is on GitHub but not in labels.list")


def _ruleset_problems(rulesets: Sequence[Mapping[str, Any]], default: str, conventions: Conventions) -> list[str]:
    """What differs between the active ruleset of the default branch and the `merge` section."""
    merge = conventions.merge
    active = [
        ruleset
        for ruleset in rulesets
        if ruleset.get("enforcement") == "active"
        and any(
            name in ("~DEFAULT_BRANCH", "~ALL", f"refs/heads/{default}")
            for name in ruleset.get("conditions", {}).get("ref_name", {}).get("include", [])
        )
    ]
    if not active:
        return [f"no active ruleset protects {default}"]
    rules = [rule for ruleset in active for rule in ruleset.get("rules", [])]
    kinds = {rule["type"] for rule in rules}
    problems = [f"the ruleset has no {kind!r} rule" for kind in ("deletion", "non_fast_forward") if kind not in kinds]
    if ("required_linear_history" in kinds) != merge.linear_history:
        state = "on" if "required_linear_history" in kinds else "off"
        problems.append(f"linear history is {state}, merge.linear_history says otherwise")
    for rule in rules:
        parameters = rule.get("parameters", {})
        if rule["type"] == "pull_request" and set(parameters.get("allowed_merge_methods", [])) != set(merge.methods):
            have = sorted(parameters.get("allowed_merge_methods", []))
            problems.append(f"merge methods are {have}, merge.methods says {sorted(merge.methods)}")
        if rule["type"] == "required_status_checks":
            contexts = {item["context"] for item in parameters.get("required_status_checks", [])}
            if contexts != set(merge.required_checks):
                missing = sorted(set(merge.required_checks) - contexts)
                extra = sorted(contexts - set(merge.required_checks))
                problems.append(
                    f"required checks differ (missing on GitHub: {missing}; not in merge.required_checks: {extra})"
                )
            strict = bool(parameters.get("strict_required_status_checks_policy"))
            if strict != merge.up_to_date_required:
                problems.append(
                    f"branch must be up to date is {strict}, merge.up_to_date_required says {merge.up_to_date_required}"
                )
    if "required_status_checks" not in kinds:
        problems.append("the ruleset has no required status checks")
    return problems


@check("gh.ruleset")
def ruleset_matches(context: GithubContext) -> Iterator[Problem]:
    """The repository settings and the default branch's ruleset are what `merge` says."""
    snapshot = context.snapshot
    merge = context.conventions.merge
    if snapshot.settings is not None:
        settings = snapshot.settings
        expected = {
            "allow_squash_merge": "squash" in merge.methods,
            "allow_merge_commit": "merge" in merge.methods,
            "allow_rebase_merge": "rebase" in merge.methods,
            "allow_auto_merge": merge.auto_merge,
            "delete_branch_on_merge": merge.delete_branch_on_merge,
            "squash_merge_commit_title": merge.squash_title,
            "squash_merge_commit_message": merge.squash_message,
        }
        for key, wanted in expected.items():
            if key in settings and settings[key] != wanted:
                yield Problem(f"setting {key}", f"is {settings[key]!r} on GitHub, merge section says {wanted!r}")
    if snapshot.rulesets is not None:
        for message in _ruleset_problems(snapshot.rulesets, snapshot.default_branch, context.conventions):
            yield Problem("ruleset", message)
