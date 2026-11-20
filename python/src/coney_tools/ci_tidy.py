# SPDX-License-Identifier: GPL-3.0-or-later
"""The changed-lines clang-tidy job: `coney-tools ci tidy-plan` and `ci tidy-run`.

A full pass over Coney's sources takes close to an hour on a CI runner, so a pull request, and a push to main, is
tidied on **the lines it changes** only (the nightly job still checks every file). `make_plan` reads the change's diff
and picks what to run:

* a changed source (`ci.tidy.sources`) is checked itself;
* a changed header (`ci.tidy.headers`) is checked through **one** source that includes it, directly or through other
  headers: the source with the header's own name if there is one (`brain.h` through `brain.cpp`), else the nearest
  includer. One is enough to analyse the header's lines, and a header reaches 100 to 280 sources.

and builds one **line filter** (clang-tidy's `--line-filter`): the changed lines of every file, so a diagnostic on a
line the pull request did not touch is never reported, whichever source it was found through. `run_plan` runs
clang-tidy on the chosen sources in parallel against the `ci` preset's compile database and reads its text output into
`Finding`s; `to_sarif` writes them as SARIF 2.1.0 for GitHub code scanning (annotations on the pull request) and
`to_annotations` as workflow commands (for a pull request from a fork, whose token cannot upload SARIF).

A change to the build setup (`ci.tidy.whole_when`: `CMakeLists.txt`, `cmake/`, `CMakePresets.json`, `.clang-tidy`)
affects every file, so the plan then says `everything`: the job runs the whole-codebase pass the nightly run does,
instead of the changed lines (docs/guides/clang-tidy.md).
"""

from __future__ import annotations

import json
import re
import subprocess
import tempfile
from collections.abc import Callable, Iterable, Sequence
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from coney_tools import tidy_cache
from coney_tools.conventions import CiTidy
from coney_tools.tidy_cache import TidyCache

#: Runs a command in a folder and returns its exit code and its output (stdout and stderr together).
Runner = Callable[[list[str], Path], tuple[int, str]]

_FILE_LINE = re.compile(r"^\+\+\+ (?P<path>.+?)\t?$")
_HUNK = re.compile(r"^@@ -\d+(?:,(?P<old>\d+))? \+(?P<start>\d+)(?:,(?P<count>\d+))? @@")
_INCLUDE = re.compile(r'^\s*#\s*include\s+"(?P<path>[^"]+)"', re.MULTILINE)
_FINDING = re.compile(
    r"^(?P<path>(?:[A-Za-z]:)?[^:\n]+?):(?P<line>\d+):(?P<column>\d+): (?P<level>warning|error): "
    r"(?P<message>.*?) \[(?P<check>[A-Za-z0-9_.,-]+)\]$"
)
_OCTAL = re.compile(r"\\([0-7]{3})")

#: SARIF's published schema and version.
SARIF_SCHEMA = "https://json.schemastore.org/sarif-2.1.0.json"
SARIF_VERSION = "2.1.0"


# --- reading the diff -------------------------------------------------------------------------------------------------


def _unquote(path: str) -> str:
    """A path as git prints it: plain, or in double quotes with octal escapes for bytes outside ASCII."""
    if not (path.startswith('"') and path.endswith('"')):
        return path
    inner = path[1:-1]
    data = bytearray()
    index = 0
    while index < len(inner):
        octal = _OCTAL.match(inner, index) if inner[index] == "\\" else None
        if octal:
            data.append(int(octal.group(1), 8))
            index += 4
        elif inner[index] == "\\" and index + 1 < len(inner):
            data.extend({"n": b"\n", "t": b"\t"}.get(inner[index + 1], inner[index + 1].encode("utf-8")))
            index += 2
        else:
            data.extend(inner[index].encode("utf-8"))
            index += 1
    return data.decode("utf-8", errors="replace")


