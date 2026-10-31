# SPDX-License-Identifier: GPL-3.0-or-later
"""The scanners of the code and test standards (`coney-tools standards`, docs/guides/coney-tools-standards.md).

A source file is below the code standard when it breaks one of three rules, each counted per finding:

* `comment`: a function (declared in a header or defined anywhere) with no comment above it and none on its declaration
  or definition elsewhere in the folder, unless the name says everything: a definition of one statement, with no
  branch, inside a class or outside, a constructor, destructor or operator declared without a body, or an `override` or
  `final` of a virtual, whose doc comment is on the base declaration (docs/guides/conventions.md#comments-and-orig,
  rules 1 and 2);
* `type-doc`: a public class or struct a header declares (not a private nested one, nor one in an anonymous
  namespace) with no comment above it (rule 1);
* `tag`: a function of a gameplay folder (`paths.gameplay`) with neither `@orig` nor `@coney-only` (the older "Coney's
  own" wording does not count: it marks only code written before the check).

A test file is below the test standard when a test case has no `// Expected:` line that cites where its expected value
comes from (`citation`): a section of a research page, or `unverified` with the issue that verifies it.

The reader of C++ is `orig_scan`; this module adds no parsing of its own beyond the test macros. The checks are
deliberately forgiving: they flag what is surely a gap, never what might be one. The whole-file clang-tidy pass of the
code standard stays CI's.
"""

from __future__ import annotations

import re
from collections import defaultdict
from dataclasses import dataclass
from pathlib import PurePosixPath

from coney_tools.conventions import CitationRule, TestsStandard
from coney_tools.orig_index import find_untagged, is_exempt
from coney_tools.orig_scan import HEADER_SUFFIXES, Function, SourceFile

CODE_RULES = ("comment", "type-doc", "tag")
TEST_RULES = ("citation",)
#: Every rule the legacy file of code and test files can list, in the order the file writes them.
RULES = (*CODE_RULES, *TEST_RULES)

_SPDX = re.compile(r"SPDX-License-Identifier")


@dataclass(frozen=True)
class Finding:
    """One place a file breaks a rule of the standard."""

    path: str
    line: int
    rule: str
    message: str


def counts(findings: list[Finding]) -> dict[str, int]:
    """How many findings each rule has, in the order of `RULES`."""
    found: dict[str, int] = {}
    for rule in RULES:
        number = sum(1 for f in findings if f.rule == rule)
        if number:
            found[rule] = number
    return found


# --- the code standard -----------------------------------------------------------------------------------------------


def _says_something(comment: str) -> bool:
    """Whether a comment block holds words of its own: the licence line at the top of a file is not a comment on the
    first function below it."""
    for line in comment.splitlines():
        if _SPDX.search(line):
            continue
        if line.strip().strip("/*!<").strip():
            return True
    return False


def place_of(function: Function) -> str:
    """Where a function's declarations and definitions meet: its file for a private helper, else its folder. A
    declaration and a definition with the same place, scope and name are the one function."""
    return function.path if function.helper else PurePosixPath(function.path).parent.as_posix()


def _is_special(function: Function) -> bool:
    """A constructor, a destructor or an operator: the name says what it is, and a header declares them with no body."""
    last = function.scope.rsplit("::", 1)[-1]
    return function.name.startswith(("~", "operator")) or (bool(last) and function.name == last)


def _says_it_by_name(function: Function) -> bool:
    """A function whose name says everything (rule 2): a constructor, destructor or operator with no statement, or a
    definition of at most one statement with no branch or lambda (an accessor, a forwarder) that is not public. A
    public one needs its doc comment like any other (rule 1)."""
    if _is_special(function) and function.statements == 0 and not function.blocks:
        return True
    if not function.body or function.public:
        return False
    return is_exempt(function) or (function.statements <= 1 and not function.blocks)


def _comment_findings(source: SourceFile, commented: set[tuple[str, str, str]]) -> list[Finding]:
    """The `comment` and `type-doc` findings of one file; `commented` holds the (place, scope, name) of every function
    with a comment of its own anywhere in the sources, and of every override."""
    found: list[Finding] = []
    for function in source.functions:
        if _says_it_by_name(function):
            continue
        if (place_of(function), function.scope, function.name) in commented:
            continue
        what = "declared" if not function.body else "defined"
        found.append(
            Finding(
                source.path,
                function.line,
                "comment",
                f"{function.qualified} is {what} with no comment above it (conventions.md#comments-and-orig)",
            )
        )
    if source.path.endswith(HEADER_SUFFIXES):
        for name, comment in source.class_comments.items():
            if source.class_public.get(name, True) and not _says_something(comment):
                line = source.class_lines.get(name, 1)
                found.append(Finding(source.path, line, "type-doc", f"{name} has no doc comment above it"))
    return found


