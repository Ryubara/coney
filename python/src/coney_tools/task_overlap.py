# SPDX-License-Identifier: GPL-3.0-or-later
"""Pending work that overlaps a unit: the open pull requests and the other units in progress that touch the same
files or `@orig` addresses (docs/guides/pending-work.md).

A unit starts from `main`, so work that is finished but not merged is invisible to it: it may repeat research another
unit did, contradict a research page that is about to change or write code that conflicts with what lands next. This
module reads that work and says where it meets the unit. It never makes `main` less the truth: the list is for the
engineer to read, to note in STATUS.md and to tell the coordinator, who orders the two (docs/guides/how-we-work.md).

* **What the unit names or changes** (`Wanted`): the paths and addresses of the task file (`scope`, and what the issue
  writes in the sections Issues, Goal, Done and Scope, read with the leads search's `identifiers_in`), plus, once the
  unit has a branch, the paths and the addresses of the changed lines of its diff (`issue_impact.parse_diff` and
  `diff_addresses`) and the files git shows as changed but not committed.
* **What is pending** (`Pending`): each open pull request (paths from `gh pr list`, `@orig` addresses from the diff of
  at most `tasks.overlap.diff_prs` of them, read with `issue_impact.diff_addresses`) and each other unit that has a
  worktree, is not a fix task, is not finished and has not opened its pull request (one that has is listed by its pull
  request, and then only while that is open, so a merged unit never appears).
* **The match** (`overlaps`): a pending path overlaps when it is a path the unit wants, lies inside a folder it wants or
  fits a glob in its scope; an address overlaps when it is equal. Items with the most shared addresses and paths come
  first; `tasks.overlap` limits how many are listed.

`task start` writes the list into the task file's "Pending work that overlaps" section, `task check` and `task review`
print it again when it differs from what the section holds, and `task overlap` shows it. A read that fails (GitHub down,
git failing in another worktree) is a note and never a refusal. `gh`, the clock and the git reads are injected, so the
tests touch neither the network nor other worktrees.
"""

from __future__ import annotations

import fnmatch
import json
import re
import subprocess
from collections.abc import Callable, Iterable
from dataclasses import dataclass, field
from datetime import UTC, datetime
from pathlib import Path

from coney_tools import issue_impact, stamps, unit
from coney_tools.audit_github import Runner
from coney_tools.config import ConfigError
from coney_tools.conventions import Conventions, OverlapRules
from coney_tools.slot_fill_github import bounded_runner
from coney_tools.task_file import TaskFile, parse_task_file
from coney_tools.task_leads import identifiers_in
from coney_tools.unit_record import UnitRecord, UnitRecordError, read_record

#: The task file section the list is kept in.
SECTION = "Pending work that overlaps"
#: What `gh pr list` is asked for.
PR_FIELDS = "number,title,headRefName,isDraft,files"
#: The sections of the task file that say what the unit is about: the issue's own words and the scope.
_ABOUT = ("Issues", "Goal", "Done", "Scope")
_ADDRESS = re.compile(r"^0x[0-9a-fA-F]{8}$")
_ANCHOR = re.compile(r"#[\w{}-]*$")
_HEADING = re.compile(r"^## (?P<name>.+?)\s*$", re.MULTILINE)
#: The most open pull requests whose diffs one run reads is `tasks.overlap.diff_prs`; this many characters of a
#: failure's message are shown.
_WHY = 120


@dataclass(frozen=True)
class Wanted:
    """What a unit names or changes: repository paths (a folder ends with `/`, a glob has `*`) and addresses."""

    paths: tuple[str, ...] = ()
    addresses: tuple[str, ...] = ()

    def plus(self, change: Change) -> Wanted:
        """These and what a branch changes, each once."""
        return Wanted(
            tuple(dict.fromkeys([*self.paths, *sorted(change.paths)])),
            tuple(dict.fromkeys([*self.addresses, *sorted(change.addresses)])),
        )

    def covers(self, path: str) -> bool:
        """Whether a path is one the unit wants: the same file, a file in a wanted folder or one a glob fits."""
        for wanted in self.paths:
            if "*" in wanted:
                if fnmatch.fnmatchcase(path, wanted):
                    return True
            elif path == wanted or path.startswith(wanted.rstrip("/") + "/"):
                return True
        return False


