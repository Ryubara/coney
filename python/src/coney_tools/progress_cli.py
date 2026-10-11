# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools progress ...` commands: show, check, render, sizes, ghidra and backlog."""

from __future__ import annotations

import json
import re
from pathlib import Path

from coney_tools import elf, progress, progress_render, progress_research, wad
from coney_tools.config import ConfigError, find_repo_root
from coney_tools.wad_cli import open_disc


def _report_problems(problems: list[str]) -> None:
    """Print each problem on its own line."""
    for problem in problems:
        print(f"problem: {problem}")


def run_show(as_json: bool) -> int:
    """Print the summary (or all of it as JSON); 1 when the inputs disagree with each other."""
    state = progress.load(find_repo_root(Path.cwd()))
    if as_json:
        print(json.dumps(state.to_json(), indent=2))
        return 1 if state.problems else 0
    totals = state.totals
    game = totals.game_bytes
    pct = progress_render.percent
    print(f"reimplemented: {pct(state.reimplemented_bytes, game)} ({state.reimplemented_bytes:,} of {game:,} bytes,")
    print(f"               {len(state.functions)} functions)")
    print(f"researched:    {pct(totals.researched_bytes, game)} ({totals.researched_bytes:,} bytes)")
    u = state.understanding
    if u is not None:
        print(f"understood:    {pct(u.understood_bytes, u.bytes)} ({u.understood_bytes:,} of {u.bytes:,} bytes,")
        print(f"               {u.understood_functions:,} of {u.functions:,} functions)")
    finished = sum(1 for m in state.milestones if m.status == "done")
    print(f"milestones:    {finished} of {len(state.milestones)} done")
    for name, total, done, count in state.subsystem_rows():
        if count:
            print(f"  {name:<14} {pct(done, total):>6}  {count} functions")
    missing = sum(1 for f in state.functions if f.size is None)
    if missing:
        print(f"{missing} functions have no size; fill them with `coney-tools progress sizes --fill`")
    _report_problems(state.problems)
    return 1 if state.problems else 0


def _normalise(text: str) -> str:
    """`text` with LF line endings, so a CRLF checkout compares equal to the generated text."""
    return text.replace("\r\n", "\n")


def _load_checked(root: Path) -> progress.Progress | None:
    """The tracker's state for `root`, or None (after printing the problems) when the inputs disagree."""
    state = progress.load(root)
    if state.problems:
        _report_problems(state.problems)
        return None
    return state


def _page_template(root: Path) -> str:
    """The committed docs/progress/index.md, which holds the marker the build fills in."""
    path = root / progress.PAGE_FILE
    try:
        return _normalise(path.read_bytes().decode("utf-8"))
    except (OSError, UnicodeDecodeError) as error:
        raise ConfigError(f"{path}: cannot be read ({error})") from error


def run_check() -> int:
    """Validate the inputs and that the docs build can render them; 1 when they disagree.

    Nothing generated is committed (the docs build writes the tables and the badge data), so this is what CI runs:
    functions.toml against the @orig tags in src/ and the source map's ranges, the roadmap's status table, and the
    page's marker.
    """
    root = find_repo_root(Path.cwd())
    state = _load_checked(root)
    if state is None:
        print("progress: fix the problems above")
        return 1
    progress_render.fill_page(_page_template(root), progress_render.render_page(state), progress.PAGE_FILE.as_posix())
    progress_render.badge_files(state)
    print(f"progress: inputs are consistent ({len(state.functions)} functions, {len(state.milestones)} milestones)")
    return 0


def run_render(out_dir: Path) -> int:
    """Write what the docs build publishes to `out_dir`: the filled page and the badge JSON files.

    For looking at the numbers without a docs build; nothing under the checkout changes.
    """
    root = find_repo_root(Path.cwd())
    state = _load_checked(root)
    if state is None:
        print("progress: fix the problems above first")
        return 1
    page = progress_render.fill_page(
        _page_template(root), progress_render.render_page(state), progress.PAGE_FILE.as_posix()
    )
    badges = progress_render.badge_files(state)
    try:
        (out_dir / progress_render.BADGES_DIR).mkdir(parents=True, exist_ok=True)
        (out_dir / "progress" / "index.md").write_text(page, encoding="utf-8")
        for name, text in badges.items():
            (out_dir / progress_render.BADGES_DIR / name).write_text(text, encoding="utf-8")
    except OSError as error:
        raise ConfigError(f"{out_dir}: cannot be written ({error})") from error
    print(f"progress: wrote progress/index.md and {len(badges)} badge files to {out_dir}")
    return 0


