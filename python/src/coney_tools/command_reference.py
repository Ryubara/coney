# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools docs render`: the generated parameter blocks of the coney-tools guides.

Every group of the command line has one block in the guide page of its cluster, written from the argparse parsers
(`command_params`) between two marker lines:

    <!-- command-reference:begin repo -->
    ... generated; never edited by hand ...
    <!-- command-reference:end -->

`docs render` rewrites the blocks, `--check` changes nothing and fails when one is stale, as CI does, and
`check_blocks` is the same gate as a list of problems for `repo audit --files` (so `ci run` and CI run it): a
group with no block, a group with two, a marker that names no group, a stale block (one finding for each, naming its
group) and a command or parameter that has no help text. A command added by someone else appears in its group's block
when the block is rendered again; a new group needs its markers placed once, on the page of the cluster it belongs to.
Markers inside a fenced code block are an example and are left alone. The design is that of `repo render`
(conventions_render).

Where the guides are is read from `paths.docs` of `.github/conventions.yml`: the `guides` folder inside each folder that
key names (`docs/` gives `docs/guides`), so moving the documentation moves the reference with it.

Research: docs/guides/coney-tools.md#parameter-reference
"""

from __future__ import annotations

import re
import sys
from collections.abc import Sequence
from pathlib import Path
from typing import NamedTuple

from coney_tools.command_params import Group, missing_help, read_groups, read_program, render_group
from coney_tools.config import ConfigError

#: The folder, inside each folder of `paths.docs`, whose Markdown files (at any depth) may hold blocks.
GUIDES_FOLDER = "guides"
_BEGIN = re.compile(r"^<!-- command-reference:begin ([a-z][a-z0-9-]*) -->$")
_PREFIX = "<!-- command-reference:begin"
_END = "<!-- command-reference:end -->"


class BlockError(ConfigError):
    """A marker in a page is wrong (unknown group, no end, an end without a begin); the message names page and line."""

    def __init__(self, where: str, line: int, reason: str) -> None:
        super().__init__(f"{where}:{line}: {reason}")
        self.where = where
        self.line = line
        self.reason = reason


class Finding(NamedTuple):
    """One thing wrong with the reference: what it is about (a page, a group, a command) and what is wrong."""

    item: str
    message: str

    def __str__(self) -> str:
        return f"{self.item}: {self.message}"


def command_groups() -> dict[str, Group]:
    """Every group the user can run: those of `coney-tools`, and the launcher `coney-agent`, a program of its own."""
    # Imported here: the command line imports this module through `docs_cli`.
    from coney_tools import cli, session_launcher

    groups = read_groups(cli._build_parser())
    launcher = read_program(session_launcher._build_parser())
    groups[launcher.name] = launcher
    return groups


def guide_folders(root: Path, docs: Sequence[str]) -> list[Path]:
    """The guides folders of the checkout at `root`: `guides` inside each folder `docs` (`paths.docs`) names that has
    one. A prefix that is not a folder (no trailing slash) holds none."""
    folders = (root / prefix / GUIDES_FOLDER for prefix in docs if prefix.endswith("/"))
    return [folder for folder in folders if folder.is_dir()]


def block_files(root: Path, docs: Sequence[str]) -> list[Path]:
    """Every Markdown file under the guides folders (`guide_folders`), in path order: any of them may hold blocks."""
    files = [path for folder in guide_folders(root, docs) for path in folder.rglob("*.md")]
    return sorted(files, key=lambda path: path.as_posix())


def _render(text: str, groups: dict[str, Group], where: str) -> tuple[str, list[str], list[str]]:
    """`text` with every block refilled, the names of the blocks in it and the names of those that were out of date;
    BlockError for a wrong marker."""
    out: list[str] = []
    names: list[str] = []
    stale: list[str] = []
    in_fence = False
    opened: tuple[str, int] | None = None
    fresh: list[str] = []
    old: list[str] = []
    for number, line in enumerate(text.replace("\r\n", "\n").split("\n"), 1):
        stripped = line.strip()
        begin = _BEGIN.match(stripped)
        if opened is not None:
            # The old content of a block is dropped; only a marker matters until it ends.
            if stripped == _END:
                # A rendered entry may hold several lines, so compare the text, not the lists.
                if "\n".join(old) != "\n".join(fresh):
                    stale.append(opened[0])
                out.append(line)
                opened = None
            elif begin or stripped.startswith(_PREFIX):
                raise BlockError(where, number, f"a begin marker inside the block opened on line {opened[1]}")
            else:
                old.append(line)
            continue
        if stripped.startswith(("```", "~~~")):
            in_fence = not in_fence
        if in_fence or stripped.startswith(("```", "~~~")):
            out.append(line)
            continue
        if stripped == _END:
            raise BlockError(where, number, "an end marker without a begin")
        if begin:
            name = begin.group(1)
            if name not in groups:
                raise BlockError(where, number, f"unknown group '{name}' (known: {', '.join(groups)})")
            out.append(line)
            fresh = render_group(groups[name])
            out += fresh
            old = []
            opened = (name, number)
            names.append(name)
            continue
        out.append(line)
    if opened is not None:
        raise BlockError(where, opened[1], f"the block for '{opened[0]}' is never closed (no end marker)")
    return "\n".join(out), names, stale


def render_text(text: str, groups: dict[str, Group], where: str) -> str:
    """`text` with the content of every block rewritten from `groups`; `where` names the page in an error.

    Line ends become LF. Raises BlockError for a marker that names no group, an end without a begin or a block never
    closed.
    """
    return _render(text, groups, where)[0]


class _Page(NamedTuple):
    """One guide, read and rendered."""

    name: str
    path: Path
    #: The text as it is and as it should be (both with LF line ends), and the groups of its blocks.
    current: str
    text: str
    names: list[str]
    #: The groups whose blocks differ from what the parsers render, in the order of the page.
    stale_names: list[str]

    @property
    def stale(self) -> bool:
        """Whether a block of the page differs from what the parsers render."""
        return self.text != self.current


def _scan(root: Path, groups: dict[str, Group], docs: Sequence[str]) -> tuple[list[_Page], list[Finding]]:
    """Read and render every guide that may hold blocks: the pages, and a finding for each page whose markers are
    wrong."""
    pages: list[_Page] = []
    errors: list[Finding] = []
    for path in block_files(root, docs):
        name = path.relative_to(root).as_posix()
        current = path.read_bytes().decode("utf-8").replace("\r\n", "\n")
        try:
            text, names, stale_names = _render(current, groups, name)
        except BlockError as error:
            errors.append(Finding(name, f"line {error.line}: {error.reason}"))
            continue
        pages.append(_Page(name, path, current, text, names, stale_names))
    return pages, errors


def _structure(root: Path, pages: list[_Page], groups: dict[str, Group], docs: Sequence[str]) -> list[Finding]:
    """The problems no rendering can fix: a group with no block, a group with two, a command without help text."""
    found: list[Finding] = []
    folders = guide_folders(root, docs)
    where = ", ".join(folder.relative_to(root).as_posix() for folder in folders) or f"{GUIDES_FOLDER}/ of paths.docs"
    homes: dict[str, str] = {}
    for page in pages:
        for group in page.names:
            if group in homes:
                found.append(Finding(group, f"two blocks, in {homes[group]} and {page.name}"))
            homes.setdefault(group, page.name)
    found += [
        Finding(
            group,
            f"no block of its parameters in {where} (add `<!-- command-reference:begin {group} -->` and "
            f"`{_END}` to the page of its cluster, at the end of the group's section)",
        )
        for group in groups
        if group not in homes
    ]
    return [*found, *(Finding(name, "no help text") for name in missing_help(groups))]


def _stale(pages: list[_Page]) -> list[Finding]:
    """A finding for each block that is out of date, naming its group (a page with one stale block among several
    names that one only)."""
    return [
        Finding(page.name, f"the block for {group!r} is out of date (run `coney-tools docs render`)")
        for page in pages
        for group in dict.fromkeys(page.stale_names)
    ]


def findings(root: Path, groups: dict[str, Group], docs: Sequence[str]) -> list[Finding]:
    """What is wrong with the reference in the checkout at `root`; empty when it is complete and current.

    `groups` is the command line (`command_groups`) and `docs` the folders of `paths.docs`, whose `guides` folders
    hold the blocks. Changes nothing."""
    pages, errors = _scan(root, groups, docs)
    return [*errors, *_stale(pages), *_structure(root, pages, groups, docs)]


def check_blocks(root: Path, groups: dict[str, Group], docs: Sequence[str]) -> list[str]:
    """`findings` as one line each."""
    return [str(finding) for finding in findings(root, groups, docs)]


def run_render(root: Path, groups: dict[str, Group], check: bool, docs: Sequence[str]) -> int:
    """Rewrite the stale blocks of every guide under `root` (the `guides` folders of `docs`, which is `paths.docs`), or
    with `check` only report them. Exit 1 when there is anything to report: with `check` a stale block; whichever way
    it runs a wrong marker, a group with no block or two, or a command or parameter with no help text (no rendering
    fixes those). The lines go to stderr."""
    pages, errors = _scan(root, groups, docs)
    stale = [page for page in pages if page.stale]
    if not check:
        for page in stale:
            page.path.write_bytes(page.text.encode("utf-8"))
            print(f"updated {page.name}")
    report = [*errors, *(_stale(pages) if check else []), *_structure(root, pages, groups, docs)]
    for finding in report:
        print(f"coney-tools docs render: {finding}", file=sys.stderr)
    if report:
        return 1
    blocks = sum(len(page.names) for page in pages)
    print(f"command reference: {'updated' if stale else 'ok'} ({blocks} blocks for {len(groups)} groups)")
    return 0
