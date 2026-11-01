# SPDX-License-Identifier: GPL-3.0-or-later
"""The orig review: Coney's `@orig` functions checked against the original, and the record of what was checked.

The reviewer agent (.claude/agents/reviewer.md) compares a Coney function with the original function its
`@orig` tag names, on fidelity and on own expression (the rubric: docs/guides/review/orig.md). This module gives it
what to review and holds its results:

* a **unit** is one Coney function definition with a body whose `@orig` addresses come from its own comment, a
  comment in its body or its declaration's comment; its body hash (of the code only: comments and white space are
  left out) says whether it changed since a review; its **moved hash** is the same with its own qualified name left
  out too, which says a function that changed class, name or file kept its code (`moves`);
* `pr_stamp` lists the units a branch adds or changes (`orig review-stamp --branch`) and, apart, those it only moved
  (their review and compare stand), `sweep_stamp` the units of a
  folder or of chosen addresses not yet reviewed at their current body (`orig review-stamp --folder`);
* `verdict_problems` and `gate` are the release gate on a branch's `ORIG-REVIEW.json` (`orig check-review`, run by
  `.claude/hooks/pr_guard.py` before `gh pr create`);
* the **ledger** (a JSON-lines file in the workspace's scratch, never committed) records each review: the unit, its
  body hash, the commit and the class. A unit with no entry is unreviewed, one whose body changed since is stale,
  and `orig legacy-queue` orders by it. A passing gate and `orig record-findings` (a sweep's FINDINGS.tsv) write to it.

The source is read from commits (`git cat-file --batch`), never from a working tree, so a stamp names exactly what a
push would send.
"""

from __future__ import annotations

import csv
import datetime
import functools
import hashlib
import json
import re
import subprocess
from collections import defaultdict
from collections.abc import Callable, Collection, Iterable, Mapping
from dataclasses import dataclass, field, replace
from pathlib import Path, PurePosixPath
from typing import Any

from coney_tools import stamps
from coney_tools.config import ConfigError, workspace_folder
from coney_tools.conventions import load_conventions_leniently
from coney_tools.orig_index import SOURCE_DIR, SOURCE_SUFFIXES
from coney_tools.orig_scan import PS2, XBOX, Function, Tag, read_source

#: The classes a reviewed function gets, as docs/guides/review/orig.md defines them.
CLASSES = (
    "clean",
    "diff-deliberate-ok",
    "diff-uncommented",
    "diff-bug",
    "transcription",
    "wrong-address",
    "unclear",
)
#: The classes a pull request may merge with (`unclear` is a note: the runtime comparison settles it).
PASSING = frozenset({"clean", "diff-deliberate-ok", "unclear"})
#: The classes that take a function off the audit queue until it changes.
DONE = frozenset({"clean", "diff-deliberate-ok"})
#: The verdict's keys (exactly these; docs/guides/review/orig.md#verdict).
VERDICT_KEYS = (
    "verdict",
    "tip",
    "base",
    "reviewed",
    "findings",
    "rubric_version",
    "functions",
    "sampled",
    "not_reviewed",
)


class CannotTell(ConfigError):
    """The gate cannot tell which branch or verdict it is about; the hook then lets the command through."""


@dataclass(frozen=True)
class Unit:
    """One `@orig` function definition: what a review covers."""

    key: str  # "Brain::think(int)"; a file's private helper is prefixed with its path
    path: str
    line: int  # the definition's first line
    coney: str  # "Brain::think"
    addresses: tuple[int, ...]  # in `SLUS_212.15`: what `orig study` and `orig compare` read
    names: tuple[str, ...]  # the original's names, as the tags give them
    body_sha256: str
    #: The hash the body had before comments and white space were left out of it: to read what was recorded then.
    legacy_sha256: str = field(default="", compare=False, repr=False)
    #: Addresses in the Xbox port's `default.xbe` (`@orig xbox`): the reviewer reads them by hand, no stamp covers them.
    xbox: tuple[int, ...] = ()
    #: The hash of the code with the function's own qualifier and name left out (`_moved_hash`): equal for a function
    #: whose code is as it was and whose class, name or file changed.
    moved_sha256: str = field(default="", compare=False, repr=False)

    @property
    def where(self) -> str:
        """`path:line`."""
        return f"{self.path}:{self.line}"

    @property
    def cited(self) -> tuple[tuple[int, ...], tuple[int, ...]]:
        """The PS2 and the Xbox addresses: a change of either is a change of the unit."""
        return (self.addresses, self.xbox)

    @property
    def review_addresses(self) -> list[str]:
        """What a review covers: the PS2 addresses as `0x00100010`, the Xbox ones as `xbox 0x000bd3c0`."""
        return [f"0x{a:08x}" for a in self.addresses] + [f"xbox 0x{a:08x}" for a in self.xbox]

    def to_json(self) -> dict[str, Any]:
        """The unit as a stamp lists it; `xbox` only when it has Xbox addresses, so older stamps read the same."""
        found: dict[str, Any] = {
            "key": self.key,
            "where": self.where,
            "coney": self.coney,
            "addresses": [f"0x{a:08x}" for a in self.addresses],
            "names": list(self.names),
            "body_sha256": self.body_sha256,
        }
        if self.xbox:
            found["xbox"] = [f"0x{a:08x}" for a in self.xbox]
        return found


