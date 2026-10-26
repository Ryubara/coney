# SPDX-License-Identifier: GPL-3.0-or-later
"""The standards of touched files and the legacy marks, checked (`coney-tools standards`).

Three things below today's standards are marked, and this module scans for them and holds the marks to the rules of
docs/guides/coney-tools-standards.md#standards:

* a ported function (`@orig`) carries `status = "legacy"` or `"compared"` in the function database
  (`research/functions/<system>.toml`): `legacy` until a side-by-side comparison has been made;
* a source or test file below the code or test standard is listed in the legacy file (`standards_file`);
* a research page below the research standard is listed in `research/legacy.toml` (`docs_legacy`, not touched here).

`problems` is what is wrong with the marks themselves, whatever the change: a file listed for a rule it now passes or
gone, a ported function with no mark, a file or rule added to the legacy file since the branch left its base and, from
the cutover, a function marked legacy since then. `touched` is the touch-it rule: a source or test file the change
touches is held to the whole standard (listed or not), and so is a ported function the change touches. Every report of
both is a warning while `standards.enforce` is false and an error once it is true, as it is from the cutover: with the
switch off nothing in `check` fails a pull request. The two together are `check`, which `docs check` runs when a
change can move them.
"""

from __future__ import annotations

import dataclasses
from collections.abc import Callable
from dataclasses import dataclass, field
from pathlib import Path

from coney_tools import local_gate
from coney_tools.code_link_moves import CodeDiff, touched_code_lines
from coney_tools.config import ConfigError
from coney_tools.conventions import Conventions
from coney_tools.docs_link_resolve import link_resolver
from coney_tools.orig_index import SOURCE_DIR, TagCoverage, read_sources
from coney_tools.orig_scan import SourceFile, Tag
from coney_tools.research_functions import (
    Entry,
    ResearchFunctionsError,
    SystemFunctions,
    dumps,
    load_all,
    parse_system,
)
from coney_tools.standards_file import (
    LegacyFiles,
    format_legacy_files,
    load_legacy_files,
    parse_legacy_files,
    rules_of,
)
from coney_tools.standards_scan import Finding, counts, scan_code, scan_tests

#: The status a ported function has until it has been compared with the original.
LEGACY = "legacy"
COMPARED = "compared"


@dataclass(frozen=True)
class Report:
    """One line of the check: where, which rule, what to do, and whether it fails the check."""

    path: str
    line: int
    rule: str
    message: str
    #: Whether it fails the check: `standards.enforce` (an error when true, a warning when false).
    error: bool


@dataclass
class Scan:
    """What a scan of the checkout finds: the findings of every source and test file that has any, and the sources."""

    code: dict[str, list[Finding]] = field(default_factory=dict)
    tests: dict[str, list[Finding]] = field(default_factory=dict)
    sources: list[SourceFile] = field(default_factory=list)
    code_files: int = 0
    test_files: int = 0

    def findings(self) -> dict[str, list[Finding]]:
        """Every file's findings, source and test files together."""
        return {**self.code, **self.tests}

    def counts(self) -> dict[str, dict[str, int]]:
        """For each file that has findings: its findings counted by rule (what the legacy file lists)."""
        return {path: counts(found) for path, found in sorted(self.findings().items())}


@dataclass(frozen=True)
class FunctionCounts:
    """The ported functions of the function database by status."""

    legacy: int
    compared: int
    #: A ported function (an entry with `coney`) that has no status: `address name` of each.
    unmarked: tuple[str, ...]

    @property
    def ported(self) -> int:
        """Every ported function."""
        return self.legacy + self.compared + len(self.unmarked)


# --- scanning --------------------------------------------------------------------------------------------------------


def _under(root: Path, folders: tuple[str, ...], suffixes: tuple[str, ...]) -> list[Path]:
    """The files of `suffixes` below any of the `folders` of the checkout, in path order."""
    found: list[Path] = []
    for folder in folders:
        base = root / folder
        if base.is_dir():
            found.extend(p for p in base.rglob("*") if p.suffix in suffixes and p.is_file())
    return sorted(found)


