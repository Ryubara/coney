# SPDX-License-Identifier: GPL-3.0-or-later
"""The git and GitHub steps of `coney-tools pr open`: rebase, push with a lease, find, create and edit the pull request,
read it back (docs/guides/pr.md#open).

Everything that reaches outside the process goes through two injected callables, so the tests run against a local bare
remote and a fake `gh` and never touch the network: `Git` runs git in a folder and returns the completed process (a
failing git is a return code, never an exception), and `Gh` runs `gh` in a folder and returns its output, raising
`GhError` when it fails. The repository is never written down here: `resolve_repo` asks `gh` what the remote is (the
parent when it is a fork).

The pull request is found by its head branch, so a run that failed after creating it finds it again and edits it:
every step here can be run twice. The push names the tip it expects the remote branch to have (`--force-with-lease=
<branch>:<expected>`), taken from what the last run recorded, so a bare lease, which a `git fetch` in between can
defeat, is never used and neither is a force.
"""

from __future__ import annotations

import json
import re
import subprocess
from collections.abc import Callable, Sequence
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from coney_tools.config import ConfigError

#: The oldest `gh` that uploads pictures with `--attach`.
MIN_GH = (2, 99)
#: The file in the task's scratch folder that remembers what the last run published.
PUBLISHED_FILE = "pr-published.json"
#: The file that names the paths of a rebase conflict.
REBASE_FILE = "REBASE.md"


class GhError(Exception):
    """A `gh` command failed or could not run."""


#: Runs `gh` with these arguments in this folder (None: the current one) and returns its output.
Gh = Callable[[Sequence[str], Path | None], str]
#: Runs git with these arguments in this folder and returns the finished process.
Git = Callable[[Path, Sequence[str]], "subprocess.CompletedProcess[str]"]


def real_git(folder: Path, args: Sequence[str]) -> subprocess.CompletedProcess[str]:
    """Run git in `folder`; the process, whatever its code. ConfigError when git cannot be started."""
    try:
        return subprocess.run(
            ["git", "-C", str(folder), *args], capture_output=True, encoding="utf-8", errors="replace", check=False
        )
    except OSError as error:
        raise ConfigError(f"git cannot be run ({error})") from error


def real_gh(args: Sequence[str], folder: Path | None) -> str:
    """Run `gh` in `folder`; its output, or GhError with its message."""
    try:
        done = subprocess.run(
            ["gh", *args],
            cwd=folder,
            capture_output=True,
            encoding="utf-8",
            errors="replace",
            timeout=600,
            check=False,
        )
    except (OSError, subprocess.SubprocessError) as error:
        raise GhError(f"gh could not run: {error}") from error
    if done.returncode != 0:
        raise GhError(f"gh {' '.join(args[:2])} failed: {(done.stderr or done.stdout).strip()}")
    return done.stdout


def out(git: Git, folder: Path, *args: str) -> str:
    """The output of a git command that must succeed; ConfigError with git's message when it does not."""
    done = git(folder, args)
    if done.returncode != 0:
        raise ConfigError(f"git {' '.join(args[:3])}: {(done.stderr or done.stdout).strip() or 'failed'}")
    return done.stdout


# --- gh: version, repository, pull requests --------------------------------------------------------------------------


def gh_version(gh: Gh) -> tuple[int, ...]:
    """The version `gh --version` reports; GhError when it is not a version."""
    found = re.search(r"(\d+)\.(\d+)\.(\d+)", gh(["--version"], None))
    if found is None:
        raise GhError("gh --version printed no version")
    return tuple(int(part) for part in found.groups())


def resolve_repo(gh: Gh, git: Git, worktree: Path, remote: str, explicit: str | None) -> str:
    """`OWNER/NAME` of the repository the pull request goes to: `explicit`, else what `gh` makes of the remote's address
    (the parent when the remote is a fork)."""
    if explicit:
        return explicit
    url = out(git, worktree, "remote", "get-url", remote).strip()
    try:
        data = json.loads(gh(["repo", "view", url, "--json", "nameWithOwner,parent"], worktree))
        parent = data.get("parent")
        if isinstance(parent, dict) and parent.get("name") and isinstance(parent.get("owner"), dict):
            return f"{parent['owner']['login']}/{parent['name']}"
        return str(data["nameWithOwner"])
    except (ValueError, KeyError, TypeError) as error:
        raise GhError(f"gh repo view printed no repository ({error})") from error


@dataclass(frozen=True)
class PullRequest:
    """A pull request as `gh pr list` shows it."""

    number: int
    #: `OPEN`, `CLOSED` or `MERGED`.
    state: str
    url: str


def _number_of(url: str) -> int:
    """The pull request number at the end of its address."""
    tail = url.strip().rsplit("/", 1)[-1]
    if not tail.isdigit():
        raise GhError(f"gh printed no pull request address: {url.strip()[:80]!r}")
    return int(tail)


