# SPDX-License-Identifier: GPL-3.0-or-later
"""Steps 1-4 of a release, read only: the log, Check, the compared functions and Review (docs/guides/pr.md).

`coney-tools pr verify` runs `verify` on a unit: it trusts nothing it can recompute. It re-reads the unit's stamp log,
recomputes every hash the stamps rely on from the files and the git tree, and reports every stage that does not hold
for the change on the branch, each with the exact message and the command to redo. A stage that fails is reported
together with all the others, not alone. `pr open` will run the same function before it pushes.

What the stamps are bound to is the *change* (`pr_diff.change_sha256`), not the tip, so a clean rebase keeps them. The
rules are read from `origin/<default branch>` (the branch `origin/HEAD` names), never from the branch being released:
every section that decides whether a release is allowed (`conventions.load_release_rules_at`: the stamps, who may
review, the pull request and commit rules, the path classes, the proof's profiles, checks and build preset). The
required profile and checks come from `proof_profiles`, the functions `task check` stamps with, so the two always
agree.

Nothing here writes: no stamp, no file, no git state.
"""

from __future__ import annotations

import dataclasses
import hashlib
import json
import subprocess
from collections.abc import Callable, Mapping, Sequence
from dataclasses import dataclass, field
from datetime import UTC, datetime, timedelta
from pathlib import Path, PurePosixPath
from typing import Any

from coney_tools import (
    build_record,
    fails_first,
    identity,
    orig_copy,
    orig_cut,
    orig_pairs,
    orig_review,
    orig_study,
    pr_diff,
    proof_profiles,
    review_verdict,
    stamps,
)
from coney_tools.config import ConfigError
from coney_tools.conventions import (
    Conventions,
    ConventionsError,
    load_conventions_leniently,
    load_release_rules_at,
)
from coney_tools.orig_index import SOURCE_DIR, SOURCE_SUFFIXES
from coney_tools.orig_review import Unit as OrigUnit
from coney_tools.unit import Unit, tracked_files

#: How far a stamp's time may be ahead of this clock before it counts as in the future.
CLOCK_SKEW = timedelta(minutes=5)
#: The task kinds that have no game behaviour to test first: a `none` stamp is their fails-first proof.
NO_TEST_KINDS = ("docs", "tooling", "ci-fix")
#: What a reviewer may call a function and still let the change through (`orig_review.PASSING` also lets `unclear`
#: through for a branch's review; a release holds to the two that settle the question).
PASSING_CLASSES = ("clean", "diff-deliberate-ok")
#: The longest statement of the original that a note may not repeat, in characters.
QUOTE_MIN = 20
#: The step that follows each stage's refusal: the command to run again (`{subject}` is the stamp's).
_REDO = {
    "assigned": "task start",
    "claimed": "task start",
    "studied": "orig study {subject}",
    "compared": "orig compare {subject}",
    "fails-first": "orig fails-first",
    "checked": "task check",
    "reviewed": "task review",
}


@dataclass(frozen=True)
class TextHashes:
    """The hashes of the engineer's text a review covers (`pr_text.hashes_of` makes them)."""

    title_sha256: str
    paragraph_sha256: str
    images_sha256: str


@dataclass(frozen=True)
class Refusal:
    """One reason a release is refused: the stage that found it, the full message and what to redo."""

    #: `worktree`, `log`, `checked`, `compared`, `fails-first`, `reviewed` or (for `pr text`) `text`.
    stage: str
    #: The message, starting with the stage's name (`checked: stale: ...`).
    text: str
    #: The command to run again, or `engineer` (edit or commit), `none` (the unit is unusable: tell a maintainer).
    redo: str


@dataclass(frozen=True)
class Subject:
    """Everything a release check reads once: the branch, the rules at its base and the unit's log."""

    root: Path
    unit: Unit
    task: str
    #: The task's kind (`TASK.md`).
    kind: str
    #: The rules the release is held to: every section that decides a release (`load_release_rules_at`) as
    #: `origin/<default branch>` has it, the rest of the branch's own file.
    conventions: Conventions
    #: Where the proof rules came from: `base`, `branch` while the base has no `proof` section yet, or `given` (a
    #: test's, which replace all of them).
    rules_from: str
    remote_ref: str
    tip: str
    base: str
    report: stamps.Report
    paths: tuple[str, ...]
    diff_sha256: str
    #: The Check profile the change needs and the checks of it that apply to its paths.
    profile: str
    checks: tuple[str, ...]
    #: The `@orig` functions the branch adds or changes, and those it removes (a note, not a refusal).
    changed: tuple[OrigUnit, ...]
    removed: tuple[OrigUnit, ...]
    #: Paths with uncommitted changes (tracked, or untracked and not ignored).
    dirty: tuple[str, ...]
    now: datetime
    #: Each original address -> the keys of all the `@orig` functions at the tip that carry it (a `compared` stamp
    #: belongs to one of them when there are several).
    citing: Mapping[str, tuple[str, ...]] = field(default_factory=dict)


@dataclass
class ProofResult:
    """What `verify` found."""

    subject: Subject
    refusals: list[Refusal] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        """True when nothing is refused."""
        return not self.refusals


