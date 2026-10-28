# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools task review`: the review stage of a task (docs/guides/review/task.md).

A review has three steps, and the reviewer takes all of them:

1. **Prepare** (`task review T`): the tool part. Everything a program can check is checked first, so that the reviewer
   spends its time on judgement: the stamp log is intact, every changed `@orig` function has a current `compared`
   stamp, `orig check-copy` is clean, every added test has a passing `fails-first` stamp, `docs check` passes on the
   changed pages, the description has a title and a summary and its pictures exist. Any problem is listed (all of
   them) and no reviewer is started. Otherwise a **packet** is written to `<scratch>/<task>/review/PACKET.json`: the
   tip, the base, the hash of the change, which checklists apply, the `@orig` functions with their body hashes, the
   research pages with their addresses and the description's hashes.
2. **Judge**: the reviewer reads the packet, applies the checklists (docs/guides/review/) and writes `REVIEW.json`
   (`review_verdict`) and `REVIEW.md` beside the packet. It copies the packet's hashes into the verdict.
3. **Record** (`task review T --record`): the packet is built again from the repository as it is now, the verdict must
   answer it (`review_verdict.problems`: nothing stale, every checklist, function, page and picture covered, no
   contradiction), the caller must be a reviewer who did not work on the task and, for reverse-engineering review, runs
   on Opus; then one `reviewed` stamp is appended: `pass` or `fail`, bound to the change (`diff_sha256`), the pairs and
   the description's hashes. A verdict with a problem writes no stamp.

`task review T --run` does all three headlessly (`claude -p --agent reviewer`), for a person; a coordinator starts the
reviewer as a subagent instead, which runs steps 1 and 3 itself, so the stamp's `who` is the reviewer's own hook
identity. `--text` reviews the description alone, for an edited paragraph or picture, while a full pass still holds.

Everything the command touches outside git is injected (`ReviewEnv`), so the tests use a real temporary repository and
fakes for the agent, the diff hash and the page checks.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from collections.abc import Callable, Mapping
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

from coney_tools import (
    docs_check,
    docs_cli,
    fails_first,
    identity,
    orig_copy,
    orig_pairs,
    orig_review,
    pr_diff,
    pr_review,
    pr_text_file,
    review_verdict,
    stamps,
    unit,
)
from coney_tools.config import ConfigError, find_repo_root
from coney_tools.conventions import REVIEW_KINDS, Conventions, load_conventions
from coney_tools.repo_checks import check_docs, check_title

#: The agent definition handed to `claude --agents` for a run, written in the review's folder and removed after.
AGENTS_FILE = ".reviewer-agents.json"
#: How long a headless review may take before it is stopped.
DEFAULT_TIMEOUT_MINUTES = 60
#: Runs a command in a folder and returns its exit code (the real one starts `claude`).
Launcher = Callable[[list[str], Path], int]
#: The hash of the change a branch makes (base, tip): stamps are bound to it, not to a commit, so a clean rebase keeps
#: them (docs/guides/stamps.md).
ChangeHash = Callable[[Path, str, str], str]
#: Finds problems in a checkout against a base: `docs check` on the changed pages, `orig check-copy` on the added lines.
Checker = Callable[[Path, str], list[str]]

_ADDRESS = re.compile(r"0x[0-9a-fA-F]{8}")
_TEST_SUFFIXES = (".cpp", ".h")


@dataclass
class ReviewEnv:
    """Everything a `task review` command needs from outside git and the stamp log."""

    #: The checkout the tool runs from (its `.claude/agents` holds the reviewer).
    root: Path
    #: The unit's worktree: the branch under review.
    worktree: Path
    #: The task's scratch folder.
    unit: unit.Unit
    conventions: Conventions
    #: What the branch is compared with (`origin/main`).
    base_ref: str
    #: The engineer's description file.
    text_file: Path
    change_hash: ChangeHash
    docs_problems: Checker
    copy_problems: Checker
    #: Who runs the command, as the hooks vouch for it.
    identity: identity.Identity
    #: The `who` of the stamp this command writes.
    who: Mapping[str, Any]
    clock: Callable[[], datetime] = lambda: datetime.now(UTC)

    @property
    def folder(self) -> Path:
        """The review's folder: the packet, the verdict, its report and the recorded copies."""
        return self.unit.dir / self.conventions.tasks.review.folder