def _scan(diff: str) -> tuple[dict[str, list[tuple[int, int]]], list[str]]:
    """Read a unified diff: the added line ranges of each file, and every file it names (in order, each once)."""
    result: dict[str, list[tuple[int, int]]] = {}
    touched: list[str] = []
    current: str | None = None
    lines = diff.splitlines()
    index = 0
    while index < len(lines):
        line = lines[index]
        index += 1
        header = _FILE_LINE.match(line)
        if header and line.startswith("+++ "):
            path = _unquote(header.group("path"))
            current = None if path == "/dev/null" else path.removeprefix("b/")
            if current is not None and current not in touched:
                touched.append(current)
            continue
        if line.startswith("--- "):
            # A deleted file has only this header; a file that was changed has both and is listed once.
            old = _unquote(line[4:].split("\t", 1)[0])
            if old != "/dev/null" and old.removeprefix("a/") not in touched:
                touched.append(old.removeprefix("a/"))
            continue
        hunk = _HUNK.match(line)
        if not hunk or current is None:
            continue
        old_count = int(hunk.group("old")) if hunk.group("old") is not None else 1
        count = int(hunk.group("count")) if hunk.group("count") is not None else 1
        start = int(hunk.group("start"))
        if count:
            result.setdefault(current, []).append((start, start + count - 1))
        # The hunk's own lines follow: skip them, so a line that looks like a file header is not read as one.
        index += old_count + count
        while index < len(lines) and lines[index].startswith("\\"):
            index += 1
    return result, touched


def changed_lines(diff: str) -> dict[str, list[tuple[int, int]]]:
    """The lines a unified diff (`git diff -U0`) adds or changes: file -> inclusive (first, last) ranges, in order.

    A deleted file and a hunk that only deletes lines add none, and are left out. The diff's `+++ b/` prefix is dropped.
    """
    return _scan(diff)[0]


def touched_files(diff: str) -> list[str]:
    """Every file a unified diff adds, changes or deletes (a deleted file counts, unlike in `changed_lines`)."""
    return _scan(diff)[1]


# --- choosing what to check -------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class Unit:
    """One source clang-tidy runs on."""

    source: str


@dataclass
class TidyPlan:
    """What the job runs: the sources, the lines to report (all units share one filter) and notes for the summary."""

    units: list[Unit] = field(default_factory=list)
    lines: dict[str, list[tuple[int, int]]] = field(default_factory=dict)
    notes: list[str] = field(default_factory=list)
    #: True when the change is to the build setup (`ci.tidy.whole_when`): the job then checks every file, not `units`.
    everything: bool = False

    def line_filter(self) -> list[dict[str, object]]:
        """The `--line-filter` value as a JSON-able list: {"name": file, "lines": [[first, last], ...]}, by file."""
        return [{"name": name, "lines": [list(pair) for pair in self.lines[name]]} for name in sorted(self.lines)]

    def as_json(self) -> dict[str, object]:
        """The plan as the JSON file `ci tidy-plan` writes and `ci tidy-run` reads."""
        return {
            "sources": [unit.source for unit in self.units],
            "line_filter": self.line_filter(),
            "notes": list(self.notes),
            "everything": self.everything,
        }

    @classmethod
    def from_json(cls, data: object) -> TidyPlan:
        """The plan in `data` (what `as_json` wrote); ValueError when it is not one."""
        if not isinstance(data, dict) or not isinstance(data.get("sources"), list):
            raise ValueError("not a clang-tidy plan")
        lines: dict[str, list[tuple[int, int]]] = {}
        for entry in data.get("line_filter", []):
            lines[str(entry["name"])] = [(int(first), int(last)) for first, last in entry["lines"]]
        return cls(
            [Unit(str(name)) for name in data["sources"]],
            lines,
            [str(note) for note in data.get("notes", [])],
            data.get("everything") is True,
        )


def _own_folder(path: str, settings: CiTidy) -> str | None:
    """The folder of `settings.folders` that holds `path`, or None."""
    return next((folder for folder in settings.folders if path.startswith(folder)), None)


def _includes_of(root: Path, settings: CiTidy) -> dict[str, list[str]]:
    """Every checked file (a source or a header under the folders) -> the quoted paths it `#include`s."""
    found: dict[str, list[str]] = {}
    extensions = (*settings.sources, *settings.headers)
    for folder in settings.folders:
        base = root / folder
        for path in sorted(base.rglob("*")) if base.is_dir() else []:
            if path.is_file() and path.suffix in extensions:
                try:
                    text = path.read_text(encoding="utf-8", errors="replace")
                except OSError:
                    continue
                found[path.relative_to(root).as_posix()] = [match.group("path") for match in _INCLUDE.finditer(text)]
    return found


