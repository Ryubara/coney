# SPDX-License-Identifier: GPL-3.0-or-later
"""clang-tidy on this machine, on the lines a branch changes: `task check`'s `clang-tidy` check and `ci tidy-local`
(docs/guides/clang-tidy.md#local).

It is CI's pull-request pass (ci_tidy.py) run here before the pull request is opened, so a finding costs a fix in the
worktree, not a fix unit, a new check, a new review and another CI round. The same steps, from the same code:

1. the branch's diff against its merge base with the base branch (`git diff -U0 --no-renames`, as the workflow);
2. `ci_tidy.make_plan`: a changed source itself, a changed header through one source that includes it;
3. `ci_tidy.run_plan` against the compile database of the `ci` preset's build (`proof.build.preset`), which `task
   check`'s build has just written, with the repository's `.clang-tidy`; only findings on the changed lines count.

Where it differs from CI, and why (docs/guides/clang-tidy.md#local has the measured differences):

* **The compiler's view.** On Windows the database is MSVC's (`cl.exe` commands), so clang-tidy reads the sources as
  clang-cl does, against Microsoft's standard library and with `_WIN32` code paths; CI reads them as Clang 19 on Linux
  against libstdc++. A check that looks at the library's own code shapes can report on one and not the other.
* **Paths.** clang-tidy matches `.clang-tidy`'s `HeaderFilterRegex` (written with `/`) against Windows paths written
  with backslashes, so the regex is handed over with each `/` widened to either slash (`header_filter`); and the line
  filter is applied here, after clang-tidy, instead of by clang-tidy's `--line-filter` (`RunOptions.line_filter`).
* **A build-setup change** (`ci.tidy.whole_when`) makes CI check every file; here only the change's own sources are
  checked, and CI's whole-codebase pass covers the rest.
* **Warnings MSVC lacks.** The warnings `ci.tidy.local_warnings` names (a member left out of an aggregate or designated
  initializer) are on in CI's GCC and Clang builds and not in MSVC's, so they are turned on here
  (`warning_arguments`) and reported on the changed lines like any finding.
* **An unchecked `*optional`.** Microsoft's `std::optional` declares `operator*` in a base class, where
  `bugprone-unchecked-optional-access` does not look, so on Windows the run reads a copy of `<optional>` that declares
  it in `std::optional` (tidy_msvc_optional.py), and reports what CI's libstdc++ run reports.
* **Advice.** On Windows the findings of the checks `ci.tidy.local_advisory` names (they follow the standard library's
  own code: `bugprone-exception-escape`) are printed as advice and fail nothing, as they differ from CI's both ways.

The program is `clang_tidy_program` of coney.local.toml, else the copy `workspace init --fetch clang-tidy` put in the
workspace's tools folder, else `clang-tidy-<major>` or `clang-tidy` on PATH; its major version must be CI's
(`ci.tidy.llvm_major`), or the check cannot run (a newer clang-tidy reports what CI would not, an older one misses
what it would).
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
from collections.abc import Callable
from dataclasses import dataclass, field
from pathlib import Path

from coney_tools import build_root, ci_tidy, tidy_cache, tidy_msvc_optional, workspace_fetch
from coney_tools.config import ConfigError, load_config, local_config_file, workspace_root
from coney_tools.conventions import Conventions

#: The `.clang-tidy` key whose regex chooses the headers clang-tidy reports on.
HEADER_FILTER_KEY = "HeaderFilterRegex"


class TidyUnavailable(ConfigError):
    """clang-tidy cannot run here (no program of the right version, no compile database): nothing is wrong with the
    change, so `task check` writes no stamp."""


@dataclass(frozen=True)
class LocalResult:
    """What a local pass did: the plan it ran, what clang-tidy found and the notes for the reader."""

    plan: ci_tidy.TidyPlan
    result: ci_tidy.TidyResult
    program: str
    notes: list[str] = field(default_factory=list)
    #: Findings of the checks `ci.tidy.local_advisory` names, on Windows: printed as advice, never a failure (they
    #: follow the standard library's own code, which differs from CI's). `result.findings` holds the rest.
    advisory: list[ci_tidy.Finding] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        """True when nothing but advice was found on the changed lines and clang-tidy ran on every source."""
        return self.result.ok

    def lines(self) -> list[str]:
        """Each finding as `path:line:column [check] message`, then each failure of clang-tidy itself, then the advice
        (marked as such)."""
        found = [_line(item) for item in self.result.findings]
        failed = [f"clang-tidy failed: {failure}" for failure in self.result.failures]
        advice = [f"advice, not a failure (may differ from CI): {_line(item)}" for item in self.advisory]
        return found + failed + advice


def _line(item: ci_tidy.Finding) -> str:
    """A finding as `path:line:column [check] message`."""
    return f"{item.path}:{item.line}:{item.column} [{item.check}] {item.message}"


def header_filter(config_text: str, windows: bool) -> str | None:
    """The `--header-filter` to hand clang-tidy on this platform, or None to keep `.clang-tidy`'s own: on Windows its
    `HeaderFilterRegex` with every `/` widened to `[/\\\\]`, as clang-tidy sees the headers' paths with backslashes."""
    if not windows:
        return None
    match = re.search(rf"^{HEADER_FILTER_KEY}:\s*'(?P<regex>[^']*)'", config_text, re.MULTILINE)
    if match is None:
        return None
    return match.group("regex").replace("/", r"[/\\]")