# --- reading the source at a commit -------------------------------------------------------------------------------


def _git(root: Path, *args: str, stdin: bytes | None = None) -> bytes:
    """Run git in `root`; its stdout, or a ConfigError with its message."""
    done = subprocess.run(["git", "-C", str(root), *args], input=stdin, capture_output=True, check=False)
    if done.returncode != 0:
        raise ConfigError(f"git {' '.join(args)}: {done.stderr.decode('utf-8', 'replace').strip() or 'failed'}")
    return done.stdout


def resolve(root: Path, ref: str) -> str:
    """The commit `ref` names."""
    return _git(root, "rev-parse", "--verify", f"{ref}^{{commit}}").decode().strip()


def files_at(root: Path, commit: str) -> dict[str, str]:
    """Every C++ file under `src/` at `commit` -> its text."""
    listing = _git(root, "ls-tree", "-r", "-z", "--name-only", commit, "--", SOURCE_DIR.as_posix()).decode()
    paths = [p for p in listing.split("\0") if p and PurePosixPath(p).suffix in SOURCE_SUFFIXES]
    if not paths:
        return {}
    out = _git(root, "cat-file", "--batch", stdin="".join(f"{commit}:{p}\n" for p in paths).encode())
    texts: dict[str, str] = {}
    offset = 0
    for path in paths:
        header_end = out.index(b"\n", offset)
        size = int(out[offset:header_end].split()[2])
        start = header_end + 1
        texts[path] = out[start : start + size].decode("utf-8", "replace")
        offset = start + size + 1
    return texts


#: The token classes, tried in this order at every position: a comment (a `//` one goes on over a backslash and a
#: newline), a raw string, a string, a number (it may hold digit separators: `48'000`), a character literal, a word,
#: white space, any other character. A literal is matched whole, so a `//` or a quote inside one is code.
_TOKEN = re.compile(
    r"""
    (?P<comment>//(?:\\\r?\n|[^\n])*|/\*.*?(?:\*/|\Z))
    |(?P<raw>(?:u8|u|U|L)?R"(?P<delim>[^ ()\\\t\v\f\r\n"]{0,16})\(.*?\)(?P=delim)")
    |(?P<string>(?:u8|u|U|L)?"(?:\\.|[^"\\\n])*")
    |(?P<number>\.?\d(?:[eEpP][+-]|['\w.])*)
    |(?P<char>(?:u8|u|U|L)?'(?:\\.|[^'\\\n])*')
    |(?P<word>[A-Za-z_]\w*)
    |(?P<space>\s+)
    |(?P<other>.)
    """,
    re.VERBOSE | re.DOTALL,
)
#: The characters that would run into a neighbour if nothing stood between them (`a ++b` is not `a + +b`).
_OPERATOR_CHARS = frozenset("+-*/%&|^<>=!:.~?#")


def _glues(before: str, after: str) -> bool:
    """Whether a space between these two characters is code: two word characters, or two operator characters, would
    merge into one token without it."""

    def word(char: str) -> bool:
        return char.isalnum() or char == "_"

    return (word(before) and word(after)) or (before in _OPERATOR_CHARS and after in _OPERATOR_CHARS)


def code_text(text: str) -> str:
    """`text` as code only: each comment and run of white space is gone, except one space where two tokens would
    otherwise run together, and every string, raw string and character literal is kept whole (a `//` inside a string
    is code). Two texts a compiler reads the same way, up to comments and layout, give one `code_text`."""
    out: list[str] = []
    pending = False
    for match in _TOKEN.finditer(text):
        if match.lastgroup in ("comment", "space"):
            pending = True
            continue
        token = match.group(0)
        if pending and out and _glues(out[-1][-1], token[0]):
            out.append(" ")
        pending = False
        out.append(token)
    return "".join(out)