def scan_tree(root: Path, conventions: Conventions) -> Scan:
    """Scan every source and test file of the checkout against the code and test standards."""
    standards = conventions.standards
    sources = [s for s in read_sources(root) if rules_of(s.path, standards)]
    scan = Scan(sources=sources, code_files=len(sources))
    scan.code = scan_code(sources, conventions.paths.gameplay)
    for path in _under(root, standards.tests.folders, standards.tests.suffixes):
        name = path.relative_to(root).as_posix()
        scan.test_files += 1
        found = scan_tests(name, path.read_text(encoding="utf-8", errors="replace"), standards.tests)
        if found:
            scan.tests[name] = found
    return scan


def function_counts(systems: list[SystemFunctions]) -> FunctionCounts:
    """The ported functions (entries with `coney`) of the database counted by status."""
    ported = [entry for system in systems for entry in system.entries if entry.coney]
    return FunctionCounts(
        legacy=sum(1 for e in ported if e.status == LEGACY),
        compared=sum(1 for e in ported if e.status == COMPARED),
        unmarked=tuple(f"{e.address:#010x} {e.name}" for e in ported if not e.status),
    )


def mark_unmarked(root: Path, conventions: Conventions) -> list[str]:
    """Give every ported function with no status `legacy`, rewriting the system files that hold one; returns the files
    rewritten. A function becomes `compared` only by a side-by-side comparison, never here."""
    changed: list[str] = []
    for system in load_all(root, Path(conventions.research.files.functions)):
        if not any(e.coney and not e.status for e in system.entries):
            continue
        entries = tuple(
            dataclasses.replace(e, status=LEGACY) if e.coney and not e.status else e for e in system.entries
        )
        text = dumps(dataclasses.replace(system, entries=entries))
        (root / system.path).write_text(text, encoding="utf-8", newline="\n")
        changed.append(system.path.as_posix())
    return changed


# --- the legacy file -------------------------------------------------------------------------------------------------


def initial_text(root: Path, conventions: Conventions, scan: Scan) -> str:
    """The text of the legacy file as a first scan writes it. ConfigError when the file already exists."""
    name = conventions.standards.legacy_file
    if (root / name).exists():
        raise ConfigError(f"{name} exists: the file only shrinks, use `standards prune`")
    return format_legacy_files(LegacyFiles(scan.counts()))


def pruned(root: Path, conventions: Conventions, scan: Scan) -> LegacyFiles:
    """The current legacy file with what is fixed taken out and the counts lowered; nothing is added."""
    listed = load_legacy_files(root, conventions.standards)
    now = scan.counts()
    kept: dict[str, dict[str, int]] = {}
    for path, rules in listed.files.items():
        current = now.get(path, {})
        left = {rule: min(count, current[rule]) for rule, count in rules.items() if rule in current}
        if left:
            kept[path] = left
    return LegacyFiles(kept)


def _file_at(root: Path, since: str, name: str) -> str | None:
    """The text of `name` as the commit `since` has it, or None when it had none."""
    return local_gate.file_at(root, since, name)


def _status_at(root: Path, since: str, conventions: Conventions) -> dict[int, str]:
    """The status of every ported function as the commit `since` has it (address to status)."""
    folder = conventions.research.files.functions.rstrip("/")
    listing = local_gate.run_command(["git", "-C", str(root), "ls-tree", "-r", "--name-only", since, folder], root)[1]
    status: dict[int, str] = {}
    for name in listing.splitlines():
        text = _file_at(root, since, name)
        if text is None or not name.endswith(".toml"):
            continue
        try:
            system = parse_system(Path(name).stem, text, Path(name))
        except ResearchFunctionsError:
            continue  # a file that did not parse at the base marks nothing
        status.update({e.address: e.status for e in system.entries if e.coney})
    return status


