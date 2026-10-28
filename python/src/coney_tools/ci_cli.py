# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools ci`: the commands the CI workflows run (docs/guides/coney-tools-ci.md).

Each command is a thin wrapper: the logic is in the module it names (`ci_dependencies`, `ci_tidy`, `ci_failure`), so a
workflow holds no logic of its own and a person can run the same command by hand.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path

from coney_tools import ci_dependencies, ci_failure, ci_tidy
from coney_tools.audit_github import Runner, run_gh
from coney_tools.config import ConfigError, find_repo_root
from coney_tools.conventions import load_conventions


def add_ci_parser(
    groups: argparse._SubParsersAction[argparse.ArgumentParser],
) -> argparse._SubParsersAction[argparse.ArgumentParser]:
    """Add the `ci` group and its workflow commands to the top-level parser; return its commands, to which the local
    gate (`ci run`, `ci result`) is added by the caller."""
    ci = groups.add_parser(
        "ci", help="CI's checks: run locally on what a change touched (run, result), and the commands the workflows run"
    )
    commands = ci.add_subparsers(dest="command", required=True)
    dependencies = commands.add_parser(
        "dependencies",
        help="fail while a pull request this one says it `Depends on` is not merged",
        description=(
            "Reads the pull request's description (--pr, from GitHub as it is now; or --body, a file) for lines "
            "`Depends on #N` (pull_requests.stacked.depends_line) and asks GitHub whether each is merged. Exit code 0 "
            "when none is waiting, 1 when one is, 2 when gh or the conventions cannot be read."
        ),
    )
    dependencies.add_argument(
        "--pr", type=int, metavar="N", help="the pull request; its description is read from GitHub"
    )
    dependencies.add_argument(
        "--body", metavar="FILE", help="a file holding the description (no --pr: not checked for itself)"
    )
    rerun = commands.add_parser(
        "rerun-dependencies",
        help="restart the failed `dependencies` run of each pull request whose dependencies have all merged",
        description=(
            "Reads the open pull requests, and for each whose `Depends on` pull requests are all merged and whose "
            "newest `dependencies` run failed, lists (or with --apply restarts) that run. The workflow runs it on "
            "every push to the default branch. Exit code 0, or 2 when gh or the conventions cannot be read."
        ),
    )
    rerun.add_argument(
        "--apply", action="store_true", help="really restart the runs (gh run rerun); default: list them"
    )
    failure = commands.add_parser(
        "report-failure",
        help="open a P-critical issue for a failed run on the default branch (or comment on the open one)",
        description=(
            "Reads the run (--run: its id), and when it is a failure of a watched workflow (ci.failure.workflows) on "
            "the default branch, from an event that counts (ci.failure.events), files an issue in the task form with "
            "the failed jobs, the log's errors and the last commits of the files it names, labelled "
            "ci.failure.labels; a workflow that already has an open issue gets a comment instead. Without --apply "
            "it prints what it would write. Exit code 0, or 2 when gh or the conventions cannot be read."
        ),
    )
    failure.add_argument("--run", type=int, required=True, metavar="ID", help="the workflow run's id")
    failure.add_argument("--apply", action="store_true", help="really create the issue or comment (default: print it)")
    plan = commands.add_parser(
        "tidy-plan",
        help="choose the files and lines clang-tidy checks for a pull request's diff",
        description=(
            "Reads a unified diff (git diff -U0 --no-renames base...head; - for stdin) and writes the plan: the "
            "sources to check (a changed source itself, a changed header through one source that includes it) and the "
            "changed lines to report. A change to the build setup (ci.tidy.whole_when) makes it a plan for every "
            "file. Sets run and all (true or false) in $GITHUB_OUTPUT when it is set. Exit code 0."
        ),
    )
    plan.add_argument("--diff", required=True, metavar="FILE", help="the diff; - reads stdin")
    plan.add_argument("--out", required=True, metavar="FILE", help="where the plan (JSON) is written")
    tidy_run = commands.add_parser(
        "tidy-run",
        help="run clang-tidy as a plan says and report only the changed lines",
        description=(
            "Runs clang-tidy on each source of the plan (several at once) with the plan's line filter, against the "
            "compile database of --build-dir. Prints each finding, writes a SARIF file (--sarif) for code scanning, "
            "a summary (--summary, appended: $GITHUB_STEP_SUMMARY) and with --annotate workflow commands that "
            "annotate the pull request's diff. Exit code 0 when clean, 1 on a finding or when clang-tidy failed, "
            "2 when the plan cannot be read."
        ),
    )
    tidy_run.add_argument("--plan", required=True, metavar="FILE", help="the plan `tidy-plan` wrote")
    tidy_run.add_argument("--build-dir", required=True, metavar="DIR", help="the folder holding compile_commands.json")
    tidy_run.add_argument("--clang-tidy", default="clang-tidy", metavar="PROGRAM", help="default: %(default)s")
    tidy_run.add_argument("--jobs", type=int, metavar="N", help="clang-tidy processes at once (default: the cores)")
    tidy_run.add_argument("--sarif", metavar="FILE", help="write the findings as SARIF (also when there are none)")
    tidy_run.add_argument("--summary", metavar="FILE", help="append a Markdown summary to this file")
    tidy_run.add_argument(
        "--annotate",
        action="store_true",
        help="also print ::error workflow commands (a fork's token cannot upload SARIF)",
    )
    return commands


