# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools progress ...` commands: show, check, render, sizes, import-functions, ghidra and backlog."""

from __future__ import annotations

import bisect
import json
from pathlib import Path

from coney_tools import (
    elf,
    orig_index,
    progress,
    progress_import,
    progress_render,
    progress_research,
    progress_site,
    research_functions,
    wad,
)
from coney_tools.config import ConfigError, find_repo_root
from coney_tools.conventions import load_conventions
from coney_tools.wad_cli import open_disc


def _report_problems(problems: list[str]) -> None:
    """Print each problem on its own line."""
    for problem in problems:
        print(f"problem: {problem}")


#: The warnings `progress check` prints before it summarises the rest.
SHOWN_WARNINGS = 5


def _report_warnings(warnings: list[str], limit: int = SHOWN_WARNINGS) -> None:
    """Print the first `limit` warnings, then how many more there are."""
    for warning in warnings[:limit]:
        print(f"warning: {warning}")
    if len(warnings) > limit:
        print(f"warning: ... and {len(warnings) - limit} more like these")


def _check_names(root: Path, state: progress.Progress, base: str, require_base: bool = False) -> bool:
    """Apply the name rule: a tag the branch adds that spells a name differently fails; the rest is a warning.

    Returns False when a new mismatch fails the check. Without a `base` git can name (a copy of the files) nothing is
    known to be new, so the rule is not enforced and that is said, unless `require_base` (CI) makes it a failure.
    """
    if not state.mismatches:
        return True
    try:
        fresh = progress.new_mismatches(state.mismatches, progress.base_tag_keys(root, base))
    except ConfigError as error:
        _report_warnings(state.warnings)
        if require_base:
            print(f"problem: cannot compare with {base} ({error}); the name rule needs the base")
            return False
        print(f"warning: cannot compare with {base} ({error}); the name rule is not enforced")
        return True
    for mismatch in fresh:
        print(f"problem: {mismatch.text}")
    _report_warnings([m.text for m in state.mismatches if m not in fresh])
    since = f"{len(fresh)} new since {base}" if fresh else f"none is new since {base}"
    print(f"names: {len(state.mismatches)} @orig tag(s) spell a name differently from the database; {since}")
    return not fresh


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
        print(f"{missing} functions have no size: {state.no_size_reason}")
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
    return _template(root, progress.PAGE_FILE)


def _template(root: Path, page: Path) -> str:
    """The committed page `page` (relative to `root`), with LF line endings."""
    path = root / page
    try:
        return _normalise(path.read_bytes().decode("utf-8"))
    except (OSError, UnicodeDecodeError) as error:
        raise ConfigError(f"{path}: cannot be read ({error})") from error


def run_check(base: str = "origin/main", require_base: bool = False) -> int:
    """Validate the inputs and that the docs build can render them; 1 when they disagree.

    Nothing generated is committed (the docs build writes the tables and the badge data), so this is what CI runs:
    the function database's `coney` entries against the @orig tags in src/ and the source map's ranges, the roadmap's
    status table, and the page's marker. A tag that spells a name differently from the database is a warning only.
    """
    root = find_repo_root(Path.cwd())
    state = _load_checked(root)
    if state is None:
        print("progress: fix the problems above")
        return 1
    if not _check_names(root, state, base, require_base):
        print("progress: fix the problems above")
        return 1
    site = progress_site.build_site(root, state, reconstruct_history=False)
    progress_render.fill_page(
        _page_template(root),
        progress_render.render_page(state, site.grid_html, site.missions_html),
        progress.PAGE_FILE.as_posix(),
    )
    progress_render.fill_history_page(
        _template(root, progress.HISTORY_PAGE_FILE), site.history_html, progress.HISTORY_PAGE_FILE.as_posix()
    )
    progress_render.badge_files(state)
    print(f"progress: inputs are consistent ({len(state.functions)} functions, {len(state.milestones)} milestones)")
    return 0


def run_render(out_dir: Path, history: bool = True) -> int:
    """Write what the docs build publishes to `out_dir`: the filled pages, the badge JSON, the pictures and numbers.

    For looking at the numbers without a docs build; nothing under the checkout changes. `history` False skips the
    git walk that rebuilds the daily history (the chart then holds only today's point).
    """
    root = find_repo_root(Path.cwd())
    state = _load_checked(root)
    if state is None:
        print("progress: fix the problems above first")
        return 1
    site = progress_site.build_site(root, state, reconstruct_history=history)
    page = progress_render.fill_page(
        _page_template(root),
        progress_render.render_page(state, site.grid_html, site.missions_html),
        progress.PAGE_FILE.as_posix(),
    )
    history_page = progress_render.fill_history_page(
        _template(root, progress.HISTORY_PAGE_FILE), site.history_html, progress.HISTORY_PAGE_FILE.as_posix()
    )
    badges = progress_render.badge_files(state)
    try:
        (out_dir / progress_render.BADGES_DIR).mkdir(parents=True, exist_ok=True)
        (out_dir / "progress" / "index.md").write_text(page, encoding="utf-8")
        (out_dir / "progress" / "history.md").write_text(history_page, encoding="utf-8")
        for name, text in badges.items():
            (out_dir / progress_render.BADGES_DIR / name).write_text(text, encoding="utf-8")
        for name, text in site.files.items():
            target = out_dir / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(text, encoding="utf-8")
    except OSError as error:
        raise ConfigError(f"{out_dir}: cannot be written ({error})") from error
    print(
        f"progress: wrote 2 pages, {len(badges)} badge files and {len(site.files)} picture and data files to {out_dir}"
    )
    return 0


