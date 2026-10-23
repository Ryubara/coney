# SPDX-License-Identifier: GPL-3.0-or-later
"""The one-off move of `docs/progress/functions.toml` into the function database (issue #778).

The progress tracker used to count the functions Coney reimplements from a list of its own,
`docs/progress/functions.toml` (address, name, subsystem, size). The function database
(`research/functions/<system>.toml`) now says what a function is, what the original does and where Coney ports it, so
the list is retired and the tracker reads the entries that have a `coney` field. This module does the move, and stays
for a branch that still adds rows to the old file:

* `read_legacy` reads the old file;
* `plan` turns its rows into database entries: the entry of a function the database already lists gets `coney`; any
  other function gets a new entry, taken from the research that cites it (see `_ranked`) and put in the file of the
  system its page belongs to (`system_of`);
  a few functions the rules read wrongly are named in `OVERRIDES` instead, each with the page's words that say so;
* `legacy_functions` gives the history (progress_history.py) the reimplemented list of a day before the move.

A field of the old file maps like this: `address` is the entry's address; `name` becomes Ghidra's name for the address
(the entry is checked against the listing, `research render --check`); `subsystem` is worked out from the address
(totals.toml) and checked no more; `size` is the Ghidra listing's size of the function, so the tracker measures the
reimplemented code in the same unit as the Understood measure. The `coney` field comes from the `@orig` tag in `src/`,
and `status` is `legacy`: the port predates the side-by-side comparison.

Research: docs/guides/research-function-database.md#ported
"""

from __future__ import annotations

import re
import tomllib
from collections import Counter
from collections.abc import Iterable, Mapping
from dataclasses import dataclass, field, replace
from pathlib import Path

from coney_tools import docs_page, progress, research_functions, research_import
from coney_tools.conventions import Research
from coney_tools.orig_index import IndexRow
from coney_tools.progress import Function, ProgressError
from coney_tools.progress_research import ListedFunction
from coney_tools.research_evidence import Levels, cites, levels_in
from coney_tools.research_subject import section_names

#: Where a binding's reference page is, relative to docs/research like the entry's `page`.
BINDINGS_FOLDER = "../references/bindings/"
#: The note on an entry the import could tie no level to: the page may well state one, but not in a form that is
#: certainly about this function alone, and the entry never claims a level a page does not give it.
UNGRADED_NOTE = "the import tied no level to this function (made from the progress list, issue 778); read the section"
_BINDING_WRAPPER = re.compile(r"^\s*wrapper:\s*0x([0-9a-fA-F]{8})\b", re.MULTILINE)
_BINDING_NAME = re.compile(r"^\s*-?\s*name:\s*(\w+)", re.MULTILINE)
_BINDING_LEVEL = re.compile(r"^\s*evidence:\s*(\S+)", re.MULTILINE)
#: A choice between the sources of a level, best first: a table row of a page; a sentence that names the address and
#: states a level (`claim`); a block with one level that is about the function alone, or a row's own cell (`block`); a
#: script binding's evidence; the lead line of the heading above; a page that only mentions the address (`cited`).
_RANK = {"table": 0, "claim": 1, "block": 2, "binding": 3, "lead": 4, "cited": 5}
#: A section about Coney's own code or about what is not known yet describes no function of the original: its
#: heading, or an ancestor's, says so.
_WEAK_HEADING = re.compile(r"\bconey\b|\bopen questions?\b|how this was checked|\bsummary\b", re.IGNORECASE)


@dataclass(frozen=True)
class LegacyRow:
    """One `[[function]]` of the retired list."""

    address: int
    name: str
    subsystem: str
    size: int | None


@dataclass(frozen=True)
class Candidate:
    """An entry that research could give a function, and where it came from (a key of `_RANK`)."""

    via: str
    entry: research_functions.Entry
    named: bool = False  # the section names the function (in its heading or a code span of the block)
    mentions: int = 0  # how many blocks of the section cite the address
    describes: bool = (
        False  # the block or its heading is about this function: it opens with the function, or is its row
    )
    weak: bool = False  # the section is Coney's own, or open questions, or a summary: only a last-resort home
    shared: bool = False  # a table row that lists more than one function in its first cell (`0x1 / 0x2`)
    headed: bool = False  # the heading of the section names the function: the whole section is about it
    row: bool = False  # the candidate is a table row (of a table the function tables do not read)


@dataclass(frozen=True)
class Override:
    """A hand reading of a page for one function, where the rules of this module cannot express what the page says.

    `page` is the entry's page (`file.md#section`), `level` the level key the page gives the function, `source` the
    words that say so, as `file.md: words` (the file may be another page than `page`'s: the level is then said to be
    from it), and `reason` why the rules get it wrong. `override_problems` checks that each still matches its page.
    """

    page: str
    level: str
    source: str
    reason: str


#: The functions whose home or level the rules get wrong and a reader sees at once, by address. Keep it short: a rule
#: that fixes a kind of case belongs in the importer. The reasons cite the page lines (as of 2026-10-23).
OVERRIDES: dict[int, Override] = {
    # The block names the function in the middle of a sentence or after its own topic, so the rules read a mention.
    0x0038D188: Override(  # Car_ReleaseTrunkItem
        "cars.md#parts",
        "confirmed-code",
        "cars.md: `Car_ReleaseTrunkItem` (`0x0038d188`) moves the",
        "cars.md:158-162: the boot-item item is about it and ends 'Confirmed (code).'; it opens with its topic",
    ),
    0x004134F8: Override(  # PlayerBox_Update
        "scripting.md#triggers",
        "confirmed-code",
        "scripting.md: update `0x004134f8`): the same",
        "scripting.md:362-367: the kind-2 box item gives its update in the opening parenthesis; 'Confirmed (code).'",
    ),
    0x002DF288: Override(  # TrackHumanGoal_Start
        "ai-gangs.md#formations",
        "confirmed-code",
        "ai-gangs.md: Start (`0x002df288`) joins the target's formation",
        "ai-gangs.md:1425-1433: the GoalTrackHuman paragraph walks Start, End and Process and ends 'Confirmed (code)'",
    ),
    0x00357998: Override(  # Script_GetPlatform
        "gui.md#coneys-implementation",
        "confirmed-code",
        "gui.md: it returns **1** (`0x00357998`)",
        "gui.md:1336-1338: 'confirmed (code)' for the return value; the wording is the separate runtime remark",
    ),
    0x003579A0: Override(  # ScriptSystem_DoFileLua
        "gui.md#coneys-implementation",
        "confirmed-code",
        "formats/lua-chunks.md: confirmed (code) at `0x003579a0` `ScriptSystem_DoFileLua`",
        "formats/lua-chunks.md:86-88 grades it; that page has front matter, which the importer does not read",
    ),
    0x002195E0: Override(  # Human_SetCoverFlag5
        "sound-events.md#covered",
        "confirmed-code",
        "sound-events.md: `Human_SetCoverFlag5` `0x002195e0`, which also pauses the wind-blown garbage",
        "sound-events.md:702-707: the section is about the byte it sets; 'Confirmed (code)'",
    ),
    0x00165678: Override(  # Gang_GetLeader
        "ai-gangs.md#tactic-kinds",
        "confirmed-code",
        "ai-gangs.md: **Leader**: gang `+0x44` holds the leader's handle; `0x00165678` returns him",
        "ai-gangs.md:214-216: the first bullet of 'Shared behaviour, confirmed (code):' describes it",
    ),
    0x003860B8: Override(  # Object_SetLuaMsgHandler
        "scripting.md#message-handlers",
        "confirmed-code",
        "scripting.md: (`0x00386298` → `0x003860b8`) gives the object a",
        "scripting.md:276-283: the SetMsgHandler paragraph describes it, 'Confirmed (code).'; cars.md lists addresses",
    ),
    0x00415378: Override(  # VolumeBox_Update
        "scripting.md#triggers",
        "confirmed-code",
        "scripting.md: The update (`0x00415378`), while enabled (byte `+0x68`), collects the humans",
        "scripting.md:346-352: the volume-box item describes the update, under 'Confirmed (code):'; rumble.md lists",
    ),
    0x0041C4E0: Override(  # WarriorCommand_Dispatch
        "ai-gangs.md#warrior-commands",
        "confirmed-code",
        "ai-gangs.md: go through one dispatcher, `0x0041c4e0(state, chief, command, forced, pos, arg)`, confirmed",
        "ai-gangs.md:873-878: the section's paragraph and steps describe the dispatcher; characters.md names it",
    ),
    0x00336A00: Override(  # Quat_Slerp
        "maths.md#rotations",
        "confirmed-code",
        "maths.md: | `0x00336a00` | `Quat_Slerp` | Spherical interpolation of two quaternions",
        "maths.md:83: its own full row; physics.md:56 is a one-line row and objects.md:90 a shared one",
    ),
}


