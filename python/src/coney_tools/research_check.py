# SPDX-License-Identifier: GPL-3.0-or-later
"""The research page standard of docs/guides/writing-docs.md#research-pages, checked by program (`docs check`).

A research page is a map from what the game does to the original code that does it, and this module holds the rules
that a program can see. Each yields `Finding`s whose `rule` names it (the legacy file waives rules by these names):

* `front-matter`, `owns`: the page says what kind it is, which system it belongs to and which addresses it owns, no
  other page owns the same ones (`research_pages`) and a source file it owns is one the source files data has
  (`research_owners.check_owned_files`);
* `skeleton`: a `# Title`, then the kind's sections in order and no others, none of them empty;
* `verified`: one `Verified against:` line, short;
* `evidence`: every block that cites an address outside the ungraded sections carries a level in the forms of the
  evidence levels, in the block itself, in its row's Evidence cell or under a lead line (`research_evidence`);
* `address-form`, `address-resolve`, `address-name`: an address is `0x` and eight digits, resolves in the executable,
  and is named as Ghidra names it (a warning);
* `decompile`: none of Ghidra's made-up names (`param_1`, `uVar2`, `DAT_005127e4`) stands in the text, the mark of a
  pasted decompile (LEGAL.md#research-limits);
* `function-entries`: each function database entry that points at the page is for an address the page owns
  (`research_owners`, the first of `EXTRA_RULES`);
* `copied`: no paragraph of 200 characters or more is written on two pages;
* `history`: no date and no issue number outside In Coney and How this was checked;
* `length`: no page longer than its kind allows.

Everything is a function of the page text and of the `ResearchEnv` (the conventions, the executable's functions and
sections, the other pages and the legacy file; `research_subject`), so the tests use made-up pages and a made-up
executable.

Extending it: the later parts of the rework add rules with the same signature as `Rule` (they take the `Subject` of the
check) to `EXTRA_RULES` and list their names in `RULES`. The function entries of `research/functions/` (their address,
Ghidra name, page anchor and owner) and the owner index of `research/find/` are such rules; so are the checks of a
test's `// Expected:` citation.
"""

from __future__ import annotations

import re
from collections.abc import Iterable

from coney_tools.docs_page import Finding, Line, Page, section_text
from coney_tools.orig_copy import FINGERPRINTS
from coney_tools.research_evidence import check_evidence
from coney_tools.research_owners import check_function_entries, check_owned_files
from coney_tools.research_pages import key_line, owner_conflicts, paragraph_key, read_front_matter
from coney_tools.research_subject import (
    ADDRESS,
    VERIFIED_LINE,
    ResearchEnv,
    Rule,
    Subject,
    section_names,
)

#: The names of the rules, which the legacy file may waive. `legacy` (a stale or illegal entry) is not one of them.
RULES: tuple[str, ...] = (
    "front-matter",
    "owns",
    "skeleton",
    "verified",
    "evidence",
    "address-form",
    "address-resolve",
    "decompile",
    "function-entries",
    "copied",
    "history",
    "length",
)

#: Six or seven digits after `0x`: the short form of an address, or a mask or an offset.
_SHORT = re.compile(r"(?<![\w+])0x(?P<digits>[0-9a-fA-F]{6,7})\b")
#: The sections that hold the executable's globals. A short number that lands in one is the short form of a global's
#: address (`0x6fe998`); the number of a mask, a flag or a colour lies elsewhere, almost always.
_GLOBAL_SECTIONS = frozenset({".data", ".sdata", ".sbss", ".bss"})
#: The end of a range written `0x00284920-0x00288000`: the first address after it need not be a function.
_RANGE_END = re.compile(r"0x[0-9a-fA-F]{8}[`\s]*(?:-|\u2013|\u2014|\bto\b)[`\s]*$")

