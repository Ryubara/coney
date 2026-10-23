# SPDX-License-Identifier: GPL-3.0-or-later
"""The fidelity audit's queue: which reimplemented functions to compare with the original first.

`coney-tools orig queue` (docs/guides/coney-tools-fidelity.md#orig) ranks every original function Coney tags, and every
gameplay function it neither tags nor marks Coney-only, by what makes a difference from the original likely or
costly:

* open `bug` issues that name the function, its address, its file or its research section (a high-priority issue
  weighs more), and those whose area label covers its directory;
* a tag Ghidra does not bear out (no function there, inside another function, another function's name);
* research that grades the function only inferred or speculative, or does not cite it;
* code with no tag at all, which nobody has compared with anything;
* the orig review's ledger (orig_review.py): a function not yet reviewed, changed since its review, or reviewed
  with a finding (a bug, a transcription, a wrong address, a missing comment, a question for a runtime run) moves
  up; one reviewed clean, or different on purpose with the comment that says so, leaves the queue until it changes.

The weights are a starting order, not a measure; the audit team re-ranks as it learns. The queue holds addresses,
names and issue numbers only and is written to the scratch folder, never committed.
"""

from __future__ import annotations

import json
import re
import subprocess
from collections import defaultdict
from collections.abc import Iterable
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath
from typing import Any

from coney_tools.config import ConfigError
from coney_tools.orig_ghidra import GhidraFinding
from coney_tools.orig_index import IndexRow, Untagged, weakest_evidence

#: Which `src/` directories an area label covers (the labels' descriptions in .github/conventions.yml).
AREA_DIRS: dict[str, tuple[str, ...]] = {
    "A-ai": ("ai",),
    "A-combat": ("combat", "human"),
    "A-missions": ("scripting", "gamemodes", "warriors"),
    "A-hud": ("hud", "gui"),
    "A-graphics": ("graphics", "effects", "animation", "characters"),
    "A-audio": ("audio",),
    "A-physics": ("physics", "raycast", "world_objects", "world"),
    "A-camera": ("camera", "scenes", "movies"),
    "A-platform": ("platform", "fileio"),
    "A-core": ("core", "scripting"),
    "A-tools": ("debug", "sandbox"),
}
PRIORITY_WEIGHT = {"P-high": 3, "P-medium": 2, "P-low": 1}
#: Points for each reason; see the module docstring.
WEIGHTS = {
    "issue-names-it": 10,
    "issue-names-file": 3,
    "issue-names-section": 3,
    "issue-area": 1,
    "no-function": 30,
    "not-a-start": 30,
    "names-differ": 15,
    "renamed": 3,
    "unnamed": 1,
    "speculative": 8,
    "inferred": 5,
    "uncited": 4,
    "none": 2,
    "section-cites-nothing": 2,
    "page-lacks-address": 2,
    "untagged": 6,
    "untagged-helper": 2,
    "untagged-inline": 0,
    "unreviewed": 3,
    "stale": 5,
    "diff-bug": 40,
    "wrong-address": 30,
    "transcription": 25,
    "diff-uncommented": 10,
    "unclear": 6,
}
#: What the queue says for each review status (orig_review.status_of) that keeps a function in it.
REVIEW_REASONS = {"unreviewed": "not reviewed", "stale": "changed since its review"}
#: The most the area labels of all open issues add to one entry.
AREA_CAP = 4


@dataclass(frozen=True)
class Issue:
    """An open bug issue, reduced to what the matching reads."""

    number: int
    title: str
    text: str  # title, body and comments
    labels: tuple[str, ...]
    #: The names, file names and addresses the text holds (`Brain::think`, `brain.cpp`, `0x00100010`), read once so
    #: that matching thousands of entries is a set lookup, not a regular expression over every issue.
    tokens: frozenset[str] = frozenset()
    folded: str = ""  # the text in lower case, for research links

    def __post_init__(self) -> None:
        object.__setattr__(self, "tokens", frozenset(_TOKEN.findall(self.text)))
        object.__setattr__(self, "folded", self.text.casefold())

    @property
    def weight(self) -> int:
        """How much one mention by this issue counts: its priority label's weight, 1 without one."""
        return max((PRIORITY_WEIGHT.get(label, 0) for label in self.labels), default=0) or 1

    def dirs(self) -> set[str]:
        """The `src/` directories its area labels cover."""
        return {d for label in self.labels for d in AREA_DIRS.get(label, ())}


