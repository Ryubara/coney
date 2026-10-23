# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools original ...`: study an original function, compare it with Coney's, record fails-first runs.

The stage commands of docs/guides/stamps.md. Names are placeholders for the renaming pass: each is one parser entry here
and one function in its module. Exit codes: 0 done, 1 refused (the output says what to fix), 2 it cannot start (a usage
or configuration error), 3 no ghidra-mcp answers.
"""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Any

from coney_tools import fails_first, orig_compare, orig_study, stamp_sign, stamps, unit
from coney_tools.config import find_repo_root, load_config, local_config_file
from coney_tools.conventions import Conventions, load_conventions
from coney_tools.progress_research import DEFAULT_GHIDRA_URL, http_fetch


def add_parser(groups: Any) -> None:
    """Register `coney-tools original study|compare|verify|fails-first`."""
    group = groups.add_parser(
        "original", help="study an original function, compare it with Coney's, record fails-first"
    )
    commands = group.add_subparsers(dest="command", required=True)

    study = commands.add_parser(
        "study",
        help="save an original function's decompile and neighbours from Ghidra, number its lines, cut its pieces",
        description=(
            "Reads the function at ADDR from the running ghidra-mcp (reading endpoints only), saves the decompile, "
            "callers, callees and globals under the task's scratch folder (never committed), numbers the statements, "
            "cuts them into pieces and appends a `studied` stamp. With --recut it re-cuts the saved decompile without "
            "Ghidra. Exit 3 when no ghidra-mcp answers."
        ),
    )
    study.add_argument("address", help="the function's address, such as 0x00230d00")
    _task(study)
    study.add_argument("--url", default=DEFAULT_GHIDRA_URL, help="the ghidra-mcp server (default: %(default)s)")
    study.add_argument(
        "--recut",
        action="store_true",
        help="re-cut the saved decompile without Ghidra (it must match the newest studied stamp)",
    )
    study.add_argument("--print", action="store_true", dest="show", help="also print the numbered decompile")

    compare = commands.add_parser(
        "compare",
        help="show the decompile beside the Coney function, check its pairs and stamp `compared`",
        description=(
            "The first run writes mapping/<addr>.toml with one blank pair per piece. Fill in coney and relation, then "
            "run it again: it checks coverage, relations, locations, stand-in comments, that the Coney file is "
            "committed and that `orig copy-check` is clean, and only then appends the `compared` stamp."
        ),
    )
    compare.add_argument("address")
    _task(compare)
    compare.add_argument(
        "--base", default="origin/main", help="what the copy check measures from (default: %(default)s)"
    )
    compare.add_argument("--coney-function", help="the unit key, when several Coney functions carry the address")
    compare.add_argument("--carry", action="store_true", help="bring the pairs forward onto a changed or renamed study")
    compare.add_argument("--show", action="store_true", help="print the side-by-side view")

    verify = commands.add_parser(
        "verify",
        help="whether a saved study still matches Ghidra: fresh, renamed or stale",
        description=(
            "Re-reads the function from Ghidra and compares it with the saved study: Ghidra's hash of its bytes, the "
            "cut's shape, the addresses of its callees and globals, and the text's skeleton (names numbered by first "
            "appearance, so only a one-to-one rename keeps it). Exits 1 when stale, 3 when Ghidra is down."
        ),
    )
    verify.add_argument("address")
    _task(verify)
    verify.add_argument("--url", default=DEFAULT_GHIDRA_URL, help="the ghidra-mcp server (default: %(default)s)")

    first = commands.add_parser(
        "fails-first",
        help="run named tests on the test commit and on the fix commit and stamp `fails-first`",
        description=(
            "Runs `coney-tools build --test -R` for the named tests on --test-commit (they must fail) and on "
            "--fix-commit (they must pass), restoring the branch afterwards. With --none REASON, stamps that the task "
            "has no such test (docs, tooling) and runs nothing."
        ),
    )
    first.add_argument(
        "--test", action="append", default=[], help="an exact, whole TEST_CASE name in the test commit (repeatable)"
    )
    first.add_argument("--test-commit", help="the commit that adds the test (and only the stubs it needs to compile)")
    first.add_argument("--fix-commit", help="the commit that makes it pass (default: HEAD)")
    first.add_argument("--preset", default="dev", help="the CMake preset (default: %(default)s)")
    first.add_argument("--agent", help="your id for the build slots (as `build --agent`)")
    first.add_argument("--none", metavar="REASON", help="stamp that there is no fails-first test, with the reason")
    _task(first)


def _task(parser: argparse.ArgumentParser) -> None:
    """The --task option the commands share."""
    parser.add_argument("--task", help="the task (default: CONEY_TASK, else the branch)")


def _conventions(root: Path) -> Conventions:
    """The conventions of the checkout."""
    return load_conventions(root)


def signer_for(root: Path, conventions: Conventions, step: str) -> stamps.Sign | None:
    """A signing function for `step` when it is a signed step and `stamp_key` is configured, else None (unsigned)."""
    if step not in conventions.stamps.signed or not local_config_file(root):
        return None
    key = load_config(root).paths.get("stamp_key")
    return stamp_sign.signer_for(key, conventions.stamps.namespace)


def run(args: argparse.Namespace) -> int:
    """Run an `original` command."""
    root = find_repo_root(Path.cwd())
    conventions = _conventions(root)
    task = unit.task_name(root, args.task)
    folder = unit.Unit.of(root, task)
    who = stamps.who_from_env(f"coney-tools original {args.command}")
    if args.command == "fails-first":
        return _fails_first(args, root, conventions, folder, who)
    address = unit.address(args.address)
    try:
        if args.command == "study":
            return _study(args, root, conventions, folder, address, who)
        if args.command == "verify":
            return _verify(args, folder, address)
        return _compare(args, root, conventions, folder, address, who)
    except orig_study.GhidraDown as error:
        print(f"original {args.command}: {error}")
        return 3


def _study(
    args: argparse.Namespace, root: Path, conventions: Conventions, folder: unit.Unit, address: str, who: dict[str, Any]
) -> int:
    """`original study`."""
    fetch = None if args.recut else http_fetch(args.url.rstrip("/"))
    result = orig_study.study(
        folder,
        address,
        fetch,
        rules=conventions.stamps,
        commit=unit.head_commit(root),
        who=who,
        sign=signer_for(root, conventions, "studied"),
    )
    study = result.study
    kinds: dict[str, int] = {}
    for piece in result.cut.pieces:
        kinds[piece.kind] = kinds.get(piece.kind, 0) + 1
    listing = folder.study(address) / "numbered.txt"
    print(
        f"original study: {address} {study.name}: {len(result.cut.statements)} lines, {len(result.cut.pieces)} pieces"
    )
    print("  pieces: " + ", ".join(f"{count} {kind}" for kind, count in sorted(kinds.items())))
    print(f"  callers {len(study.callers)}, callees {len(study.callees)}, globals {len(study.globals)}")
    print(f"  numbered decompile (scratch only, never commit): {listing}")
    if args.show:
        print(listing.read_text(encoding="utf-8"), end="")
    if result.change == "unchanged":
        print("  unchanged since the last study")
    else:
        print(f"  {result.change}; {'stamped studied' if result.stamped else 'a studied stamp already covers it'}")
    mapping = folder.mapping(address)
    # The pairs header binds the decompile's hash, so a rename makes the file stale as much as a change does.
    if result.change in ("changed", "renamed") and mapping.is_file():
        print(f"  the pairs file {mapping} was made for the old study: run `original compare {address} --carry`")
    return 0


def _verify(args: argparse.Namespace, folder: unit.Unit, address: str) -> int:
    """`original verify`."""
    saved = orig_study.load_study(folder, address)
    state = orig_study.freshness(saved, http_fetch(args.url.rstrip("/")))
    print(f"original verify: {address} {saved.name}: {state}")
    return 0 if state != "stale" else 1


def _compare(
    args: argparse.Namespace, root: Path, conventions: Conventions, folder: unit.Unit, address: str, who: dict[str, Any]
) -> int:
    """`original compare`."""
    result = orig_compare.compare(
        folder,
        root,
        address,
        conventions.stamps,
        commit=unit.head_commit(root),
        who=who,
        base=args.base,
        key=args.coney_function,
        carry=args.carry,
        sign=signer_for(root, conventions, "compared"),
    )
    if args.show or result.status != "stamped":
        print(result.view_path.read_text(encoding="utf-8"), end="")
    print(f"pairs file: {result.pairs_path}")
    if result.status == "created":
        print(f"original compare: wrote {result.counts['pairs']} blank pairs, one per piece")
        print("  fill in `coney` (path:line) and `relation` for each, then run it again")
        return 1
    for dropped in result.dropped:
        print(f"  dropped (lines {dropped.lines}): the numbering moved; map those lines again")
    if result.status == "refused":
        for problem in result.problems:
            print(f"  {problem}")
        print(f"original compare: refused, {len(result.problems)} problem(s); nothing stamped")
        return 1
    counts = result.counts
    stamp = result.stamp
    print(
        f"original compare: {address} {result.status}: {counts['pairs']} pairs over {counts['pieces']} pieces "
        f"({counts['same']} same, {counts['stand-in']} stand-in, {counts['not-ported']} not-ported)"
        + (f"; stamp #{stamp.seq}" if stamp else "")
    )
    return 0


def _fails_first(
    args: argparse.Namespace, root: Path, conventions: Conventions, folder: unit.Unit, who: dict[str, Any]
) -> int:
    """`original fails-first`."""
    rules = conventions.stamps
    sign = signer_for(root, conventions, "fails-first")
    if args.none is not None and (args.test or args.test_commit or args.fix_commit):
        print("original fails-first: --none runs nothing; give it alone, without --test or --test-commit")
        return 2
    if args.none is not None:
        stamp = fails_first.record_none(folder, root, args.none, rules=rules, who=who, sign=sign)
        print(f"original fails-first: none ({stamp.detail['reason']}); stamp #{stamp.seq}")
        return 0
    if not args.test or not args.test_commit:
        print("original fails-first: give --test NAME (once per test) and --test-commit SHA, or --none REASON")
        return 2
    agent = args.agent or who["agent"]["id"]
    if agent == "unknown":
        print("original fails-first: give --agent ID (the build slots need one)")
        return 2
    written = fails_first.run(
        folder,
        root,
        tests=args.test,
        test_commit=args.test_commit,
        fix_commit=args.fix_commit,
        preset=args.preset,
        runner=fails_first.build_runner(agent),
        rules=rules,
        who=who,
        sign=sign,
    )
    for stamp in written:
        print(
            f"original fails-first: {stamp.subject}: {stamp.result} "
            f"(test commit {stamp.detail['red']}, fix commit {stamp.detail['green']}); stamp #{stamp.seq}"
        )
    return 0 if all(s.result == "pass" for s in written) else 1
