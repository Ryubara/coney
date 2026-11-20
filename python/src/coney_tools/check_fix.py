# SPDX-License-Identifier: GPL-3.0-or-later
"""The fix profile of `coney-tools task check` (`proof.fix`, docs/guides/task-check-fix.md#fix).

A fix unit changes a line or two of a pull request whose full Check already passed, and the full Check of the whole
change ran everything again: about 4200 C++ tests with the disc for a branch that touched a CMake file, 25 to 45
minutes on a loaded machine. So when the task's log holds a passing Check of an earlier change of the branch (the
*anchor*: a `checked` stamp that passed, was not itself a fix, and ran a profile at least as big as the one the change
needs now), the Check runs the same checks but scopes the Python and C++ tests to the files the branch changed since
the anchor. Those are the paths whose own part of the change hash differs (`pr_diff.file_hashes`), so a rebase that
leaves the branch's lines as they were changes none, and a resolved conflict is a change.

The full Check runs instead when what changed since is too wide or cannot be traced (`why_full`): more than
`max_files` files, a path matching `full_when` (the build, presets, test fixtures and helpers), a header that more
than `max_header_includers` C++ files include, or C++ tests that come out as the whole suite anyway. The anchor must
reproduce: its commit must still be readable and give the change hash its stamp covers.

`task check` and `pr verify` both decide with `assess`, from the same stamp log and the same commits, so a fix stamp
`pr verify` accepts is one `task check` would write again. Standard library only, besides git through pr_diff and
ctest_scope.
"""

from __future__ import annotations

import fnmatch
import hashlib
import json
from collections import defaultdict
from collections.abc import Iterable, Mapping
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from coney_tools import ctest_scope, pr_diff, proof_profiles, stamps
from coney_tools.config import ConfigError
from coney_tools.conventions import Conventions, ConventionsError

#: The key of a fix stamp's detail (`record`): which Check it stands on and what changed since.
DETAIL = "fix"
#: How many changed paths a message names.
SHOWN = 5


@dataclass(frozen=True)
class FixPlan:
    """What `assess` decided: the anchor it stands on, the files changed since, and either the scoped tests of the
    fix profile or why the full Check runs instead (`why_full`, "" when the fix profile is used)."""

    anchor: stamps.Stamp
    paths: tuple[str, ...]
    why_full: str = ""
    #: The C++ tests the fix runs; None when the profile has no build or the full Check runs.
    ctest: ctest_scope.CtestScope | None = None

    @property
    def used(self) -> bool:
        """Whether the fix profile runs (not the full Check)."""
        return not self.why_full

    @property
    def text(self) -> str:
        """The decision in words, for `task check`'s output."""
        on = f"Check #{self.anchor.seq} (change {str(self.anchor.covers.get(stamps.CHANGE, ''))[:12]})"
        if not self.used:
            return f"not used: {self.why_full}; the full check runs (an earlier passing {on})"
        count = len(self.paths)
        shown = ", ".join(self.paths[:SHOWN]) + (" ..." if count > SHOWN else "")
        return f"on {on}: {count} file{'s' if count != 1 else ''} changed since ({shown}); the tests are theirs"


def is_fix(stamp: stamps.Stamp) -> bool:
    """Whether a `checked` stamp is a fix profile's (its detail names the Check it stands on)."""
    detail = stamp.detail if isinstance(stamp.detail, dict) else {}
    return DETAIL in detail


def can_anchor(stamp: stamps.Stamp, conventions: Conventions, needed: str, current: str) -> str:
    """Why `stamp` cannot be the anchor of a fix of the change `current` that needs the profile `needed`, or "": it
    must be a passing `checked` stamp of another change, not a fix itself, of a profile at least `needed`."""
    if stamp.step != "checked" or stamp.result != "pass":
        return f"stamp #{stamp.seq} is not a passing Check"
    if is_fix(stamp):
        return f"Check #{stamp.seq} is a fix itself"
    if stamp.covers.get(stamps.CHANGE) == current:
        return f"Check #{stamp.seq} is of this change"
    ran = str(stamp.detail.get("profile"))
    if ran not in [profile.name for profile in conventions.proof.profiles]:
        return f"Check #{stamp.seq} ran profile {ran!r}, which is no profile"
    if not proof_profiles.at_least(ran, needed, conventions):
        return f"Check #{stamp.seq} ran profile {ran}, this change needs {needed}"
    return ""


def find_anchor(report: stamps.Report, conventions: Conventions, needed: str, current: str) -> stamps.Stamp | None:
    """The newest stamp of the log that `can_anchor` a fix of the change `current`, or None."""
    for stamp in reversed(report.stamps):
        if stamp.step == "checked" and not can_anchor(stamp, conventions, needed, current):
            return stamp
    return None