def _overridden(
    override: Override, address: int, name: str, port: str, found: list[Candidate]
) -> research_functions.Entry:
    """The entry an override gives: its page and level, the role the page's own row gives, and a note naming the page
    the level is from when that is not the entry's own."""
    role = next((c.entry.role for c in found if c.entry.page == override.page and c.entry.role), "")
    source = override.source.partition(": ")[0]
    note = "" if source == override.page.partition("#")[0] else f"the level is from [{source}]({source})"
    return research_functions.Entry(
        address, name, override.page, override.level, role=role, note=note, coney=port, status="legacy"
    )


def _source_problems(
    label: str, address: int, override: Override, source: str, words: str, page: docs_page.Page
) -> list[str]:
    """Why the block of `page` that holds `words` does not support `override`: it is missing, does not cite the
    address, or states no such level (itself, or in the lead line of its list: the paragraph above that ends in a
    colon)."""
    wanted = " ".join(words.split())
    found = [
        (index, block)
        for index, block in enumerate(page.blocks)
        if block.kind != "heading" and wanted in " ".join(block.text.split())
    ]
    if not found:
        return [f"{label}: {source} no longer says {words!r}"]
    cited = [(i, b) for i, b in found if address in _addresses(b.text)]
    if not cited:
        return [f"{label}: the block of {source} with {words!r} does not cite the address"]
    for index, block in cited:
        texts = [block.text]
        while index > 0 and page.blocks[index].item:
            index -= 1
        if (
            page.blocks[index].kind == "text"
            and not page.blocks[index].item
            and page.blocks[index].text.rstrip().endswith(":")
        ):
            texts.append(page.blocks[index].text)
        if any(override.level in {_KEYS[k] for k in levels_in(text).kinds} for text in texts):
            return []
    return [f"{label}: the block of {source} with {words!r} does not state {override.level}"]


def override_problems(overrides: Mapping[int, Override], pages: Mapping[str, str]) -> list[str]:
    """Why an override no longer matches its page: the page or its section is gone, or the block that holds the quoted
    words no longer cites the address or states the level. Empty when every override still holds."""
    problems = []
    for address, override in sorted(overrides.items()):
        label = f"{address:#010x} {override.page}"
        file, section = override.page.partition("#")[0], override.page.partition("#")[2]
        if file not in pages:
            problems.append(f"{label}: the page is gone")
            continue
        if section not in docs_page.heading_ids(docs_page.read_page(pages[file])):
            problems.append(f"{label}: the page has no section {section!r}")
        if override.level not in research_functions.LEVELS:
            problems.append(f"{label}: {override.level!r} is no level")
        source, _, words = override.source.partition(": ")
        if source not in pages:
            problems.append(f"{label}: the source page {source} is gone")
            continue
        problems += _source_problems(label, address, override, source, words, docs_page.read_page(pages[source]))
    return problems


@dataclass
class Report:
    """What the move did, for the person who reviews it."""

    rows: int = 0
    added: int = 0  # new entries
    updated: int = 0  # entries the database had, now with `coney`
    unchanged: int = 0  # entries that already had it
    sources: Counter[str] = field(default_factory=Counter)  # new entries by where their page and level came from
    systems: Counter[str] = field(default_factory=Counter)  # new entries by system
    renamed: list[str] = field(default_factory=list)  # rows whose name is not Ghidra's
    unlisted: list[str] = field(default_factory=list)  # rows the Ghidra listing has no function entry for
    ungraded: list[str] = field(
        default_factory=list
    )  # entries made with no level, for want of one tied to the function
    borrowed: list[str] = field(default_factory=list)  # entries whose level is from a page other than their own
    disagreeing: list[str] = field(default_factory=list)  # entries another source of the research grades differently
    skipped: list[str] = field(default_factory=list)  # rows that got no entry, with the reason
    untagged: list[str] = field(default_factory=list)  # rows with no `@orig` tag in `src/`


@dataclass
class Plan:
    """The database after the move, and the report."""

    systems: list[research_functions.SystemFunctions]
    changed: set[str]  # the systems whose file the move writes
    report: Report


# --- the retired file ---------------------------------------------------------------------------------------------


def read_legacy(path: Path) -> list[LegacyRow]:
    """The rows of the retired list at `path`. Raises ProgressError for a missing file or a malformed row."""
    try:
        data = tomllib.loads(path.read_bytes().decode("utf-8"))
    except FileNotFoundError as error:
        raise ProgressError(f"{path}: missing") from error
    except (tomllib.TOMLDecodeError, UnicodeDecodeError, OSError) as error:
        raise ProgressError(f"{path}: {error}") from error
    rows = []
    for number, item in enumerate(data.get("function", []), start=1):
        where = f"{path}: function {number}"
        address, name, size = item.get("address"), item.get("name"), item.get("size")
        if not isinstance(address, int) or isinstance(address, bool) or address < 0:
            raise ProgressError(f"{where}: address must be a non-negative integer, got {address!r}")
        if not isinstance(name, str) or not name:
            raise ProgressError(f"{where}: name must be a non-empty string, got {name!r}")
        if size is not None and (not isinstance(size, int) or isinstance(size, bool) or size < 0):
            raise ProgressError(f"{where}: size must be a non-negative integer, got {size!r}")
        rows.append(LegacyRow(address, name, str(item.get("subsystem", "")), size))
    return rows


