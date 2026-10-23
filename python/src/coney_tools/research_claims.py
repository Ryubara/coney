# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools research claims`: the graded blocks a branch added or changed on the research pages, for the reviewer.

A claim is a block (a paragraph, a list item or a table row) of a research page that carries an evidence level, in the
block itself (a row in its Evidence cell) or in the lead line of the H3-or-deeper heading it sits under, read exactly
as `docs check` reads them (coney_tools.research_evidence; docs/guides/writing-docs.md#checking).
For each, the list gives the page and line, the level, the addresses or script locators it cites, what backs it and the
tests that cite its section. What backs it:

* `confirmed (code)`: a `studied` stamp for each cited address in the unit's stamp log (the saved decompile is there
  to read the claim against), else `none`, which the reviewer checks in Ghidra.
* `confirmed (runtime)`: the run record the block cites (`run r-20261023-grab-reach`), which must exist and be a good
  record (coney_tools.run_records). A legacy claim that names the PCSX2 version and its method instead is allowed with
  a warning. There is no stamp for a run: the record is the proof (maintainer decision, 2026-10-23).
* `speculative`: the open question it links, which must exist when it is on the same page.
* `inferred`: nothing to look up (`docs check` holds its basis).

Not claims: the title, the Verified line, the sections of `research.ungraded_sections` in .github/conventions.yml (they
state nothing the port rests on), and a lead line itself. A block that cites an address and has no level anywhere is
counted, not listed: `docs check` reports it.

