# SPDX-License-Identifier: GPL-3.0-or-later
"""The decisions of the hook that keeps an agent off a PCSX2 copy it has not claimed (`.claude/hooks/pcsx2_guard.py`).

**Why.** Every agent of a session shares one `pcsx2` MCP server, and the server holds one connection. When one agent
calls `pcsx2_connect` with the ports of a copy it does not own, the connection of every other agent in the session
moves with it: on 2026-10-21 a memory write landed in another agent's game that way. The ports of every copy are
readable by anyone, and the server cannot tell its callers apart, so the check sits in front of the tool, where Claude
Code names the caller (`session_id`, and inside a subagent `agent_id`).

**What it enforces** (docs/guides/agents.md#pcsx2-guard): `pcsx2_connect` is allowed only with **both** ports given
(the server's defaults belong to whichever copy it was started for, and the arguments stay consistent whatever the
mode), both of one copy whose claim is live and held by an id the caller goes by (`Registry.require_ports`). The caller
goes by the ids of its own `coney-tools pcsx2 claim | launch | record ... --agent ID` commands, which the same hook
reads from the shell tools (`agents_in`, `Guard.bind`), so an agent that never claimed through a shell is told to.
Nothing else is refused, and nothing is locked: no lease, no per-session state beyond those ids.

**What it cannot do.** The server holds one connection per process, so another agent's `pcsx2_connect` to its *own*
claim still moves this agent's connection, and `mode: debug` leaves the previous PINE half. Both are the server's to
fix (a connection per caller, `pcsx2_connect` replacing both halves; the plan is in the track's STATUS.md).

The guard fails open where it cannot work (no PCSX2 copies set up, no session id in the payload, anything unexpected),
and writes one line to `guard.log` beside its state when it fails itself. State is a few small files below the claims
folder (`<claims dir>/mcp/`), one per caller, replaced whole and pruned after `BINDING_KEEP_SECONDS`.
"""

from __future__ import annotations

import json
import os
import re
import time
import uuid
from collections.abc import Collection
from pathlib import Path
from typing import Any

from coney_tools import pcsx2_claims
from coney_tools.config import ConfigError
from coney_tools.pcsx2_claims import ClaimError, Registry

#: The prefix of the `pcsx2` MCP server's tools.
SERVER_PREFIX = "mcp__pcsx2__"
#: The two ports `pcsx2_connect` is given whatever its mode: the debug server's and PINE's.
_PORTS = ("debug_port", "pine_port")
#: How long a caller's binding file is kept (3 days); older ones are removed when the next is written.
BINDING_KEEP_SECONDS = 3 * 86400.0
#: Where the hook notes its own failures, below the guard's folder.
LOG_FILE = "guard.log"

_AGENT_ID = r"[A-Za-z0-9._-]{1,64}"
#: The `coney-tools pcsx2` commands that run under the caller's own claim (making one when it holds none), so their
#: `--agent` names the caller. `release`, `status`, `keys` and the rest can name anyone and bind nobody.
CLAIMING_COMMANDS = ("claim", "launch", "record", "clip", "mission", "recipe")
#: A `coney-tools pcsx2 <claiming command>` invocation, up to the end of the line or the next shell separator.
_PCSX2_COMMAND = re.compile(
    r"coney-tools(?:\.exe)?[\"']?\s+pcsx2\s+(?:" + "|".join(CLAIMING_COMMANDS) + r")(?=\s|$)(?P<rest>[^\n;&|]*)"
)
#: `--agent ID`, `--agent=ID`, with the id bare or in single or double quotes.
_AGENT_FLAG = re.compile(rf"(?:^|\s)--agent(?:=|\s+)(?P<q>['\"]?)(?P<id>{_AGENT_ID})(?P=q)(?=\s|$)")
#: A line continuation: a backslash (POSIX) or a backtick (PowerShell) at the end of a line.
_CONTINUATION = re.compile(r"[\\`][ \t]*\r?\n")


def agents_in(command: str) -> list[str]:
    """The agent ids a shell command passes to a claiming `coney-tools pcsx2 ...` command (`--agent ID`, `--agent=ID`,
    quoted or not, also over continued lines), in order. An id after another coney-tools command (`build --agent x`)
    or after a PCSX2 command that can name anyone (`release --agent x`) is not one: only a command that runs under
    the caller's own claim says who the caller is."""
    found: list[str] = []
    for call in _PCSX2_COMMAND.finditer(_CONTINUATION.sub(" ", command)):
        found.extend(flag.group("id") for flag in _AGENT_FLAG.finditer(call.group("rest")))
    return found


def _key(text: str) -> str:
    """A name safe to use in a file name (the session and agent ids come from the agent tool, not from us)."""
    return re.sub(r"[^A-Za-z0-9_-]", "_", text)[:80] or "main"