def legacy_functions(root: Path, listing: list[ListedFunction] | None) -> list[Function] | None:
    """The reimplemented functions of a checkout that still has the retired list, or None when it has not.

    A size is the Ghidra listing's where it lists the function (as the tracker measures now), else the one the row
    recorded, so a day before the move is measured by today's definition as far as its inputs allow.
    """
    path = root / progress.LEGACY_FUNCTIONS_FILE
    if not path.is_file():
        return None
    sizes = {f.address: f.size for f in listing or []}
    return [
        Function(row.address, row.name, row.subsystem, sizes.get(row.address, row.size)) for row in read_legacy(path)
    ]


# --- the systems --------------------------------------------------------------------------------------------------


def system_of(page: str, pages: set[str]) -> str:
    """The system a page below docs/research belongs to: its name, or the longest prefix of it that is a page too.

    `combat-grabs.md` belongs to `combat` when `combat.md` is a page, `hud-functions-lua.md` to `hud`; `file-io.md`
    stays `file-io`. The folder of a page (`formats/animation.md`) is not a system and is left out. A script binding's
    reference page belongs to `scripting`, whatever its group.
    """
    if page.startswith(BINDINGS_FOLDER):
        return research_functions.BINDINGS_SYSTEM
    stem = page.partition("#")[0].rpartition("/")[2].removesuffix(".md")
    parts = stem.split("-")
    for count in range(len(parts) - 1, 0, -1):
        prefix = "-".join(parts[:count])
        if f"{prefix}.md" in pages:
            return prefix
    return stem


# --- what research says about a function --------------------------------------------------------------------------


def _table_candidates(pages: Mapping[str, str], wanted: set[int]) -> dict[int, list[Candidate]]:
    """The entries the pages' function tables give for the `wanted` addresses (research_import)."""
    found: dict[int, list[Candidate]] = {}
    for page, text in sorted(pages.items()):
        shared = _shared_rows(page, text)
        for entry in research_import.import_tables(text, page, source=page).entries:
            if entry.address in wanted:
                found.setdefault(entry.address, []).append(
                    Candidate("table", entry, shared=(entry.page, entry.address) in shared)
                )
    return found


def _shared_rows(page: str, text: str) -> set[tuple[str, int]]:
    """(section, address) of each table row of the page whose first cell lists several functions: the row is about them
    together, so a row of the function's own elsewhere describes it better."""
    parsed = docs_page.read_page(text)
    anchors = docs_page.heading_ids(parsed)
    found = set()
    for block in parsed.blocks:
        if block.kind != "row" or block.section < 0:
            continue
        cells = docs_page.split_cells(block.text)
        if cells and len(_outside_ranges(cells[0])) > 1:
            found |= {(f"{page}#{anchors[block.section]}", a) for a in _outside_ranges(cells[0])}
    return found


_LEAD_IN = re.compile(r"^[\s>*_\-0-9.)]*(?:confirmed\s*\(\s*(?:code|runtime)\s*\)|inferred|speculative)", re.IGNORECASE)
_LEVEL_PHRASE = re.compile(
    r"confirmed\s*\(\s*(?:code|runtime)\s*\)|\binferred\b|\bspeculative\b|\bnot needed\s*:", re.IGNORECASE
)
#: The levels a sentence of prose may give a function. An aside such as "(the player; inferred)" or "(runtime test
#: setup)" qualifies a detail of the sentence, so only a code reading, which the page states for what it describes, or a
#: "not needed: <reason>" counts; any other level needs a table's Evidence cell or a block that opens with it.
_CLAIM_LEVELS = frozenset({"confirmed-code", "not-needed"})
_SENTENCE_END = re.compile(r"(?<=[.!?])\s+(?=[A-Z`*(\[])")
_ADDRESS = re.compile(r"\b0x([0-9a-fA-F]{8})\b")
_KEYS = {
    "code": "confirmed-code",
    "runtime": "confirmed-runtime",
    "inferred": "inferred",
    "speculative": "speculative",
    "not-needed": "not-needed",
}


def sentences(text: str) -> list[str]:
    """`text` cut into sentences; a full stop inside a code span (`".lua"`) or a number (`0.3`) ends none."""
    masked = re.sub(r"`[^`]*`", lambda m: "`" * len(m.group(0)), text)
    parts, start = [], 0
    for match in _SENTENCE_END.finditer(masked):
        parts.append(text[start : match.start()])
        start = match.end()
    parts.append(text[start:])
    return [p for p in parts if p.strip()]


def _one_level(levels: Levels) -> str:
    """The key of the level `levels` states when it states exactly one in a valid form, else an empty string."""
    if len(levels.kinds) != 1 or levels.refused:
        return ""
    return _KEYS[next(iter(levels.kinds))]


def _spelled_in(name: str, sentence: str) -> bool:
    """True when `name` is a word of `sentence` (`End_It` is not in `Pre_End_It`)."""
    return re.search(rf"(?<![A-Za-z0-9_]){re.escape(name)}(?![A-Za-z0-9_])", sentence) is not None


#: A binding wrapper's address before an arrow (`0x1 → 0x2`): the wrapper beside the function it calls.
_ARROW_SOURCE = re.compile(r"0x([0-9a-fA-F]{8})[`)\s]*(?:→|->)")
#: What may lie between a function's address and the level that follows it, for the level to be that function's: only
#: punctuation and markup (`0x1`, confirmed (code)` / `0x1`); inferred`).
_ADJACENT = re.compile(r"[\s,;:)\]\[`*_(]*")
_LEVEL_AT = re.compile(
    rf"({_LEVEL_PHRASE.pattern}),?\s*(?:[^`.;:()\n]{{0,40}}?\s)?\bat\s+"
    r"((?:`?0x[0-9a-fA-F]{8}`?(?:\s*(?:,\s*and|,|and|-)\s*)?)+)",
    re.IGNORECASE,
)


def _functions_in(sentence: str, address: int, names: Mapping[int, frozenset[str]] | None) -> list[tuple[int, int]]:
    """(position, address) of each function `sentence` cites, in order. A binding wrapper's address before an arrow
    is the wrapper of the function after it, and, once `names` says which addresses are functions, a data word
    (`0x110c0880`) is no function of the sentence."""
    wrappers = {int(m.group(1), 16) for m in _ARROW_SOURCE.finditer(sentence)} - {address}
    found = []
    for match in _ADDRESS.finditer(sentence):
        cited = int(match.group(1), 16)
        if cited in wrappers or (names is not None and cited != address and cited not in names):
            continue
        found.append((match.start(), cited))
    return found


