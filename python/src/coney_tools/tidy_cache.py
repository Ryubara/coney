# SPDX-License-Identifier: GPL-3.0-or-later
"""The clang-tidy result cache of `ci tidy-run --cache` (docs/guides/clang-tidy.md#cache).

clang-tidy spends tens of seconds on a source, almost all of it in the headers it reads, and a source whose every input
is unchanged reports the same findings. So each run of a source is kept as one entry, named by what it was run with:

* the **salt**: clang-tidy's version, the repository's `.clang-tidy` and the extra arguments (`TidyCache.open`);
* the source's **compile command** from the compile database (its directory, its arguments or command line).

An entry holds clang-tidy's exit code, every finding it printed (unfiltered: a change's line filter is applied after
the cache, so one entry serves any change) and the **files it read**: the dependency file clang-tidy writes when asked
(`depfile_arguments`, system headers included), each with its SHA-256. `get` gives the entry back only while every one
of those files still has that hash, as ccache's direct mode does; a file that changed or went missing is a miss and
clang-tidy runs again. A failure of the tool (an exit code with no finding) is never kept, nor a source that does not
compile (ci_tidy.py).

A known limit: the dependency file names only the files that were read, so a **newly added** header that shadows an
include (a file of the same name in an earlier include directory) or that makes a `__has_include` true is not seen,
and the kept outcome stands until another input of the source changes (docs/guides/clang-tidy.md#cache).

The folder is plain files, one JSON per entry, so CI keeps it between runs with actions/cache. Paths are written
relative to the checkout when they are inside it, so an entry made in one checkout reads the same in another.
"""

from __future__ import annotations

import hashlib
import json
import re
import threading
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import TYPE_CHECKING, Any

if TYPE_CHECKING:
    from coney_tools.ci_tidy import Finding

#: The format of an entry; an entry of another version is a miss.
VERSION = 2


def depfile_arguments(depfile: Path) -> list[str]:
    """The arguments that make clang-tidy write the files it reads to `depfile`, system headers included.

    They are the compiler front end's own options, each joined to its `-Xclang=`, so they mean the same to clang and
    to clang-cl. Joined, because clang tooling strips dependency options from a GCC/Clang-style command before it
    runs: every word that starts with `-M`, and the word after `-MT`. A separate `-Xclang -MT -Xclang tidy` would
    lose its `-MT` and leave `-Xclang tidy`, an input file named `tidy`; `-Xclang=-MT` starts with neither.
    test_tidy_cache.py runs the real clang-tidy on a clang++ and a cl.exe compile database to prove both.
    """
    words = ["-dependency-file", str(depfile), "-MT", "tidy", "-sys-header-deps"]
    return [f"-Xclang={word}" for word in words]


def read_depfile(text: str) -> list[str]:
    """The files a Make-style dependency file names, in order, each once (the target before the colon is not one).

    Paths with spaces have them escaped with a backslash; a line ending in a lone backslash continues on the next.
    """
    body = re.sub(r"\\\r?\n", " ", text)
    _, _, rest = body.partition(": ")
    found: list[str] = []
    for token in re.findall(r"(?:\\ |[^\s])+", rest):
        path = token.replace("\\ ", " ")
        if path not in found:
            found.append(path)
    return found


@dataclass(frozen=True)
class Entry:
    """One source's outcome: clang-tidy's exit code and every finding it printed."""

    code: int
    findings: tuple[Finding, ...]