def _dependencies(args: argparse.Namespace, run: Runner) -> int:
    """`ci dependencies`: 0 when nothing is waited on, 1 when something is."""
    if args.pr is None and args.body is None:
        raise ConfigError("ci dependencies needs --pr or --body")
    conventions = load_conventions(find_repo_root(Path.cwd()))
    line = conventions.pull_requests.stacked.depends_line
    if args.body is not None:
        try:
            body = Path(args.body).read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError) as error:
            raise ConfigError(f"{args.body}: cannot be read ({error})") from error
    else:
        body = str(json.loads(run(["pr", "view", str(args.pr), "--json", "body"])).get("body") or "")
    numbers = ci_dependencies.dependency_numbers(body, line)
    if not numbers:
        print("dependencies: ok (no Depends on line)")
        return 0
    waiting = ci_dependencies.waiting_on(args.pr or 0, body, line, run)
    if not waiting:
        print(f"dependencies: ok ({', '.join(f'#{n}' for n in numbers)} merged)")
        return 0
    for text in ci_dependencies.format_waiting(args.pr or 0, waiting):
        print(text)
    return 1


def _rerun_dependencies(args: argparse.Namespace, run: Runner) -> int:
    """`ci rerun-dependencies`: print one line per run restarted (or planned)."""
    conventions = load_conventions(find_repo_root(Path.cwd()))
    for result in ci_dependencies.rerun_waiting(conventions, run, args.apply):
        print(result.line())
    return 0


def _tidy_plan(args: argparse.Namespace) -> int:
    """`ci tidy-plan`: write the plan for a diff and say what it will check."""
    root = find_repo_root(Path.cwd())
    try:
        text = sys.stdin.read() if args.diff == "-" else Path(args.diff).read_text(encoding="utf-8", errors="replace")
    except OSError as error:
        raise ConfigError(f"{args.diff}: cannot be read ({error})") from error
    settings = load_conventions(root).ci.tidy
    plan = ci_tidy.make_plan(ci_tidy.changed_lines(text), root, settings, ci_tidy.touched_files(text))
    Path(args.out).write_text(json.dumps(plan.as_json(), indent=1) + "\n", encoding="utf-8")
    if plan.everything:
        print("clang-tidy will check every file (the build setup changed): the job runs the whole-codebase pass.")
    elif plan.units:
        print(f"clang-tidy will check {len(plan.units)} file{'' if len(plan.units) == 1 else 's'}:")
        for unit in plan.units:
            print(f"  {unit.source}")
    else:
        print("Nothing for clang-tidy to check.")
    for note in plan.notes:
        print(f"note: {note}")
    output = os.environ.get("GITHUB_OUTPUT")
    if output:
        with Path(output).open("a", encoding="utf-8") as handle:
            # `all` sends the job to the whole-codebase pass; `run` says the job has anything to build and check.
            handle.write(f"run={'true' if plan.units or plan.everything else 'false'}\n")
            handle.write(f"all={'true' if plan.everything else 'false'}\n")
    return 0


