# SPDX-License-Identifier: GPL-3.0-or-later
"""The delta review: what `task review` judges of a change whose earlier review passed (review/task.md#delta).

A fix unit often changes a few lines of a pull request that has already passed its review: a clang-tidy or compiler
fix, a conflict resolved in a rebase. Reviewing the whole change again costs a full review, on Opus when the change has
`@orig` functions or research pages. When the earlier review still holds for the rest, only the **delta** is reviewed:
the files whose own change differs between the change that was reviewed and the change as it is now.

* **The earlier review** is the newest passing `reviewed` stamp of the code (scope `full` or `delta`, not `text`) made
  for another change than the current one; a failed full review in between means a full review again, and a failed
  delta review is passed over (the pass before it still holds; the new delta includes the fix of what failed).
* **Rebase noise is left out**: a file is in the delta when the branch's own removed and added lines of it (`git diff
  -U0`, without the hunk headers, as the change hash reads them, `pr_diff`) differ between the two changes. A rebase
  onto a newer main moves line numbers and context, not those lines.
* **A full review instead**, by `tasks.review.delta.full_when` in `.github/conventions.yml`: the delta changes an
  `@orig` function's body or addresses (or the pairs it was compared with), a claim line of research work (an address
  or an evidence level, `claim_patterns`), or the code of a test (a `TEST_CASE` or a Python `test_` function under
  `test_paths`). Those are what a reviewer must see against the original or against what the test proves.

The delta's checklists and model follow from the delta alone: the orig checklist only for a function that moved in the
delta, the research one only for a research file in it (without a claim change), so the reviewer needs Opus only then.

The per-file comparison here reads `git diff -U0` itself; it can use the per-file hash of `pr_diff` once that exists.
Standard library only, besides the repository's own modules.
"""

from __future__ import annotations

import ast
import hashlib
import json
import re
import subprocess
from collections import Counter
from collections.abc import Iterable, Mapping, Sequence
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from coney_tools import fails_first, orig_review, review_verdict, stamps
from coney_tools.config import ConfigError
from coney_tools.conventions import DeltaReview, TaskReview

#: The scope a delta review's stamp and packet carry.
DELTA = "delta"
#: The scopes of a review of the code (a `text` review judges the description alone).
CODE_SCOPES = ("full", DELTA)
#: The endings of a C++ file that can declare a `TEST_CASE`.
_CPP_TESTS = (".cpp", ".h", ".hpp")
#: The keys of an `@orig` function that, changed in the delta, change what was compared with the original.
_FUNCTION_IDENTITY = ("key", "body_sha256", "addresses", "xbox")
#: The header lines of a patch that say a file's mode changed, or that it was added or deleted.
_MODE_LINES = ("old mode", "new mode", "new file mode", "deleted file mode")
#: The description's hashes, as a `reviewed` stamp covers them.
TEXT_COVERS = ("title_sha256", "paragraph_sha256", "images_sha256")


@dataclass(frozen=True)
class Delta:
    """What a delta review judges: the files whose change differs since the earlier review, and what it rests on."""

    #: The passing review of the code this one stacks on.
    since: stamps.Stamp
    #: The full review at the bottom of the stack (`since` itself when it is a full one).
    full_seq: int
    #: The tip and the base the earlier review judged.
    since_tip: str
    since_base: str
    #: The files whose own change differs, sorted.
    files: tuple[str, ...]
    #: The files among them that main also changed between the two bases: their delta is read patch against patch.
    rebased: tuple[str, ...]
    #: The `@orig` functions that moved in the delta (their compare stands; the reviewer diffs the copies).
    moved: tuple[dict[str, Any], ...]
    #: Whether the description's title, paragraph or pictures changed since the earlier review.
    text_changed: bool


@dataclass(frozen=True)
class Plan:
    """Which review the change gets: a delta, or a full one and why."""

    delta: Delta | None
    #: Why the review is a full one; empty for a delta.
    reasons: tuple[str, ...] = field(default=())


# --- git -------------------------------------------------------------------------------------------------------------