class TidyCache:
    """The cache in `folder` for one salt, against the compile database `commands` (source -> its entry)."""

    def __init__(self, folder: Path, root: Path, salt: str, commands: dict[str, dict[str, Any]]) -> None:
        self.folder = folder
        self.root = root
        self.salt = salt
        self.commands = commands
        self._hashes: dict[str, str | None] = {}
        self._lock = threading.Lock()
        #: How many lookups found an entry, and how many did not.
        self.hits = 0
        self.misses = 0

    @classmethod
    def open(
        cls, folder: Path, root: Path, build_dir: Path, version: str, config: str, extra: tuple[str, ...] = ()
    ) -> TidyCache:
        """The cache in `folder` for clang-tidy `version` with the configuration text `config` (the `.clang-tidy`
        file) and `extra` arguments, against the compile database of `build_dir`."""
        folder.mkdir(parents=True, exist_ok=True)
        salt = hashlib.sha256(json.dumps([VERSION, version, config, list(extra)]).encode("utf-8")).hexdigest()
        return cls(folder, root, salt, load_commands(build_dir, root))

    def _key(self, source: str) -> str | None:
        """The entry's name for `source`, or None when the compile database has no command for it."""
        command = self.commands.get(source)
        if command is None:
            return None
        fields = {name: command.get(name) for name in ("directory", "arguments", "command")}
        text = json.dumps([self.salt, source, fields], sort_keys=True)
        return hashlib.sha256(text.encode("utf-8")).hexdigest()

    def _relative(self, path: str) -> str:
        """`path` relative to the checkout when it is inside it (forward slashes), else as written."""
        text = path.replace("\\", "/")
        base = str(self.root).replace("\\", "/").rstrip("/") + "/"
        return text[len(base) :] if text.casefold().startswith(base.casefold()) else text

    def _hash(self, path: str) -> str | None:
        """The SHA-256 of the file at `path` (relative: in the checkout), remembered for the run; None when it is
        gone."""
        with self._lock:
            if path in self._hashes:
                return self._hashes[path]
        full = Path(path) if Path(path).is_absolute() else self.root / path
        try:
            digest: str | None = hashlib.sha256(full.read_bytes()).hexdigest()
        except OSError:
            digest = None
        with self._lock:
            self._hashes[path] = digest
        return digest

    def get(self, source: str) -> Entry | None:
        """The kept outcome of `source`, or None when there is none or a file it read has changed."""
        from coney_tools.ci_tidy import Finding  # ci_tidy imports this module

        key = self._key(source)
        data: dict[str, Any] | None = None
        if key is not None:
            try:
                data = json.loads((self.folder / f"{key}.json").read_text(encoding="utf-8"))
            except (OSError, ValueError):
                data = None
        fresh = (
            isinstance(data, dict)
            and data.get("version") == VERSION
            and all(self._hash(path) == digest for path, digest in dict(data.get("files", {})).items())
        )
        with self._lock:
            self.hits += fresh
            self.misses += not fresh
        if not fresh or data is None:
            return None
        return Entry(int(data["code"]), tuple(Finding(**item) for item in data["findings"]))

    def put(self, source: str, entry: Entry, depfile: Path) -> None:
        """Keep `entry` for `source` with the files clang-tidy read (`depfile`); nothing is kept when the dependency
        file is missing or names a file that cannot be read, or the source has no compile command."""
        key = self._key(source)
        try:
            text = depfile.read_text(encoding="utf-8", errors="replace")
        except OSError:
            return
        files: dict[str, str] = {}
        for path in read_depfile(text):
            relative = self._relative(path)
            digest = self._hash(relative)
            if digest is None:
                return
            files[relative] = digest
        if key is None or not files:
            return
        data = {"version": VERSION, "code": entry.code, "findings": [asdict(item) for item in entry.findings]}
        data["files"] = files
        target = self.folder / f"{key}.json"
        partial = target.with_name(target.name + ".part")
        partial.write_text(json.dumps(data, sort_keys=True), encoding="utf-8")
        partial.replace(target)


def load_commands(build_dir: Path, root: Path) -> dict[str, dict[str, Any]]:
    """The compile database of `build_dir` by source, each named relative to the checkout `root` (forward slashes);
    empty when there is none."""
    try:
        entries = json.loads((build_dir / "compile_commands.json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}
    base = str(root).replace("\\", "/").rstrip("/") + "/"
    found: dict[str, dict[str, Any]] = {}
    for entry in entries if isinstance(entries, list) else []:
        if not isinstance(entry, dict) or "file" not in entry:
            continue
        file = str(entry["file"]).replace("\\", "/")
        if not Path(file).is_absolute():
            file = str(entry.get("directory", "")).replace("\\", "/").rstrip("/") + "/" + file
        name = file[len(base) :] if file.casefold().startswith(base.casefold()) else file
        found[name] = entry
    return found
