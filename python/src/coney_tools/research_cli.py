# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools research ...`: the function database of research/functions/ and the pages made from it.

* `research render OUT_DIR` writes the generated list page of each system; `research render --check` checks the whole
  database (schema, pages and sections, Ghidra names, source files) and writes nothing; CI runs it.
* `research import-table PAGE --system NAME` fills a system file from the code-index tables of a research page.

Research: docs/guides/research-function-database.md
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Any

from coney_tools import research_claims_cli, research_functions_check, research_import, research_render
from coney_tools import research_functions as rf
from coney_tools.config import ConfigError, find_repo_root

RESEARCH_DIR = research_functions_check.RESEARCH_DIR


def add_parser(groups: Any) -> None:
    """Register `coney-tools research render|import-table|claims`."""
    group = groups.add_parser(
        "research",
        help="the function database (research/functions/), the pages made from it, and the claims a branch changed",
    )
    commands = group.add_subparsers(dest="command", required=True)
    research_claims_cli.add_commands(commands)
    render = commands.add_parser(
        "render", help="check the function database and write each system's generated list page to a folder"
    )
    render.add_argument("out_dir", nargs="?", type=Path, help="a folder to write research/<system>/functions.md to")
    render.add_argument(
        "--check", action="store_true", help="write nothing; check the data and exit 1 when it has a problem"
    )
    table = commands.add_parser(
        "import-table", help="import a research page's code-index tables into research/functions/<system>.toml"
    )
    table.add_argument("page", type=Path, help="the research page, below docs/research")
    table.add_argument("--system", required=True, help="the system's file: lower case with hyphens, such as combat")
    table.add_argument("--section", default="", help="import only the tables under this heading's anchor")
    table.add_argument(
        "--replace", action="store_true", help="take the page's row for an address the file has with other contents"
    )
    table.add_argument("--dry-run", action="store_true", help="print the report and write nothing")


def run(args: argparse.Namespace) -> int:
    """Dispatch `coney-tools research ...`; returns the exit status."""
    if args.command == "claims":
        return research_claims_cli.run(args)
    if args.command == "render":
        return run_render(args.out_dir, args.check)
    return run_import_table(args.page, args.system, args.section, args.replace, args.dry_run)


def _plural(count: int, noun: str) -> str:
    """`1 function`, `2 functions`."""
    return f"{count} {noun}" if count == 1 else f"{count} {noun}s"


def run_render(out_dir: Path | None, check_only: bool) -> int:
    """Check the database; with `out_dir`, then write the pages there. 1 when the check finds an error."""
    if check_only == (out_dir is not None):
        raise ConfigError("give a folder to write the pages to, or --check (not both)")
    root = find_repo_root(Path.cwd())
    findings = research_functions_check.check_root(root)
    for warning in findings.warnings:
        print(f"warning: {warning}", file=sys.stderr)
    for error in findings.errors:
        print(f"error: {error}", file=sys.stderr)
    if findings.errors:
        print(f"coney-tools: the function database has {_plural(len(findings.errors), 'problem')}", file=sys.stderr)
        return 1
    systems = rf.load_all(root, research_functions_check.functions_dir(root))
    count = sum(len(s.entries) for s in systems)
    if out_dir is None:
        print(f"{_plural(count, 'function')} in {_plural(len(systems), 'system')}: ok")
        return 0
    pages = research_render.render_pages(root, systems)
    for name, text in pages.items():
        path = out_dir / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(text.encode("utf-8"))
    print(f"{_plural(len(pages), 'page')} written to {out_dir} ({_plural(count, 'function')})")
    return 0


def _research_page(root: Path, page: Path) -> tuple[Path, str]:
    """The page's file and its path below docs/research. Raises ConfigError for a file that is not a research page."""
    path = page if page.is_absolute() or page.is_file() else root / page
    base = (root / RESEARCH_DIR).resolve()
    try:
        below = path.resolve().relative_to(base)
    except ValueError as error:
        raise ConfigError(f"{page}: not below {RESEARCH_DIR.as_posix()}") from error
    if below.suffix != ".md" or not path.is_file():
        raise ConfigError(f"{page}: not a research page (a Markdown file below {RESEARCH_DIR.as_posix()})")
    return path, below.as_posix()


def _print_lines(title: str, lines: list[str]) -> None:
    """A heading and its indented lines, when there are any."""
    if lines:
        print(f"{title}:")
        for line in lines:
            print(f"  {line}")


def run_import_table(page: Path, system: str, section: str, replace_existing: bool, dry_run: bool) -> int:
    """Import `page`'s function tables into the file of `system`; 1 when the page gave no function at all."""
    root = find_repo_root(Path.cwd())
    if not rf.SYSTEM_NAME.match(system):
        raise ConfigError(f"{system!r}: a system name is lower case with hyphens, such as combat or ai-goals")
    path, below = _research_page(root, page)
    source = f"{RESEARCH_DIR.as_posix()}/{below}"
    functions_dir = research_functions_check.functions_dir(root)
    target = functions_dir / f"{system}.toml"
    imported = research_import.import_tables(path.read_text(encoding="utf-8"), below, source=source, section=section)
    if not imported.entries:
        _print_lines("skipped", imported.skipped)
        print(f"coney-tools: {source}: no function table rows to import", file=sys.stderr)
        return 1

    # The file as it stands, and which addresses the other systems own.
    systems = rf.load_all(root, functions_dir)
    existing = next((s for s in systems if s.system == system), rf.SystemFunctions(system, target, "", ()))
    owners = {e.address: s.path.as_posix() for s in systems if s.system != system for e in s.entries}
    merged = research_import.merge(existing, imported.entries, owners, replace_existing=replace_existing)
    text = rf.dumps(merged.system)

    print(f"{source} -> {target.as_posix()}")
    print(
        f"{_plural(len(imported.entries), 'row')} imported: {merged.added} new, {merged.unchanged} unchanged, "
        f"{merged.replaced} replaced, {len(merged.kept)} kept as they were, {len(merged.foreign)} owned elsewhere"
    )
    _print_lines("kept (the file differs from the page)", merged.kept)
    _print_lines("not imported (another system owns the function)", merged.foreign)
    _print_lines(
        f"skipped ({_plural(len(imported.skipped), 'row')} or table; fix the page or add them by hand)",
        imported.skipped,
    )
    _print_lines("check these", imported.notes)

    # What `research render --check` would say about the file once written.
    others = [s for s in systems if s.system != system]
    findings = research_functions_check.check_systems(root, [merged.system, *others])
    mine = [f for f in [*findings.errors, *findings.warnings] if f.startswith(target.as_posix() + ":")]
    _print_lines("after the import `research render --check` reports", mine)

    full = root / target
    current = full.read_bytes().decode("utf-8").replace("\r\n", "\n") if full.is_file() else None
    if current == text:
        print(f"{target.as_posix()} is up to date")
    elif dry_run:
        print(f"would write {target.as_posix()} ({_plural(len(merged.system.entries), 'function')})")
    else:
        full.parent.mkdir(parents=True, exist_ok=True)
        full.write_bytes(text.encode("utf-8"))
        print(f"wrote {target.as_posix()} ({_plural(len(merged.system.entries), 'function')})")
    return 0