def find_pr(gh: Gh, repo: str, branch: str) -> PullRequest | None:
    """The pull request whose head is `branch` (the open one when several), or None."""
    fields = "number,state,url,mergedAt"
    listing = gh(["pr", "list", "--repo", repo, "--head", branch, "--state", "all", "--json", fields], None)
    try:
        items = json.loads(listing or "[]")
    except ValueError as error:
        raise GhError(f"gh pr list printed no JSON ({error})") from error
    found = [
        PullRequest(int(item["number"]), "MERGED" if item.get("mergedAt") else str(item["state"]).upper(), item["url"])
        for item in items
    ]
    found.sort(key=lambda pr: (pr.state != "OPEN", -pr.number))
    return found[0] if found else None


def _body_arg(body_file: Path, cwd: Path) -> str:
    """The body file as `gh` is given it: its bare name when it is in the folder `gh` runs in, else its whole path."""
    return body_file.name if body_file.parent == cwd else str(body_file)


def create_pr(
    gh: Gh, repo: str, base: str, branch: str, title: str, body_file: Path, attach: Sequence[str], cwd: Path
) -> PullRequest:
    """Open the pull request. `gh` runs in `cwd`, the folder of the pictures' `./shots/x.png` paths."""
    args = ["pr", "create", "--repo", repo, "--base", base, "--head", branch, "--title", title]
    args += ["--body-file", _body_arg(body_file, cwd)]
    for picture in attach:
        args += ["--attach", picture]
    lines = gh(args, cwd).strip().splitlines()
    url = lines[-1] if lines else ""
    return PullRequest(_number_of(url), "OPEN", url)


def edit_pr(gh: Gh, repo: str, number: int, title: str, body_file: Path, attach: Sequence[str], cwd: Path) -> None:
    """Replace the title and description of pull request `number`, uploading the pictures in `attach`."""
    args = ["pr", "edit", str(number), "--repo", repo, "--title", title, "--body-file", _body_arg(body_file, cwd)]
    for picture in attach:
        args += ["--attach", picture]
    gh(args, cwd)


def reopen_pr(gh: Gh, repo: str, number: int) -> None:
    """Reopen a closed pull request."""
    gh(["pr", "reopen", str(number), "--repo", repo], None)


def disable_auto_merge(gh: Gh, repo: str, number: int) -> None:
    """Turn auto-merge off (it was on when it should not be)."""
    gh(["pr", "merge", str(number), "--repo", repo, "--disable-auto"], None)


def enable_auto_merge(gh: Gh, repo: str, number: int, method: str) -> None:
    """Turn auto-merge on with the merge `method` the repository allows (`squash`, `merge` or `rebase`)."""
    gh(["pr", "merge", str(number), "--repo", repo, f"--{method}", "--auto"], None)


@dataclass(frozen=True)
class View:
    """What `gh pr view` shows of a pull request."""

    title: str
    body: str
    head: str
    auto_merge: bool


def view_pr(gh: Gh, repo: str, number: int) -> View:
    """Read the pull request back."""
    fields = "body,title,headRefOid,autoMergeRequest"
    try:
        data = json.loads(gh(["pr", "view", str(number), "--repo", repo, "--json", fields], None))
        return View(
            str(data["title"]),
            str(data["body"]).replace("\r\n", "\n"),
            str(data["headRefOid"]),
            data.get("autoMergeRequest") is not None,
        )
    except (ValueError, KeyError, TypeError) as error:
        raise GhError(f"gh pr view printed no pull request ({error})") from error


# --- git: rebase and push --------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class RebaseResult:
    """What a rebase did: `current` (already on the upstream), `rebased`, or `conflict` (aborted, nothing changed)."""

    status: str
    old_tip: str
    new_tip: str
    #: For a conflict, the paths git could not merge and the commit it stopped at.
    paths: tuple[str, ...] = ()
    commit: str = ""


def _rebasing(git: Git, worktree: Path) -> bool:
    """Whether a rebase is in progress in `worktree`."""
    for marker in ("rebase-merge", "rebase-apply"):
        path = out(git, worktree, "rev-parse", "--git-path", marker).strip()
        if (worktree / path).exists():
            return True
    return False