def _includers(header: str, includes: dict[str, list[str]], settings: CiTidy) -> list[str]:
    """The files that `#include` `header`: by its path below its folder (`ai/brain.h`; src/ and tests/ are include
    folders) or, from the same folder, by its name."""
    folder = _own_folder(header, settings)
    spelled = header[len(folder) :] if folder else header
    parent = header.rsplit("/", 1)[0] if "/" in header else ""
    name = header.rsplit("/", 1)[-1]
    found = []
    for path, written in includes.items():
        same_folder = path.rsplit("/", 1)[0] == parent if "/" in path else parent == ""
        if spelled in written or (same_folder and name in written):
            found.append(path)
    return sorted(found)


def _source_for(header: str, includes: dict[str, list[str]], settings: CiTidy) -> str | None:
    """The one source to check `header` through: the nearest includer, the one with the header's own name first."""
    stem = header.rsplit(".", 1)[0]
    seen = {header}
    level = [header]
    while level:
        reached: list[str] = []
        for current in level:
            for includer in _includers(current, includes, settings):
                if includer not in seen:
                    seen.add(includer)
                    reached.append(includer)
        sources = sorted(path for path in reached if path.endswith(settings.sources))
        if sources:
            own = [path for path in sources if path.rsplit(".", 1)[0] == stem]
            return own[0] if own else sources[0]
        level = sorted(path for path in reached if path.endswith(settings.headers))
    return None


def build_setup_files(touched: Iterable[str], settings: CiTidy) -> list[str]:
    """The files among `touched` in the build setup: `settings.whole_when`, a folder (ending in /) or a file."""
    return sorted(
        {
            path
            for path in touched
            for entry in settings.whole_when
            if (path.startswith(entry) if entry.endswith("/") else path == entry)
        }
    )


def make_plan(
    changed: dict[str, list[tuple[int, int]]], root: Path, settings: CiTidy, touched: Iterable[str] = ()
) -> TidyPlan:
    """The plan for a change in the checkout at `root`.

    `changed` is `changed_lines` of its diff, and `touched` is `touched_files` of it (a deleted file too): a change to
    the build setup among them makes the plan `everything`, because a compiler flag or a check setting reaches files
    the change did not touch. The sources and lines of the change are planned all the same.
    """
    plan = TidyPlan()
    setup = build_setup_files([*changed, *touched], settings)
    if setup:
        plan.everything = True
        shown = ", ".join(setup[:3]) + (f" and {len(setup) - 3} more" if len(setup) > 3 else "")
        plan.notes.append(f"the build setup changed ({shown}): every file is checked, not only the changed lines")
    sources: set[str] = set()
    includes: dict[str, list[str]] | None = None
    for path in sorted(changed):
        ranges = changed[path]
        if not ranges or _own_folder(path, settings) is None or not (root / path).is_file():
            continue
        if path.endswith(settings.sources):
            sources.add(path)
            plan.lines.setdefault(path, []).extend(ranges)
        elif path.endswith(settings.headers):
            includes = includes if includes is not None else _includes_of(root, settings)
            source = _source_for(path, includes, settings)
            if source is None:
                plan.notes.append(f"{path}: no source includes it, so only the nightly pass checks it")
                continue
            sources.add(source)
            plan.lines.setdefault(path, []).extend(ranges)
    plan.units = [Unit(source) for source in sorted(sources)]
    return plan


def every_source(build_dir: Path, root: Path, settings: CiTidy) -> list[Unit]:
    """Every source of Coney's own (under `settings.folders`, with a source extension) the compile database of
    `build_dir` compiles, never one of the fetched dependencies (`_deps/`): the units of the whole-codebase pass."""
    return [
        Unit(name)
        for name in sorted(tidy_cache.load_commands(build_dir, root))
        if _own_folder(name, settings) is not None and name.endswith(settings.sources) and "_deps/" not in name
    ]


