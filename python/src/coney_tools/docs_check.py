# SPDX-License-Identifier: GPL-3.0-or-later
"""The page standards of docs/guides/writing-docs.md, checked by program: `coney-tools docs check`.

A page is held to the rules of its kind (`kind_of`): a research page keeps the research standard (front matter, the
sections of its kind, a level beside every address it cites, addresses that resolve; `research_check`), and every
hand-written page keeps the Style rules that a program can see (no machine paths, no time-bound wording, `repo:` links,
roles not people, no repository or site address). An engine page (`docs/engine/`, `docs_engine`) keeps those and,
because it cites Coney's code against the original, the history, address and landing-page rules. The check runs on the
pages a change touches and on those only, and on a page that was there before only in the sections the change touched
(`standards.touch`): a finding elsewhere on the page is reported as `info` and fails nothing, a new page meets the
standard whole. A research page that cannot meet the standard yet is listed in the legacy file (`research_legacy`),
which excuses it from the rules it fails outside the sections a change touches.

Everything here is a function of the page text, so the tests use made-up pages. The heuristics are deliberately
forgiving (they flag what is surely a gap, not what might be one); each is described where it is implemented and in
docs/guides/coney-tools-build.md#docs.
"""

from __future__ import annotations

import bisect
import dataclasses
import posixpath
from collections import Counter
from collections.abc import Callable, Iterable
from dataclasses import dataclass, field
from pathlib import Path

from coney_tools import docs_legacy, local_gate, source_files, standards_check
from coney_tools.audit_files import repository_address_terms
from coney_tools.config import ConfigError
from coney_tools.conventions import Research, TouchScope, load_conventions_leniently
from coney_tools.docs_engine import FOLDER as ENGINE_FOLDER
from coney_tools.docs_engine import INDEX as ENGINE_INDEX
from coney_tools.docs_engine import check_engine, index_finding
from coney_tools.docs_link_moves import touched_lines
from coney_tools.docs_link_resolve import link_resolver
from coney_tools.docs_page import Finding, Page, kind_of, read_page
from coney_tools.docs_style import check_style
from coney_tools.research_catalog import load_catalog
from coney_tools.research_check import check_research
from coney_tools.research_functions import ResearchFunctionsError, load_all
from coney_tools.research_functions_check import entries_by_page
from coney_tools.research_legacy import Touch, apply_legacy, load_legacy
from coney_tools.research_pages import Corpus
from coney_tools.research_subject import ResearchEnv

__all__ = ["Context", "Finding", "Result", "check_page", "kind_of"]

#: The kinds of file `kind_of` tells apart. Only the first four are checked.
CHECKED_KINDS: tuple[str, ...] = ("research", "engine", "guide", "page")
#: Lines above which a page gets a length warning (never a failure). A reader takes a page in whole before changing
#: it, and the Read tool shows 2,000 lines at a time; a guide past 800 (the top tenth, median 260) is several guides.
#: A warning, because splitting a page is the author's call: the cut is usually along a sub-subject that deserves its
#: own page. A research page's limit is its kind's `max_lines` in `.github/conventions.yml`.
LENGTH_WARNING: dict[str, int] = {"guide": 800, "engine": 800}


@dataclass(frozen=True)
class Context:
    """What the rules take from the repository: the words that must not name a person (`repo.wording.avoid`), the
    lower-case strings that spell the repository's and the docs site's address and, for research pages, everything the
    research standard checks against (`ResearchEnv`; without it a research page is held to the Style rules only) and how
    far the touch-it rule reaches (`standards.touch`; without it a touched page is held whole)."""

    avoid: tuple[str, ...] = ()
    address_terms: tuple[str, ...] = ()
    research: ResearchEnv | None = None
    touch: TouchScope | None = None


@dataclass
class Result:
    """The outcome of checking a set of pages: the pages in order, and the findings of those that have any."""

    pages: list[str] = field(default_factory=list)
    findings: dict[str, list[Finding]] = field(default_factory=dict)
    #: How many of the pages are listed in the legacy file.
    legacy: int = 0

    @property
    def failed(self) -> dict[str, list[Finding]]:
        """The pages with at least one error, with all their findings (warnings too)."""
        return {page: found for page, found in self.findings.items() if any(item.severity == "error" for item in found)}

    def by_kind(self) -> dict[str, tuple[int, int]]:
        """For each kind of page checked: how many pass (no error) and how many there are."""
        failed = self.failed
        totals: dict[str, list[int]] = {}
        for page in self.pages:
            if kind_of(page) not in CHECKED_KINDS:
                continue
            counts = totals.setdefault(kind_of(page), [0, 0])
            counts[1] += 1
            counts[0] += page not in failed
        return {kind: (counts[0], counts[1]) for kind, counts in totals.items()}

    def rule_counts(self) -> Counter[str]:
        """For each rule, the number of pages with an error of that rule."""
        counts: Counter[str] = Counter()
        for found in self.findings.values():
            counts.update({item.rule for item in found if item.severity == "error"})
        return counts