# --- reading the unit -----------------------------------------------------------------------------------------------


def _git(root: Path, *args: str) -> str:
    """Run git in `root`; its stdout, or a ConfigError with its message."""
    try:
        done = subprocess.run(["git", "-C", str(root), *args], capture_output=True, encoding="utf-8", check=False)
    except OSError as error:
        raise ConfigError(f"git cannot be run ({error})") from error
    if done.returncode != 0:
        raise ConfigError(f"git {' '.join(args)}: {done.stderr.strip() or 'failed'}")
    return done.stdout


def dirty_paths(root: Path) -> tuple[str, ...]:
    """The paths of `root` with uncommitted changes: tracked files that differ, and untracked files not ignored."""
    # `--no-optional-locks`: a read that refreshes the index would hold its lock, and fail an engineer's commit that
    # runs at the same moment (Conclave asks this for every unit, in the background).
    out = _git(root, "--no-optional-locks", "status", "--porcelain", "-z", "--untracked-files=all")
    entries = [e for e in out.split("\0") if e]
    paths: list[str] = []
    skip = False
    for entry in entries:
        if skip:  # the old name of a rename
            skip = False
            continue
        if entry[0] in "RC":
            skip = True
        paths.append(entry[3:])
    return tuple(sorted(paths))


#: The remote whose default branch holds the proof rules a release is held to.
RULES_REMOTE = "origin"


def default_branch(root: Path) -> str:
    """The default branch, as `origin`'s `HEAD` names it (`refs/remotes/origin/HEAD`, which `git clone` sets), not as
    the branch's own conventions file says: a branch could name another branch there. ConfigError when it is unset."""
    try:
        ref = _git(root, "symbolic-ref", "--quiet", f"refs/remotes/{RULES_REMOTE}/HEAD").strip()
    except ConfigError:
        raise ConfigError(
            f"{RULES_REMOTE}/HEAD is not set here, so the default branch is unknown; "
            f"run `git remote set-head {RULES_REMOTE} --auto`"
        ) from None
    return ref.removeprefix(f"refs/remotes/{RULES_REMOTE}/")


def _touches_source(paths: Sequence[str]) -> bool:
    """Whether a changed path is a C++ file under `src/`, the only place an `@orig` function can be added, changed or
    removed (`orig_review.files_at` reads nothing else)."""
    folder = SOURCE_DIR.as_posix() + "/"
    return any(p.startswith(folder) and PurePosixPath(p).suffix in SOURCE_SUFFIXES for p in paths)


def changed_functions(
    root: Path, base: str, tip: str, paths: Sequence[str]
) -> tuple[tuple[OrigUnit, ...], tuple[OrigUnit, ...], dict[str, tuple[str, ...]]]:
    """The `@orig` functions the branch adds or changes, those it removes, between `base` and `tip`, and which Coney
    functions at the tip cite each original address (`orig_review.citing`: a `compared` stamp belongs to one of
    several functions that share an address, so the lookup needs all of them, not only the changed ones).

    Reading every source file at two commits takes seconds, so a change that touches no C++ file under `src/` (a
    guide, a tool, a workflow) is known to add, change and remove none, and to need no stamp looked up, without reading
    them."""
    if not _touches_source(paths):
        return (), (), {}
    before = {u.key: u for u in orig_review.units_at(root, base)}
    after = {u.key: u for u in orig_review.units_at(root, tip)}
    changed = tuple(
        u
        for u in after.values()
        if u.key not in before or before[u.key].body_sha256 != u.body_sha256 or before[u.key].cited != u.cited
    )
    removed = tuple(u for key, u in before.items() if key not in after)
    return changed, removed, orig_review.citing(after.values())