def _body_hash(lines: list[str], function: Function) -> str:
    """The SHA-256 of a definition's code (`code_text` of its lines): what says it changed. An edit of only comments
    or white space leaves it as it was (issue 859)."""
    body = code_text("\n".join(lines[function.line - 1 : function.line - 1 + function.lines]))
    return hashlib.sha256(body.encode("utf-8")).hexdigest()


def _legacy_body_hash(lines: list[str], function: Function) -> str:
    """The SHA-256 of a definition's lines, trailing blanks dropped: the hash before comments and white space were
    left out. Kept to read what was recorded then (`recorded_body_is_current`, `status_of`)."""
    body = "\n".join(line.rstrip() for line in lines[function.line - 1 : function.line - 1 + function.lines])
    return hashlib.sha256(body.encode("utf-8")).hexdigest()


#: What stands in the moved hash for a function's own name.
_NAME_SLOT = "\x01"


def _moved_hash(lines: list[str], function: Function) -> str:
    """The SHA-256 of a definition's code without its own qualified name: every `Scope::` that leads to the function's
    class (`ns::Mind::`, `Mind::`) is dropped wherever it stands, and the function's name is dropped where it first
    stands (its declarator), so a function that moved to another class, a renamed class, another name or another file
    keeps the hash while its code does not change. A name the code uses for something else (`Other::kStep`, a call)
    still counts, so a moved body that now reads other things has another hash."""
    text = code_text("\n".join(lines[function.line - 1 : function.line - 1 + function.lines]))
    if function.scope:
        owner = re.escape(function.scope.rsplit("::", 1)[-1])
        text = re.sub(rf"(?<![\w:])(?:\w+::)*{owner}::", "", text)
    name = function.name.lstrip("~")
    word = rf"\b{re.escape(name)}\b" if re.fullmatch(r"\w+", name) else re.escape(name)
    text = re.sub(word, _NAME_SLOT, text, count=1)
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def _stem(path: str) -> tuple[str, str]:
    """(folder, file stem): what a header and its source file share."""
    pure = PurePosixPath(path)
    return (pure.parent.as_posix(), pure.stem)


def units_from(texts: dict[str, str]) -> list[Unit]:
    """The units of these files: each definition with a body that a tag covers (its own, its body's or its
    declaration's, found in the same file pair, then the same folder, then for a member anywhere)."""
    sources = {path: read_source(path, text) for path, text in sorted(texts.items())}
    declared: dict[tuple[str, str], list[tuple[Function, list[Tag]]]] = defaultdict(list)
    for source in sources.values():
        for function in source.functions:
            if function.tags:
                declared[function.key].append((function, function.tags))
    found: list[Unit] = []
    for path, source in sources.items():
        lines = texts[path].splitlines()
        for function in source.functions:
            if not function.body:
                continue
            tags = function.tags + function.body_tags or _declared_tags(function, declared.get(function.key, []))
            if not tags:
                continue
            key = f"{function.qualified}({function.params})"
            if function.helper:
                key = f"{path}::{key}"
            found.append(
                Unit(
                    key,
                    path,
                    function.line,
                    function.qualified,
                    tuple(dict.fromkeys(t.address for t in tags if t.build == PS2)),
                    tuple(dict.fromkeys(t.name for t in tags)),
                    _body_hash(lines, function),
                    _legacy_body_hash(lines, function),
                    tuple(dict.fromkeys(t.address for t in tags if t.build == XBOX)),
                    _moved_hash(lines, function),
                )
            )
    # Two definitions with one key (a platform variant of a free function) are told apart by their file.
    counts: dict[str, int] = defaultdict(int)
    for unit in found:
        counts[unit.key] += 1
    return [
        replace(u, key=f"{u.path}::{u.key}") if counts[u.key] > 1 and not u.key.startswith(u.path) else u for u in found
    ]


def _declared_tags(function: Function, candidates: list[tuple[Function, list[Tag]]]) -> list[Tag]:
    """The tags of `function`'s declaration among `candidates` (same scope and name), nearest first."""
    others = [(f, tags) for f, tags in candidates if (f.path, f.line) != (function.path, function.line)]
    if function.helper:
        others = [(f, tags) for f, tags in others if f.path == function.path]
    stem = _stem(function.path)
    nearness: tuple[Callable[[Function], bool], ...] = (
        lambda f: _stem(f.path) == stem,
        lambda f: _stem(f.path)[0] == stem[0],
        lambda f: bool(function.scope),
    )
    for near in nearness:
        group = [(f, tags) for f, tags in others if near(f)]
        exact = [tags for f, tags in group if f.params == function.params]
        if exact:
            return exact[0]
        if len({f.params for f, _ in group}) == 1:
            return group[0][1]
    return []


