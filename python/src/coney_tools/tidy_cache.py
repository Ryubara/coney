# SPDX-License-Identifier: GPL-3.0-or-later
"""The clang-tidy result cache every `coney-tools check changed` shares (docs/guides/building.md#clang-tidy-cache).

A gate tidies every `.cpp` its change reaches, often 100 to 280 files at about a minute each; a rerun after a one-line
fix used to pay all of it again, and a file another worktree already found clean was checked once per worktree. The
cache remembers each file clean clang-tidy run by a key of everything that decides the result, so the same file in
the same state is checked once on the machine:

- the setup (`setup_text`): clang-tidy's `--version`, the checks and extra arguments the gate passes, the repository's
  `.clang-tidy` and `cmake/deps.cmake` (the pinned third-party versions, whose headers the key does not read), and the
  environment variables that add system include folders (HEADER_ENV: MSVC's and the Windows SDK's through `INCLUDE`);
- the file's compile command with the checkout's own path taken out (so worktrees share entries), without a compiler
  launcher or an `@response` file (CMake's module maps exist only after a build), and the size and modification time
  of the compiler it names (an update in place, or a short 8.3 path that hides the version folder, changes them);
- the file's path in the checkout, and the contents of the file and of every header inside the checkout it reaches
  through `#include`, transitively. Every include line counts, whatever `#if` it sits in: that over-counts (an edit
  can miss a hit it did not need to), never under-counts. Headers outside the checkout (the standard library, the
  compiler's own) and under a `_deps` folder (third-party, pinned by deps.cmake) are not read; the compile command
  names the compiler's folder, which changes with its version.

Only a clean result is stored, as an empty-ish file named by the key; a failure is always rerun, as its findings are
what the gate prints. The gate works a file's key out again when its run ends and stores the result only if the key is
the same, so a file edited while it waited or ran is never stored as clean under its old contents. An entry not used
for MAX_AGE_SECONDS is pruned.

Limits, none of which Coney's build uses today: the contents of `@response` files are not read (CMake writes them only
for module maps); a forced include (`-include`, `/FI`), `-idirafter` and `#include MACRO` are not followed; and the
headers in a system include folder count by the folder's name, not their contents.
"""

from __future__ import annotations

import contextlib
import hashlib
import json
import os
import re
import time
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from coney_tools import local_gate
from coney_tools.config import ConfigError, load_config, local_config_file, workspace_root