@dataclass
class Entry:
    """One line of the queue: an original function with its Coney sites, or an untagged Coney function."""

    key: str  # "0x002b2e28" or the untagged function's "src/...:line"
    title: str  # "FightGoal_TryTackle" or "Fighter::combatInput (untagged)"
    directory: str
    sites: list[str] = field(default_factory=list)  # Coney file:line of each tag, or the function's own
    reasons: list[str] = field(default_factory=list)  # "#231 names it", "inferred", ...
    issues: set[int] = field(default_factory=set)
    score: int = 0

    def add(self, reason: str, points: int) -> None:
        """Count one reason."""
        if points:
            self.reasons.append(reason)
            self.score += points


def issues_from_json(data: Iterable[dict[str, Any]]) -> list[Issue]:
    """Issues from `gh issue list --json number,title,body,labels,comments` output."""
    issues = []
    for item in data:
        comments = " ".join(str(c.get("body", "")) for c in item.get("comments") or [])
        labels = tuple(str(label["name"]) for label in item.get("labels") or [])
        text = f"{item.get('title', '')}\n{item.get('body', '')}\n{comments}"
        issues.append(Issue(int(item["number"]), str(item.get("title", "")), text, labels))
    return issues


def fetch_issues(label: str = "bug") -> list[Issue]:
    """The open issues with `label`, through gh."""
    command = ["gh", "issue", "list", "--label", label, "--state", "open", "--limit", "1000"]
    command += ["--json", "number,title,body,labels,comments"]
    try:
        done = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", check=True)
    except (OSError, subprocess.CalledProcessError) as error:
        raise ConfigError(f"gh issue list failed ({error}); pass --issues FILE to rank without GitHub") from error
    return issues_from_json(json.loads(done.stdout))


#: A name (`Brain::think`, `~Brain`, `FightGoal_TryTackle`), a file name (`brain.cpp`) or an address.
_TOKEN = re.compile(r"0x[0-9a-fA-F]{8}\b|~?[A-Za-z_]\w*(?:::~?[A-Za-z_]\w*)*(?:\.(?:cpp|hpp|h)\b)?")


def _names_of_row(row: IndexRow) -> list[str]:
    """The names an issue may use for a tagged function: the original's name and the Coney function's."""
    names = []
    original = row.name.split()[0] if row.name.split() else ""
    if len(original) >= 6:
        names.append(original)
    if "::" in row.coney or len(row.coney) >= 12:
        names.append(row.coney)
    return names


def _file_terms(path: str) -> list[str]:
    """How an issue may name a Coney file: `brain.cpp` or `brain.h` (a tag in the header documents the definition in
    the source file), or the stem when it is distinctive (`fighter_grab`)."""
    pure = PurePosixPath(path)
    terms = [f"{pure.stem}.cpp", f"{pure.stem}.h"]
    if "_" in pure.stem and len(pure.stem) >= 8:
        terms.append(pure.stem)
    return terms


def _mentions(issues: list[Issue], names: Iterable[str], links: Iterable[str] = ()) -> list[Issue]:
    """The issues that hold any of `names` as a whole token, or any research link of `links` (`ai.md#brain`, not
    followed by more of an anchor)."""
    wanted = set(names)
    patterns = [re.compile(re.escape(link.casefold()) + r"(?![\w-])") for link in links]
    return [i for i in issues if wanted & i.tokens or any(p.search(i.folded) for p in patterns)]


def _directory(path: str) -> str:
    """The `src/` directory of a repository path."""
    parts = PurePosixPath(path).parts
    return parts[1] if len(parts) > 2 else ""