Research: docs/guides/research-workflow.md#run-records
"""

from __future__ import annotations

import json
import re
import subprocess
from collections.abc import Iterable
from dataclasses import dataclass, field
from pathlib import Path

from coney_tools import run_records, stamps
from coney_tools.config import ConfigError
from coney_tools.conventions import load_conventions
from coney_tools.docs_page import Page, heading_ids, read_page
from coney_tools.research_check import subject_of
from coney_tools.research_evidence import (
    QUESTION_LINK,
    graded_section,
    is_header_row,
    lead_lines,
    level_cell,
    level_column_of,
    levels_in,
)
from coney_tools.research_subject import ADDRESS, LOCATOR, ResearchEnv, Subject

#: The pages whose claims are listed, as repository-relative paths.
RESEARCH_DIR = "docs/research/"
#: How alike two blocks are (the share of their words in common) for the later one to count as an edit of the earlier.
SAME_BLOCK = 0.5
#: Where tests cite a claim's section: `// Expected: research/combat/grabs.md#grab-intro, confirmed (code)`.
TEST_FOLDERS = ("tests", "python/tests")
TEST_SUFFIXES = (".cpp", ".h", ".py")

_PCSX2_VERSION = re.compile(r"PCSX2\s+v?\d+\.\d+", re.IGNORECASE)
_EXPECTED = re.compile(r"research/(?P<page>[A-Za-z0-9_./-]+\.md)#(?P<anchor>[A-Za-z0-9_-]+)")
_HUNK = re.compile(r"^@@ -\d+(?:,\d+)? \+(?P<start>\d+)(?:,(?P<count>\d+))? @@")

CODE, RUNTIME, INFERRED, SPECULATIVE, NOT_NEEDED = "code", "runtime", "inferred", "speculative", "not-needed"
#: The levels in the order a claim lists them, with the words that name them (docs check's `levels_in` reads them).
_NAMES = {
    CODE: "confirmed (code)",
    RUNTIME: "confirmed (runtime)",
    INFERRED: "inferred",
    SPECULATIVE: "speculative",
    NOT_NEEDED: "not needed",
}


@dataclass(frozen=True)
class Claim:
    """One graded block of a page."""

    page: str
    line: int
    #: The levels the block carries, in the order they are written (`code`, `runtime`, `inferred`, `speculative`).
    levels: tuple[str, ...]
    #: `block` when the block states its level, `lead` when the heading's lead line does.
    via: str
    #: Addresses (`0x0026c548`) and script locators (`level99.lua:f31`) the block cites.
    targets: tuple[str, ...]
    #: The anchor of the nearest heading.
    section: str
    #: What backs it: `studied`, `run <id>`, `question <id>`, `legacy method`, `none` or `-` when nothing applies.
    backing: str
    #: The tests that cite its section, as `path:line`.
    tests: tuple[str, ...]
    #: The block's text, whitespace collapsed.
    text: str


@dataclass
class Report:
    """The claims of a branch and what is wrong with them."""

    pages: list[str] = field(default_factory=list)
    claims: list[Claim] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)
    #: Per page, the changed blocks that cite an address and carry no level.
    ungraded: dict[str, int] = field(default_factory=dict)


# --- what changed -----------------------------------------------------------------------------------------------------


def _git(root: Path, *args: str) -> str:
    """Run git in `root`; ConfigError with its message when it fails."""
    done = subprocess.run(["git", "-C", str(root), *args], capture_output=True, encoding="utf-8", check=False)
    if done.returncode != 0:
        raise ConfigError(f"git {' '.join(args)}: {done.stderr.strip() or 'failed'}")
    return done.stdout


def merge_base(root: Path, base: str) -> str:
    """The commit the branch left `base` at. Raises ConfigError for a `base` that is no commit."""
    try:
        return _git(root, "merge-base", base, "HEAD").strip()
    except ConfigError as error:
        raise ConfigError(f"{base}: not a commit to compare with ({error})") from error


def changed_lines(root: Path, point: str) -> dict[str, set[int] | None]:
    """The research pages the working tree changed since the commit `point`, each with its added or changed lines
    (None for a page with no earlier version: every line counts)."""
    out = _git(root, "diff", "--unified=0", "--no-color", "--no-ext-diff", "--no-renames", point, "--", RESEARCH_DIR)
    changed: dict[str, set[int] | None] = {}
    path: str | None = None
    for line in out.splitlines():
        if line.startswith("+++ "):
            name = line[4:].strip()
            path = name[2:] if name.startswith("b/") else None
            if path is not None and path.endswith(".md"):
                changed.setdefault(path, set())
            else:
                path = None
        elif path is not None and (hunk := _HUNK.match(line)):
            count = int(hunk.group("count") if hunk.group("count") is not None else 1)
            lines = changed[path]
            assert lines is not None
            lines.update(range(int(hunk.group("start")), int(hunk.group("start")) + count))
    for name in _git(root, "ls-files", "--others", "--exclude-standard", "--", RESEARCH_DIR).splitlines():
        if name.endswith(".md"):
            changed[name] = None
    return changed


def _all_pages(root: Path) -> dict[str, set[int] | None]:
    """Every research page, every line changed."""
    folder = root / RESEARCH_DIR
    return {p.relative_to(root).as_posix(): None for p in sorted(folder.rglob("*.md"))} if folder.is_dir() else {}


# --- what backs a claim -----------------------------------------------------------------------------------------------


def studied_addresses(folder: Path | None, root: Path) -> set[str]:
    """The addresses with a passing `studied` stamp in the log in `folder` (none without a log, or one that is not
    intact: a stamp in a broken chain proves nothing)."""
    if folder is None or not folder.is_dir():
        return set()
    report = stamps.verify(folder, load_conventions(root).stamps)
    if not report.ok:
        return set()
    done: set[str] = set()
    for stamp in report.stamps:
        newest = report.latest("studied", stamp.subject)
        if stamp.step == "studied" and newest is stamp and stamp.result == "pass":
            done.add(stamp.subject)
    return done


def test_citations(root: Path) -> dict[tuple[str, str], list[str]]:
    """The tests that cite a section: (page relative to docs/research, anchor) -> `path:line` of each `Expected:`."""
    cited: dict[tuple[str, str], list[str]] = {}
    for folder in TEST_FOLDERS:
        base = root / folder
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if path.suffix not in TEST_SUFFIXES or not path.is_file():
                continue
            try:
                lines = path.read_text(encoding="utf-8").splitlines()
            except (OSError, UnicodeDecodeError):
                continue
            for number, text in enumerate(lines, start=1):
                if "Expected:" in text:
                    for match in _EXPECTED.finditer(text):
                        key = (match.group("page"), match.group("anchor"))
                        cited.setdefault(key, []).append(f"{path.relative_to(root).as_posix()}:{number}")
    return cited


@dataclass
class _Backing:
    """What the claims of one run are checked against."""

    studied: set[str]
    records: dict[str, run_records.RunRecord]
    bad_records: dict[str, str]
    cited: dict[tuple[str, str], list[str]]
    require_study: bool


@dataclass(frozen=True)
class _Site:
    """A claim's block, for the checks: where it is, what grades it and whether it is new on the branch."""

    where: str
    #: The text that grades the block (its own, a row's Evidence cell, or the lead line), for a legacy claim's method.
    text: str
    levels: tuple[str, ...]
    targets: tuple[str, ...]
    #: The run ids the grading text cites (`confirmed (runtime), run <id>`).
    runs: tuple[str, ...]
    #: The open questions it links: (page, anchor), the page empty for this one.
    questions: tuple[tuple[str, str], ...]
    anchors: set[str]
    #: False when the block is an edit of one the base page had (a legacy claim touched), True for a new one.
    is_new: bool