def _about(sentence: str, address: int, names: Mapping[int, frozenset[str]] | None) -> bool:
    """True when a level in `sentence` can be about the function at `address`: the sentence cites no other function,
    or it names every function it cites (`names` by address), so the address of a caller beside it does not make the
    level a statement about the whole sentence."""
    cited = {a for _, a in _functions_in(sentence, address, names)}
    if cited <= {address}:
        return True
    known = names or {}
    return all(any(_spelled_in(n, sentence) for n in known.get(a, frozenset())) for a in cited)


def _attached(sentence: str, address: int, names: Mapping[int, frozenset[str]] | None) -> bool:
    """True when the level of `sentence` is attached to the function at `address` rather than to a neighbour: it is the
    only function cited, the level leads or trails every address, or it stands right after this address (between two
    addresses, `A (0x1, confirmed (code)): ... B (0x2)`, it belongs to the one before it)."""
    cited = _functions_in(sentence, address, names)
    level = _LEVEL_PHRASE.search(sentence)
    if len({a for _, a in cited}) < 2 or level is None:
        return True
    before = [a for position, a in cited if position < level.start()]
    after = [a for position, a in cited if position > level.start()]
    return not before or not after or before[-1] == address


#: The next function of a call chain (`0x1 -> 0x2 -> 0x3`, with a parenthetical allowed after a link).
_CHAIN_NEXT = re.compile(r"(?:[`\s]|\([^()]*\)|[;,][^()]*\))*(?:→|->)\s*`?0x[0-9a-fA-F]{8}`?")


def _clause(sentence: str, position: int) -> tuple[str, int]:
    """The part of `sentence` between semicolons outside parentheses that holds `position`, and where it starts."""
    depth, start = 0, 0
    for index, char in enumerate(sentence):
        if char == "(":
            depth += 1
        elif char == ")":
            depth = max(0, depth - 1)
        elif char == ";" and depth == 0:
            if index >= position:
                return sentence[start:index], start
            start = index + 1
    return sentence[start:], start


def _clauses(sentence: str) -> list[str]:
    """`sentence` cut at the semicolons outside parentheses."""
    parts, position = [], 0
    while position <= len(sentence):
        text, start = _clause(sentence, position)
        parts.append(text)
        position = start + len(text) + 1
    return parts


def _local_key(sentence: str, address: int, names: Mapping[int, frozenset[str]] | None) -> tuple[str, bool]:
    """The level a sentence gives the function at `address` by where it stands, as (key, any level allowed).

    Read in the clause that holds the address (a semicolon outside parentheses ends a clause). A level right after the
    address, or after the call chain it ends (`0x1 → 0x2 → 0x3`), with only punctuation between (`(F, 0x1, confirmed
    (code))`, `(0x1); inferred from both readers`), is that function's whatever it is. A code reading that follows the
    first function of the clause, with no other function between, is the claim of its subject (`F (0x1) copies the
    rest, confirmed (code): the stats (0x2)`). Empty when neither holds or the clause states more than one level.
    """
    first = next((position for position, a in _functions_in(sentence, address, names) if a == address), None)
    if first is None:
        return "", False
    clause, offset = _clause(sentence, first)
    cited = _functions_in(clause, address, names)
    mine = [position for position, a in cited if a == address]
    if not mine:
        return "", False
    end = mine[0] + len("0x00000000")
    while (link := _CHAIN_NEXT.match(clause, end)) is not None:
        end = link.end()
    phrase = _LEVEL_PHRASE.search(clause, end)
    key = _one_level(levels_in(clause))
    if phrase is not None and not key:
        return "", False
    if phrase is None:
        # `(0x1); inferred from both readers`: the next clause opens with the level the address ends on.
        following = sentence[offset + len(clause) + 1 :]
        if not levels_in(clause).kinds and _ADJACENT.fullmatch(clause[end:]) and _LEAD_IN.match(following):
            after = _one_level(levels_in(_clause(following, 0)[0]))
            return (after, True) if after else ("", False)
        return "", False
    between = [position for position, a in cited if end <= position < phrase.start()]
    if not between and _ADJACENT.fullmatch(clause[end : phrase.start()]):
        return key, True
    if cited[0][1] == address and not between and key in _CLAIM_LEVELS:
        return key, False
    return "", False


def _listed_keys(parts: list[str], address: int) -> set[str]:
    """The levels the sentences give the function at `address` by listing it after a level: `Confirmed (code) at A, B
    and C`, `the door side, confirmed (code) at A, B:`. Only a code reading or a `not needed` is taken."""
    keys = set()
    for part in (clause for sentence in parts for clause in _clauses(sentence)):
        key = _one_level(levels_in(part))
        if key not in _CLAIM_LEVELS:
            continue
        for match in _LEVEL_AT.finditer(part):
            if address in _addresses(match.group(2)):
                keys.add(key)
    return keys


def _level_only(sentence: str) -> bool:
    """True for a short sentence that is only a level (`Confirmed (code).`): it has no code of its own to be about."""
    return "`" not in sentence and _LEAD_IN.match(sentence) is not None and len(sentence) < 90


def tied_level(text: str, address: int, names: Mapping[int, frozenset[str]] | None = None) -> tuple[str, str] | None:
    """The level `text` (a paragraph, list item or row with no Evidence column) states for the function at `address`,
    as (level key, how: `claim` or `block`), or None when no level can be tied to that function alone.

    Tied is only: the sentence that cites the address states exactly one level in a valid form, about the function
    itself (`_about`) and attached to it, not to a neighbour's address (`_attached`); a level-only sentence without
    code right after it, or after one sentence between (`Confirmed (code).` closing two statements), counts as part of
    it; a level that stands right after the address (`_local_key`); a level that lists the address (`_listed_keys`);
    or the block opens with a level ("Confirmed (code) at the addresses given:") and states no other. A sentence gives
    only a code reading or a "not needed" (`_CLAIM_LEVELS`) unless the level stands right after the address:
    "inferred" or "runtime" inside a sentence qualifies a detail. A paragraph that says "confirmed (runtime)" about a
    table below it, or grades two claims differently, ties nothing.
    """
    parts = sentences(text)
    keys: set[str] = set()
    anywhere = False  # a level that stands right after the address may be any
    refused = False
    stated = False  # some sentence is about the function
    for i, part in enumerate(parts):
        if address not in _addresses(part):
            continue
        local, free = _local_key(part, address, names)
        if local:
            keys.add(local)
            anywhere = anywhere or free
            refused = refused or bool(levels_in(part).refused)
            stated = True
            continue
        about = _about(part, address, names)
        functions = _functions_in(part, address, names)
        # The first function of a sentence is its subject: a level-only sentence after it grades it, whatever else
        # the sentence cites.
        if not about and not (functions and functions[0][1] == address):
            continue
        if about:
            stated = True
            refused = refused or bool(levels_in(part).refused)
            if _attached(part, address, names):
                keys |= {_KEYS[k] for k in levels_in(part).kinds}
        for gap in (1, 2):
            if gap == 2 and (i + 1 >= len(parts) or levels_in(parts[i + 1]).kinds):
                break
            after = parts[i + gap] if i + gap < len(parts) else ""
            if after and _level_only(after):
                keys |= {_KEYS[k] for k in levels_in(after).kinds}
                stated = True
                break
    listed = _listed_keys(parts, address)
    keys |= listed
    if not stated and not listed:
        return None
    if len(keys) == 1 and not refused:
        (key,) = keys
        if key in _CLAIM_LEVELS or anywhere:
            return key, "claim"
    if not keys and _LEAD_IN.match(text) and _one_level(levels_in(text)):
        return _one_level(levels_in(text)), "block"
    return None