def units_now(root: Path) -> list[Unit]:
    """The units of `src/` as the files are on disk."""
    base = root / SOURCE_DIR
    paths = sorted(p for p in base.rglob("*") if p.suffix in SOURCE_SUFFIXES and p.is_file()) if base.is_dir() else []
    return units_from({p.relative_to(root).as_posix(): p.read_text(encoding="utf-8", errors="replace") for p in paths})


def units_at(root: Path, ref: str) -> list[Unit]:
    """The units of `src/` at the commit `ref` names."""
    return units_from(files_at(root, resolve(root, ref)))


# --- stamps -------------------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class Move:
    """A function that kept its code and its `@orig` tags and changed its qualified name or its file: `unit` as it is
    now, `was` as it stood."""

    unit: Unit
    was: Unit

    def to_json(self) -> dict[str, Any]:
        """The unit as a stamp lists it, with the key it moved from."""
        return {**self.unit.to_json(), "was": self.was.key, "was_where": self.was.where}


@dataclass(frozen=True)
class Changes:
    """What a branch did to the `@orig` functions: those it adds or changes, those it removes and those it moved."""

    changed: tuple[Unit, ...]
    removed: tuple[Unit, ...]
    moved: tuple[Move, ...]


def _same_tags(was: Unit, now: Unit) -> bool:
    """Whether two units cite the same original functions under the same tags (the addresses of both builds and the
    original's names)."""
    return was.cited == now.cited and sorted(was.names) == sorted(now.names)


def moves(before: Iterable[Unit], after: Iterable[Unit]) -> Changes:
    """The changes from the units `before` to the units `after`. A unit is **changed** when its key is new or its code
    or its addresses are not as they were. A new key whose code (the body hash, or the moved hash when only its
    qualified name differs), addresses and tag names equal those of a unit that is gone is a **move**, not a change:
    the function kept its study and its compare. Each gone unit is taken over once, so a copy (the old one still in
    the code) and a second function with the same code stay changed. A unit gone and not taken over is **removed**."""
    was = {u.key: u for u in before}
    now = {u.key: u for u in after}
    changed = [
        u
        for u in now.values()
        if u.key not in was or was[u.key].body_sha256 != u.body_sha256 or was[u.key].cited != u.cited
    ]
    gone = [u for key, u in was.items() if key not in now]
    taken: set[str] = set()
    found: dict[str, Move] = {}
    for exact in (True, False):  # the same code first, then the same code but for its own qualified name
        for unit in changed:
            if unit.key in found or unit.key in was:
                continue
            for old in gone:
                if old.key in taken or not _same_tags(old, unit):
                    continue
                same = (
                    old.body_sha256 == unit.body_sha256
                    if exact
                    else bool(unit.moved_sha256) and (old.moved_sha256 == unit.moved_sha256)
                )
                if same:
                    taken.add(old.key)
                    found[unit.key] = Move(unit, old)
                    break
    return Changes(
        tuple(u for u in changed if u.key not in found),
        tuple(u for u in gone if u.key not in taken),
        tuple(found[u.key] for u in changed if u.key in found),
    )


def pr_stamp(root: Path, tip_ref: str, base_ref: str) -> dict[str, Any]:
    """What a branch's orig review covers: the units at its tip that are new or changed (body or addresses) since
    it left `base_ref`. A function that only moved (`moves`) is listed under `moved`, not in `functions`: its review
    and its compare stand."""
    tip = resolve(root, tip_ref)
    base = _git(root, "merge-base", resolve(root, base_ref), tip).decode().strip()
    found = moves(units_at(root, base), units_at(root, tip))
    return {
        "mode": "pr",
        "tip": tip,
        "base": base,
        "functions": [u.to_json() for u in found.changed],
        "moved": [m.to_json() for m in found.moved],
    }


def sweep_stamp(
    root: Path, ref: str, folders: list[str], addresses: list[int], ledger: Ledger, every: bool = False
) -> dict[str, Any]:
    """The units in `folders` or carrying `addresses` at `ref` that are not reviewed at their current body (all of
    them with `every`)."""
    commit = resolve(root, ref)
    units = units_at(root, commit)
    status = status_of(ledger, units)
    wanted = [
        u
        for u in units
        if (any(u.path.startswith(f) for f in folders) or set(u.addresses) & set(addresses))
        and (every or status[u.key] in ("unreviewed", "stale"))
    ]
    return {"mode": "sweep", "commit": commit, "functions": [{**u.to_json(), "status": status[u.key]} for u in wanted]}


# --- hashes recorded before comments were left out -----------------------------------------------------------------

#: The `covers` key of a `compared` stamp that holds the Coney function's body hash.
BODY_COVER = "coney_body_sha256"