def scan_code(sources: list[SourceFile], gameplay: tuple[str, ...]) -> dict[str, list[Finding]]:
    """The findings of the code standard for each source file that has any (path to its findings, in line order).

    `gameplay` is `paths.gameplay`: only the functions of those folders need `@orig` or `@coney-only`.
    """
    commented: set[tuple[str, str, str]] = set()
    for source in sources:
        for function in source.functions:
            # An override or final is documented by the virtual it overrides, in whichever folder that is.
            if function.override or _says_something(function.comment) or function.tags or function.body_tags:
                commented.add((place_of(function), function.scope, function.name))
    tagless, _ = find_untagged(sources, gameplay, with_legacy=True)
    tag_findings: dict[str, list[Finding]] = defaultdict(list)
    for item in tagless:
        if item.exempt:
            continue
        path, _, line = item.where.rpartition(":")
        older = ' (the older "Coney\'s own" wording marks only code written before the check)' if item.legacy else ""
        tag_findings[path].append(
            Finding(path, int(line), "tag", f"{item.coney} has no @orig and no @coney-only{older}")
        )
    found: dict[str, list[Finding]] = {}
    for source in sources:
        findings = [*_comment_findings(source, commented), *tag_findings.get(source.path, [])]
        if findings:
            found[source.path] = sorted(findings, key=lambda f: (f.line, f.rule))
    return found


# --- the test standard -----------------------------------------------------------------------------------------------


def _case_pattern(standard: TestsStandard) -> re.Pattern[str]:
    """A line that starts a test case: one of the macros, at the start of the line."""
    names = "|".join(re.escape(name) for name in standard.macros)
    return re.compile(rf"^\s*(?:{names})\s*\(")


def cited_sections(text: str, rule: CitationRule) -> list[tuple[str, str]]:
    """The (page, anchor) of each research section a line cites after the rule's marker (`Expected:`), the page below
    `docs/research` whether it is written from there, from `research/` or from the repository (`docs/research/`). The
    one reader of the form, for the standards and
    for `research claims`: the form is `standards.tests.citation` in .github/conventions.yml."""
    _, found, rest = text.partition(rule.marker)
    if not found:
        return []
    sections = []
    for match in re.finditer(rule.reference, rest):
        page, _, anchor = match.group(0).partition("#")
        sections.append((page.removeprefix("docs/research/").removeprefix("research/"), anchor))
    return sections


def _is_citation(text: str, standard: TestsStandard) -> bool:
    """Whether a comment line cites a source for an expected value: the marker followed by a section of a page, or by
    the `unverified` form with an issue number."""
    marker = re.search(rf"//\s*{re.escape(standard.citation.marker)}\s*(?P<rest>.*)", text)
    if marker is None:
        return False
    rest = marker.group("rest")
    if rest.startswith(standard.citation.unverified):
        return re.match(r"\d+", rest[len(standard.citation.unverified) :]) is not None
    return bool(cited_sections(text, standard.citation))


def case_spans(text: str, standard: TestsStandard) -> list[range]:
    """The lines (1-based) each test case of a file spans: from the comment block right above its macro, which may hold
    its citation, to the line before the next case's span, or the end of the file. The touch-it rule holds a case to
    its citation when the change touches a line of its span."""
    lines = text.splitlines()
    pattern = _case_pattern(standard)
    starts: list[int] = []
    for number, line in enumerate(lines):
        if not pattern.match(line):
            continue
        first = number
        while first > 0 and lines[first - 1].lstrip().startswith("//"):
            first -= 1
        starts.append(first + 1)
    ends = [start - 1 for start in starts[1:]] + [len(lines)]
    return [range(start, end + 1) for start, end in zip(starts, ends, strict=True)]


def scan_tests(path: str, text: str, standard: TestsStandard) -> list[Finding]:
    """The test cases of one file with no citation, one finding each at the line of its macro.

    A citation belongs to the case below it when it is in the comment block that ends right above the case's macro,
    and to the case it sits in otherwise.
    """
    lines = text.splitlines()
    pattern = _case_pattern(standard)
    cases = [number for number, line in enumerate(lines) if pattern.match(line)]
    cited: set[int] = set()
    for number, line in enumerate(lines):
        if not _is_citation(line, standard):
            continue
        below = number + 1
        while below < len(lines) and lines[below].lstrip().startswith("//"):
            below += 1
        if below < len(lines) and below in cases:
            cited.add(below)
            continue
        before = [case for case in cases if case < number]
        if before:
            cited.add(before[-1])
    found = []
    for case in cases:
        if case in cited:
            continue
        name = re.search(r'"([^"]*)"', lines[case])
        label = f' "{name.group(1)}"' if name else ""
        found.append(
            Finding(path, case + 1, "citation", f"the test case{label} cites no source for its expected values")
        )
    return found