def _addresses(text: str) -> set[int]:
    """The addresses `text` cites: any `0x` and eight hex digits (a data address is one too)."""
    return {int(m.group(1), 16) for m in _ADDRESS.finditer(text)}


#: What a lead line says about its reach: "at the addresses cited", "unless marked", "for each function below", or a
#: colon that opens the list. A first paragraph that merely states a level about its own subject is not one.
_SCOPE = re.compile(r"\b(?:cited|address|addresses|given|unless|marked|each|below|section)\b|:\s*$", re.IGNORECASE)


def _is_scope(sentence: str) -> bool:
    """True for a sentence that states a level and the extent of it (`_SCOPE`)."""
    return bool(levels_in(sentence).kinds) and _SCOPE.search(sentence) is not None


def _leads(page: docs_page.Page) -> dict[int, Levels]:
    """The lead line of each heading of level 3 or deeper: a statement citing no address that states a valid level for
    the blocks under the heading (research_evidence.lead_lines)."""
    leads: dict[int, Levels] = {}
    for index, block in enumerate(page.blocks[:-1]):
        if block.kind != "heading" or page.headings[block.section].level < 3:
            continue
        follower = page.blocks[index + 1]
        if follower.kind != "text" or follower.section != block.section or cites(follower.text):
            continue
        levels = levels_in(follower.text)
        if levels.kinds and not levels.refused and any(_is_scope(part) for part in sentences(follower.text)):
            leads[block.section] = levels
    return leads


#: A block that opens with one bold, italic or code span (`**The type**`, `` `Name(args)` ``, `**`Name`**`) or a link
#: (``[`SetHandler`](../x.md#sethandler)``), the way a page introduces what it describes.
_OPENING_SPAN = re.compile(r"^\s*(?:[-*+]\s+|\d+[.)]\s+)?(?:\*\*.+?\*\*|\*[^*]+\*|`[^`]+`|_[^_]+_|\[[^\]]*\]\([^)]*\))")
#: What may stand between that span and the address it introduces (once the function's own names are taken out): an
#: opening parenthesis, or a chain of the wrapper addresses that lead to it, each perhaps after its name
#: (`(0x1 → 0x2 → 0x3)`, ``(`Sectors_SectorOf` `0x1` → `0x2`)``).
_INTRO_GAP = re.compile(r"[\s(,*`_]*(?:(?:[A-Za-z_]\w*[\s`,]*)?0x[0-9a-fA-F]{8}[\s`)*_]*(?:→|->)[\s`(*_]*)*")
_LINK = re.compile(r"\[[^\]]*\]\([^)]*\)")
#: A range of addresses (`0x1`-`0x2`): its ends are the edges of a stretch of code, not functions the text is about.
_RANGE = re.compile(r"0x[0-9a-fA-F]{8}[`\s]*[-\u2013\u2014][`\s]*`?0x[0-9a-fA-F]{8}")


def _outside_ranges(text: str) -> set[int]:
    """The addresses `text` cites other than as the end of a range (`0x1`-`0x2`)."""
    return _addresses(_RANGE.sub(" ", text))


def _opens_with(block: docs_page.Block, address: int, spelled: frozenset[str]) -> bool:
    """True when the block opens with one span and the function comes at once: its address before any colon or other
    function (`**The type** (`0x...`): ...`, `- **`Name(args)`** (`0x...`): ...`, ``(`Name`, `0x...`)``), or, after a
    head such as `(`0x1`)` or `(answered)` and a colon, its name (``**A point's area** (`0x1`): `Name` (`0x2`) casts``).
    """
    opening = _OPENING_SPAN.match(block.text)
    if opening is None:
        return False
    names = "|".join(re.escape(n) for n in sorted(spelled))
    rest = block.text[opening.end() :]
    token = f"0x{address:08x}"
    position = rest.lower().find(token)
    if position >= 0:
        gap = _LINK.sub(" ", rest[:position])
        if names:
            gap = re.sub(rf"(?<![A-Za-z0-9_])(?:{names})(?![A-Za-z0-9_])", "", gap)
        if _INTRO_GAP.fullmatch(gap) is not None:
            return True
    head = re.sub(r"^(?:\s*\([^()]*\))*\s*[:,]?\s*", "", rest)
    return bool(names) and re.match(rf"`(?:{names})`", head) is not None


def _describes(block: docs_page.Block, address: int, heading: str, spelled: frozenset[str]) -> bool:
    """True when the block (or the heading above it) is about the function at `address` rather than mentions it.

    A table row is about the function whose address stands in its first cell with little else around it (or in its
    second, when the first is the function's name); a heading that names the function makes every other block under it
    about it; any other block is when it opens with one span and the address comes at once (`_opens_with`).
    """
    if block.kind == "row":
        cells = docs_page.split_cells(block.text)
        if cells and address in _addresses(cells[0]) and _alone(cells[0]):
            return True
        # `| Name | Address | ... |`: the function's own name leads the row.
        return len(cells) > 1 and address in _addresses(cells[1]) and any(_spelled_in(n, cells[0]) for n in spelled)
    return any(_spelled_in(name, heading) for name in spelled) or _opens_with(block, address, spelled)


_CODE_SPAN = re.compile(r"\[[^\]]*\]\([^)]*\)|`[^`]*`|\([^()]*\)")


def _alone(cell: str) -> bool:
    """True when `cell` is an address (or a few) with little else around it (`0x1`, `Name 0x1 (vtable 0x2)`, `start 0x1,
    tick 0x2`), not a sentence that happens to cite it (`sets +0x04 while active; ... (called from 0x1)`)."""
    words = re.findall(r"[A-Za-z]+", _CODE_SPAN.sub(" ", cell))
    return len(words) <= 2 * len(_addresses(cell)) + 1


def _row_subject(block: docs_page.Block, address: int, skip: int | None = None) -> bool:
    """True when the function at `address` is what a table row lists rather than something its prose mentions: some
    cell holds the address with little around it (`_alone`). The Evidence cell (`skip`) is the row's grade, never its
    subject."""
    return any(
        index != skip and address in _addresses(cell) and _alone(cell)
        for index, cell in enumerate(docs_page.split_cells(block.text))
    )