@functools.lru_cache(maxsize=8)
def _units_at_commit(root: str, commit: str) -> dict[str, Unit]:
    """The units of `src/` at one commit by key (a commit never changes, so the answer is kept)."""
    return {u.key: u for u in units_at(Path(root), commit)}


def recorded_body_is_current(root: Path, recorded: str | None, commit: str, key: str, body_sha256: str) -> bool:
    """Whether a body hash recorded at `commit` (a stamp's `coney_body_sha256`) still stands for the function `key`,
    whose body hash now is `body_sha256`: it is that hash, or the hash the body had before comments and white space
    were left out and the function's code at `commit` is the code it has now. This rehashes on read: no recorded
    stamp is rewritten or invalidated by the change of hash, and a code edit after the stamp still refuses it."""
    if not recorded:
        return False
    if recorded == body_sha256:
        return True
    if not re.fullmatch(r"[0-9a-f]{40}", commit or ""):
        return False
    try:
        then = _units_at_commit(str(root), commit).get(key)
    except ConfigError:
        return False
    return then is not None and then.legacy_sha256 == recorded and then.body_sha256 == body_sha256


def citing(units: Iterable[Unit]) -> dict[str, tuple[str, ...]]:
    """Every address (`0x00501000`) -> the keys of all the Coney functions that carry it: what a `compared` stamp's
    lookup needs to tell a function that shares its address with another from the only one citing it."""
    found: dict[str, list[str]] = {}
    for unit in units:
        for address in unit.addresses:
            found.setdefault(f"0x{address:08x}", []).append(unit.key)
    return {address: tuple(keys) for address, keys in found.items()}


def current_compared(
    root: Path,
    report: stamps.Report,
    address: str,
    covers: Mapping[str, str],
    key: str,
    cited_by: Collection[str] = (),
) -> stamps.Stamp | None:
    """The newest `compared` stamp of the function `key` at `address` when it passes and covers `covers`, where the
    function's body hash (`BODY_COVER`) is read by `recorded_body_is_current`; else None. `cited_by` is the key of every
    Coney function that carries `address` (`citing`): while another one does, only the stamps made for `key` count, so
    each of two functions of one original function keeps its own."""
    found = report.current("compared", address, covers, function=key, cited_by=cited_by)
    if found is not None:
        return found
    latest = report.latest("compared", address, function=key, cited_by=cited_by)
    if latest is None or latest.result != "pass":
        return None
    if any(latest.covers.get(name) != value for name, value in covers.items() if name != BODY_COVER):
        return None
    if BODY_COVER in covers and recorded_body_is_current(
        root, latest.covers.get(BODY_COVER), latest.commit, key, covers[BODY_COVER]
    ):
        return latest
    return None


# --- the verdict --------------------------------------------------------------------------------------------------


def function_addresses(function: Mapping[str, Any]) -> list[str]:
    """The addresses a stamped `function` can be reviewed at: its PS2 ones as `0x00100010`, then the Xbox port's as
    `xbox 0x000bd3c0`, so the two builds' equal numbers stay apart (docs/guides/review/task.md#verdict)."""
    return [*function["addresses"], *(f"xbox {a}" for a in function.get("xbox", ()))]