def assess(
    root: Path,
    conventions: Conventions,
    target: str,
    change: pr_diff.Change,
    anchor: stamps.Stamp,
    tree: Mapping[str, str],
    *,
    needs_cpp: bool,
) -> FixPlan:
    """Whether the change can be checked as a fix on `anchor`, and with which tests: the files changed since (each
    file's own change against its merge base with `target`), the full Check's reason when one holds (`why_full`), and
    the C++ tests of what changed when the profile builds (`needs_cpp`). `tree` is the C++ files at the change's tip
    (`ctest_scope.tree_at`)."""
    expected = str(anchor.covers.get(stamps.CHANGE, ""))
    try:
        again = pr_diff.change_of(root, target, str(anchor.commit))
    except ConfigError as error:
        return FixPlan(anchor, (), f"the commit of Check #{anchor.seq} cannot be read ({error})")
    if again.sha256 != expected:
        return FixPlan(anchor, (), f"the commit of Check #{anchor.seq} no longer gives the change it checked")
    since = pr_diff.changed_between(
        pr_diff.file_hashes(root, target, again.tip), pr_diff.file_hashes(root, target, change.tip)
    )
    why = why_full(conventions, since, tree)
    if why:
        return FixPlan(anchor, since, why)
    if not needs_cpp:
        return FixPlan(anchor, since)
    tests = fix_ctest_scope(conventions, since, change.paths, tree)
    if tests.whole:
        return FixPlan(anchor, since, f"the C++ tests of what changed are the whole suite ({tests.culprit})")
    return FixPlan(anchor, since, ctest=tests)


def why_full(conventions: Conventions, since: Iterable[str], tree: Mapping[str, str]) -> str:
    """Why the files changed since the anchor need the full Check (`proof.fix`), or "": too many of them, one the
    build or the test fixtures read, or a header included too widely."""
    rules = conventions.proof.fix
    paths = list(since)
    if len(paths) > rules.max_files:
        return f"{len(paths)} files changed since, more than {rules.max_files}"
    for path in paths:
        for pattern in rules.full_when:
            if fnmatch.fnmatchcase(path, pattern):
                return f"{path} changed (proof.fix.full_when: {pattern})"
    headers = conventions.ci.tidy.headers
    wide = [path for path in paths if path.endswith(headers) and path in tree]
    if wide:
        counted = includers(tree, conventions.ci.tidy.folders, wide)
        for path in wide:
            if counted[path] > rules.max_header_includers:
                return (
                    f"{path} is included by {counted[path]} C++ files, more than {rules.max_header_includers} "
                    "(proof.fix.max_header_includers)"
                )
    return ""


def includers(tree: Mapping[str, str], folders: tuple[str, ...], headers: Iterable[str]) -> dict[str, int]:
    """Each of `headers` -> how many other files of `tree` include it, directly or through other headers (only the
    includes: what a change to it recompiles, not what its implementing sources reach)."""
    included_by: dict[str, set[str]] = defaultdict(set)
    for path, targets in ctest_scope.includes(tree, folders).items():
        for target in targets:
            included_by[target].add(path)
    counted: dict[str, int] = {}
    for header in headers:
        found: set[str] = set()
        stack = [header]
        while stack:
            for user in included_by.get(stack.pop(), ()):
                if user not in found and user != header:
                    found.add(user)
                    stack.append(user)
        counted[header] = len(found)
    return counted


def fix_ctest_scope(
    conventions: Conventions, since: Iterable[str], paths: Iterable[str], tree: Mapping[str, str]
) -> ctest_scope.CtestScope:
    """The C++ tests of a fix: those the files changed since reach (`ctest_scope`); when none of them is a C++ file,
    the branch's own test files (`paths`, the whole change), so the build is still run against the tests it wrote."""
    tidy = conventions.ci.tidy
    changed = list(since)
    if any(path.startswith(tidy.folders) and path.endswith((*tidy.sources, *tidy.headers)) for path in changed):
        return ctest_scope.ctest_scope(conventions, changed, tree)
    suffix = conventions.proof.ctest.test_suffix
    own = [path for path in paths if path.endswith(suffix) and path in tree]
    if not own:
        return ctest_scope.CtestScope(True, culprit="nothing changed since is C++ and the branch has no test file")
    return ctest_scope.ctest_scope(conventions, own, tree)


# --- what the stamp records ------------------------------------------------------------------------------------------


def record(plan: FixPlan) -> dict[str, Any]:
    """The `fix` detail of a fix stamp: the anchor's sequence number, the hash of its stamp and of its change, and the
    count and hash of the files changed since (the list itself is recomputed by `pr verify`)."""
    return {
        "on": plan.anchor.seq,
        "on_stamp": plan.anchor.hash,
        "on_change": plan.anchor.covers.get(stamps.CHANGE),
        "files": len(plan.paths),
        "sha256": paths_sha256(plan.paths),
    }


def paths_sha256(paths: Iterable[str]) -> str:
    """The SHA-256 of the sorted, distinct paths as canonical JSON (what `record` holds)."""
    data = json.dumps(sorted(set(paths)), separators=(",", ":"), ensure_ascii=True).encode("utf-8")
    return hashlib.sha256(data).hexdigest()


def anchor_of(report: stamps.Report, detail: object) -> stamps.Stamp:
    """The stamp a fix stamp's `fix` detail names, checked against the hash it holds; ConventionsError when the detail
    is not in that form or names no stamp of the log."""
    if not isinstance(detail, dict) or not isinstance(detail.get("on"), int):
        raise ConventionsError(f"checked stamp: fix is {detail!r}, not the record of the Check it stands on")
    for stamp in report.stamps:
        if stamp.seq == detail["on"]:
            if stamp.hash != detail.get("on_stamp"):
                raise ConventionsError(f"checked stamp: fix names stamp #{stamp.seq}, whose hash differs")
            return stamp
    raise ConventionsError(f"checked stamp: fix names stamp #{detail['on']}, which is not in the log")


def matches(plan: FixPlan, detail: Mapping[str, Any]) -> bool:
    """Whether a fix stamp's `fix` detail holds the files `plan` recomputed as changed since its anchor."""
    return detail.get("files") == len(plan.paths) and detail.get("sha256") == paths_sha256(plan.paths)
