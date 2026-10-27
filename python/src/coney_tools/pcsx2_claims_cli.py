# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools pcsx2 claim | release | status | check-ports | keys | screenshot` commands (the claims are in
pcsx2_claims.py, the Win32 calls in pcsx2_proc.py). Nothing here focuses a window: keys are posted to a window's
handle and screenshots read it by handle, so the user keeps the keyboard."""

from __future__ import annotations

import json
from dataclasses import asdict
from pathlib import Path

from coney_tools import pcsx2_claims as claims
from coney_tools import pcsx2_proc
from coney_tools.config import ConfigError
from coney_tools.wad import refuse_inside_repo


def _registry(max_age_hours: float | None = None) -> claims.Registry:
    """The registry of this machine, with the stale-claim age from `--max-age-hours` when given."""
    registry = claims.default_registry()
    if max_age_hours is not None:
        registry.max_age = max_age_hours * 3600
    return registry


def run_claim(agent: str, copy: str | None, as_json: bool, max_age_hours: float | None) -> int:
    """`pcsx2 claim`: claim a free copy (or the named one) and print its folder, PINE port and debug server port
    (the ports `pcsx2_connect` of the PCSX2 MCP server takes). The ports are the copy's ini's now: a claim held from
    before `pcsx2 setup-copies` changed them still prints the ports PCSX2 will use."""
    registry = _registry(max_age_hours)
    claim, took_over = registry.claim(agent, copy)
    held = registry.copy(claim.copy)
    if as_json:
        current = {**asdict(claim), "port": held.port, "debug_port": held.debug}
        print(json.dumps({**current, "took_over": asdict(took_over) if took_over else None}))
        return 0
    if took_over is not None:
        print(f"took over a stale claim of {took_over.agent} (since {took_over.time}) on {claim.copy}")
    debug = held.debug if held.debug is not None else "off"
    print(f"{agent} holds {claim.copy}: folder {claim.path}, PINE port {held.port}, debug server port {debug}")
    print(f"pcsx2-dir={claim.path} port={held.port} debug-port={debug}")
    _warn_sdl([held])
    return 0


def _warn_sdl(copies: list[claims.Copy]) -> None:
    """Warn about copies whose ini lets a physical gamepad drive them (`[InputSources]` `SDL = true`)."""
    on = [c.name for c in copies if c.sdl]
    if on:
        print(
            f"warning: SDL = true in the ini of {', '.join(on)}: a physical gamepad would drive the game; "
            "the user turns it on only to test by hand, so set it false in [InputSources] unless told otherwise",
        )


def run_release(agent: str, copy: str | None, force: bool) -> int:
    """`pcsx2 unclaim`: close the copy's PCSX2 and drop the claim."""
    for claim, closed in _registry().release(agent, copy, force):
        print(f"released {claim.copy} (held by {claim.agent}){'; closed its PCSX2' if closed else ''}")
    return 0


def _state(row: claims.Row) -> str:
    """The state word of a status row."""
    if row.claim is None:
        return "in use?" if row.running else "free"
    return "stale" if row.stale else "held"


def status_json(rows: list[claims.Row]) -> list[dict[str, object]]:
    """The rows of `pcsx2 status --json` (also what `coord status` shows of the copies)."""
    return [
        {
            "copy": r.copy.name,
            "path": str(r.copy.path),
            "port": r.copy.port,
            "debug_port": r.copy.debug,
            "state": _state(r),
            "claim": asdict(r.claim) if r.claim else None,
            "running": r.running,
            "sdl": r.copy.sdl,
            "pids": list(r.pids),
        }
        for r in rows
    ]


def status_rows(max_age_hours: float | None = None) -> list[dict[str, object]]:
    """This machine's copies as `pcsx2 status --json` lists them."""
    return status_json(_registry(max_age_hours).status())


def run_status(as_json: bool, max_age_hours: float | None) -> int:
    """`pcsx2 status`: every copy, who holds it, and whether its PCSX2 really runs."""
    rows = _registry(max_age_hours).status()
    if as_json:
        print(json.dumps(status_json(rows)))
        return 0
    table = [("copy", "state", "agent", "since", "port", "debug", "running")]
    for r in rows:
        table.append(
            (
                r.copy.name,
                _state(r),
                r.claim.agent if r.claim else "-",
                r.claim.time if r.claim else "-",
                str(r.copy.port or "off"),
                str(r.copy.debug or "off"),
                ("yes (pid " + ",".join(map(str, r.pids)) + ")") if r.pids else ("port" if r.running else "no"),
            )
        )
    widths = [max(len(line[i]) for line in table) for i in range(len(table[0]))]
    for line in table:
        print("  ".join(cell.ljust(width) for cell, width in zip(line, widths, strict=True)).rstrip())
    _warn_sdl([r.copy for r in rows])
    unclaimed = [r.copy.name for r in rows if r.claim is None and r.running]
    if unclaimed:
        print(f"note: {', '.join(unclaimed)} run PCSX2 without a claim; whoever started it should claim it")
    return 0


def run_check_ports(agent: str, debug_port: int | None, pine_port: int | None) -> int:
    """`pcsx2 check-claim`: succeed only when the ports are those of one copy that `agent` holds. Run it before pointing
    a client (the pcsx2 MCP server's `pcsx2_connect`, a script) at ports you were given or read: a copy another agent
    holds is refused with a ClaimError (exit 2) naming its holder."""
    claim = claims.default_registry().require_ports([agent], debug_port=debug_port, pine_port=pine_port)
    print(f"ok: {agent} holds {claim.copy}")
    return 0


def _running_copy(agent: str, name: str) -> tuple[claims.Registry, int]:
    """The registry and PCSX2 pid of a copy the agent holds. Raises ClaimError when it is not held or not running."""
    registry = claims.default_registry()
    claim = registry.require_claim(agent, name)
    row = next(r for r in registry.status() if r.copy.name == name)
    if not row.pids:
        raise claims.ClaimError(f"{name} runs no PCSX2 (claim: {claim.agent}); start it with `pcsx2 launch`")
    return registry, row.pids[0]


def run_keys(agent: str, copy: str, keys: list[str], hold_ms: int, gap_ms: int) -> int:
    """`pcsx2 send-keys`: post keys to the copy's window by handle. The window is never focused: with no focus a key may
    not reach PCSX2, and then the command says so rather than taking the focus."""
    chords = pcsx2_proc.parse_chords(keys)
    _, pid = _running_copy(agent, copy)
    targets = [w.hwnd for w in pcsx2_proc.key_targets(pid)]
    pcsx2_proc.post_keys(targets, chords, hold_ms, gap_ms)
    print(f"posted {' '.join(keys)} to {len(targets)} window(s) of {copy} (pid {pid}); no focus changed")
    return 0


def run_screenshot(copy: str, out: Path) -> int:
    """`pcsx2 screenshot`: write the copy's window as a PNG, read by handle (no focus change)."""
    refuse_inside_repo(out)
    registry = claims.default_registry()
    row = next((r for r in registry.status() if r.copy.name == registry.copy(copy).name), None)
    if row is None or not row.pids:
        raise ConfigError(f"{copy} runs no PCSX2")
    window = pcsx2_proc.render_window(row.pids[0])
    width, height, lit = pcsx2_proc.capture(window.hwnd, out)
    print(f"{out}: {width} x {height} of window {window.hwnd:#x} ({window.title or window.cls})")
    if not lit:
        print("warning: the picture is all black (PCSX2 may not draw while hidden or minimised)")
    return 0