def verdict_problems(verdict: Any, stamp: dict[str, Any], rubric_version: int, strict_tip: bool = False) -> list[str]:
    """Why a verdict does not let the branch the stamp describes through (empty: it does).

    The verdict binds to what was reviewed: each function's body hash and addresses. A rebase that leaves every
    stamped function's body as reviewed keeps the verdict (its `tip` and `base` then name the commits it was made
    at); a changed or new function makes it stale. With `strict_tip`, `tip` and `base` must also be the stamp's.
    """
    if not isinstance(verdict, dict) or set(verdict) != set(VERDICT_KEYS):
        return [f"the verdict must be a JSON object with exactly the keys {', '.join(VERDICT_KEYS)}"]
    problems: list[str] = []
    for name in ("tip", "base"):
        if not (isinstance(verdict[name], str) and re.fullmatch(r"[0-9a-f]{40}", verdict[name])):
            problems.append(f"`{name}` must be a full commit hash")
        elif strict_tip and verdict[name] != stamp[name]:
            problems.append(f"the verdict's {name} is {verdict[name][:12]}, the branch's is {stamp[name][:12]}")
    if verdict["rubric_version"] != rubric_version:
        problems.append(f"the verdict used rubric {verdict['rubric_version']!r}; the rubric is at {rubric_version}")
    listed = {str(a).lower() for a in verdict["reviewed"]} if isinstance(verdict["reviewed"], list) else set()
    if verdict["verdict"] != "pass":
        problems.append(f"the verdict is {verdict['verdict']!r}, not 'pass'")
    findings = verdict["findings"] if isinstance(verdict["findings"], list) else []
    if any(isinstance(f, dict) and f.get("severity") == "blocker" for f in findings):
        problems.append("the verdict has a blocker finding")
    reviewed: dict[str, dict[str, Any]] = {}
    for item in verdict["functions"] if isinstance(verdict["functions"], list) else []:
        if isinstance(item, dict) and isinstance(item.get("key"), str):
            reviewed[item["key"]] = item
    skipped = (
        {k for k in verdict["not_reviewed"] if isinstance(k, str)}
        if isinstance(verdict["not_reviewed"], list)
        else set()
    )
    for function in stamp["functions"]:
        item = reviewed.get(function["key"])
        if item is None:
            if function["key"] not in skipped:
                problems.append(
                    f"{function['coney']} ({function['where']}) is neither reviewed nor listed as not reviewed"
                )
            continue
        cls = item.get("class")
        if cls not in CLASSES:
            problems.append(f"{function['coney']}: class {cls!r} is not one of {', '.join(CLASSES)}")
        elif cls not in PASSING:
            problems.append(f"{function['coney']}: class {cls} fails the review")
        if item.get("body_sha256") != function["body_sha256"]:
            problems.append(f"{function['coney']}: reviewed at another body (body_sha256 differs)")
        # A function that carries many tags (a binding table's lambdas) is compared where the change touches it, so
        # one of its addresses is enough; `addresses` says which.
        compared = item.get("addresses")
        own = {a.lower() for a in function_addresses(function)}
        if not isinstance(compared, list) or not compared:
            problems.append(f"{function['coney']}: no compared `addresses`")
        elif any(not isinstance(a, str) or a.lower() not in own or a.lower() not in listed for a in compared):
            problems.append(f"{function['coney']}: `addresses` must be its own and listed in `reviewed`")
    if skipped and not (isinstance(verdict["sampled"], str) and verdict["sampled"].strip()):
        problems.append("functions are listed as not reviewed but `sampled` gives no reason")
    return problems


def check_verdict_file(
    root: Path, path: Path, tip_ref: str, base_ref: str, strict_tip: bool = False
) -> tuple[dict[str, Any], list[str]]:
    """The stamp of the branch at `tip_ref` (since it left `base_ref`) and why the verdict at `path` does not cover
    it (empty: it does, or the branch changes no `@orig` function and needs none). For a release step to call."""
    stamp = pr_stamp(root, tip_ref, base_ref)
    if not stamp["functions"]:
        return stamp, []
    try:
        verdict = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return stamp, [f"{path} is missing; the branch changes {len(stamp['functions'])} @orig function(s)"]
    except (OSError, ValueError) as error:
        return stamp, [f"{path}: cannot be read ({error})"]
    rubric = load_conventions_leniently(root).pull_requests.orig_review.rubric_version
    return stamp, verdict_problems(verdict, stamp, rubric, strict_tip)


def scratch_root(root: Path) -> Path:
    """The workspace's scratch folder."""
    return workspace_folder(root, "scratch")


def _branch_of(root: Path, body: Path, head: str | None, cwd: str | None) -> str:
    """The branch a `gh pr create` is about: `--head`, else the branch checked out where it runs, else the folder of
    the description under the workspace's scratch (`scratch/<branch>/PR.md`)."""
    if head:
        return head.split(":", 1)[-1]
    if cwd:
        done = subprocess.run(
            ["git", "-C", cwd, "symbolic-ref", "--quiet", "--short", "HEAD"],
            capture_output=True,
            text=True,
            check=False,
        )
        if done.returncode == 0 and done.stdout.strip():
            return done.stdout.strip()
    try:
        relative = body.resolve().parent.relative_to(scratch_root(root).resolve())
    except ValueError:
        raise CannotTell(f"{body}: not in the scratch folder and no --head; cannot tell the branch") from None
    if not relative.parts:
        raise CannotTell(f"{body}: directly in the scratch folder; cannot tell the branch")
    return relative.as_posix()


