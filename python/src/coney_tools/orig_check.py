# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools orig check`: a new function in a gameplay folder says where it comes from.

Every function defined in a folder of `paths.gameplay` (.github/conventions.yml) carries `@orig` for the original
function it reimplements, or `@coney-only <reason>` (docs/guides/conventions.md#coney-only), unless it is exempt
(orig_index.is_exempt). The functions that said neither before this check existed are the fidelity audit's backlog,
not a gate, so the check refuses only what a change adds: a function that is untagged now and was not, under the same
scope and name in the same folder, at the point where the branch left `base`. A function that loses its tag counts as
new. Every folder of `src/` must also be in `paths.gameplay` or `paths.coney_only`, so a new subsystem cannot slip
past.
"""

from __future__ import annotations

import subprocess
from pathlib import Path, PurePosixPath

from coney_tools import local_gate
from coney_tools.config import find_repo_root
from coney_tools.conventions import CONVENTIONS_FILE, load_conventions
from coney_tools.orig_index import SOURCE_DIR, SOURCE_SUFFIXES, Untagged, find_untagged, read_sources
from coney_tools.orig_scan import SourceFile, read_source

#: What a finding tells the author to add.
REMEDY = (
    "add `@orig 0x<address> <Name> (<File>.cpp)` for the original function at that address in Ghidra, "
    "or `@coney-only <reason>` (docs/guides/conventions.md#coney-only)"
)


def _known(untagged: Untagged) -> tuple[str, str, str]:
    """(folder, scope, name): an untagged function as recognised at another commit, even moved to another file."""
    path, scope, name = untagged.key
    return (PurePosixPath(path).parent.as_posix(), scope, name)


def _base_text(root: Path, commit: str, path: str) -> str | None:
    """The text of `path` at `commit`, or None when the file did not exist there."""
    done = subprocess.run(
        ["git", "-C", str(root), "show", f"{commit}:{path}"], capture_output=True, encoding="utf-8", errors="replace"
    )
    return done.stdout if done.returncode == 0 else None


def unlisted_folders(root: Path, gameplay: tuple[str, ...], coney_only: tuple[str, ...]) -> list[str]:
    """The folders of `src/` holding C++ files that are in neither list."""
    base = root / SOURCE_DIR
    folders = sorted(p for p in base.iterdir() if p.is_dir()) if base.is_dir() else []
    problems = []
    for folder in folders:
        name = f"{SOURCE_DIR.as_posix()}/{folder.name}/"
        has_code = any(p.suffix in SOURCE_SUFFIXES for p in folder.rglob("*"))
        if has_code and name not in gameplay and name not in coney_only:
            problems.append(name)
    return problems


def check(root: Path, base: str) -> list[str]:
    """The problems with what changed since HEAD left `base`; an empty list means the change passes."""
    paths = load_conventions(root).paths
    problems = [
        f"{folder}: in neither paths.gameplay nor paths.coney_only of {CONVENTIONS_FILE}; add it to one"
        for folder in unlisted_folders(root, paths.gameplay, paths.coney_only)
    ]
    changed = [
        p
        for p in local_gate.changed_files(root, base)
        if p.startswith(f"{SOURCE_DIR.as_posix()}/") and PurePosixPath(p).suffix in SOURCE_SUFFIXES
    ]
    if not any(paths.gameplay_dir(p) for p in changed):
        return problems
    # Now: every file as it is. Then: the changed files as they were where the branch left `base`, the rest as now
    # (a declaration's tag in an unchanged header still covers a changed definition).
    now = read_sources(root)
    since = local_gate.merge_base(root, base)
    then: list[SourceFile] = [s for s in now if s.path not in changed]
    for path in changed:
        text = _base_text(root, since, path)
        if text is not None:
            then.append(read_source(path, text))
    current, _ = find_untagged(now, paths.gameplay)
    before, _ = find_untagged(then, paths.gameplay)
    old = {_known(u) for u in before}
    for function in current:
        if not function.exempt and _known(function) not in old:
            problems.append(f"{function.where}: {function.coney} has no @orig and no @coney-only; {REMEDY}")
    return problems


def run(base: str) -> int:
    """Print each problem; 0 when there are none, 1 otherwise."""
    problems = check(find_repo_root(Path.cwd()), base)
    for problem in problems:
        print(problem)
    if problems:
        print(f"orig check: {len(problems)} problem(s) since {base}")
        return 1
    print(f"orig check: ok (every new gameplay function since {base} carries @orig or @coney-only)")
    return 0