_DATE = re.compile(r"\b(?:19|20)\d\d-\d\d-\d\d\b")
_ISSUE = re.compile(r"(?<![\w&/#])#\d+\b|\b(?:issue|pull request|PR)s?\s+\d+\b")
_CODE_SPAN = re.compile(r"`[^`\n]*`")
_LINK_TARGET = re.compile(r"\]\([^)\n]*\)")
#: A name written beside an address: `` `0x00149160` `Name` `` or `` `Name` (`0x00149160`) ``.
_NAME_AFTER = re.compile(
    r"`(?P<addr>0x[0-9a-fA-F]{8})`\s*[:\u2013\u2014-]?\s*`(?P<name>~?[A-Za-z_][\w:~<>]*)(?:\(\))?`"
)
_NAME_BEFORE = re.compile(
    r"`(?P<name>~?[A-Za-z_][\w:~<>]*)(?:\(\))?`\s*(?:\(\s*|at\s+|@\s*)?`(?P<addr>0x[0-9a-fA-F]{8})`"
)


#: The rules the later parts of the rework add (see the module docstring). Each takes the subject of the check.
EXTRA_RULES: tuple[Rule, ...] = (check_function_entries, check_owned_files)


def subject_of(path: str, page: Page, env: ResearchEnv, *, legacy_page: bool = False) -> tuple[Subject, list[Finding]]:
    """The research page at `path`, read as `page`, as the rules see it (its kind, front matter and section names), and
    the findings its front matter gave."""
    research = env.research
    front, found = read_front_matter(page, research, env.catalog)
    kind = front.kind or research.kind_of_path(path)
    return Subject(path, page, kind, front, env, legacy_page, section_names(page, research, kind)), found


def check_research(path: str, page: Page, env: ResearchEnv, *, legacy_page: bool = False) -> list[Finding]:
    """Every finding for the research page at `path`, read as `page`, in line order.

    `legacy_page` says the page is in the legacy file, which lets a runtime claim of the old form (PCSX2 named, no run
    record) stand. The findings are the full set: the legacy file's waivers are applied afterwards (`research_legacy`).
    """
    subject, found = subject_of(path, page, env, legacy_page=legacy_page)
    found.extend(_check_system(subject))
    found.extend(_check_owners(subject))
    found.extend(_check_sections(subject))
    found.extend(_check_verified(subject))
    found.extend(check_evidence(subject))
    found.extend(_check_addresses(subject))
    found.extend(_check_decompile(subject))
    found.extend(_check_copies(subject))
    found.extend(_check_history(subject))
    found.extend(_check_length(subject))
    for rule in EXTRA_RULES:
        found.extend(rule(subject))
    return sorted(found, key=lambda item: item.line)


# --- the system and the owners ----------------------------------------------------------------------------------------


def _check_system(subject: Subject) -> Iterable[Finding]:
    """A system has one page; a topic names a system that has one."""
    front = subject.front
    if not front.system or front.kind not in ("system", "topic"):
        return
    systems = [
        facts.path
        for facts in subject.env.corpus.others(subject.path)
        if facts.front.kind == "system" and facts.front.system == front.system
    ]
    line = key_line(subject.page, "system")
    if front.kind == "system" and systems:
        yield Finding("front-matter", line, f"{systems[0]} is already the page of the system '{front.system}'")
    if front.kind == "topic" and not systems and subject.env.corpus.pages:
        yield Finding("front-matter", line, f"no page of kind system names the system '{front.system}'")


def _check_owners(subject: Subject) -> Iterable[Finding]:
    """No other page owns a file, a function or a range this page owns."""
    owns = subject.front.owns
    if owns is None:
        return
    line = key_line(subject.page, "owns")
    for what, other in owner_conflicts(subject.path, owns, subject.env.corpus):
        yield Finding("owns", line, f"{what} is also owned by {other}: one owner per address, describe it there")


# --- the skeleton -----------------------------------------------------------------------------------------------------


