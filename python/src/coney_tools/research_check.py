# SPDX-License-Identifier: GPL-3.0-or-later
"""The research page standard of docs/guides/writing-docs.md#research-pages, checked by program (`docs check`).

A research page is a map from what the game does to the original code that does it, and this module holds the rules
that a program can see. Each yields `Finding`s whose `rule` names it (the legacy file waives rules by these names):

* `front-matter`, `owns`: the page says what kind it is, which system it belongs to and which addresses it owns, and no
  other page owns the same ones (`research_pages`);
* `skeleton`: a `# Title`, then the kind's sections in order and no others, none of them empty;
* `verified`: one `Verified against:` line, short;
* `evidence`: every block that cites an address outside the ungraded sections carries a level in the forms of
  [the evidence levels](#levels), in the block itself, in its row's Evidence cell or under a lead line;
* `address-form`, `address-resolve`, `address-name`: an address is `0x` and eight digits, resolves in the executable,
  and is named as Ghidra names it (a warning);
* `decompile`: none of Ghidra's made-up names (`param_1`, `uVar2`, `DAT_005127e4`) stands in the text, the mark of a
  pasted decompile (LEGAL.md#research-limits);
* `copied`: no paragraph of 200 characters or more is written on two pages;
* `history`: no date and no issue number outside In Coney and How this was checked;
* `length`: no page longer than its kind allows.

Everything is a function of the page text and of the `ResearchEnv` (the conventions, the executable's functions and
sections, the other pages and the legacy file), so the tests use made-up pages and a made-up executable.

Extending it: the later parts of the rework add rules with the same signature as `_Rule` to `EXTRA_RULES` and list their
names in `RULES`. The function entries of `research/functions/` (their address, Ghidra name, page anchor and owner)
and the owner index of `research/find/` are such rules; so are the checks of a test's `// Expected:` citation.
"""

from __future__ import annotations

import re
from collections.abc import Callable, Iterable
from dataclasses import dataclass, field

from coney_tools.conventions import Research
from coney_tools.docs_page import (
    Block,
    Finding,
    Page,
    address_line,
    heading_name,
    section_text,
    split_cells,
)
from coney_tools.orig_copy import FINGERPRINTS
from coney_tools.research_catalog import Catalog
from coney_tools.research_legacy import Legacy
from coney_tools.research_pages import (
    Corpus,
    FrontMatter,
    key_line,
    owner_conflicts,
    paragraph_key,
    read_front_matter,
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
    "copied",
    "history",
    "length",
)

#: An address in the executable, written as the conventions say: `0x` and eight hex digits (`0x00149160`), within the
#: EE's 32 MB of main memory where the executable and its data live (`0x00100000`-`0x01ffffff`). A field offset, a mask
#: or a flag value is written shorter; a name hash, a disc offset or a full-width mask (`0x80000000`) falls outside.
ADDRESS = re.compile(r"(?<![\w])0x0(?:0[1-9a-fA-F]|1[0-9a-fA-F])[0-9a-fA-F]{5}\b")
#: Six or seven digits after `0x`: the short form of an address, or a mask or an offset.
_SHORT = re.compile(r"(?<![\w+])0x(?P<digits>[0-9a-fA-F]{6,7})\b")
#: A script locator (`level99.lua:f31`: function prototype 31 of the chunk), which mission pages cite instead of an
#: address.
LOCATOR = re.compile(r"\b[\w-]+\.lua:f\d+\b")
#: The end of a range written `0x00284920-0x00288000`: the first address after it need not be a function.
_RANGE_END = re.compile(r"0x[0-9a-fA-F]{8}[`\s]*(?:-|\u2013|\u2014|\bto\b)[`\s]*$")

# --- the levels -------------------------------------------------------------------------------------------------------