def _back_runtime(report: Report, backing: _Backing, site: _Site) -> str:
    """What backs a `confirmed (runtime)` claim: the run records it cites. A claim that cites none is an error when it
    is new; a touched legacy claim keeps its level, with a warning, while its text names the PCSX2 version."""
    ids = list(dict.fromkeys(site.runs))
    where = site.where
    if not ids:
        if site.is_new:
            report.errors.append(f"{where}: a new confirmed (runtime) claim cites a run record (`run <id>`)")
            return "none"
        if _PCSX2_VERSION.search(site.text):
            report.warnings.append(f"{where}: confirmed (runtime) cites no run record (legacy: method in the text)")
            return "legacy method"
        report.warnings.append(f"{where}: confirmed (runtime) names no run record and no PCSX2 version (legacy)")
        return "none"
    for run_id in ids:
        if run_id in backing.records:
            if backing.records[run_id].source != "hook":
                report.warnings.append(f"{where}: run {run_id} was not named by the hook (who.source is unverified)")
        elif run_id in backing.bad_records:
            report.errors.append(f"{where}: run {run_id} is not a good record ({backing.bad_records[run_id]})")
        else:
            report.errors.append(f"{where}: run {run_id}: no run record {run_id} (research/runs/{run_id}.toml)")
    return "run " + ", ".join(ids)


def _back(report: Report, backing: _Backing, site: _Site) -> str:
    """What backs a claim of `site.levels`; problems go into `report`. The forms of the levels (an inferred claim's
    basis, a speculative one's link) are `docs check`'s; this says what each claim rests on."""
    if RUNTIME in site.levels:
        return _back_runtime(report, backing, site)
    if CODE in site.levels:
        addresses = [t for t in site.targets if t.startswith("0x")]
        if not addresses:
            return "-"
        done = [a for a in addresses if a in backing.studied]
        if len(done) == len(addresses):
            return "studied"
        if backing.require_study:
            left = ", ".join(a for a in addresses if a not in done)
            report.errors.append(f"{site.where}: confirmed (code) at {left} was not studied in this task")
        return f"studied {len(done)} of {len(addresses)}" if done else "none"
    if SPECULATIVE in site.levels:
        if not site.questions:
            report.warnings.append(f"{site.where}: speculative links no open question (q-... anchor)")
            return "none"
        page, question = site.questions[0]
        if not page and question not in site.anchors:
            report.errors.append(f"{site.where}: speculative links open question {question}, which is not on the page")
            return "none"
        return f"question {page}#{question}" if page else f"question {question}"
    return "-"


# --- the report -------------------------------------------------------------------------------------------------------


def _words(text: str) -> frozenset[str]:
    """The lower-case words of a block, to tell an edited block from a new one."""
    return frozenset(re.findall(r"\w+", text.lower()))


def _is_edit(text: str, known: list[frozenset[str]] | None) -> bool:
    """Whether a block shares at least half its words with a block of the base page: an edit of a claim that was there
    (legacy), not a new one. Without a base page every block is new."""
    words = _words(text)
    if not known or not words:
        return False
    return any(len(words & other) / len(words | other) >= SAME_BLOCK for other in known)


def _targets(text: str) -> tuple[str, ...]:
    """The addresses and script locators a block cites, in order, as `docs check` reads them."""
    found = [m.group(0).lower() for m in ADDRESS.finditer(text)] + [m.group(0) for m in LOCATOR.finditer(text)]
    return tuple(dict.fromkeys(found))


def _last_line(page: Page, index: int) -> int:
    """The last source line of block `index`."""
    block = page.blocks[index]
    return max((number for _, number in block.offsets), default=block.line)


def _enclosing(page: Page, index: int) -> list[int]:
    """Heading `index` and each shallower heading before it, nearest first: the sections a test may cite."""
    chain = [index]
    level = page.headings[index].level
    for position in range(index - 1, -1, -1):
        if page.headings[position].level < level:
            chain.append(position)
            level = page.headings[position].level
    return chain


