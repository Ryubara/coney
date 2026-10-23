# SPDX-License-Identifier: GPL-3.0-or-later
"""Numbers the statements of one original function's decompile and cuts them into pieces, the same way every time.

This is the pure half of `coney-tools original study` (docs/guides/stamps.md#study): text in, numbered statements and
pieces out, no I/O. A *line* is a logical statement of the function body (a condition, an assignment, a call, a
return, a label), numbered 1..N in text order; a *piece* is one anchor statement (a branch, loop, call, field write,
constant or return) with the statements that only feed it, and a pair of `original compare` maps one piece to the
Coney code that does it.

Numbering skips what carries no behaviour: the comments and signature before the body's opening brace, the block of
local declarations, braces, a bare `else` and `do {`. A statement may span physical lines (a long condition).

The attachment rules (applied in this order; they read only the text):

1. an early exit: a `return` that is the only statement of its block, directly under a branch, belongs to that branch;
2. a result feeder: a call that assigns a local read by exactly one later branch, return or field write (before the
   local is assigned again or the block ends) belongs to that reader;
3. a compute feeder: a plain local computation belongs to the first later anchor that reads it, else to the next
   statement, else to the previous one;
4. labels (`case`, `default`, `LAB_..:`) belong to the next statement, `goto`/`break`/`continue` to the previous one;
   where that would loop the group is one branch piece.
"""

from __future__ import annotations

import hashlib
import re
from dataclasses import dataclass

from coney_tools.config import ConfigError
from coney_tools.orig_copy import code_only


class CutError(ConfigError):
    """The text is not a function body this cutter can number."""


#: The statement kinds that start a piece.
ANCHORS = frozenset({"branch", "loop", "call", "field", "constant", "return"})
_KEYWORDS = frozenset(
    {"if", "else", "while", "for", "do", "switch", "case", "default", "return", "goto", "break", "continue", "sizeof"}
)
_IDENT = re.compile(r"[A-Za-z_]\w*")
#: A name that does not start inside a number (`0x2a` holds no name).
_NAME = re.compile(r"(?<!\w)[A-Za-z_]\w*")
_LABEL = re.compile(r"^(?:case\b[^?]*|default|[A-Za-z_]\w*)$")
_DECLARATION = re.compile(r"^(?:[A-Za-z_]\w*[\s*]+)+[A-Za-z_]\w*(?:\s*\[[^\]]*\])?\s*;$")
_LITERAL = re.compile(r"^[-+]?(?:0[xX][0-9a-fA-F]+|\d+\.?\d*|\.\d+)(?:[fFuUlL]*)$|^(?:true|false|NULL)$")
_CALL = re.compile(r"\b(?!(?:if|while|for|switch|return|sizeof)\b)[A-Za-z_]\w*\s*\(")
_CAST = re.compile(r"^\((?:\s*[A-Za-z_]\w*[\s*]*)+\)\s*")
#: A top-level `=` that is an assignment: not part of `==`, `!=`, `<=`, `>=`.
_ASSIGN = re.compile(r"(?<![=!<>])=(?!=)")


@dataclass(frozen=True)
class Statement:
    """One numbered statement."""

    number: int  # 1-based, in text order
    first_line: int  # the physical line it starts on (1-based, in the normalised text)
    last_line: int
    text: str  # its source, whitespace collapsed
    depth: int  # brace depth outside its own block
    kind: str  # branch loop return call field constant compute jump label


@dataclass(frozen=True)
class Piece:
    """One piece: an anchor and what attaches to it."""

    id: int  # 1-based, in order of the first line
    kind: str  # one of ANCHORS
    lines: tuple[int, ...]  # statement numbers


@dataclass(frozen=True)
class Cut:
    """The numbered statements of a function and their pieces."""

    statements: tuple[Statement, ...]
    pieces: tuple[Piece, ...]


# --- reading the text --------------------------------------------------------------------------------------------


def normalise(text: str) -> str:
    """`text` with `\\r\\n` and `\\r` turned into `\\n`: what is saved and hashed."""
    return text.replace("\r\n", "\n").replace("\r", "\n")


def _signature_names(signature: str) -> set[str]:
    """The parameter names of a C signature: the last identifier of each comma-separated parameter."""
    start, end = signature.find("("), signature.rfind(")")
    if start < 0 or end < start:
        return set()
    names = set()
    for part in signature[start + 1 : end].split(","):
        found = _IDENT.findall(part)
        if found and part.strip() != "void":
            names.add(found[-1])
    return names


