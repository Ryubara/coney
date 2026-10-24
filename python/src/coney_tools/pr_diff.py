# SPDX-License-Identifier: GPL-3.0-or-later
"""The change a branch makes, as one hash: what a stamp is bound to (docs/guides/stamps.md#change).

A stamp bound to a commit would be stale the moment `pr open` rebases the branch, since a rebase rewrites every commit
id, and main moves all day. So `checked`, `reviewed` and `pr-opened` are bound to the *change*: a hash of what the
branch itself adds, removes and edits, which a clean rebase keeps. The hash covers, per changed file, in git's path
order (the byte order of the path):

* the path, the status (`A`, `M`, `D` or `T`, a change of file type) and the mode before and after;
* for a text file the removed and added lines of `git diff -U0`, in order, without the hunk headers, so neither the
  line numbers (which move when main edits the same file elsewhere) nor the pre-image blob id (which changes with
  main) take part;
* for a binary file the SHA-256 of the file as the branch has it (`deleted` when the branch removes it).

Whitespace counts (a changed tab, a CRLF or a missing final newline is a changed line). Renames are off, so a moved
file is a deletion and an addition. The base is the merge base of the branch with its target, so a target that is
older than the branch's own base would pull main's changes into the hash: fetch first.

The same commits give the same hash on every machine. The diff runs with an explicit algorithm (`myers`), the indent
heuristic on, an empty order file, no context, no external diff, no textconv, no colour, no renames, no prefixes, the
short submodule form and the user's `GIT_DIFF_OPTS` removed, and `-a`, so a `.gitattributes` rule (`-diff`, `binary`,
`diff=driver`) never changes what is hashed. What is binary is decided here, not by git: a NUL byte in the first 8000
bytes of the file (git's own test without attributes). Standard library only.
"""

from __future__ import annotations

import hashlib
import os
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path

from coney_tools.config import ConfigError

#: The first line of the hashed text: a change to how the text is built changes every hash, on purpose.
FORMAT = b"coney-change 1\n"
#: How much of a file is searched for the NUL byte that makes it binary (git's own limit).
BINARY_PROBE = 8000
#: Overrides of the user's settings that `git diff` reads, so every machine builds the same text.
_GIT = [
    "-c",
    "core.quotepath=false",
    "-c",
    "core.autocrlf=false",
    "-c",
    "diff.renames=false",
    "-c",
    "diff.algorithm=myers",
    "-c",
    "diff.indentHeuristic=true",
    "-c",
    "diff.interHunkContext=0",
    "-c",
    "diff.submodule=short",
    "-c",
    "diff.ignoreSubmodules=none",
    "-c",
    "diff.colorMoved=false",
    "-c",
    "diff.relative=false",
    "-c",
    "diff.noprefix=true",
    "-c",
    "diff.mnemonicPrefix=false",
    "-c",
    "color.ui=false",
]
#: The options of every diff: what is compared and how it is printed (see the module docstring).
_DIFF = [
    "diff",
    "--no-renames",
    "--no-color",
    "--no-ext-diff",
    "--no-textconv",
    "--no-prefix",
    "--no-relative",
    "--no-color-moved",
    "--full-index",
    "--text",
    "--diff-algorithm=myers",
    "--indent-heuristic",
    "--inter-hunk-context=0",
    "--submodule=short",
    "--ignore-submodules=none",
]


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


def _environment() -> dict[str, str]:
    """The environment of a git call: the user's, without the variables that change a diff."""
    return {k: v for k, v in os.environ.items() if k not in ("GIT_DIFF_OPTS", "GIT_EXTERNAL_DIFF")}


def _git(root: Path, *args: str, stdin: bytes | None = None) -> bytes:
    """Run git in `root` and return its stdout; ConfigError with git's message when it fails."""
    done = subprocess.run(
        ["git", "-C", str(root), *_GIT, *args], capture_output=True, check=False, input=stdin, env=_environment()
    )
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
    """The change `tip` makes on top of its merge base with `target`, and its hash (`change_sha256`). `target` must be
    at least as new as the branch's own base (fetch first), or main's own changes since are part of the change."""
    tip_hash = resolve(root, tip)
    base = merge_base(root, resolve(root, target), tip_hash)
    paths, text = _canonical(root, base, tip_hash)
    return Change(base=base, tip=tip_hash, paths=paths, sha256=hashlib.sha256(text).hexdigest())


