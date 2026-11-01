# SPDX-License-Identifier: GPL-3.0-or-later
"""Which C++ tests a change needs: the `ctest-full-disc` check of `coney-tools task check` (`proof.ctest`).

A test can only call code its file reaches: the headers it `#include`s, the headers those include, and the sources
that implement each of them, with their own includes in turn. So a changed C++ file needs exactly the test files whose
reach holds it. A header is implemented by the source of its own name in its folder (`brain.h`, `brain.cpp`) and by
every source of that folder that has no header of its own and includes it (`human.h` and `human_hiding.cpp`), which is
how the code splits a big class. The game's core is one large cycle of such links, so a change there reaches hundreds
of test files; a leaf (the launcher, a test file, a small helper) reaches a few. A change that reaches more than
`max_files`, or none, runs a narrow set instead (`narrow`): the test files near it, named in the stamp as narrowed by
reach, since CI and `ci disc-suite` after the merge run the rest (the maintainer's decision of 2026-11-01).

Each test file is one ctest label: tests/CMakeLists.txt lists the tests with Catch2's `--filenames-as-tags`, which tags
every test case `#<file name without extension>`, and `catch_discover_tests(... ADD_TAGS_AS_LABELS)` makes the tags
labels. The narrowed run is `ctest -L '^#(a_test|b_test)$'`, the disc tests among them with the disc. A path this
cannot map runs the whole suite: a prefix of `whole_when`, a class outside `skip` that is no C++ file (CMake files,
cmake/, assets, test scripts, an unknown path), a file the change deleted or moved (what included it is gone), no test
file near it at all, or a test file whose name a label cannot hold.

The files are read at a commit through git (`tree_at`), so `task check` and `pr verify` compute the same scope from
the same tip whatever the worktree holds. Standard library only.
"""

from __future__ import annotations

import re
import subprocess
from collections import defaultdict
from collections.abc import Iterable, Mapping
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath

from coney_tools.config import ConfigError
from coney_tools.conventions import Conventions, ConventionsError

#: How a stamp records the whole suite (`scope_record`).
WHOLE = "whole"
#: The tag Catch2 gives a test case for its file (`--filenames-as-tags`), before the file's name.
LABEL_PREFIX = "#"
#: A quoted include: the path as written. Angle brackets are other libraries'.
_INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*"(?P<path>[^"\n]+)"', re.MULTILINE)
#: The names a label can hold without escaping in a CMake regular expression.
_LABEL_NAME = re.compile(r"[A-Za-z0-9_]+")


@dataclass(frozen=True)
class CtestScope:
    """Which C++ tests a change needs: the whole suite, or the tests of `files` (test files, by path). `culprit` says
    why it is the whole suite, when it is."""

    whole: bool
    files: tuple[str, ...] = ()
    culprit: str = field(default="", compare=False)  # not part of the scope: only why it is whole
    #: Why the files are a narrow set rather than every test file the change reaches ("" when they are all of them).
    narrowed: str = field(default="", compare=False)

    @property
    def label(self) -> str | None:
        """The ctest `-L` regular expression that selects exactly the tests of `files`; None for the whole suite."""
        if self.whole:
            return None
        names = sorted({PurePosixPath(path).name.split(".")[0] for path in self.files})
        return f"^{LABEL_PREFIX}(" + "|".join(names) + ")$"

    @property
    def text(self) -> str:
        """The scope in words, for `task check`'s output."""
        if not self.whole:
            count = len(self.files)
            files = f"{count} test file{'s' if count != 1 else ''}"
            if self.narrowed:
                return f"the tests of {files}, with the disc, narrowed by reach ({self.narrowed})"
            return f"the tests of {files} the change reaches, with the disc"
        return f"the whole suite with the disc ({self.culprit})" if self.culprit else "the whole suite with the disc"


def _whole(why: str) -> CtestScope:
    """The whole suite, for the reason `why`."""
    return CtestScope(True, culprit=why)