@dataclass
class _Raw:
    """A statement as scanned, before it is classified."""

    start: int
    end: int
    depth: int


def _scan(code: str) -> tuple[int, list[_Raw]]:
    """(the offset of the body's first character, the raw statements of the body) of the code-only text."""
    paren = 0
    open_at = -1
    for index, char in enumerate(code):
        if char == "(":
            paren += 1
        elif char == ")":
            paren -= 1
        elif char == "{" and paren == 0:
            open_at = index
            break
    if open_at < 0:
        raise CutError("no function body (no opening brace after the signature)")
    raws: list[_Raw] = []
    depth = 0  # braces opened inside the body
    paren = 0
    start = -1
    index = open_at + 1

    def flush(end: int) -> None:
        nonlocal start
        if start >= 0 and code[start:end].strip():
            raws.append(_Raw(start, end, depth))
        start = -1

    while index < len(code):
        char = code[index]
        if paren == 0 and char == "{":
            flush(index)
            depth += 1
        elif paren == 0 and char == "}":
            flush(index)
            if depth == 0:
                return open_at + 1, raws
            depth -= 1
        else:
            if start < 0 and not char.isspace():
                start = index
            if char == "(" or char == "[":
                paren += 1
            elif char == ")" or char == "]":
                paren -= 1
            elif (paren == 0 and char == ";") or (
                paren == 0 and char == ":" and start >= 0 and _LABEL.match(code[start:index].strip())
            ):
                flush(index + 1)
        index += 1
    raise CutError("unbalanced braces: the body is not closed (is the decompile cut short?)")


def _collapse(text: str) -> str:
    """Runs of whitespace as one space."""
    return " ".join(text.split())


def _top_level_assignment(code: str) -> tuple[str, str] | None:
    """(left, right) of the first assignment outside brackets of a statement, or None."""
    depth = 0
    for match in _ASSIGN.finditer(code):
        depth = code.count("(", 0, match.start()) - code.count(")", 0, match.start())
        depth += code.count("[", 0, match.start()) - code.count("]", 0, match.start())
        if depth == 0:
            left = code[: match.start()].rstrip()
            if left and left[-1] in "+-*/%&|^<>" and not left.endswith(("++", "--")):
                left = left[:-1].rstrip()
                if left.endswith(("<", ">")):  # <<= and >>=
                    left = left[:-1].rstrip()
            return left, code[match.end() :].rstrip(" ;")
    return None


def _classify(code: str, locals_: set[str]) -> tuple[str, set[str], set[str]]:
    """(kind, identifiers assigned, identifiers read) of one statement's code-only text (whitespace collapsed)."""
    reads = set(_IDENT.findall(code)) - _KEYWORDS
    assigned = set(re.findall(r"\b([A-Za-z_]\w*)\s*(?<![=!<>])=(?!=)", code))
    first = _IDENT.match(code)
    word = first.group(0) if first else ""
    if word in ("case", "default") or (_LABEL.match(code.rstrip(":")) and code.endswith(":")):
        return "label", set(), reads
    if word == "switch" or word == "if":
        return "branch", assigned, reads
    if word in ("while", "for"):
        return "loop", assigned, reads
    if word == "return":
        return "return", set(), reads
    if word in ("goto", "break", "continue"):
        return "jump", set(), reads
    split = _top_level_assignment(code)
    has_call = bool(_CALL.search(code)) or "(code *)" in code
    if split is None:
        return ("call" if has_call else "compute"), set(), reads
    left, right = split
    base = _IDENT.match(left)
    plain = base is not None and base.group(0) in locals_ and not re.search(r"[*\[]|->", left)
    if not plain:
        return "field", set(), reads
    name = base.group(0) if base else ""
    rhs_reads = set(_IDENT.findall(right)) - _KEYWORDS
    compound = code[len(left) :].lstrip()[:2].strip() not in ("=",) and not code[len(left) :].lstrip().startswith("=")
    if compound:
        rhs_reads.add(name)
    bare = _CAST.sub("", right.strip()).strip()
    while bare.startswith("(") and bare.endswith(")") and bare.count("(") == bare.count(")") == 1:
        bare = bare[1:-1].strip()
    if not compound and _LITERAL.match(bare):
        return "constant", {name}, set()
    if has_call and not compound:
        return "call", {name}, rhs_reads
    return "compute", {name}, rhs_reads