_CODE = re.compile(r"confirmed\s*\(\s*code\s*\)", re.IGNORECASE)
_RUNTIME = re.compile(r"confirmed\s*\(\s*runtime\s*\)(?:\s*,\s*run\s+(?P<id>[A-Za-z0-9][\w.-]*))?", re.IGNORECASE)
_INFERRED = re.compile(r"\binferred\b", re.IGNORECASE)
_SPECULATIVE = re.compile(r"\bspeculative\b", re.IGNORECASE)
_NOT_NEEDED = re.compile(r"\bnot needed\s*:", re.IGNORECASE)
#: `confirmed (disc)`, `confirmed (name hash)`, `confirmed (runtime, coney)`: a level that is none of ours.
_OTHER_CONFIRMED = re.compile(r"\bconfirmed\s*\((?!\s*(?:code|runtime)\s*\))[^)\n]*\)", re.IGNORECASE)
_CONFIDENCE = re.compile(r"\bconfidence\s*:", re.IGNORECASE)
_HEDGE = re.compile(r"\b(?:probably|likely)\b", re.IGNORECASE)
#: What an `inferred` claim rests on, in prose.
_BASIS = re.compile(
    r"\binferred\s+(?:from|by|because|since)\b|\b(?:because|since|follows|based on|basis)\b", re.IGNORECASE
)
#: A link to an open question: `[q-name](#q-name)` or `[text](other.md#q-name)`.
_QUESTION_LINK = re.compile(r"\]\((?P<page>[^)#\s]*)#(?P<anchor>q-[^)\s]+)\)")
_DATE = re.compile(r"\b(?:19|20)\d\d-\d\d-\d\d\b")
_ISSUE = re.compile(r"(?<![\w&/#])#\d+\b|\b(?:issue|pull request|PR)s?\s+\d+\b")
_CODE_SPAN = re.compile(r"`[^`\n]*`")
_LINK_TARGET = re.compile(r"\]\([^)\n]*\)")
_VERIFIED = re.compile(r"^[ \t>*_]*verified against\b[ \t*_]*:?[ \t*_]*(?P<rest>.*)$", re.IGNORECASE)
#: A name written beside an address: `` `0x00149160` `Name` `` or `` `Name` (`0x00149160`) ``.
_NAME_AFTER = re.compile(
    r"`(?P<addr>0x[0-9a-fA-F]{8})`\s*[:\u2013\u2014-]?\s*`(?P<name>~?[A-Za-z_][\w:~<>]*)(?:\(\))?`"
)
_NAME_BEFORE = re.compile(
    r"`(?P<name>~?[A-Za-z_][\w:~<>]*)(?:\(\))?`\s*(?:\(\s*|at\s+|@\s*)?`(?P<addr>0x[0-9a-fA-F]{8})`"
)


@dataclass(frozen=True)
class Levels:
    """The levels one piece of text states, and what is wrong with how it states them."""

    kinds: frozenset[str] = frozenset()  # code, runtime, inferred, speculative, not-needed
    runs: tuple[str, ...] = ()  # the run ids of its `confirmed (runtime), run <id>` claims
    bare_runtime: bool = False  # a `confirmed (runtime)` with no run id
    refused: tuple[str, ...] = ()  # spellings that are no level of ours


def levels_in(text: str) -> Levels:
    """The levels `text` states (writing-docs.md#evidence-levels): the four spellings, `not needed: <reason>`, and the
    spellings that are refused."""
    kinds: set[str] = set()
    runs: list[str] = []
    bare = False
    if _CODE.search(text):
        kinds.add("code")
    for match in _RUNTIME.finditer(text):
        kinds.add("runtime")
        if match.group("id"):
            runs.append(match.group("id"))
        else:
            bare = True
    if _INFERRED.search(text):
        kinds.add("inferred")
    if _SPECULATIVE.search(text):
        kinds.add("speculative")
    if _NOT_NEEDED.search(text):
        kinds.add("not-needed")
    refused = [match.group(0) for match in _OTHER_CONFIRMED.finditer(text)]
    refused.extend(match.group(0) for match in _CONFIDENCE.finditer(text))
    return Levels(frozenset(kinds), tuple(runs), bare, tuple(refused))