def _check_sections(subject: Subject) -> Iterable[Finding]:
    """A `# Title`, then the kind's sections in order and no others; none of them empty."""
    page, research, kind = subject.page, subject.env.research, subject.kind
    spec = research.kinds[kind]
    titles = [heading for heading in page.headings if heading.level == 1]
    if not titles or page.headings[0].level != 1:
        yield Finding("skeleton", 1, "the page has no `# Title` before its first section")
    for extra in titles[1:]:
        yield Finding("skeleton", extra.line, "a page has one `# Title`; make this a section or a subsection")
    found: dict[str, int] = {}
    for index, heading in enumerate(page.headings):
        if heading.level != 2:
            continue
        name = research.canonical(kind, heading.text)
        if not name:
            yield Finding(
                "skeleton",
                heading.line,
                f"'{heading.text}' is not a section of a {kind} page ({', '.join(spec.sections)}): "
                "put its content in a subsection (###) of one of them",
            )
        elif name in found:
            yield Finding("skeleton", heading.line, f"the section '{name}' is written twice")
        else:
            found[name] = index
            if not section_text(page, index):
                yield Finding(
                    "skeleton", heading.line, f"the section '{name}' is empty: say in one line that nothing is known"
                )
    for name in spec.required:
        if name not in found:
            yield Finding(
                "skeleton",
                1,
                f"missing the section '{name}' (a section with nothing known says so in one line, "
                "writing-docs.md#research-pages)",
            )
    positions = [spec.sections.index(name) for name in found]
    if positions != sorted(positions):
        yield Finding(
            "skeleton",
            page.headings[min(found.values())].line,
            f"the sections are out of order; a {kind} page has " + ", ".join(spec.sections),
        )


def _check_verified(subject: Subject) -> Iterable[Finding]:
    """One `Verified against: ...` line before the first section, no longer than the limit."""
    page, limit = subject.page, subject.env.research.verified_max_chars
    first_section = next((heading.line for heading in page.headings if heading.level == 2), len(page.lines) + 1)
    for index, line in enumerate(page.lines):
        if line.number >= first_section:
            break
        found = VERIFIED_LINE.match(line.text) if not line.fenced else None
        if not found:
            continue
        following = page.lines[index + 1].text if index + 1 < len(page.lines) else ""
        text = line.text.strip()
        if not found.group("rest").strip():
            yield Finding("verified", line.number, "the 'Verified against:' line says nothing after the colon")
        elif following.strip() and line.number + 1 < first_section and not following.lstrip().startswith("#"):
            yield Finding(
                "verified",
                line.number,
                "the 'Verified against:' line is one line: what was read, not how (the method goes to the run record "
                "or 'How this was checked')",
            )
        elif len(text) > limit:
            yield Finding(
                "verified",
                line.number,
                f"the 'Verified against:' line is {len(text)} characters, over the {limit} allowed: what was read, "
                "not how",
            )
        return
    yield Finding(
        "verified",
        1,
        "no 'Verified against: ...' line before the first section (the executable, or the pinned library or "
        "document, one line)",
    )


# --- addresses --------------------------------------------------------------------------------------------------------


#: The section that records how the page was checked, where a method may name an address that is no global.
_METHOD_SECTION = "how this was checked"


def _lines_by_section(subject: Subject) -> Iterable[tuple[Line, str]]:
    """Each line of the page with the canonical name of the level-2 section it sits in ('' above the first)."""
    current = ""
    headings = {heading.line: index for index, heading in enumerate(subject.page.headings)}
    for line in subject.page.lines:
        if line.number in headings:
            current = subject.sections[headings[line.number]]
        yield line, current


def _check_addresses(subject: Subject) -> Iterable[Finding]:
    """Every address is `0x` and eight digits (a short form of a function entry is refused), resolves in the executable
    and is named as Ghidra names it."""
    catalog = subject.env.catalog
    if not catalog.can_resolve:
        return
    seen: set[int] = set()
    for line, section in _lines_by_section(subject):
        if line.fenced or not line.text.strip():
            continue
        text = line.text
        for short in _SHORT.finditer(text):
            value = int(short.group("digits"), 16)
            entry = catalog.entry(value)
            holder = catalog.section_of(value)
            if (entry is not None and entry.named) or (holder is not None and holder.name in _GLOBAL_SECTIONS):
                yield Finding(
                    "address-form",
                    line.number,
                    f"0x{short.group('digits')} is the short form of 0x{value:08x}: write `0x` and eight digits",
                )
        for match in ADDRESS.finditer(text):
            value = int(match.group(0), 16)
            if value in seen:
                continue
            seen.add(value)
            if (
                _RANGE_END.search(text[: match.start()])
                and catalog.image
                and catalog.image[0] <= value <= catalog.image[1]
            ):
                continue
            # How this was checked may name the heap address a run watched, which is in no section of the executable.
            if not catalog.resolves(value) and section.casefold() != _METHOD_SECTION:
                yield Finding(
                    "address-resolve",
                    line.number,
                    f"{match.group(0)} is no function, no place inside one and no data section of SLUS_212.15 "
                    "(docs/progress/ghidra-functions.tsv, research/elf-sections.toml)",
                )
        for pattern in (_NAME_AFTER, _NAME_BEFORE):
            for named in pattern.finditer(text):
                entry = catalog.entry(int(named.group("addr"), 16))
                name = named.group("name")
                if entry is None or not entry.named or _same_name(entry.name, name):
                    continue
                yield Finding(
                    "address-name",
                    line.number,
                    f"{named.group('addr')} is named {entry.name} in Ghidra, not {name}: refresh the page, or the "
                    "listing if the function was renamed",
                    "warning",
                )


