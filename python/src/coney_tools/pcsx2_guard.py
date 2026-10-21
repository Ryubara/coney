# SPDX-License-Identifier: GPL-3.0-or-later
"""The decisions of the hook that keeps an agent off a PCSX2 copy it has not claimed (`.claude/hooks/pcsx2_guard.py`).

**Why.** Every agent of a session shares one `pcsx2` MCP server, and the server holds one connection. When one agent
calls `pcsx2_connect`, the connection of every other agent in the session moves with it: on 2026-10-21 a memory write
landed in another agent's game that way, and another agent's emulator was paused for 10 to 20 seconds. The server
cannot tell its callers apart, and the ports of every copy are readable by anyone, so the check has to sit in front
of the tool, where Claude Code names the caller (`session_id`, and inside a subagent `agent_id`).

**What it enforces** (docs/guides/agents.md#setup, docs/guides/research-workflow.md#several-at-once):

* `pcsx2_connect` is allowed only with the ports the connection needs given explicitly (the server's defaults belong
  to whichever copy it was started for), both of one copy, whose claim is live and held by an id the caller goes by
  (`Registry.require_ports`). The caller goes by the ids of its own `coney-tools pcsx2 ... --agent ID` commands, which
  the same hook reads from the shell tools (`agents_in`, `Guard.bind`), so an agent that never claimed through a shell
  is told to.
* One **lease** per session says whose connection the server holds: the caller of the last allowed connect. While the
  lease is valid (its claim still held by the same id, and used within `LEASE_IDLE_SECONDS`), another caller's connect
  is refused, and so is every other `pcsx2_*` tool of another caller: they cannot move, pause or write to the holder's
  game. They reach their own copy with `coney-tools pcsx2 ...` or `DebugClient.claimed` instead, which are not shared.
* `pcsx2_status` (reads only what the server says about itself) and the `ps2recomp_*` tools (no emulator) always pass.

The guard fails open where it cannot work: no PCSX2 copies set up on the machine, no session id in the payload,
anything unexpected. State is a few small files below the claims folder (`<claims dir>/mcp/`), one per caller and one
lease per session, replaced whole; nothing here needs a server.
"""

from __future__ import annotations

import json
import os
import re
import time
import uuid
from collections.abc import Callable, Collection
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

from coney_tools import pcsx2_claims
from coney_tools.config import ConfigError
from coney_tools.pcsx2_claims import ClaimError, Registry

#: The prefix of the `pcsx2` MCP server's tools.
SERVER_PREFIX = "mcp__pcsx2__"
#: A connection nobody has used for this many seconds is given up (15 minutes), so an agent that stopped without
#: releasing its copy does not keep the others out of the server.
LEASE_IDLE_SECONDS = 900.0
#: The `pcsx2_connect` ports each mode needs.
_MODE_PORTS = {"auto": ("debug_port", "pine_port"), "debug": ("debug_port",), "pine": ("pine_port",)}

_AGENT_ID = r"[A-Za-z0-9._-]{1,64}"
#: A `coney-tools pcsx2 <command>` invocation, up to the end of the line or the next shell separator.
_PCSX2_COMMAND = re.compile(r"coney-tools(?:\.exe)?\s+pcsx2\s+[a-z][\w-]*(?P<rest>[^\n;&|]*)")
_AGENT_FLAG = re.compile(rf"(?:^|\s)--agent(?:=|\s+)(?P<id>{_AGENT_ID})(?=\s|$)")


def agents_in(command: str) -> list[str]:
    """The agent ids a shell command passes to `coney-tools pcsx2 ...` (`--agent ID` or `--agent=ID`), in order. An id
    after another coney-tools command (`build --agent x`) is not one: only a PCSX2 command makes the caller its
    claim's holder."""
    found: list[str] = []
    for call in _PCSX2_COMMAND.finditer(command):
        found.extend(flag.group("id") for flag in _AGENT_FLAG.finditer(call.group("rest")))
    return found


def _key(text: str) -> str:
    """A name safe to use in a file name (the session and agent ids come from the agent tool, not from us)."""
    return re.sub(r"[^A-Za-z0-9_-]", "_", text)[:80] or "main"