def next_span(span: int, address: int, listed: list[int]) -> int:
    """`span` (to the executable's next known function), cut at the next of `listed` (sorted) if that is nearer."""
    index = bisect.bisect_right(listed, address)
    if index == len(listed):
        return span
    return min(span, listed[index] - address) if span else span


def run_sizes(disc_arg: str | None) -> int:
    """Check each reimplemented function's size against the executable on the disc.

    The size is the Ghidra listing's. Prints only the addresses and sizes of the functions in the function database that
    have `coney`. Returns 1 when a size is impossible (it runs into the next function).
    """
    root = find_repo_root(Path.cwd())
    functions = progress.load(root).functions
    if not functions:
        print("no reimplemented functions in the function database yet; nothing to check")
        return 0
    disc = open_disc(disc_arg)
    with disc.open(wad.ELF_FILE) as handle:
        executable = elf.read_elf(handle.read())
    starts = elf.function_starts(executable)
    listed = sorted({f.address for f in functions})
    failed = 0
    for f in functions:
        result = elf.check_size(executable, f.address, f.size, starts)
        # A listed function that nothing calls (a virtual method) is no start to the executable's scan, but it still
        # ends the span of the function before it: a size that ran into it would be wrong.
        span = next_span(result.span, f.address, listed)
        given = "-" if f.size is None else str(f.size)
        print(f"{f.address:#010x} {f.name}: size {given}, span to the next known function {span}")
        for message in result.errors:
            print(f"  error: {message}")
        for message in result.warnings:
            print(f"  warning: {message}")
        failed += bool(result.errors)
    return 1 if failed else 0


#: The rows of each list of the import report that are shown without `--all`.
SHOWN_ROWS = 8


def _print_rows(title: str, rows: list[str], show_all: bool) -> None:
    """Print a titled list of report rows, the first few unless `show_all`."""
    if not rows:
        return
    print(f"{title}: {len(rows)}")
    for row in rows if show_all else rows[:SHOWN_ROWS]:
        print(f"  {row}")
    if not show_all and len(rows) > SHOWN_ROWS:
        print(f"  ... and {len(rows) - SHOWN_ROWS} more (--all lists them)")


def run_import_functions(file: Path | None, dry_run: bool, show_all: bool) -> int:
    """Move the rows of the retired `docs/progress/functions.toml` into the function database (issue #778).

    An entry the database has gets `coney` and `status`; any other function gets one from the research that cites it
    (progress_import.py). Writes the system files it changed unless `dry_run`; delete the old file afterwards. Returns
    1 when a row got no entry.
    """
    root = find_repo_root(Path.cwd())
    path = file if file is not None else root / progress.LEGACY_FUNCTIONS_FILE
    rows = progress_import.read_legacy(path)
    listing = progress_research.load_listing(root / progress_research.LISTING_FILE)
    if listing is None:
        raise ConfigError(f"no {progress_research.LISTING_FILE.as_posix()}; run `coney-tools progress ghidra` first")
    sources = orig_index.read_sources(root)
    plan = progress_import.plan(
        rows,
        listing={f.address: f for f in listing},
        ports=progress_import.ports_of(orig_index.build_rows(sources)),
        pages=progress_import.read_pages(root, frozenset({"source-map.md"})),
        bindings=progress_import.read_bindings(root),
        existing=progress.load_database(root),
        research=load_conventions(root).research,
    )
    report = plan.report
    print(
        f"import-functions: {report.rows} rows: {report.added} new entries, {report.updated} entries given `coney`, "
        f"{report.unchanged} already recorded, {len(report.skipped)} left out"
    )
    print("  new entries by source: " + ", ".join(f"{n} {k}" for k, n in sorted(report.sources.items())))
    print("  new entries by system: " + ", ".join(f"{k} {n}" for k, n in sorted(report.systems.items())))
    _print_rows("rows whose name is not Ghidra's (the entry has Ghidra's)", report.renamed, show_all)
    _print_rows("rows with no function entry in the Ghidra listing (no size)", report.unlisted, show_all)
    _print_rows("entries with no level (the import tied none to the function)", report.ungraded, show_all)
    _print_rows("entries that take their level from another page", report.borrowed, show_all)
    _print_rows("entries the research grades two ways (the note says where)", report.disagreeing, show_all)
    _print_rows("rows with no entry", report.skipped, show_all)
    if dry_run:
        print("import-functions: dry run, nothing written")
    else:
        written = progress_import.write(plan, root)
        print(f"import-functions: wrote {len(written)} files under {research_functions.FUNCTIONS_DIR.as_posix()}/")
        print("  next: git rm the old list, then `research render --check` and `progress check`")
    return 1 if report.skipped else 0


def run_ghidra(url: str | None, check_only: bool) -> int:
    """Re-export the Ghidra listing from a running ghidra-mcp; with `check_only`, 1 when the committed one differs.

    Only reading endpoints are called, so this is safe on the server the engineers share.
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
