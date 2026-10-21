# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools orig copy-check`: decompiler fingerprints in the C++ lines a change adds.

Coney ports the original's behaviour and writes it in its own code (docs/guides/conventions.md#porting): a line that
still carries what Ghidra's output looks like was transcribed, not ported. This check finds the marks that cannot be
mistaken for Coney's own code, in the code part of each added line of a C++ file under `src/` (comments, string and
character literals and raw strings are blanked first, so a shader's `in_pos0` or a comment citing `FUN_00123456`
passes):

* Ghidra's automatic names: `iVar1`, `puVar3`, `param_1`, `local_1c`, `auStack_20`, `DAT_003a1f20`, `FUN_...`,
  `LAB_...`, `unaff_s0`, `extraout_v0`, `in_v1` (a MIPS register after `in_`);
* its `undefined`, `undefined1` to `undefined8` types;
* a raw dereference at a byte offset, `*(T *)(x + 0x40)` or `*reinterpret_cast<T*>(x + 0x40)`;
* `goto`.

A line may carry `// copy-check: allow <reason>` when one of them is deliberate (a `goto` out of nested loops in
platform code). What it cannot see (the statement-by-statement shape of a function) is the orig-reviewer agent's
part (docs/guides/review/orig.md).
"""

from __future__ import annotations

import re
import subprocess
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

from coney_tools import local_gate
from coney_tools.config import find_repo_root
from coney_tools.orig_index import SOURCE_DIR, SOURCE_SUFFIXES

#: Each fingerprint: (kind, pattern over the code part of a line).
FINGERPRINTS: tuple[tuple[str, re.Pattern[str]], ...] = (
    (
        "ghidra-name",
        re.compile(
            r"\b(?:[a-z]{1,4}Var\d+"
            r"|param_\d+"
            r"|local_(?:res)?[0-9a-f]+(?:_\d+)?"
            r"|[a-z]{1,4}Stack_[0-9a-f]+"
            r"|(?:DAT|FUN|PTR|LAB|SUB|UNK|switchD|caseD)_[0-9a-fA-F]{4,}\w*"
            r"|unaff_\w+|extraout_\w+"
            r"|in_(?:zero|at|v[01]|a[0-3]|t\d|s[0-8]|k[01]|gp|sp|fp|ra|lo|hi|f\d+|stack_\w+|FCR\w*))\b"
        ),
    ),
    ("undefined-type", re.compile(r"\bundefined[1-8]?\b")),
    (
        "raw-offset",
        re.compile(
            r"\*\s*\(\s*(?:const\s+|volatile\s+)?[A-Za-z_][\w:]*(?:\s+[A-Za-z_]\w*)?\s*\*+\s*\)\s*\(.*?[+-]\s*(?:0x[0-9a-fA-F]+|\d+)\s*\)"
            r"|\*\s*reinterpret_cast\s*<[^<>]*\*\s*>\s*\(.*?[+-]\s*(?:0x[0-9a-fA-F]+|\d+)\s*\)"
        ),
    ),
    ("goto", re.compile(r"\bgoto\b")),
)
#: What each kind tells the author.
ADVICE = {
    "ghidra-name": "a name Ghidra made up; name it for what it is",
    "undefined-type": "Ghidra's placeholder type; use the real type",
    "raw-offset": "a raw read at a byte offset; use a Coney type's member",
    "goto": "a goto; write the control flow as Coney would",
}
#: The marker that lets one line through, with its reason after it.
ALLOW = re.compile(r"//\s*copy-check:\s*allow\s+\S")
#: A raw string literal's opening: an optional encoding prefix, `R"`, the delimiter and `(`.
_RAW_OPEN = re.compile(r'(?:u8|[uUL])?R"([^()\\\s]{0,16})\(')


@dataclass(frozen=True)
class Finding:
    """One fingerprint on one line."""

    path: str
    line: int
    kind: str
    text: str  # the matched text

    def __str__(self) -> str:
        return f"{self.path}:{self.line}: `{self.text}`: {ADVICE[self.kind]} (docs/guides/conventions.md#porting)"


def code_only(text: str) -> str:
    """`text` with comments, string, character and raw string literals blanked to spaces, newlines kept, so line
    numbers and columns still point into `text`."""
    out = list(text)
    i, n = 0, len(text)

    def blank(start: int, end: int) -> None:
        for k in range(start, min(end, n)):
            if out[k] != "\n":
                out[k] = " "

    while i < n:
        char = text[i]
        if text.startswith("//", i):
            end = text.find("\n", i)
            end = n if end < 0 else end
            blank(i, end)
            i = end
        elif text.startswith("/*", i):
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            blank(i, end)
            i = end
        elif (raw := _RAW_OPEN.match(text, i)) is not None and (
            i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")
        ):
            close = text.find(")" + raw.group(1) + '"', raw.end())
            end = n if close < 0 else close + len(raw.group(1)) + 2
            blank(i, end)
            i = end
        elif char in "\"'":
            j = i + 1
            while j < n and text[j] != char and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            blank(i, j + 1)
            i = j + 1
        else:
            i += 1
    return "".join(out)


def scan_text(path: str, text: str, lines: set[int] | None = None) -> list[Finding]:
    """The fingerprints in `text` (one file), on the 1-based `lines` only when given."""
    found: list[Finding] = []
    raw_lines = text.splitlines()
    for number, code in enumerate(code_only(text).splitlines(), start=1):
        if lines is not None and number not in lines:
            continue
        if number <= len(raw_lines) and ALLOW.search(raw_lines[number - 1]):
            continue
        for kind, pattern in FINGERPRINTS:
            for match in pattern.finditer(code):
                found.append(Finding(path, number, kind, match.group(0).strip()))
    return found


def _is_source(path: str) -> bool:
    """Whether `path` (repository-relative) is a C++ file under `src/`."""
    return path.startswith(f"{SOURCE_DIR.as_posix()}/") and PurePosixPath(path).suffix in SOURCE_SUFFIXES


_HUNK = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@", re.MULTILINE)


def added_lines(root: Path, base: str) -> dict[str, set[int] | None]:
    """Each C++ file under `src/` changed since HEAD left `base` (committed or not) -> the line numbers it adds now;
    None for an untracked file, whose every line is new."""
    since = local_gate.merge_base(root, base)
    done = subprocess.run(
        [
            "git",
            "-C",
            str(root),
            "diff",
            "-U0",
            "--no-color",
            "--no-ext-diff",
            "--diff-filter=ACMR",
            since,
            "--",
            "src",
        ],
        capture_output=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    result: dict[str, set[int] | None] = {}
    for chunk in re.split(r"^diff --git ", done.stdout, flags=re.MULTILINE)[1:]:
        target = re.search(r"^\+\+\+ b/(.+)$", chunk, re.MULTILINE)
        if target is None or not _is_source(target.group(1)):
            continue
        numbers: set[int] = set()
        for hunk in _HUNK.finditer(chunk):
            start, count = int(hunk.group(1)), int(hunk.group(2) or "1")
            numbers.update(range(start, start + count))
        result[target.group(1)] = numbers
    for path in local_gate.changed_files(root, base):
        if _is_source(path) and path not in result:
            result[path] = None
    return result


def check(root: Path, base: str, every_line: bool = False) -> list[Finding]:
    """The fingerprints on the lines changed since HEAD left `base`; with `every_line`, on every line of `src/`."""
    if every_line:
        targets: dict[str, set[int] | None] = {
            p.relative_to(root).as_posix(): None
            for p in sorted((root / SOURCE_DIR).rglob("*"))
            if p.suffix in SOURCE_SUFFIXES and p.is_file()
        }
    else:
        targets = added_lines(root, base)
    found: list[Finding] = []
    for path, lines in sorted(targets.items()):
        if lines is not None and not lines:
            continue
        file = root / path
        if file.is_file():
            found.extend(scan_text(path, file.read_text(encoding="utf-8", errors="replace"), lines))
    return found


def run(base: str, every_line: bool) -> int:
    """Print each finding and the counts per kind; 0 when there are none, 1 otherwise."""
    found = check(find_repo_root(Path.cwd()), base, every_line)
    for finding in found:
        print(finding)
    scope = "every line of src/" if every_line else f"the lines changed since {base}"
    if found:
        counts = {kind: sum(1 for f in found if f.kind == kind) for kind, _ in FINGERPRINTS}
        summary = ", ".join(f"{kind} {count}" for kind, count in counts.items() if count)
        print(f"orig copy-check: {len(found)} decompiler fingerprint(s) in {scope} ({summary})")
        return 1
    print(f"orig copy-check: ok (no decompiler fingerprint in {scope})")
    return 0