def gate(root: Path, body: Path, head: str | None, base: str | None, cwd: str | None, ledger: Ledger) -> list[str]:
    """Why the branch a `gh pr create` opens may not be opened yet (empty: it may). A branch that adds or changes no
    `@orig` function needs no verdict; one that does needs a passing `ORIG-REVIEW.json` for its tip next to the
    description. A pass is recorded in the ledger. CannotTell when the branch or base cannot be found."""
    rules = load_conventions_leniently(root).pull_requests.orig_review
    branch = _branch_of(root, body, head, cwd)
    try:
        tip = resolve(root, f"refs/heads/{branch}")
    except ConfigError as error:
        raise CannotTell(f"branch {branch!r} not found: {error}") from None
    base_ref = base or f"origin/{load_conventions_leniently(root).repo.default_branch}"
    try:
        stamp = pr_stamp(root, tip, base_ref)
    except ConfigError as error:
        raise CannotTell(str(error)) from None
    if not stamp["functions"]:
        return []
    names = ", ".join(f["coney"] for f in stamp["functions"][:5]) + (" ..." if len(stamp["functions"]) > 5 else "")
    path = body.parent / rules.verdict_file
    how = (
        f"run the {rules.agent} agent on branch {branch} (docs/guides/agents.md#reviewer) so it writes "
        f"{rules.verdict_file} next to {body.name}"
    )
    if not path.is_file():
        count = len(stamp["functions"])
        return [f"branch {branch} changes {count} @orig function(s) ({names}) and {path} is missing; {how}"]
    try:
        verdict = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        return [f"{path}: cannot be read ({error}); {how}"]
    problems = verdict_problems(verdict, stamp, rules.rubric_version)
    if problems:
        return [f"{path}: {p}" for p in problems] + [how]
    record_verdict(root, ledger, verdict, stamp, str(path))
    return []


# --- the ledger ---------------------------------------------------------------------------------------------------


class Ledger:
    """The record of reviews: one JSON object per line, appended to, never rewritten."""

    def __init__(self, path: Path) -> None:
        self.path = path

    def entries(self) -> list[dict[str, Any]]:
        """Every entry, oldest first; a line that is not an object is skipped."""
        if not self.path.is_file():
            return []
        found = []
        for line in self.path.read_text(encoding="utf-8").splitlines():
            try:
                item = json.loads(line)
            except ValueError:
                continue
            if isinstance(item, dict) and isinstance(item.get("key"), str):
                found.append(item)
        return found

    def add(self, entries: Iterable[dict[str, Any]]) -> int:
        """Append the entries not already there (same key, body, commit and class); returns how many were added."""
        have = {(e["key"], e.get("body_sha256"), e.get("commit"), e.get("class")) for e in self.entries()}
        new = [e for e in entries if (e["key"], e["body_sha256"], e["commit"], e["class"]) not in have]
        if new:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            with self.path.open("a", encoding="utf-8") as stream:
                for item in new:
                    stream.write(json.dumps(item, sort_keys=True) + "\n")
        return len(new)


def default_ledger(root: Path) -> Ledger:
    """The workspace's ledger (`pull_requests.orig_review.ledger` under the scratch folder)."""
    return Ledger(scratch_root(root) / load_conventions_leniently(root).pull_requests.orig_review.ledger)


def entry(unit: Unit, commit: str, cls: str, mode: str, source: str, note: str, evidence: str = "") -> dict[str, Any]:
    """A ledger entry for one reviewed unit; `xbox` lists its Xbox addresses when it has any, as its stamp does."""
    if cls not in CLASSES:
        raise ConfigError(f"class {cls!r} is not one of {', '.join(CLASSES)}")
    found: dict[str, Any] = {
        "key": unit.key,
        "coney": unit.coney,
        "where": unit.where,
        "addresses": [f"0x{a:08x}" for a in unit.addresses],
        "body_sha256": unit.body_sha256,
        "commit": commit,
        "class": cls,
        "evidence": evidence,
        "mode": mode,
        "source": source,
        "note": note,
        "date": datetime.date.today().isoformat(),
    }
    if unit.xbox:
        found["xbox"] = [f"0x{a:08x}" for a in unit.xbox]
    return found


def status_of(ledger: Ledger, units: list[Unit]) -> dict[str, str]:
    """Each unit's key -> its latest review's class at its current body, "stale" when it was reviewed at another
    body only, "unreviewed" when never. A review recorded under the hash from before comments were left out counts
    while the function's lines are as they were then (its `legacy_sha256`)."""
    latest: dict[tuple[str, str], str] = {}
    seen: set[str] = set()
    for item in ledger.entries():
        latest[(item["key"], str(item.get("body_sha256")))] = str(item.get("class"))
        seen.add(item["key"])
    return {
        u.key: latest.get((u.key, u.body_sha256))
        or (latest.get((u.key, u.legacy_sha256)) if u.legacy_sha256 else None)
        or ("stale" if u.key in seen else "unreviewed")
        for u in units
    }


#: How much each status needs attention, least first: an address's status is its worst unit's.
_ATTENTION = (
    "clean",
    "diff-deliberate-ok",
    "unclear",
    "unreviewed",
    "stale",
    "diff-uncommented",
    "wrong-address",
    "transcription",
    "diff-bug",
)