def problems(root: Path, conventions: Conventions, scan: Scan, base: str | None = None) -> list[Report]:
    """What is wrong with the marks as they stand, whatever the change touches, one report each.

    A listed file that is gone or now passes a rule it is listed for; a ported function with no status; against `base`,
    a file or rule added to the legacy file since the branch left it (when it already had the file) and, once the
    cutover is set, a function marked legacy that was not. Each is a warning until `standards.enforce`.
    """
    standards = conventions.standards
    listed = load_legacy_files(root, standards)
    now = scan.counts()
    found: list[Report] = []
    name = standards.legacy_file
    error = standards.enforce
    for path, rules in listed.files.items():
        if not (root / path).is_file():
            found.append(Report(name, 1, "legacy", f"{path} is gone: run `coney-tools standards prune`", error))
            continue
        found.extend(
            Report(name, 1, "legacy", f"{path} now passes '{rule}': run `coney-tools standards prune`", error)
            for rule in rules
            if rule not in now.get(path, {})
        )
    counted = function_counts(load_all(root, Path(conventions.research.files.functions)))
    found.extend(
        Report(
            conventions.research.files.functions,
            1,
            "status",
            f'{item} is ported and has no status: record it as `status = "compared"` after its side-by-side comparison',
            error,
        )
        for item in counted.unmarked
    )
    since = local_gate.merge_base(root, base) if base else None
    if since is not None:
        found.extend(_added_since(root, conventions, listed, since, error))
    return found


def _added_since(root: Path, conventions: Conventions, listed: LegacyFiles, since: str, error: bool) -> list[Report]:
    """The files and rules the legacy file gained since `since`, and (from the cutover) the functions marked legacy."""
    standards = conventions.standards
    found: list[Report] = []
    name = standards.legacy_file
    before = _file_at(root, since, name)
    if before is not None:
        earlier = parse_legacy_files(before, standards, f"{since}:{name}")
        for path, rules in listed.files.items():
            if path not in earlier.files:
                found.append(
                    Report(name, 1, "legacy", f"{path} is added: a file below the standard is fixed, not listed", error)
                )
            else:
                found.extend(
                    Report(name, 1, "legacy", f"{path} is listed for '{rule}' that it was not before", error)
                    for rule in rules
                    if rule not in earlier.files[path]
                )
    if standards.cutover is not None:
        was = _status_at(root, since, conventions)
        for system in load_all(root, Path(conventions.research.files.functions)):
            found.extend(
                Report(
                    system.path.as_posix(),
                    1,
                    "status",
                    f"{e.address:#010x} {e.name} is marked legacy since the cutover: compare it with the original",
                    error,
                )
                for e in system.entries
                if e.coney and e.status == LEGACY and was.get(e.address) != LEGACY
            )
    return found


# --- touching --------------------------------------------------------------------------------------------------------


def _code_diff(
    root: Path,
    since: str,
    name: str,
    gone: set[str],
    resolves: Callable[[str, str], bool],
    held: Callable[[str, str], bool],
) -> CodeDiff | None:
    """What the change touched of `name` (committed or not), or None for a new file (all of it). A hunk that only
    rewrites research references in comments to follow a page the change moved touches nothing (`code_link_moves`)."""
    before = _file_at(root, since, name)
    if before is None:
        return None
    diff = local_gate.run_command(["git", "-C", str(root), "diff", "-U0", since, "--", name], root)[1]
    after = (root / name).read_text(encoding="utf-8", errors="replace")
    return touched_code_lines(diff, gone, resolves, held, before.splitlines(), after.splitlines())


def _ported(root: Path, conventions: Conventions) -> dict[int, Entry]:
    """Every ported function of the database by address."""
    return {
        e.address: e
        for system in load_all(root, Path(conventions.research.files.functions))
        for e in system.entries
        if e.coney
    }