def cites(text: str) -> re.Match[str] | None:
    """The first address or script locator `text` cites."""
    return ADDRESS.search(text) or LOCATOR.search(text)


# --- the environment --------------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class ResearchEnv:
    """What a page is checked against besides its own text."""

    research: Research
    catalog: Catalog = field(default_factory=Catalog)
    corpus: Corpus = field(default_factory=Corpus)
    legacy: Legacy = field(default_factory=Legacy)


@dataclass(frozen=True)
class _Subject:
    """The page under check, with what every rule needs of it."""

    path: str
    page: Page
    kind: str
    front: FrontMatter
    env: ResearchEnv
    legacy_page: bool
    #: For each heading, the canonical name of the level-2 section it sits in ('' above the first, or unknown name).
    sections: list[str]


_Rule = Callable[["_Subject"], Iterable[Finding]]
#: The rules the later parts of the rework add (see the module docstring). Each takes the subject of the check.
EXTRA_RULES: tuple[_Rule, ...] = ()


def check_research(path: str, page: Page, env: ResearchEnv, *, legacy_page: bool = False) -> list[Finding]:
    """Every finding for the research page at `path`, read as `page`, in line order.

    `legacy_page` says the page is in the legacy file, which lets a runtime claim of the old form (PCSX2 named, no run
    record) stand. The findings are the full set: the legacy file's waivers are applied afterwards (`research_legacy`).
    """
    research = env.research
    front, found = read_front_matter(page, research, env.catalog)
    kind = front.kind or research.kind_of_path(path)
    subject = _Subject(path, page, kind, front, env, legacy_page, _section_names(page, research, kind))
    found.extend(_check_system(subject))
    found.extend(_check_owners(subject))
    found.extend(_check_sections(subject))
    found.extend(_check_verified(subject))
    found.extend(_check_evidence(subject))
    found.extend(_check_addresses(subject))
    found.extend(_check_decompile(subject))
    found.extend(_check_copies(subject))
    found.extend(_check_history(subject))
    found.extend(_check_length(subject))
    for rule in EXTRA_RULES:
        found.extend(rule(subject))
    return sorted(found, key=lambda item: item.line)


def _section_names(page: Page, research: Research, kind: str) -> list[str]:
    """For each heading, the level-2 section it sits in: the kind's name for it, the heading's own text when the kind
    has no such section, '' above the first level-2 heading."""
    names: list[str] = []
    current = ""
    for heading in page.headings:
        if heading.level == 1:
            current = ""
        elif heading.level == 2:
            current = research.canonical(kind, heading.text) or heading_name(heading.text)
        names.append(current)
    return names


# --- the system and the owners ----------------------------------------------------------------------------------------


def _check_system(subject: _Subject) -> Iterable[Finding]:
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


def _check_owners(subject: _Subject) -> Iterable[Finding]:
    """No other page owns a file, a function or a range this page owns."""
    owns = subject.front.owns
    if owns is None:
        return
    line = key_line(subject.page, "owns")
    for what, other in owner_conflicts(subject.path, owns, subject.env.corpus):
        yield Finding("owns", line, f"{what} is also owned by {other}: one owner per address, describe it there")


# --- the skeleton -----------------------------------------------------------------------------------------------------


def _check_sections(subject: _Subject) -> Iterable[Finding]:
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


def _check_verified(subject: _Subject) -> Iterable[Finding]:
    """One `Verified against: ...` line before the first section, no longer than the limit."""
    page, limit = subject.page, subject.env.research.verified_max_chars
    first_section = next((heading.line for heading in page.headings if heading.level == 2), len(page.lines) + 1)
    for index, line in enumerate(page.lines):
        if line.number >= first_section:
            break
        found = _VERIFIED.match(line.text) if not line.fenced else None
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