def rebase(git: Git, worktree: Path, upstream: str) -> RebaseResult:
    """Rebase the branch of `worktree` onto `upstream`, linear. A conflict is aborted so the worktree is as it was.

    ConfigError, with git's own message, when git fails for another reason: before it started (nothing to abort), or
    mid-way with no path left unmerged (no committer identity, a hook; aborted too), or when the abort itself fails (the
    worktree is then mid-rebase and the message says so).
    """
    tip = out(git, worktree, "rev-parse", "HEAD").strip()
    if git(worktree, ["merge-base", "--is-ancestor", upstream, "HEAD"]).returncode == 0:
        return RebaseResult("current", tip, tip)
    flags = ["-c", "rebase.autoSquash=false", "-c", "rebase.updateRefs=false"]
    done = git(worktree, [*flags, "rebase", "--no-rebase-merges", upstream])
    if done.returncode == 0:
        return RebaseResult("rebased", tip, out(git, worktree, "rev-parse", "HEAD").strip())
    if not _rebasing(git, worktree):
        raise ConfigError(f"git rebase {upstream} failed before it started: {(done.stderr or done.stdout).strip()}")
    try:
        paths = tuple(sorted(out(git, worktree, "diff", "--name-only", "--diff-filter=U").split()))
        stopped = git(worktree, ["rev-parse", "--verify", "--quiet", "REBASE_HEAD"]).stdout.strip()
    finally:
        aborted = git(worktree, ["rebase", "--abort"])
        if aborted.returncode != 0:
            raise ConfigError(f"git rebase --abort failed: {aborted.stderr.strip()}; the worktree is mid-rebase")
    if not paths:
        said = (done.stderr or done.stdout).strip() or f"exit {done.returncode}"
        raise ConfigError(f"git rebase {upstream} stopped with no conflict: {said}; aborted, the branch is as it was")
    return RebaseResult("conflict", tip, tip, paths, stopped)


def remote_tip(git: Git, worktree: Path, remote: str, branch: str) -> str:
    """The commit `branch` has on `remote` now, or an empty string when it does not exist there."""
    done = git(worktree, ["ls-remote", "--heads", remote, f"refs/heads/{branch}"])
    if done.returncode != 0:
        raise ConfigError(f"git ls-remote {remote}: {done.stderr.strip() or 'failed'}")
    line = done.stdout.strip().splitlines()[0] if done.stdout.strip() else ""
    return line.split()[0] if line else ""


@dataclass(frozen=True)
class PushResult:
    """What a push did: whether the branch is on the remote at the tip now, and why not."""

    ok: bool
    #: `pushed` or `current` (the remote branch already had the tip), else the reason.
    message: str


def push(git: Git, worktree: Path, remote: str, branch: str, tip: str, expected: str) -> PushResult:
    """Push `branch` with a lease on the tip the last run left there (`expected`; empty when it never pushed).

    A remote branch already at `tip` is left alone. Otherwise `--force-with-lease=<branch>:<expected>` is used, so the
    push is refused when somebody else moved the branch, whatever a fetch in between has shown. A branch that is gone
    from the remote (GitHub deleted it, as when a pull request was closed) is expected absent: the push can only create
    it, never overwrite anything.
    """
    there = remote_tip(git, worktree, remote, branch)
    if there == tip:
        return PushResult(True, "current")
    if not there:
        expected = ""
    lease = f"--force-with-lease={branch}:{expected}"
    done = git(worktree, ["push", lease, remote, f"{branch}:refs/heads/{branch}"])
    if done.returncode == 0:
        return PushResult(True, "pushed")
    now = remote_tip(git, worktree, remote, branch)
    if now and now != expected:
        recorded = f"the last pr open left it at {expected[:12]}" if expected else "no earlier pr open pushed it"
        return PushResult(
            False,
            f"{remote}/{branch} moved to {now[:12]} ({recorded}); fetch it and have the engineer rebase",
        )
    return PushResult(False, (done.stderr or done.stdout).strip() or "git push failed")


# --- what the last run published --------------------------------------------------------------------------------------


@dataclass
class Published:
    """What `pr open` remembers of the pull request in the task's scratch folder: the lease, the uploaded pictures."""

    number: int = 0
    url: str = ""
    #: The branch tip the last run pushed: the lease of the next push.
    tip: str = ""
    #: Picture hash to the address GitHub gave its upload, so a later run does not upload it again.
    images: dict[str, str] = field(default_factory=dict)
    engineer: dict[str, Any] = field(default_factory=dict)
    time: str = ""
    #: Whether `/progress pr-opened` was posted on the issues.
    progress_posted: bool = False


def read_published(folder: Path) -> Published:
    """The record in `folder`, or an empty one when there is none (or it is damaged: the lease is then simply empty)."""
    try:
        data = json.loads((folder / PUBLISHED_FILE).read_text(encoding="utf-8"))
        return Published(
            number=int(data["number"]),
            url=str(data["url"]),
            tip=str(data["tip"]),
            images={str(k): str(v) for k, v in dict(data.get("images", {})).items()},
            engineer=dict(data.get("engineer", {})),
            time=str(data.get("time", "")),
            progress_posted=bool(data.get("progress_posted", False)),
        )
    except (OSError, ValueError, KeyError, TypeError):
        return Published()


def write_published(folder: Path, published: Published) -> None:
    """Write the record, replacing the file whole."""
    data = {
        "number": published.number,
        "url": published.url,
        "tip": published.tip,
        "images": published.images,
        "engineer": published.engineer,
        "time": published.time,
        "progress_posted": published.progress_posted,
    }
    folder.mkdir(parents=True, exist_ok=True)
    temporary = folder / (PUBLISHED_FILE + ".tmp")
    temporary.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")
    temporary.replace(folder / PUBLISHED_FILE)
