# SPDX-License-Identifier: GPL-3.0-or-later
"""The lines of a touched source file that a change touched, without the code it only moved (`standards_check`).

The touch-it rule holds the parts of a file the change changed to the standard. Code that only moved (a function that
kept its code, tags and standing under another file, class or both: `orig_review.function_moves`, the rule `orig
review-stamp` and `orig check-tags` read) keeps the standing it had, and so does the place it was taken out of, so
neither is a touch. This module holds that half of the rule: `changed_lines` turns the diff of each touched file into
the set of lines it touched, and the helpers below read what a deletion took out.
"""

from __future__ import annotations

from collections import Counter
from collections.abc import Iterable, Sequence
from pathlib import Path

from coney_tools import orig_review
from coney_tools.code_link_moves import CodeDiff
from coney_tools.comment_spans import comment_spans, is_comment
from coney_tools.orig_index import SOURCE_DIR
from coney_tools.orig_scan import Function, SourceFile, read_source


def git_lines(text: str) -> list[str]:
    """The lines of `text` as git counts them: broken at a newline only (`str.splitlines` also breaks at a form feed,
    U+0085, U+2028 and others, which would shift the diff's line numbers onto the wrong lines), without a trailing
    carriage return."""
    lines = text.split("\n")
    if lines[-1] == "":
        lines.pop()
    return [line.removesuffix("\r") for line in lines]


def changed_lines(
    root: Path,
    sources: Iterable[SourceFile],
    since: str,
    diffs: dict[str, CodeDiff | None],
    renamed: dict[str, str],
) -> dict[str, frozenset[int] | None]:
    """The lines of each file of `diffs` that the change touched (None: all of a new file), without the functions it
    only moved: a definition or declaration whose code, tags and standing are those of one gone from the base, under
    another file, class or both (`orig_review.function_moves`), keeps the standing it had, so neither its findings nor
    its comparison are owed. A file that only moved code in from elsewhere is held for the rest of what it holds.
    Taking the moved code out of its old place is part of the move: a pure deletion whose lines are each nothing but
    comment and blanks (as a scan of the old file's comments reads them) or a line of a function that moved away
    (`renamed` gives a renamed file's old path) does not touch the line it sits at. `sources` are the files as they are
    now, `since` the commit the change is measured from."""
    lines: dict[str, frozenset[int] | None] = {name: None if d is None else d.lines for name, d in diffs.items()}
    if not any(name.startswith(f"{SOURCE_DIR.as_posix()}/") for name in diffs):
        return lines
    now = orig_review.texts_now(root)
    before = orig_review.files_at(root, since, known=now)
    moved = orig_review.function_moves(before, now)
    if not moved:
        return lines
    kept: dict[str, set[int]] = {}
    for source in sources:
        if source.path in diffs:
            for function in source.functions:
                if (source.path, function.line) in moved:
                    kept.setdefault(source.path, set()).update(_span(function))
    away = _moved_away(before, moved.values())
    for name, diff in diffs.items():
        changed = lines[name]
        if diff is not None:
            gone = away.get(renamed.get(name, name), set())
            marks = _deletion_marks(before.get(renamed.get(name, name), ""), diff.deletions, gone)
            changed = frozenset(line for line in diff.lines if line in diff.added or line in marks)
        elif name in kept:
            count = len(git_lines((root / name).read_bytes().decode("utf-8", errors="replace")))
            changed = frozenset(range(1, count + 1))
        lines[name] = None if changed is None else changed - kept.get(name, set())
    return lines


def _span(function: Function) -> range:
    """The lines of a definition or declaration, from its statement's first line."""
    return range(function.line, function.line + max(function.lines, 1))


def _moved_away(before: dict[str, str], was: Iterable[orig_review.Unit]) -> dict[str, set[int]]:
    """The lines, file by file at the base, of the functions that moved elsewhere (`was`: where each stood)."""
    starts: dict[str, set[int]] = {}
    for unit in was:
        starts.setdefault(unit.path, set()).add(unit.line)
    lines: dict[str, set[int]] = {}
    for path, wanted in starts.items():
        for function in read_source(path, before.get(path, "")).functions:
            if function.line in wanted:
                lines.setdefault(path, set()).update(_span(function))
    return lines


def _deletion_marks(text: str, deletions: Sequence[tuple[int, int, int]], away: set[int]) -> set[int]:
    """The lines that the pure `deletions` of the old file `text` touch, leaving out each one that only took out moved
    code: its lines are each nothing but comment and blanks, as a scan of the old file's comments reads them (a line
    inside a block comment is one; `*p = 5;` and code beside a comment are not), or a line of a function that moved
    away (`away`). git may align a deletion on equal lines of the function above (its `return a;` and `}` for the moved
    function's), so a code line outside `away` is the moved code's too while an equal line of `away` is left that no
    deletion took out; each such line counts once, so a code line deleted beside a move still touches its function."""
    if not deletions:
        return set()
    old = git_lines(text)
    spans = comment_spans(old)
    taken = {n for _, first, count in deletions for n in range(first, first + count)}
    # The lines of the moved code that git's alignment kept in place: what a deleted equal line stands for.
    spare = Counter(old[n - 1].strip() for n in away - taken if n <= len(old))
    marks: set[int] = set()
    for line, first, count in deletions:
        for number in range(first, first + count):
            if number in away or (number <= len(old) and is_comment(old[number - 1], spans[number - 1])):
                continue
            code = old[number - 1].strip() if number <= len(old) else ""
            if spare[code] > 0:
                spare[code] -= 1
            else:
                marks.add(line)
    return marks