def _page_claims(
    subject: Subject,
    lines: set[int] | None,
    report: Report,
    backing: _Backing,
    known: list[frozenset[str]] | None,
) -> None:
    """Add the claims of one page that touch `lines` (all of them when None) to `report`, graded as `docs check`
    grades them. `known` holds the word sets of the base page's blocks (None without a base)."""
    page, name = subject.page, subject.path
    leads = lead_lines(subject)
    map_section = subject.env.research.map_section
    docs_page = name.removeprefix(RESEARCH_DIR)
    ids = heading_ids(page)  # the ids MkDocs gives, so a second `## Purpose` is `purpose_1` as `Page.anchors` has it
    for index, block in enumerate(page.blocks):
        if block.kind == "heading" or is_header_row(subject, index) or not graded_section(subject, block.section):
            continue
        if lines is not None and not any(block.line <= n <= _last_line(page, index) for n in lines):
            continue
        lead = leads.get(block.section)
        if lead == index:
            continue
        column = level_column_of(subject, block)
        if block.kind == "row" and column is None and subject.sections[block.section] == map_section:
            continue  # a Map table with no level column indexes functions; their levels are in their entries
        grading = level_cell(subject, block)
        levels = levels_in(grading)
        targets = _targets(block.text)
        via = "block"
        if not levels.kinds:
            covered = block.kind != "row" or column is None
            if lead is None or not covered or not targets:
                if targets:
                    report.ungraded[name] = report.ungraded.get(name, 0) + 1
                continue
            # The lead line grades the block, and says what backs it (`Confirmed (runtime), run r-...`).
            grading, via = page.blocks[lead].text, "lead"
            levels = levels_in(grading)
        stated = tuple(level for level in _NAMES if level in levels.kinds)
        cited = backing.cited
        tests = tuple(
            dict.fromkeys(t for h in _enclosing(page, block.section) for t in cited.get((docs_page, ids[h]), []))
        )
        links = QUESTION_LINK.finditer(f"{block.text} {grading}")
        site = _Site(
            f"{name}:{block.line}",
            grading,
            stated,
            targets,
            levels.runs,
            tuple(dict.fromkeys((m.group("page"), m.group("anchor")) for m in links)),
            page.anchors,
            not _is_edit(block.text, known),
        )
        what = _back(report, backing, site)
        anchor = ids[block.section]
        report.claims.append(Claim(name, block.line, stated, via, targets, anchor, what, tests, block.text))


def build(
    root: Path,
    *,
    base: str | None,
    stamp_folder: Path | None = None,
    require_study: bool = False,
) -> Report:
    """The claims of the research pages changed since the branch left `base` (every page and line when None), checked
    against the run records of the checkout and the stamp log in `stamp_folder`. Raises ConfigError."""
    report = Report()
    records, problems = run_records.load_all(root)
    bad = {Path(p.split(":", 1)[0]).stem: p.split(": ", 1)[1] if ": " in p else p for p in problems}
    report.errors.extend(problems)
    env = ResearchEnv(load_conventions(root).research)
    backing = _Backing(studied_addresses(stamp_folder, root), records, bad, test_citations(root), require_study)
    point = None if base is None else merge_base(root, base)
    pages = _all_pages(root) if point is None else changed_lines(root, point)
    for page, lines in sorted(pages.items()):
        path = root / page
        if not path.is_file():
            continue
        report.pages.append(page)
        known = None if point is None else _base_blocks(root, point, page)
        subject, _ = subject_of(page, read_page(path.read_text(encoding="utf-8")), env)
        _page_claims(subject, lines, report, backing, known)
    return report


def _base_blocks(root: Path, point: str, page: str) -> list[frozenset[str]]:
    """The word sets of the blocks the page had at the commit `point` (none for a page that was not there)."""
    try:
        text = _git(root, "show", f"{point}:{page}")
    except ConfigError:
        return []
    return [_words(block.text) for block in read_page(text).blocks if block.kind != "heading"]


def _plural(count: int, noun: str) -> str:
    """`1 page`, `2 pages`."""
    return f"{count} {noun}{'' if count == 1 else 's'}"


def render(report: Report) -> str:
    """The report as text: the claims by page, then the problems and a summary line."""
    out: list[str] = []
    for page in dict.fromkeys(c.page for c in report.claims):
        out.append(page)
        for claim in (c for c in report.claims if c.page == page):
            level = " / ".join(_NAMES[x] for x in claim.levels) + (" (lead)" if claim.via == "lead" else "")
            row = f"  L{claim.line}  {level}  {', '.join(claim.targets) or '-'}  {claim.backing}"
            out.append(row + (f"  tests: {', '.join(claim.tests)}" if claim.tests else ""))
    out += [f"error: {e}" for e in report.errors]
    out += [f"warning: {w}" for w in report.warnings]
    ungraded = sum(report.ungraded.values())
    out.append(
        f"{_plural(len(report.claims), 'claim')} in {_plural(len({c.page for c in report.claims}), 'page')} "
        f"({_plural(len(report.pages), 'page')} changed); {_plural(len(report.errors), 'error')}, "
        f"{_plural(len(report.warnings), 'warning')}; {_plural(ungraded, 'address block')} with no level "
        "(`docs check` reports them)"
    )
    return "\n".join(out)


def render_json(report: Report) -> str:
    """The report as JSON, for a tool to read."""
    rows: Iterable[dict[str, object]] = (
        {
            "page": c.page,
            "line": c.line,
            "levels": list(c.levels),
            "via": c.via,
            "targets": list(c.targets),
            "section": c.section,
            "backing": c.backing,
            "tests": list(c.tests),
        }
        for c in report.claims
    )
    return json.dumps(
        {
            "pages": report.pages,
            "claims": list(rows),
            "errors": report.errors,
            "warnings": report.warnings,
            "ungraded": report.ungraded,
        },
        indent=2,
    )