def touched(
    root: Path, conventions: Conventions, scan: Scan, base: str, names: list[str] | None = None
) -> list[Report]:
    """The touch-it rule for the files the change since `base` touches: each source or test file below the standard,
    and each ported function whose lines the change touches that has not been compared. Warnings until
    `standards.enforce`, errors after."""
    standards = conventions.standards
    error = standards.enforce
    names = names if names is not None else local_gate.changed_files(root, base)
    found: list[Report] = []
    findings = scan.findings()
    since = local_gate.merge_base(root, base)
    # What the pages moved is read as `docs check` reads it (`docs_check.change_touches`).
    gone = local_gate.removed_files(root, base, conventions.research.moved_similarity)
    resolves, held = link_resolver(root), link_resolver(root, since)
    diffs: dict[str, CodeDiff | None] = {
        name: _code_diff(root, since, name, gone, resolves, held) for name in names if rules_of(name, standards)
    }
    for name in diffs:
        diff = diffs[name]
        if diff is not None and diff.link_moves_only:
            continue  # a file whose only change is its comments' research links following a moved page
        by_rule: dict[str, list[Finding]] = {}
        for item in findings.get(name, []):
            by_rule.setdefault(item.rule, []).append(item)
        for rule, items in by_rule.items():
            first = items[0]
            found.append(
                Report(
                    name,
                    first.line,
                    rule,
                    f"{len(items)} finding{'s' if len(items) != 1 else ''} in a touched file; the whole file meets the "
                    f"standard when touched (first: {first.message})",
                    error,
                )
            )
    found.extend(_touched_functions(root, conventions, scan, since, diffs, error))
    return found


def _touched_functions(
    root: Path, conventions: Conventions, scan: Scan, since: str, diffs: dict[str, CodeDiff | None], error: bool
) -> list[Report]:
    """The ported functions whose lines a changed file touches that are not `compared`. A function is held to its tags
    wherever they are written: above it, inside its body, or on its declaration (most ports carry `@orig` in the
    header), the declaration matched overload by overload as `orig check` matches it (`TagCoverage`)."""
    wanted = {n for n in diffs if n.startswith(f"{SOURCE_DIR.as_posix()}/")}
    if not wanted:
        return []
    database = _ported(root, conventions)
    coverage = TagCoverage(scan.sources)
    found: list[Report] = []
    for source in scan.sources:
        if source.path not in wanted:
            continue
        diff = diffs[source.path]
        changed = None if diff is None else diff.lines
        for function in source.functions:
            span = range(function.line, function.line + max(function.lines, 1))
            if changed is not None and not any(line in changed for line in span):
                continue
            tags: list[Tag] = [*function.tags, *function.body_tags]
            for other in coverage.declarations(function):
                tags += [*other.tags, *other.body_tags]
            for tag in dict.fromkeys(tags):
                entry = database.get(tag.address)
                if entry is not None and entry.status != COMPARED:
                    found.append(
                        Report(
                            source.path,
                            function.line,
                            "compared",
                            f"{function.qualified} ports {tag.address:#010x} {tag.name}, which has not been compared "
                            "with the original: study it, pair it and have it reviewed, then set its status "
                            "`compared`",
                            error,
                        )
                    )
    return found


# --- the whole check -------------------------------------------------------------------------------------------------


def relevant(names: list[str], conventions: Conventions) -> bool:
    """Whether a change can move the marks or hold a touched file to a standard: it touches a source or test file, the
    function database, the legacy file, the conventions or the code of these checks."""
    standards = conventions.standards
    folder = conventions.research.files.functions.rstrip("/") + "/"
    return any(
        bool(rules_of(name, standards))
        or name.startswith(folder)
        or name in (standards.legacy_file, ".github/conventions.yml")
        or name.startswith("python/src/coney_tools/standards_")
        for name in names
    )


def check(root: Path, conventions: Conventions, base: str) -> list[Report]:
    """Everything wrong with the marks and everything a touched file or function owes, for the change since `base`."""
    scan = scan_tree(root, conventions)
    return [*problems(root, conventions, scan, base), *touched(root, conventions, scan, base)]