def _issue_reasons(entry: Entry, issues: list[Issue], names: list[str], files: list[str], sections: list[str]) -> None:
    """Score what the open bug issues say about an entry."""
    keys = [entry.key.casefold(), entry.key] if entry.key.startswith("0x") else []
    named = _mentions(issues, [*names, *keys])
    for issue in named:
        entry.add(f"#{issue.number} names it", WEIGHTS["issue-names-it"] * issue.weight)
        entry.issues.add(issue.number)
    seen = {i.number for i in named}
    by_file = _mentions([i for i in issues if i.number not in seen], files)
    for issue in by_file:
        entry.add(f"#{issue.number} names its file", WEIGHTS["issue-names-file"] * issue.weight)
        entry.issues.add(issue.number)
    seen |= {i.number for i in by_file}
    links = [s.removeprefix("docs/research/") for s in sections if "#" in s]
    by_section = _mentions([i for i in issues if i.number not in seen], [], links) if links else []
    for issue in by_section:
        entry.add(f"#{issue.number} cites its research", WEIGHTS["issue-names-section"] * issue.weight)
        entry.issues.add(issue.number)
    seen |= {i.number for i in by_section}
    # An area label only says "somewhere in this directory": all of them together count at most AREA_CAP.
    by_area = [i for i in issues if i.number not in seen and entry.directory in i.dirs()]
    if by_area:
        points = min(sum(WEIGHTS["issue-area"] * i.weight for i in by_area), AREA_CAP)
        entry.add("area of " + " ".join(f"#{i.number}" for i in by_area), points)
        entry.issues.update(i.number for i in by_area)


@dataclass
class QueueInputs:
    """Everything the ranking reads."""

    rows: list[IndexRow]
    untagged: list[Untagged]
    issues: list[Issue]
    findings: list[GhidraFinding] = field(default_factory=list)
    evidence: dict[int, set[str]] = field(default_factory=dict)
    empty_sections: set[str] = field(default_factory=set)  # research links whose section cites no address
    uncited: set[str] = field(default_factory=set)  # IndexRow.where of tags whose linked pages lack the address
    #: Each tagged address -> its review status (orig_review.address_status); an address not in it is unreviewed.
    reviews: dict[int, str] = field(default_factory=dict)


def review_done(status: str) -> bool:
    """Whether a review status takes a function off the queue: reviewed clean or deliberately different (with
    its comment) at its current body."""
    return status in ("clean", "diff-deliberate-ok")


def add_review(entry: Entry, status: str) -> None:
    """Count an address's review status in its entry."""
    if not review_done(status) and status in WEIGHTS:
        entry.add(REVIEW_REASONS.get(status, f"review: {status}"), WEIGHTS[status])