def cut_decompile(text: str) -> Cut:
    """Number the statements of the function in `text` (a decompile) and cut them into pieces."""
    text = normalise(text)
    code = code_only(text)
    body_start, raws = _scan(code)
    signature = code[:body_start]
    names = _signature_names(signature)
    # The block of local declarations at the top is not numbered; its names tell locals from globals.
    locals_ = set(names)
    index = 0
    while index < len(raws):
        snippet = _collapse(code[raws[index].start : raws[index].end])
        first = _IDENT.match(snippet)
        if (first and first.group(0) in _KEYWORDS) or not _DECLARATION.match(snippet) or "=" in snippet:
            break
        locals_.add(_IDENT.findall(snippet)[-1])
        index += 1
    raws = raws[index:]
    statements: list[Statement] = []
    defs: dict[int, set[str]] = {}
    uses: dict[int, set[str]] = {}
    for raw in raws:
        snippet = _collapse(code[raw.start : raw.end])
        shown = _collapse(text[raw.start : raw.end])
        if snippet.startswith("else "):
            snippet = snippet[5:]
            shown = shown[5:] if shown.startswith("else ") else shown
        if snippet in ("else", "do"):
            continue
        number = len(statements) + 1
        kind, assigned, reads = _classify(snippet, locals_)
        statements.append(
            Statement(
                number,
                text.count("\n", 0, raw.start) + 1,
                text.count("\n", 0, raw.end - 1) + 1,
                shown,
                raw.depth,
                kind,
            )
        )
        defs[number], uses[number] = assigned, reads
    if not statements:
        raise CutError("no statements in the body")
    return Cut(tuple(statements), _pieces(statements, defs, uses))


# --- cutting -----------------------------------------------------------------------------------------------------


def _readers(statements: list[Statement], defs: dict[int, set[str]], uses: dict[int, set[str]], at: int) -> list[int]:
    """The anchor statements after `at` that read what it assigns, up to its redefinition or the end of its block."""
    name = next(iter(defs[at]), "")
    found: list[int] = []
    home = statements[at - 1].depth
    for later in statements[at:]:
        if later.depth < home:
            break
        if name in uses[later.number] and later.kind in ANCHORS:
            found.append(later.number)
        if name in defs[later.number]:
            break
    return found


def _next_to(n: int, count: int) -> int:
    """The statement after `n`, else the one before it, else `n` itself (a body of one statement)."""
    if n < count:
        return n + 1
    return n - 1 if n > 1 else n


def _pieces(statements: list[Statement], defs: dict[int, set[str]], uses: dict[int, set[str]]) -> tuple[Piece, ...]:
    """Group the statements into pieces by the attachment rules."""
    count = len(statements)
    by = {s.number: s for s in statements}
    #: Where each statement points: an int (statement number) to follow, or itself when it is an anchor of its own.
    target: dict[int, int] = {}
    for st in statements:
        n = st.number
        target[n] = n
        previous, following = by.get(n - 1), by.get(n + 1)
        if st.kind == "return":
            if (
                previous is not None
                and previous.kind == "branch"
                and previous.depth == st.depth - 1
                and (following is None or following.depth < st.depth)
            ):
                target[n] = previous.number
        elif st.kind == "call" and defs[n]:
            readers = _readers(statements, defs, uses, n)
            if len(readers) == 1 and by[readers[0]].kind in ("branch", "return", "field"):
                target[n] = readers[0]
        elif st.kind == "compute":
            readers = _readers(statements, defs, uses, n) if defs[n] else []
            target[n] = readers[0] if readers else _next_to(n, count)
        elif st.kind == "label":
            target[n] = _next_to(n, count)
        elif st.kind == "jump":
            target[n] = n - 1 if n > 1 else _next_to(n, count)
    roots: dict[int, int] = {}
    for st in statements:
        seen: list[int] = []
        at = st.number
        while target[at] != at and at not in seen:
            seen.append(at)
            at = target[at]
        if target[at] != at:  # a loop of labels and jumps: the earliest statement of the loop is the root
            at = min(seen[seen.index(at) :])
        roots[st.number] = at
    groups: dict[int, list[int]] = {}
    for st in statements:
        groups.setdefault(roots[st.number], []).append(st.number)
    ordered = sorted(groups.values(), key=lambda members: members[0])
    pieces = []
    for piece_id, members in enumerate(ordered, start=1):
        # A group with no anchor (labels and jumps pointing at each other, or a lone statement) transfers control.
        root_kind = by[roots[members[0]]].kind
        pieces.append(Piece(piece_id, root_kind if root_kind in ANCHORS else "branch", tuple(members)))
    return tuple(pieces)