def ctest_scope(conventions: Conventions, paths: Iterable[str], tree: Mapping[str, str]) -> CtestScope:
    """The C++ tests the change to `paths` needs (`proof.ctest`), given `tree`: every C++ file of `ci.tidy`'s folders
    at the change's tip, path to text (`tree_at`). See the module docstring for when it is the whole suite."""
    rules = conventions.proof.ctest
    if not rules.enabled:
        return _whole("the base's rules have no proof.ctest")
    tidy = conventions.ci.tidy
    suffixes = (*tidy.sources, *tidy.headers)
    changed: list[str] = []
    for path in paths:
        if rules.whole_when and path.startswith(rules.whole_when):
            return _whole(f"{path} runs everything")
        if path.startswith(tidy.folders) and path.endswith(suffixes):
            if path not in tree:
                return _whole(f"{path} was deleted or moved")
            if path.startswith(rules.tests) and path.endswith(tidy.sources) and not path.endswith(rules.test_suffix):
                return _whole(f"{path} is a test program's source with no file label")
            changed.append(path)
        elif conventions.paths.ci_class(path) not in rules.skip:
            return _whole(f"{path} is not a C++ file the tests can be traced from")
    if not changed:
        return _whole("the change has no C++ file")
    edges = includes(tree, tidy.folders)
    implemented = implementers(tree, edges, tidy.headers)
    reached = sorted(p for p in reaching(edges, implemented, changed) if p.endswith(rules.test_suffix))
    narrowed = ""
    if not reached or len(reached) > rules.max_files:
        why = (
            f"{len(reached)} test files reach it, more than {rules.max_files}" if reached else "no test file reaches it"
        )
        near, how = narrow(tree, edges, implemented, changed, rules.test_suffix)
        if reached and how == SUBSYSTEM:
            near = [path for path in near if path in reached]  # a subsystem's files that do not reach it test nothing
        reached = near
        if not reached:
            return _whole(f"{why}, and no test file is near {changed[0]}")
        narrowed = f"{why}; {how}; CI and ci disc-suite after the merge run the rest"
    for path in reached:
        if not _LABEL_NAME.fullmatch(PurePosixPath(path).name.split(".")[0]):
            return _whole(f"{path}: a ctest label cannot hold that name")
    return CtestScope(False, tuple(reached), narrowed=narrowed)


def narrow(
    tree: Mapping[str, str],
    edges: Mapping[str, set[str]],
    implemented: Mapping[str, set[str]],
    changed: list[str],
    suffix: str,
) -> tuple[list[str], str]:
    """The narrow set of a change whose reach is too wide (or empty), and how it was chosen: the changed test files,
    the test files that directly include a changed file or a header a changed source implements and is named after,
    and those whose name holds a changed file's name as whole words (`brain.cpp`: `brain_test.cpp`,
    `brain_events_test.cpp`); when there are none, the test files of the changed files' subsystems (`src/ai/...`:
    `tests/ai/`)."""
    tests = [path for path in tree if path.endswith(suffix)]
    # A header a changed source implements counts when the source is named after it (`human.h`: `human.cpp`,
    # `human_hiding.cpp`), not every header a source with no header of its own includes (`main.cpp`).
    targets = set(changed) | {
        header
        for header, sources in implemented.items()
        for source in sources & set(changed)
        if f"{PurePosixPath(source).stem}_".startswith(f"{PurePosixPath(header).stem}_")
    }
    stems = {PurePosixPath(path).stem for path in changed}
    found = {
        test
        for test in tests
        if test in targets
        or edges.get(test, set()) & targets
        or any(f"_{stem}_" in f"_{PurePosixPath(test).stem}_" for stem in stems)
    }
    if found:
        return sorted(found), "the test files that include it directly or are named after it"
    folders = {f"tests/{parts[1]}/" for parts in (PurePosixPath(path).parts for path in changed) if len(parts) > 2}
    return sorted(test for test in tests if test.startswith(tuple(folders))), SUBSYSTEM


#: How `narrow` says it fell back to the subsystem's test files.
SUBSYSTEM = "the test files of its subsystem"


def includes(tree: Mapping[str, str], folders: tuple[str, ...]) -> dict[str, set[str]]:
    """Every file of `tree` -> the files of `tree` it includes: a quoted path is looked for beside the file, then under
    each of `folders` (the include roots: `#include "ai/brain.h"` is src/ai/brain.h). A path found nowhere (a
    generated header, another library's) is left out."""
    found: dict[str, set[str]] = {}
    for path, text in tree.items():
        here = PurePosixPath(path).parent
        targets: set[str] = set()
        for match in _INCLUDE.finditer(text):
            written = match.group("path")
            for candidate in (here / written, *(PurePosixPath(folder) / written for folder in folders)):
                name = _normal(candidate)
                if name in tree:
                    targets.add(name)
                    break
        found[path] = targets
    return found


def _normal(path: PurePosixPath) -> str:
    """`path` with `.` and `..` worked out, as a repository path."""
    parts: list[str] = []
    for part in path.parts:
        if part == "..":
            if parts:
                parts.pop()
        elif part != ".":
            parts.append(part)
    return "/".join(parts)


