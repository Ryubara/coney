# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools ghidra import-names`: give a contributor's own Ghidra project the function names we publish.

The maintainers' project names most functions of `SLUS_212.15`; only the names are published, with each function's
address and size, in docs/progress/ghidra-functions.tsv (reference data, no code). A fresh analysis of the same
executable finds nearly all of those functions at the same addresses, so the names are applied by address: a Ghidra
script, ImportNames.java, run by `analyzeHeadless` on the contributor's project, renames each function it finds,
creates one where analysis found none and its size then matches the listing, and writes one outcome per name to a
report this module counts. Names the contributor gave by hand are kept unless asked to overwrite them. See
docs/guides/ghidra.md#import-names.
"""

from __future__ import annotations

import os
import tempfile
from collections import Counter
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path

from coney_tools.config import ConfigError
from coney_tools.ghidra_headless import GhidraProject, Runner, analyze_headless, failed, java_env, run_logged
from coney_tools.progress_research import ListedFunction
from coney_tools.workspace_fetch import platform_name

#: The folder of Coney's Ghidra scripts, shipped with the package, and the one this command runs.
SCRIPT_DIR = Path(__file__).parent / "ghidra_scripts"
SCRIPT = "ImportNames.java"
#: The log of the headless run, in the work folder.
LOG_NAME = "ghidra-import-names.log"

#: What the script did with a name, by group. Matched: Ghidra has a function at the address (renamed; already has
#: the name; or kept, as the contributor named it by hand). Created: no function there, and the one created has the
#: listed size. Skipped: the address lies inside another function, holds data, cannot be disassembled, the function
#: could not be created or came out another size (it is removed again), or Ghidra refused the name.
MATCHED = ("renamed", "same", "kept")
CREATED = ("created",)
SKIPPED = ("inside", "data", "nocode", "failed", "size", "error")
OUTCOMES = (*MATCHED, *CREATED, *SKIPPED)


@dataclass(frozen=True)
class ImportJob:
    """Where the names go and with what: the project and program, the Ghidra install, its JDK and a work folder."""

    project: GhidraProject
    program: str
    install: Path
    jdk: Path
    #: Where the log goes (the scratch folder); the script's input and report are temporary files in it.
    work: Path
    dry_run: bool = False
    overwrite: bool = False


@dataclass(frozen=True)
class Summary:
    """The outcome of an import: how many names, and what happened to them by outcome."""

    names: int
    counts: Counter[str]
    #: Matched functions whose size in Ghidra differs from the listing's: renamed anyway, the address is the key.
    size_differs: int

    @property
    def matched(self) -> int:
        """Names whose address already held a function."""
        return sum(self.counts[outcome] for outcome in MATCHED)

    @property
    def created(self) -> int:
        """Names given to a function created for them."""
        return sum(self.counts[outcome] for outcome in CREATED)

    @property
    def skipped(self) -> int:
        """Names that were not applied."""
        return sum(self.counts[outcome] for outcome in SKIPPED)


def names_input(functions: Sequence[ListedFunction]) -> str:
    """The script's input: `0x<address>\\t<size>\\t<name>` for every function of the listing that has our name."""
    lines = ["# address\tsize\tname (from docs/progress/ghidra-functions.tsv)"]
    lines += [f"0x{f.address:08x}\t{f.size}\t{f.name}" for f in functions if f.named]
    return "\n".join(lines) + "\n"


def parse_report(text: str) -> dict[int, tuple[str, int]]:
    """The script's report, `0x<address>\\t<outcome>\\t<size>` per line, by address (size -1: no function)."""
    report: dict[int, tuple[str, int]] = {}
    for number, line in enumerate(text.splitlines(), start=1):
        if not line.strip():
            continue
        fields = line.split("\t")
        if len(fields) != 3 or fields[1] not in OUTCOMES:
            raise ConfigError(f"import-names report line {number}: unknown outcome or layout: {line!r}")
        report[int(fields[0], 16)] = (fields[1], int(fields[2]))
    return report


def summarize(functions: Sequence[ListedFunction], report: Mapping[int, tuple[str, int]]) -> Summary:
    """Count the report's outcomes for the named functions; ConfigError when the report misses one of them."""
    named = [f for f in functions if f.named]
    answered = [f for f in named if f.address in report]
    if len(answered) != len(named):
        raise ConfigError(f"the import-names report has {len(answered)} of {len(named)} names; the run was cut short")
    counts = Counter(report[f.address][0] for f in named)
    differs = sum(1 for f in named if report[f.address][0] in MATCHED and report[f.address][1] != f.size)
    return Summary(len(named), counts, differs)


def import_command(
    analyze: Path, project: GhidraProject, program: str, names: Path, report: Path, mode: str, dry_run: bool
) -> list[str]:
    """The `analyzeHeadless` command that runs the script on `program` in `project`, without analysing it again.

    `-readOnly` (a dry run) must come before `-postScript`, whose arguments run to the end of the command.
    """
    command = [str(analyze), str(project.folder), project.name, "-process", program, "-noanalysis"]
    if dry_run:
        command.append("-readOnly")
    return [*command, "-scriptPath", str(SCRIPT_DIR), "-postScript", SCRIPT, str(names), str(report), mode]


def import_names(job: ImportJob, functions: Sequence[ListedFunction], runner: Runner = run_logged) -> Summary:
    """Apply the listing's names to `job.program` in `job.project` through analyzeHeadless; returns the counts.

    A dry run opens the program read-only, so the script does everything and Ghidra saves none of it: the counts are
    the ones a real run would give.
    """
    analyze = analyze_headless(job.install, platform_name())
    job.work.mkdir(parents=True, exist_ok=True)
    log = job.work / LOG_NAME
    mode = "overwrite" if job.overwrite else "apply"
    with tempfile.TemporaryDirectory(dir=job.work, prefix=".import-names-") as temp:
        names, report = Path(temp) / "names.tsv", Path(temp) / "report.tsv"
        names.write_text(names_input(functions), encoding="utf-8")
        command = import_command(analyze, job.project, job.program, names, report, mode, job.dry_run)
        code = runner(command, java_env(job.jdk, os.environ), log, job.work)
        if not report.is_file():
            raise failed(f"analyzeHeadless on {job.project.gpr} ({job.program})", code, log)
        return summarize(functions, parse_report(report.read_text(encoding="utf-8")))


def format_summary(summary: Summary, dry_run: bool) -> list[str]:
    """The counts as printed: a total line, then one line per group with its outcomes."""
    head = "import-names (dry run, nothing saved)" if dry_run else "import-names"
    lines = [
        f"{head}: {summary.names:,} names; matched {summary.matched:,}, created {summary.created:,}, "
        f"skipped {summary.skipped:,}"
    ]
    for group in (MATCHED, CREATED, SKIPPED):
        parts = [f"{outcome} {summary.counts[outcome]:,}" for outcome in group if summary.counts[outcome]]
        if parts:
            lines.append("  " + ", ".join(parts))
    if summary.size_differs:
        lines.append(f"  {summary.size_differs:,} matched functions differ in size from the listing (named anyway)")
    return lines