def _check_length(kind: str, text: str) -> Iterable[Finding]:
    """A warning when a guide is longer than `LENGTH_WARNING` allows (a research page's limit is its kind's)."""
    limit = LENGTH_WARNING.get(kind)
    count = len(text.splitlines())
    if limit and count > limit:
        yield Finding(
            "length",
            1,
            f"{count} lines is over the {limit} of a {kind} page: split off the sub-subject that has its own page",
            "warning",
        )


# --- the research standard and the legacy file -----------------------------------------------------------------------


def _touched(page: Page, lines: frozenset[int], level: int) -> Callable[[Finding], bool]:
    """A test for "this finding is in a section the change touched": the sections start at the headings of `level` or
    above (and the title's part above the first is one), and `lines` are the lines the change added or changed."""
    starts = [heading.line for heading in page.headings if heading.level <= level] or [1]
    changed = {max(bisect.bisect_right(starts, number) - 1, 0) for number in lines}
    return lambda finding: max(bisect.bisect_right(starts, finding.line) - 1, 0) in changed


def _untouched(finding: Finding) -> bool:
    """The test of a moved page that the change touched nowhere: no finding is in a touched section."""
    return False


def _check_research(
    path: str, page: Page, env: ResearchEnv, touch: Touch, style: list[Finding], level: int
) -> list[Finding]:
    """The research standard for `path` and its `style` findings, with the legacy file's excuses applied. A change that
    touches a listed page ends the excuse in the sections (headings of `level` or above) it touches, the length aside
    (`apply_legacy`). A page git sees as moved is read from the lines git's diff of the renamed pair changes, but its
    entry excuses the grading alone, outside the sections the move changed: every other rule it lists is checked in
    full, the length too."""
    moved = touch.moved_from is not None
    excused = env.legacy.rules(path)
    if moved:
        excused = {rule: count for rule, count in excused.items() if rule == "evidence"}
    listed = bool(excused)
    findings = [*check_research(path, page, env, legacy_page=listed), *style]
    if not listed:
        return findings
    if touch.new:
        findings.append(
            Finding(
                "legacy",
                1,
                f"a new page cannot be legacy: meet the standard and take it out of {env.research.files.legacy}",
            )
        )
        return findings
    touched: Callable[[Finding], bool] | None = _touched(page, touch.lines, level) if touch.lines else None
    if moved and touched is None:
        # A move that changed no line still ends the excuse of everything but the grading of its sections.
        touched = _untouched
    return apply_legacy(findings, excused, env.research.files.legacy, touched, scoped=not moved)


def _scope(found: list[Finding], page: Page, touch: Touch | None, scope: TouchScope | None) -> list[Finding]:
    """The touch-it rule's reach on a page that was there before the change: an error outside the sections the change
    touched becomes `info`, which fails nothing, unless its rule is one of `scope.page_rules`. A warning stays one (it
    fails nothing either, and a legacy page's excuses read as warnings). A new page, a page git sees as moved
    (`_check_research` reads it) and a page checked by name are held whole."""
    if touch is None or touch.new or touch.moved_from is not None or touch.lines is None or scope is None:
        return found
    inside = _touched(page, touch.lines, scope.section_level)
    return [
        item
        if item.severity != "error" or item.rule in scope.page_rules or inside(item)
        else dataclasses.replace(item, severity="info")
        for item in found
    ]


# --- checking ---------------------------------------------------------------------------------------------------------


def check_page(path: str, text: str, context: Context | None = None, touch: Touch | None = None) -> list[Finding]:
    """Every finding for the page at `path` (relative to the repository, forward slashes) with content `text`, by line.
    A file that is not a checked page (`kind_of`) has none. `touch` says what the change did to the page, which decides
    how strictly a legacy page is read."""
    kind = kind_of(path)
    if kind not in CHECKED_KINDS:
        return []
    context = context or Context()
    page = read_page(text)
    found = list(check_style(path, page, kind, context.avoid, context.address_terms))
    if kind == "research" and context.research is not None:
        # Without the touch-it scope a legacy page is read by level-2 sections, the scope's own default.
        level = context.touch.section_level if context.touch is not None else 2
        found = _check_research(path, page, context.research, touch or Touch(), found, level)
    if kind == "engine":
        found.extend(check_engine(path, page, context.research))
    if kind != "research":
        found.extend(_check_length(kind, text))
    return sorted(_scope(found, page, touch, context.touch), key=lambda item: item.line)