def load_subject(
    root: Path,
    unit: Unit,
    task: str,
    kind: str,
    *,
    remote: str = "origin",
    now: datetime | None = None,
    conventions: Conventions | None = None,
    scan: bool = True,
    say: Callable[[str], None] | None = None,
) -> Subject:
    """Read the unit at `root` (its worktree): the branch tip, where it left `<remote>/<default branch>`, the rules of
    `origin/<default branch>` (`conventions` replaces them, for the tests), the stamp log and the `@orig` functions it
    changes.

    The default branch is the one `origin`'s `HEAD` names (`default_branch`), and the release rules are always read from
    `origin/<default branch>`, whatever `remote` is: nothing the branch holds or names (its conventions file, a remote
    it adds) chooses the ref its rules come from. ConfigError when a ref is unknown (fetch it first), or git or the
    rules cannot be read.

    `scan=False` leaves out the `@orig` functions (`changed` and `removed` are empty), which are the slow part on a
    change under `src/`; `scanned` adds them. `refusals_before_scan` is what such a subject can already refuse.
    The branch's own conventions file is read leniently (an unknown key is skipped); `say`, if given, is told which.
    """
    default = default_branch(root)
    remote_ref = f"{remote}/{default}"
    rules_ref = f"{RULES_REMOTE}/{default}"
    for ref, name in ((remote_ref, remote), (rules_ref, RULES_REMOTE)):
        try:
            _git(root, "rev-parse", "--verify", "--quiet", f"{ref}^{{commit}}")
        except ConfigError:
            raise ConfigError(f"{ref} is not known here; run `git fetch {name} {default}` first") from None
    if conventions is not None:
        rules, rules_from = conventions, "given"
    else:
        try:
            rules, rules_from = load_release_rules_at(root, rules_ref, load_conventions_leniently(root, say))
        except ConventionsError as error:
            raise ConfigError(str(error)) from error
    tip = _git(root, "rev-parse", "--verify", "HEAD").strip()
    change = pr_diff.change_of(root, remote_ref, tip)
    base, paths = change.base, change.paths
    profile = proof_profiles.required_profile(rules, paths)
    changed, removed, citing = changed_functions(root, base, tip, paths) if scan else ((), (), {})
    return Subject(
        root=root,
        unit=unit,
        task=task,
        kind=kind,
        conventions=rules,
        rules_from=rules_from,
        remote_ref=remote_ref,
        tip=tip,
        base=base,
        report=stamps.verify(unit.stamps, rules.stamps, task=task),
        paths=paths,
        diff_sha256=change.sha256,
        profile=profile,
        checks=tuple(proof_profiles.required_checks(rules, profile, paths)),
        changed=changed,
        citing=citing,
        removed=removed,
        dirty=dirty_paths(root),
        now=now or datetime.now(UTC),
    )


def scanned(subject: Subject) -> Subject:
    """`subject` with the `@orig` functions its change adds, changes and removes read in (`load_subject(scan=False)`
    leaves them out)."""
    changed, removed, citing = changed_functions(subject.root, subject.base, subject.tip, subject.paths)
    return dataclasses.replace(subject, changed=changed, removed=removed, citing=citing)


def refusals_before_scan(subject: Subject) -> list[Refusal]:
    """What a subject read with `scan=False` can already refuse, without reading an `@orig` function: uncommitted
    changes, a Check that is missing, failed or made for another change, and a Review that is missing, failed or made
    for another change. Each is a refusal `verify` finds too (these are the same functions), so a unit refused here is
    refused there; one that passes here may still be refused there, by the stages that need the functions."""
    found = _dirty_refusals(subject)
    found += check_checked(subject)
    stamp = subject.report.latest("reviewed")
    refused = _reviewed_unsettled(stamp)
    if refused is not None:
        return [*found, refused]
    assert stamp is not None  # a missing stamp is refused above
    stale = _review_change_stale(subject, stamp)
    return [*found, *([stale] if stale is not None else [])]


def pairs_set_sha256(unit: Unit, addresses: Sequence[str]) -> str:
    """The `pairs_set_sha256` of the changed functions now (`orig_pairs.set_sha256`, the definition `task review`
    binds a review to), so a pairs file edited after Review makes it stale. An address with no pairs file is left
    out; step 3 says it is missing."""
    hashes: dict[str, str] = {}
    for address in addresses:
        try:
            hashes[address] = orig_pairs.file_sha256(unit.mapping(address))
        except ConfigError:
            continue
    return orig_pairs.set_sha256(hashes)


def changed_addresses(subject_changed: Sequence[OrigUnit]) -> list[str]:
    """The original addresses of the changed functions, in order."""
    return [address for unit in subject_changed for address in _addresses(unit)]


def _hex(value: object, size: int = 8) -> str:
    """The first `size` characters of a hash, for a message."""
    return str(value)[:size] if value else "none"


def _addresses(unit: OrigUnit) -> list[str]:
    """A unit's original addresses as `0x00501000`."""
    return [f"0x{a:08x}" for a in unit.addresses]


def _redo(step: str, subject: str = "") -> str:
    """The command that redoes a stage (`none` for a step nobody redoes)."""
    return _REDO.get(step, "none").format(subject=subject).strip()


# --- the whole ------------------------------------------------------------------------------------------------------


def _dirty_refusals(subject: Subject) -> list[Refusal]:
    """The refusal for a worktree with uncommitted changes (the stamps are bound to what is committed)."""
    if not subject.dirty:
        return []
    shown = ", ".join(subject.dirty[:5]) + (f" and {len(subject.dirty) - 5} more" if len(subject.dirty) > 5 else "")
    text = f"worktree: uncommitted changes: {shown}; the engineer commits or removes them"
    return [Refusal("worktree", text, "engineer")]


def verify(
    subject: Subject,
    text: TextHashes | None = None,
    *,
    copy_check: Callable[[Path, str], list[orig_copy.Finding]] = orig_copy.check,
) -> ProofResult:
    """Steps 1-4 on `subject`: every stage's refusals, in the order of the workflow.

    `text` holds the hashes of the engineer's file now (title, paragraph, pictures), which Review covered; without
    it those three are not compared. A log that is not intact stops the run after step 1: nothing else in it can be
    trusted.
    """
    result = ProofResult(subject)
    result.refusals += _dirty_refusals(subject)
    log, intact = check_log(subject)
    result.refusals += log
    if not intact:
        return result
    result.refusals += check_checked(subject)
    compared, notes = check_compared(subject, copy_check)
    result.refusals += compared
    result.notes += notes
    result.refusals += check_fails_first(subject)
    result.refusals += check_reviewed(subject, text)
    return result