def warning_arguments(warnings: tuple[str, ...]) -> tuple[str, ...]:
    """The clang-tidy arguments that turn on the clang warnings `ci.tidy.local_warnings` names and report them: each
    one's `-W` flag for the compiler, and its `clang-diagnostic-` check (a diagnostic is shown only when its check is
    enabled, and `.clang-tidy`'s own list names none: the `--checks` here is added to it). The compile database of
    Windows is MSVC's, whose /W4 gives neither warning that CI's -Wextra does."""
    if not warnings:
        return ()
    checks = ",".join(f"clang-diagnostic-{name}" for name in warnings)
    return (f"--checks={checks}", *(f"--extra-arg=-W{name}" for name in warnings))


def _version_of(program: str) -> str | None:
    """The LLVM version `program --version` reports (`19.1.1`), or None when it does not run or will not say."""
    try:
        done = subprocess.run([program, "--version"], capture_output=True, text=True, check=False, timeout=60)
    except (OSError, subprocess.SubprocessError):
        return None
    found = re.search(r"LLVM version (\S+)", done.stdout + done.stderr)
    return found.group(1) if found else None


def find_program(
    root: Path,
    major: int,
    which: Callable[[str], str | None] = shutil.which,
    version_of: Callable[[str], str | None] = _version_of,
) -> str:
    """The clang-tidy to run for the checkout at `root` (see the module docstring), checked to be of LLVM `major`;
    TidyUnavailable, saying how to get one, when there is none."""
    hint = (
        "`coney-tools workspace init --fetch clang-tidy`"
        if sys.platform == "win32"
        else f"apt install clang-tidy-{major}, or Homebrew's llvm@{major}"
    )
    configured = load_config(root).paths.get("clang_tidy_program") if local_config_file(root) else None
    candidates: list[str] = []
    if configured is not None:
        if not configured.is_file():
            raise TidyUnavailable(f"clang_tidy_program {configured} is not a file")
        candidates.append(str(configured))
    else:
        fetched = workspace_fetch.tool_destination("clang-tidy", workspace_root(root)) / "bin" / "clang-tidy.exe"
        if fetched.is_file():
            candidates.append(str(fetched))
        candidates += [found for name in (f"clang-tidy-{major}", "clang-tidy") if (found := which(name))]
    if not candidates:
        raise TidyUnavailable(f"clang-tidy {major} is not installed ({hint}; docs/guides/clang-tidy.md#local)")
    program = candidates[0]
    version = version_of(program)
    if version is None or version.split(".")[0] != str(major):
        raise TidyUnavailable(
            f"{program} is clang-tidy {version or 'of an unknown version'}, and CI runs {major} (ci.tidy.llvm_major): "
            f"its findings would differ; install {major} ({hint})"
        )
    return program