def address_status(ledger: Ledger, units: list[Unit], build: str = PS2) -> dict[int, str]:
    """Each address tagged in `build` (the PS2's `SLUS_212.15`, or `xbox` for `default.xbe`) -> the status of the unit
    carrying it that most needs attention. The two builds' addresses are separate maps, as the same number names
    different code in each."""
    status = status_of(ledger, units)
    result: dict[int, str] = {}
    for unit in units:
        for address in unit.xbox if build == XBOX else unit.addresses:
            old = result.get(address)
            if old is None or _ATTENTION.index(status[unit.key]) > _ATTENTION.index(old):
                result[address] = status[unit.key]
    return result


def record_verdict(root: Path, ledger: Ledger, verdict: dict[str, Any], stamp: dict[str, Any], source: str) -> int:
    """Record a pull request verdict's reviewed functions; returns how many entries were added."""
    by_key = {f["key"]: f for f in stamp["functions"]}
    units = {u.key: u for u in units_from_stamp(stamp) if u.key in by_key}
    entries = [
        entry(
            units[item["key"]],
            stamp["tip"],
            item["class"],
            "pr",
            source,
            str(item.get("note", "")),
            str(item.get("evidence", "")),
        )
        for item in verdict["functions"]
        if isinstance(item, dict) and item.get("key") in units and item.get("class") in CLASSES
    ]
    return ledger.add(entries)


def units_from_stamp(stamp: dict[str, Any]) -> list[Unit]:
    """The units a stamp lists, back as units."""
    found = []
    for f in stamp["functions"]:
        path, _, line = f["where"].rpartition(":")
        found.append(
            Unit(
                f["key"],
                path,
                int(line),
                f["coney"],
                tuple(int(a, 16) for a in f["addresses"]),
                tuple(f.get("names", ())),
                f["body_sha256"],
                xbox=tuple(int(a, 16) for a in f.get("xbox", ())),
            )
        )
    return found


_ADDRESS = re.compile(r"(?:0x)?([0-9a-fA-F]{8})")


def _matches(unit: Unit, coney: str) -> bool:
    """Whether a findings row's Coney function names `unit` (with or without its namespaces)."""
    name = coney.strip().split("(")[0].strip()
    return bool(name) and (name == unit.coney or name.endswith("::" + unit.coney))


def import_findings(root: Path, ledger: Ledger, paths: list[Path], ref: str) -> tuple[int, list[str]]:
    """Record the rows of sweep FINDINGS.tsv files (addr, orig_name, coney_function, file:line, class, evidence,
    note) against the units at `ref`, the commit the sweep read. Returns the entries added and the rows that match
    no unit (an address with no tagged definition there, or a class that is not one of CLASSES)."""
    commit = resolve(root, ref)
    units = units_at(root, commit)
    by_address: dict[int, list[Unit]] = defaultdict(list)
    for unit in units:
        for address in unit.addresses:
            by_address[address].append(unit)
    entries: list[dict[str, Any]] = []
    unmatched: list[str] = []
    for path in paths:
        with path.open(encoding="utf-8", newline="") as stream:
            for number, row in enumerate(csv.reader(stream, delimiter="\t"), start=1):
                if len(row) < 5 or not _ADDRESS.fullmatch(row[0].strip()):
                    continue  # the header, a blank or short line
                address = int(_ADDRESS.fullmatch(row[0].strip()).group(1), 16)  # type: ignore[union-attr]
                cls = row[4].strip()
                where_file = row[3].strip().rpartition(":")[0] or row[3].strip()
                candidates = by_address.get(address, [])
                named = [u for u in candidates if _matches(u, row[2])]
                if len(named) > 1:
                    named = [u for u in named if u.path == where_file] or named[:1]
                chosen = named or (candidates if len(candidates) == 1 else [])
                if not chosen or cls not in CLASSES:
                    unmatched.append(f"{path.name}:{number}: {row[0].strip()} {row[2].strip()} ({cls})")
                    continue
                evidence = row[5].strip() if len(row) > 5 else ""
                note = row[6].strip() if len(row) > 6 else ""
                entries.extend(entry(u, commit, cls, "sweep", str(path), note, evidence) for u in chosen)
    return ledger.add(entries), unmatched


def summary(ledger: Ledger, units: list[Unit]) -> dict[str, int]:
    """How many units have each status."""
    counts: dict[str, int] = defaultdict(int)
    for value in status_of(ledger, units).values():
        counts[value] += 1
    return dict(sorted(counts.items()))