#: The object file of a compile command: `<folder>/CMakeFiles/<target>.dir/<source>.o` (or `.obj`).
_TARGET_DIR = re.compile(r"(?:^|[/\\])CMakeFiles[/\\](?P<target>[^/\\]+)\.dir[/\\]")
#: A phony target of CMake's Ninja generator that builds what a target's objects need before they compile (the
#: headers its custom commands generate, and its dependencies'), without compiling anything.
ORDER_TARGET = "cmake_object_order_depends_target_"


def build_targets(build_dir: Path, root: Path, units: Sequence[Unit]) -> list[str]:
    """The Ninja targets that make what clang-tidy needs to analyse `units`: for each, the order-only target of the
    CMake target that compiles it (the generated headers, nothing compiled), when the build file has one, else its
    object file. clang-tidy needs the compile database and the generated headers, not the objects, so the job builds
    these instead of the whole project."""
    commands = tidy_cache.load_commands(build_dir, root)
    try:
        ninja = (build_dir / "build.ninja").read_text(encoding="utf-8", errors="replace")
    except OSError:
        ninja = ""
    found: list[str] = []
    for unit in units:
        output = str(commands.get(unit.source, {}).get("output", ""))
        match = _TARGET_DIR.search(output)
        target = f"{ORDER_TARGET}{match.group('target')}" if match else ""
        chosen = target if target and f"build {target}:" in ninja else output.replace("\\", "/")
        if chosen and chosen not in found:
            found.append(chosen)
    return found


# --- running clang-tidy -----------------------------------------------------------------------------------------------


@dataclass(frozen=True, order=True)
class Finding:
    """One diagnostic of clang-tidy: where, how bad, which check and what it says."""

    path: str
    line: int
    column: int
    level: str
    check: str
    message: str


def _relative(path: str, root: Path) -> str:
    """`path` below the checkout `root` with forward slashes; a path outside it is kept (slashes made forward)."""
    text = path.replace("\\", "/")
    base = str(root).replace("\\", "/").rstrip("/") + "/"
    return text[len(base) :] if text.casefold().startswith(base.casefold()) else text


def parse_findings(output: str, root: Path) -> list[Finding]:
    """The warnings and errors in clang-tidy's text output, in order; notes and its own chatter are not findings."""
    found = []
    for line in output.splitlines():
        match = _FINDING.match(line.strip())
        if match:
            found.append(
                Finding(
                    _relative(match.group("path"), root),
                    int(match.group("line")),
                    int(match.group("column")),
                    match.group("level"),
                    match.group("check").split(",")[0],
                    match.group("message"),
                )
            )
    return found


@dataclass
class TidyResult:
    """What a run found: the findings (deduplicated, sorted), and the units where clang-tidy itself failed."""

    findings: list[Finding] = field(default_factory=list)
    failures: list[str] = field(default_factory=list)
    #: How many units took their outcome from the cache instead of running clang-tidy.
    cached: int = 0

    @property
    def ok(self) -> bool:
        """True when nothing was found and clang-tidy ran everywhere."""
        return not self.findings and not self.failures


#: The longest line filter handed to clang-tidy as an argument. Linux refuses a single argument over 128 KiB, and a
#: change that touches hundreds of files can pass it; then clang-tidy runs unfiltered and `run_plan` keeps only the
#: findings on the changed lines itself.
MAX_FILTER_BYTES = 100_000


@dataclass(frozen=True)
class RunOptions:
    """How `run_plan` runs clang-tidy beyond the plan itself."""

    #: More arguments for every clang-tidy command, before the source (local_tidy.py's header filter on Windows).
    extra_args: tuple[str, ...] = ()
    #: Hand the plan's lines to clang-tidy as `--line-filter`. Off, clang-tidy reports every line and `run_plan` keeps
    #: the changed ones itself (the same result: the filter only decides what is printed, not what is analysed). Off
    #: on Windows, where clang-tidy matches the filter's names against paths written with backslashes.
    line_filter: bool = True
    #: The result cache (tidy_cache.py): a source whose compile command, configuration and every file it read are
    #: unchanged takes its findings from there instead of running clang-tidy. It always runs without the line filter,
    #: so one entry serves any change.
    cache: TidyCache | None = None