def compile_database(root: Path, preset: str) -> Path:
    """The build folder of `preset` holding its compile database; TidyUnavailable when it has none (build first)."""
    folder = build_root.preset_folder(root, preset)
    if not (folder / "compile_commands.json").is_file():
        raise TidyUnavailable(
            f"no compile database in {folder}: build the {preset} preset first "
            f"(`coney-tools build --preset {preset}`; task check builds it before this check)"
        )
    return folder


def branch_diff(root: Path, base: str) -> str:
    """The branch's change against its merge base with `base`, as the workflow diffs it (`-U0 --no-renames`)."""

    def git(*words: str) -> str:
        done = subprocess.run(
            ["git", "-c", "core.quotepath=false", *words],
            cwd=root,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )
        if done.returncode != 0:
            raise ConfigError(f"git {' '.join(words)}: {done.stderr.strip()}")
        return done.stdout

    merge_base = git("merge-base", base, "HEAD").strip()
    return git("diff", "-U0", "--no-renames", "--no-color", "--no-ext-diff", merge_base, "HEAD")


def default_jobs(root: Path) -> int:
    """clang-tidy processes at once: a check slot's share of the cores, as the pytest run takes, at least one."""
    from coney_tools import check_slots  # check_slots reads the machine's configuration; only a real run needs it

    return max(1, (os.cpu_count() or 1) // max(1, check_slots.default_check_slots(root).slots))


def run_local(
    root: Path,
    base: str,
    conventions: Conventions,
    run: ci_tidy.Runner,
    *,
    program: str,
    build_dir: Path,
    jobs: int,
    diff: str | None = None,
    windows: bool = sys.platform == "win32",
    probe: ci_tidy.Runner | None = None,
) -> LocalResult:
    """clang-tidy on the lines the branch at `root` changes against `base` (or `diff`, when given), with `program`
    against the compile database in `build_dir`, `jobs` at a time. On Windows, a `probe` (a runner of clang-tidy for the
    one-off run that finds Microsoft's `<optional>`) makes the run see an unchecked `*optional` too
    (tidy_msvc_optional.py)."""
    settings = conventions.ci.tidy
    text = branch_diff(root, base) if diff is None else diff
    plan = ci_tidy.make_plan(ci_tidy.changed_lines(text), root, settings, ci_tidy.touched_files(text))
    notes = list(plan.notes)
    if plan.everything:
        plan.everything = False  # here only the change's own sources; CI's whole-codebase pass does the rest
        notes.append("the build setup changed: CI checks every file; this run checks the change's own sources")
    config_file = conventions.repo.tools["tidy"]
    try:
        config = (root / config_file).read_text(encoding="utf-8")
    except OSError as error:
        raise ConfigError(f"{config_file}: cannot be read ({error})") from error
    regex = header_filter(config, windows)
    header_arguments = (f"--header-filter={regex}",) if regex else ()
    extra: tuple[str, ...] = (*header_arguments, *warning_arguments(settings.local_warnings))
    if windows and probe is not None:
        model = tidy_msvc_optional.prepare(program, build_dir, probe)
        extra += model.arguments()
        notes += model.notes
    options = ci_tidy.RunOptions(extra_args=extra, line_filter=not windows)
    # A source the database does not know (the build is older than the branch) would be read with a guessed command,
    # which is not the build's: clang-tidy then reports compile errors that are not there, so it is not run.
    known = tidy_cache.load_commands(build_dir, root)
    missing = [unit.source for unit in plan.units if unit.source not in known]
    runnable = ci_tidy.TidyPlan([unit for unit in plan.units if unit.source in known], plan.lines, plan.notes)
    result = ci_tidy.run_plan(runnable, root, build_dir, program, jobs, run, options)
    result.failures += [f"{source}: not in the compile database of {build_dir}; build again" for source in missing]
    advisory = [item for item in result.findings if windows and item.check in settings.local_advisory]
    result.findings = [item for item in result.findings if item not in advisory]
    return LocalResult(plan, result, program, notes, advisory)
