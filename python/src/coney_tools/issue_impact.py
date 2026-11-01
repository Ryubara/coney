# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools issue impact`: which open issues a branch may have changed.

Documented in docs/guides/coney-tools-issues.md#issue-impact.

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

The result is a suggestion for the author to read, never a verdict: the Issues affected section lists the issues a
person confirms. The open issues come from `issue_impact_open` (`gh` or its cache) and the output is built by
`issue_impact_report`. A page section is matched by the anchors of the head and of the base, so an issue citing a
heading the change removed or renamed still matches. A name or address that only a changed test file holds is not
counted (invented fixture values match real issues by chance).
"""

from __future__ import annotations

import re
import subprocess
from collections import Counter
from collections.abc import Collection, Iterable, Mapping, Sequence
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from coney_tools import audit_text, orig_review
from coney_tools.config import ConfigError
from coney_tools.conventions import ImpactRules
from coney_tools.issue_impact_open import OpenIssue
from coney_tools.task_leads import code_spans, is_code_like

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
_HUNK = re.compile(
    r"^@@ -(?P<old_start>\d+)(?:,(?P<old_count>\d+))? \+(?P<start>\d+)(?:,(?P<count>\d+))? @@ ?(?P<context>.*)$"
)
_DIFF_HEADER = re.compile(r"^diff --git a/(?P<old>.+) b/(?P<new>.+)$")
#: Folders are named by their first two components at the shortest (`src/ai`), never `src` alone.
_MIN_FOLDER_DEPTH = 2
#: A test file: invented fixture names and addresses in its changed lines say nothing about an issue's.
_TEST_FILE = re.compile(r"(?:^|/)(?:tests?|testing)/|(?:^|/)test_[^/]*$|_tests?\.\w+$")
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
    """Whether a word of a changed line reads like an identifier worth matching (`is_code_like`'s test)."""
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
    #: The new side's changed line ranges (first, last), and the old side's.
    ranges: list[tuple[int, int]] = field(default_factory=list)
    old_ranges: list[tuple[int, int]] = field(default_factory=list)
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
                old_start = int(hunk.group("old_start"))
                old_count = 1 if hunk.group("old_count") is None else int(hunk.group("old_count"))
                current.old_ranges.append((old_start, old_start + max(old_count, 1) - 1))
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
        _add_file(root, touched, diff, merge_base, head)
    _add_functions(root, touched, base, head)
    for text in (*messages, description):
        touched.fixes |= closing_numbers(text)
    touched.replaces = list(dict.fromkeys([*replaced_refs("\n".join(messages)), *replaces]))
    return touched


def diff_addresses(patch: str, only: Collection[str] | None = None) -> dict[str, str]:
    """The addresses a `git diff` patch holds in changed lines, each with the first file that holds it.

    The files and lines `gather` counts and no others: a test file's invented fixture values and generated or binary
    files say nothing about the original's addresses. Used for a pull request's diff, which is not a checkout, and for
    a unit's; `only`, when given, limits it to those (new) paths.
    """
    found: dict[str, str] = {}
    for diff in parse_diff(patch):
        if (only is not None and diff.new not in only) or _SKIP_CONTENT.search(diff.new) or _TEST_FILE.search(diff.new):
            continue
        for text in diff.text:
            for address in _ADDRESS.findall(text):
                found.setdefault(address.lower(), diff.new)
    return found


def _add_file(root: Path, touched: Touched, diff: _FileDiff, base: str, head: str) -> None:
    """Record one changed file: its paths, the addresses and names in its changed lines, its changed sections."""
    if diff.status == "renamed":
        touched.paths[diff.old] = f"renamed to {diff.new}"
        touched.paths[diff.new] = f"renamed from {diff.old}"
    else:
        touched.paths[diff.new] = diff.status
    if _SKIP_CONTENT.search(diff.new):
        return
    if not _TEST_FILE.search(diff.new):
        for text in diff.text:
            for address in _ADDRESS.findall(text):
                touched.addresses.setdefault(address.lower(), f"a changed line of {diff.new} holds it")
            for name in _names_in_line(text):
                touched.names.setdefault(name, diff.new)
    if diff.new.endswith(".md") and diff.status != "deleted":
        anchors = _changed_anchors(root, head, diff.new, diff.ranges) | _changed_anchors(
            root, base, diff.old, diff.old_ranges
        )
        if anchors:
            touched.sections[diff.new] = frozenset(anchors)


def _changed_anchors(root: Path, ref: str, path: str, ranges: Sequence[tuple[int, int]]) -> set[str]:
    """The anchors of the sections of `path` at `ref` that the changed line `ranges` fall in (and their parents).

    Read from the base too, so a heading the change removed or renamed is still known by its old anchor.
    """
    if not ranges:
        return set()
    try:
        page = git_output(root, "show", f"{ref}:{path}")
    except ConfigError:
        return set()
    spans = audit_text.heading_spans(page)
    anchors: set[str] = set()
    for first, last in ranges:
        for line in range(first, last + 1):
            anchors |= _heading_chain(spans, line)
    return anchors


def _add_functions(root: Path, touched: Touched, base: str, head: str) -> None:
    """Record the `@orig` address and name of every function whose body or tags the branch changed, and of every one
    that only moved (an issue that cites its address is still about code the branch touched)."""
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
    for moved in stamp["moved"]:
        note = f"the @orig of {moved['coney']} ({moved['where']}), which moved with its code as it was"
        for address in moved["addresses"]:
            touched.addresses.setdefault(address.lower(), note)


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