def context_for(root: Path) -> Context:
    """The `Context` of the checkout at `root`: the wording to avoid, the address of the repository and the research
    standard with the data it checks against."""
    # Lenient: `task review` runs this on a branch cut before main added a key; main's value fills it (#1355).
    conventions = load_conventions_leniently(root)
    research = conventions.research
    try:
        systems = load_all(root, Path(research.files.functions))
    except ResearchFunctionsError:
        systems = []  # `research render --check` reports a broken database; the pages are checked without it
    try:
        ranges = source_files.load_ranges(root, Path(research.files.source_files))
    except source_files.SourceFilesError:
        ranges = ()  # `research render --check` reports a broken file; the pages are checked without it
    env = ResearchEnv(
        research,
        load_catalog(root, research),
        Corpus.from_root(root, research),
        load_legacy(root, research, docs_legacy.LEGACY_RULES),
        functions=entries_by_page(systems),
        source_files=ranges,
    )
    return Context(
        conventions.repo.wording.avoid, tuple(repository_address_terms(root)), env, conventions.standards.touch
    )


def check_files(root: Path, names: Iterable[str], context: Context, touches: dict[str, Touch] | None = None) -> Result:
    """Check each of `names` (repository-relative, forward slashes) that is a checked page and exists."""
    result = Result()
    for name in names:
        if kind_of(name) not in CHECKED_KINDS or not (root / name).is_file():
            continue
        result.pages.append(name)
        if context.research is not None and name in context.research.legacy:
            result.legacy += 1
        text = (root / name).read_text(encoding="utf-8", errors="replace")
        found = check_page(name, text, context, (touches or {}).get(name))
        if kind_of(name) == "engine" and not (root / ENGINE_INDEX).is_file():
            found = sorted([*found, index_finding()], key=lambda item: item.line)
        if found:
            result.findings[name] = found
    return result


def change_touches(root: Path, base: str, names: Iterable[str], similarity: int | None = None) -> dict[str, Touch]:
    """For each checked page among `names`, what the change since HEAD left `base` did to it: whether it is new, the
    page it was moved from when git sees a rename at or above `similarity` percent (the research conventions'
    `moved_similarity`; git's own 50 when None), and which lines it added or changed (committed or not).

    A line whose only change is a link rewritten to a page the change moved, or a repository path repointed to where a
    file the change renamed went, is no touch (`docs_link_moves`)."""
    since = local_gate.merge_base(root, base)
    moves = local_gate.renamed_files(root, base, similarity)
    gone = local_gate.removed_files(root, base, similarity)
    resolves = link_resolver(root)
    held = link_resolver(root, since)
    # A `repo:` path follows a renamed file only to where git says it went (old path -> new).
    renamed = {old: new for new, old in moves.items()}
    touches: dict[str, Touch] = {}
    for name in names:
        if kind_of(name) not in CHECKED_KINDS or not (root / name).is_file():
            continue
        if name in moves:
            # A moved page is read line by line from git's diff of the renamed pair, so only what the move really
            # changed is touched; its links are read from the folder it left.
            old = moves[name]
            detect = "-M" if similarity is None else f"-M{similarity}%"
            diff = local_gate.run_command(
                ["git", "-C", str(root), "diff", "-U0", detect, since, "--", old, name], root
            )[1]
            folders = (posixpath.dirname(name), posixpath.dirname(old))
            lines = touched_lines(diff, folders[0], gone, resolves, folders[1], _file_lines(root, name), held, renamed)
            touches[name] = Touch(lines, moved_from=old)
            continue
        code, _ = local_gate.run_command(["git", "-C", str(root), "cat-file", "-e", f"{since}:{name}"], root)
        if code != 0:
            touches[name] = Touch(None, True)
            continue
        diff = local_gate.run_command(["git", "-C", str(root), "diff", "-U0", since, "--", name], root)[1]
        lines = touched_lines(
            diff, posixpath.dirname(name), gone, resolves, None, _file_lines(root, name), held, renamed
        )
        touches[name] = Touch(lines)
    return touches


def _file_lines(root: Path, name: str) -> list[str]:
    """The lines of the page `name` as the checkout holds it, which the diff of the change is read against."""
    return (root / name).read_text(encoding="utf-8", errors="replace").splitlines()


