# SPDX-License-Identifier: GPL-3.0-or-later
"""The trial rebase behind Conclave's conflict-to-work events (docs/guides/conclave.md#conflicts).

A pull request that conflicts with `main` is work for someone, and which kind depends on one question: does the branch
rebase onto `main` cleanly, and do the stamps it carries still cover its change after the rebase? `run_trial` asks it
the way `pr open --dry-run` does: it rebases the branch's tip in a throwaway worktree, weighs the stamps there with the
same function `pr open` proves a unit by (`pr_ready.readiness`), and removes the worktree. The unit's own worktree, its
branch and its remote are never touched, and nothing is pushed.

* `ready`: the rebase is clean and the stamps cover the rebased change, so `pr open` would pass and no engineer is
  needed: Conclave raises `unit-ready`.
* `stale`: the rebase is clean but a stamp no longer covers the change (or the description no longer passes): an
  engineer redoes the stamps.
* `conflict`: git stopped on paths only a person can merge: an engineer resolves them.
* `current`: `main` is already in the branch, so there is nothing to rebase.
* `unknown`: the answer could not be had (a fetch or git failed, a rebase is in progress in the worktree).

Each conflicting pull request has a file in the scratch directory (`conclave.watch.conflicts_dir`, the `Ledger`): when
its conflict was first seen on the current head, which is how a waived conflict knows it is a day old, and the last
trial with what it was made from, so a conflict that waits for days is not rebased again at every read.
"""

from __future__ import annotations

import contextlib
import hashlib
import json
import os
import shutil
import subprocess
import tempfile
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

from coney_tools import pr_publish
from coney_tools.config import ConfigError
from coney_tools.conventions import Conventions
from coney_tools.pr_ready import Readiness
from coney_tools.unit import Unit

#: The statuses of a trial.
READY, STALE, CONFLICT, CURRENT, UNKNOWN = "ready", "stale", "conflict", "current", "unknown"
#: How many paths a text line names before it says how many more.
PATHS_SHOWN = 4
#: How many digits of a commit a fingerprint or an id keeps.
SHORT = 7

#: Whether `pr open` would pass the proof for a unit: (its worktree, its unit, the description file, the conventions,
#: the clock) to a `Readiness` (`Sources.ready`).
ReadyFn = Callable[[Path, Unit, Path, Conventions, datetime], Readiness]


@dataclass(frozen=True)
class Trial:
    """What a trial rebase found for one branch."""

    #: One of `READY`, `STALE`, `CONFLICT`, `CURRENT`, `UNKNOWN`.
    status: str
    #: Why the unit is not ready (the first refusal), or why the answer is unknown; empty otherwise.
    reason: str = ""
    #: The paths git could not merge (a conflict).
    paths: tuple[str, ...] = ()
    #: The change the stamps were weighed against after the rebase (`ready` and `stale`).
    change: str = ""

    @property
    def clean(self) -> bool:
        """Whether the rebase went through (the stamps may still be stale)."""
        return self.status in (READY, STALE, CURRENT)

    def as_json(self) -> dict[str, Any]:
        """The trial as the ledger stores it."""
        return {"status": self.status, "reason": self.reason, "paths": list(self.paths), "change": self.change}

    @classmethod
    def from_json(cls, data: object) -> Trial | None:
        """The trial a ledger holds, or None when what it holds is not one."""
        if not isinstance(data, dict) or data.get("status") not in (READY, STALE, CONFLICT, CURRENT):
            return None
        paths = data.get("paths")
        return cls(
            str(data["status"]),
            str(data.get("reason") or ""),
            tuple(str(path) for path in paths) if isinstance(paths, list) else (),
            str(data.get("change") or ""),
        )

    def paths_text(self) -> str:
        """The unmerged paths as a short list: the first few, then how many more."""
        shown = ", ".join(self.paths[:PATHS_SHOWN])
        more = len(self.paths) - PATHS_SHOWN
        return shown + (f" and {more} more" if more > 0 else "")


# --- git ------------------------------------------------------------------------------------------------------------


def real_git(folder: Path, args: Sequence[str]) -> subprocess.CompletedProcess[str]:
    """Run git in `folder` without optional locks (a read of the snapshot must never make an engineer's commit fail)."""
    try:
        return subprocess.run(
            ["git", "-C", str(folder), *args],
            capture_output=True,
            encoding="utf-8",
            errors="replace",
            check=False,
            env={**os.environ, "GIT_OPTIONAL_LOCKS": "0"},
        )
    except OSError as error:
        raise ConfigError(f"git cannot be run ({error})") from error


