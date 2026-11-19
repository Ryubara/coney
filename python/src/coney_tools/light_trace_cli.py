# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools trace lights`: trace the original's light selection in a PCSX2 you hold a claim on
(coney_tools.light_trace), write the trace beside the work in scratch and, with `--run-record`, its run record
(docs/guides/coney-tools-trace.md#trace-lights)."""

from __future__ import annotations

import argparse
from dataclasses import replace
from pathlib import Path
from typing import Any

from coney_tools import light_trace, run_records_cli
from coney_tools.config import find_repo_root
from coney_tools.debug_server import DebugClient
from coney_tools.pine import PineClient, PineError
from coney_tools.wad import refuse_inside_repo

#: The record's scenario name, and its id's name unless `--run-name` gives another.
RUN_NAME = "light-selection"
TOOL = "trace lights"
#: What the record says of the method, so that a reader knows what its counts count.
METHOD = (
    "Breakpoints on LightManager_SelectLights' entry and its callers' return addresses through the debug server: at "
    "the entry the arguments and lists A, B, C and the manager's point lights, at the return the upload list; every "
    "light read as values (type, radius, colour, frame position). The CSVs stay in scratch; their hashes are below."
)
#: The addresses the record names, docs/research/lighting.md#select.
ADDRESSES = {
    "LightManager_SelectLights": light_trace.ENTRY,
    "upload_list": light_trace.UPLOAD_LIST,
    "list_a": light_trace.COPIED_LISTS["a"],
    "list_b": light_trace.COPIED_LISTS["b"],
    "list_c": light_trace.COPIED_LISTS["c"],
}


def add_command(commands: Any) -> None:
    """Register `trace lights`."""
    lights = commands.add_parser(
        "lights", help="trace the original's light selection per call in a claimed PCSX2 (inputs and lights chosen)"
    )
    lights.add_argument("--out", type=Path, required=True, help="the selections CSV (outside the repository)")
    lights.add_argument("--agent", required=True, help="your id: the trace runs on the PCSX2 copy you have claimed")
    lights.add_argument("--copy", help="the claimed copy, when you hold more than one")
    lights.add_argument("--calls", type=int, default=300, help="the calls to trace (default 300)")
    lights.add_argument(
        "--wait", type=float, default=10.0, help="seconds without a call after which the trace ends (default 10)"
    )
    lights.add_argument(
        "--run-record",
        action="store_true",
        help="also write research/runs/<id>.toml (counts and the CSVs' hashes) for a claim to cite",
    )
    lights.add_argument(
        "--library-state", help="with --run-record: the state library id the run started from, or unknown"
    )
    lights.add_argument("--run-name", help=f"with --run-record: the run's name in its id (default: {RUN_NAME})")


def run(args: argparse.Namespace) -> int:
    """`trace lights` from its parsed arguments."""
    request = run_records_cli.trace_request(args.run_record, args.library_state, args.run_name)
    return run_lights(args.out, args.agent, args.copy, (args.calls, args.wait), request)


def debug_client(agent: str, copy: str) -> DebugClient:
    """A client of the debug server of `copy`, which `agent` holds; a function so that tests can give a fake."""
    return DebugClient.claimed(agent, copy)


def _held_copy(agent: str, copy: str | None) -> str:
    """The copy `agent` traces on: `copy`, or the only one it holds. ConfigError when it holds none or several."""
    from coney_tools import pcsx2_claims as claims  # the claim layer, only needed for a run

    if copy is not None:
        return copy
    held = [claim.copy for claim in claims.default_registry().held_by(agent)]
    if len(held) != 1:
        raise claims.ClaimError(
            f"{agent} holds {', '.join(held) or 'no PCSX2 copy'}: claim one with `coney-tools pcsx2 claim` or name it"
            " with --copy"
        )
    return held[0]


def run_lights(
    out: Path,
    agent: str,
    copy: str | None,
    limits: tuple[int, float],
    request: run_records_cli.RecordRequest | None,
) -> int:
    """Trace `limits` (calls, seconds to wait for one) in the PCSX2 of `agent`'s claim, write the files at `out` and,
    with `request`, the run record. Returns 1 when no call came (nothing is written then), else 0."""
    refuse_inside_repo(out)
    name = _held_copy(agent, copy)
    held = run_records_cli.claimed_copy(agent, name)
    pending = None
    root = None
    if request is not None:
        root = find_repo_root(Path.cwd())
        stand_in = dict.fromkeys(light_trace.facts([]), -(2**63 - 1))
        hashes = dict.fromkeys(("selections_csv", "calls_csv", "lists_csv"), "0" * 64)
        pending = run_records_cli.draft(
            root,
            kind="trace",
            tool=TOOL,
            run_name=request.name or RUN_NAME,
            copy=name,
            state=request.state,
            name=RUN_NAME,
            method=METHOD,
            values=stand_in,
            addresses=ADDRESSES,
            hashes=hashes,
        )
        run_records_cli.precheck(root, pending)
    calls_wanted, wait = limits
    with debug_client(agent, name) as client:
        calls = light_trace.trace(client, calls_wanted, wait)
    if not calls:
        print(f"no call of the light selection within {wait:g} s: is the game drawing a level, and not paused?")
        return 1
    hashes = light_trace.write_files(calls, out)
    facts = light_trace.facts(calls)
    shown = ", ".join(f"{facts[f'calls_{c}']} {c}" for c in light_trace.CALLERS.values() if facts[f"calls_{c}"])
    print(f"{out}: {len(calls)} calls ({shown}), the inputs beside it (.calls.csv, .lists.csv)")
    if pending is not None and root is not None and held.port is not None:
        version = run_records_cli.UNKNOWN
        try:
            with PineClient(held.port) as pine:
                version = run_records_cli.pcsx2_version(pine)
        except PineError as error:
            print(f"warning: PCSX2's version could not be read ({error}); the record says unknown")
        run_records_cli.save(root, replace(pending, values=facts, hashes=hashes, pcsx2_version=version))
    return 0