# --- git -------------------------------------------------------------------------------------------------------------


def _git(worktree: Path, *args: str) -> str:
    """Run git in `worktree`; its output, or a ConfigError with git's message."""
    done = subprocess.run(
        ["git", "-C", str(worktree), *args], capture_output=True, text=True, encoding="utf-8", check=False
    )
    if done.returncode != 0:
        raise ConfigError(f"git {' '.join(args)}: {done.stderr.strip() or 'failed'}")
    return done.stdout


def _show(worktree: Path, ref: str, path: str) -> str | None:
    """The text of `path` at `ref`, or None when it does not exist there."""
    done = subprocess.run(
        ["git", "-C", str(worktree), "show", f"{ref}:{path}"],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    return done.stdout if done.returncode == 0 else None


# --- the default checks ----------------------------------------------------------------------------------------------


def default_docs_problems(worktree: Path, base: str) -> list[str]:
    """`docs check` on the pages changed since the branch left `base`: a line per error (not per warning)."""
    result = docs_check.check_changed(worktree, base, docs_check.context_for(worktree))
    lines: list[str] = []
    for page in result.failed:
        lines += [line for line in docs_cli.page_lines(result, page) if ": warning: " not in line]
    return lines


def default_copy_problems(worktree: Path, base: str) -> list[str]:
    """`orig check-copy` on the lines the branch added."""
    return [str(finding) for finding in orig_copy.check(worktree, base)]


def default_change_hash(worktree: Path, base: str, tip: str) -> str:
    """The hash of the change from `base` to `tip` (`pr_diff`, docs/guides/stamps.md#change)."""
    return pr_diff.change_sha256(worktree, base, tip)


# --- the packet ------------------------------------------------------------------------------------------------------


def _kinds(files: list[str], functions: list[dict[str, Any]], env: ReviewEnv, mode: str) -> list[str]:
    """Which checklists the change needs, in the order the reviewer applies them (docs/guides/review/index.md)."""
    if mode == "text":
        return ["text"]
    rules = env.conventions.tasks.review
    research = tuple(rules.research_paths)
    suffixes = tuple(rules.docs_suffixes)
    needed = {"text", "commits"}
    for name in files:
        if name.startswith(research):
            needed.add("research")
        elif name.endswith(suffixes):
            needed.add("docs")
        else:
            needed.add("code")
    if functions:
        needed.add("orig")
    return [kind for kind in REVIEW_KINDS if kind in needed]


def _pages(files: list[str], env: ReviewEnv, tip: str) -> list[dict[str, Any]]:
    """The research pages the change touches and the addresses each one writes."""
    rules = env.conventions.tasks.review
    pages: list[dict[str, Any]] = []
    for name in files:
        if not rules.is_research_page(name):
            continue
        text = _show(env.worktree, tip, name)
        if text is not None:
            pages.append({"path": name, "addresses": sorted({a.lower() for a in _ADDRESS.findall(text)})})
    return pages


def _added_tests(env: ReviewEnv, base: str, tip: str) -> list[str]:
    """The names of the C++ `TEST_CASE`s the branch adds."""
    names: set[str] = set()
    listing = _git(env.worktree, "diff", "--name-only", "-z", "--diff-filter=AM", base, tip, "--", "tests")
    for path in (item for item in listing.split("\0") if item.endswith(_TEST_SUFFIXES)):
        after = fails_first.declared_names(_show(env.worktree, tip, path) or "")
        before = fails_first.declared_names(_show(env.worktree, base, path) or "")
        names |= after - before
    return sorted(names)


def _function_problems(
    env: ReviewEnv, functions: list[dict[str, Any]], report: stamps.Report, tip: str
) -> tuple[list[str], str]:
    """The `compared` problems of the changed `@orig` functions, and the hash of all their pairs files."""
    problems: list[str] = []
    hashes: dict[str, str] = {}
    # Every Coney function at the tip that carries an address: a function that shares one has a stamp of its own.
    citing = orig_review.citing(orig_review.units_at(env.worktree, tip)) if functions else {}
    for function in functions:
        for address in function["addresses"]:
            name = f"{function['coney']} ({address})"
            redo = f"orig compare {address}"
            if len(citing.get(address, ())) > 1:  # then `compare` must be told which function
                redo += f" --coney-function '{function['key']}'"
            pairs = env.unit.mapping(address)
            try:
                pairs_hash = orig_pairs.file_sha256(pairs)
            except orig_pairs.PairsError:
                problems.append(f"compared: {name}: no pairs file (run `{redo}`)")
                continue
            hashes[address] = pairs_hash
            covers = {orig_review.BODY_COVER: function["body_sha256"]}
            stamp = orig_review.current_compared(
                env.worktree, report, address, covers, function["key"], citing.get(address, ())
            )
            if stamp is None:
                problems.append(f"compared: {name}: no current `compared` stamp (run `{redo}`)")
            elif stamp.covers.get("pairs_sha256") != pairs_hash:
                problems.append(f"compared: {name}: the pairs file changed after it was compared (run `{redo}`)")
    return problems, orig_pairs.set_sha256(hashes)


def _text_problems(env: ReviewEnv, files: list[str], text: pr_text_file.TextFile) -> list[str]:
    """What the description file gets wrong that a program can see: the title, the pictures, the docs rule."""
    conventions = env.conventions
    problems = [f"title: {p}" for p in check_title(text.title, conventions.commits.title)]
    problems += [
        f"{path}: the picture is missing (paths are relative to {env.text_file.name})" for path in text.missing
    ]
    body = env.text_file.read_text(encoding="utf-8", errors="replace").partition("\n")[2]
    problems += [f"docs: {p}" for p in check_docs(files, body, conventions.paths, conventions.pull_requests.docs_none)]
    return problems


def _full_pass_problem(report: stamps.Report, diff: str, pairs_set: str) -> list[str]:
    """For a text review: the newest full review must be a pass that still covers the code and the pairs."""
    for stamp in reversed(report.stamps):
        if stamp.step == "reviewed" and stamp.detail.get("scope") == "full":
            if (
                stamp.result == "pass"
                and stamp.covers.get(stamps.CHANGE) == diff
                and stamp.covers.get("pairs_set_sha256") == pairs_set
            ):
                return []
            break
    return ["text: no passing full review covers the code as it is now (run `task review`, without --text)"]


def build_packet(env: ReviewEnv, mode: str) -> tuple[dict[str, Any], list[str]]:
    """The packet for the branch as it is now, and the problems the tool part found (empty when it may be reviewed).

    `mode` is `full` or `text` (the description alone). The packet is complete even when there are problems, so the
    caller can show what it saw; it is written only when there are none.
    """
    problems: list[str] = []
    worktree = env.worktree
    tip = _git(worktree, "rev-parse", "--verify", "HEAD").strip()
    base = _git(worktree, "merge-base", env.base_ref, tip).strip()
    if tip == base:
        problems.append(f"nothing to review: the branch has no commit beyond {env.base_ref}")
    dirty = [line[3:] for line in _git(worktree, "status", "--porcelain").splitlines() if line.strip()]
    if dirty:
        shown = ", ".join(dirty[:5]) + (" ..." if len(dirty) > 5 else "")
        problems.append(f"the worktree has uncommitted changes ({shown}): the review is of what is committed")
    files = sorted(item for item in _git(worktree, "diff", "--name-only", "-z", base, tip).split("\0") if item)
    report = stamps.verify(env.unit.stamps, env.conventions.stamps)
    problems += [f"stamp log: {p}" for p in report.problems if p.severity == "error"]
    functions = orig_review.pr_stamp(worktree, tip, base)["functions"] if tip != base else []
    function_problems, pairs_set = _function_problems(env, functions, report, tip)
    diff = env.change_hash(worktree, base, tip)
    try:
        text = pr_text_file.read(env.text_file)
    except ConfigError as error:
        problems.append(str(error))
        text = pr_text_file.TextFile(env.text_file, "", "", (), ())
    else:
        problems += _text_problems(env, files, text)
    tests = []
    if mode == "full":
        problems += function_problems
        problems += [f"copy-check: {p}" for p in env.copy_problems(worktree, env.base_ref)]
        problems += [f"docs check: {p}" for p in env.docs_problems(worktree, env.base_ref)]
        for name in _added_tests(env, base, tip) if tip != base else []:
            latest = report.latest("fails-first", name)
            tests.append({"name": name, "stamp": latest.result if latest else "missing"})
            if latest is None or latest.result != "pass":
                problems.append(
                    f'fails-first: "{name}" was added and has no passing fails-first stamp (run `orig fails-first`)'
                )
    else:
        problems += _full_pass_problem(report, diff, pairs_set)
    packet: dict[str, Any] = {
        "v": review_verdict.VERSION,
        "task": env.unit.task,
        "mode": mode,
        "tip": tip,
        "base": base,
        "diff_sha256": diff,
        "kinds": _kinds(files, functions, env, mode),
        "files": files,
        "functions": functions if mode == "full" else [],
        "pairs_set_sha256": pairs_set,
        "pages": _pages(files, env, tip) if mode == "full" else [],
        "tests": tests,
        "text": {
            "file": env.text_file.name,
            "title": text.title,
            "title_sha256": text.title_sha256 if text.title else "",
            "paragraph": text.paragraph,
            "paragraph_sha256": text.paragraph_sha256 if text.paragraph else "",
            "images": [{"path": image.path, "sha256": image.sha256} for image in text.images],
            "images_sha256": text.images_sha256,
        },
        "rubric_version": env.conventions.pull_requests.orig_review.rubric_version,
    }
    return packet, problems


# --- prepare ---------------------------------------------------------------------------------------------------------


def _refuse(task: str, what: str, problems: list[str]) -> int:
    """Print every problem and return 1."""
    print(f"task review {task}: {what}, {len(problems)} problem(s):")
    for problem in problems:
        print(f"  {problem}")
    return 1


def prepare(env: ReviewEnv, mode: str) -> int:
    """Run the tool part and write the packet; 0 when the reviewer may start, 1 with every problem listed."""
    packet, problems = build_packet(env, mode)
    task = env.unit.task
    if problems:
        return _refuse(task, "refused (no reviewer was started)", problems)
    rules = env.conventions.tasks.review
    env.folder.mkdir(parents=True, exist_ok=True)
    # A verdict written for an earlier packet must not be taken for this one's.
    for name in (rules.verdict_file, rules.report_file):
        (env.folder / name).unlink(missing_ok=True)
    packet_path = env.folder / rules.packet_file
    packet_path.write_text(json.dumps(packet, indent=2) + "\n", encoding="utf-8")
    functions = (
        ", ".join(f"{f['coney']} {' '.join(review_verdict.review_addresses(f))}" for f in packet["functions"]) or "none"
    )
    tip, base, diff = packet["tip"][:12], packet["base"][:12], packet["diff_sha256"][:12]
    print(f"task review {task}: {mode} review of {tip} (base {base}, change {diff})")
    print(f"  checklists: {', '.join(packet['kinds'])}")
    print(f"  @orig functions: {functions}")
    print(f"  research pages: {', '.join(p['path'] for p in packet['pages']) or 'none'}")
    print("  the tool part passed")
    print(f"  packet: {packet_path}")
    print(
        f"  write {rules.verdict_file} and {rules.report_file} in {env.folder}, then run `task review {task} --record`"
    )
    return 0


# --- record ----------------------------------------------------------------------------------------------------------


def _reviewer_problems(env: ReviewEnv, packet: Mapping[str, Any], report: stamps.Report) -> list[str]:
    """Why the caller may not record a review: it must be a reviewer who did not work on the task, on a model that the
    kind of review allows. A caller the hooks do not vouch for is let through and recorded as unverified."""
    who = env.identity
    if not who.verified:
        return []
    rules = env.conventions.tasks.review
    problems: list[str] = []
    if who.type != "reviewer":
        problems.append(
            f"only a reviewer records a review (this caller is {who.type}; the coordinator starts a reviewer)"
        )
    # Everyone who ran any other step of the task, a reviewer's own `compared` or `fails-first` included; the caller's
    # earlier `reviewed` stamps are its own reviews, not work on the task.
    caller = who.agent()
    if any(
        stamp.step != "reviewed"
        and isinstance(stamp.who.get("agent"), dict)
        and identity.same_agent(caller, stamp.who["agent"])
        for stamp in report.stamps
    ):
        problems.append(f"reviewer {who.id} also worked on this task: a review is by someone who did not do the work")
    kinds = sorted(set(packet["kinds"]) & set(rules.reverse_engineering_kinds))
    if kinds and not any(word.lower() in who.model.lower() for word in rules.reverse_engineering_models):
        problems.append(
            f"a reverse-engineering review ({', '.join(kinds)}) runs on "
            f"{' or '.join(rules.reverse_engineering_models)}; "
            f"this reviewer's model is {who.model}"
        )
    return problems


def _same(stamp: stamps.Stamp, result: str, commit: str, covers: Mapping[str, str]) -> bool:
    """Whether `stamp` already says what a new `reviewed` stamp would."""
    return (
        stamp.step == "reviewed"
        and stamp.result == result
        and stamp.commit == commit
        and dict(stamp.covers) == dict(covers)
    )


def record(env: ReviewEnv, mode: str) -> int:
    """Bind the reviewer's verdict to the change as it is now and append the `reviewed` stamp.

    0 for a recorded pass; 1 for a recorded fail, or when nothing was recorded (a problem is printed); the stamp is
    written only for a verdict that answers the packet and a caller who may record it.
    """
    task = env.unit.task
    rules = env.conventions.tasks.review
    packet, problems = build_packet(env, mode)
    if problems:
        return _refuse(task, "cannot record (the tool part fails again)", problems)
    verdict_path = env.folder / rules.verdict_file
    try:
        raw = verdict_path.read_bytes().replace(b"\r\n", b"\n")
    except OSError:
        print(
            f"task review {task}: no {rules.verdict_file} in {env.folder}: "
            "the reviewer writes it after reading the packet"
        )
        return 1
    try:
        verdict = review_verdict.parse(raw.decode("utf-8", errors="replace"))
    except ConfigError as error:
        print(f"task review {task}: {rules.verdict_file}: {error}")
        return 1
    report = stamps.verify(env.unit.stamps, env.conventions.stamps)
    found = _reviewer_problems(env, packet, report)
    found += review_verdict.problems(verdict, packet, rules.pages_min_addresses)
    if found:
        return _refuse(task, "the verdict was not recorded", found)
    covers = {
        stamps.CHANGE: packet["diff_sha256"],
        "pairs_set_sha256": packet["pairs_set_sha256"],
        "title_sha256": packet["text"]["title_sha256"],
        "paragraph_sha256": packet["text"]["paragraph_sha256"],
        "images_sha256": packet["text"]["images_sha256"],
        "review_sha256": hashlib.sha256(raw).hexdigest(),
    }
    detail = review_verdict.stamp_detail(verdict)
    detail["reviewer"] = dict(env.who.get("agent", identity.Identity().agent()))
    if mode == "text":
        _carry_full_review(report, detail)
    result = str(verdict["verdict"])
    newest = report.latest("reviewed")
    if newest is not None and _same(newest, result, packet["tip"], covers):
        print(f"task review {task}: reviewed: {result} (unchanged: stamp #{newest.seq} already says so)")
        return 0 if result == "pass" else 1
    stamp = stamps.append(
        env.unit.stamps,
        rules=env.conventions.stamps,
        task=task,
        step="reviewed",
        subject="",
        result=result,
        commit=packet["tip"],
        covers=covers,
        detail=detail,
        who=dict(env.who),
        clock=env.clock,
    )
    # The verdict is kept twice: by stamp, so a text review can point at the full one it rests on, and as the task's
    # `reviewed.json`, the newest, which the stamp's `review_sha256` covers (docs/guides/stamps.md#api).
    (env.folder / f"recorded-{stamp.seq}.json").write_bytes(raw)
    (env.unit.dir / review_verdict.RECORDED).write_bytes(raw)
    blockers = review_verdict.blocking(verdict)
    print(
        f"task review {task}: reviewed: {result} (stamp #{stamp.seq}, change {packet['diff_sha256'][:12]}, "
        f"{len(blockers)} blocking finding(s))"
    )
    for finding in blockers:
        print(f"  {finding['severity']}: {finding['where']}: {finding['text']}")
    return 0 if result == "pass" else 1


def _carry_full_review(report: stamps.Report, detail: dict[str, Any]) -> None:
    """A text review keeps what the full review it rests on said about the functions and the pages."""
    for stamp in reversed(report.stamps):
        if stamp.step == "reviewed" and stamp.detail.get("scope") == "full":
            for key in ("functions", "function_count", "pages"):
                if key in stamp.detail:
                    detail[key] = stamp.detail[key]
                else:
                    detail.pop(key, None)
            detail["full_seq"] = stamp.seq
            return


# --- run -------------------------------------------------------------------------------------------------------------


def review_prompt(env: ReviewEnv, mode: str) -> str:
    """The task the headless reviewer is given: where everything is and how to finish."""
    rules = env.conventions.tasks.review
    task = env.unit.task
    flag = " --text" if mode == "text" else ""
    return (
        f"Review task {task} ({mode} review). The branch is checked out in {env.worktree}.\n"
        f"- The tool part passed. The packet is {env.folder / rules.packet_file}: read it, then the checklists for its "
        "`kinds`, and judge the change as your instructions say.\n"
        f"- Write {rules.verdict_file} and {rules.report_file} in {env.folder} and nothing else.\n"
        f"- Then run `uv run --project python coney-tools task review {task}{flag} --record` in {env.worktree} and "
        "report its result.\n"
        "Follow your instructions to the letter."
    )


def run(
    env: ReviewEnv,
    mode: str,
    *,
    launch: Launcher | None = None,
    claude: str = "claude",
    model: str | None = None,
    effort: str | None = None,
    timeout_minutes: float = DEFAULT_TIMEOUT_MINUTES,
) -> int:
    """Prepare, run the reviewer headlessly (`claude -p`, in the worktree, so its hooks give it its own identity) and
    report the stamp it recorded; 0 for a recorded pass."""
    code = prepare(env, mode)
    if code != 0:
        return code
    rules = env.conventions.tasks.review
    task = env.unit.task
    before = len(stamps.verify(env.unit.stamps, env.conventions.stamps).stamps)
    definition = pr_review.agent_definition(env.root / ".claude" / "agents" / f"{rules.agent}.md")
    agents_file = env.folder / AGENTS_FILE
    agents_file.write_text(json.dumps({rules.agent: definition}), encoding="utf-8")
    command = pr_review.review_command(
        claude,
        agents_file,
        rules.agent,
        model or str(definition.get("model", "opus")),
        review_prompt(env, mode),
        effort,
    )
    try:
        status = (launch or (lambda c, cwd: pr_review.launch_claude(c, cwd, timeout_minutes)))(command, env.worktree)
    finally:
        agents_file.unlink(missing_ok=True)
    if status != 0:
        print(f"task review {task}: {claude} exited with {status}")
        return 1
    report = stamps.verify(env.unit.stamps, env.conventions.stamps)
    recorded = [s for s in report.stamps[before:] if s.step == "reviewed"]
    if not recorded:
        print(f"task review {task}: the reviewer did not record a review (see {env.folder / rules.report_file})")
        return 1
    stamp = recorded[-1]
    print(
        f"task review {task}: reviewed: {stamp.result} (stamp #{stamp.seq}, {stamp.detail.get('blockers', 0)} blocking)"
    )
    return 0 if stamp.result == "pass" else 1


# --- the command line ------------------------------------------------------------------------------------------------


def add_review_parser(commands: Any) -> None:
    """Add `task review` to the `task` group's commands."""
    review = commands.add_parser(
        "review",
        help="review a task: prepare what to judge, record the reviewer's verdict as a `reviewed` stamp, or run both",
        description=(
            "Without options: run the tool part (stamp log, compared stamps, copy-check, fails-first, docs check, the "
            "description) and write the packet the reviewer judges. --record: bind the reviewer's REVIEW.json to the "
            "change and append the `reviewed` stamp. --run: do both with a headless reviewer. Exit 0 when ready or "
            "recorded as a pass, 1 when refused or recorded as a fail, 2 when it cannot run."
        ),
    )
    review.add_argument("task", nargs="?", help="the task (default: CONEY_TASK, else the branch)")
    review.add_argument("--worktree", type=Path, help="the unit's worktree (default: the current checkout)")
    review.add_argument("--base", help="what the branch is compared with (default: origin/<default branch>)")
    review.add_argument("--pr-file", type=Path, help="the description file (default: <scratch>/<task>/PR.md)")
    review.add_argument(
        "--text", action="store_true", help="review the description alone (a full pass must still hold)"
    )
    which = review.add_mutually_exclusive_group()
    which.add_argument("--record", action="store_true", help="record the reviewer's verdict as a `reviewed` stamp")
    which.add_argument(
        "--run", action="store_true", help="prepare, run a headless reviewer and report what it recorded"
    )
    review.add_argument(
        "--claude", default="claude", help="the Claude Code executable for --run (default: %(default)s)"
    )
    review.add_argument("--model", help="the model for --run (default: the agent file's)")
    review.add_argument(
        "--effort", choices=("low", "medium", "high", "xhigh", "max"), help="the effort level for --run"
    )
    review.add_argument(
        "--timeout-minutes", type=float, default=DEFAULT_TIMEOUT_MINUTES, help="stop a --run review after this long"
    )


def environment(args: argparse.Namespace) -> ReviewEnv:
    """The environment of the real world for the parsed arguments."""
    worktree = (args.worktree or find_repo_root(Path.cwd())).resolve()
    conventions = load_conventions(worktree)
    task = unit.task_name(worktree, args.task)
    folder = unit.Unit.of(worktree, task)
    text_file = args.pr_file or folder.dir / "PR.md"
    base = args.base or f"origin/{conventions.repo.default_branch}"
    return ReviewEnv(
        root=worktree,
        worktree=worktree,
        unit=folder,
        conventions=conventions,
        base_ref=base,
        text_file=text_file,
        change_hash=default_change_hash,
        docs_problems=default_docs_problems,
        copy_problems=default_copy_problems,
        identity=identity.current(),
        who=stamps.who_from_env("coney-tools task review"),
    )


def run_review_command(args: argparse.Namespace) -> int:
    """Run `task review` for parsed arguments."""
    try:
        env = environment(args)
        mode = "text" if args.text else "full"
        if args.record:
            return record(env, mode)
        if args.run:
            return run(
                env,
                mode,
                claude=args.claude,
                model=args.model,
                effort=args.effort,
                timeout_minutes=args.timeout_minutes,
            )
        return prepare(env, mode)
    except ConfigError as error:
        print(f"task review: {error}", file=sys.stderr)
        return 2
