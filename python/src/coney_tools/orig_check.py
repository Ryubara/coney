# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools orig check-tags`: a new function in a gameplay folder says where it comes from.

Every function defined in a folder of `paths.gameplay` (.github/conventions.yml) carries `@orig` for the original
function it reimplements, or `@coney-only <reason>` (docs/guides/conventions.md#coney-only), unless it is exempt
(orig_index.is_exempt). The functions that said neither before this check existed are the fidelity audit's backlog,
not a gate, so the check refuses only what a change adds: a function that is untagged now and was not, under the same
scope, name and parameter types in the same folder (a private helper: the same file), at the point where the branch
left `base`. A function that loses its tag, an exempt accessor that grows, and a new function marked only by the older
"Coney's own" wording count as new; a file renamed with `git mv` keeps its old functions, and a function that only
moved (to another file, folder or class, its code, tags and standing as they were: `orig_review.function_moves`) keeps
the standing it had. Every folder of `src/` must also be in `paths.gameplay` or `paths.coney_only`, so a new subsystem
cannot slip past.
"""

from __future__ import annotations

from pathlib import Path, PurePosixPath

from coney_tools import local_gate, orig_review
from coney_tools.config import find_repo_root
from coney_tools.conventions import CONVENTIONS_FILE, load_conventions
from coney_tools.orig_index import SOURCE_DIR, SOURCE_SUFFIXES, Untagged, find_untagged, read_sources
from coney_tools.orig_scan import SourceFile, read_source

#: What a finding tells the author to add.
REMEDY = (
    "add `@orig 0x<address> <Name> (<File>.cpp)` for the original function at that address in Ghidra, "
    "or `@coney-only <reason>` (docs/guides/conventions.md#coney-only)"
)


def _known(untagged: Untagged) -> tuple[str, str, str, str]:
    """(place, scope, name, parameter types): an untagged function as recognised at another commit. The place is its
    folder, so a member or free function moved to another file of the folder is still known; a file's private helper
    is known only in its own file (another file's helper of the same name is another function)."""
    path, scope, name, params = untagged.key
    place = path if untagged.kind == "helper" else PurePosixPath(path).parent.as_posix()
    return (place, scope, name, params)


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
    # A renamed file (`git mv`, to this folder or another) is read at the base under its new path, so its old
    # functions are known where they are now.
    renamed = local_gate.renamed_files(root, base)
    then: list[SourceFile] = [s for s in now if s.path not in changed]
    for path in changed:
        text = local_gate.file_at(root, since, renamed.get(path, path))
        if text is not None:
            then.append(read_source(path, text))
    current, _ = find_untagged(now, paths.gameplay, with_legacy=True)
    before, _ = find_untagged(then, paths.gameplay, with_legacy=True)
    # A function exempt at the base (an accessor) that is no longer exempt is new work and needs its tag.
    old = {_known(u) for u in before if not u.exempt}
    new = [f for f in current if not f.exempt and _known(f) not in old]
    # A function that only moved (its code, tags and standing as they were, from any folder: orig_review.moves)
    # keeps the standing it had: it was in the fidelity audit's backlog, or needed nothing in a Coney-only folder.
    moved = orig_review.moves_since(root, since) if new else {}
    for function in new:
        path, line = function.where.rsplit(":", 1)
        if (path, int(line)) not in moved:
            wording = ' (the older "Coney\'s own" wording marks only code written before this check)'
            problems.append(
                f"{function.where}: {function.coney} has no @orig and no @coney-only"
                f"{wording if function.legacy else ''}; {REMEDY}"
            )
    return problems


def run(base: str) -> int:
    """Print each problem; 0 when there are none, 1 otherwise."""
    problems = check(find_repo_root(Path.cwd()), base)
    for problem in problems:
        print(problem)
    if problems:
        print(f"orig check-tags: {len(problems)} problem(s) since {base}")
        return 1
    print(f"orig check-tags: ok (every new gameplay function since {base} carries @orig or @coney-only)")
    return 0