@dataclass(frozen=True)
class Change:
    """What a pending change touches: its changed paths and the `@orig` addresses of its changed lines."""

    paths: frozenset[str] = frozenset()
    addresses: frozenset[str] = frozenset()


@dataclass(frozen=True)
class Pending:
    """One piece of pending work: an open pull request (`kind` `pr`, key `#N`) or a unit in progress (`unit`, key its
    task id)."""

    kind: str
    key: str
    title: str
    state: str
    branch: str
    change: Change


@dataclass(frozen=True)
class Overlap:
    """Where one piece of pending work meets the unit: the paths and addresses they share."""

    pending: Pending
    paths: tuple[str, ...]
    addresses: tuple[str, ...]

    def line(self, max_paths: int) -> str:
        """The list entry: what it is, its state, and the shared paths (a few named, the rest counted) and addresses."""
        what = f"PR {self.pending.key}" if self.pending.kind == "pr" else f"unit {self.pending.key}"
        shown = ", ".join(self.paths[:max_paths])
        more = len(self.paths) - max_paths
        shared = []
        if self.paths:
            shared.append(shown + (f" and {more} more path(s)" if more > 0 else ""))
        if self.addresses:
            shared.append(
                "@orig "
                + ", ".join(self.addresses[:max_paths])
                + (f" and {len(self.addresses) - max_paths} more" if len(self.addresses) > max_paths else "")
            )
        return f"- {what} ({self.pending.state}) {self.pending.title}: shares {'; '.join(shared)}"


@dataclass
class Result:
    """The pending work that overlaps, and what could not be read."""

    overlaps: list[Overlap] = field(default_factory=list)
    #: The pull requests and units that were read; the list is only as complete as these.
    read_prs: int = 0
    read_units: int = 0
    #: Reads that failed; with `prs_failed` the list is known to be incomplete.
    notices: list[str] = field(default_factory=list)
    prs_failed: bool = False


@dataclass
class OverlapEnv:
    """The world the reads work in; every part can be replaced in a test."""

    scratch: Path
    conventions: Conventions
    #: `gh` (read only); None reads no pull requests.
    gh: Runner | None
    now: Callable[[], datetime]
    #: What a unit's branch changes (`unit_change`).
    change_of: Callable[[UnitRecord], Change] = lambda record: unit_change(record)


# --- what the unit wants ----------------------------------------------------------------------------------------


def wanted_of_task(task: TaskFile) -> Wanted:
    """The paths and addresses a task file names: its `scope`, and what the sections about the issue write."""
    paths: list[str] = []
    addresses: list[str] = []
    for token in task.scope:
        if _ADDRESS.match(token):
            addresses.append(token.lower())
        elif token.strip("./"):
            paths.append(token)
    text = "\n".join(task.sections.get(name, "") for name in _ABOUT)
    found_addresses, _, found_paths = identifiers_in(text)
    addresses.extend(found_addresses)
    paths.extend(_ANCHOR.sub("", path) for path in found_paths)
    return Wanted(tuple(dict.fromkeys(paths)), tuple(dict.fromkeys(addresses)))


def _status_paths(lines: Iterable[str]) -> set[str]:
    """The paths of `git status --porcelain` lines (a rename names both)."""
    found: set[str] = set()
    for line in lines:
        for part in line[3:].split(" -> "):
            found.add(part.strip().strip('"'))
    return found


