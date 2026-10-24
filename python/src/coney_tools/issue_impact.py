# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools issue impact`: which open issues a branch may have changed.

Documented in docs/guides/coney-tools-repo.md#issue-impact.

A pull request names the issue it fixes (`Fixes #N`) and, until the Issues affected section existed, nothing else, so
the issues it half-fixed or made untrue stayed as they were (the audit of 2026-10-23 found 175 of 311 open issues out
of date). This module reads what a branch touches from its diff and matches it against the open issues' bodies:

* **Facts of the branch** (`gather`): the changed paths; the `@orig` addresses and names of the functions whose body
  changed; the addresses and the code-like names in the changed lines; for Markdown pages, the anchors of the
  sections whose lines changed; the pull requests and branches the change replaces; the issues it fixes (which merging
  closes, so they are not listed).
* **The match** (`match_issues`): an issue matches when its body cites one of those things, and the reason says which:
  it names a pull request or branch the change replaces, an `@orig` address the change touches, a page section the
  change edits, a file or folder the change touches, or a function or constant name a changed line holds. A name,
  address or path that more than `issues.impact.common_max_issues` open issues cite says little about any one of
  them and is skipped.

The result is a suggestion for the author to read, never a verdict: `pr text` and the Issues affected section take the
issues a person confirms. `gh` is injected, so the tests never touch the network.
"""

from __future__ import annotations

import json
import re
import subprocess
from collections import Counter
from collections.abc import Callable, Iterable, Mapping, Sequence
from dataclasses import dataclass, field
from datetime import UTC, datetime, timedelta
from pathlib import Path
from typing import Any

from coney_tools import audit_text, orig_review
from coney_tools.config import ConfigError
from coney_tools.conventions import ImpactRules
from coney_tools.task_leads import code_spans, is_code_like

#: The shape of the JSON `--json` prints; raise it when a key changes meaning.
JSON_VERSION = 1
#: What `gh issue list` is asked for.
ISSUE_FIELDS = "number,title,body,labels,url,updatedAt"
#: How many open issues one read asks for (the audit uses the same figure).
ISSUE_LIMIT = 3000
#: The strongest reason first: the order issues and reasons are listed in.
KINDS = ("replaced", "branch", "address", "section", "path", "folder", "name")

#: `0x001490b8`, the form every address in the repository is written in.
_ADDRESS = re.compile(r"\b0x[0-9a-fA-F]{8}\b")
#: A name in a code span, up to a call's parentheses: `Brain::think()` is `Brain::think`.
_NAME = re.compile(r"^~?[A-Za-z_]\w*(?:::~?[A-Za-z_]\w*)*")
#: A word of a changed line that could be a function, type or constant name.
_WORD = re.compile(r"~?[A-Za-z_]\w*(?:::~?[A-Za-z_]\w*)*")
#: The link prefix of a page on the website or the repository, which hides the path behind it (`../blob/main/`).
_BLOB_PREFIX = re.compile(r"(?:https?://[^\s)]*?/|(?:\.\./)+)?blob/[\w.-]+/")
#: A closing keyword line (`Fixes #12`), in a description or a commit message.
_CLOSING = re.compile(r"^[ \t]*(?:fix(?:es|ed)?|close[sd]?|resolve[sd]?)[ \t]+#(?P<number>\d+)", re.I | re.M)
#: A line that says the change replaces a pull request or an issue (`Replaces #630`, `Supersedes #12`).
_REPLACING = re.compile(
    r"^[ \t]*(?:replaces|supersedes|obsoletes)[ \t]+(?P<refs>#\d+(?:[ \t,]+(?:and[ \t]+)?#\d+)*)", re.I | re.M
)
#: A hunk header of `git diff -U0`: the new side's first line and length, then the function context git found.
_HUNK = re.compile(r"^@@ -\d+(?:,\d+)? \+(?P<start>\d+)(?:,(?P<count>\d+))? @@ ?(?P<context>.*)$")
_DIFF_HEADER = re.compile(r"^diff --git a/(?P<old>.+) b/(?P<new>.+)$")
#: Folders are named by their first two components at the shortest (`src/ai`), never `src` alone.
_MIN_FOLDER_DEPTH = 2
#: Files whose changed lines say nothing about a name an issue cites (generated, binary or very large).
_SKIP_CONTENT = re.compile(
    r"\.(?:png|jpe?g|gif|webp|bin|lock|svg|toml|csv|tsv|json)$|^docs/progress/|^docs/references/"
)


@dataclass(frozen=True)
class Reason:
    """Why an issue was matched: the kind (one of `KINDS`), what it cites and what the branch does to that."""

    kind: str
    what: str
    detail: str

    def text(self) -> str:
        """The reason as one phrase."""
        return f"{self.detail}" if self.detail else self.what

    def to_json(self) -> dict[str, str]:
        """The reason as the JSON output lists it."""
        return {"kind": self.kind, "what": self.what, "detail": self.detail}


@dataclass(frozen=True)
class Candidate:
    """An open issue the branch may have changed, with the reasons."""

    number: int
    title: str
    url: str
    labels: tuple[str, ...]
    reasons: tuple[Reason, ...]

    @property
    def strength(self) -> str:
        """The kind of the strongest reason."""
        return self.reasons[0].kind

    def to_json(self) -> dict[str, Any]:
        """The candidate as the JSON output lists it."""
        return {
            "number": self.number,
            "title": self.title,
            "url": self.url,
            "labels": list(self.labels),
            "strength": self.strength,
            "reasons": [reason.to_json() for reason in self.reasons],
        }


@dataclass(frozen=True)
class OpenIssue:
    """An open issue as the match reads it."""

    number: int
    title: str
    body: str
    url: str
    labels: tuple[str, ...]


@dataclass
class Touched:
    """What a branch touches: the facts the open issues are matched against."""

    base: str = ""
    head: str = ""
    branch: str = ""
    #: Every changed path (a renamed file under both names), with what happened to it: `changed`, `added`,
    #: `deleted` or `renamed to X` / `renamed from X`.
    paths: dict[str, str] = field(default_factory=dict)
    #: Address (`0x001490b8`) to where the branch touches it, for an address in a changed line or an `@orig` tag.
    addresses: dict[str, str] = field(default_factory=dict)
    #: Name to the first changed file that holds it in a changed line.
    names: dict[str, str] = field(default_factory=dict)
    #: Page to the anchors of the sections whose lines changed (its own and every enclosing section's).
    sections: dict[str, frozenset[str]] = field(default_factory=dict)
    #: The pull requests (`#630`) and branch names the change replaces.
    replaces: list[str] = field(default_factory=list)
    #: The issues the change finishes (`Fixes #N`): merging closes them, so they are not candidates.
    fixes: set[int] = field(default_factory=set)
    notices: list[str] = field(default_factory=list)


# --- reading the branch ------------------------------------------------------------------------------------------


def git_output(root: Path, *args: str) -> str:
    """Run git in `root`; its stdout, or a ConfigError with its message."""
    done = subprocess.run(
        ["git", "-C", str(root), "-c", "core.quotepath=off", *args],
        capture_output=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    if done.returncode != 0:
        raise ConfigError(f"git {' '.join(args)}: {done.stderr.strip() or 'failed'}")
    return done.stdout


def closing_numbers(text: str) -> set[int]:
    """The issue numbers `Fixes #N` lines of a description or a commit message name."""
    return {int(match.group("number")) for match in _CLOSING.finditer(text)}


def replaced_refs(text: str) -> list[str]:
    """The `#N` references `Replaces #630`-style lines of a commit message name, in order."""
    found: list[str] = []
    for line in _REPLACING.finditer(text):
        found.extend(re.findall(r"#\d+", line.group("refs")))
    return list(dict.fromkeys(found))


def _is_code_name(word: str) -> bool:
    """Whether a word of a changed line reads like an identifier worth matching: code-like, not a keyword."""
    return is_code_like(word)


def _names_in_line(line: str) -> Iterable[str]:
    """The code-like names of one changed line, each qualified name whole and by its code-like parts."""
    for word in _WORD.findall(line):
        if _is_code_name(word):
            yield word
        if "::" in word:
            for part in word.split("::"):
                if _is_code_name(part.lstrip("~")):
                    yield part.lstrip("~")


@dataclass
class _FileDiff:
    """One file of `git diff -U0`: its names, what happened and the lines it changed."""

    old: str
    new: str
    status: str = "changed"
    #: The new side's changed line ranges (first, last).
    ranges: list[tuple[int, int]] = field(default_factory=list)
    #: The text of every added and removed line, and the function context of each hunk.
    text: list[str] = field(default_factory=list)


def parse_diff(patch: str) -> list[_FileDiff]:
    """The files of a `git diff -U0` patch, with the changed ranges and lines of each."""
    files: list[_FileDiff] = []
    current: _FileDiff | None = None
    for line in patch.splitlines():
        header = _DIFF_HEADER.match(line)
        if header:
            current = _FileDiff(header.group("old"), header.group("new"))
            if current.old != current.new:
                current.status = "renamed"
            files.append(current)
        elif current is None:
            continue
        elif line.startswith("new file mode"):
            current.status = "added"
        elif line.startswith("deleted file mode"):
            current.status = "deleted"
        elif line.startswith("rename from "):
            current.old = line[len("rename from ") :]
            current.status = "renamed"
        elif line.startswith("rename to "):
            current.new = line[len("rename to ") :]
            current.status = "renamed"
        elif line.startswith("@@"):
            hunk = _HUNK.match(line)
            if hunk:
                start = int(hunk.group("start"))
                count = 1 if hunk.group("count") is None else int(hunk.group("count"))
                current.ranges.append((start, start + max(count, 1) - 1))
                current.text.append(hunk.group("context"))
        elif line.startswith(("+", "-")) and not line.startswith(("+++", "---")):
            current.text.append(line[1:])
    return files


def _heading_chain(spans: Sequence[tuple[int, int, frozenset[str]]], line: int) -> frozenset[str]:
    """The anchors of the section holding `line` and of every section that encloses it."""
    index = max((i for i, (number, _, _) in enumerate(spans) if number <= line), default=-1)
    found: set[str] = set()
    level = 7
    while index >= 0:
        _, depth, anchors = spans[index]
        if depth < level:
            found |= anchors
            level = depth
        index -= 1
    return frozenset(found)


def gather(
    root: Path, base: str, head: str, messages: Sequence[str] = (), description: str = "", replaces: Sequence[str] = ()
) -> Touched:
    """Read what the branch `head` touches since it left `base`, in the checkout at `root`.

    `messages` are the commits' messages (they may say `Fixes #N` or `Replaces #N`), `description` is the pull request
    text when there is one (its `Fixes #N` lines), and `replaces` names more pull requests or branches the change
    replaces. ConfigError when git cannot read the refs.
    """
    touched = Touched(base=base, head=head)
    merge_base = git_output(root, "merge-base", base, head).strip()
    patch = git_output(root, "diff", "-M", "-U0", "--no-color", "--no-ext-diff", merge_base, head)
    for diff in parse_diff(patch):
        _add_file(root, touched, diff, head)
    _add_functions(root, touched, base, head)
    for text in (*messages, description):
        touched.fixes |= closing_numbers(text)
    touched.replaces = list(dict.fromkeys([*replaced_refs("\n".join(messages)), *replaces]))
    return touched


def _add_file(root: Path, touched: Touched, diff: _FileDiff, head: str) -> None:
    """Record one changed file: its paths, the addresses and names in its changed lines, its changed sections."""
    if diff.status == "renamed":
        touched.paths[diff.old] = f"renamed to {diff.new}"
        touched.paths[diff.new] = f"renamed from {diff.old}"
    else:
        touched.paths[diff.new] = diff.status
    if _SKIP_CONTENT.search(diff.new):
        return
    for text in diff.text:
        for address in _ADDRESS.findall(text):
            touched.addresses.setdefault(address.lower(), f"a changed line of {diff.new} holds it")
        for name in _names_in_line(text):
            touched.names.setdefault(name, diff.new)
    if diff.new.endswith(".md") and diff.status != "deleted" and diff.ranges:
        try:
            page = git_output(root, "show", f"{head}:{diff.new}")
        except ConfigError:
            return
        spans = audit_text.heading_spans(page)
        anchors: set[str] = set()
        for first, last in diff.ranges:
            for line in range(first, last + 1):
                anchors |= _heading_chain(spans, line)
        if anchors:
            touched.sections[diff.new] = frozenset(anchors)


def _add_functions(root: Path, touched: Touched, base: str, head: str) -> None:
    """Record the `@orig` address and name of every function whose body or tags the branch changed."""
    try:
        stamp = orig_review.pr_stamp(root, head, base)
    except ConfigError as error:
        touched.notices.append(f"the changed @orig functions were not read ({error})")
        return
    for function in stamp["functions"]:
        for address in function["addresses"]:
            touched.addresses[address.lower()] = (
                f"the @orig of {function['coney']} ({function['where']}), which changed"
            )


# --- matching the open issues ----------------------------------------------------------------------------------


def issue_names(body: str) -> list[str]:
    """The code-like names an issue writes in code spans, a call's parentheses dropped (`Brain::think()` is
    `Brain::think`)."""
    found: list[str] = []
    for span in code_spans(body):
        name = _NAME.match(span.strip())
        if name and is_code_like(name.group(0).lstrip("~")):
            found.append(name.group(0))
    return list(dict.fromkeys(found))


def _prose(body: str) -> str:
    """An issue body ready for path matching: the link prefix of a page (`../blob/main/`) removed."""
    return _BLOB_PREFIX.sub(" ", body)


def _cites_path(text: str, path: str) -> re.Match[str] | None:
    """The place `text` writes the file `path`, with an optional `#anchor`; None when it does not."""
    if path not in text:
        return None
    return re.search(rf"(?<![\w/.-]){re.escape(path)}(?:#(?P<anchor>[\w{{}}-]+))?(?![\w/-]|\.\w)", text)


def _cites_folder(text: str, folder: str) -> bool:
    """Whether `text` writes the folder `folder` (`src/ai`, `src/ai/`), not a file inside it."""
    if folder not in text:
        return False
    return re.search(rf"(?<![\w/.-]){re.escape(folder)}/?(?![\w/-]|\.\w)", text) is not None


def _folders(path: str) -> list[str]:
    """The folders that hold `path`, outermost first, from `_MIN_FOLDER_DEPTH` components down."""
    parts = path.split("/")[:-1]
    return ["/".join(parts[:depth]) for depth in range(_MIN_FOLDER_DEPTH, len(parts) + 1)]


@dataclass(frozen=True)
class _Hit:
    """One thing an issue cites that the branch touches, before the common ones are dropped."""

    number: int
    reason: Reason


def _hits(
    issue: OpenIssue, touched: Touched, folders: Mapping[str, str], branch_pattern: re.Pattern[str] | None
) -> list[_Hit]:
    """Everything the branch touches that `issue` cites."""
    hits: list[_Hit] = []
    text = _prose(issue.body)
    for ref in touched.replaces:
        if ref.startswith("#"):
            if re.search(rf"(?<![\w&/]){re.escape(ref)}\b", issue.body):
                hits.append(_Hit(issue.number, Reason("replaced", ref, f"cites {ref}, which this change replaces")))
        elif re.search(rf"(?<![\w/.-]){re.escape(ref)}(?![\w/-]|\.\w)", issue.body):
            hits.append(
                _Hit(issue.number, Reason("replaced", ref, f"names the branch {ref}, which this change replaces"))
            )
    if branch_pattern is not None and branch_pattern.search(issue.body):
        hits.append(_Hit(issue.number, Reason("branch", touched.branch, f"names this branch ({touched.branch})")))
    for address in dict.fromkeys(a.lower() for a in _ADDRESS.findall(issue.body)):
        where = touched.addresses.get(address)
        if where:
            hits.append(_Hit(issue.number, Reason("address", address, f"cites {address}: {where}")))
    for path, what in touched.paths.items():
        found = _cites_path(text, path)
        if found is None:
            continue
        anchor = found.group("anchor")
        sections = touched.sections.get(path)
        if anchor and sections is not None and not what.startswith("renamed to"):
            if anchor in sections:
                hits.append(
                    _Hit(
                        issue.number,
                        Reason("section", f"{path}#{anchor}", f"cites {path}#{anchor}, a section this change edits"),
                    )
                )
        else:
            hits.append(_Hit(issue.number, Reason("path", path, f"cites {path}, which this change {_verb(what)}")))
    for folder, example in folders.items():
        if _cites_folder(text, folder):
            hits.append(
                _Hit(
                    issue.number,
                    Reason("folder", folder, f"cites the folder {folder}/, where this change touches {example}"),
                )
            )
    for name in issue_names(issue.body):
        where = touched.names.get(name)
        if where:
            hits.append(
                _Hit(issue.number, Reason("name", name, f"names `{name}`, which a changed line of {where} holds"))
            )
    return hits


def _verb(what: str) -> str:
    """The phrase for what happens to a path: `modifies`, `adds`, `deletes` or `renames`."""
    if what.startswith("renamed"):
        return "renames" if what.startswith("renamed to") else "creates by a rename"
    return {"added": "adds", "deleted": "deletes"}.get(what, "modifies")


def match_issues(
    issues: Iterable[OpenIssue], touched: Touched, rules: ImpactRules
) -> tuple[list[Candidate], list[dict[str, Any]]]:
    """The candidates among `issues` and the issues left out (the ones the change fixes).

    The candidates are ordered by their strongest reason, then by how many reasons they have, then by number.
    """
    everyone = [issue for issue in issues]
    excluded = [
        {"number": issue.number, "why": "this change fixes it (Fixes #N): merging closes it"}
        for issue in everyone
        if issue.number in touched.fixes
    ]
    pool = [issue for issue in everyone if issue.number not in touched.fixes]
    folders: dict[str, str] = {}
    for path in touched.paths:
        for folder in _folders(path):
            folders.setdefault(folder, path)
    branch = touched.branch
    pattern = (
        re.compile(rf"(?<![\w/.-]){re.escape(branch)}(?![\w/-]|\.\w)")
        if branch and branch not in ("HEAD", "main", "master")
        else None
    )
    hits = [hit for issue in pool for hit in _hits(issue, touched, folders, pattern)]
    # A token that many open issues cite says little about any of them: drop the kinds that name one.
    cited: Counter[tuple[str, str]] = Counter((hit.reason.kind, hit.reason.what) for hit in hits)
    kept = [
        hit
        for hit in hits
        if hit.reason.kind in ("replaced", "branch")
        or cited[(hit.reason.kind, hit.reason.what)] <= rules.common_max_issues
    ]
    by_issue: dict[int, list[Reason]] = {}
    for hit in kept:
        by_issue.setdefault(hit.number, []).append(hit.reason)
    index = {issue.number: issue for issue in pool}
    candidates = []
    for number, reasons in by_issue.items():
        ordered = sorted(reasons, key=lambda r: (KINDS.index(r.kind), r.what))
        issue = index[number]
        candidates.append(Candidate(number, issue.title, issue.url, issue.labels, tuple(ordered)))
    candidates.sort(key=lambda c: (KINDS.index(c.strength), -len(c.reasons), c.number))
    return candidates, excluded


# --- the open issues, from gh or the cache ---------------------------------------------------------------------

#: Runs `gh` and returns its output; raises OSError when it fails.
Runner = Callable[[list[str]], str]


def parse_issues(data: object) -> list[OpenIssue]:
    """The open issues of `gh issue list --json` output; ConfigError when it is not a list of issues."""
    if not isinstance(data, list):
        raise ConfigError("the issue list is not a JSON list")
    try:
        return [
            OpenIssue(
                number=int(item["number"]),
                title=str(item.get("title", "")),
                body=str(item.get("body") or ""),
                url=str(item.get("url", "")),
                labels=tuple(str(label["name"]) for label in item.get("labels") or [] if isinstance(label, dict)),
            )
            for item in data
        ]
    except (KeyError, TypeError, ValueError) as error:
        raise ConfigError(f"the issue list is malformed ({type(error).__name__}: {error})") from error


def read_open_issues(
    cache: Path | None, rules: ImpactRules, run: Runner, now: datetime, refresh: bool = False
) -> tuple[list[OpenIssue], list[str]]:
    """The open issues and the notices about how they were read.

    The list is read from GitHub once and kept in `cache` for `rules.cache_minutes`; `refresh` reads it again. When
    GitHub cannot be reached a cache of any age is used, with a notice; with no cache either, ConfigError.
    """
    notices: list[str] = []
    stored = _load_cache(cache)
    if stored is not None and not refresh and now - stored[0] < timedelta(minutes=rules.cache_minutes):
        return parse_issues(stored[1]), notices
    try:
        raw = json.loads(run(["issue", "list", "--state", "open", "--limit", str(ISSUE_LIMIT), "--json", ISSUE_FIELDS]))
    except (OSError, ValueError) as error:
        if stored is None:
            raise ConfigError(f"cannot read the open issues with gh ({error})") from error
        notices.append(
            f"gh could not be reached ({error}); the issues are from the cache of {stored[0]:%Y-%m-%d %H:%M} UTC"
        )
        return parse_issues(stored[1]), notices
    if isinstance(raw, list) and len(raw) >= ISSUE_LIMIT:
        notices.append(f"{len(raw)} open issues read: the limit, so some may be missing")
    _save_cache(cache, now, raw)
    return parse_issues(raw), notices


def _load_cache(cache: Path | None) -> tuple[datetime, Any] | None:
    """The cached issue list and when it was read; None when there is none or it is damaged."""
    if cache is None or not cache.is_file():
        return None
    try:
        data = json.loads(cache.read_text(encoding="utf-8"))
        return datetime.fromisoformat(str(data["read"])), data["issues"]
    except (OSError, ValueError, KeyError, TypeError):
        return None


def _save_cache(cache: Path | None, now: datetime, issues: object) -> None:
    """Keep the issue list for the next run; a folder that cannot be written costs only the cache."""
    if cache is None:
        return
    try:
        cache.parent.mkdir(parents=True, exist_ok=True)
        cache.write_text(json.dumps({"read": now.astimezone(UTC).isoformat(), "issues": issues}), encoding="utf-8")
    except OSError:
        return


# --- the output ------------------------------------------------------------------------------------------------


def report_json(
    touched: Touched, candidates: Sequence[Candidate], excluded: Sequence[dict[str, Any]], notices: Sequence[str]
) -> dict[str, Any]:
    """The machine-readable report (`--json`; `pr text` reads it): see `JSON_VERSION`."""
    return {
        "version": JSON_VERSION,
        "base": touched.base,
        "head": touched.head,
        "branch": touched.branch,
        "touched": {
            "paths": len(touched.paths),
            "addresses": len(touched.addresses),
            "names": len(touched.names),
            "sections": sum(len(anchors) for anchors in touched.sections.values()),
        },
        "issues": [candidate.to_json() for candidate in candidates],
        "excluded": list(excluded),
        "notices": [*touched.notices, *notices],
    }


def report_lines(
    candidates: Sequence[Candidate], excluded: Sequence[dict[str, Any]], notices: Sequence[str], max_reasons: int
) -> list[str]:
    """The report as text: one line per issue with why it matched, then what was left out and the notices."""
    lines = []
    for candidate in candidates:
        shown = [reason.text() for reason in candidate.reasons[:max_reasons]]
        more = len(candidate.reasons) - max_reasons
        if more > 0:
            shown.append(f"and {more} more")
        lines.append(f"#{candidate.number}  {candidate.title}  --  " + "; ".join(shown))
    lines.append(f"{len(candidates)} open issue(s) may be affected")
    for item in excluded:
        lines.append(f"left out: #{item['number']} ({item['why']})")
    lines.extend(f"note: {notice}" for notice in notices)
    return lines