# --- lines, hashes, listing -------------------------------------------------------------------------------------


def format_lines(numbers: tuple[int, ...] | list[int]) -> str:
    """`(1, 2, 3, 7)` as `1-3,7`."""
    ordered = sorted(numbers)
    parts: list[str] = []
    index = 0
    while index < len(ordered):
        end = index
        while end + 1 < len(ordered) and ordered[end + 1] == ordered[end] + 1:
            end += 1
        parts.append(str(ordered[index]) if end == index else f"{ordered[index]}-{ordered[end]}")
        index = end + 1
    return ",".join(parts)


def parse_lines(text: str) -> tuple[int, ...]:
    """`1-3,7` as `(1, 2, 3, 7)`; CutError for anything else (a range backwards, a 0, an empty part)."""
    numbers: list[int] = []
    for part in text.strip().split(","):
        match = re.fullmatch(r"\s*(\d+)(?:\s*-\s*(\d+))?\s*", part)
        if match is None:
            raise CutError(f"{text!r}: not a list of line numbers and ranges such as 1-3,7")
        low = int(match.group(1))
        high = int(match.group(2) or low)
        if low < 1 or high < low:
            raise CutError(f"{text!r}: {part.strip()!r} is not a range of lines counted from 1")
        numbers.extend(range(low, high + 1))
    return tuple(numbers)


def shape_sha256(cut: Cut) -> str:
    """The hash of the sequence of (kind, depth) of the statements: it does not change when Ghidra renames things."""
    text = "".join(f"{s.kind}:{s.depth}\n" for s in cut.statements)
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def cut_sha256(cut: Cut) -> str:
    """The hash of the pieces (id, kind, lines), the thing a pairs file is made against."""
    text = "".join(f"{p.id}:{p.kind}:{format_lines(p.lines)}\n" for p in cut.pieces)
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def piece_of_line(cut: Cut) -> dict[int, int]:
    """Statement number -> id of its piece."""
    return {n: piece.id for piece in cut.pieces for n in piece.lines}


def statements_by_line(cut: Cut) -> dict[int, list[Statement]]:
    """The statements that start on each physical line (several when a line holds `Foo(); return 2;`)."""
    found: dict[int, list[Statement]] = {}
    for statement in cut.statements:
        found.setdefault(statement.first_line, []).append(statement)
    return found


def skeleton_sha256(text: str) -> str:
    """The hash of a decompile with comments and string contents blanked and each name replaced by the number of its
    first appearance: it changes when an operator, a number, the structure or which name stands where changes (swapped
    operands, another global or callee), not when Ghidra renames a function, variable or type, since a rename is
    one-to-one."""
    code = code_only(normalise(text))
    order: dict[str, int] = {}

    # The name's place in order of first appearance, so `a < b` and `b < a` differ but `x < y` matches `a < b`.
    def number(match: re.Match[str]) -> str:
        name = match.group(0)
        if name in _KEYWORDS:
            return name
        return f"_{order.setdefault(name, len(order))}"

    return hashlib.sha256(_collapse(_NAME.sub(number, code)).encode("utf-8")).hexdigest()


def render_numbered(text: str, cut: Cut) -> str:
    """The reading copy of a decompile: the numbers of the statements that start on each line and their pieces in the
    right gutter (`P3`); lines that start no statement (braces, declarations, comments) keep a blank number column."""
    lines = normalise(text).split("\n")
    owner = piece_of_line(cut)
    starts = statements_by_line(cut)
    labels = {row: ",".join(str(s.number) for s in found) for row, found in starts.items()}
    column = max((len(label) for label in labels.values()), default=3)
    column = max(column, 3)
    width = max((len(line) for line in lines), default=0)
    out = []
    for number, line in enumerate(lines, start=1):
        found = starts.get(number)
        if not found:
            out.append(f"{' ' * (column + 1)}{line}".rstrip())
            continue
        pieces = ",".join(f"P{i}" for i in sorted({owner[s.number] for s in found}))
        out.append(f"{labels[number]:>{column}} {line.ljust(width)}  {pieces}".rstrip())
    return "\n".join(out) + "\n"