def _same_name(listed: str, written: str) -> bool:
    """Whether a page's name for a function is Ghidra's, reading `Class::Method` as Ghidra's `Class_Method` and letting
    the method name alone stand for it (`Alloc` for `MemoryFilter_Alloc`)."""
    ours, theirs = listed.replace("::", "_"), written.replace("::", "_")
    return ours == theirs or ours.endswith("_" + theirs) or theirs.endswith("_" + ours)


# --- copies, history, length ------------------------------------------------------------------------------------------


#: Ghidra's automatic names, the fingerprints of a pasted decompile, as `orig copy-check` finds them in C++.
_GHIDRA_NAME = next(pattern for kind, pattern in FINGERPRINTS if kind == "ghidra-name")


#: A Ghidra name alone in an inline code span: a mention, as in a survey of what Ghidra makes of an executable.
_NAME_MENTION = re.compile("`" + _GHIDRA_NAME.pattern + "`")


def _check_decompile(subject: Subject) -> Iterable[Finding]:
    """No name Ghidra made up stands in the text, code blocks included: write the address, or name the thing for what
    it is. Pasted decompiled code is game code (LEGAL.md#research-limits). A survey (what an executable or a tool
    gives us) may name a register or a placeholder by itself in a code span; an expression, a bare word and a code
    block stay refused."""
    survey = subject.kind == "survey"
    for line in subject.page.lines:
        text = _NAME_MENTION.sub(" ", line.text) if survey and not line.fenced else line.text
        found = _GHIDRA_NAME.search(text)
        if found:
            yield Finding(
                "decompile",
                line.number,
                f"'{found.group(0)}' is a name Ghidra made up, the mark of a pasted decompile: cite the address, or "
                "name it for what it is",
            )


def _check_copies(subject: Subject) -> Iterable[Finding]:
    """No paragraph of `copy_min_chars` or more also stands on another research page: it is described at its owner and
    the others link there."""
    limit = subject.env.research.copy_min_chars
    others = list(subject.env.corpus.others(subject.path))
    if not others:
        return
    for block in subject.page.blocks:
        if block.kind != "text":
            continue
        key = paragraph_key(block.text)
        if len(key) < limit:
            continue
        for other in others:
            if key in other.paragraphs:
                yield Finding(
                    "copied",
                    block.line,
                    f"this paragraph is also on {other.path}:{other.paragraphs[key]}: write it once, at the page "
                    "that owns the subject, and link to it",
                )
                break


def _check_history(subject: Subject) -> Iterable[Finding]:
    """No date and no issue number outside the sections that may hold them: history lives in git and the pull
    requests, and plans in issues."""
    research = subject.env.research
    allowed = {name.casefold() for name in research.dated_sections}
    for line, current in _lines_by_section(subject):
        if line.fenced or not line.text.strip() or current.casefold() in allowed:
            continue
        plain = _LINK_TARGET.sub("]", _CODE_SPAN.sub(" ", line.text))
        found = _DATE.search(plain) or _ISSUE.search(plain)
        if found:
            yield Finding(
                "history",
                line.number,
                f"'{found.group(0)}' dates the page or points at an issue: say what is true, and keep dates and "
                f"issue numbers to {' and '.join(research.dated_sections)}",
            )


def _check_length(subject: Subject) -> Iterable[Finding]:
    """The page is no longer than its kind allows (the front matter does not count)."""
    spec = subject.env.research.kinds[subject.kind]
    count = subject.page.body_lines
    if count > spec.max_lines:
        yield Finding(
            "length",
            1,
            f"{count} lines is over the {spec.max_lines} of a {subject.kind} page: split off the behaviour that has "
            "its own entry functions into a topic page",
        )
