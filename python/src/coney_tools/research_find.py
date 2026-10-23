# SPDX-License-Identifier: GPL-3.0-or-later
"""The generated pages of the research that find things: the source map's file tables and the find pages.

The pages are for people; the data files are for tools. Nothing generated is committed. The docs build fills the
marker of `docs/research/source-map.md` with the tables of `research/source-files.toml` (mkdocs_hooks.py) and adds
`research/find/addresses.md` (which page describes each stretch of the executable) and `research/find/words.md` (the
player's words, from the glossary); `coney-tools research render OUT_DIR` writes the same pages to a folder to look at,
like the progress tables. The file tables stay on the source map's page, under the same headings as before they were
data, so the links to `source-map.md#gui` and its kin keep their targets.

Research: docs/guides/research-where.md
"""

from __future__ import annotations

from collections.abc import Callable, Mapping, Sequence

from coney_tools.progress_research import ListedFunction
from coney_tools.research_glossary import Glossary
from coney_tools.research_owners import RESEARCH_DIR, Owners
from coney_tools.source_files import SourceFile, SourceFiles, SourceFilesError

#: The source map page below docs/, and the line in it where the file tables go.
SOURCE_MAP_PAGE = "research/source-map.md"
FILES_MARKER = "<!-- source-files:generated -->"
GENERATED_NOTE = (
    "<!-- Generated when the docs are built, from research/source-files.toml. Edit that file, not these tables. -->"
)
#: The find pages below docs/.
ADDRESSES_PAGE = "research/find/addresses.md"
WORDS_PAGE = "research/find/words.md"
NO_PAGE = "(no page)"


def _cell(text: str) -> str:
    """Text safe inside a table cell."""
    return text.replace("|", "\\|").replace("\n", " ")


def _name_cell(file: SourceFile) -> str:
    """The file's name as code; a unit with no path string (`(no path string)`) is not a name, so plain text."""
    return _cell(file.name) if file.name.startswith("(") else f"`{_cell(file.name)}`"


def _range_cell(file: SourceFile) -> str:
    """The file's attributed range, or where its path string sits when no code is attributed."""
    if file.start is None or file.end is None:
        return f"none (path string at `0x{file.path_string or 0:08x}` has no code reference)"
    return f"`0x{file.start:08x}`-`0x{file.end:08x}`"


def _anchors_cell(file: SourceFile) -> str:
    """The anchors with their tags, `+N more` after them, or the note when the file has one instead."""
    if file.note:
        return _cell(file.note)
    parts = []
    for anchor in file.anchors:
        tags = ", ".join(f"`{_cell(tag)}`" for tag in anchor.tags)
        parts.append(f"`0x{anchor.address:08x}`" + (f" ({tags})" if tags else ""))
    text = "; ".join(parts)
    if file.more_anchors:
        text += f", +{file.more_anchors} more"
    return text


def _page_link(page: str, base: str = "") -> str:
    """A link to the research page `docs/research/...` from a page in the folder `base` below docs/research (`..` for
    the find pages, '' for the source map), labelled with the page's path below docs/research."""
    name = page.removeprefix(RESEARCH_DIR.as_posix() + "/")
    return f"[{name}]({base}/{name})" if base else f"[{name}]({name})"


def render_file_tables(data: SourceFiles, owner_of: Callable[[SourceFile], str] | None = None) -> str:
    """The source map's file tables: a heading and a table per directory, in the order of the data.

    `owner_of` gives the page (`docs/research/...`) that owns a file, or '' for none; when some file has one, each table
    gains a Page column that links it.
    """
    owners = {file.path: owner_of(file) for file in data.files} if owner_of is not None else {}
    paged = any(owners.values())
    directories: dict[str, list[SourceFile]] = {}
    for file in data.files:
        directories.setdefault(file.directory, []).append(file)
    header = ["File", "Attributed range", "Functions", "Static-init stub", "Anchors (allocation tags they pass)"]
    if paged:
        header.append("Page")
    out: list[str] = []
    for directory, files in directories.items():
        out += [
            f"#### {directory}",
            "",
            "| " + " | ".join(header) + " |",
            "| " + " | ".join("---" for _ in header) + " |",
        ]
        for file in files:
            stub = f"`0x{file.stub:08x}`" if file.stub is not None else ""
            cells = [_name_cell(file), _range_cell(file), str(file.functions), stub, _anchors_cell(file)]
            if paged:
                cells.append(_page_link(owners[file.path]) if owners[file.path] else "")
            out.append("| " + " | ".join(cells) + " |")
        out.append("")
    return "\n".join(out).rstrip("\n") + "\n"