# --- step 1: the log ------------------------------------------------------------------------------------------------


def _author_date(root: Path, commit: str, cache: dict[str, datetime | None]) -> datetime | None:
    """The author date of `commit`, None when it no longer exists (a rebase rewrote it)."""
    if commit not in cache:
        done = subprocess.run(
            ["git", "-C", str(root), "show", "-s", "--format=%aI", commit],
            capture_output=True,
            encoding="utf-8",
            check=False,
        )
        try:
            cache[commit] = (
                datetime.fromisoformat(done.stdout.strip()).astimezone(UTC) if done.returncode == 0 else None
            )
        except ValueError:
            cache[commit] = None
    return cache[commit]


def check_log(subject: Subject) -> tuple[list[Refusal], bool]:
    """Step 1: the log is intact and was written the way the workflow writes it. Returns the refusals and whether the
    chain itself holds (when it does not, the other steps must not run)."""
    report, rules = subject.report, subject.conventions
    found: list[Refusal] = []
    errors = [p for p in report.problems if p.severity == "error"]
    for problem in errors:
        found.append(Refusal("log", f"log: {problem.where}: {problem.text}", "none"))
    if errors:
        return found, False
    if not report.stamps:
        return [Refusal("log", f"log: {subject.task} has no stamps; run task start", "task start")], False
    # The start of the log: assigned, then claimed, by an engineer.
    start = report.stamps[:2]
    if [s.step for s in start] != ["assigned", "claimed"] or any(s.result != "pass" for s in start):
        seen = ", ".join(s.step for s in start)
        found.append(
            Refusal("log", f"log: the log does not start with assigned and claimed (found: {seen})", "task start")
        )
    else:
        who = start[1].who.get("agent")
        if not (isinstance(who, dict) and isinstance(who.get("id"), str) and who["id"]):
            found.append(Refusal("log", "log: the claimed stamp names no engineer", "task start"))
    # The agent behind each stamp the other steps rely on was vouched for by the hook.
    if rules.stamps.require_hook_source:
        latest: dict[tuple[str, str], stamps.Stamp] = {}
        for stamp in report.stamps:
            latest[(stamp.step, stamp.subject)] = stamp
        for stamp in latest.values():
            if stamp.who.get("source") != "hook":
                what = f"{stamp.step} {stamp.subject}" if stamp.subject else stamp.step
                found.append(
                    Refusal(
                        "log",
                        f"log: seq {stamp.seq} {what} was written without the hook identity (source unverified)",
                        _redo(stamp.step, stamp.subject),
                    )
                )
    # Times: never in the future, never before the commit the stamp names was written.
    cache: dict[str, datetime | None] = {}
    for stamp in report.stamps:
        if stamp.moment > subject.now + CLOCK_SKEW:
            found.append(Refusal("log", f"log: seq {stamp.seq}: time {stamp.time} is in the future", "none"))
        authored = _author_date(subject.root, stamp.commit, cache) if stamp.commit else None
        if authored is not None and stamp.moment < authored.replace(microsecond=0):
            found.append(
                Refusal(
                    "log",
                    f"log: seq {stamp.seq}: time {stamp.time} is before its commit {stamp.commit[:12]} was authored "
                    f"({authored.strftime('%Y-%m-%dT%H:%M:%SZ')})",
                    "none",
                )
            )
    found += _quotes(subject)
    return found, True


def _squash(text: str) -> str:
    """`text` with every run of whitespace made one space."""
    return " ".join(text.split())


def _quotes(subject: Subject) -> list[Refusal]:
    """No note or merge reason of a changed function's pairs repeats a statement of the saved decompile (LEGAL.md)."""
    found: list[Refusal] = []
    for unit in subject.changed:
        for address in _addresses(unit):
            saved = subject.unit.study(address) / "decompile.c"
            try:
                statements = orig_cut.cut_decompile(orig_cut.normalise(saved.read_text(encoding="utf-8"))).statements
                pairs = orig_pairs.load(subject.unit.mapping(address)).pairs
            except (OSError, ConfigError):
                continue  # step 3 says what is missing
            quoted = [s.text for s in statements if len(_squash(s.text)) >= QUOTE_MIN]
            for number, pair in enumerate(pairs, start=1):
                for label, value in (("note", pair.note), ("merge_reason", pair.merge_reason)):
                    flat = _squash(value)
                    if any(_squash(statement) in flat for statement in quoted):
                        found.append(
                            Refusal(
                                "log",
                                f"log: mapping/{address}.toml pair {number} {label} repeats a line of the original; "
                                "say it in your own words",
                                f"orig compare {address}",
                            )
                        )
    return found


# --- step 2: Check --------------------------------------------------------------------------------------------------


