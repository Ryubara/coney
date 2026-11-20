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
  or an evidence level, `claim_patterns`), or the code of any file under `test_paths` (a test, a helper, a fixture, a
  conftest, a data file). Those are what a reviewer must see against the original or against what the test proves.

The delta's checklists and model follow from the delta alone: the orig checklist for an `@orig` function whose comments,
tag or declaration the delta edits (its body hash leaves comments out, but a comment at a difference is what makes the
difference deliberate) and for one that moved in the delta, the research one for a research file in it (without a
claim change), so the reviewer needs Opus only then; the description is read with every delta.

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
from coney_tools.orig_index import SOURCE_SUFFIXES
from coney_tools.orig_scan import Function, read_source

#: The scope a delta review's stamp and packet carry.
DELTA = "delta"
#: The scopes of a review of the code (a `text` review judges the description alone).
CODE_SCOPES = ("full", DELTA)
#: The endings of a C++ file under the test paths, read as code (comments and layout are no change).
_CPP_TESTS = (".cpp", ".h", ".hpp", ".inl")
#: How many lines a separate declaration may run over, up to its `;`.
_DECLARATION_LINES = 8
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
    #: The `@orig` functions whose body and addresses stand but whose comments, tag or declaration the delta edits:
    #: judged again under the orig checklist, since a comment at a difference is what makes it deliberate.
    functions: tuple[dict[str, Any], ...]
    #: Whether the description's title, paragraph or pictures changed since the earlier review: said to the reviewer,
    #: who reads the description with every delta.
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


def _recorded(folder: Path, stamp: stamps.Stamp) -> dict[str, Any] | None:
    """The verdict a review stamp recorded (`recorded-<seq>.json`), only when it is the file the stamp's
    `review_sha256` covers; None otherwise."""
    try:
        raw = (folder / f"recorded-{stamp.seq}.json").read_bytes()
        data = json.loads(raw)
    except (OSError, ValueError):
        return None
    if hashlib.sha256(raw).hexdigest() != stamp.covers.get("review_sha256") or not isinstance(data, dict):
        return None
    return data


def _since_base(folder: Path, stamp: stamps.Stamp) -> str | None:
    """The base the earlier review judged, from the verdict its stamp covers; None when there is none (the caller then
    takes the merge base)."""
    data = _recorded(folder, stamp)
    base = data.get("base") if data is not None else None
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


def _span(lines: Sequence[str], function: Function) -> str:
    """A function's whole text as written: its comment block, then its definition through the closing brace, or its
    declaration through the `;`; trailing space dropped, comments kept."""
    if function.body:
        statement = lines[function.line - 1 : function.line - 1 + function.lines]
    else:
        statement = []
        for line in lines[function.line - 1 : function.line - 1 + _DECLARATION_LINES]:
            statement.append(line)
            if ";" in line:
                break
    return "\n".join([function.comment, *(line.rstrip() for line in statement)])


def _orig_spans(worktree: Path, ref: str, files: Iterable[str], names: set[str]) -> dict[str, list[str]]:
    """Each named function -> the texts of its definitions and declarations in `files` at `ref`."""
    found: dict[str, list[str]] = {}
    for path in files:
        if not path.endswith(tuple(SOURCE_SUFFIXES)):
            continue
        text = _show(worktree, ref, path)
        lines = text.splitlines()
        for function in read_source(path, text).functions:
            if function.qualified in names:
                found.setdefault(function.qualified, []).append(f"{path}\n{_span(lines, function)}")
    return {name: sorted(texts) for name, texts in found.items()}