def set_size(text: str, address: int, size: int) -> str:
    """functions.toml text with the `[[function]]` at `address` given `size = <size>` (replaced or added)."""
    lines = text.split("\n")
    address_line = re.compile(rf"^\s*address\s*=\s*0x0*{address:x}\s*(#.*)?$", re.IGNORECASE)
    for i, line in enumerate(lines):
        if not address_line.match(line):
            continue
        # The block runs to the next table header or the end of the file.
        end = next((j for j in range(i + 1, len(lines)) if lines[j].lstrip().startswith("[")), len(lines))
        start = max((j for j in range(i) if lines[j].lstrip().startswith("[")), default=0)
        for j in range(start, end):
            if re.match(r"^\s*size\s*=", lines[j]):
                lines[j] = f"size = {size}"
                return "\n".join(lines)
        # No size yet: add one after the block's last line that is neither blank nor a comment.
        last = max(j for j in range(start, end) if lines[j].strip() and not lines[j].lstrip().startswith("#"))
        lines.insert(last + 1, f"size = {size}")
        return "\n".join(lines)
    raise ConfigError(f"no [[function]] with address = {address:#010x} in {progress.FUNCTIONS_FILE}")


def run_sizes(disc_arg: str | None, fill: bool) -> int:
    """Check each listed function's size against the executable on the disc; `fill` writes the missing ones.

    Prints only the addresses and sizes of the functions already in functions.toml. Returns 1 when a given size
    is impossible (it runs into the next function).
    """
    root = find_repo_root(Path.cwd())
    path = root / progress.FUNCTIONS_FILE
    functions = progress.load_functions(path)
    if not functions:
        print("no functions in functions.toml yet; nothing to check")
        return 0
    disc = open_disc(disc_arg)
    with disc.open(wad.ELF_FILE) as handle:
        executable = elf.read_elf(handle.read())
    starts = elf.function_starts(executable)
    # Edit with LF and write back with the file's own line endings, so a CRLF checkout does not change every line.
    text = path.read_bytes().decode("utf-8")
    newline = "\r\n" if "\r\n" in text else "\n"
    text = text.replace("\r\n", "\n")
    failed = filled = 0
    for f in functions:
        result = elf.check_size(executable, f.address, f.size, starts)
        given = "-" if f.size is None else str(f.size)
        print(f"{f.address:#010x} {f.name}: size {given}, span to the next known function {result.span}")
        for message in result.errors:
            print(f"  error: {message}")
        for message in result.warnings:
            print(f"  warning: {message}")
        failed += bool(result.errors)
        if fill and f.size is None and result.span and not result.errors:
            text = set_size(text, f.address, result.span)
            filled += 1
    if filled:
        path.write_bytes(text.replace("\n", newline).encode("utf-8"))
        print(f"filled {filled} size(s) in {progress.FUNCTIONS_FILE.as_posix()}; check them, then commit")
    return 1 if failed else 0


def run_ghidra(url: str | None, check_only: bool) -> int:
    """Re-export the Ghidra listing from a running ghidra-mcp; with `check_only`, 1 when the committed one differs.

    Only reading endpoints are called, so this is safe on the server the analysts share.
    """
    root = find_repo_root(Path.cwd())
    path = root / progress_research.LISTING_FILE
    fetch = progress_research.http_fetch((url or progress_research.DEFAULT_GHIDRA_URL).rstrip("/"))
    exported = progress_research.export_listing(fetch)
    text = progress_research.format_listing(exported)
    try:
        current = _normalise(path.read_bytes().decode("utf-8"))
    except FileNotFoundError:
        current = ""
    except (OSError, UnicodeDecodeError) as error:
        raise ConfigError(f"{path}: cannot be read ({error})") from error
    named = sum(1 for f in exported if f.named)
    print(f"ghidra: {len(exported):,} functions, {named:,} named, {sum(f.plate for f in exported):,} with a plate")
    if current == text:
        print(f"ghidra: {progress_research.LISTING_FILE.as_posix()} is up to date")
        return 0
    if check_only:
        print(f"ghidra: {progress_research.LISTING_FILE.as_posix()} is stale; run `coney-tools progress ghidra`")
        return 1
    path.write_bytes(text.encode("utf-8"))
    print(f"ghidra: wrote {progress_research.LISTING_FILE.as_posix()}; the docs build now uses it")
    return 0


def run_backlog(out_dir: Path) -> int:
    """Write one backlog file per subsystem (not-yet-understood functions, largest first) to `out_dir`."""
    wad.refuse_inside_repo(out_dir)
    state = progress.load(find_repo_root(Path.cwd()))
    u = state.understanding
    if u is None:
        raise ConfigError(f"no {progress_research.LISTING_FILE.as_posix()}; run `coney-tools progress ghidra` first")
    try:
        out_dir.mkdir(parents=True, exist_ok=True)
        rows = u.by_subsystem([s.name for s in state.totals.subsystems])
        for row in rows:
            name = progress_research.backlog_file_name(row.name)
            (out_dir / name).write_text(progress_research.render_backlog(u, row), encoding="utf-8")
    except OSError as error:
        raise ConfigError(f"{out_dir}: cannot be written ({error})") from error
    print(f"backlog: wrote {len(rows)} files to {out_dir}")
    return 0