def unit_change(record: UnitRecord) -> Change:
    """What a unit's worktree changes that `main` does not have: the files of the branch's diff since it left its base
    ref (three dots, so what the branch merged from `main` is not its own) whose content still differs from the base ref
    now (a branch already squash-merged changes nothing), their paths and the addresses of their changed lines
    (`issue_impact.parse_diff` and `diff_addresses`), and the files git shows as changed but not committed. Not
    `issue_impact.gather`: it also reads the `@orig` functions whose body changed, which takes 10 to 20 seconds a unit,
    and a start, a check and a review read every unit in progress."""
    tree = record.worktree
    ref = record.base_ref
    try:
        issue_impact.git_output(tree, "rev-parse", "--verify", "--quiet", f"{ref}^{{commit}}")
        span = f"{ref}...HEAD"
    except ConfigError:
        ref, span = record.base_commit, f"{record.base_commit}..HEAD"
    patch = issue_impact.git_output(tree, "diff", "-M", "-U0", "--no-color", "--no-ext-diff", span)
    differing = set(issue_impact.git_output(tree, "diff", "--name-only", ref, "HEAD").splitlines())
    paths = _status_paths(unit.status_lines(tree))
    kept: set[str] = set()
    for diff in issue_impact.parse_diff(patch):
        if diff.new in differing or diff.old in differing:
            paths.update((diff.old, diff.new))
            kept.add(diff.new)
    return Change(frozenset(paths), frozenset(issue_impact.diff_addresses(patch, kept)))


# --- what is pending --------------------------------------------------------------------------------------------


def read_open_prs(gh: Runner, rules: OverlapRules) -> list[tuple[Pending, bool]]:
    """The open pull requests: each with its changed paths, and whether it is a draft; ConfigError when the answer is
    not a list of pull requests, OSError when `gh` fails."""
    out = gh(["pr", "list", "--state", "open", "--limit", str(rules.open_prs), "--json", PR_FIELDS])
    try:
        data = json.loads(out)
        if not isinstance(data, list):
            raise ConfigError("the pull request list is not a JSON list")
        found = []
        for item in data:
            number = int(item["number"])
            draft = bool(item.get("isDraft"))
            paths = frozenset(str(f["path"]) for f in item.get("files") or [])
            found.append(
                (
                    Pending(
                        "pr",
                        f"#{number}",
                        f"`{item.get('title', '')}`",
                        "draft pull request" if draft else "open pull request",
                        str(item.get("headRefName", "")),
                        Change(paths),
                    ),
                    draft,
                )
            )
        return found
    except (ValueError, KeyError, TypeError) as error:
        raise ConfigError(f"the pull request list is malformed ({type(error).__name__}: {error})") from error


def _add_addresses(pending: Pending, addresses: Iterable[str]) -> Pending:
    """The pending work with more addresses in its change."""
    change = Change(pending.change.paths, pending.change.addresses | frozenset(addresses))
    return Pending(pending.kind, pending.key, pending.title, pending.state, pending.branch, change)


def pending_units(env: OverlapEnv, skip_tasks: set[str], skip_branches: set[str], result: Result) -> list[Pending]:
    """The other units in progress on this machine: a started task that is not a fix, is not finished and has not
    opened its pull request, whose worktree exists. A unit whose changes cannot be read is a note."""
    found: list[Pending] = []
    conventions = env.conventions
    if not env.scratch.is_dir():
        return found
    for folder in sorted(env.scratch.iterdir()):
        path = folder / conventions.tasks.start.unit_file
        if not folder.is_dir() or not path.is_file():
            continue
        try:
            record = read_record(path)
        except UnitRecordError as error:
            result.notices.append(f"unit {folder.name}: not read ({str(error)[:_WHY]})")
            continue
        if record.parent or record.task in skip_tasks or record.branch in skip_branches or not record.worktree.is_dir():
            continue
        report = stamps.verify(env.scratch / record.task / "stamps", conventions.stamps)
        if report.ok and (
            report.latest("finished") is not None or report.latest("pr-opened", result="pass") is not None
        ):
            continue
        try:
            change = env.change_of(record)
        except (ConfigError, OSError, subprocess.SubprocessError) as error:
            result.notices.append(f"unit {record.task}: its changes were not read ({str(error)[:_WHY]})")
            continue
        who = f"claimed by {record.claimant}" if record.claimant else "in progress"
        issues = ", ".join(f"#{number}" for number in record.issues)
        found.append(Pending("unit", record.task, f"for {issues}", f"unit in progress, {who}", record.branch, change))
    return found