#: A sentence that opens with a level, whatever the case of the word before it: `Confirmed (code) for the rays;`.
_SENTENCE_START = re.compile(r"(?<=[.!?])\s+(?=[A-Za-z`*(\[])")


def closing_key(text: str, address: int) -> str:
    """`confirmed-code` when every sentence of `text` that opens with a level opens with a code reading, and there is
    one; else an empty string. Such a block, which opens with the function it describes, grades it by that closing
    statement however many asides and other addresses it holds (`inferred` in a clause qualifies a detail), unless the
    closing statement lists other addresses (`Confirmed (code) at A, B`) without this one."""
    masked = re.sub(r"`[^`]*`", lambda m: "`" * len(m.group(0)), text)
    keys, start = set(), 0
    for match in [*_SENTENCE_START.finditer(masked), None]:
        end = match.start() if match is not None else len(text)
        sentence = text[start:end]
        start = match.end() if match is not None else end
        lead = _LEAD_IN.match(sentence)
        if lead is None:
            continue
        keys.add(_one_level(levels_in(lead.group(0))))
        at = _LEVEL_AT.search(sentence)
        if at is not None and address not in _addresses(at.group(2)):
            return ""
    return "confirmed-code" if keys == {"confirmed-code"} else ""


def _weak(page: docs_page.Page, index: int, section: str, ungraded: set[str]) -> bool:
    """True when the heading at `index`, or one above it, is Coney's own code, open questions or a summary, or the
    page's section of that name is one the conventions say states no claims (`research.ungraded_sections`)."""
    if section.casefold() in ungraded:
        return True
    level = 99
    for heading in reversed(page.headings[: index + 1]):
        if 1 < heading.level < level:
            level = heading.level
            if _WEAK_HEADING.search(heading.text):
                return True
    return False


def _prose_candidates(
    pages: Mapping[str, str], wanted: set[int], names: Mapping[int, frozenset[str]], research: Research
) -> dict[int, list[Candidate]]:
    """The entries the other mentions of the `wanted` addresses give, each graded only by what is tied to that function.

    A paragraph, list item or row that cites the address gives `claim` or `block` (tied_level; a block that opens
    with the function and closes with a code reading, `closing_key`), a row's own Evidence cell gives `block` (for the
    function whose address starts the row), and the lead line of the heading above gives `lead` to a block about the
    function that states no level itself. A mention that ties none, or the end of a range of addresses, is `cited`: no
    level and a note saying to read the page. Weak sections (Coney's, open questions, summaries, `*-reimpl.md`) grade
    what they state too, but give no lead line's level. Each candidate says whether it describes the function
    (`_describes`), whether its heading names it, and whether its section is a weak one (`_weak`).
    """
    ungraded = {name.casefold() for name in research.ungraded_sections}
    found: dict[int, list[Candidate]] = {}
    for page, text in sorted(pages.items()):
        parsed = docs_page.read_page(text)
        anchors = docs_page.heading_ids(parsed)
        sections = section_names(parsed, research, research.kind_of_path(f"docs/research/{page}"))
        leads = _leads(parsed)
        for block in parsed.blocks:
            if block.kind == "heading":
                continue
            cited = _addresses(block.text) & wanted
            if not cited:
                continue
            anchor = anchors[block.section] if block.section >= 0 else ""
            target = f"{page}#{anchor}" if anchor else page
            section = sections[block.section] if block.section >= 0 else ""
            weak = block.section < 0 or page.endswith("-reimpl.md") or _weak(parsed, block.section, section, ungraded)
            heading = parsed.headings[block.section].text if block.section >= 0 else ""
            outside = _outside_ranges(block.text)
            for address in sorted(cited):
                spelled = names.get(address, frozenset())
                named = any(n in block.text or n in heading for n in spelled)
                # The end of a range (`0x1`-`0x2`) is an edge of code the text speaks of, not a function it is about.
                ranged = address not in outside
                describes = not ranged and _describes(block, address, heading, spelled)
                headed = not ranged and any(_spelled_in(name, heading) for name in spelled)
                subject = describes or (
                    not ranged and block.kind == "row" and _row_subject(block, address, _level_column(parsed, block))
                )
                # Every section grades the claims it states itself; a weak one (Coney's, open questions, a summary, a
                # page of corrections) is only a home of last resort and gives no lead line's level.
                graded = None
                if section and not ranged:
                    graded = _grade(parsed, block, address, leads, names, subject, weak, describes, spelled)
                tied = _prose_entry(address, target, graded[0], block.text) if graded is not None else None
                if graded is None or tied is None:
                    # No level tied to this function alone: the entry has none, and its note says to read the page.
                    entry = research_functions.Entry(address, "", target, note=UNGRADED_NOTE)
                    found.setdefault(address, []).append(
                        Candidate("cited", entry, named, 0, describes, weak, headed=headed, row=block.kind == "row")
                    )
                else:
                    found.setdefault(address, []).append(
                        Candidate(graded[1], tied, named, 0, describes, weak, headed=headed, row=block.kind == "row")
                    )
    for address, candidates in found.items():
        counts = Counter(c.entry.page for c in candidates)
        found[address] = [replace(c, mentions=counts[c.entry.page]) for c in candidates]
    return found


def _level_column(page: docs_page.Page, block: docs_page.Block) -> int | None:
    """The index of the Evidence cell of the table the row `block` belongs to, or None."""
    return page.tables[block.table].level_column if block.table is not None else None


def _grade(
    page: docs_page.Page,
    block: docs_page.Block,
    address: int,
    leads: Mapping[int, Levels],
    names: Mapping[int, frozenset[str]],
    subject: bool,
    weak: bool,
    describes: bool,
    spelled: frozenset[str],
) -> tuple[str, str] | None:
    """The level of the block for the function at `address` and how it is tied (`claim`, `block` or `lead`), or None."""
    if block.kind == "row" and block.table is not None:
        # A row of a table: with an Evidence column that cell is the row's claim, for the function(s) whose address
        # is listed in it; without one the row grades nothing, whatever level a cell mentions about its own field.
        column = page.tables[block.table].level_column
        if column is None:
            return None
        cells = docs_page.split_cells(block.text)
        key = _one_level(levels_in(cells[column])) if column < len(cells) else ""
        if not key:
            return None
        # A row that is about something else grades the function only where its Evidence cell lists the address.
        if not subject and _listed_keys([cells[column]], address) != {key}:
            return None
        return key, "block"
    if levels_in(block.text).kinds or levels_in(block.text).refused:
        tied = tied_level(block.text, address, names)
        if tied is None and describes and _opens_with(block, address, spelled):
            # A block that opens with the function and closes with a code reading grades it, whatever asides it holds.
            key = closing_key(block.text, address)
            return (key, "block") if key else None
        return tied
    lead = leads.get(block.section)
    key = _one_level(lead) if lead is not None and not weak else ""
    return (key, "lead") if key else None


