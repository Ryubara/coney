# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools orig check-tags --backlog`: the untagged functions that arrived in a gameplay folder by a move.

`orig check-tags` lets a function that only moved (orig_review.function_moves) keep the standing it had, so code that
needed no tag in a Coney-only folder (`src/platform/`) enters a gameplay folder (`src/gamemodes/`) with neither
`@orig` nor `@coney-only`, and some of it ports original behaviour. This lists those functions, each with the commit
that brought it in, so the fidelity audit can tag them (docs/guides/coney-tools-fidelity.md#moved-backlog).

Nothing is listed by name: the history says what moved. Each function that is untagged now (and not exempt) is
followed back through the commits that changed its file. A commit where it is a move (the same code, tags and standing
under another file, folder or class name) from a file outside the gameplay folders is where it arrived; a move from
another gameplay folder only changes where to look next; a function that is new in a commit, or was already in its file
as it is, was not moved in from outside.
"""

from __future__ import annotations

import subprocess
from dataclasses import dataclass, replace
from pathlib import Path

from coney_tools import orig_review
from coney_tools.config import ConfigError, find_repo_root
from coney_tools.conventions import load_conventions
from coney_tools.orig_index import SOURCE_DIR, SOURCE_SUFFIXES, find_untagged, read_sources

#: The columns of the TSV `--out` writes.
COLUMNS = ("where", "function", "lines", "moved_from", "commit", "title")

#: What `git log` prints before each commit of the history (and splits it on).
_MARK = "\x01"


@dataclass(frozen=True)
class MovedFunction:
    """An untagged function of a gameplay folder and the commit that moved it in from outside the gameplay folders."""

    where: str  # `path:line` now
    coney: str  # the qualified name now
    lines: int
    moved_from: str  # `path:line` where it stood before that commit, outside the gameplay folders
    commit: str
    title: str


@dataclass(frozen=True)
class _Track:
    """A function being followed back: how it is found now, and its key and file at the commit being looked at."""

    where: str
    coney: str
    lines: int
    key: str
    path: str


@dataclass(frozen=True)
class _Commit:
    """A commit of the history and what it changed under `src/`."""

    sha: str
    parent: str
    title: str
    paths: frozenset[str]


def _history(root: Path, since: str | None) -> list[_Commit]:
    """The commits that changed C++ files under `src/`, newest first along the first parent, with those files. The
    squash-merged history of `main` has no merges; a merge counts as the change against its first parent."""
    args = ["log", "--first-parent", "-m", "--name-only", "--no-renames", f"--format={_MARK}%H %P%x09%s"]
    args += [f"{since}..HEAD"] if since else ["HEAD"]
    args += ["--", SOURCE_DIR.as_posix()]
    done = subprocess.run(["git", "-C", str(root), *args], capture_output=True, check=False)
    if done.returncode != 0:
        raise ConfigError(f"git log: {done.stderr.decode('utf-8', 'replace').strip() or 'failed'}")
    commits: list[_Commit] = []
    for block in done.stdout.decode("utf-8", "replace").split(_MARK)[1:]:
        head, _, names = block.partition("\n")
        ids, _, title = head.partition("\t")
        sha, *parents = ids.split()
        paths = frozenset(n for n in names.split("\n") if n and Path(n).suffix in SOURCE_SUFFIXES)
        if paths:
            commits.append(_Commit(sha, parents[0] if parents else "", title.strip(), paths))
    return commits


def _name_of(track: _Track) -> str:
    """The function's own name, without its class and namespaces."""
    return track.coney.rsplit("::", 1)[-1]


def _diff_text(root: Path, commit: _Commit) -> tuple[dict[str, str], dict[str, str]]:
    """The lines the commit added to and removed from each of its C++ files (against its first parent), per path, each
    joined by newlines."""
    args = ["diff", "--no-renames", "-U0", commit.parent, commit.sha, "--", *sorted(commit.paths)]
    done = subprocess.run(["git", "-C", str(root), *args], capture_output=True, check=False)
    if done.returncode != 0:
        raise ConfigError(f"git diff: {done.stderr.decode('utf-8', 'replace').strip() or 'failed'}")
    added: dict[str, list[str]] = {}
    removed: dict[str, list[str]] = {}
    path = ""
    for line in done.stdout.decode("utf-8", "replace").splitlines():
        if line.startswith("+++ "):
            path = path if line == "+++ /dev/null" else line[4:].removeprefix("b/")
        elif line.startswith("--- "):
            path = line[4:].removeprefix("a/")  # a deleted file has no "+++" path: its lines are all removed
        elif line.startswith("+"):
            added.setdefault(path, []).append(line[1:])
        elif line.startswith("-"):
            removed.setdefault(path, []).append(line[1:])
    return (
        {p: chr(10).join(text) for p, text in added.items()},
        {p: chr(10).join(text) for p, text in removed.items()},
    )


def _could_have_moved_in(track: _Track, added: dict[str, str], removed: dict[str, str]) -> bool:
    """Whether the commit's lines allow the function to have moved into its file: its name is on a line added to the
    file and on a line removed from another file. (A cheap test that spares reading both sides of most commits.)"""
    name = _name_of(track)
    return name in added.get(track.path, "") and any(
        name in text for path, text in removed.items() if path != track.path
    )


def _tracked(root: Path, gameplay: tuple[str, ...]) -> list[_Track]:
    """The functions to follow: every untagged function of the gameplay folders now that is not exempt."""
    sources = read_sources(root)
    untagged, _ = find_untagged(sources, gameplay)
    wanted = {u.where for u in untagged if not u.exempt}
    units = orig_review.standings(orig_review.texts_now(root))
    by_where = {unit.where: unit for unit in units.values() if unit.where in wanted}
    return [
        _Track(u.where, u.coney, u.lines, by_where[u.where].key, by_where[u.where].path)
        for u in untagged
        if u.where in by_where
    ]


def find(root: Path, since: str | None = None) -> list[MovedFunction]:
    """The untagged functions of the gameplay folders that arrived by a move from outside them, newest move first.
    With `since` only the commits after it (`since..HEAD`) are looked at."""
    gameplay = load_conventions(root).paths.gameplay
    following = {t.where: t for t in _tracked(root, gameplay)}
    found: list[MovedFunction] = []
    for commit in _history(root, since):
        if not following:
            break
        # Only a function whose name was added to its file and removed from another can have moved in here: the rest
        # stood there as they are or are new, so the (slow) reading of both sides is skipped when none can have.
        diff = _diff_text(root, commit) if commit.parent else None
        here = {
            w: t
            for w, t in following.items()
            if t.path in commit.paths and (diff is None or _could_have_moved_in(t, *diff))
        }
        if not here:
            continue
        before = orig_review.files_at(root, commit.parent, commit.paths) if commit.parent else {}
        after = orig_review.files_at(root, commit.sha, commit.paths)
        was_units = orig_review.standings(before)
        changes = orig_review.moves(was_units.values(), orig_review.standings(after).values())
        moved = {move.unit.key: move.was for move in changes.moved}
        known = {unit.key for unit in was_units.values()}
        for where, track in here.items():
            if track.key in moved:
                was = moved[track.key]
                if was.path.startswith(gameplay):  # moved within the gameplay folders: look further back
                    following[where] = replace(track, key=was.key, path=was.path)
                    continue
                found.append(MovedFunction(track.where, track.coney, track.lines, was.where, commit.sha, commit.title))
            elif track.key in known:
                continue  # it stood there as it is: any move into this file was an earlier commit's
            del following[where]  # new here (or moved with its code changed): written in the gameplay folders
    return found


def _line(found: MovedFunction) -> str:
    """One finding as the command prints it."""
    return (
        f"{found.where}: {found.coney} ({found.lines} lines) moved from {found.moved_from} "
        f"in {found.commit[:12]} {found.title}"
    )


def run(since: str | None, out: Path | None) -> int:
    """Print each function and a count per commit, and write the TSV when asked; 0 (a list, not a gate)."""
    found = find(find_repo_root(Path.cwd()), since)
    for item in found:
        print(_line(item))
    per_commit: dict[str, int] = {}
    for item in found:
        per_commit[f"{item.commit[:12]} {item.title}"] = per_commit.get(f"{item.commit[:12]} {item.title}", 0) + 1
    for commit, count in per_commit.items():
        print(f"  {count:4d}  {commit}")
    if out is not None:
        rows = ["\t".join(COLUMNS)]
        rows += ["\t".join([f.where, f.coney, str(f.lines), f.moved_from, f.commit, f.title]) for f in found]
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text("\n".join(rows) + "\n", encoding="utf-8")
        print(f"orig check-tags --backlog: wrote {out}")
    where = f"since {since}" if since else "in the history"
    print(f"orig check-tags --backlog: {len(found)} untagged function(s) moved into gameplay folders {where}")
    return 0