def pending_prs(env: OverlapEnv, wanted: Wanted, skip_branches: set[str], result: Result) -> list[Pending]:
    """The open pull requests other than the unit's own, each with its `@orig` addresses when the unit names any (the
    diffs of at most `tasks.overlap.diff_prs` of them are read)."""
    rules = env.conventions.tasks.overlap
    if env.gh is None:
        result.prs_failed = True
        result.notices.append("the open pull requests were not read (no gh)")
        return []
    try:
        listed = read_open_prs(env.gh, rules)
    except (OSError, ConfigError) as error:
        result.prs_failed = True
        result.notices.append(f"the open pull requests were not read ({str(error)[:_WHY]})")
        return []
    if len(listed) >= rules.open_prs:
        result.notices.append(f"{len(listed)} open pull requests read: the limit, so some may be missing")
    prs = [pending for pending, _ in listed if pending.branch not in skip_branches]
    result.read_prs = len(prs)
    if not wanted.addresses:
        return prs
    diffed = 0
    for index, pending in enumerate(prs):
        if diffed >= rules.diff_prs:
            result.notices.append(
                f"only the first {rules.diff_prs} open pull requests were read for @orig addresses "
                f"({len(prs) - index} left)"
            )
            break
        diffed += 1
        try:
            patch = env.gh(["pr", "diff", pending.key.lstrip("#")])
        except OSError as error:
            result.notices.append(f"PR {pending.key}: its diff was not read ({str(error)[:_WHY]})")
            continue
        prs[index] = _add_addresses(pending, issue_impact.diff_addresses(patch))
    return prs


# --- the match --------------------------------------------------------------------------------------------------


def overlaps(wanted: Wanted, pending: Iterable[Pending]) -> list[Overlap]:
    """The pending work that shares a path or an address with `wanted`, the most shared first, then pull requests
    before units, then by key."""
    found = []
    mine = set(wanted.addresses)
    for item in pending:
        paths = tuple(sorted(path for path in item.change.paths if wanted.covers(path)))
        addresses = tuple(sorted(item.change.addresses & mine))
        if paths or addresses:
            found.append(Overlap(item, paths, addresses))
    found.sort(key=lambda o: (-len(o.addresses), -len(o.paths), o.pending.kind != "pr", o.pending.key))
    return found


def compute(env: OverlapEnv, task: TaskFile, record: UnitRecord | None) -> Result:
    """The pending work that overlaps the task: what it names, and what its branch changes once it has one."""
    result = Result()
    wanted = wanted_of_task(task)
    skip_tasks = {task.task, task.parent} - {""}
    skip_branches = {task.task}
    if record is not None:
        skip_tasks.add(record.task)
        skip_branches.add(record.branch)
        if record.worktree.is_dir():
            try:
                wanted = wanted.plus(env.change_of(record))
            except (ConfigError, OSError, subprocess.SubprocessError) as error:
                result.notices.append(f"this unit's own changes were not read ({str(error)[:_WHY]})")
    pending = pending_prs(env, wanted, skip_branches, result)
    # A unit with an open pull request is listed by the pull request; its branch is skipped among the units.
    open_branches = {item.branch for item in pending}
    units = pending_units(env, skip_tasks, skip_branches | open_branches, result)
    result.read_units = len(units)
    result.overlaps = overlaps(wanted, [*pending, *units])
    return result