# --- the evidence -----------------------------------------------------------------------------------------------------


def _graded_section(subject: _Subject, heading: int) -> bool:
    """Whether the blocks under heading index `heading` state behaviour: they sit in a level-2 section, and not in one
    of the ungraded ones (Summary, In Coney, Open questions, How this was checked)."""
    name = subject.sections[heading] if heading >= 0 else ""
    ungraded = {entry.casefold() for entry in subject.env.research.ungraded_sections}
    return bool(name) and name.casefold() not in ungraded


def _lead_lines(subject: _Subject) -> dict[int, int]:
    """The heading indexes (level 3 and deeper) whose first block is a lead line, to that block's index: a statement
    that cites no address and states a valid level for the blocks under the heading."""
    page = subject.page
    leads: dict[int, int] = {}
    for index, block in enumerate(page.blocks[:-1]):
        if block.kind != "heading" or page.headings[block.section].level < 3:
            continue
        follower = page.blocks[index + 1]
        if follower.kind != "text" or follower.section != block.section or cites(follower.text):
            continue
        levels = levels_in(follower.text)
        if levels.kinds and not levels.refused:
            leads[block.section] = index + 1
    return leads


def _pcsx2_documented(subject: _Subject) -> bool:
    """Whether the page names PCSX2 where its old-form runtime claims are explained: the Verified line or the section
    How this was checked."""
    page = subject.page
    for index in range(len(page.headings)):
        if (
            subject.sections[index].casefold() == "how this was checked"
            and "pcsx2" in section_text(page, index).casefold()
        ):
            return True
    head = " ".join(line.text for line in page.lines if _VERIFIED.match(line.text))
    return "pcsx2" in head.casefold()


def _check_evidence(subject: _Subject) -> Iterable[Finding]:
    """Every block that cites an address outside the ungraded sections is graded in the forms of the standard; every
    level that is stated is stated in its form. A level word is never an excuse: `confirmed (disc)` is none."""
    page, env = subject.page, subject.env
    leads = _lead_lines(subject)
    old_runtime_ok = subject.legacy_page and _pcsx2_documented(subject)
    anchors = page.anchors
    for index, block in enumerate(page.blocks):
        if block.kind == "heading" or _is_header_row(subject, index):
            continue
        table = page.tables[block.table] if block.table is not None else None
        column = table.level_column if table else None
        cell = block.text
        if block.kind == "row" and column is not None:
            cells = split_cells(block.text)
            cell = cells[column] if column < len(cells) else ""
        levels = levels_in(cell)
        whole = levels_in(block.text) if cell is not block.text else levels
        line = block.line
        for spelling in whole.refused:
            yield Finding(
                "evidence",
                line,
                f"'{spelling}' is no evidence level: use confirmed (code), confirmed (runtime), run <id>, inferred "
                "or speculative",
            )
        if _HEDGE.search(cell) and column is not None and block.kind == "row":
            yield Finding(
                "evidence",
                line,
                "an Evidence cell states a level, not 'probably' or 'likely': write inferred, with its basis",
            )
        if not _graded_section(subject, block.section):
            continue
        if block.kind == "row" and column is None and subject.sections[block.section] == env.research.map_section:
            continue  # a Map table with no level column indexes functions, whose levels are in their entries
        address = cites(block.text)
        line = _cite_line(block, address)
        if address is not None and not levels.kinds:
            covered = block.kind != "row" or column is None
            lead = leads.get(block.section)
            if lead is not None and covered and lead != index:
                address = None
            else:
                where = "its Evidence cell" if block.kind == "row" and column is not None else "its block"
                yield Finding(
                    "evidence",
                    line,
                    f"{address.group(0)} is cited with no evidence level in {where}: write confirmed (code), "
                    "confirmed (runtime), run <id>, inferred from <basis> or speculative ([q-...](#q-...)); "
                    "or put a lead line under an ### heading (writing-docs.md#evidence-levels)",
                )
        yield from _level_forms(subject, block, levels, cell, line, anchors, old_runtime_ok)