#: What a change can touch that moves the legacy file's truth without editing the file: a research page, the
#: conventions and the code of the checks. The data files the checks read are named by `research.files`.
_LEGACY_INPUTS = ("docs/research/", ".github/conventions.yml", "python/src/coney_tools/research_")


def legacy_relevant(names: Iterable[str], research: Research) -> bool:
    """Whether the change can make the legacy file stale: it touches a research page, one of the data files or folders
    the research checks read (`research.files`: the function listing, the sections, the run records, the function
    database and the legacy file itself), the conventions or the checks themselves."""
    files = research.files
    data = (files.ghidra_functions, files.elf_sections, files.legacy)
    folders = (f"{files.runs.rstrip('/')}/", f"{files.functions.rstrip('/')}/")
    return any(
        name.startswith(_LEGACY_INPUTS)
        or name in data
        or name.startswith(folders)
        or (name.startswith("python/src/coney_tools/docs_") and name.endswith(".py"))
        for name in names
    )


def check_changed(root: Path, base: str, context: Context) -> Result:
    """Check the pages changed since the branch left `base`, committed or not, untracked pages included. When the change
    can make the legacy file stale (`legacy_relevant`), the file is checked as a whole too (`docs_legacy.problems`)."""
    names = local_gate.changed_files(root, base)
    similarity = context.research.research.moved_similarity if context.research is not None else None
    result = check_files(root, names, context, change_touches(root, base, names, similarity))
    _check_engine_index(result, root, base)
    if context.research is not None and legacy_relevant(names, context.research.research):
        # A page that now passes must leave the file, and a changed file may only have shrunk.
        found = [Finding("legacy", 1, line) for line in docs_legacy.problems(root, context, base)]
        if found:
            legacy_file = context.research.research.files.legacy
            result.pages.append(legacy_file)
            result.findings[legacy_file] = found
    if context.research is not None:
        _add_standards(result, root, base, names)
    return result


def _check_engine_index(result: Result, root: Path, base: str) -> None:
    """A change that deletes (or moves away) the engine section's landing page while other engine pages remain fails on
    the landing page's path: no page of the change names it, so the per-page check cannot see it."""
    folder = root / ENGINE_FOLDER
    if (root / ENGINE_INDEX).is_file():
        return
    since = local_gate.merge_base(root, base)
    command = [
        "git",
        "-C",
        str(root),
        "diff",
        "--no-renames",
        "--diff-filter=D",
        "--name-only",
        since,
        "--",
        ENGINE_INDEX,
    ]
    if ENGINE_INDEX not in local_gate.run_command(command, root)[1].split():
        return
    if folder.is_dir() and any(folder.rglob("*.md")):
        result.pages.append(ENGINE_INDEX)
        result.findings[ENGINE_INDEX] = [index_finding()]


def _add_standards(result: Result, root: Path, base: str, names: list[str]) -> None:
    """Hold the files the change touches to the code and test standards, and the legacy marks to their rules, when the
    change can move them (`standards_check`). Each file with a report becomes a "page" of the result, so the output
    reads like the pages'."""
    # Lenient: `task review` runs this on a branch cut before main added a key; main's value fills it (#1355).
    conventions = load_conventions_leniently(root)
    if not standards_check.relevant(names, conventions):
        return
    try:
        reports = standards_check.check(root, conventions, base)
    except ResearchFunctionsError:
        return  # `research render --check` reports a broken function database
    for report in reports:
        if report.path not in result.findings:
            result.pages.append(report.path)
        severity = "info" if report.info else "error" if report.error else "warning"
        result.findings.setdefault(report.path, []).append(Finding(report.rule, report.line, report.message, severity))


def check_paths(root: Path, paths: Iterable[str], context: Context) -> Result:
    """Check the named files, and for a folder every Markdown page under it, whatever has changed."""
    names: list[str] = []
    for path in paths:
        target = Path(path)
        target = (target if target.is_absolute() else root / target).resolve()
        top = root.resolve()
        if top != target and top not in target.parents:
            raise ConfigError(f"{path} is outside the checkout")
        if target.is_dir():
            names.extend(sorted(item.relative_to(top).as_posix() for item in target.rglob("*.md")))
        elif not target.is_file():
            raise ConfigError(f"{path}: no such file or folder")
        elif kind_of(name := target.relative_to(top).as_posix()) not in CHECKED_KINDS:
            raise ConfigError(f"{path}: not a page the check covers (a hand-written page under docs/)")
        else:
            names.append(name)
    return check_files(root, names, context)