def _mid_operation(git: pr_publish.Git, root: Path) -> bool:
    """Whether the worktree is in the middle of a rebase or a merge: somebody is already on it."""
    for marker in ("rebase-merge", "rebase-apply", "MERGE_HEAD"):
        done = git(root, ["rev-parse", "--git-path", marker])
        if done.returncode == 0 and (root / done.stdout.strip()).exists():
            return True
    return False


def prepare(
    root: Path, conventions: Conventions, *, remote: str = "origin", git: pr_publish.Git = real_git, fetch: bool = True
) -> Trial | None:
    """Get a trial ready: None when it may go on, else the `unknown` trial that says why not (somebody is in the
    middle of a rebase or merge in the worktree, or the fetch of the remote's default branch failed). `fetch` false
    skips the fetch, for a read that fetched for an earlier branch: the remote is the repository's, not the branch's."""
    default = conventions.repo.default_branch
    try:
        if _mid_operation(git, root):
            return Trial(UNKNOWN, "a rebase or merge is in progress in the worktree")
        fetched = git(root, ["fetch", "-q", remote, default]) if fetch else None
    except ConfigError as error:
        return Trial(UNKNOWN, str(error))
    if fetched is not None and fetched.returncode != 0:
        return Trial(UNKNOWN, f"git fetch {remote} {default} failed: {fetched.stderr.strip() or 'no output'}")
    return None


def run_trial(
    root: Path,
    unit: Unit,
    text: Path,
    conventions: Conventions,
    now: datetime,
    ready: ReadyFn,
    *,
    remote: str = "origin",
    git: pr_publish.Git = real_git,
) -> Trial:
    """Rebase the tip of the worktree `root` onto `<remote>/<default branch>` (as fetched: see `prepare`) in a
    throwaway worktree and weigh the unit (scratch folder `unit.dir`, description `text`) there.

    The worktree's uncommitted changes are not part of it: the trial is about the commits the pull request has.
    Nothing but the throwaway worktree is written, and it is removed again."""
    upstream = f"{remote}/{conventions.repo.default_branch}"
    try:
        if git(root, ["merge-base", "--is-ancestor", upstream, "HEAD"]).returncode == 0:
            return Trial(CURRENT)
        folder = Path(tempfile.mkdtemp(prefix="coney-conflict-"))
        tree = folder / "tree"
        try:
            pr_publish.out(git, root, "worktree", "add", "--detach", "-q", str(tree), "HEAD")
            try:
                return _weigh(git, tree, unit, text, conventions, now, ready, upstream)
            finally:
                git(root, ["worktree", "remove", "--force", str(tree)])
                git(root, ["worktree", "prune"])
        finally:
            shutil.rmtree(folder, ignore_errors=True)
    except ConfigError as error:
        return Trial(UNKNOWN, str(error))


def _weigh(
    git: pr_publish.Git,
    tree: Path,
    unit: Unit,
    text: Path,
    conventions: Conventions,
    now: datetime,
    ready: ReadyFn,
    upstream: str,
) -> Trial:
    """Rebase in `tree`, then weigh the stamps against the rebased change."""
    result = pr_publish.rebase(git, tree, upstream)
    if result.status == "conflict":
        return Trial(CONFLICT, f"rebase onto {upstream} stops at {result.commit[:12]}", result.paths)
    weighed = ready(tree, unit, text, conventions, now)
    if weighed.unknown:
        return Trial(UNKNOWN, weighed.reason)
    if weighed.ready:
        return Trial(READY, change=weighed.change)
    return Trial(STALE, weighed.reason, change=weighed.change)


# --- the ledger -----------------------------------------------------------------------------------------------------


def fingerprint(git: pr_publish.Git, root: Path, unit: Unit, text: Path, remote: str, default: str) -> str:
    """What a trial's answer depends on, as one hash: the branch's tip, `main`'s tip, the stamp log's files and the
    description file. A trial is reused while it is the same. An empty string when the tips cannot be read."""
    tip = git(root, ["rev-parse", "HEAD"])
    top = git(root, ["rev-parse", f"{remote}/{default}"])
    if tip.returncode != 0 or top.returncode != 0:
        return ""
    parts = [tip.stdout.strip(), top.stdout.strip()]
    for path in sorted([*unit.stamps.glob("*"), text], key=lambda p: str(p)):
        with contextlib.suppress(OSError):
            parts.append(f"{path.name}:{path.stat().st_mtime_ns}:{path.stat().st_size}")
    return hashlib.sha1("|".join(parts).encode("utf-8")).hexdigest()


