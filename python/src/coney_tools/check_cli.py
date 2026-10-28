# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools ci run` command: what CI would fail on, checked locally for the files a change touched.

It is the step before handing a unit over (docs/guides/testing.md#local-gate). Each step runs only when the change
touches what it checks; the command prints one line per step and exits 1 when any step fails. clang-tidy is not a step:
CI tidies the lines of every pull request (`ci_tidy`) and the whole codebase nightly. `--detach` runs the whole gate in
the background, as `build --detach` does, and `ci result` reads its outcome.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import sys
import tempfile
import time
from collections.abc import Callable, Mapping
from pathlib import Path

from coney_tools import (
    build_cli,
    docs_check,
    docs_cli,
    local_gate,
    orig_check,
    orig_copy,
    process_lifetime,
    standards_check,
)
from coney_tools.audit import Report, run_checks, specs_in
from coney_tools.audit_files import FilesContext
from coney_tools.build_run import EXIT_QUEUED
from coney_tools.config import ConfigError, find_repo_root
from coney_tools.conventions import Paths, load_conventions
from coney_tools.local_gate import StepResult
from coney_tools.pcsx2_claims import AGENT_NAME

#: The steps, in the order they run and print; `--only` and `--skip` take these names.
STEPS: tuple[str, ...] = ("repo", "orig", "pre-commit", "docs", "python")
#: Characters of file names handed to one `pre-commit run --files`; Windows refuses a command line over ~32,000.
COMMAND_LINE_BUDGET = 20000
#: The mypy run, the same words as CI's python job. It runs from the repository root, where mypy finds no config, so
#: `--config-file` hands it the `[tool.mypy] strict = true` of python/pyproject.toml (without it the run is not strict).
MYPY_COMMAND: tuple[str, ...] = (
    "uv",
    "run",
    "--project",
    "python",
    "mypy",
    "--config-file",
    "python/pyproject.toml",
    "python/src",
    "python/tests",
)
#: What ruff lints: the package and the hook scripts every Claude Code session on a machine runs. The one source of
#: these paths for the gate; CI's python job and pre-commit's ruff hooks spell the same two and a test compares them.
#: Each folder has its own config (python/pyproject.toml; .claude/hooks/ruff.toml extends it), so the same rules apply.
RUFF_PATHS: tuple[str, ...] = ("python", ".claude/hooks")
#: The ruff runs, the same words as CI's python job.
RUFF_COMMANDS: tuple[tuple[str, ...], ...] = tuple(
    ("uv", "run", "--project", "python", "ruff", *action, *RUFF_PATHS) for action in (("check",), ("format", "--check"))
)
#: The files in the worktree's scratch folder that a --detach run records itself in, and its console output.
RESULT_FILE = "check-result.json"
DETACHED_OUTPUT = "check-detached.out"


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


def _step_orig(root: Path, base: str, changed: list[str]) -> StepResult:
    """`coney-tools orig check-tags` and `orig check-copy`, which CI runs: a new function in a gameplay folder carries
    `@orig` or `@coney-only`, every folder of `src/` is listed as gameplay or Coney-only, and no changed C++ line
    carries a decompiler fingerprint (docs/guides/conventions.md#porting)."""
    if not any(name.startswith("src/") for name in changed):
        return _skipped("orig", "src/ unchanged")
    problems = orig_check.check(root, base) + [str(finding) for finding in orig_copy.check(root, base)]
    if problems:
        return StepResult(
            "orig",
            False,
            f"{len(problems)} untagged functions, unlisted folders or fingerprints",
            output=_tail(problems),
        )
    return StepResult("orig", True, "every new gameplay function carries @orig or @coney-only; no fingerprints")


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


#: Python files the docs build imports (mkdocs_hooks.py loads the progress tracker, the function database and its
#: pages, and the config errors, from python/src): a name starting with one of these changes what the build produces.
_DOCS_BUILD_PYTHON = (
    "python/src/coney_tools/progress",
    "python/src/coney_tools/research",
    "python/src/coney_tools/config.py",
)


def _docs_changed(changed: list[str], paths: Paths) -> bool:
    """True when the change touches the documentation build: a file matching `paths.docs_only`, or the python that
    the build imports (the progress tracker)."""
    return any(paths.is_docs_only(name) or name.startswith(_DOCS_BUILD_PYTHON) for name in changed)


def _python_changed(changed: list[str]) -> bool:
    """True when the change touches what pytest and mypy check: `python/`, and `.claude/`, whose hooks the tests run."""
    return any(name.startswith(("python/", ".claude/")) for name in changed)


def _standards(root: Path, base: str) -> tuple[int, list[str], list[str]]:
    """The page standards (`coney-tools docs check`, as CI runs it) on the pages changed since `base`: how many pages
    were checked, the findings of the pages that fail, and the warnings of those that pass. A listed research page is
    read as touched (`docs_check.change_touches`), and the legacy file is checked when the change can make it stale."""
    result = docs_check.check_changed(root, base, docs_check.context_for(root))
    failed = result.failed
    problems = [line for page in result.pages if page in failed for line in docs_cli.page_lines(result, page)]
    notes = [line for page in result.pages if page not in failed for line in docs_cli.page_lines(result, page)]
    return len(result.pages), problems, notes


def _step_docs(root: Path, base: str, changed: list[str]) -> StepResult:
    """The page standards on the changed pages, then `mkdocs build --strict` into a throwaway folder, when docs or the
    site setup changed. Both run, so one run shows every problem. A change to source or test files runs the standards
    alone: they also hold the touched files and the legacy marks (`standards_check`), as CI's `docs check` does."""
    conventions = load_conventions(root)
    site = _docs_changed(changed, conventions.paths)
    if not site and not standards_check.relevant(changed, conventions):
        return _skipped("docs", "no docs changed")
    pages, problems, notes = _standards(root, base)
    built = _mkdocs_build(root) if site else StepResult("docs", True, "no docs changed: mkdocs build skipped")
    ok = built.ok and not problems
    parts = [f"{pages} pages meet the standards" if not problems else "page standards failed"]
    if problems:
        problems.append("bring each failing page wholly up to docs/guides/writing-docs.md; `coney-tools docs check`")
    parts.append(built.detail)
    return StepResult("docs", ok, ", ".join(parts), output=_tail([*problems, *built.output, *notes]))


def _mkdocs_build(root: Path) -> StepResult:
    """`mkdocs build --strict` into a throwaway folder."""
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
    return StepResult("docs", False, "mkdocs build --strict failed", output=tuple(lines or output.splitlines()))


def _step_python(root: Path, changed: list[str]) -> StepResult:
    """ruff, pytest and mypy, when anything under `python/` or `.claude/` changed (the tests also run the hooks).

    ruff runs here over every folder in RUFF_PATHS, as CI does; pre-commit also runs it on the changed files.
    """
    if not _python_changed(changed):
        return _skipped("python", "no Python changed")
    if not shutil.which("uv"):
        raise ConfigError("uv is not on PATH (docs/guides/building.md#python-tools)")
    problems: list[str] = []
    for label, command in (
        *(
            (" ".join(command[command.index("ruff") : command.index("ruff") + 2]), list(command))
            for command in RUFF_COMMANDS
        ),
        ("pytest", ["uv", "run", "--project", "python", "pytest", "python/tests", "-q", "-x", "--no-header"]),
        ("mypy", list(MYPY_COMMAND)),
    ):
        code, output = local_gate.run_command(command, root)
        if code != 0:
            problems.append(f"{label} failed:")
            problems.extend(f"  {line}" for line in output.splitlines()[-15:] if line.strip())
    if problems:
        return StepResult("python", False, "ruff, pytest or mypy failed", output=_tail(problems))
    return StepResult("python", True, "ruff, pytest, mypy")


def default_agent(root: Path, env: Mapping[str, str]) -> str:
    """Who a detached gate's record is for: `CONEY_AGENT`, else the worktree's folder name, with any character an
    agent id may not hold replaced by `_`."""
    named = env.get("CONEY_AGENT") or root.name
    cleaned = re.sub(r"[^A-Za-z0-9._-]", "_", named)[:64] or "gate"
    return cleaned if AGENT_NAME.match(cleaned) else "gate"


def run_changed(base: str, only: list[str] | None, skip: list[str] | None) -> int:
    """Check what the change since `base` touched; print a line per step; 0 when all pass, 1 when any fails."""
    root = find_repo_root(Path.cwd())
    wanted = [step for step in STEPS if (not only or step in only) and step not in (skip or [])]
    changed = local_gate.changed_files(root, base)
    print(f"changed since {base}: {len(changed)} files")
    steps: dict[str, Callable[[], StepResult]] = {
        "repo": lambda: _step_repo(root),
        "orig": lambda: _step_orig(root, base, changed),
        "pre-commit": lambda: _step_pre_commit(root, changed),
        "docs": lambda: _step_docs(root, base, changed),
        "python": lambda: _step_python(root, changed),
    }
    failed = False
    for name in wanted:
        result = _timed(name, steps[name])
        mark = "skip" if result.skipped else "ok  " if result.ok else "FAIL"
        timing = "" if result.skipped else f" ({result.seconds:.0f}s)"
        print(f"{mark} {name}: {result.detail}{timing}", flush=True)
        for line in result.output:
            print(f"     {line}")
        failed = failed or not result.ok
    if failed:
        print("coney-tools ci run: FAILED (CI would fail too)")
        return 1
    print("coney-tools ci run: ok. CI only: GCC-only warnings, macOS, sanitizers, clang-tidy on changed lines.")
    return 0


def run_check(args: argparse.Namespace) -> int:
    """`ci run` (in the foreground, as a --detach run's child, or starting one) and `ci result`."""
    if args.command == "result":
        return run_result(Path.cwd())
    if args.result_file is not None:
        return _detached_child(args)
    if args.detach:
        return _start_detached(args)
    process_lifetime.bind_children()  # an interrupted gate leaves none of its programs behind
    return run_changed(args.base, args.only, args.skip)


def _scratch_folder(root: Path) -> Path:
    """The worktree's folder in the scratch directory, where `build` keeps its logs and results too."""
    return build_cli.scratch_root(root) / root.name


def _child_command(args: argparse.Namespace, result: Path) -> list[str]:
    """The command line of a --detach run's child: this run's flags and its result file."""
    command = [sys.executable, "-m", "coney_tools.cli", "ci", "run", "--base", args.base]
    for flag, values in (("--only", args.only), ("--skip", args.skip)):
        for value in values or []:
            command += [flag, value]
    return [*command, "--result-file", str(result)]


def _start_detached(args: argparse.Namespace) -> int:
    """--detach: record the run as started, start its child apart from this shell and return at once. Refuses a
    second detached gate in a worktree whose last one still runs (they would share one result file)."""
    root = find_repo_root(Path.cwd())
    folder = _scratch_folder(root)
    result = folder / RESULT_FILE
    previous = build_cli._read_record(result)
    if previous is not None and previous.get("state") == "running" and build_cli._alive(previous):
        raise ConfigError(
            f"a detached check of {previous.get('agent')} is still running (pid {previous.get('pid')}): "
            "`coney-tools ci result` shows how it ends"
        )
    output = folder / DETACHED_OUTPUT
    agent = default_agent(root, os.environ)
    record = {"state": "running", "agent": agent, "pid": None, "log": str(output), "started": time.time()}
    build_cli._write_record(result, record)
    pid = build_cli.spawn_detached(_child_command(args, result), root, output)
    print(f"detached: pid {pid}, output in {output}")
    print(
        "write STATUS.md and stop, or do other work: `coney-tools ci result` shows the outcome (exit 75 while it runs)"
    )
    return 0


def _detached_child(args: argparse.Namespace) -> int:
    """The child of a --detach run: the gate itself, its exit code recorded in --result-file however it ends."""
    record = {
        "state": "running",
        "agent": default_agent(find_repo_root(Path.cwd()), os.environ),
        "pid": os.getpid(),
        # When this process started, so a later process given the same pid is not taken for this run (build_cli._alive).
        "process_started": process_lifetime.process_started(os.getpid()),
        "log": str(args.result_file.parent / DETACHED_OUTPUT),
        "started": time.time(),
    }
    build_cli._write_record(args.result_file, record)
    code = 1
    try:
        process_lifetime.bind_children()
        process_lifetime.stop_on_signals()
        code = run_changed(args.base, args.only, args.skip)
        return code
    except ConfigError as error:
        print(f"coney-tools: {error}")
        code = 2
        return code
    except KeyboardInterrupt:
        code = 130
        raise
    finally:
        sys.stdout.flush()
        build_cli._write_record(args.result_file, {**record, "state": "done", "exit_code": code, "ended": time.time()})


def run_result(directory: Path) -> int:
    """`ci result`: the output and outcome of the worktree's last --detach gate. Exit: the gate's own code once it
    has ended, 75 while it runs, 1 when there is none or it ended without a result."""
    root = find_repo_root(directory)
    folder = _scratch_folder(root)
    record = build_cli._read_record(folder / RESULT_FILE)
    if record is None:
        print("no detached check recorded for this worktree")
        return 1
    output = folder / DETACHED_OUTPUT
    try:
        print(output.read_text(encoding="utf-8", errors="replace").rstrip())
    except OSError:
        print(f"(no output in {output} yet)")
    if record.get("state") == "running":
        if build_cli._alive(record):
            print(f"still running (pid {record.get('pid')}): look again later")
            return EXIT_QUEUED
        print(f"ended without a result (pid {record.get('pid')} is gone)")
        return 1
    code = int(record.get("exit_code", 1))
    print(f"exit {code}")
    return code
