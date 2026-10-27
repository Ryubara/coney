# SPDX-License-Identifier: GPL-3.0-or-later
"""Where the comments of C++ lines are: the columns of each line that a comment holds, found by a scan of the lines.

A `//` inside a string, a `"//"` or `"/*"` in a literal, and the code before or after a `/* ... */` on one line are not
comment, so a tool that reads a reference "in a comment" (`code_link_moves`) asks this scan, not the first `//` of the
line. The scan skips string literals (with escapes), character literals (and the digit separator of `1'000`) and raw
strings (`R"delim( ... )delim"`, over several lines too), and follows a block comment, and a string that ends in a
backslash, over lines. A line continuation at the end of a `//` comment is not followed.
"""

from __future__ import annotations

import re
from collections.abc import Sequence

#: The columns [start, end) of a line that a comment holds.
Span = tuple[int, int]

#: A raw string's opening; the prefix letters before it do not matter, and its delimiter is up to 16 characters.
_RAW_OPEN = re.compile(r'R"(?P<delim>[^()\\\s"]{0,16})\(')

#: The encoding prefix that makes the apostrophe after it a character literal (`L'"'`, `u8'"'`), not a digit separator.
_CHAR_PREFIX = re.compile(r"(?<![A-Za-z0-9_])(?:L|u8|u|U)$")


def comment_spans(lines: Sequence[str]) -> list[list[Span]]:
    """For each of `lines` (consecutive lines of one file, from its first or from any line outside a comment and string)
    the spans of its columns that a comment holds."""
    found: list[list[Span]] = []
    in_block = False
    raw_end: str | None = None
    open_quote: str | None = None  # the quote of a string or character literal that a trailing backslash continues
    for line in lines:
        spans: list[Span] = []
        i = 0
        size = len(line)
        if open_quote is not None:
            i, open_quote = _skip_literal(line, -1, open_quote)
        while i < size:
            if raw_end is not None:
                end = line.find(raw_end, i)
                if end < 0:
                    i = size
                else:
                    i = end + len(raw_end)
                    raw_end = None
            elif in_block:
                end = line.find("*/", i)
                spans.append((i, size if end < 0 else end + 2))
                in_block, i = end < 0, size if end < 0 else end + 2
            elif line.startswith("//", i):
                spans.append((i, size))
                i = size
            elif line.startswith("/*", i):
                end = line.find("*/", i + 2)
                spans.append((i, size if end < 0 else end + 2))
                in_block, i = end < 0, size if end < 0 else end + 2
            elif line[i] == '"':
                raw = _RAW_OPEN.match(line, i - 1) if i > 0 and line[i - 1] == "R" else None
                if raw is not None:
                    raw_end, i = f'){raw.group("delim")}"', raw.end()
                else:
                    i, open_quote = _skip_literal(line, i, '"')
            elif line[i] == "'":
                # `1'000` is a digit separator, not a character literal; `L'x'` and `u8'x'` are character literals.
                if i > 0 and line[i - 1].isalnum() and not _CHAR_PREFIX.search(line, 0, i):
                    i += 1
                else:
                    i, open_quote = _skip_literal(line, i, "'")
            else:
                i += 1
        found.append(spans)
    return found


def _skip_literal(line: str, start: int, quote: str) -> tuple[int, str | None]:
    """The column after the string or character literal that opens at `start` (-1 for the rest of one continued from
    the line above): past its closing `quote`, skipping escapes, or the end of the line when it is not closed there.
    The second value is `quote` when a backslash ends the line and the literal goes on in the next one."""
    i = start + 1
    while i < len(line):
        if line[i] == "\\":
            if i + 1 == len(line):
                return len(line), quote
            i += 2
        elif line[i] == quote:
            return i + 1, None
        else:
            i += 1
    return len(line), None


def is_comment(line: str, spans: Sequence[Span]) -> bool:
    """Whether `line` holds nothing but comment (and blanks): every column outside `spans` is whitespace."""
    covered = [False] * len(line)
    for start, end in spans:
        covered[start:end] = [True] * (end - start)
    return all(done or char.isspace() for char, done in zip(line, covered, strict=True))
