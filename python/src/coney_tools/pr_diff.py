# SPDX-License-Identifier: GPL-3.0-or-later
"""The change a branch makes, as one hash: what a stamp is bound to (docs/guides/stamps.md#change).

A stamp bound to a commit would be stale the moment `pr open` rebases the branch, since a rebase rewrites every commit
id, and main moves all day. So `checked`, `reviewed` and `pr-opened` are bound to the *change*: a hash of what the
branch itself adds, removes and edits, which a clean rebase keeps. The hash covers, per file and in path order:

* the path, the status (`A`, `M` or `D`) and the mode before and after;
* for a text file the removed and added lines of `git diff -U0`, in order, without the hunk headers, so neither the
  line numbers (which move when main edits the same file elsewhere) nor the pre-image blob id (which changes with
  main) take part;
* for a binary file the SHA-256 of the file as the branch has it (`deleted` when the branch removes it).

Whitespace counts (a changed tab, a CRLF or a missing final newline is a changed line). Renames are off, so a moved
file is a deletion and an addition. The base is the merge base of the branch with its target, so a branch that is
behind main has the same hash as after the rebase. The hash is computed from git's objects alone, with the user's
`diff.*` and `core.quotepath` settings overridden, so every machine gets the same value. Standard library only.
"""

from __future__ import annotations

import hashlib
import subprocess
from dataclasses import dataclass
from pathlib import Path

from coney_tools.config import ConfigError

#: The first line of the hashed text: a change to how the text is built changes every hash, on purpose.
FORMAT = b"coney-change 1\n"
_GIT = ["-c", "core.quotepath=false", "-c", "diff.renames=false", "-c", "core.autocrlf=false"]
_DIFF = ["diff", "--no-renames", "--no-color", "--no-ext-diff", "--no-textconv", "--no-prefix", "--full-index"]


@dataclass(frozen=True)
class Change:
    """The change of a branch: where it starts, where it ends, the files it touches and the hash of it all."""

    #: The merge base of the branch with its target (a full commit hash).
    base: str
    #: The branch's tip (a full commit hash).
    tip: str
    #: The changed paths, sorted, as git names them (forward slashes).
    paths: tuple[str, ...]
    #: The SHA-256 of the canonical text, as hex: what a stamp's `covers.diff_sha256` holds.
    sha256: str


def _git(root: Path, *args: str) -> bytes:
    """Run git in `root` and return its stdout; ConfigError with git's message when it fails."""
    done = subprocess.run(["git", "-C", str(root), *_GIT, *args], capture_output=True, check=False)
    if done.returncode != 0:
        message = done.stderr.decode("utf-8", "replace").strip() or "failed"
        raise ConfigError(f"git {' '.join(args[:3])}: {message}")
    return done.stdout


def resolve(root: Path, commit: str) -> str:
    """The full hash of `commit` in the repository at `root`; ConfigError when it names nothing."""
    done = subprocess.run(
        ["git", "-C", str(root), "rev-parse", "--verify", "--quiet", f"{commit}^{{commit}}"],
        capture_output=True,
        check=False,
    )
    if done.returncode != 0:
        raise ConfigError(f"{commit}: not a commit of {root}")
    return done.stdout.decode("ascii").strip()


def merge_base(root: Path, target: str, tip: str) -> str:
    """The merge base of `tip` and `target` (a branch such as `origin/main`)."""
    return _git(root, "merge-base", target, tip).decode("ascii").strip()


def change_of(root: Path, target: str, tip: str) -> Change:
    """The change `tip` makes on top of its merge base with `target`, and its hash (`change_sha256`)."""
    tip_hash = resolve(root, tip)
    base = merge_base(root, resolve(root, target), tip_hash)
    paths, text = _canonical(root, base, tip_hash)
    return Change(base=base, tip=tip_hash, paths=paths, sha256=hashlib.sha256(text).hexdigest())


def change_sha256(root: Path, base: str, tip: str) -> str:
    """The hash of what `tip` changes since the commit `base` (normally the merge base; `change_of` finds it)."""
    return hashlib.sha256(_canonical(root, resolve(root, base), resolve(root, tip))[1]).hexdigest()


def _canonical(root: Path, base: str, tip: str) -> tuple[tuple[str, ...], bytes]:
    """The changed paths and the canonical text that is hashed."""
    raw = _git(root, *_DIFF, "--raw", "-z", base, tip).split(b"\0")
    entries = _raw_entries(raw)
    patch = _git(root, *_DIFF, "-U0", base, tip)
    sections = _sections(patch)
    if len(sections) != len(entries):
        raise ConfigError(f"git diff {base[:10]}..{tip[:10]}: {len(entries)} files but {len(sections)} patches")
    out = [FORMAT]
    for (path, status, old_mode, new_mode), section in zip(entries, sections, strict=True):
        out.append(b"file " + path + b"\n")
        out.append(f"status {status}\nmode {old_mode} {new_mode}\n".encode("ascii"))
        if section.binary:
            out.append(b"binary " + _post_image(root, tip, path) + b"\n")
        else:
            out.extend(section.lines)
    return tuple(path.decode("utf-8", "surrogateescape") for path, *_ in entries), b"".join(out)


def _raw_entries(raw: list[bytes]) -> list[tuple[bytes, str, str, str]]:
    """The `--raw -z` records as (path, status, old mode, new mode), in git's path order."""
    entries: list[tuple[bytes, str, str, str]] = []
    fields = [item for item in raw if item]
    index = 0
    while index < len(fields):
        header = fields[index].decode("ascii").lstrip(":").split(" ")
        old_mode, new_mode, status = header[0], header[1], header[4][:1]
        entries.append((fields[index + 1], status, old_mode, new_mode))
        index += 2
    return entries


@dataclass
class _Section:
    """One file's part of a patch: whether git calls it binary, and its added and removed lines."""

    binary: bool
    lines: list[bytes]


def _sections(patch: bytes) -> list[_Section]:
    """Split a `git diff -U0` patch into one section per file, keeping only the removed and added lines (and git's
    "no newline at end of file" marker, which is a change too)."""
    sections: list[_Section] = []
    in_hunk = False
    for line in patch.split(b"\n"):
        if line.startswith(b"diff --git "):
            sections.append(_Section(False, []))
            in_hunk = False
        elif not sections:
            continue
        elif line.startswith(b"Binary files ") or line.startswith(b"GIT binary patch"):
            sections[-1].binary = True
        elif line.startswith(b"@@"):
            in_hunk = True
        elif in_hunk and line[:1] in (b"+", b"-", b"\\"):
            sections[-1].lines.append(line + b"\n")
    return sections


def _post_image(root: Path, tip: str, path: bytes) -> bytes:
    """The SHA-256 of the file `path` as `tip` has it, as hex bytes, or `deleted`."""
    shown = subprocess.run(
        ["git", "-C", str(root), "cat-file", "blob", f"{tip}:{path.decode('utf-8', 'surrogateescape')}"],
        capture_output=True,
        check=False,
    )
    if shown.returncode != 0:
        return b"deleted"
    return hashlib.sha256(shown.stdout).hexdigest().encode("ascii")