def check_checked(subject: Subject) -> list[Refusal]:
    """Step 2: a passing `checked` stamp for this change, of a big enough profile, with the checks it lists and (for a
    full Check) the build record it names."""
    stamp = subject.report.latest("checked")
    refused = _checked_unsettled(stamp)
    if refused is not None:
        return [refused]
    assert stamp is not None  # a missing stamp is refused above
    if stamp.covers.get(stamps.CHANGE) != subject.diff_sha256:
        saved = stamp.covers.get(stamps.CHANGE)
        text = f"checked: stale: the branch changed since Check (diff {_hex(saved)} now {_hex(subject.diff_sha256)})"
        return [Refusal("checked", text, "task check")]
    proof = subject.conventions.proof
    required = subject.profile
    ran = str(stamp.detail.get("profile"))
    names = [p.name for p in proof.profiles]
    if ran not in names:
        return [
            Refusal(
                "checked", f"checked: Check ran profile {ran!r}, which is not one of {', '.join(names)}", "task check"
            )
        ]
    if not proof_profiles.at_least(ran, required, subject.conventions):
        ran_classes = next(p.classes for p in proof.profiles if p.name == ran)
        culprit = next((p for p in subject.paths if subject.conventions.paths.ci_class(p) not in ran_classes), "")
        return [
            Refusal(
                "checked",
                f"checked: Check ran profile {ran}, this change needs {required} (touches {culprit})",
                "task check",
            )
        ]
    found: list[Refusal] = []
    listed = stamp.detail.get("checks")
    done = set(listed) if isinstance(listed, list) else set()
    for check in subject.checks:
        if check not in done:
            found.append(Refusal("checked", f"checked: Check did not run: {check}", "task check"))
    # A profile with the build checks has a build record behind them (`proof.build`), as `task check` writes it.
    if set(subject.checks) & set(proof.build.checks):
        found += _build_record(subject, stamp)
    return found


def _checked_unsettled(stamp: stamps.Stamp | None) -> Refusal | None:
    """The refusal when the newest `checked` stamp is missing or did not pass, whatever it covers."""
    if stamp is None:
        return Refusal("checked", "checked: no passing Check; run task check", "task check")
    if stamp.result != "pass":
        return Refusal(
            "checked", "checked: the newest Check failed; fix what it found and run task check", "task check"
        )
    return None


def _build_record(subject: Subject, stamp: stamps.Stamp) -> list[Refusal]:
    """The `full` Check's build record: it exists and hashes to what the stamp covers, `build_record.problems` (the
    rule `task check` applies) finds nothing wrong with it at the commit the stamp names, and its test counts are the
    stamp's."""
    wanted = str(stamp.covers.get("build_record_sha256", ""))

    def refuse(text: str) -> list[Refusal]:
        return [Refusal("checked", f"checked: {text}", "task check")]

    try:
        record = build_record.read(subject.unit.dir / build_record.FOLDER, wanted)
    except build_record.BuildRecordError as error:
        return refuse(f"the build record {_hex(wanted, 12)} cannot be read: {error}")
    preset = subject.conventions.proof.build.preset
    found: list[Refusal] = []
    for problem in build_record.problems(record, tip=stamp.commit, preset=preset):
        found += refuse(problem)
    counts = record.get("tests")
    tests: dict[str, Any] = counts if isinstance(counts, dict) else {}
    counted = stamp.detail.get("tests")
    keys = ("total", "passed", "failed", "skipped", "skipped_disc")
    if not isinstance(counted, dict) or any(counted.get(k) != tests.get(k) for k in keys):
        found += refuse("the build record's test counts differ from the Check stamp's")
    return found


# --- step 3: the compared functions and the tests that failed first ---------------------------------------------------


def check_compared(
    subject: Subject, copy_check: Callable[[Path, str], list[orig_copy.Finding]] = orig_copy.check
) -> tuple[list[Refusal], list[str]]:
    """Step 3: every `@orig` function the branch adds or changes was studied and compared at the body it has now, at
    every address it carries, and is clean of decompiler fingerprints. Returns the refusals and the notes (functions
    the branch removed)."""
    found: list[Refusal] = []
    notes = [
        f"{u.coney} ({', '.join(_addresses(u))}) is no longer in the code: removed or untagged by this change"
        for u in subject.removed
    ]
    tracked = tracked_files(subject.root)
    files: set[str] = set()
    for unit in subject.changed:
        files.add(unit.path)
        for address in _addresses(unit):
            found += _compared_one(subject, unit, address, tracked, files)
    if subject.changed:
        for finding in copy_check(subject.root, subject.remote_ref):
            if finding.path in files:
                found.append(Refusal("compared", f"compared: {finding} (copy-check)", "engineer, then orig compare"))
    return found, notes


