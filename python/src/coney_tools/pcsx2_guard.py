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

**The server's `agent` argument.** A PCSX2-MCP server that keeps a connection per caller takes an optional `agent` on
every `pcsx2_*` call (docs/guides/agents.md#pcsx2-guard). The guard checks it where it is given: it must be an id the
caller claimed with (and, on `pcsx2_connect`, the id the copy is claimed as), so a caller cannot act in another agent's
name by mistake. `pcsx2_connect` **must** carry it: whatever server runs (a local-scope `pcsx2` entry, an older
checkout, the old fork), two agents that left it out would share the server's `default` connection and one's connect
would move the other's, so the hook refuses a connect without it (an older server ignores the extra key). Other calls
may leave it out: that is the server's to require (`PCSX2_REQUIRE_AGENT=1`). Against an older server, which has one
connection per process, another agent's `pcsx2_connect` to its own claim still moves this agent's connection, and
`mode: debug` leaves the previous PINE half.

**What it is not.** The ids come from the text of shell commands, so the guard stops mistakes, not impersonation: a
caller that runs `claim --agent <another agent's id>` (or only mentions it in a command) goes by that id, exactly as
`claim` itself hands that agent's claim back to anyone who asks.

The guard fails open where it cannot work (no PCSX2 copies set up, no session id in the payload, anything
unexpected), and writes one line to `guard.log` beside its state whenever it does so for a failure of its own (also an
unreadable payload; when the package cannot even load, the line goes to `coney-pcsx2-guard.log` in the system temp
folder). Not logged: a hook that the 10 second timeout kills (Claude Code shows that itself). State is a few small
files below the claims folder (`<claims dir>/mcp/`), one per caller and claim id, written whole by a rename, renewed
whenever the caller uses its binding and pruned after `BINDING_KEEP_SECONDS`; the log is cut to its newest half past
`LOG_MAX_BYTES`.
"""

from __future__ import annotations

import contextlib
import hashlib
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
#: The log is cut to its newest half once it is larger than this.
LOG_MAX_BYTES = 64 * 1024
#: A half-written binding file nobody finished (a crash between writing and renaming) is removed after this long.
TEMPORARY_KEEP_SECONDS = 3600.0
#: How often a rename that Windows refuses (the destination is open in another hook process) is tried.
REPLACE_ATTEMPTS = 6

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

    def _binding(self, session: str, agent_id: str, name: str | None = None) -> Path:
        """The file that holds one claim id one caller of a session goes by (`name`), or, without it, the prefix
        shared by all the caller's files. One file per id means a bind never reads and rewrites another's, so two
        hook processes binding at once lose nothing."""
        prefix = f"{_key(session)}.bind.{_key(agent_id)}."
        if name is None:
            return self.folder / prefix
        return self.folder / f"{prefix}{_key(name)}-{hashlib.sha1(name.encode('utf-8')).hexdigest()[:8]}.json"

    def bind(self, session: str, agent_id: str, names: Collection[str]) -> None:
        """Note that this caller goes by `names` (the ids of its claiming `pcsx2 ... --agent` commands) from now on,
        and drop the files nobody has renewed for BINDING_KEEP_SECONDS."""
        self.folder.mkdir(parents=True, exist_ok=True)
        for name in sorted(set(names)):
            path = self._binding(session, agent_id, name)
            if path.exists():
                self._renew(path)  # a caller that keeps claiming keeps its binding
                continue
            temporary = path.with_name(f"{path.name}.{os.getpid()}.{uuid.uuid4().hex[:6]}")
            temporary.write_text(json.dumps({"name": name}), encoding="utf-8")
            self._replace(temporary, path)
        self._prune()

    @staticmethod
    def _replace(temporary: Path, path: Path) -> None:
        """Rename `temporary` to `path`, trying again when Windows refuses because another hook process has `path`
        open (the `bound` of a parallel tool call). The content is the same whoever wins, so a `path` that appears
        meanwhile settles it. The temporary file never stays behind."""
        for attempt in range(REPLACE_ATTEMPTS):
            try:
                temporary.replace(path)
                return
            except PermissionError:
                if path.exists() or attempt == REPLACE_ATTEMPTS - 1:
                    break
                time.sleep(0.05 * (attempt + 1))
        with contextlib.suppress(OSError):
            temporary.unlink()
        if not path.exists():
            raise PermissionError(f"could not write {path.name}")

    @staticmethod
    def _renew(path: Path) -> None:
        """Mark a binding file as used now (best effort: it may be pruned or replaced at this very moment)."""
        try:
            os.utime(path, None)
        except OSError:
            return

    def _prune(self) -> None:
        """Remove binding files not renewed for BINDING_KEEP_SECONDS (a session's file is never read again once it
        ends) and half-written temporary ones left by a crash, which no glob of `bound` would ever match."""
        now = time.time()
        for path in self.folder.glob("*.bind.*"):
            keep = BINDING_KEEP_SECONDS if path.suffix == ".json" else TEMPORARY_KEEP_SECONDS
            try:
                if now - path.stat().st_mtime > keep:
                    path.unlink()
            except OSError:
                continue

    def bound(self, session: str, agent_id: str) -> set[str]:
        """The claim ids this caller goes by."""
        names: set[str] = set()
        for path in self.folder.glob(f"{self._binding(session, agent_id).name}*.json"):
            try:
                data = json.loads(path.read_text(encoding="utf-8"))
            except (OSError, ValueError):
                continue
            name = data.get("name") if isinstance(data, dict) else None
            if isinstance(name, str):
                names.add(name)
        return names

    # -- the decision

    def decide(self, session: str, agent_id: str, tool: str, arguments: dict[str, Any]) -> str | None:
        """None when the call may go ahead, else why it is refused (shown to the agent). A `pcsx2_*` call that names
        an `agent` (the connection name of the server's per-caller connections) must name an id the caller goes by;
        `pcsx2_connect` is refused unless its ports are those of a claim the caller holds. Fails open when no copies
        are set up."""
        name = tool.removeprefix(SERVER_PREFIX)
        if name == tool or not name.startswith("pcsx2_"):
            return None
        is_connect = name == "pcsx2_connect"
        agent = arguments.get("agent")
        if not is_connect and agent is None:
            return None
        try:
            self.registry.copies()
        except (ClaimError, ConfigError):
            return None  # no managed copies on this machine: nothing to protect, and no claims to check against
        names = self.bound(session, agent_id)
        if agent is not None and not isinstance(agent, str):
            return f"Blocked: `agent` must be a string, your claim id; it is {agent!r}. {_PASS_YOUR_ID}"
        if is_connect and not agent:
            # Whatever server runs: without a name two agents share its `default` connection and one moves the other's.
            return f"Blocked: pcsx2_connect needs `agent` (it names your connection). {_PASS_YOUR_ID}"
        if agent is not None and agent not in names:
            return (
                f"Blocked: `agent` is {agent!r}, which is not an id you claimed with"
                + (f" (you claimed {', '.join(sorted(names))})" if names else "")
                + f". {_PASS_YOUR_ID} Passing another agent's would act on its PCSX2."
            )
        if agent is not None:
            self._renew(self._binding(session, agent_id, agent))  # the binding is in use: it must not expire
        if not is_connect:
            return None
        ports: dict[str, int] = {}
        for key in _PORTS:
            number = _port(arguments.get(key))
            if number is None:
                what = "is missing" if arguments.get(key) is None else "is not a port number"
                return (
                    f"Blocked: pcsx2_connect needs both `debug_port` and `pine_port` (`{key}` {what}), of your "
                    "claimed copy, in every mode. The server's defaults belong to whichever copy it was started for, "
                    "and a connect moves the connection of every agent of this session. Run `uv run --project python "
                    "coney-tools pcsx2 claim --agent <your id>` (it returns your claim again if you hold one), connect "
                    'with the `debug-port` and `port` (PINE) it prints and `mode: "debug"`, and check `pcsx2_status` '
                    "before the first write."
                )
            ports[key] = number
        try:
            claim = self.registry.require_ports(names, debug_port=ports["debug_port"], pine_port=ports["pine_port"])
        except ClaimError as error:
            return f"Blocked: {error}."
        if agent != claim.agent:
            return (
                f"Blocked: name your connection after the claim you connect to: `agent` is {agent!r} but the copy "
                f"{claim.copy} is claimed as {claim.agent!r}."
            )
        return None


#: What a refusal about `agent` tells the agent to do.
_PASS_YOUR_ID = (
    'Pass `agent: "<your id>"` on every pcsx2_* call, the id of `uv run --project python coney-tools pcsx2 claim '
    "--agent <your id>`, run in a shell first so this guard learns it (it returns your claim again if you hold one): "
    "it names your connection."
)


def _port(value: object) -> int | None:
    """A TCP port from what a tool call carries (an integer, a whole float or a string of ASCII digits), else None.
    Strings like "\u00b2" pass `str.isdigit()` and then break `int()`, so the digits are checked as ASCII."""
    if isinstance(value, bool):
        return None
    number: int | None
    if isinstance(value, float):
        number = int(value) if value.is_integer() else None
    elif isinstance(value, int):
        number = value
    elif isinstance(value, str) and value.isascii() and value.isdigit() and len(value) <= 5:
        number = int(value)
    else:
        number = None
    return number if number is not None and 0 < number < 65536 else None


def default_guard(start: Path) -> Guard:
    """The guard for this machine: the registry of copies and claims (pcsx2_claims.default_registry, from the
    checkout at `start`) and the state folder `mcp/` beside the claims."""
    registry = pcsx2_claims.default_registry(start)
    return Guard(registry, registry.claims_dir / "mcp")


def log_failure(start: Path, error: BaseException) -> bool:
    """Note one line about a failure of the hook itself in `guard.log` (the hook lets the call through, so without
    this a broken guard would be invisible). Best effort: returns whether the line was written."""
    try:
        folder = pcsx2_claims.default_registry(start).claims_dir / "mcp"
        folder.mkdir(parents=True, exist_ok=True)
        append_log(folder / LOG_FILE, error)
    except Exception:
        return False
    return True


def append_log(path: Path, error: BaseException) -> None:
    """Append `<time> <ErrorType>: <message>` to the log at `path`; past LOG_MAX_BYTES keep only its newest half."""
    line = f"{time.strftime('%Y-%m-%dT%H:%M:%S')} {type(error).__name__}: {error}".replace("\n", " ") + "\n"
    with path.open("a", encoding="utf-8") as log:
        log.write(line)
    if path.stat().st_size > LOG_MAX_BYTES:
        kept: list[str] = []
        size = 0
        for old in reversed(path.read_text(encoding="utf-8", errors="replace").splitlines(keepends=True)):
            size += len(old.encode("utf-8"))
            if size > LOG_MAX_BYTES // 2:
                break
            kept.append(old)
        path.write_text("".join(reversed(kept)), encoding="utf-8")


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