@dataclass(frozen=True)
class Lease:
    """Whose connection the session's server holds: the caller (`agent_id`, the agent tool's id for it) that made the
    last allowed `pcsx2_connect`, the claim id it went by, the copy, and when it last used the server."""

    agent_id: str
    agent: str
    copy: str
    used: float


class Guard:
    """The guard's state (below `folder`) and decisions, over a registry of copies and claims."""

    def __init__(self, registry: Registry, folder: Path, clock: Callable[[], float] = time.time) -> None:
        """`folder` holds the bindings and leases; `clock` returns epoch seconds (tests pass a fake one)."""
        self.registry = registry
        self.folder = folder
        self.clock = clock

    # -- files

    def _write(self, path: Path, data: dict[str, Any]) -> None:
        """Replace a state file whole (a temporary file, then an atomic replace)."""
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary = path.with_name(f"{path.name}.{os.getpid()}.{uuid.uuid4().hex[:6]}")
        temporary.write_text(json.dumps(data), encoding="utf-8")
        temporary.replace(path)

    def _read(self, path: Path) -> dict[str, Any] | None:
        """A state file's object, None when there is none or it cannot be read."""
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return None
        return data if isinstance(data, dict) else None

    def _binding(self, session: str, agent_id: str) -> Path:
        """The file that holds the claim ids one caller of a session goes by."""
        return self.folder / f"{_key(session)}.bind.{_key(agent_id)}.json"

    def _lease_file(self, session: str) -> Path:
        """The file that holds a session's lease."""
        return self.folder / f"{_key(session)}.lease.json"

    # -- who is calling

    def bind(self, session: str, agent_id: str, names: Collection[str]) -> None:
        """Note that this caller goes by `names` (the ids of its `pcsx2 ... --agent` commands) from now on."""
        known = self.bound(session, agent_id)
        if set(names) <= known:
            return
        self._write(self._binding(session, agent_id), {"names": sorted(known | set(names))})

    def bound(self, session: str, agent_id: str) -> set[str]:
        """The claim ids this caller goes by."""
        data = self._read(self._binding(session, agent_id))
        names = data.get("names") if data else None
        return {name for name in names if isinstance(name, str)} if isinstance(names, list) else set()

    # -- the lease

    def _lease(self, session: str) -> Lease | None:
        """The session's lease when it is still valid: its claim is live and held by the id it went by, and it was
        used lately. An invalid lease is as good as none."""
        data = self._read(self._lease_file(session))
        try:
            lease = Lease(**(data or {}))
        except TypeError:
            return None
        if self.clock() - lease.used > LEASE_IDLE_SECONDS:
            return None
        row = next((r for r in self.registry.status() if r.copy.name == lease.copy), None)
        if row is None or row.claim is None or row.stale or row.claim.agent != lease.agent:
            return None
        return lease

    def _set_lease(self, session: str, lease: Lease) -> None:
        """Make `lease` the session's."""
        self._write(self._lease_file(session), asdict(lease))

    # -- the decisions

    def decide(self, session: str, agent_id: str, tool: str, arguments: dict[str, Any]) -> str | None:
        """None when the call may go ahead, else why it is refused (shown to the agent). Fails open when no copies are
        set up."""
        name = tool.removeprefix(SERVER_PREFIX)
        if name == tool or not name.startswith("pcsx2_") or name == "pcsx2_status":
            return None
        try:
            self.registry.copies()
        except (ClaimError, ConfigError):
            return None  # no managed copies on this machine: nothing to protect, and no claims to check against
        if name == "pcsx2_connect":
            return self._connect(session, agent_id, arguments)
        return self._use(session, agent_id, name)

    def _connect(self, session: str, agent_id: str, arguments: dict[str, Any]) -> str | None:
        """The decision on `pcsx2_connect`: the ports must be those of a copy the caller claimed, and the session's
        connection must not be another caller's."""
        mode = arguments.get("mode", "auto")
        needed = _MODE_PORTS.get(mode if isinstance(mode, str) else "auto", _MODE_PORTS["auto"])
        ports: dict[str, int] = {}
        for key in needed:
            value = arguments.get(key)
            if isinstance(value, bool) or not isinstance(value, int | float | str) or not str(value).isdigit():
                return (
                    f"Blocked: pcsx2_connect needs `{key}` of your claimed copy. The server's defaults belong to "
                    "whichever copy it was started for, and a connect moves the connection of every agent of this "
                    "session. Run `uv run --project python coney-tools pcsx2 claim --agent <your id>` (it returns your "
                    "claim again if you hold one), then connect with the `debug-port` and `port` (PINE) it prints, "
                    '`mode: "debug"`, and check `pcsx2_status` before the first write.'
                )
            ports[key] = int(value)
        names = self.bound(session, agent_id)
        if not names:
            return (
                "Blocked: this guard does not know your claim. Run `uv run --project python coney-tools pcsx2 claim "
                "--agent <your id>` in a shell first (it returns your claim again if you hold one); the guard reads "
                "the id from that command, then lets you connect to the ports it prints."
            )
        try:
            claim = self.registry.require_ports(
                names, debug_port=ports.get("debug_port"), pine_port=ports.get("pine_port")
            )
        except ClaimError as error:
            return f"Blocked: {error}."
        lease = self._lease(session)
        if lease is not None and lease.agent_id != agent_id:
            return self._in_use(lease)
        self._set_lease(session, Lease(agent_id, claim.agent, claim.copy, self.clock()))
        return None

    def _use(self, session: str, agent_id: str, name: str) -> str | None:
        """The decision on any other tool that reaches the emulator: only the holder of the session's lease may."""
        lease = self._lease(session)
        if lease is None:
            return (
                f"Blocked: {name} acts on whatever copy the shared pcsx2 server is connected to, and no connection of "
                "yours is on record. Call pcsx2_connect with the `debug_port` and `pine_port` of your own claim "
                "(`coney-tools pcsx2 claim --agent <your id>` prints them) first."
            )
        if lease.agent_id != agent_id:
            return self._in_use(lease)
        self._set_lease(session, Lease(lease.agent_id, lease.agent, lease.copy, self.clock()))
        return None

    def _in_use(self, lease: Lease) -> str:
        """The refusal for a caller that is not the lease's holder."""
        return (
            f"Blocked: the pcsx2 server of this session is connected to {lease.copy} for {lease.agent}, another agent; "
            "one connect or one write from you would move or change that agent's game. Use your own copy without the "
            "shared server: `coney-tools pcsx2 ...` (record, recipe, clip) or, from a script, "
            '`DebugClient.claimed("<your id>")` (coney_tools.debug_server), both bound to your claim. The server frees '
            f"up when {lease.agent} releases its copy or leaves it unused for {LEASE_IDLE_SECONDS / 60:.0f} minutes."
        )