def _compared_one(
    subject: Subject, unit: OrigUnit, address: str, tracked: Mapping[str, str], files: set[str]
) -> list[Refusal]:
    """The study, the pairs and the `compared` stamp of one address of a changed function."""
    report = subject.report
    redo_study, redo_compare = f"orig study {address}", f"orig compare {address}"
    if len(subject.citing.get(address, ())) > 1:  # then `compare` must be told which function
        redo_compare += f" --coney-function '{unit.key}'"

    def refuse(text: str, redo: str = redo_compare) -> Refusal:
        return Refusal("compared", f"compared: {text}", redo)

    seen = report.latest("studied", address)
    if seen is None:
        return [refuse(f"{unit.coney} ({address}) was changed and never studied", redo_study)]
    try:
        study = orig_study.load_study(subject.unit, address)
    except ConfigError as error:
        return [refuse(f"{address}: the saved study is unusable ({error})", redo_study)]
    wanted = {k: study.covers[k] for k in ("identity_sha256", "decompile_sha256", "ghidra_function_hash", "cut_sha256")}
    if report.current("studied", address, wanted) is None:
        return [refuse(f"{address}: study/decompile.c differs from what the studied stamp covers", redo_study)]
    path = subject.unit.mapping(address)
    if not path.is_file():
        return [refuse(f"{address}: there is no pairs file; the function was never compared")]
    try:
        file = orig_pairs.load(path)
    except ConfigError as error:
        return [refuse(f"{address}: {error}")]
    header = orig_pairs.header_from_cut(
        study.cut,
        address=address,
        name=study.name,
        program=study.program,
        decompile_sha256=study.decompile_sha256,
        ghidra_function_hash=study.ghidra_function_hash,
        coney=unit.key,
    )
    rules = subject.conventions.stamps.pairs
    problems = orig_pairs.check_structure(file, study.cut, address, rules, header)
    if not problems:
        problems += orig_pairs.check_code(file, subject.root, rules, tracked)
    found = [refuse(f"{address}: {p}") for p in problems]
    files.update(orig_pairs.pair_files(file, tracked))
    covers = {
        "decompile_sha256": study.decompile_sha256,
        "ghidra_function_hash": study.ghidra_function_hash,
        "cut_sha256": study.cut_sha256,
        "pairs_sha256": orig_pairs.file_sha256(path),
        "coney_body_sha256": unit.body_sha256,
    }
    cited_by = subject.citing.get(address, ())
    if orig_review.current_compared(subject.root, report, address, covers, unit.key, cited_by) is not None:
        return found
    last = report.latest("compared", address, function=unit.key, cited_by=cited_by)
    if last is None:
        found.append(refuse(f"{address}: {unit.coney} was never compared"))
    elif last.result != "pass":
        found.append(refuse(f"{address}: the newest compare failed"))
    elif last.covers.get("coney_body_sha256") != unit.body_sha256:
        found.append(
            refuse(
                f"{address}: {unit.coney} changed after it was compared (body "
                f"{_hex(last.covers.get('coney_body_sha256'), 4)} now {_hex(unit.body_sha256, 4)})"
            )
        )
    elif last.covers.get("pairs_sha256") != covers["pairs_sha256"]:
        found.append(refuse(f"{address}: the pairs file changed after it was compared"))
    else:
        found.append(refuse(f"{address}: the study changed after it was compared"))
    return found


def _added_tests(subject: Subject) -> dict[str, str]:
    """The `TEST_CASE` names the branch adds under the C++ test folder -> the file that declares each."""
    changed = [p for p in subject.paths if p.startswith(fails_first.TEST_DIR)]
    before: set[str] = set()
    after: dict[str, str] = {}
    for path in changed:
        before |= fails_first.declared_in(subject.root, subject.base, path)
        for name in fails_first.declared_in(subject.root, subject.tip, path):
            after.setdefault(name, path)
    return {name: path for name, path in after.items() if name not in before}


def _decision_refusal(subject: Subject, stamp: stamps.Stamp) -> Refusal | None:
    """A passing stamp of a test that changed between its commits rests on the coordinator's decision
    (`fails_first.record_decision`, the stamp its `decision_seq` names): None when that decision is in the log for the
    same test and commits (and, with `stamps.require_hook_source`, the hook vouched that the coordinator wrote it)."""
    seq = stamp.detail.get("decision_seq")
    if seq is None:
        return None
    decision = next((s for s in subject.report.stamps if s.seq == seq), None)
    agent = decision.who.get("agent") if decision is not None else None
    if (
        decision is not None
        and decision.step == "fails-first"
        and decision.subject == stamp.subject
        and decision.detail.get("decision") == fails_first.DECISION
        and all(decision.covers.get(key) == stamp.covers.get(key) for key in ("test_commit", "fix_commit"))
        and (
            not subject.conventions.stamps.require_hook_source
            or (decision.who.get("source") == "hook" and isinstance(agent, dict) and agent.get("type") == "coordinator")
        )
    ):
        return None
    return Refusal(
        "fails-first",
        f'fails-first: "{stamp.subject}" changed between its commits and rests on decision #{seq}, which is not the '
        "coordinator's decision for them",
        'the coordinator: orig fails-first --accept-change "<reason>"',
    )