def _prose_entry(address: int, target: str, level: str, text: str) -> research_functions.Entry | None:
    """An entry at `target` with `level`, or None for a `not needed` with no reason after it, which the schema refuses.
    The note is left out: for a paragraph it would be the whole paragraph."""
    if level == "not-needed":
        reason = re.split(r"not needed\s*:", text, maxsplit=1, flags=re.IGNORECASE)[-1]
        reason = re.split(r"(?<=[.!?])\s", reason.strip())[0].strip(" .;")
        return research_functions.Entry(address, "", target, level, role=reason) if reason else None
    return research_functions.Entry(address, "", target, level)


def binding_candidates(bindings: Mapping[str, str], wanted: set[int]) -> dict[int, list[Candidate]]:
    """The entries the script binding files give for the wrappers among the `wanted` addresses.

    A wrapper is described by its binding (the reference pages are generated from them), not by a research page; its
    entry points at the section of the generated reference page of its group that names the wrapper's address
    (`../references/bindings/<group>.md#<name in lower case>`, the group being the file's name), with the level the
    binding states.
    """
    found: dict[int, list[Candidate]] = {}
    for _file, text in sorted(bindings.items()):
        for chunk in re.split(r"\n- ", text):
            wrapper = _BINDING_WRAPPER.search(chunk)
            level = _BINDING_LEVEL.search(chunk)
            if wrapper and level and level.group(1) in research_functions.LEVELS and level.group(1) != "not-needed":
                address = int(wrapper.group(1), 16)
                name = _BINDING_NAME.search(chunk)
                if address in wanted and name is not None:
                    page = f"{BINDINGS_FOLDER}{Path(_file).stem}.md#{name.group(1).lower()}"
                    entry = research_functions.Entry(address, "", page, level.group(1))
                    found.setdefault(address, []).append(Candidate("binding", entry))
    return found


def _home_class(candidate: Candidate) -> int:
    """How good a home the candidate's section is, best first: the function's own table row;
    its script binding's reference (the maintainer's rule: a wrapper's entry points at the binding reference); a
    section whose heading names it, or a row of a table that is its own; a block that is about it; a block that only
    mentions it; and last a Coney, open-questions or corrections section, which describes nothing of the original."""
    if candidate.via == "table":
        return 0
    if candidate.via == "binding":
        return 1
    if candidate.weak:
        return 5
    if candidate.headed or (candidate.describes and candidate.row):
        return 2
    return 3 if candidate.describes else 4


def _ranked(candidates: list[Candidate], weight: Mapping[str, int]) -> list[Candidate]:
    """The candidates as a home for the entry, best first: the kind of section (`_home_class`); one that states a level
    for the function over one that does not, a table row, a sentence of its own, a block, its binding and a lead line in
    that order; then the page with the most reimplemented functions on it (their home
    page, where the others are probably described); then, on that page, the section that names the function over one
    that only cites its address, and the one that cites it most; then the page and section by name."""
    return sorted(
        candidates,
        key=lambda c: (
            _home_class(c),
            c.shared,
            _RANK[c.via],
            -weight.get(c.entry.page_path, 0),
            not c.named,
            -c.mentions,
            c.entry.page,
        ),
    )


def _donor(candidates: list[Candidate], weight: Mapping[str, int]) -> Candidate | None:
    """The best statement of a level for the function among `candidates`, wherever it stands: a table row, a sentence
    of its own, a block, its binding, a lead line over a block about the function, in that order, a non-weak section
    first."""
    # A lead line grades the blocks under its heading, which says nothing of a side remark on another page's subject.
    graded = [c for c in candidates if c.entry.level and (c.via != "lead" or c.describes)]
    if not graded:
        return None
    return min(
        graded,
        key=lambda c: (
            c.weak,
            _RANK[c.via],
            not c.describes,
            not c.named,
            -weight.get(c.entry.page_path, 0),
            c.entry.page,
        ),
    )


def _choose(
    found: list[Candidate], weight: Mapping[str, int], name: str, port: str, page_names: set[str]
) -> tuple[research_functions.Entry, str, Candidate | None] | str:
    """The entry for a function from its candidates, the way it came (a key of `_RANK`) and the donor of its level, or
    the reason no candidate is valid.

    The best home the schema accepts (a table's cell may carry what an entry may not, decompiler output) is taken; a
    home that states no level takes the level another page states for the function, and says where it is from, unless
    that is the home's own page (then the level is the page's own).
    """
    problems = []
    donor = _donor(found, weight)
    for chosen in _ranked(found, weight):
        entry = replace(chosen.entry, name=name, coney=port, status="legacy")
        if not entry.level and donor is not None:
            note = (
                "" if donor.entry.page == entry.page else f"the level is from [{donor.entry.page}]({donor.entry.page})"
            )
            entry = replace(entry, level=donor.entry.level, role=entry.role or donor.entry.role, note=note)
        if not (problem := research_import.entry_problem(entry, system_of(entry.page, page_names))):
            return entry, chosen.via, (donor if not chosen.entry.level else None)
        problems.append(problem)
    return problems[0]


def _with_rivals(
    entry: research_functions.Entry, found: list[Candidate], report: Report, label: str
) -> research_functions.Entry:
    """`entry` with a note on the other grades of the research for the function (a row of a table, a block about the
    function or a code reading of it that gives a level other than the entry's; an `inferred` or runtime remark in a
    block about something else is a side remark, no grade): two are shown, the rest counted. The page that grades a
    function twice, differently, is not hidden."""
    rivals = sorted(
        {
            (c.entry.level, c.entry.page)
            for c in found
            if c.via in ("table", "claim", "block")
            and (c.via == "table" or c.describes or c.entry.level in _CLAIM_LEVELS)
            if c.entry.level not in ("", entry.level)
        }
    )
    if not entry.level or not rivals:
        return entry
    said = "; ".join(f"also graded {level} at [{page}]({page})" for level, page in rivals[:2])
    if len(rivals) > 2:
        said += f"; and {len(rivals) - 2} more"
    report.disagreeing.append(f"{label}: {entry.level} at {entry.page}, {said}")
    return replace(entry, note=f"{entry.note}; {said}" if entry.note else said)


# --- the plan -----------------------------------------------------------------------------------------------------