def rank(inputs: QueueInputs) -> list[Entry]:
    """The queue, highest score first (ties by key)."""
    by_address: dict[int, list[IndexRow]] = defaultdict(list)
    for row in inputs.rows:
        by_address[row.address].append(row)
    findings: dict[int, list[GhidraFinding]] = defaultdict(list)
    for finding in inputs.findings:
        findings[finding.row.address].append(finding)
    entries = []
    for address, rows in by_address.items():
        review = inputs.reviews.get(address, "unreviewed")
        if review_done(review):
            continue
        name = rows[0].name.split()[0] if rows[0].name.split() else f"0x{address:08x}"
        entry = Entry(f"0x{address:08x}", name, rows[0].directory, [r.where for r in rows])
        names = sorted({n for r in rows for n in _names_of_row(r)})
        files = sorted({t for r in rows for t in _file_terms(r.path)})
        sections = sorted({s for r in rows for s in r.research})
        _issue_reasons(entry, inputs.issues, names, files, sections)
        for problem in sorted({f.problem for f in findings[address]}):
            ghidra = next(f.ghidra_name for f in findings[address] if f.problem == problem)
            entry.add(f"ghidra {problem}" + (f" ({ghidra})" if ghidra else ""), WEIGHTS[problem])
        grade = weakest_evidence(inputs.evidence.get(address))
        if grade != "confirmed":
            entry.add(f"research {grade}", WEIGHTS[grade])
        if any(s in inputs.empty_sections for s in sections):
            entry.add("linked section cites no address", WEIGHTS["section-cites-nothing"])
        if any(r.where in inputs.uncited for r in rows):
            entry.add("linked page lacks this address", WEIGHTS["page-lacks-address"])
        add_review(entry, review)
        entries.append(entry)
    for function in inputs.untagged:
        if function.exempt:
            continue  # needs neither a tag nor a marker (orig_index.is_exempt)
        path = function.where.rsplit(":", 1)[0]
        entry = Entry(
            function.where, f"{function.coney} (untagged {function.kind})", _directory(path), [function.where]
        )
        weight = {"helper": "untagged-helper", "inline": "untagged-inline"}.get(function.kind, "untagged")
        entry.add(
            f"no @orig ({function.lines} lines)",
            WEIGHTS[weight] + (min(function.lines // 20, 5) if weight == "untagged" else 0),
        )
        names = [function.coney] if "::" in function.coney or len(function.coney) >= 12 else []
        _issue_reasons(entry, inputs.issues, names, _file_terms(path), [])
        entries.append(entry)
    entries.sort(key=lambda e: (-e.score, e.key))
    return entries


@dataclass(frozen=True)
class SubsystemRow:
    """One `src/` directory's line of the queue's summary."""

    directory: str
    score: int
    issues: tuple[int, ...]
    tagged: int  # original functions tagged there
    untagged: int  # untagged functions, inline accessors left out
    weak: int  # tagged functions whose research is inferred, speculative or missing
    ghidra: int  # tagged functions Ghidra does not bear out (renames and unnamed left out)


def subsystems(entries: list[Entry]) -> list[SubsystemRow]:
    """The queue summed per `src/` directory, highest score first."""
    rows: dict[str, dict[str, Any]] = defaultdict(lambda: {"score": 0, "issues": set(), "t": 0, "u": 0, "w": 0, "g": 0})
    for entry in entries:
        row = rows[entry.directory]
        row["score"] += entry.score
        row["issues"] |= entry.issues
        if entry.key.startswith("0x"):
            row["t"] += 1
            row["w"] += any(r.startswith("research ") for r in entry.reasons)
            row["g"] += any(r.startswith(("ghidra no-", "ghidra not-", "ghidra names-")) for r in entry.reasons)
        elif "untagged inline" not in entry.title:
            row["u"] += 1
    result = [
        SubsystemRow(d, v["score"], tuple(sorted(v["issues"])), v["t"], v["u"], v["w"], v["g"]) for d, v in rows.items()
    ]
    result.sort(key=lambda r: (-r.score, r.directory))
    return result


def render_queue(entries: list[Entry], summary: list[SubsystemRow], shown: int) -> str:
    """The queue as Markdown: the per-directory summary, then the first `shown` entries."""
    lines = [
        "# Fidelity audit queue",
        "",
        "Generated by `coney-tools orig queue`; not committed. Scores are a starting order (orig_queue.WEIGHTS).",
        "",
        "## By directory",
        "",
        "| Directory | Score | Open bug issues | Tagged | Untagged | Weak research | Ghidra problems |",
        "| --- | ---: | --- | ---: | ---: | ---: | ---: |",
    ]
    for row in summary:
        issues = " ".join(f"#{n}" for n in row.issues)
        lines.append(
            f"| {row.directory or '(src)'} | {row.score} | {issues} | {row.tagged} | {row.untagged} | {row.weak} | "
            f"{row.ghidra} |"
        )
    lines += ["", f"## Functions (first {min(shown, len(entries))} of {len(entries)})", ""]
    lines += ["| # | Score | Function | Key | Coney | Why |", "| ---: | ---: | --- | --- | --- | --- |"]
    for number, entry in enumerate(entries[:shown], start=1):
        sites = ", ".join(entry.sites[:3]) + (f" (+{len(entry.sites) - 3})" if len(entry.sites) > 3 else "")
        why = "; ".join(entry.reasons)
        lines.append(f"| {number} | {entry.score} | {entry.title} | {entry.key} | {sites} | {why} |")
    return "\n".join(lines) + "\n"


def render_queue_tsv(entries: list[Entry]) -> str:
    """Every entry, one per line, for tools: score, key, title, directory, sites, issues, reasons."""
    lines = ["score\tkey\ttitle\tdirectory\tsites\tissues\treasons"]
    for e in entries:
        issues = ",".join(str(n) for n in sorted(e.issues))
        lines.append(
            f"{e.score}\t{e.key}\t{e.title}\t{e.directory}\t{','.join(e.sites)}\t{issues}\t{'; '.join(e.reasons)}"
        )
    return "\n".join(lines) + "\n"


def load_issues_file(path: Path) -> list[Issue]:
    """Issues saved from `gh issue list --json number,title,body,labels,comments`."""
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ConfigError(f"{path}: cannot be read as gh's issue JSON ({error})") from error
    if not isinstance(data, list):
        raise ConfigError(f"{path}: expected a JSON list of issues")
    return issues_from_json(data)