def change_sha256(root: Path, base: str, tip: str) -> str:
    """The hash of what `tip` changes since the commit `base` (normally the merge base; `change_of` finds it)."""
    return hashlib.sha256(_canonical(root, resolve(root, base), resolve(root, tip))[1]).hexdigest()


@dataclass(frozen=True)
class _Entry:
    """One line of `git diff --raw`: a changed path with its status, modes and blob ids."""

    path: bytes
    status: str
    old_mode: str
    new_mode: str
    old_sha: str
    new_sha: str


def _canonical(root: Path, base: str, tip: str) -> tuple[tuple[str, ...], bytes]:
    """The changed paths and the canonical text that is hashed."""
    with tempfile.TemporaryDirectory(prefix="coney-order-") as folder:
        order = Path(folder) / "empty"
        order.write_bytes(b"")  # an empty order file: git's own path order, whatever the user's diff.orderFile says
        options = [*_DIFF, f"-O{order}"]
        entries = _raw_entries(_git(root, *options, "--raw", "-z", base, tip))
        patch = _git(root, *options, "-U0", base, tip)
    sections = _sections(patch)
    # A change of file type (status T) is printed as two patches, a deletion and an addition, for its one raw entry.
    wanted = sum(2 if entry.status == "T" else 1 for entry in entries)
    if len(sections) != wanted:
        raise ConfigError(f"git diff {base[:10]}..{tip[:10]}: {wanted} patches expected but {len(sections)} found")
    per_entry: list[list[bytes]] = []
    taken = 0
    for entry in entries:
        count = 2 if entry.status == "T" else 1
        per_entry.append([line for section in sections[taken : taken + count] for line in section])
        taken += count
    blobs = _blobs(root, [e.new_sha if e.status != "D" else e.old_sha for e in entries if e.new_mode != "160000"])
    out = [FORMAT]
    for entry, lines in zip(entries, per_entry, strict=True):
        out.append(b"file " + entry.path + b"\n")
        out.append(f"status {entry.status}\nmode {entry.old_mode} {entry.new_mode}\n".encode("ascii"))
        data = blobs.get(entry.new_sha if entry.status != "D" else entry.old_sha, b"")
        if b"\0" in data[:BINARY_PROBE]:
            image = b"deleted" if entry.status == "D" else hashlib.sha256(data).hexdigest().encode("ascii")
            out.append(b"binary " + image + b"\n")
        else:
            out.extend(lines)
    return tuple(e.path.decode("utf-8", "surrogateescape") for e in entries), b"".join(out)


def _raw_entries(raw: bytes) -> list[_Entry]:
    """The `--raw -z` records, in git's path order."""
    fields = [item for item in raw.split(b"\0") if item]
    entries: list[_Entry] = []
    for index in range(0, len(fields) - 1, 2):
        header = fields[index].decode("ascii").lstrip(":").split(" ")
        entries.append(_Entry(fields[index + 1], header[4][:1], header[0], header[1], header[2], header[3]))
    return entries


def _sections(patch: bytes) -> list[list[bytes]]:
    """Split a `git diff -U0` patch into one list per file of its removed and added lines (and git's "no newline at end
    of file" marker, which is a change too); the header lines before the first hunk are left out."""
    sections: list[list[bytes]] = []
    in_hunk = False
    for line in patch.split(b"\n"):
        if line.startswith(b"diff --git "):
            sections.append([])
            in_hunk = False
        elif not sections:
            continue
        elif line.startswith(b"@@"):
            in_hunk = True
        elif in_hunk and line[:1] in (b"+", b"-", b"\\"):
            sections[-1].append(line + b"\n")
    return sections


def _blobs(root: Path, shas: list[str]) -> dict[str, bytes]:
    """The content of each blob, from one `git cat-file --batch`; an object git cannot show is left out."""
    if not shas:
        return {}
    shown = _git(root, "cat-file", "--batch", stdin=("\n".join(shas) + "\n").encode("ascii"))
    found: dict[str, bytes] = {}
    position = 0
    for sha in shas:
        end = shown.index(b"\n", position)
        header = shown[position:end].split(b" ")
        position = end + 1
        if len(header) == 3 and header[1] == b"blob":
            size = int(header[2])
            found[sha] = shown[position : position + size]
            position += size + 1
    return found