class Ledger:
    """The files of `conclave.watch.conflicts_dir`: one JSON object per conflicting pull request (or branch).

    `{"head": ..., "first_seen": ..., "fingerprint": ..., "trial": ...}`. A file that cannot be read or written is
    ignored: the worst that follows is a conflict that looks new at every read and a trial that is made again."""

    def __init__(self, folder: Path) -> None:
        self.folder = folder
        #: Whether `main` was fetched for an earlier trial of this read (one fetch serves every branch).
        self.fetched = False

    def _path(self, key: str) -> Path:
        """The file of one pull request or branch (a `/` in a branch name becomes `-`)."""
        return self.folder / f"{key.replace('/', '-')}.json"

    def _read(self, key: str) -> dict[str, Any]:
        """The object in the file, or an empty one."""
        try:
            data = json.loads(self._path(key).read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return {}
        return data if isinstance(data, dict) else {}

    def _write(self, key: str, data: dict[str, Any]) -> None:
        """Replace the file whole; a failure is ignored."""
        path = self._path(key)
        temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
        try:
            self.folder.mkdir(parents=True, exist_ok=True)
            temporary.write_text(json.dumps(data, indent=1) + "\n", encoding="utf-8")
            temporary.replace(path)
        except OSError:
            with contextlib.suppress(OSError):
                temporary.unlink(missing_ok=True)

    def first_seen(self, key: str, head: str, now: datetime) -> datetime:
        """When the conflict of `key` was first seen on `head` (a new head starts a new conflict, written down)."""
        data = self._read(key)
        if data.get("head") == head:
            try:
                return datetime.fromisoformat(str(data["first_seen"]).replace("Z", "+00:00")).astimezone(UTC)
            except (KeyError, ValueError):
                pass
        kept: dict[str, Any] = {
            name: data[name] for name in ("fingerprint", "trial") if data.get("head") == head and name in data
        }
        self._write(key, {"head": head, "first_seen": now.astimezone(UTC).strftime("%Y-%m-%dT%H:%M:%SZ"), **kept})
        return now

    def cached(self, key: str, head: str, print_: str) -> Trial | None:
        """The trial made for this fingerprint on this head, or None."""
        data = self._read(key)
        if not print_ or data.get("head") != head or data.get("fingerprint") != print_:
            return None
        return Trial.from_json(data.get("trial"))

    def remember(self, key: str, head: str, print_: str, trial: Trial) -> None:
        """Keep the trial with the fingerprint it was made from (an `unknown` one is never kept). A file of another
        head is replaced: its trial was about another branch tip."""
        if not print_ or trial.status == UNKNOWN:
            return
        data = self._read(key)
        same = data if data.get("head") == head else {}
        self._write(key, {**same, "head": head, "fingerprint": print_, "trial": trial.as_json()})

    def forget_except(self, keys: set[str]) -> None:
        """Remove the files of every pull request not in `keys`: its conflict is over, the next one starts afresh."""
        if not self.folder.is_dir():
            return
        wanted = {self._path(key).name for key in keys}
        for path in self.folder.glob("*.json"):
            if path.name not in wanted:
                with contextlib.suppress(OSError):
                    path.unlink()


def trial_of(
    root: Path,
    unit: Unit,
    text: Path,
    conventions: Conventions,
    now: datetime,
    ready: ReadyFn,
    ledger: Ledger,
    key: str,
    head: str,
    *,
    remote: str = "origin",
    git: pr_publish.Git = real_git,
) -> Trial:
    """Fetch (`prepare`), then `run_trial`, reusing the ledger's answer while nothing it depends on has moved: the
    branch's tip, `main`'s tip, the stamp log and the description."""
    default = conventions.repo.default_branch
    refused = prepare(root, conventions, remote=remote, git=git, fetch=not ledger.fetched)
    if refused is not None:
        return refused
    ledger.fetched = True
    print_ = fingerprint(git, root, unit, text, remote, default)
    cached = ledger.cached(key, head, print_)
    if cached is not None:
        return cached
    found = run_trial(root, unit, text, conventions, now, ready, remote=remote, git=git)
    ledger.remember(key, head, print_, found)
    return found


def tip_of(root: Path, *, git: pr_publish.Git = real_git) -> str:
    """The commit the worktree `root` is at, or an empty string when git cannot say."""
    try:
        done = git(root, ["rev-parse", "HEAD"])
    except ConfigError:
        return ""
    return done.stdout.strip() if done.returncode == 0 else ""