def default_guard(start: Path) -> Guard:
    """The guard for this machine: the registry of copies and claims (pcsx2_claims.default_registry, from the
    checkout at `start`) and the state folder `mcp/` beside the claims."""
    registry = pcsx2_claims.default_registry(start)
    return Guard(registry, registry.claims_dir / "mcp")


def handle(payload: dict[str, Any], guard: Guard) -> str | None:
    """What the hook does with one payload of Claude Code: a shell command that runs `coney-tools pcsx2 ... --agent ID`
    binds the caller to ID (nothing is refused), and a call of the pcsx2 MCP server is decided. Returns the reason to
    refuse, or None. A payload with no session is let through (there is nothing to key the connection on)."""
    session = payload.get("session_id")
    if not isinstance(session, str) or not session:
        return None
    agent_id = payload.get("agent_id")
    agent_id = agent_id if isinstance(agent_id, str) else ""
    tool = payload.get("tool_name")
    arguments = payload.get("tool_input")
    if not isinstance(tool, str) or not isinstance(arguments, dict):
        return None
    if tool in ("Bash", "PowerShell"):
        command = arguments.get("command")
        names = agents_in(command) if isinstance(command, str) else []
        if names:
            guard.bind(session, agent_id, names)
        return None
    return guard.decide(session, agent_id, tool, arguments)