def fill_source_map(markdown: str, data: SourceFiles, owner_of: Callable[[SourceFile], str] | None = None) -> str:
    """The source map page's `markdown` with FILES_MARKER replaced by the generated note and the tables of `data`.

    Raises SourceFilesError, naming the page, when the marker is not there exactly once: the page would show no files.
    """
    if markdown.count(FILES_MARKER) != 1:
        raise SourceFilesError(
            f"docs/{SOURCE_MAP_PAGE}: needs exactly one {FILES_MARKER} line, where the file tables go"
        )
    tables = render_file_tables(data, owner_of)
    return markdown.replace(FILES_MARKER, f"{GENERATED_NOTE}\n\n{tables}".rstrip("\n"))


# --- the find pages ---------------------------------------------------------------------------------------------------


def _plural(count: int, noun: str) -> str:
    """`1 file`, `2 files`."""
    return f"{count} {noun}" if count == 1 else f"{count} {noun}s"


def render_addresses(functions: Sequence[ListedFunction], owners: Owners) -> str:
    """The page that says which research page describes each stretch of the executable.

    A table of the pages that own code (what each lists and how many functions it holds), then the executable in
    address order as runs of consecutive functions with the same owner, a run of functions no page owns included.
    """
    head = [
        "<!-- Generated by `coney-tools research render` from the Ghidra listing and the pages' front matter."
        " Do not edit. -->",
        "",
        "# Where each address is described",
        "",
    ]
    if not functions:
        return "\n".join(
            [
                *head,
                "The Ghidra listing (`docs/progress/ghidra-functions.tsv`) is not in this checkout, so there are no "
                "functions to place.",
                "",
            ]
        )
    runs: list[tuple[str, int, int, int]] = []  # owner, first address, end address, functions
    held: dict[str, int] = {}
    for function in functions:
        owner = owners.owner(function.address)
        end = function.address + max(function.size, 1)
        if owner:
            held[owner] = held.get(owner, 0) + 1
        if runs and runs[-1][0] == owner:
            runs[-1] = (owner, runs[-1][1], end, runs[-1][3] + 1)
        else:
            runs.append((owner, function.address, end, 1))
    owned = sum(held.values())
    out = [
        *head,
        f"{_plural(len(functions), 'function')}: {owned} owned by a page, {len(functions) - owned} with no page.",
        "",
        "A function belongs to the one page that lists it, or the narrowest range or source file that holds it, in "
        "that page's front matter ([one owner per address](../../guides/writing-docs.md#research-pages)). "
        "`coney-tools research where` answers for one address, name, file or word "
        "([Finding where a behaviour lives](../../guides/research-where.md)).",
        "",
    ]
    pages = [(path, facts.front.owns) for path, facts in sorted(owners.corpus.pages.items()) if facts.front.owns]
    pages = [(path, owns) for path, owns in pages if not owns.empty]
    if pages:
        out += ["## Pages that own code", "", "| Page | Owns | Functions |", "| --- | --- | --- |"]
        for path, owns in pages:
            parts = [
                _plural(count, noun)
                for count, noun in (
                    (len(owns.files), "file"),
                    (len(owns.ranges), "range"),
                    (len(owns.functions), "function"),
                )
                if count
            ]
            out.append(f"| {_page_link(path, '..')} | {', '.join(parts)} | {held.get(path, 0)} |")
        out.append("")
    out += ["## Address map", "", "| Addresses | Functions | Page |", "| --- | --- | --- |"]
    for owner, first, end, count in runs:
        link = _page_link(owner, "..") if owner else NO_PAGE
        out.append(f"| `0x{first:08x}`-`0x{end:08x}` | {count} | {link} |")
    return "\n".join(out) + "\n"


def render_words(glossary: Glossary, names: Mapping[int, str]) -> str:
    """The page of the player's words: each term with the page that describes it, its functions and its other names.

    `names` are the Ghidra names of the functions the terms list, by address.
    """
    out = [
        "<!-- Generated by `coney-tools research render` from research/glossary.yaml. Do not edit. -->",
        "",
        "# Player words",
        "",
    ]
    if not glossary.terms:
        out += [
            'No word is listed yet. A word a player uses ("sprint", "stamina", "mount") is added to '
            "[research/glossary.yaml](repo:research/glossary.yaml) when a study links it to the code "
            "([Finding where a behaviour lives](../../guides/research-where.md#words)).",
            "",
        ]
        return "\n".join(out)
    out += [
        f"{_plural(len(glossary.terms), 'word')} a player uses, each with the page that describes it and the functions "
        "where its logic lives. `coney-tools research where WORD` answers the same from the terminal.",
        "",
        "| Word | Page | Functions | Also called |",
        "| --- | --- | --- | --- |",
    ]
    for term in sorted(glossary.terms, key=lambda t: t.term.casefold()):
        target = term.page
        functions = ", ".join(f"`0x{a:08x}`" + (f" `{names[a]}`" if names.get(a) else "") for a in term.functions)
        out.append(f"| {_cell(term.term)} | [{target}](../{target}) | {functions} | {_cell(', '.join(term.also))} |")
    return "\n".join(out) + "\n"