#: Bumped when what goes into a key changes, so entries made by an older rule are never read.
FORMAT = "coney-tidy-cache 1"
#: The cache's folder in the workspace (beside repos/, worktrees/, cache/ccache), when `tidy_cache_dir` is not set.
DEFAULT_DIR = Path("cache") / "clang-tidy"
#: An entry not written or hit for this long (30 days) is removed by `prune`.
MAX_AGE_SECONDS = 30 * 24 * 3600.0
#: Files of the repository that change what clang-tidy reports for any file (besides the file and its headers).
SETUP_FILES: tuple[str, ...] = (".clang-tidy", "cmake/deps.cmake")
#: Environment variables that add system include folders for the compiler driver clang-tidy imitates.
HEADER_ENV: tuple[str, ...] = ("INCLUDE", "EXTERNAL_INCLUDE", "CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH")
#: A leftover `.tmp` of `store` (a run killed between write and rename) older than this is removed by `prune`.
TEMPORARY_MAX_AGE_SECONDS = 3600.0
#: An include directive, quoted or angled.
_INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*([<"])([^>"\n]+)[>"]', re.MULTILINE)
#: Compiler options that add an include folder, with the folder in the same argument or (bare) in the next one.
_INCLUDE_FLAGS: tuple[str, ...] = ("-I", "/I", "-external:I", "/external:I", "-isystem", "-iquote", "-imsvc")


def setup_text(
    root: Path, version: str, checks: str, extra: Sequence[str], environment: Mapping[str, str] | None = None
) -> str:
    """The part of every key that is the same for all files of one run: the format, clang-tidy's version text, the
    checks and extra arguments, the contents of SETUP_FILES (a missing one counts as missing) and the HEADER_ENV
    variables of `environment` (default: this process's)."""
    environment = os.environ if environment is None else environment
    parts = [FORMAT, version.strip(), checks, *extra]
    for name in SETUP_FILES:
        path = root / name
        parts.append(f"{name}:{_digest(path.read_bytes()) if path.is_file() else 'missing'}")
    parts.extend(f"{name}={environment.get(name, '')}" for name in HEADER_ENV)
    return "\n".join(parts)


def _comparable(path: Path) -> str:
    """A path in the platform's case rules with forward slashes (normcase turns them back on Windows)."""
    return os.path.normcase(str(path)).replace("\\", "/")


def _digest(data: bytes) -> str:
    """A SHA-256 of some bytes, as hex."""
    return hashlib.sha256(data).hexdigest()


class KeyMaker:
    """Makes the cache keys of one run's files; each header is read and hashed once however many files reach it."""

    def __init__(self, root: Path, setup: str) -> None:
        """`root` is the checkout; `setup` is `setup_text` of this run."""
        self.root = Path(os.path.realpath(root))
        self.setup = setup
        self._root_text = _comparable(self.root)
        #: Per file (comparable path): its digest and the include names it writes, read once.
        self._files: dict[str, tuple[str, list[tuple[str, str]]]] = {}
        #: Where each include lands, per (kind, name, includer's folder for a quoted one, include folders): a header
        #: reached by hundreds of files is looked up on disk once.
        self._found: dict[tuple[str, str, str, tuple[Path, ...]], Path | None] = {}
        #: Each path's checkout-relative name, or None outside it (`_inside`), worked out once.
        self._names: dict[Path, str | None] = {}
        #: Each compiler's size and modification time (`_stamp`), looked up once.
        self._stamps: dict[str, str] = {}

    def key(self, source: str, entry: dict[str, Any]) -> str:
        """The key of `source` (relative to the checkout) compiled by the compile database `entry`."""
        arguments = [part for part in local_gate.entry_command(entry) if not part.strip('"').startswith("@")]
        base = Path(str(entry.get("directory", self.root)))
        folders = self._include_folders(arguments, base)
        command = self._portable(" ".join(arguments))
        compiler = self._stamp(Path(arguments[0].strip('"'))) if arguments else "none"
        reached = self._closure(self.root / source, folders)
        lines = [self.setup, source, command, compiler]
        lines.extend(f"{name} {digest}" for name, digest in sorted(reached.items()))
        return _digest("\n".join(lines).encode("utf-8"))

    def _stamp(self, compiler: Path) -> str:
        """The size and modification time of the compiler a command names ("missing" when it is not a file there)."""
        text = str(compiler)
        if text not in self._stamps:
            try:
                status = compiler.stat()
                self._stamps[text] = f"{status.st_size}:{status.st_mtime_ns}"
            except OSError:
                self._stamps[text] = "missing"
        return self._stamps[text]

    def _portable(self, text: str) -> str:
        """`text` with forward slashes and the checkout's own path replaced, so two worktrees write it the same."""
        slashed = text.replace("\\", "/")
        pattern = re.compile(re.escape(self.root.as_posix()), re.IGNORECASE if os.name == "nt" else 0)
        return pattern.sub("<root>", slashed)

    def _include_folders(self, arguments: list[str], base: Path) -> list[Path]:
        """The include folders a command names, in order, relative ones resolved against the entry's folder."""
        folders: list[Path] = []
        pending = False
        for argument in arguments:
            bare = argument.strip('"')
            if pending:
                folders.append(base / bare)
                pending = False
                continue
            for flag in _INCLUDE_FLAGS:
                if bare == flag:
                    pending = True
                elif bare.startswith(flag):
                    folders.append(base / bare[len(flag) :].strip('"'))
                else:
                    continue
                break
        return folders

    def _inside(self, path: Path) -> str | None:
        """The checkout-relative name of `path` when it is in the checkout outside a `_deps` folder; else None."""
        if path not in self._names:
            text = _comparable(Path(os.path.realpath(path)))
            relative = text[len(self._root_text) + 1 :] if text.startswith(self._root_text + "/") else None
            self._names[path] = None if relative is None or "_deps" in relative.split("/") else relative
        return self._names[path]

    def _read(self, path: Path) -> tuple[str, list[tuple[str, str]]]:
        """A file's digest and its include directives (kind, name), read once per run. A file that cannot be read
        (locked, or deleted mid-run) counts by its error and includes nothing: its key differs from any key made when
        it was readable, and clang-tidy itself reports the problem."""
        name = self._inside(path) or str(path)
        if name not in self._files:
            try:
                data = path.read_bytes()
            except OSError as error:
                self._files[name] = (f"unreadable: {error.strerror or error}", [])
                return self._files[name]
            text = data.decode("utf-8", errors="replace")
            self._files[name] = (_digest(data), [(m.group(1), m.group(2).strip()) for m in _INCLUDE.finditer(text)])
        return self._files[name]

    def _resolve(self, kind: str, name: str, includer: Path, folders: list[Path]) -> Path | None:
        """Where an include lands: beside the includer first for a quoted one, then each include folder in order."""
        memo = (kind, name, str(includer.parent) if kind == '"' else "", tuple(folders))
        if memo not in self._found:
            candidates = ([includer.parent] if kind == '"' else []) + folders
            self._found[memo] = next((folder / name for folder in candidates if (folder / name).is_file()), None)
        return self._found[memo]

    def _closure(self, source: Path, folders: list[Path]) -> dict[str, str]:
        """Checkout-relative name to digest of the source and every checkout header it reaches."""
        if not source.is_file():
            raise ConfigError(f"{source}: no such file to tidy")
        reached: dict[str, str] = {}
        queue = [source]
        while queue:
            path = queue.pop()
            relative = self._inside(path)
            if relative is None or relative in reached:
                continue
            digest, includes = self._read(path)
            reached[relative] = digest
            for kind, name in includes:
                found = self._resolve(kind, name, path, folders)
                if found is not None:
                    queue.append(found)
        return reached


@dataclass(frozen=True)
class TidyCache:
    """The machine's clang-tidy cache: one small file per clean result, in two-character subfolders."""

    directory: Path

    def path(self, key: str) -> Path:
        """The entry file of a key."""
        return self.directory / key[:2] / f"{key}.json"

    def hit(self, key: str) -> bool:
        """Whether the file with this key was found clean before; a hit refreshes the entry's age."""
        path = self.path(key)
        if not path.is_file():
            return False
        # Another run may be pruning or rewriting it; the hit stands either way.
        with contextlib.suppress(OSError):
            os.utime(path)
        return True

    def store(self, key: str, source: str) -> None:
        """Remember a clean result (written to a private file, then renamed: a reader never sees half of one)."""
        path = self.path(key)
        try:
            path.parent.mkdir(parents=True, exist_ok=True)
            temporary = path.with_name(f".{path.name}.{os.getpid()}.tmp")
            temporary.write_text(json.dumps({"file": source, "at": time.time()}), encoding="utf-8")
            temporary.replace(path)
        except OSError:
            pass  # a cache that cannot be written only costs the next run time

    def prune(self, now: float | None = None) -> int:
        """Remove the entries not used for MAX_AGE_SECONDS, and the `.tmp` leftovers of killed runs older than
        TEMPORARY_MAX_AGE_SECONDS; returns how many went."""
        now = time.time() if now is None else now
        found = [(path, now - MAX_AGE_SECONDS) for path in self._files("*/*.json")]
        found += [(path, now - TEMPORARY_MAX_AGE_SECONDS) for path in self._files("*/.*.tmp")]
        removed = 0
        for path, limit in found:
            try:
                if path.stat().st_mtime < limit:
                    path.unlink()
                    removed += 1
            except OSError:
                continue
        return removed

    def _files(self, pattern: str) -> list[Path]:
        """The cache's files matching a glob pattern (none while the folder does not exist)."""
        return list(self.directory.glob(pattern)) if self.directory.is_dir() else []


def default_cache(root: Path) -> TidyCache:
    """The cache of the checkout at `root`: `tidy_cache_dir` of coney.local.toml (this checkout's, else the main
    checkout's), else `cache/clang-tidy` in the workspace, beside the compiler cache."""
    workspace = workspace_root(root)
    try:
        config = load_config(root) if local_config_file(root) else None
    except ConfigError:
        config = None
    configured = config.paths.get("tidy_cache_dir") if config else None
    return TidyCache(configured or workspace / DEFAULT_DIR)
