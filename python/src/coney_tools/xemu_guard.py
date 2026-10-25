# SPDX-License-Identifier: GPL-3.0-or-later
"""The decisions of the hook that keeps an agent off an xemu copy it has not claimed (`.claude/hooks/xemu_guard.py`,
docs/guides/agents.md#xemu-guard).

The `xemu` MCP server already connects each caller only to the copy its `agent` id holds (coney_tools.xemu_mcp), so
what is left to check is the id itself: every agent of a session shares the one server, and an `agent` naming another
agent's id would act on that agent's game. The hook therefore refuses an `mcp__xemu__xemu_*` call unless its `agent` is
an id the caller used in one of its own claiming shell commands (`coney-tools xemu claim | launch | read | record-run
--agent ID`), and that id holds a claim. The ids are learned from the shell commands exactly as the PCSX2 guard
learns them (coney_tools.pcsx2_guard.Bindings), in a folder of their own beside the xemu claims.

Like the PCSX2 guard it stops mistakes, not impersonation (a caller that types another agent's id in a claim command
goes by it), and it fails open where it cannot work: no xemu copies set up, no session id, anything unexpected (a line
then goes to `guard.log` beside the bindings).
"""

from __future__ import annotations

from pathlib import Path
from typing import Any

from coney_tools import pcsx2_guard, xemu_claims
from coney_tools.config import ConfigError
from coney_tools.pcsx2_claims import ClaimError
from coney_tools.pcsx2_guard import Bindings, agents_in, command_pattern

#: The prefix of the `xemu` MCP server's tools.
SERVER_PREFIX = "mcp__xemu__"
#: The `coney-tools xemu` commands that run under the caller's own claim, so their `--agent` names the caller.
CLAIMING_COMMANDS = ("claim", "launch", "read", "record-run")
_XEMU_COMMAND = command_pattern("xemu", CLAIMING_COMMANDS)

_CLAIM_FIRST = (
    'Pass `agent: "<your id>"` on every xemu_* call: the id of `uv run --project python coney-tools xemu claim --agent '
    "<your id>`, run in a shell first so this guard learns it (it hands your claim back if you hold one)."
)


def ids_in(command: str) -> list[str]:
    """The agent ids a shell command passes to a claiming `coney-tools xemu ...` command, in order."""
    return agents_in(command, _XEMU_COMMAND)


class Guard(Bindings):
    """The bindings of the callers (below `folder`) and the decision, over the xemu claims."""

    def __init__(self, registry: xemu_claims.Registry, folder: Path) -> None:
        """`folder` holds the callers' bindings."""
        super().__init__(folder)
        self.registry = registry

    def decide(self, session: str, agent_id: str, tool: str, arguments: dict[str, Any]) -> str | None:
        """None when the call may go ahead, else why it is refused."""
        if not tool.startswith(SERVER_PREFIX):
            return None
        try:
            copies = self.registry.copies()
        except (ClaimError, ConfigError):
            return None  # no xemu copies on this machine: nothing to protect
        agent = arguments.get("agent")
        if not isinstance(agent, str) or not agent:
            return f"Blocked: {tool.removeprefix(SERVER_PREFIX)} needs `agent`. {_CLAIM_FIRST}"
        names = self.bound(session, agent_id)
        if agent not in names:
            return (
                f"Blocked: `agent` is {agent!r}, which is not an id you claimed an xemu copy with"
                + (f" (you claimed {', '.join(sorted(names))})" if names else "")
                + f". {_CLAIM_FIRST} Passing another agent's id would act on its game."
            )
        self._renew(self._binding(session, agent_id, agent))
        # The claim files only: whether the claim still lives is the server's to check, and a probe of each copy's
        # ports would slow every call. (A stale claim's call is refused by the server, naming it.)
        claims = (self.registry.read(copy.name) for copy in copies)
        if not any(claim is not None and claim.agent == agent for claim in claims):
            return f"Blocked: {agent} holds no xemu claim. {_CLAIM_FIRST}"
        return None


def default_guard(start: Path) -> Guard:
    """The guard of this machine: the xemu registry (from the checkout at `start`) and `mcp/` beside its claims."""
    registry = xemu_claims.default_registry(start)
    return Guard(registry, registry.claims_dir / "mcp")


def log_failure(start: Path, error: BaseException) -> bool:
    """One line about a failure of the hook itself in `guard.log` beside the bindings; whether it was written."""
    try:
        folder = xemu_claims.default_registry(start).claims_dir / "mcp"
        folder.mkdir(parents=True, exist_ok=True)
        pcsx2_guard.append_log(folder / pcsx2_guard.LOG_FILE, error)
    except Exception:
        return False
    return True


def handle(payload: dict[str, Any], guard: Guard) -> str | None:
    """What the hook does with one payload: a shell command that runs a claiming `coney-tools xemu ... --agent ID`
    binds the caller to ID (nothing is refused), and an xemu MCP call is decided. A payload with no session passes."""
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
        names = ids_in(command) if isinstance(command, str) else []
        if names:
            guard.bind(session, agent_id, names)
        return None
    return guard.decide(session, agent_id, tool, arguments)
