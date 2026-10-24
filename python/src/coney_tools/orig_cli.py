# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools orig`: the `@orig` index and the fidelity audit's queue (docs/guides/coney-tools-fidelity.md#orig)."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from coney_tools import (
    orig_check,
    orig_copy,
    orig_ghidra,
    orig_index,
    orig_queue,
    orig_report,
    orig_review,
    progress,
    progress_research,
)
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
    copy = commands.add_parser(
        "copy-check",
        help="refuse decompiler fingerprints (iVar1, param_1, undefined4, *(T *)(x + 0x10), goto) in changed C++ lines",
        description=(
            "Fails when a line of a C++ file under src/ added since HEAD left BASE carries what Ghidra's output looks "
            "like in its code (comments and string literals are skipped): an automatic name, an undefined type, a "
            "raw read at a byte offset or a goto (docs/guides/conventions.md#porting). A line may carry "
            "`// copy-check: allow <reason>`."
        ),
    )
    copy.add_argument("--base", default="origin/main", help="what the change is measured from (default: %(default)s)")
    copy.add_argument("--all", action="store_true", help="check every line of src/, not only the changed ones")
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
    _ledger_option(queue)
    stamp = commands.add_parser(
        "review-stamp",
        help="print, as JSON, the @orig functions an orig review covers (a branch's changes, or a sweep's scope)",
        description=(
            "With --branch or --worktree: the functions carrying @orig (their own tag, one in their body or their "
            "declaration's) that the branch adds or changes since it left BASE, read from commits (pr mode). With "
            "--folder or --address: those of the folders or addresses at REF that the ledger has not seen at their "
            "current body (sweep mode; --every lists the reviewed ones too). The reviewer agent starts here."
        ),
    )
    side = stamp.add_mutually_exclusive_group(required=True)
    side.add_argument("--branch", help="the branch to review (pr mode)")
    side.add_argument("--worktree", type=Path, help="review the commit checked out in this worktree (pr mode)")
    side.add_argument("--folder", action="append", default=[], help="a src/ folder to sweep (repeatable)")
    side.add_argument("--address", action="append", default=[], help="an @orig address to sweep (repeatable)")
    stamp.add_argument("--base", default="origin/main", help="pr mode: the branch's base (default: %(default)s)")
    stamp.add_argument("--ref", default="HEAD", help="sweep mode: the commit to read (default: %(default)s)")
    stamp.add_argument("--every", action="store_true", help="sweep mode: list reviewed functions too")
    _ledger_option(stamp)
    gate = commands.add_parser(
        "check-review",
        help="a branch that changes @orig functions needs a passing ORIG-REVIEW.json for its tip",
        description=(
            "Exit 0 when the branch changes no @orig function, or when the verdict file next to the description "
            "passes for the branch's tip and covers every changed function (a pass is recorded in the ledger); 1 "
            "when it does not; 2 when the branch cannot be told. The branch is --head, else the one checked out in "
            "--cwd, else the description's folder under the workspace's scratch."
        ),
    )
    gate.add_argument("--body", type=Path, required=True, help="the description file (PR.md)")
    gate.add_argument("--head", help="the branch (as gh pr create --head names it)")
    gate.add_argument("--base", help="the base branch (default: the default branch on origin)")
    gate.add_argument("--cwd", help="the folder the release runs in")
    _ledger_option(gate)
    record = commands.add_parser(
        "record",
        help="record a sweep's FINDINGS.tsv files in the review ledger and print the ledger's counts",
        description=(
            "Reads each FINDINGS.tsv (addr, orig_name, coney_function, file:line, class, evidence, note; "
            "docs/guides/review/orig.md#findings) and records each row against the function at REF, the commit the "
            "sweep read; then prints how many @orig functions at HEAD are unreviewed, stale or in each class."
        ),
    )
    record.add_argument("findings", type=Path, nargs="*", help="FINDINGS.tsv files")
    record.add_argument("--ref", default="HEAD", help="the commit the findings were made at (default: %(default)s)")
    _ledger_option(record)


