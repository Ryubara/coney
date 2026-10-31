# SPDX-License-Identifier: GPL-3.0-or-later
"""Which of the repository's scoped path classes each test reads, and the `reads` marker that must say so.

`task check` runs only some of the Python tests for a change that touches only the classes of
`proof.pytest.scoped` in `.github/conventions.yml` (docs and engine files): the tests marked
`@pytest.mark.reads("<class>", ...)` with one of the classes the change touches (`--reads <class>`, conftest.py). That
choice is safe only while every test that reads a file of such a class carries the marker, so this module watches:
an audit hook (`sys.addaudithook`) sees every file a test opens and every folder it lists, from the start of its setup
to the end of its teardown, and the reads a module makes while it is imported count for each of its tests. A read of
the checkout's own files whose class is scoped, by a test whose marker does not name that class, fails the test at
teardown with the paths and the marker to add, so CI, which runs the whole suite on every ready pull request, refuses
a test that a scoped local run would have skipped (docs/guides/coney-tools-repo.md#task-check).

The audit events cover opening and listing only: CPython raises none for `stat`, `exists`, `is_file` or `getsize`, so
`install` also wraps the `os.stat`/`os.lstat`/`os.access` functions and the `os.path` existence, kind, size and time
checks (`STAT_CALLS`), which `pathlib` calls through, and a test that only checks a file exists is seen too.

What it cannot see, and a test that has one of these is marked by hand:
- a child process's reads: a test that starts a program inside the checkout (git, bash, coney-tools) with no folder of
  its own reads whatever that program does;
- a stat made through a reference taken before `install` ran (`from os.path import exists` in a module imported by the
  conftest or by pytest itself), through `DirEntry.stat()`/`is_file()` of a listing (the listing is seen), or by a C
  extension;
- the reads of a cache or a shared helper, which count for the first test that triggers them (and, in a parallel run,
  for whichever test a worker happens to give it to), not for the others that rely on it.

`CONEY_TEST_READS_LOG=<folder>` also writes one JSON line per test (its id, the classes it read and a few paths) into a
file per process there, to review the markers against what the tests do.
"""

from __future__ import annotations

import functools
import json
import os
import sys
from collections.abc import Callable, Iterable
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from coney_tools.conventions import Conventions, load_conventions

#: The checkout these tests belong to.
REPO = Path(__file__).resolve().parents[2]
#: The marker a test names its scoped classes with.
MARKER = "reads"
#: The audit events that read a file or list a folder; their first argument is the path.
EVENTS = frozenset({"open", "os.listdir", "os.scandir", "glob.glob"})
#: The calls that look at a file without opening it, which CPython raises no audit event for: (module, function names).
#: `pathlib` reaches them as `os.stat(...)` and `os.path.exists(...)`, looked up at call time, so wrapping them there
#: sees `Path.exists`, `is_file`, `is_dir`, `stat` and the rest too.
STAT_CALLS: tuple[tuple[object, tuple[str, ...]], ...] = (
    (os, ("stat", "lstat", "access")),
    (
        os.path,
        (
            "exists",
            "lexists",
            "isfile",
            "isdir",
            "islink",
            "isjunction",
            "getsize",
            "getmtime",
            "getatime",
            "getctime",
        ),
    ),
)
#: The audit event of a child process, and the key its folder and program are noted under (in the log only).
SPAWN = "subprocess.Popen"
SPAWNED = "spawned"
#: The class `paths.ci_class` gives a path no class names.
UNKNOWN = "unknown"
#: The most paths a failure or a log line names per class.
SHOWN = 3


@dataclass(frozen=True)
class Scope:
    """The rules this module applies: the scoped classes and the prefixes whose changes run the whole suite, and how a
    path is classed (`paths` of the conventions)."""

    conventions: Conventions
    scoped: tuple[str, ...]
    whole_when: tuple[str, ...]

    def class_of(self, relative: str) -> str | None:
        """The scoped class of a repository-relative path, or None when a change to it runs the whole suite anyway (so
        no marker is needed for reading it)."""
        if not relative or relative.startswith(self.whole_when):
            return None
        found = self.conventions.paths.ci_class(relative)
        if found == UNKNOWN:
            # A top folder (`docs`, `src`): the class patterns end in a slash, which a listing of the folder lacks.
            found = self.conventions.paths.ci_class(relative.rstrip("/") + "/")
        return found if found in self.scoped else None


def load_scope(root: Path = REPO) -> Scope:
    """The scope rules of the checkout's own `.github/conventions.yml`."""
    conventions = load_conventions(root)
    rules = conventions.proof.pytest
    return Scope(conventions, rules.scoped, rules.whole_when)