def implementers(
    tree: Mapping[str, str], edges: Mapping[str, set[str]], headers: tuple[str, ...]
) -> dict[str, set[str]]:
    """Every header -> the sources that implement it: the source of the header's own name in its folder, and each
    source of the folder with no header of its own that includes it."""
    found: dict[str, set[str]] = defaultdict(set)
    by_stem: dict[str, list[str]] = defaultdict(list)
    for path in tree:
        if path.endswith(headers):
            by_stem[_stem(path)].append(path)
    for path in tree:
        if path.endswith(headers):
            continue
        own = _stem(path)
        for header in by_stem.get(own, ()):
            found[header].add(path)
        if own in by_stem:
            continue
        folder = PurePosixPath(path).parent
        for header in edges.get(path, ()):
            if header.endswith(headers) and PurePosixPath(header).parent == folder:
                found[header].add(path)
    return found


def _stem(path: str) -> str:
    """`path` without its extension (`src/ai/brain.cpp` -> `src/ai/brain`)."""
    return str(PurePosixPath(path).with_suffix(""))


def reaching(edges: Mapping[str, set[str]], implemented: Mapping[str, set[str]], changed: Iterable[str]) -> set[str]:
    """Every file whose reach holds one of `changed` (itself included), given the includes (`includes`) and the
    implementing sources (`implementers`): a file reaches what it includes, and a header the sources implementing it."""
    reached_by: dict[str, set[str]] = defaultdict(set)  # file -> the files that reach it in one step
    for path, targets in edges.items():
        for target in targets:
            reached_by[target].add(path)
    for header, sources in implemented.items():
        for source in sources:
            reached_by[source].add(header)
    found = set(changed)
    stack = list(found)
    while stack:
        for user in reached_by.get(stack.pop(), ()):
            if user not in found:
                found.add(user)
                stack.append(user)
    return found


# --- what the stamp records ------------------------------------------------------------------------------------------


def covers(ran: CtestScope, needed: CtestScope) -> bool:
    """Whether a run of the scope `ran` holds every test the scope `needed` asks for."""
    return ran.whole or (not needed.whole and set(needed.files) <= set(ran.files))


def scope_record(scope: CtestScope) -> str | list[str]:
    """The scope as the `checked` stamp records it (`detail.ctest_scope`): `whole`, or the list of test files."""
    return WHOLE if scope.whole else list(scope.files)


def scope_from_record(value: object) -> CtestScope:
    """The scope a stamp recorded; a stamp without one (made before the scope was recorded) ran the whole suite.
    ConventionsError for anything else."""
    if value is None or value == WHOLE:
        return CtestScope(True)
    if isinstance(value, list) and value and all(isinstance(item, str) for item in value):
        return CtestScope(False, tuple(value))
    raise ConventionsError(f"checked stamp: ctest_scope is {value!r}, not {WHOLE!r} or a list of test files")


# --- reading the files at a commit -----------------------------------------------------------------------------------


def tree_at(root: Path, commit: str, folders: Iterable[str], suffixes: Iterable[str]) -> dict[str, str]:
    """The text of every file under `folders` ending in one of `suffixes` at `commit`, by repository path, read with
    one `git cat-file --batch`. ConfigError when git cannot list or read them."""
    wanted = tuple(suffixes)
    listed = _git(root, ["ls-tree", "-r", "-z", "--name-only", commit, "--", *folders], b"")
    names = [name for name in listed.decode("utf-8").split("\0") if name.endswith(wanted)]
    if not names:
        return {}
    output = _git(root, ["cat-file", "--batch"], "".join(f"{commit}:{name}\n" for name in names).encode("utf-8"))
    tree: dict[str, str] = {}
    position = 0
    for name in names:
        end = output.index(b"\n", position)
        header = output[position:end].split()
        if len(header) != 3 or header[1] != b"blob":
            raise ConfigError(f"git cat-file could not read {commit[:10]}:{name}")
        size = int(header[2])
        tree[name] = output[end + 1 : end + 1 + size].decode("utf-8", errors="replace")
        position = end + 1 + size + 1  # the blob, then the newline cat-file ends it with
    return tree


def _git(root: Path, arguments: list[str], given: bytes) -> bytes:
    """Run git in `root` with `given` on its input and return what it printed; ConfigError when it fails."""
    try:
        done = subprocess.run(["git", "-C", str(root), *arguments], input=given, capture_output=True, check=False)
    except OSError as error:
        raise ConfigError(f"git could not run: {error}") from error
    if done.returncode != 0:
        message = done.stderr.decode("utf-8", errors="replace").strip()
        raise ConfigError(f"git {arguments[0]} failed: {message}")
    return done.stdout