def check_fails_first(subject: Subject) -> list[Refusal]:
    """The tests the branch adds each failed first (a passing stamp over the test files as they are now); a task that
    has nothing to test says so with a `none` stamp; a gameplay change with no test is refused."""
    report = subject.report
    found: list[Refusal] = []
    added = _added_tests(subject)
    for name in sorted(added):
        stamp = report.latest("fails-first", name)
        if stamp is None or stamp.result != "pass":
            found.append(
                Refusal(
                    "fails-first",
                    f'fails-first: "{name}" was added and has no passing fails-first stamp',
                    "orig fails-first",
                )
            )
            continue
        refused = _decision_refusal(subject, stamp)
        if refused is not None:
            found.append(refused)
            continue
        files = stamp.detail.get("test_files")
        listed = [str(f) for f in files] if isinstance(files, list) else []
        if stamp.covers.get("tests_sha256") != fails_first.tests_sha256(subject.root, subject.tip, listed):
            found.append(
                Refusal(
                    "fails-first",
                    f'fails-first: "{name}": its test file changed since ({", ".join(listed)})',
                    "orig fails-first",
                )
            )
    if added:
        return found
    # A coordinator's decision on a changed test proves nothing by itself: only the run after it does.
    proofs = [
        s
        for s in report.stamps
        if s.step == "fails-first" and s.result in ("pass", "none") and s.detail.get("decision") != fails_first.DECISION
    ]
    if subject.kind in NO_TEST_KINDS:
        if not proofs:
            found.append(
                Refusal(
                    "fails-first",
                    f"fails-first: no fails-first stamp; a {subject.kind} task records "
                    '`orig fails-first --none "<reason>"`',
                    "orig fails-first",
                )
            )
        return found
    gameplay = next((p for p in subject.paths if subject.conventions.paths.gameplay_dir(p)), "")
    if gameplay and not any(s.result == "pass" for s in proofs):
        folder = next(f for f in subject.conventions.paths.gameplay if gameplay.startswith(f))
        found.append(
            Refusal(
                "fails-first",
                f"fails-first: a feature or fix under {folder} needs a test that failed first, or --none with a reason",
                "orig fails-first",
            )
        )
    return found


# --- step 4: Review -------------------------------------------------------------------------------------------------


#: A class by its one letter, as `review_verdict` writes it to a stamp's small detail.
_LETTERS = {letter: name for name, letter in review_verdict.CLASS_LETTERS.items()}


def _class_name(value: object) -> str | None:
    """The class a stamp's `functions` entry names: its one letter or its full name; None when there is none."""
    if not isinstance(value, str):
        return None
    return _LETTERS.get(value, value)


def _review_functions(subject: Subject, stamp: stamps.Stamp) -> Mapping[str, Any] | None:
    """The address -> class map of a review: in the stamp, or (when it was too big) in the task's recorded verdict
    (`reviewed.json`, or `review.json` for a review recorded before the rename), whose hash the stamp covers as
    `review_sha256`."""
    inline = stamp.detail.get("functions")
    if isinstance(inline, dict):
        return _lower_keys(inline)
    wanted = stamp.covers.get("review_sha256")
    for name in (review_verdict.RECORDED, review_verdict.RECORDED_BEFORE):
        try:
            raw = (subject.unit.dir / name).read_bytes()
            data = json.loads(raw)
        except (OSError, ValueError):
            continue
        if wanted and hashlib.sha256(raw).hexdigest() == wanted and isinstance(data, dict):
            listed = _listed_functions(data)
            return None if listed is None else _lower_keys(listed)
    return None


def _lower_keys(functions: Mapping[str, Any]) -> Mapping[str, Any]:
    """`functions` with its addresses in lower case, as `review_addresses` writes them and the verdict check compares
    them: a reviewer may copy `Xbox 0x000BD3C0` from a research page, and it covers `xbox 0x000bd3c0`."""
    return {str(address).lower(): judged for address, judged in functions.items()}


def _listed_functions(data: Mapping[str, Any]) -> Mapping[str, Any] | None:
    """The address -> class map of a verdict's `functions` list, or None when it has none."""
    listed = data.get("functions")
    if not isinstance(listed, list):
        return None
    return {
        str(address): item.get("class")
        for item in listed
        if isinstance(item, dict)
        for address in item.get("addresses", [])
    }


def _worked_on(subject: Subject, reviewer: Mapping[str, object]) -> bool:
    """Whether `reviewer` wrote any stamp of the unit other than a `reviewed` one: the rule `task review --record`
    applies, by `identity.same_agent`, whatever the agent's type (an engineer, a tooling agent or the coordinator)."""
    return any(
        stamp.step != "reviewed"
        and isinstance(stamp.who.get("agent"), dict)
        and identity.same_agent(reviewer, stamp.who["agent"])
        for stamp in subject.report.stamps
    )


def _reviewed_unsettled(stamp: stamps.Stamp | None) -> Refusal | None:
    """The refusal when the newest `reviewed` stamp is missing or is a failed Review, whatever it covers."""
    if stamp is None:
        return Refusal("reviewed", "reviewed: not reviewed", "task review")
    if stamp.result != "pass" or stamp.detail.get("verdict", "pass") != "pass":
        blockers = stamp.detail.get("blockers", 0)
        count = (
            f"{blockers} blocking finding{'s' if blockers != 1 else ''}"
            if isinstance(blockers, int)
            else "blocking findings"
        )
        return Refusal("reviewed", f"reviewed: Review failed ({count}, see the verdict)", "task review")
    return None


