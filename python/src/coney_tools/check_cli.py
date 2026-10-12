# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools check changed` command: what CI would fail on, checked locally for the files a change touched.

It is the step before handing a unit over (docs/guides/testing.md#local-gate). Each step runs only when the change
touches what it checks; the command prints one line per step and exits 1 when any step fails.
"""

from __future__ import annotations

import os
import re
import shutil
import tempfile
import time
from collections.abc import Callable
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from coney_tools import local_gate
from coney_tools.audit import Report, run_checks, specs_in
from coney_tools.audit_files import FilesContext
from coney_tools.config import ConfigError, find_repo_root
from coney_tools.conventions import Paths, load_conventions
from coney_tools.local_gate import StepResult

#: The steps, in the order they run and print; `--only` and `--skip` take these names.
STEPS: tuple[str, ...] = ("repo", "pre-commit", "clang-tidy", "docs", "python")
#: Characters of file names handed to one `pre-commit run --files`; Windows refuses a command line over ~32,000.
COMMAND_LINE_BUDGET = 20000
#: Lines of a failed clang-tidy run shown when nothing in it looks like a diagnostic.
FALLBACK_TAIL_LINES = 30


def _timed(name: str, work: Callable[[], StepResult]) -> StepResult:
    """Run one step and stamp its duration on the result."""
    started = time.monotonic()
    result = work()
    return StepResult(name, result.ok, result.detail, time.monotonic() - started, result.skipped, result.output)


def _skipped(name: str, why: str) -> StepResult:
    """A step that has nothing to check for this change."""
    return StepResult(name, True, why, skipped=True)


def _tail(lines: list[str]) -> tuple[str, ...]:
    """The lines worth printing for a failure: the first MAX_OUTPUT_LINES, and a count of the rest."""
    kept = lines[: local_gate.MAX_OUTPUT_LINES]
    if len(lines) > len(kept):
        kept.append(f"... {len(lines) - len(kept)} more lines; rerun the step by hand for all of it")
    return tuple(kept)


def _step_repo(root: Path) -> StepResult:
    """`coney-tools repo audit --files`, which CI runs: the conventions file loads, the agent pointer files, the
    generated blocks, the issue forms, the pull request template, the CI scope script and pre-commit's size limit
    agree with it."""
    conventions = load_conventions(root)
    report = Report(("files",))
    run_checks(specs_in(conventions, ["files"]), {"files": FilesContext(root, conventions)}, report)
    problems = [f"{finding.check} {finding.item}: {finding.message}" for finding in report.findings]
    return StepResult(
        "repo",
        not problems,
        f"{len(report.ran)} audit checks" if not problems else f"{len(problems)} problems",
        output=tuple(problems),
    )


def _chunks(names: list[str], budget: int = COMMAND_LINE_BUDGET) -> list[list[str]]:
    """`names` in order, split so each group's names (and a separating space each) stay within `budget` characters."""
    groups: list[list[str]] = []
    used = 0
    for name in names:
        if not groups or used + len(name) + 1 > budget:
            groups.append([])
            used = 0
        groups[-1].append(name)
        used += len(name) + 1
    return groups


def _step_pre_commit(root: Path, changed: list[str]) -> StepResult:
    """pre-commit on the changed files (formatting, hygiene, the no-real-clock rule, the 512 KB guard)."""
    if not changed:
        return _skipped("pre-commit", "no changed files")
    runner = ["uvx", "pre-commit"] if shutil.which("uvx") else ["pre-commit"]
    # A big change would overflow the command line limit of Windows, so the files go in groups.
    failures: list[str] = []
    failed = False
    for group in _chunks(changed):
        code, output = local_gate.run_command([*runner, "run", "--files", *group], root)
        if code != 0:
            failed = True
            failures.extend(output.splitlines())
    if not failed:
        return StepResult("pre-commit", True, f"{len(changed)} files")
    # A hook that rewrote a file fails once; the rerun passes. Say so, since the files changed under the agent.
    lines = [line for line in failures if line.strip()]
    if any("files were modified by this hook" in line for line in lines):
        lines.append("hooks rewrote files: review `git diff`, then run this command again")
    return StepResult("pre-commit", False, "failed", output=_tail(lines))


def _tidy_one(command: list[str], root: Path, source: str) -> tuple[str, int, list[str], list[str]]:
    """Run clang-tidy on one source; returns the source, the exit code, its meaningful output lines and all of them."""
    code, output = local_gate.run_command(command, root)
    return source, code, local_gate.tidy_output(output), [line for line in output.splitlines() if line.strip()]


def _is_diagnostic(line: str) -> bool:
    """True for a clang-tidy output line worth showing: an indented code excerpt, a file location or an error."""
    return line.startswith(("  ", "/")) or re.match(r"[A-Za-z]:", line) is not None or "error" in line.lower()


def _step_tidy(root: Path, changed: list[str], build_dir: Path, jobs: int, clang_tidy: str | None) -> StepResult:
    """clang-tidy on what the change touches, with the GCC and Clang warnings on."""
    sources = local_gate.tidy_sources(root, changed)
    if not sources:
        return _skipped("clang-tidy", "no C++ changed")
    tool, major = local_gate.find_clang_tidy(clang_tidy)
    database = local_gate.load_compile_db(build_dir)
    absent = local_gate.missing_from_database(root, sources, database)
    if absent:
        raise ConfigError(
            f"{build_dir / 'compile_commands.json'} has no entry for {', '.join(absent[:3])}"
            f"{' ...' if len(absent) > 3 else ''}: it belongs to another checkout, or the files are new and need "
            "`coney-tools build --configure-only --agent <id>` again"
        )
    checks = local_gate.load_tidy_checks(root)
    first = database[local_gate.comparable(root / sources[0])]
    extra = local_gate.warning_args(local_gate.uses_clang_cl_driver(first))
    notes: list[str] = []
    if local_gate.uses_gcc_driver(first):
        notes.append(
            f"note: {build_dir} is a GCC compile database; CI tidies a Clang one and clang-tidy can reject GCC-only "
            "flags. Configure one with `coney-tools build --agent <id> --configure-only --preset dev "
            f"--build-dir build/tidy --env CC=clang-{local_gate.TIDY_MAJOR} --env CXX=clang++-{local_gate.TIDY_MAJOR}` "
            "and pass `--build-dir build/tidy` here"
        )
    with tempfile.TemporaryDirectory(prefix="coney-tidy-") as scratch:
        database_dir = local_gate.without_missing_response_files(database, Path(scratch))
        commands = [(local_gate.clean_tidy_command(tool, database_dir, checks, extra, name), name) for name in sources]
        with ThreadPoolExecutor(max_workers=max(1, min(jobs, len(commands)))) as pool:
            results = list(pool.map(lambda item: _tidy_one(item[0], root, item[1]), commands))
    failed = [(name, lines, everything) for name, code, lines, everything in results if code != 0]
    version = f"clang-tidy {major}" if major is not None else "clang-tidy"
    if major != local_gate.TIDY_MAJOR:
        notes.append(f"note: CI runs clang-tidy {local_gate.TIDY_MAJOR}, this is {major}")
    if not failed:
        return StepResult("clang-tidy", True, f"{len(sources)} files ({version})", output=tuple(notes))
    lines: list[str] = []
    untouched = 0
    for name, found, everything in failed:
        # A file reached through a changed header can fail for a reason that is not the change's.
        if name in changed:
            lines.append(f"{name}:")
        else:
            lines.append(f"{name} (not edited; it includes a changed header):")
            untouched += 1
        shown = [line for line in found if _is_diagnostic(line)]
        # A crash, or a message in a form the filter does not know, must not leave a failure with nothing to read.
        shown = shown or everything[-FALLBACK_TAIL_LINES:]
        lines.extend(f"  {line[:200]}" for line in shown)
    if untouched:
        lines.append(
            "note: a finding in a file you did not edit can come from MSVC's standard library, which CI's libstdc++ "
            "does not share; read it, and fix it only if the cause is in your change"
        )
    return StepResult(
        "clang-tidy", False, f"{len(failed)} of {len(sources)} files ({version})", output=_tail(lines + notes)
    )


def _docs_changed(changed: list[str], paths: Paths) -> bool:
    """True when the change touches the documentation build (a file matching `paths.docs_only`)."""
    return any(paths.is_docs_only(name) for name in changed)


def _step_docs(root: Path, changed: list[str]) -> StepResult:
    """`mkdocs build --strict` into a throwaway folder, when docs or the site setup changed."""
    if not _docs_changed(changed, load_conventions(root).paths):
        return _skipped("docs", "no docs changed")
    with tempfile.TemporaryDirectory(prefix="coney-site-") as site:
        venv_python = (
            root / ".venv" / ("Scripts" if os.name == "nt" else "bin") / ("python.exe" if os.name == "nt" else "python")
        )
        if venv_python.is_file():
            command = [str(venv_python), "-m", "mkdocs", "build", "--strict", "--site-dir", site]
        elif shutil.which("uv"):
            command = [
                "uv",
                "run",
                "--no-project",
                "--with-requirements",
                "requirements-docs.txt",
                "python",
                "-m",
                "mkdocs",
                "build",
                "--strict",
                "--site-dir",
                site,
            ]
        else:
            raise ConfigError("mkdocs needs the root .venv (docs/guides/building.md) or uv on PATH")
        code, output = local_gate.run_command(command, root)
    if code == 0:
        return StepResult("docs", True, "mkdocs build --strict")
    lines = [
        line for line in output.splitlines() if line.startswith(("WARNING", "ERROR", "Aborted")) or "Error" in line
    ]
    return StepResult("docs", False, "mkdocs build --strict failed", output=_tail(lines or output.splitlines()))


def _step_python(root: Path, changed: list[str]) -> StepResult:
    """pytest and mypy over python/, when anything under it changed (ruff runs in pre-commit)."""
    if not any(name.startswith("python/") for name in changed):
        return _skipped("python", "no Python changed")
    if not shutil.which("uv"):
        raise ConfigError("uv is not on PATH (docs/guides/building.md#python-tools)")
    problems: list[str] = []
    for label, command in (
        ("pytest", ["uv", "run", "--project", "python", "pytest", "python/tests", "-q", "-x", "--no-header"]),
        ("mypy", ["uv", "run", "--project", "python", "mypy", "python/src", "python/tests"]),
    ):
        code, output = local_gate.run_command(command, root)
        if code != 0:
            problems.append(f"{label} failed:")
            problems.extend(f"  {line}" for line in output.splitlines()[-15:] if line.strip())
    if problems:
        return StepResult("python", False, "pytest or mypy failed", output=_tail(problems))
    return StepResult("python", True, "pytest, mypy")


def run_changed(
    base: str, build_dir: str, only: list[str] | None, skip: list[str] | None, jobs: int, clang_tidy: str | None
) -> int:
    """Check what the change since `base` touched; print a line per step; 0 when all pass, 1 when any fails."""
    root = find_repo_root(Path.cwd())
    wanted = [step for step in STEPS if (not only or step in only) and step not in (skip or [])]
    changed = local_gate.changed_files(root, base)
    print(f"changed since {base}: {len(changed)} files")
    build_path = Path(build_dir)
    if not build_path.is_absolute():
        build_path = root / build_path
    steps: dict[str, Callable[[], StepResult]] = {
        "repo": lambda: _step_repo(root),
        "pre-commit": lambda: _step_pre_commit(root, changed),
        "clang-tidy": lambda: _step_tidy(root, changed, build_path, jobs, clang_tidy),
        "docs": lambda: _step_docs(root, changed),
        "python": lambda: _step_python(root, changed),
    }
    if local_gate.needs_full_tidy_pass(changed) and "clang-tidy" in wanted:
        print("note: the build or tidy setup changed: CI tidies every file, this run only the changed ones")
    failed = False
    for name in wanted:
        result = _timed(name, steps[name])
        mark = "skip" if result.skipped else "ok  " if result.ok else "FAIL"
        timing = "" if result.skipped else f" ({result.seconds:.0f}s)"
        print(f"{mark} {name}: {result.detail}{timing}")
        for line in result.output:
            print(f"     {line}")
        failed = failed or not result.ok
    if failed:
        print("coney-tools check changed: FAILED (CI would fail too)")
        return 1
    print("coney-tools check changed: ok. CI only: GCC-only warnings, macOS, sanitizers, a full clang-tidy pass.")
    return 0