def plan(
    rows: list[LegacyRow],
    *,
    listing: Mapping[int, ListedFunction],
    ports: Mapping[int, str],
    pages: Mapping[str, str],
    bindings: Mapping[str, str],
    existing: list[research_functions.SystemFunctions],
    research: Research,
    functions_dir: Path = research_functions.FUNCTIONS_DIR,
    overrides: Mapping[int, Override] | None = None,
) -> Plan:
    """The database after the rows of the retired list are moved into it.

    `listing` is the Ghidra listing by address, `ports` the `coney` value of each tagged address, `pages` the research
    pages (their text by path below docs/research, the source map and generated pages left out) and `bindings` the
    binding files' text; `existing` is the database as it stands. An entry that exists keeps everything it says and
    gains `coney` and `status` when it lacks them; a function with no entry gets one from `_ranked`, unless `overrides`
    (`OVERRIDES` by default) names its page and level.
    """
    overrides = OVERRIDES if overrides is None else overrides
    report = Report(rows=len(rows))
    systems = {s.system: s for s in existing}
    owner = {e.address: s.system for s in existing for e in s.entries}
    changed: set[str] = set()
    page_names = set(pages)
    wanted = {row.address for row in rows if row.address not in owner or not _entry(systems, owner, row.address).coney}

    candidates = _table_candidates(pages, wanted)
    # The names each address is called in prose: Ghidra's for every listed function (a sentence that cites several is
    # about the function only if it names them), and for a row also the row's own.
    spelled = {a: frozenset({f.name}) if f.named else frozenset() for a, f in listing.items()}
    for row in rows:
        spelled[row.address] = (
            frozenset({row.name, listing[row.address].name} - {""}) if row.address in listing else frozenset({row.name})
        )
    for other in (_prose_candidates(pages, wanted, spelled, research), binding_candidates(bindings, wanted)):
        for address, more in other.items():
            candidates.setdefault(address, []).extend(more)
    # A page's weight: how many of the functions to place a section of it describes, so the functions of one subject
    # gather on the page that describes the subject.
    weight: Counter[str] = Counter()
    for found_here in candidates.values():
        for page in {c.entry.page_path for c in found_here if _home_class(c) in (0, 2, 3)}:
            weight[page] += 1

    for row in rows:
        label = f"{row.address:#010x} {row.name}"
        ghidra = listing.get(row.address)
        name = ghidra.name if ghidra is not None and ghidra.named else row.name
        if ghidra is None:
            report.unlisted.append(label)
        elif name != row.name:
            report.renamed.append(f"{label}: Ghidra calls it {name}")
        port = ports.get(row.address, "")
        if not port:
            report.untagged.append(label)
            report.skipped.append(f"{label}: no @orig tag in src/, so there is no `coney` to record")
            continue
        if row.address in owner:
            system = owner[row.address]
            entry = _entry(systems, owner, row.address)
            if entry.coney:
                report.unchanged += 1
                continue
            systems[system] = _replace_entry(systems[system], replace(entry, coney=port, status="legacy"))
            changed.add(system)
            report.updated += 1
            continue
        found = candidates.get(row.address, [])
        override = overrides.get(row.address)
        if override is None and not found:
            report.skipped.append(f"{label}: no research page cites it, so there is no page and level for an entry")
            continue
        # A section whose table row is a candidate is that row's: a block of the same section is the row read again.
        table_pages = {c.entry.page for c in found if c.via == "table"}
        found = [c for c in found if c.via != "block" or c.entry.page not in table_pages]
        if override is not None:
            entry, via, donor = _overridden(override, row.address, name, port, found), "override", None
            problem = research_import.entry_problem(entry, system_of(entry.page, page_names))
        else:
            picked = _choose(found, weight, name, port, page_names)
            if isinstance(picked, str):
                report.skipped.append(f"{label}: {picked}")
                continue
            entry, via, donor = picked
            problem = ""
        if problem:
            report.skipped.append(f"{label}: {problem}")
            continue
        if override is None:
            entry = _with_rivals(entry, found, report, label)
            if donor is not None and donor.entry.page != entry.page:
                report.borrowed.append(f"{label}: {entry.page} takes the level of {donor.entry.page}")
        system = system_of(entry.page, page_names)
        file = systems.get(system) or research_functions.SystemFunctions(
            system, functions_dir / f"{system}.toml", "", ()
        )
        systems[system] = replace(file, entries=(*file.entries, entry))
        owner[row.address] = system
        changed.add(system)
        report.added += 1
        report.sources[via] += 1
        report.systems[system] += 1
        if not entry.level:
            report.ungraded.append(f"{label}: {entry.page}")
    ordered = [
        replace(s, entries=tuple(sorted(s.entries, key=lambda e: e.address))) for _name, s in sorted(systems.items())
    ]
    return Plan(ordered, changed, report)


def _entry(
    systems: Mapping[str, research_functions.SystemFunctions], owner: Mapping[int, str], address: int
) -> research_functions.Entry:
    """The database's entry for `address`, which `owner` says exists."""
    return next(e for e in systems[owner[address]].entries if e.address == address)


def _replace_entry(
    system: research_functions.SystemFunctions, entry: research_functions.Entry
) -> research_functions.SystemFunctions:
    """`system` with its entry at the address of `entry` replaced by it."""
    return replace(system, entries=tuple(entry if e.address == entry.address else e for e in system.entries))


# --- the checkout -------------------------------------------------------------------------------------------------


def read_pages(root: Path, skip: frozenset[str], *, include_front_matter: bool = False) -> dict[str, str]:
    """The research pages under `root` by path below docs/research, without the pages named in `skip` and the pages
    with front matter (unless `include_front_matter`): a page that says what it owns is checked to own each function it
    describes, and the move cannot tell whether it does, so those entries come with the page's own work.
    """
    base = root / "docs" / "research"
    pages = {}
    for path in sorted(base.rglob("*.md")) if base.is_dir() else []:
        name = path.relative_to(base).as_posix()
        text = path.read_text(encoding="utf-8", errors="replace")
        if name not in skip and (include_front_matter or not text.startswith("---")):
            pages[name] = text
    return pages


def read_bindings(root: Path) -> dict[str, str]:
    """The script binding files under `root` by file name."""
    base = root / "research" / "bindings"
    return {p.name: p.read_text(encoding="utf-8", errors="replace") for p in sorted(base.glob("*.yaml"))}


def ports_of(rows: Iterable[IndexRow]) -> dict[int, str]:
    """The `coney` value of each tagged address, from the rows of the `@orig` index.

    A function tagged in more than one place gets the tag above a function first, then one inside a function, then the
    rest, and among equals the first in path order. The value is the file and, after a colon, the function or class
    that carries the tag; a name that is not an identifier (a constant) leaves the file alone.
    """
    order = {"comment": 0, "body": 1, "other": 2}
    ports: dict[int, tuple[tuple[int, str, str], str]] = {}
    for row in rows:
        value = f"{row.path}:{row.coney}" if row.coney else row.path
        if not research_functions.CONEY.match(value) or not re.fullmatch(r"[A-Za-z_~][\w:~]*", row.coney or "_"):
            value = row.path
        key = (order[row.placement], row.path, row.coney)
        if row.address not in ports or key < ports[row.address][0]:
            ports[row.address] = (key, value)
    return {address: value for address, (_key, value) in ports.items()}


def write(plan_: Plan, root: Path) -> list[Path]:
    """Write the database files the plan changed under `root`; returns their paths."""
    written = []
    for system in plan_.systems:
        if system.system in plan_.changed:
            path = root / system.path
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(research_functions.dumps(system).encode("utf-8"))
            written.append(path)
    return written