def _tidy_command(
    tool: str, build_dir: Path, plan: TidyPlan, source: str, options: RunOptions, depfile: Path | None
) -> list[str]:
    """The clang-tidy command for one source: the compile database, quiet, the line filter when it fits and is
    wanted, the dependency file the cache needs, the extra arguments and the source last."""
    command = [tool, "-p", str(build_dir), "--quiet"]
    line_filter = json.dumps(plan.line_filter(), separators=(",", ":"))
    wanted = options.line_filter and options.cache is None and not plan.everything
    if wanted and len(line_filter.encode("utf-8")) <= MAX_FILTER_BYTES:
        command.append(f"--line-filter={line_filter}")
    if depfile is not None:
        command += [f"--extra-arg={word}" for word in tidy_cache.depfile_arguments(depfile)]
    return [*command, *options.extra_args, source]


COMPILE_ERROR = "clang-diagnostic-error"
"""The check name clang-tidy gives a compiler error; it is reported whatever the line filter says."""


def _on_changed_line(item: Finding, lines: dict[str, list[tuple[int, int]]]) -> bool:
    """True when the finding is on a line the change added or changed."""
    return any(first <= item.line <= last for first, last in lines.get(item.path, ()))


@dataclass(frozen=True)
class UnitOutcome:
    """One source's run (or cache entry): clang-tidy's exit code, every finding it printed and its last line."""

    code: int
    findings: tuple[Finding, ...]
    tail: str
    cached: bool = False


def _run_unit(
    unit: Unit, plan: TidyPlan, root: Path, build_dir: Path, tool: str, run: Runner, options: RunOptions
) -> UnitOutcome:
    """Run clang-tidy on one source, or take its outcome from the cache; a clean outcome or one with findings is kept
    in the cache, a failure of the tool never, nor a source that does not compile: a missing include is not in the
    dependency file, so a kept compile error would outlive the arrival of the header it lacked."""
    cache = options.cache
    if cache is not None:
        kept = cache.get(unit.source)
        if kept is not None:
            return UnitOutcome(kept.code, kept.findings, "", cached=True)
    with tempfile.TemporaryDirectory(prefix="coney-tidy-") as temp:
        depfile = Path(temp) / "deps.d" if cache is not None else None
        code, output = run(_tidy_command(tool, build_dir, plan, unit.source, options, depfile), root)
        findings = tuple(parse_findings(output, root))
        tail = next((line.strip() for line in reversed(output.splitlines()) if line.strip()), "no output")
        compiled = not any(item.check == COMPILE_ERROR for item in findings)
        if cache is not None and depfile is not None and (code == 0 or findings) and compiled:
            cache.put(unit.source, tidy_cache.Entry(code, findings), depfile)
    return UnitOutcome(code, findings, tail)


def run_plan(
    plan: TidyPlan,
    root: Path,
    build_dir: Path,
    tool: str,
    jobs: int,
    run: Runner,
    options: RunOptions | None = None,
) -> TidyResult:
    """Run clang-tidy on every unit of `plan`, `jobs` at a time, and gather what it reports: the findings on the
    plan's lines, or every finding for a plan of `everything` (the whole-codebase pass).

    A unit where clang-tidy exits non-zero without printing a single finding is a failure of the tool (a missing
    program, a missing compile database, a crash), never a pass. So is a unit that does not compile
    (`clang-diagnostic-error`, on any line): clang-tidy could not analyse it, so its changed lines were never checked.
    """
    options = options or RunOptions()
    result = TidyResult()
    if not plan.units:
        return result
    with ThreadPoolExecutor(max_workers=max(1, min(jobs, len(plan.units)))) as pool:
        outcomes = list(pool.map(lambda unit: _run_unit(unit, plan, root, build_dir, tool, run, options), plan.units))
    seen: set[tuple[str, int, int, str, str]] = set()
    for unit, outcome in zip(plan.units, outcomes, strict=True):
        result.cached += outcome.cached
        if outcome.code != 0 and not outcome.findings:
            result.failures.append(f"{unit.source}: clang-tidy exited {outcome.code} with no finding: {outcome.tail}")
        broken = next((item for item in outcome.findings if item.check == COMPILE_ERROR), None)
        if broken is not None:
            result.failures.append(
                f"{unit.source}: does not compile ({broken.path}:{broken.line}: {broken.message}), "
                "so it was not checked"
            )
        for item in outcome.findings:
            if not (plan.everything or _on_changed_line(item, plan.lines)):
                continue
            key = (item.path, item.line, item.column, item.check, item.message)
            if key not in seen:
                seen.add(key)
                result.findings.append(item)
    result.findings.sort()
    return result