def _ledger_option(parser: argparse.ArgumentParser) -> None:
    """The --ledger option the review commands share."""
    parser.add_argument(
        "--ledger",
        type=Path,
        help="the review ledger (default: pull_requests.orig_review.ledger in the scratch folder)",
    )


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


def queue_inputs(
    root: Path, result: IndexResult, issues: list[orig_queue.Issue], reviews: dict[int, str] | None = None
) -> orig_queue.QueueInputs:
    """What the ranking reads, from the index result, the research pages, the issues and the review ledger."""
    return orig_queue.QueueInputs(
        result.index.rows,
        result.index.untagged,
        issues,
        result.findings,
        orig_index.evidence_levels(root),
        {gap.link for gap in result.research.gaps},
        {row.where for row in result.research.uncited},
        reviews or {},
    )


def _ledger(root: Path, args: argparse.Namespace) -> orig_review.Ledger:
    """The ledger --ledger names, else the workspace's."""
    return orig_review.Ledger(args.ledger) if args.ledger else orig_review.default_ledger(root)


def _review_command(args: argparse.Namespace) -> int:
    """Run `orig review-stamp`, `orig check-review` or `orig record`."""
    root = find_repo_root(Path.cwd())
    if args.command == "check-review":
        try:
            problems = orig_review.gate(root, args.body, args.head, args.base, args.cwd, _ledger(root, args))
        except orig_review.CannotTell as error:
            print(f"orig check-review: cannot tell ({error})")
            return 2
        for problem in problems:
            print(problem)
        print("orig check-review: " + ("refused" if problems else "ok"))
        return 1 if problems else 0
    ledger = _ledger(root, args)
    if args.command == "review-stamp":
        if args.branch or args.worktree:
            tip = args.branch or orig_review.resolve(args.worktree, "HEAD")
            stamp = orig_review.pr_stamp(root, tip, args.base)
        else:
            addresses = [int(a, 16) for a in args.address]
            stamp = orig_review.sweep_stamp(root, args.ref, args.folder, addresses, ledger, args.every)
        print(json.dumps(stamp, indent=2))
        return 0
    added, unmatched = orig_review.import_findings(root, ledger, args.findings, args.ref) if args.findings else (0, [])
    for line in unmatched:
        print(f"orig record: no @orig function matches {line}")
    counts = orig_review.summary(ledger, orig_review.units_at(root, "HEAD"))
    print(f"orig record: {added} new entries in {ledger.path}")
    print("orig record: at HEAD " + ", ".join(f"{name} {count}" for name, count in counts.items()))
    return 0


def run(args: argparse.Namespace, fetch: orig_ghidra.Fetch | None = None) -> int:
    """Run `orig index`, `orig queue` or `orig check`."""
    if args.command == "check":
        return orig_check.run(args.base)
    if args.command == "copy-check":
        return orig_copy.run(args.base, args.all)
    if args.command in ("review-stamp", "check-review", "record"):
        return _review_command(args)
    root = find_repo_root(Path.cwd())
    result = collect(root, args.ghidra, args.url, fetch)
    write_index(args.out, result)
    for line in orig_report.summary_lines(result):
        print(f"orig: {line}")
    written = [*INDEX_FILES, *(["callees.tsv"] if result.callees else [])]
    if args.command == "queue":
        issues = orig_queue.load_issues_file(args.issues) if args.issues else orig_queue.fetch_issues(args.label)
        ledger = _ledger(root, args)
        reviews = orig_review.address_status(ledger, orig_review.units_now(root))
        entries = orig_queue.rank(queue_inputs(root, result, issues, reviews))
        summary = orig_queue.subsystems(entries)
        _write(args.out, "queue.md", orig_queue.render_queue(entries, summary, args.show))
        _write(args.out, "queue.tsv", orig_queue.render_queue_tsv(entries))
        written += ["queue.md", "queue.tsv"]
        print(f"orig: queue of {len(entries):,} entries from {len(issues)} open {args.label} issues")
        done = sum(orig_queue.review_done(status) for status in reviews.values())
        print(f"orig: {done:,} reviewed addresses left out (ledger {ledger.path})")
    print(f"orig: wrote {', '.join(written)} to {args.out}")
    return 0
