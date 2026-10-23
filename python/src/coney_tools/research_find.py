# SPDX-License-Identifier: GPL-3.0-or-later
"""The generated parts of the research pages that find things: the source map's file tables.

The pages are for people; the data files are for tools. Nothing generated is committed: the docs build fills the
marker of `docs/research/source-map.md` with the tables of `research/source-files.toml` (mkdocs_hooks.py), and
`coney-tools research render OUT_DIR` writes the filled page to a folder to look at, like the progress tables.
The tables stay on the source map's page, under the same headings as before they were data, so the links to
`source-map.md#gui` and its kin keep their targets.

Research: docs/research/source-map.md
"""

from __future__ import annotations

from coney_tools.source_files import SourceFile, SourceFiles, SourceFilesError

#: The source map page below docs/, and the line in it where the file tables go.
SOURCE_MAP_PAGE = "research/source-map.md"
FILES_MARKER = "<!-- source-files:generated -->"
GENERATED_NOTE = (
    "<!-- Generated when the docs are built, from research/source-files.toml. Edit that file, not these tables. -->"
)


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


def render_file_tables(data: SourceFiles) -> str:
    """The source map's file tables: a heading and a table per directory, in the order of the data."""
    directories: dict[str, list[SourceFile]] = {}
    for file in data.files:
        directories.setdefault(file.directory, []).append(file)
    header = ["File", "Attributed range", "Functions", "Static-init stub", "Anchors (allocation tags they pass)"]
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
            out.append("| " + " | ".join(cells) + " |")
        out.append("")
    return "\n".join(out).rstrip("\n") + "\n"


def fill_source_map(markdown: str, data: SourceFiles) -> str:
    """The source map page's `markdown` with FILES_MARKER replaced by the generated note and the tables of `data`.

    Raises SourceFilesError, naming the page, when the marker is not there exactly once: the page would show no files.
    """
    if markdown.count(FILES_MARKER) != 1:
        raise SourceFilesError(
            f"docs/{SOURCE_MAP_PAGE}: needs exactly one {FILES_MARKER} line, where the file tables go"
        )
    return markdown.replace(FILES_MARKER, f"{GENERATED_NOTE}\n\n{render_file_tables(data)}".rstrip("\n"))