def _tidy_run(args: argparse.Namespace, runner: ci_tidy.Runner) -> int:
    """`ci tidy-run`: run the plan; 1 on a finding or a failure of clang-tidy."""
    root = find_repo_root(Path.cwd())
    try:
        plan = ci_tidy.TidyPlan.from_json(json.loads(Path(args.plan).read_text(encoding="utf-8")))
    except (OSError, ValueError, KeyError, TypeError) as error:
        raise ConfigError(f"{args.plan}: not a plan that tidy-plan wrote ({error})") from error
    if plan.everything:
        raise ConfigError(
            f"{args.plan}: this plan checks every file; run the whole-codebase pass (run-clang-tidy) instead"
        )
    settings = load_conventions(root).ci.tidy
    build_dir = Path(args.build_dir)
    build_dir = build_dir if build_dir.is_absolute() else root / build_dir
    result = ci_tidy.run_plan(plan, root, build_dir, args.clang_tidy, args.jobs or os.cpu_count() or 1, runner)
    sources = [unit.source for unit in plan.units]
    for item in result.findings:
        print(f"{item.path}:{item.line}:{item.column}: {item.level}: {item.message} [{item.check}]")
    for failure in result.failures:
        print(f"clang-tidy failed: {failure}")
    if args.annotate:
        for command in ci_tidy.to_annotations(result.findings):
            print(command)
    if args.sarif:
        version = ci_tidy.tool_version(args.clang_tidy, root, runner)
        Path(args.sarif).write_text(json.dumps(ci_tidy.to_sarif(result.findings, version), indent=1), encoding="utf-8")
    summary = ci_tidy.summary_lines(result.findings, sources, settings.findings_listed, plan.notes)
    summary += [f"- clang-tidy failed: {failure}" for failure in result.failures]
    print(summary[0])
    if args.summary:
        with Path(args.summary).open("a", encoding="utf-8") as handle:
            handle.write("\n".join(summary) + "\n")
    return 0 if result.ok else 1


def _report_failure(args: argparse.Namespace, run: Runner, git: ci_failure.GitRunner | None) -> int:
    """`ci report-failure`: open or update the issue of a failed run on the default branch."""
    root = find_repo_root(Path.cwd())
    conventions = load_conventions(root)
    outcome = ci_failure.report_failure(args.run, conventions, run, git or ci_failure.run_git(root), root, args.apply)
    branch = conventions.repo.default_branch
    match outcome.kind:
        case "created":
            print(f"ci report-failure: created issue #{outcome.number} for run {args.run} on {branch}")
        case "commented":
            print(f"ci report-failure: commented on issue #{outcome.number}")
        case "would-create" | "would-comment":
            print(f"ci report-failure: would {outcome.kind.removeprefix('would-')} (no --apply):")
            print(outcome.text)
        case _:
            print(f"ci report-failure: {outcome.kind}: {outcome.detail}")
    return 0


def run_ci(
    args: argparse.Namespace,
    run: Runner = run_gh,
    tidy_runner: ci_tidy.Runner = ci_tidy.run_tool,
    git_runner: ci_failure.GitRunner | None = None,
) -> int:
    """Run a `ci` command; 2 (with the reason on stderr) when GitHub or the conventions cannot be read.

    `run` is the `gh` runner of the commands that talk to GitHub, `tidy_runner` the program runner of `tidy-run` and
    `git_runner` the `git` runner of `report-failure` (default: `git` in this checkout).
    """
    try:
        if args.command == "dependencies":
            return _dependencies(args, run)
        if args.command == "tidy-plan":
            return _tidy_plan(args)
        if args.command == "tidy-run":
            return _tidy_run(args, tidy_runner)
        if args.command == "report-failure":
            return _report_failure(args, run, git_runner)
        return _rerun_dependencies(args, run)
    except OSError as error:
        print(f"coney-tools: {error}", file=sys.stderr)
        return 2
