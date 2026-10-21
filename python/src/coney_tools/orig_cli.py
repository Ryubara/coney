# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools orig ...`: the `@orig` index and the fidelity audit's queue (docs/guides/coney-tools.md#orig)."""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Any

from coney_tools import orig_check, orig_ghidra, orig_index, orig_queue, orig_report, progress, progress_research
from coney_tools.config import ConfigError, find_repo_root
from coney_tools.conventions import load_conventions
from coney_tools.orig_report import IndexResult

INDEX_FILES = ("index.tsv", "untagged.tsv", "report.md", "research-gaps.md")


def add_parser(groups: Any) -> None:
    """Register `coney-tools orig index`, `orig queue` and `orig check`."""
    group = groups.add_parser("orig", help="the @orig index of src/ and the fidelity audit's queue")
    commands = group.add_subparsers(dest="command", required=True)
    index = commands.add_parser(
        "index",
        help="index every @orig tag in src/, check it against Ghidra, list untagged gameplay functions",
        description=(
            "Reads every @orig tag in src/ and writes, to OUT: index.tsv (each tag's Coney file, line and function, "
            "the original's address, name and file, and the research pages it links), untagged.tsv (gameplay "
            "functions with no tag and no @coney-only marker), research-gaps.md (linked research sections that "
            "cite no original address) and report.md (the counts, the tags Ghidra does not bear out and, with a "
            "running ghidra-mcp, the untagged functions tagged ones call). Read only; never commit the output."
        ),
    )
    _common(index)
    check = commands.add_parser(
        "check",
        help="refuse a new gameplay function with neither @orig nor @coney-only (check changed and CI run it)",
        description=(
            "Fails when a function defined in a paths.gameplay folder of .github/conventions.yml since HEAD left "
            "BASE has no @orig tag and no @coney-only marker and is not exempt (a one-statement member defined in "
            "its class, an empty constructor or destructor), or when a folder of src/ is in neither paths.gameplay "
            "nor paths.coney_only. Functions that were already untagged at BASE are the audit's backlog and pass."
        ),
    )
    check.add_argument("--base", default="origin/main", help="what the change is measured from (default: %(default)s)")
    queue = commands.add_parser(
        "queue",
        help="rank the functions to audit first: open bug issues, Ghidra problems, weak research, untagged code",
        description=(
            "Everything `orig index` writes, plus queue.md and queue.tsv: the original functions Coney tags and the "
            "gameplay functions it does not, ranked by the open bug issues that name them (gh), the tags Ghidra "
            "does not bear out, research graded only inferred or speculative, and missing tags."
        ),
    )
    _common(queue)
    queue.add_argument("--issues", type=Path, help="read the issues from this saved `gh issue list --json` file")
    queue.add_argument("--label", default="bug", help="the label of the issues to read (default: %(default)s)")
    queue.add_argument("--show", type=int, default=300, help="entries written to queue.md (default: %(default)s)")


def _common(parser: argparse.ArgumentParser) -> None:
    """The options both commands share."""
    parser.add_argument("--out", type=Path, required=True, help="the folder to write to (the scratch folder)")
    parser.add_argument(
        "--ghidra",
        choices=("auto", "live", "listing", "off"),
        default="auto",
        help="check the tags against a running ghidra-mcp (live), the committed listing "
        "docs/progress/ghidra-functions.tsv (listing), the first that is there (auto, the default), or not at all",
    )
    parser.add_argument(
        "--url", default=progress_research.DEFAULT_GHIDRA_URL, help="the ghidra-mcp server (default: %(default)s)"
    )


def collect(root: Path, ghidra: str, url: str, fetch: orig_ghidra.Fetch | None = None) -> IndexResult:
    """Build the index, check the research links and, as `ghidra` says, check the tags against Ghidra."""
    index = orig_index.build_index(root, load_conventions(root).paths.gameplay)
    result = IndexResult(index, orig_index.check_research(root, index.rows))
    if ghidra == "off":
        return result
    fetch = fetch or progress_research.http_fetch(url.rstrip("/"))
    if ghidra in ("auto", "live") and orig_ghidra.server_answers(fetch):
        result.findings, callees = orig_ghidra.check_live(index.rows, fetch)
        result.ghidra_source = f"live {url}"
        totals_path = root / progress.TOTALS_FILE
        totals = progress.load_totals(totals_path) if totals_path.is_file() else None
        for callee in callees:
            subsystem = totals.subsystem_of(callee.address) if totals else ""
            if subsystem is not None:  # None: a library the original links in, which Coney replaces
                result.callees.append(callee)
                result.callee_subsystems[callee.address] = subsystem
        return result
    if ghidra == "live":
        raise ConfigError(f"no ghidra-mcp answers at {url}; start it (docs/guides/ghidra.md) or use --ghidra listing")
    listing = progress_research.load_listing(root / progress_research.LISTING_FILE)
    if listing is not None:
        result.findings = orig_ghidra.check_listing(index.rows, listing)
        result.ghidra_source = f"listing {progress_research.LISTING_FILE.as_posix()}"
    return result


def _write(out: Path, name: str, text: str) -> None:
    """Write one output file."""
    try:
        out.mkdir(parents=True, exist_ok=True)
        (out / name).write_text(text, encoding="utf-8")
    except OSError as error:
        raise ConfigError(f"{out / name}: cannot be written ({error})") from error


def write_index(out: Path, result: IndexResult) -> None:
    """Write the index files to `out`."""
    _write(out, "index.tsv", orig_report.render_index_tsv(result.index.rows))
    _write(out, "untagged.tsv", orig_report.render_untagged_tsv(result))
    _write(out, "report.md", orig_report.render_report(result))
    _write(out, "research-gaps.md", orig_report.render_research_gaps(result))
    if result.callees:
        _write(out, "callees.tsv", orig_report.render_callees_tsv(result))


def queue_inputs(root: Path, result: IndexResult, issues: list[orig_queue.Issue]) -> orig_queue.QueueInputs:
    """What the ranking reads, from the index result, the research pages and the issues."""
    return orig_queue.QueueInputs(
        result.index.rows,
        result.index.untagged,
        issues,
        result.findings,
        orig_index.evidence_levels(root),
        {gap.link for gap in result.research.gaps},
        {row.where for row in result.research.uncited},
    )


def run(args: argparse.Namespace, fetch: orig_ghidra.Fetch | None = None) -> int:
    """Run `orig index`, `orig queue` or `orig check`."""
    if args.command == "check":
        return orig_check.run(args.base)
    root = find_repo_root(Path.cwd())
    result = collect(root, args.ghidra, args.url, fetch)
    write_index(args.out, result)
    for line in orig_report.summary_lines(result):
        print(f"orig: {line}")
    written = [*INDEX_FILES, *(["callees.tsv"] if result.callees else [])]
    if args.command == "queue":
        issues = orig_queue.load_issues_file(args.issues) if args.issues else orig_queue.fetch_issues(args.label)
        entries = orig_queue.rank(queue_inputs(root, result, issues))
        summary = orig_queue.subsystems(entries)
        _write(args.out, "queue.md", orig_queue.render_queue(entries, summary, args.show))
        _write(args.out, "queue.tsv", orig_queue.render_queue_tsv(entries))
        written += ["queue.md", "queue.tsv"]
        print(f"orig: queue of {len(entries):,} entries from {len(issues)} open {args.label} issues")
    print(f"orig: wrote {', '.join(written)} to {args.out}")
    return 0