def _is_header_row(subject: _Subject, index: int) -> bool:
    """Whether block `index` is the first row of its table (the header, which labels columns and claims nothing)."""
    block = subject.page.blocks[index]
    return block.kind == "row" and block.table is not None and subject.page.tables[block.table].first == index


def _cite_line(block: Block, address: re.Match[str] | None) -> int:
    """The line a citation sits on: a paragraph's address's own line, a row's line."""
    if address is None or block.kind != "text":
        return block.line
    return address_line(block, address.start())


def _level_forms(
    subject: _Subject,
    block: Block,
    levels: Levels,
    cell: str,
    line: int,
    anchors: set[str],
    old_runtime_ok: bool,
) -> Iterable[Finding]:
    """The forms of the levels `levels` (read from `cell`, the part of the block that grades it)."""
    catalog = subject.env.catalog
    if levels.bare_runtime and not old_runtime_ok:
        yield Finding(
            "evidence",
            line,
            "confirmed (runtime) cites the run that observed it: `confirmed (runtime), run <id>` "
            "(a record in research/runs/)",
        )
    for run in levels.runs:
        if not catalog.has_run(run):
            yield Finding("evidence", line, f"no run record research/runs/{run}.toml")
    if "speculative" in levels.kinds:
        links = list(_QUESTION_LINK.finditer(block.text))
        if not links:
            yield Finding(
                "evidence", line, "speculative links the open question that says how to test it: ([q-name](#q-name))"
            )
        for link in links:
            if not link.group("page") and link.group("anchor") not in anchors:
                yield Finding("evidence", line, f"no open question {{#{link.group('anchor')}}} on this page")
    if "inferred" in levels.kinds and block.kind == "text" and not _BASIS.search(cell):
        yield Finding("evidence", line, "inferred names what it follows from: `inferred from <basis>`", "warning")


# --- addresses --------------------------------------------------------------------------------------------------------


def _check_addresses(subject: _Subject) -> Iterable[Finding]:
    """Every address is `0x` and eight digits (a short form of a function entry is refused), resolves in the executable
    and is named as Ghidra names it."""
    catalog = subject.env.catalog
    if not catalog.can_resolve:
        return
    seen: set[int] = set()
    for line in subject.page.lines:
        if line.fenced or not line.text.strip():
            continue
        text = line.text
        for short in _SHORT.finditer(text):
            value = int(short.group("digits"), 16)
            entry = catalog.entry(value)
            if entry is not None and entry.named:
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
            if not catalog.resolves(value):
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


def _check_decompile(subject: _Subject) -> Iterable[Finding]:
    """No name Ghidra made up stands in the text, code blocks included: write the address, or name the thing for what
    it is. Pasted decompiled code is game code (LEGAL.md#research-limits)."""
    for line in subject.page.lines:
        found = _GHIDRA_NAME.search(line.text)
        if found:
            yield Finding(
                "decompile",
                line.number,
                f"'{found.group(0)}' is a name Ghidra made up, the mark of a pasted decompile: cite the address, or "
                "name it for what it is",
            )


def _check_copies(subject: _Subject) -> Iterable[Finding]:
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


def _check_history(subject: _Subject) -> Iterable[Finding]:
    """No date and no issue number outside the sections that may hold them: history lives in git and the pull
    requests, and plans in issues."""
    research = subject.env.research
    allowed = {name.casefold() for name in research.dated_sections}
    current = ""
    headings = {heading.line: index for index, heading in enumerate(subject.page.headings)}
    for line in subject.page.lines:
        if line.number in headings:
            current = subject.sections[headings[line.number]]
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


def _check_length(subject: _Subject) -> Iterable[Finding]:
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