def _git(worktree: Path, *args: str, check: bool = True) -> str | None:
    """Run git in `worktree`: its output, None when it fails and `check` is off, else a ConfigError."""
    done = subprocess.run(
        ["git", "-C", str(worktree), "-c", "core.quotepath=false", *args],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    if done.returncode != 0:
        if not check:
            return None
        raise ConfigError(f"git {' '.join(args)}: {done.stderr.strip() or 'failed'}")
    return done.stdout


def _names(worktree: Path, old: str, new: str) -> set[str]:
    """The paths that differ between two commits."""
    listing = _git(worktree, "diff", "--no-renames", "--name-only", "-z", old, new) or ""
    return {item for item in listing.split("\0") if item}


def _show(worktree: Path, ref: str, path: str) -> str:
    """The text of `path` at `ref`, empty when it does not exist there."""
    return _git(worktree, "show", f"{ref}:{path}", check=False) or ""


def file_change(worktree: Path, base: str, tip: str, path: str) -> str:
    """What `tip` does to `path` since `base`: its mode lines and its removed and added lines, without hunk headers,
    line numbers or blob ids, so the same edit on a moved main reads the same (the change hash's rule, `pr_diff`)."""
    patch = _git(
        worktree,
        "diff",
        "--no-renames",
        "--no-color",
        "--no-ext-diff",
        "--no-textconv",
        "--text",
        "--diff-algorithm=myers",
        "-U0",
        base,
        tip,
        "--",
        path,
    )
    kept: list[str] = []
    in_hunk = False
    for line in (patch or "").split("\n"):
        if line.startswith("@@"):
            in_hunk = True
        elif (in_hunk and line[:1] in ("+", "-", "\\")) or (not in_hunk and line.startswith(_MODE_LINES)):
            kept.append(line)
    return "\n".join(kept)


# --- the earlier review ----------------------------------------------------------------------------------------------


def earlier_review(report: stamps.Report, diff: str) -> stamps.Stamp | None:
    """The passing review of the code a delta review of the change `diff` stacks on, or None (a full review).

    A review made for this very change is passed over when it passed (a second record of the same verdict finds the same
    earlier review) or when it was a failed delta; a failed full review stops the search, since only a full review can
    follow one."""
    for stamp in reversed(report.stamps):
        if stamp.step != "reviewed" or stamp.detail.get("scope", "full") not in CODE_SCOPES:
            continue
        scope = stamp.detail.get("scope", "full")
        passed = stamp.result == "pass" and stamp.detail.get("verdict", "pass") == "pass"
        if stamp.covers.get(stamps.CHANGE) == diff and passed:
            continue
        if not passed:
            if scope == DELTA:
                continue
            return None
        return stamp
    return None


def full_seq_of(stamp: stamps.Stamp) -> int:
    """The full review at the bottom of `stamp`'s stack: its own seq for a full review."""
    seq = stamp.detail.get("full_seq") if stamp.detail.get("scope") == DELTA else None
    return seq if isinstance(seq, int) else int(stamp.seq)


def _since_base(folder: Path, stamp: stamps.Stamp) -> str | None:
    """The base the earlier review judged, from its recorded verdict (`recorded-<seq>.json`); None when unreadable."""
    try:
        data = json.loads((folder / f"recorded-{stamp.seq}.json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    base = data.get("base") if isinstance(data, dict) else None
    return base if isinstance(base, str) and base else None


# --- what makes it a full review -------------------------------------------------------------------------------------


def _identity(function: Mapping[str, Any]) -> tuple[Any, ...]:
    """What of an `@orig` function a compare and a review judged: its key, body and addresses."""
    return tuple(json.dumps(function.get(key), sort_keys=True) for key in _FUNCTION_IDENTITY)


def _orig_reasons(
    old: Sequence[Mapping[str, Any]], new: Sequence[Mapping[str, Any]], since: stamps.Stamp, pairs_set: str
) -> list[str]:
    """The `@orig` functions the delta changes (body or addresses), and pairs compared again since the review."""
    seen = {_identity(function) for function in old}
    reasons = [
        f"orig-body: the delta changes the @orig function {function.get('coney')} ({function.get('where')})"
        for function in new
        if _identity(function) not in seen
    ]
    if since.covers.get("pairs_set_sha256", pairs_set) != pairs_set:
        reasons.append("orig-body: the pairs of the @orig functions changed since the earlier review")
    return reasons


def _claims(text: str, patterns: Sequence[re.Pattern[str]]) -> Counter[str]:
    """The claim lines of a research file: each line that matches a claim pattern, with surrounding space trimmed."""
    return Counter(line.strip() for line in text.splitlines() if any(p.search(line) for p in patterns))


def _claim_reasons(
    worktree: Path, files: Iterable[str], old_tip: str, tip: str, review: TaskReview, rules: DeltaReview
) -> list[str]:
    """The research files whose claim lines (an address, an evidence level) the delta adds, removes or edits."""
    patterns = [re.compile(pattern) for pattern in rules.claim_patterns]
    return [
        f"research-claim: the delta changes a claim in {path}"
        for path in files
        if review.is_research_work(path)
        and _claims(_show(worktree, old_tip, path), patterns) != _claims(_show(worktree, tip, path), patterns)
    ]


def python_test_bodies(text: str) -> dict[str, str]:
    """Each `test_` function of a Python file (by its dotted place, a method under its class) -> its code as the
    parser reads it, so a comment or a layout change is no change; the whole text under `<file>` when it does not
    parse."""
    try:
        tree = ast.parse(text)
    except SyntaxError:
        return {"<file>": text}
    found: dict[str, str] = {}

    def visit(node: ast.AST, prefix: str) -> None:
        """Collect the test functions under `node`, naming each by the classes and functions it sits in."""
        for child in ast.iter_child_nodes(node):
            if isinstance(child, ast.ClassDef | ast.FunctionDef | ast.AsyncFunctionDef):
                name = f"{prefix}{child.name}"
                if not isinstance(child, ast.ClassDef) and child.name.startswith("test"):
                    found[name] = ast.dump(child)
                visit(child, f"{name}.")

    visit(tree, "")
    return found


def _test_bodies(path: str, text: str) -> Mapping[str, object]:
    """The tests a file declares, each -> its code: C++ `TEST_CASE`s or Python `test_` functions; none otherwise."""
    if path.endswith(_CPP_TESTS):
        return fails_first.declared_bodies(text)
    if path.endswith(".py"):
        return python_test_bodies(text)
    return {}


def _test_reasons(worktree: Path, files: Iterable[str], old_tip: str, tip: str, rules: DeltaReview) -> list[str]:
    """The tests whose code the delta changes, adds or removes, in the files under the test paths."""
    reasons: list[str] = []
    for path in files:
        if not path.startswith(rules.test_paths):
            continue
        before = _test_bodies(path, _show(worktree, old_tip, path))
        after = _test_bodies(path, _show(worktree, tip, path))
        changed = sorted(name for name in set(before) | set(after) if before.get(name) != after.get(name))
        reasons += [f"test-body: the delta changes the test {name} in {path}" for name in changed]
    return reasons


# --- the plan --------------------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class Now:
    """The change as it is now, as `task review` has read it."""

    base: str
    tip: str
    diff: str
    functions: Sequence[Mapping[str, Any]]
    moved: Sequence[Mapping[str, Any]]
    pairs_set: str
    #: The description's hashes, by `TEXT_COVERS` key.
    text: Mapping[str, str]


def plan(worktree: Path, report: stamps.Report, now: Now, base_ref: str, review: TaskReview, folder: Path) -> Plan:
    """Whether the change gets a delta review, and of what; or why it gets a full one. `folder` is the review's
    folder, where the earlier review's verdict was kept."""
    rules = review.delta
    if rules is None:
        return Plan(None, ("the conventions have no tasks.review.delta",))
    since = earlier_review(report, now.diff)
    if since is None:
        return Plan(None, ("no earlier passing review of the code to stack on",))
    since_tip = str(since.commit)
    if _git(worktree, "cat-file", "-e", f"{since_tip}^{{commit}}", check=False) is None:
        return Plan(None, (f"the commit the earlier review judged ({since_tip[:12]}) is not in this repository",))
    since_base = _since_base(folder, since) or (_git(worktree, "merge-base", base_ref, since_tip) or "").strip()
    old_files = _names(worktree, since_base, since_tip)
    new_files = _names(worktree, now.base, now.tip)
    main_moved = _names(worktree, since_base, now.base)
    # A file the same at both tips and at both bases is changed the same way by both; only the rest is compared.
    candidates = (_names(worktree, since_tip, now.tip) | main_moved) & (old_files | new_files)
    files = tuple(
        sorted(
            path
            for path in candidates
            if file_change(worktree, since_base, since_tip, path) != file_change(worktree, now.base, now.tip, path)
        )
    )
    old = orig_review.pr_stamp(worktree, since_tip, since_base) if old_files else {"functions": [], "moved": []}
    reasons: list[str] = []
    if "orig-body" in rules.full_when:
        reasons += _orig_reasons(old["functions"], now.functions, since, now.pairs_set)
    if "research-claim" in rules.full_when:
        reasons += _claim_reasons(worktree, files, since_tip, now.tip, review, rules)
    if "test-body" in rules.full_when:
        reasons += _test_reasons(worktree, files, since_tip, now.tip, rules)
    if reasons:
        return Plan(None, tuple(reasons))
    kept = {_identity(function) for function in [*old["moved"], *old["functions"]]}
    moved = tuple(dict(function) for function in now.moved if _identity(function) not in kept)
    return Plan(
        Delta(
            since=since,
            full_seq=full_seq_of(since),
            since_tip=since_tip,
            since_base=since_base,
            files=files,
            rebased=tuple(path for path in files if path in main_moved),
            moved=moved,
            text_changed=any(since.covers.get(key) != now.text.get(key) for key in TEXT_COVERS),
        )
    )


def diff_text(worktree: Path, delta: Delta, base: str, tip: str) -> str:
    """What the reviewer reads of the delta: for each file, the diff from the reviewed tip to the new one, or, for a
    file main also changed, the branch's patch of it before and now (a plain diff would show main's edits too)."""
    out = [
        f"# Delta since the review of stamp #{delta.since.seq} ({delta.since_tip[:12]}, base {delta.since_base[:12]})",
        f"# now {tip[:12]}, base {base[:12]}; {len(delta.files)} file(s)",
        "",
    ]
    for path in delta.files:
        if path in delta.rebased:
            out.append(f"## {path}: main changed it too; the branch's patch then and now")
            out.append("### then")
            out.append(
                _git(worktree, "diff", "--no-color", "--no-ext-diff", delta.since_base, delta.since_tip, "--", path)
                or ""
            )
            out.append("### now")
            out.append(_git(worktree, "diff", "--no-color", "--no-ext-diff", base, tip, "--", path) or "")
        else:
            out.append(f"## {path}")
            out.append(_git(worktree, "diff", "--no-color", "--no-ext-diff", delta.since_tip, tip, "--", path) or "")
    return "\n".join(out)


def packet_entry(delta: Delta, diff_file: Path) -> dict[str, Any]:
    """The packet's `delta` object: what the review stacks on and where the reviewer reads the delta."""
    return {
        "since_seq": delta.since.seq,
        "full_seq": delta.full_seq,
        "since_tip": delta.since_tip,
        "since_base": delta.since_base,
        "since_diff_sha256": delta.since.covers.get(stamps.CHANGE, ""),
        "files": list(delta.files),
        "rebased": list(delta.rebased),
        "text_changed": delta.text_changed,
        "diff_file": str(diff_file),
    }


def _recorded_functions(folder: Path, stamp: stamps.Stamp) -> Mapping[str, Any] | None:
    """The address -> class map of a review whose stamp left its function list to the verdict kept beside the packet
    (`recorded-<seq>.json`), when that file is the one the stamp's `review_sha256` covers."""
    try:
        raw = (folder / f"recorded-{stamp.seq}.json").read_bytes()
        data = json.loads(raw)
    except (OSError, ValueError):
        return None
    if hashlib.sha256(raw).hexdigest() != stamp.covers.get("review_sha256") or not isinstance(data, dict):
        return None
    listed = data.get("functions")
    if not isinstance(listed, list):
        return None
    return {
        str(address): review_verdict.CLASS_LETTERS.get(str(item.get("class")), item.get("class"))
        for item in listed
        if isinstance(item, dict)
        for address in item.get("addresses", [])
    }


def stamp_functions(folder: Path, stamp: stamps.Stamp) -> Mapping[str, Any] | None:
    """A review stamp's address -> class map: inline, or from its recorded verdict; None when neither has it."""
    inline = stamp.detail.get("functions")
    if isinstance(inline, dict):
        return inline
    return _recorded_functions(folder, stamp)


__all__ = [
    "CODE_SCOPES",
    "DELTA",
    "TEXT_COVERS",
    "Delta",
    "Now",
    "Plan",
    "diff_text",
    "earlier_review",
    "file_change",
    "full_seq_of",
    "packet_entry",
    "plan",
    "python_test_bodies",
    "stamp_functions",
]