def _review_change_stale(subject: Subject, stamp: stamps.Stamp) -> Refusal | None:
    """The refusal when the Review `stamp` was made for another change than the branch makes now."""
    if stamp.covers.get(stamps.CHANGE) == subject.diff_sha256:
        return None
    text = (
        f"reviewed: stale: the branch changed since Review (diff {_hex(stamp.covers.get('diff_sha256'))} now "
        f"{_hex(subject.diff_sha256)})"
    )
    return Refusal("reviewed", text, "task review")


def unsettled(report: stamps.Report) -> list[Refusal]:
    """What the stamp log alone refuses, without reading git: no passing `checked` stamp, no passing `reviewed` stamp.

    These are the first refusals of `check_checked` and `check_reviewed`, which use the same two functions, so a
    caller that wants a cheap answer first (Conclave's `unit-ready`) can never say something `verify` would not."""
    found = [_checked_unsettled(report.latest("checked")), _reviewed_unsettled(report.latest("reviewed"))]
    return [refusal for refusal in found if refusal is not None]


def check_reviewed(subject: Subject, text: TextHashes | None) -> list[Refusal]:
    """Step 4: a passing `reviewed` stamp for this change, these pairs and this text, that covers every changed function
    with a passing class, by a reviewer who did not work on the task."""
    stamp = subject.report.latest("reviewed")

    def refuse(message: str, redo: str = "task review") -> Refusal:
        return Refusal("reviewed", f"reviewed: {message}", redo)

    refused = _reviewed_unsettled(stamp)
    if refused is not None:
        return [refused]
    assert stamp is not None  # a missing stamp is refused above
    stale: list[Refusal] = []
    changed_since = _review_change_stale(subject, stamp)
    if changed_since is not None:
        stale.append(changed_since)
    if stamp.covers.get("pairs_set_sha256") != pairs_set_sha256(subject.unit, changed_addresses(subject.changed)):
        stale.append(refuse("stale: the pairs changed since Review"))
    if text is not None:
        for key, what in (
            ("title_sha256", "the pull request title"),
            ("paragraph_sha256", "the summary paragraph"),
            ("images_sha256", "the screenshots"),
        ):
            if stamp.covers.get(key) != getattr(text, key):
                stale.append(refuse(f"stale: {what} changed since Review", "task review --text"))
    if stale:
        return stale
    found: list[Refusal] = []
    functions = _review_functions(subject, stamp)
    for unit in subject.changed:
        for address in unit.review_addresses:
            judged = None if functions is None else _class_name(functions.get(address))
            if judged is None:
                found.append(refuse(f"Review did not cover {address}"))
            elif judged not in PASSING_CLASSES:
                found.append(refuse(f"{address} was judged {judged}", "engineer, then task check and task review"))
    if any(subject.conventions.tasks.review.is_research_page(p) for p in subject.paths):
        pages = stamp.detail.get("pages")
        if not (isinstance(pages, list) and pages):
            found.append(refuse("Review did not grade the research pages this change touches"))
    found += _reviewer(subject, stamp, refuse)
    return found


def _reviewer(subject: Subject, stamp: stamps.Stamp, refuse: Callable[..., Refusal]) -> list[Refusal]:
    """Who reviewed: not someone who worked on the task; when the hook vouches for identities, the reviewer agent of
    `tasks.review`, on a model `tasks.review.reverse_engineering_models` allows for an `@orig` or research change."""
    info = stamp.detail.get("reviewer")
    reviewer = info if isinstance(info, dict) else {}
    found: list[Refusal] = []
    if not isinstance(reviewer.get("id"), str) or not reviewer.get("id"):
        found.append(refuse("the Review stamp names no reviewer"))
    elif _worked_on(subject, reviewer):
        found.append(refuse(f"reviewer {reviewer.get('id')} also worked on this task"))
    if not subject.conventions.stamps.require_hook_source:
        return found
    review = subject.conventions.tasks.review
    if reviewer.get("type") != review.agent:
        found.append(refuse(f"the reviewer's type is {reviewer.get('type')}, not {review.agent}"))
    # Reverse-engineering review runs on Opus (AGENTS.md): the same rule `task review --record` applies.
    kinds = {"orig"} if subject.changed else set()
    if any(p.startswith(tuple(review.research_paths)) for p in subject.paths):
        kinds.add("research")
    kinds &= set(review.reverse_engineering_kinds)
    model = str(reviewer.get("model", ""))
    words = review.reverse_engineering_models
    if kinds and not any(word.lower() in model.lower() for word in words):
        work = " and ".join(sorted(kinds))
        found.append(refuse(f"a review of {work} work must run on {' or '.join(words)}; this review ran on {model}"))
    return found


__all__ = [
    "ProofResult",
    "Refusal",
    "Subject",
    "TextHashes",
    "check_checked",
    "check_compared",
    "check_fails_first",
    "check_log",
    "check_reviewed",
    "dirty_paths",
    "load_subject",
    "pairs_set_sha256",
    "refusals_before_scan",
    "scanned",
    "unsettled",
    "verify",
]