def touched_functions(
    worktree: Path, files: Sequence[str], old_tip: str, tip: str, functions: Sequence[Mapping[str, Any]]
) -> tuple[dict[str, Any], ...]:
    """The changed `@orig` functions of the change whose text the delta edits (a comment inside, the tag, the separate
    declaration or its doc comment), though their body hash and addresses stand."""
    names = {str(function["coney"]) for function in functions}
    before = _orig_spans(worktree, old_tip, files, names)
    after = _orig_spans(worktree, tip, files, names)
    edited = {name for name in names if before.get(name) != after.get(name)}
    return tuple(dict(function) for function in functions if function["coney"] in edited)


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
    parse. Names the tests a changed file holds."""
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


def test_code(path: str, text: str) -> str:
    """What a file under the test paths says as code: a C++ file without comments and layout
    (`orig_review.code_text`), a Python file as the parser reads it (the whole text when it does not parse), any other
    file (a fixture's data) as it is."""
    if path.endswith(_CPP_TESTS):
        return orig_review.code_text(text)
    if path.endswith(".py"):
        try:
            return ast.dump(ast.parse(text))
        except SyntaxError:
            return text
    return text


def _named_tests(path: str, text: str) -> Mapping[str, object]:
    """The tests a file declares, each -> its code, to name the ones a delta changes: C++ `TEST_CASE`s or Python
    `test_` functions; none for another file."""
    if path.endswith(_CPP_TESTS):
        return fails_first.declared_bodies(text)
    if path.endswith(".py"):
        return python_test_bodies(text)
    return {}


def _test_reasons(worktree: Path, files: Iterable[str], old_tip: str, tip: str, rules: DeltaReview) -> list[str]:
    """The files under the test paths whose code the delta changes: a test, a helper, a fixture, a conftest, a
    parametrize table or a data file the tests read all change what the tests prove."""
    reasons: list[str] = []
    for path in files:
        if not path.startswith(rules.test_paths):
            continue
        before, after = _show(worktree, old_tip, path), _show(worktree, tip, path)
        if test_code(path, before) == test_code(path, after):
            continue
        tests, old_tests = _named_tests(path, after), _named_tests(path, before)
        named = sorted(name for name in set(tests) | set(old_tests) if tests.get(name) != old_tests.get(name))
        what = f"the test{'s' if len(named) > 1 else ''} {', '.join(named)}" if named else "the test code"
        reasons.append(f"test-body: the delta changes {what} in {path}")
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
    touched = touched_functions(worktree, files, since_tip, now.tip, now.functions)
    return Plan(
        Delta(
            since=since,
            full_seq=full_seq_of(since),
            since_tip=since_tip,
            since_base=since_base,
            files=files,
            rebased=tuple(path for path in files if path in main_moved),
            moved=moved,
            functions=touched,
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


def _recorded_functions(folder: Path, stamp: stamps.Stamp) -> dict[str, Any] | None:
    """The address -> class letter map of the verdict a review stamp recorded (`recorded-<seq>.json`), when that file
    is the one the stamp's `review_sha256` covers."""
    data = _recorded(folder, stamp)
    listed = data.get("functions") if data is not None else None
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


def below(report: stamps.Report, stamp: stamps.Stamp) -> stamps.Stamp | None:
    """The review a delta or a text review rests on (`since_seq`, else `full_seq`), or None for a full review or a
    stamp that names none."""
    if stamp.detail.get("scope", "full") == "full":
        return None
    seq = stamp.detail.get("since_seq", stamp.detail.get("full_seq"))
    if not isinstance(seq, int) or seq >= stamp.seq:
        return None
    return next((s for s in report.stamps if s.seq == seq and s.step == "reviewed"), None)


def stack_functions(folder: Path, report: stamps.Report, stamp: stamps.Stamp) -> dict[str, Any] | None:
    """The address -> class map a review's stack says: the newest stamp's own list when it has one (a delta carries
    the list below it, updated with what it judged), else the list below it updated with each delta's own judgement,
    read from the verdicts kept by seq. None when no list can be read."""
    inline = stamp.detail.get("functions")
    if isinstance(inline, dict):
        return dict(inline)
    under = below(report, stamp)
    if under is None:
        recorded = _recorded_functions(folder, stamp)
        return None if recorded is None else dict(recorded)
    found = stack_functions(folder, report, under)
    if found is None:
        return None
    if stamp.detail.get("scope") == DELTA:
        found.update(_recorded_functions(folder, stamp) or {})
    return found


__all__ = [
    "CODE_SCOPES",
    "DELTA",
    "TEXT_COVERS",
    "Delta",
    "Now",
    "Plan",
    "below",
    "diff_text",
    "earlier_review",
    "file_change",
    "full_seq_of",
    "packet_entry",
    "plan",
    "python_test_bodies",
    "stack_functions",
    "stamp_functions",
    "test_code",
    "touched_functions",
]
