# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools progress ...` commands: show, update and sizes."""

from __future__ import annotations

import json
import re
from pathlib import Path

from coney_tools import elf, progress, progress_render, wad
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
    """`text` with LF line endings, so a CRLF checkout compares equal to the generated block."""
    return text.replace("\r\n", "\n")


def run_update(check_only: bool) -> int:
    """Regenerate the README's and the docs page's progress blocks; with `check_only`, 1 when either is stale."""
    root = find_repo_root(Path.cwd())
    state = progress.load(root)
    if state.problems:
        _report_problems(state.problems)
        print("progress: fix the problems above first")
        return 1
    targets = {
        progress.README_FILE: progress_render.render_readme(state),
        progress.PAGE_FILE: progress_render.render_page(state),
    }
    stale = []
    for relative, block in targets.items():
        path = root / relative
        try:
            raw = path.read_bytes().decode("utf-8")
        except (OSError, UnicodeDecodeError) as error:
            raise ConfigError(f"{path}: cannot be read ({error})") from error
        current = _normalise(raw)
        wanted = progress_render.replace_block(current, block, relative)
        if wanted == current:
            continue
        stale.append(relative.as_posix())
        if not check_only:
            # Keep the file's own line endings, so a Windows checkout with CRLF does not see every line change.
            out = wanted.replace("\n", "\r\n") if "\r\n" in raw else wanted
            path.write_bytes(out.encode("utf-8"))
    if check_only and stale:
        print(f"progress: stale: {', '.join(stale)}; run `uv run --project python coney-tools progress update`")
        return 1
    print(f"progress: {'updated ' + ', '.join(stale) if stale else 'up to date'}")
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
    # Edit with LF and write back with the file's own line endings, as run_update does.
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