@dataclass
class Recorder:
    """The reads of the test running now (None between tests) and those each module made while it was imported."""

    scope: Scope
    root: str = field(default_factory=lambda: os.path.normcase(str(REPO)))
    current: dict[str, set[str]] | None = None
    by_module: dict[str, dict[str, set[str]]] = field(default_factory=dict)
    #: The class of each path seen, remembered: the hook sees the same paths again and again.
    classes: dict[str, str | None] = field(default_factory=dict)

    def hook(self, event: str, args: tuple[object, ...]) -> None:
        """The audit hook: note a scoped read while a test or a module import is being watched. It must never raise."""
        if self.current is None or not args:
            return
        if event == SPAWN:
            self._spawned(args)
            return
        if event in EVENTS:
            self.note(args[0])

    def note(self, target: object) -> None:
        """Note a path being read (opened, listed or looked at) when it is a scoped file of the checkout. It must never
        raise: it runs inside the call it watches."""
        if self.current is None:
            return
        try:
            if isinstance(target, int) or target is None:
                return
            text = os.fsdecode(os.fspath(target))  # type: ignore[call-overload]
            relative = self._relative(text)
            if relative is None:
                return
            if relative not in self.classes:
                self.classes[relative] = self.scope.class_of(relative)
            found = self.classes[relative]
            if found is not None:
                self.current.setdefault(found, set()).add(relative)
        except Exception:  # a hook that raises breaks the call it watches
            return

    def _spawned(self, args: tuple[object, ...]) -> None:
        """Note a child process started inside the checkout (its reads are not seen), for the log."""
        try:
            folder = args[2] if len(args) > 2 and args[2] is not None else Path.cwd()
            relative = self._relative(os.fsdecode(os.fspath(folder)))  # type: ignore[call-overload]
            if relative is not None and self.current is not None:
                command = args[1]
                words = [command] if isinstance(command, str | bytes) else list(command)  # type: ignore[call-overload]
                name = Path(os.fsdecode(words[0])).name if words else "?"
                self.current.setdefault(SPAWNED, set()).add(f"{relative or '.'}: {name}")
        except Exception:  # an audit hook that raises breaks the call it watches
            return

    def _relative(self, text: str) -> str | None:
        """`text` relative to the checkout, with forward slashes, or None when it is outside it."""
        # os.path, not Path.resolve: the hook runs on every open and must not touch the disk (resolve reads links).
        absolute = os.path.abspath(text)  # noqa: PTH100
        full = os.path.normcase(absolute)
        if full == self.root:
            return ""
        if not full.startswith(self.root + os.sep):
            return None
        relative = absolute[len(self.root) + 1 :].replace("\\", "/")
        return _source_of(relative)

    def start(self, module: str | None = None) -> None:
        """Start watching: a test (with its module's import reads), or the import of `module` itself."""
        self.current = {kind: set(paths) for kind, paths in self.by_module.get(module or "", {}).items()}

    def stop(self) -> dict[str, set[str]]:
        """Stop watching and return what was read."""
        reads, self.current = self.current or {}, None
        return reads


def _source_of(relative: str) -> str:
    """The source file a cached bytecode file stands for (`a/__pycache__/b.cpython-314.pyc` is `a/b.py`): a module
    loaded from its cache opens only the cache, which a fresh checkout (CI's) does not have yet."""
    folder, _, name = relative.rpartition("__pycache__/")
    if not name.endswith(".pyc") or "/" in name:
        return relative
    return folder + name.split(".", 1)[0] + ".py"


def declared(markers: Iterable[tuple[object, ...]]) -> set[str]:
    """The classes the `reads` markers of a test name (each marker's arguments)."""
    return {str(name) for args in markers for name in args}


def missing(reads: dict[str, set[str]], named: set[str]) -> dict[str, list[str]]:
    """The scoped classes read but not named, each with a few of its paths."""
    return {
        kind: sorted(paths)[:SHOWN] for kind, paths in sorted(reads.items()) if kind not in named and kind != SPAWNED
    }


def failure(test: str, unnamed: dict[str, list[str]]) -> str:
    """The message of a test that read a scoped class its marker does not name."""
    classes = ", ".join(f'"{kind}"' for kind in unnamed)
    seen = "; ".join(f"{kind}: {', '.join(paths)}" for kind, paths in unnamed.items())
    return (
        f"{test} reads the checkout's {seen}, but is not marked as reading it: add @pytest.mark.{MARKER}({classes}) "
        "to the test (or `pytestmark` to its module), so a scoped `task check` runs it "
        "(docs/guides/coney-tools-repo.md#task-check)"
    )


def log_line(test: str, reads: dict[str, set[str]]) -> str:
    """One JSON line of the reads log."""
    return json.dumps({"test": test, "reads": {kind: sorted(paths)[:20] for kind, paths in sorted(reads.items())}})


def wrap_stat_calls(recorder: Recorder) -> list[tuple[object, str, Callable[..., Any]]]:
    """Replace each of `STAT_CALLS` with a function that notes its path, then calls the original with the same
    arguments and result. Returns what was replaced, for `unwrap_stat_calls`. A function that fails to be wrapped (the
    platform lacks it) is skipped."""
    replaced: list[tuple[object, str, Callable[..., Any]]] = []
    for module, names in STAT_CALLS:
        for name in names:
            original = getattr(module, name, None)
            if original is None:
                continue
            setattr(module, name, _noting(recorder, original))
            replaced.append((module, name, original))
    return replaced


def unwrap_stat_calls(replaced: list[tuple[object, str, Callable[..., Any]]]) -> None:
    """Put back what `wrap_stat_calls` replaced."""
    for module, name, original in reversed(replaced):
        setattr(module, name, original)


def _noting(recorder: Recorder, original: Callable[..., Any]) -> Callable[..., Any]:
    """`original` wrapped to note its first argument (the path) in `recorder` before it runs."""

    @functools.wraps(original)
    def wrapper(*args: Any, **kwargs: Any) -> Any:
        if recorder.current is not None:
            recorder.note(args[0] if args else kwargs.get("path"))
        return original(*args, **kwargs)

    return wrapper


def install(recorder: Recorder) -> None:
    """Add the audit hook and wrap the stat calls (both stay for the life of the process; audit hooks cannot be
    removed)."""
    sys.addaudithook(recorder.hook)
    wrap_stat_calls(recorder)