def run_tool(command: list[str], cwd: Path) -> tuple[int, str]:
    """The real runner: the command's exit code and its output; a program that is not there is code 127."""
    try:
        done = subprocess.run(
            command, cwd=cwd, capture_output=True, text=True, encoding="utf-8", errors="replace", check=False
        )
    except OSError as error:
        return 127, f"{command[0]}: {error.strerror or error}"
    return done.returncode, done.stdout + done.stderr


# --- reporting --------------------------------------------------------------------------------------------------------


def to_sarif(findings: Sequence[Finding], version: str) -> dict[str, Any]:
    """The findings as a SARIF 2.1.0 log with one run (`version` is clang-tidy's, e.g. `clang-tidy 19`)."""
    rules: dict[str, int] = {}
    results = []
    for item in findings:
        index = rules.setdefault(item.check, len(rules))
        results.append(
            {
                "ruleId": item.check,
                "ruleIndex": index,
                "level": "error" if item.level == "error" else "warning",
                "message": {"text": item.message},
                "locations": [
                    {
                        "physicalLocation": {
                            "artifactLocation": {"uri": item.path, "uriBaseId": "%SRCROOT%"},
                            "region": {"startLine": item.line, "startColumn": item.column},
                        }
                    }
                ],
            }
        )
    driver = {
        "name": "clang-tidy",
        "fullName": version,
        "informationUri": "https://clang.llvm.org/extra/clang-tidy/",
        "rules": [{"id": check, "shortDescription": {"text": check}} for check in rules],
    }
    return {
        "$schema": SARIF_SCHEMA,
        "version": SARIF_VERSION,
        "runs": [{"tool": {"driver": driver}, "results": results}],
    }


def _escape(text: str, *, property_value: bool = False) -> str:
    """Text for a GitHub workflow command: `%`, line ends and (in a property) `:` and `,` written as %XX."""
    out = text.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
    return out.replace(":", "%3A").replace(",", "%2C") if property_value else out


def to_annotations(findings: Sequence[Finding]) -> list[str]:
    """The findings as `::error file=...::message` workflow commands, which GitHub shows on the pull request's diff."""
    return [
        f"::{'error' if item.level == 'error' else 'warning'} file={_escape(item.path, property_value=True)},"
        f"line={item.line},col={item.column},title=clang-tidy {_escape(item.check, property_value=True)}"
        f"::{_escape(item.message)}"
        for item in findings
    ]


def _files(count: int) -> str:
    """`1 file` or `3 files`."""
    return f"{count} file{'' if count == 1 else 's'}"


def summary_lines(
    findings: Sequence[Finding], sources: Sequence[str], limit: int, notes: Sequence[str], *, everything: bool = False
) -> list[str]:
    """The job summary: a headline, up to `limit` findings, a count of the rest, and the plan's notes; `everything`
    says the run was the whole-codebase pass, not a change's lines."""
    where = "in every file" if everything else "on the lines this change touches"
    if not sources:
        head = f"clang-tidy: no file to check {where}"
    elif not findings:
        head = f"clang-tidy: clean {where} ({_files(len(sources))} checked)"
    else:
        count = len(findings)
        head = f"clang-tidy: {count} finding{'' if count == 1 else 's'} {where} ({_files(len(sources))} checked)"
    lines = [head]
    lines += [
        f"- {item.path}:{item.line}:{item.column} [{item.check}] {item.message}" for item in list(findings)[:limit]
    ]
    if len(findings) > limit:
        lines.append(f"- ... and {len(findings) - limit} more (the SARIF file and the log hold all of them)")
    lines += [f"note: {note}" for note in notes]
    return lines


def tool_version(tool: str, root: Path, run: Runner) -> str:
    """`clang-tidy 19.1.1`-style text for the SARIF log, from `tool --version`; just `clang-tidy` if it will not say."""
    code, output = run([tool, "--version"], root)
    found = re.search(r"LLVM version (\S+)", output)
    return f"clang-tidy {found.group(1)}" if code == 0 and found else "clang-tidy"