# --- the task file section --------------------------------------------------------------------------------------


def entry_lines(result: Result, rules: OverlapRules) -> list[str]:
    """The list entries: at most `max_listed`, then a line counting the rest."""
    lines = [item.line(rules.max_paths) for item in result.overlaps[: rules.max_listed]]
    rest = len(result.overlaps) - rules.max_listed
    if rest > 0:
        lines.append(f"- and {rest} more (`coney-tools task overlap` lists the strongest first)")
    return lines


def section_body(result: Result, rules: OverlapRules, now: datetime, base: str = "main") -> str:
    """The text of the task file section: when it was read, the list and the notes about what could not be read."""
    read = (
        f"_Read {now:%Y-%m-%d %H:%M} UTC: {result.read_prs} open pull request(s) and {result.read_units} other unit(s) "
        f"in progress. `{base}` is the truth: read these changes, never build on them unless the unit is stacked "
        "(`Depends on #N`), note an overlap that matters in STATUS.md and tell the coordinator._"
    )
    entries = entry_lines(result, rules)
    body = [read, ""]
    body += entries or ["None: nothing pending touches the paths or addresses this unit names or changes."]
    body += ["", *(f"Not read: {notice}." for notice in result.notices)] if result.notices else []
    return "\n".join(body).rstrip() + "\n"


def entries_of(section: str) -> list[str]:
    """The list entries of a section body (its `- ` lines)."""
    return [line.strip() for line in section.splitlines() if line.startswith("- ")]


def get_section(text: str, name: str = SECTION) -> str | None:
    """The body of the `## name` section of a task file, or None when it has none."""
    text = text.replace("\r\n", "\n")
    headings = list(_HEADING.finditer(text))
    for index, heading in enumerate(headings):
        if heading.group("name") == name:
            end = headings[index + 1].start() if index + 1 < len(headings) else len(text)
            return text[heading.end() : end].strip("\n")
    return None


def set_section(text: str, body: str, name: str = SECTION) -> str:
    """The task file with the `## name` section holding `body`: replaced where it is, else added at the end."""
    text = text.replace("\r\n", "\n")
    block = f"## {name}\n\n{body.strip()}\n"
    headings = list(_HEADING.finditer(text))
    for index, heading in enumerate(headings):
        if heading.group("name") == name:
            end = headings[index + 1].start() if index + 1 < len(headings) else len(text)
            return text[: heading.start()] + block + ("\n" + text[end:] if end < len(text) else "")
    return text.rstrip("\n") + "\n\n" + block


def _read_task(env: OverlapEnv, task_id: str) -> tuple[Path, TaskFile, UnitRecord | None]:
    """The task file path, the task and its unit record (None before `task start`); ConfigError when the task file is
    missing or unreadable."""
    path = env.scratch / task_id / env.conventions.tasks.file
    if not path.is_file():
        raise ConfigError(f"{task_id}: no task file at {path}")
    task = parse_task_file(path.read_text(encoding="utf-8"), env.conventions.tasks)
    record_path = env.scratch / task_id / env.conventions.tasks.start.unit_file
    record = None
    if record_path.is_file():
        try:
            record = read_record(record_path)
        except UnitRecordError:
            record = None
    return path, task, record


def write_section(path: Path, body: str) -> None:
    """Put `body` in the task file's section."""
    path.write_text(set_section(path.read_text(encoding="utf-8"), body), encoding="utf-8")


def record_section(env: OverlapEnv, path: Path, result: Result) -> bool:
    """Record `result` in the task file's section; False (nothing written) when the open pull requests could not be
    read and the section holds a list already, which a read that missed them would only make shorter."""
    if result.prs_failed and get_section(path.read_text(encoding="utf-8")) is not None:
        return False
    rules = env.conventions.tasks.overlap
    write_section(path, section_body(result, rules, env.now(), env.conventions.repo.default_branch))
    return True