class Guard:
    """The guard's state (below `folder`) and decisions, over a registry of copies and claims."""

    def __init__(self, registry: Registry, folder: Path) -> None:
        """`folder` holds the callers' bindings."""
        self.registry = registry
        self.folder = folder

    # -- who is calling

    def _binding(self, session: str, agent_id: str) -> Path:
        """The file that holds the claim ids one caller of a session goes by."""
        return self.folder / f"{_key(session)}.bind.{_key(agent_id)}.json"

    def bind(self, session: str, agent_id: str, names: Collection[str]) -> None:
        """Note that this caller goes by `names` (the ids of its claiming `pcsx2 ... --agent` commands) from now on,
        and drop the bindings nobody has renewed for BINDING_KEEP_SECONDS."""
        known = self.bound(session, agent_id)
        path = self._binding(session, agent_id)
        path.parent.mkdir(parents=True, exist_ok=True)
        if set(names) <= known and path.exists():
            path.touch()  # renewed: a caller that keeps claiming keeps its binding
        else:
            temporary = path.with_name(f"{path.name}.{os.getpid()}.{uuid.uuid4().hex[:6]}")
            temporary.write_text(json.dumps({"names": sorted(known | set(names))}), encoding="utf-8")
            temporary.replace(path)
        self._prune()

    def _prune(self) -> None:
        """Remove binding files not renewed for BINDING_KEEP_SECONDS (a session's file is never read again once it
        ends)."""
        now = time.time()
        for path in self.folder.glob("*.bind.*.json"):
            try:
                if now - path.stat().st_mtime > BINDING_KEEP_SECONDS:
                    path.unlink()
            except OSError:
                continue

    def bound(self, session: str, agent_id: str) -> set[str]:
        """The claim ids this caller goes by."""
        try:
            data = json.loads(self._binding(session, agent_id).read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return set()
        names = data.get("names") if isinstance(data, dict) else None
        return {name for name in names if isinstance(name, str)} if isinstance(names, list) else set()

    # -- the decision

    def decide(self, session: str, agent_id: str, tool: str, arguments: dict[str, Any]) -> str | None:
        """None when the call may go ahead, else why it is refused (shown to the agent). Only `pcsx2_connect` is
        refused; fails open when no copies are set up."""
        if tool != f"{SERVER_PREFIX}pcsx2_connect":
            return None
        try:
            self.registry.copies()
        except (ClaimError, ConfigError):
            return None  # no managed copies on this machine: nothing to protect, and no claims to check against
        ports: dict[str, int] = {}
        for key in _PORTS:
            value = arguments.get(key)
            if isinstance(value, bool) or not isinstance(value, int | float | str) or not str(value).isdigit():
                return (
                    f"Blocked: pcsx2_connect needs both `debug_port` and `pine_port` (`{key}` is missing), of your "
                    "claimed copy, in every mode. The server's defaults belong to whichever copy it was started for, "
                    "and a connect moves the connection of every agent of this session. Run `uv run --project python "
                    "coney-tools pcsx2 claim --agent <your id>` (it returns your claim again if you hold one), connect "
                    'with the `debug-port` and `port` (PINE) it prints and `mode: "debug"`, and check `pcsx2_status` '
                    "before the first write."
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
            self.registry.require_ports(names, debug_port=ports["debug_port"], pine_port=ports["pine_port"])
        except ClaimError as error:
            return f"Blocked: {error}."
        return None


def default_guard(start: Path) -> Guard:
    """The guard for this machine: the registry of copies and claims (pcsx2_claims.default_registry, from the
    checkout at `start`) and the state folder `mcp/` beside the claims."""
    registry = pcsx2_claims.default_registry(start)
    return Guard(registry, registry.claims_dir / "mcp")


def log_failure(start: Path, error: BaseException) -> None:
    """Note one line about a failure of the hook itself in `guard.log` (the hook lets the call through, so without
    this a broken guard would be invisible). Best effort: a failure here is dropped."""
    try:
        folder = pcsx2_claims.default_registry(start).claims_dir / "mcp"
        folder.mkdir(parents=True, exist_ok=True)
        stamp = time.strftime("%Y-%m-%dT%H:%M:%S")
        with (folder / LOG_FILE).open("a", encoding="utf-8") as log:
            log.write(f"{stamp} {type(error).__name__}: {error}\n")
    except Exception:
        return


def handle(payload: dict[str, Any], guard: Guard) -> str | None:
    """What the hook does with one payload of Claude Code: a shell command that runs a claiming `coney-tools pcsx2 ...
    --agent ID` binds the caller to ID (nothing is refused), and a `pcsx2_connect` of the pcsx2 MCP server is decided.
    Returns the reason to refuse, or None. A payload with no session is let through."""
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