# --- the commands -----------------------------------------------------------------------------------------------


def show(env: OverlapEnv, task_id: str, write: bool) -> list[str]:
    """`task overlap`: the lines to print for a task; with `write` also the task file's section is replaced."""
    path, task, record = _read_task(env, task_id)
    result = compute(env, task, record)
    rules = env.conventions.tasks.overlap
    kept = write and not record_section(env, path, result)
    lines = [f"task overlap {task_id}: {len(result.overlaps)} pending change(s) overlap this unit"]
    lines += [f"  {line}" for line in entry_lines(result, rules)]
    lines += [f"  note: {notice}" for notice in result.notices]
    if kept:
        lines.append("  note: the task file's section was kept as it is (the pull requests were not read)")
    return lines


def start_lines(env: OverlapEnv, task: TaskFile, record: UnitRecord | None, write: bool, path: Path) -> list[str]:
    """What `task start` prints about the pending work, and (with `write`) writes into the task file at `path`."""
    result = compute(env, task, record)
    rules = env.conventions.tasks.overlap
    written = write and record_section(env, path, result)
    head = f"pending work that overlaps: {len(result.overlaps) or 'none'}" + (
        " (written to TASK.md)" if written else ""
    )
    return [head, *(f"    {line}" for line in entry_lines(result, rules)), *(f"    note: {n}" for n in result.notices)]


def stage_report(env: OverlapEnv, task_id: str, say: Callable[[str], None], write: bool = True) -> None:
    """For `task check` and `task review`: print the list again when it differs from the task file's section (new
    work appeared, some left, or the unit's own changes now meet more), then record the list in the section. Silent
    when nothing changed, when the task has no task file or when pull requests could not be read. A fault is a note,
    never a failure of the stage."""
    try:
        path, task, record = _read_task(env, task_id)
        result = compute(env, task, record)
        rules = env.conventions.tasks.overlap
        current = entry_lines(result, rules)
        held = get_section(path.read_text(encoding="utf-8"))
        before = entries_of(held) if held is not None else None
        if result.prs_failed:
            say(f"pending work that overlaps: not checked ({'; '.join(result.notices)})")
            return
        if before is not None and before == current:
            return
        new = [line for line in current if line not in (before or [])]
        gone = len([line for line in (before or []) if line not in current])
        if held is None:
            say("pending work that overlaps (not recorded at the start):")
        else:
            say(
                "pending work that overlaps has changed since it was last recorded "
                f"({len(new)} new or changed, {gone} gone):"
            )
        for line in current or ["  none"]:
            say(f"  {line}")
        say(
            "  read the overlapping changes, never build on them unstacked, and note an overlap that matters in "
            "STATUS.md and tell the coordinator (docs/guides/pending-work.md)"
        )
        if write:
            record_section(env, path, result)
    except (ConfigError, OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        say(f"pending work that overlaps: not read ({str(error)[:_WHY]})")


def default_env(root: Path, conventions: Conventions) -> OverlapEnv:
    """The real world: `gh` bounded by `tasks.overlap.gh_seconds`, the scratch folder of the checkout at `root`."""
    return OverlapEnv(
        scratch=unit.scratch_folder(root),
        conventions=conventions,
        gh=bounded_runner(float(conventions.tasks.overlap.gh_seconds)),
        now=lambda: datetime.now(UTC),
    )


def report_stage(root: Path, task_id: str, conventions: Conventions, say: Callable[[str], None], write: bool) -> None:
    """`stage_report` in the real world, for the stage commands (`task check`, `task review`)."""
    stage_report(default_env(root, conventions), task_id, say, write)


def skip_stage(root: Path, task_id: str, conventions: Conventions, say: Callable[[str], None], write: bool) -> None:
    """The stand-in for `report_stage` that reads nothing (the default of an `Env` a test builds)."""
