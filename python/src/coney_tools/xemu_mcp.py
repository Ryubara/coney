# SPDX-License-Identifier: GPL-3.0-or-later
"""The `xemu` MCP server: read and drive the Xbox build running in a claimed xemu copy
(docs/guides/coney-tools-xemu.md#mcp). `.mcp.json` starts it through `coney-tools mcp serve xemu`.

**Only your own copy.** Every tool takes `agent`, the id the caller claimed a copy with (`coney-tools xemu claim
--agent <id>`), and the server connects only to the copy that id holds now, read from the claims on every call: there
are no ports to pass, so no call can reach another agent's game, and a connection whose claim was released or taken
over is closed. One server serves every agent of a Claude Code session, each with a connection of its own; the hook
(`.claude/hooks/xemu_guard.py`) refuses an `agent` that is not an id the caller claimed with in its own shell commands.

**What the tools do.** Over the copy's GDB stub: memory, registers, breakpoints and watchpoints, pause, continue and
step; over its QEMU monitor: the run state, the version, screenshots and snapshots. A read of a running game stops it
only for the read (coney_tools.xemu_debug). `READ_ONLY_TOOLS` are the ones that change nothing in the game: the
reviewer gets those only (`.claude/agents/reviewer.md`, which a test holds to this list).

The protocol is MCP's stdio transport: one JSON-RPC 2.0 message per line on stdin and stdout. Nothing else may reach
stdout; errors of a tool are its answer (`isError`), never a crash of the server.
"""

from __future__ import annotations

import json
import os
import struct
import sys
import time
from collections.abc import Callable, Mapping
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path
from typing import IO, Any

from coney_tools import xemu_claims
from coney_tools.config import ConfigError, find_repo_root, load_config, local_config_file, main_checkout
from coney_tools.config import workspace_folder as _workspace_folder
from coney_tools.gdb_remote import POINT_KINDS, Stop
from coney_tools.wad import refuse_inside_repo
from coney_tools.xemu_debug import XemuSession

SERVER_NAME = "coney-xemu"
PROTOCOL_VERSION = "2025-06-18"
#: The most bytes one read returns (a read is for numbers, never a dump of the game's memory).
MAX_READ = 4096
#: The longest wait of `xemu_continue` for a breakpoint, in seconds.
MAX_WAIT = 60.0
#: A connection unused for this many minutes is closed at the next call (`XEMU_IDLE_MINUTES` overrides it).
IDLE_MINUTES = 30.0
#: The value types `xemu_read_memory` decodes and `xemu_write_memory` encodes (little-endian, as the x86 guest).
VALUE_TYPES: dict[str, str] = {"u8": "<B", "u16": "<H", "u32": "<I", "i32": "<i", "f32": "<f"}

_AGENT = {"type": "string", "description": "your id, the one you claimed the copy with (`coney-tools xemu claim`)"}
_ADDRESS = {"type": ["string", "integer"], "description": "a guest virtual address, the XBE's (0x0032dd40)"}


@dataclass(frozen=True)
class Tool:
    """One tool: its name, what it does, its arguments (JSON Schema properties, the required ones) and its handler."""

    name: str
    description: str
    properties: Mapping[str, object]
    required: tuple[str, ...]
    handler: Callable[[Server, XemuSession, dict[str, Any]], object]

    def schema(self) -> dict[str, object]:
        """The tool as `tools/list` lists it."""
        return {
            "name": self.name,
            "description": self.description,
            "inputSchema": {
                "type": "object",
                "properties": {"agent": _AGENT, **self.properties},
                "required": ["agent", *self.required],
            },
        }


def parse_address(value: object) -> int:
    """An address from a tool argument: an integer, or a string in hex (`0x...`) or decimal."""
    if isinstance(value, bool):
        raise ConfigError(f"address {value!r}: not a number")
    if isinstance(value, int):
        number = value
    elif isinstance(value, str):
        try:
            number = int(value.strip(), 0)
        except ValueError as error:
            raise ConfigError(f"address {value!r}: write it as 0x... or a decimal number") from error
    else:
        raise ConfigError(f"address {value!r}: not a number")
    if not 0 <= number <= 0xFFFFFFFF:
        raise ConfigError(f"address 0x{number:x}: the guest is 32-bit")
    return number


def _count(arguments: Mapping[str, Any], key: str, default: int, least: int, most: int) -> int:
    """A whole-number argument within bounds."""
    value = arguments.get(key, default)
    if isinstance(value, bool) or not isinstance(value, int) or not least <= value <= most:
        raise ConfigError(f"{key} must be a whole number from {least} to {most}, got {value!r}")
    return value


def _stop(stop: Stop | None) -> str:
    """A stop reply in words."""
    if stop is None:
        return "running"
    if stop.watch is not None:
        return f"stopped by a {stop.watch[0]} at 0x{stop.watch[1]:08x} ({stop.text})"
    return {5: "stopped (trap: a breakpoint or a step)", 2: "paused"}.get(stop.signal, f"stopped ({stop.text})")


# --- the tools ------------------------------------------------------------------------------------------------------


def _status(server: Server, session: XemuSession, arguments: dict[str, Any]) -> object:
    return session.status()


def _read_memory(server: Server, session: XemuSession, arguments: dict[str, Any]) -> object:
    address = parse_address(arguments.get("address"))
    kind = arguments.get("type", "hex")
    if kind != "hex" and kind not in VALUE_TYPES:
        raise ConfigError(f"type {kind!r}: hex or one of {', '.join(VALUE_TYPES)}")
    size = struct.calcsize(VALUE_TYPES[kind]) if kind != "hex" else 1
    count = _count(arguments, "count", 1 if kind != "hex" else 16, 1, MAX_READ // size)
    data = session.read(address, size * count)
    if kind == "hex":
        rows = [f"0x{address + i:08x}: {data[i : i + 16].hex(' ')}" for i in range(0, len(data), 16)]
        return "\n".join(rows)
    values = [v for (v,) in struct.iter_unpack(VALUE_TYPES[kind], data)]
    return {"address": f"0x{address:08x}", "type": kind, "values": values}


def _write_memory(server: Server, session: XemuSession, arguments: dict[str, Any]) -> object:
    address = parse_address(arguments.get("address"))
    if "hex" in arguments:
        try:
            data = bytes.fromhex(str(arguments["hex"]))
        except ValueError as error:
            raise ConfigError(f"hex {arguments['hex']!r}: not hex digits") from error
    else:
        kind = arguments.get("type", "u32")
        if kind not in VALUE_TYPES or "value" not in arguments:
            raise ConfigError(f"give hex, or value with a type ({', '.join(VALUE_TYPES)})")
        try:
            data = struct.pack(VALUE_TYPES[kind], arguments["value"])
        except struct.error as error:
            raise ConfigError(f"value {arguments['value']!r} is not a {kind}: {error}") from error
    if not 1 <= len(data) <= MAX_READ:
        raise ConfigError(f"write 1 to {MAX_READ} bytes, not {len(data)}")
    session.write(address, data)
    return f"wrote {len(data)} byte(s) at 0x{address:08x}"


def _read_registers(server: Server, session: XemuSession, arguments: dict[str, Any]) -> object:
    return {name: f"0x{value:08x}" for name, value in session.registers().items()}


def _write_register(server: Server, session: XemuSession, arguments: dict[str, Any]) -> object:
    name = str(arguments.get("name", ""))
    value = parse_address(arguments.get("value"))
    session.write_register(name, value)
    return f"{name} = 0x{value:08x}"


def _point(arguments: dict[str, Any]) -> tuple[str, int, int]:
    kind = str(arguments.get("kind", "break"))
    if kind not in POINT_KINDS:
        raise ConfigError(f"kind {kind!r}: one of {', '.join(POINT_KINDS)}")
    return kind, parse_address(arguments.get("address")), _count(arguments, "length", 1, 1, 8)


def _set_breakpoint(server: Server, session: XemuSession, arguments: dict[str, Any]) -> object:
    kind, address, length = _point(arguments)
    session.set_point(kind, address, length)
    return f"set a {kind} point at 0x{address:08x} ({length} byte(s)); xemu_continue with wait_seconds to catch it"


def _remove_breakpoint(server: Server, session: XemuSession, arguments: dict[str, Any]) -> object:
    kind, address, length = _point(arguments)
    session.remove_point(kind, address, length)
    return f"removed the {kind} point at 0x{address:08x}"


def _pause(server: Server, session: XemuSession, arguments: dict[str, Any]) -> object:
    return _stop(session.pause())


def _continue(server: Server, session: XemuSession, arguments: dict[str, Any]) -> object:
    wait = arguments.get("wait_seconds", 0)
    if isinstance(wait, bool) or not isinstance(wait, int | float) or not 0 <= wait <= MAX_WAIT:
        raise ConfigError(f"wait_seconds must be from 0 to {MAX_WAIT:g}")
    stop = session.resume(float(wait))
    if stop is None:
        return "running" + (f" (no stop within {wait:g} s)" if wait else "")
    eip = session.registers()["eip"]
    return f"{_stop(stop)}, eip 0x{eip:08x}"


def _step(server: Server, session: XemuSession, arguments: dict[str, Any]) -> object:
    stop = session.step(_count(arguments, "count", 1, 1, 1000))
    return f"{_stop(stop)}, eip 0x{session.registers()['eip']:08x}"


def _screenshot(server: Server, session: XemuSession, arguments: dict[str, Any]) -> object:
    out = Path(arguments["out"]) if arguments.get("out") else server.capture_path(session.copy.name)
    refuse_inside_repo(out)
    width, height, how = session.screenshot(out)
    return f"{out}: {width} x {height} ({how}); a picture of the game: keep it in scratch, never commit it"


def _list_snapshots(server: Server, session: XemuSession, arguments: dict[str, Any]) -> object:
    return session.snapshots()


def _save_snapshot(server: Server, session: XemuSession, arguments: dict[str, Any]) -> object:
    session.save_snapshot(str(arguments.get("name", "")))
    return f"saved snapshot {arguments['name']} in {session.copy.name}'s disk image"


def _load_snapshot(server: Server, session: XemuSession, arguments: dict[str, Any]) -> object:
    session.load_snapshot(str(arguments.get("name", "")))
    return f"loaded snapshot {arguments['name']}"


_POINT_ARGS = {
    "address": _ADDRESS,
    "kind": {"type": "string", "enum": list(POINT_KINDS), "description": "break (default), hardware, or a watchpoint"},
    "length": {"type": "integer", "description": "bytes a watchpoint covers: 1, 2, 4 or 8 (default 1)"},
}

TOOLS: tuple[Tool, ...] = (
    Tool(
        "xemu_status",
        "Your copy: ports, running or stopped, xemu's version, why it stopped and where.",
        {},
        (),
        _status,
    ),
    Tool(
        "xemu_read_memory",
        "Read guest memory at an XBE virtual address: hex rows, or count values of a type. A running game is stopped "
        "only for the read.",
        {
            "address": _ADDRESS,
            "type": {"type": "string", "enum": ["hex", *VALUE_TYPES], "description": "hex (default) or a value type"},
            "count": {"type": "integer", "description": f"bytes (hex) or values to read; at most {MAX_READ} bytes"},
        },
        ("address",),
        _read_memory,
    ),
    Tool(
        "xemu_write_memory",
        "Write guest memory: `hex` bytes, or one `value` of a `type`.",
        {
            "address": _ADDRESS,
            "hex": {"type": "string", "description": "the bytes as hex digits"},
            "value": {"type": "number", "description": "a value to write with `type`"},
            "type": {"type": "string", "enum": list(VALUE_TYPES), "description": "the value's type (default u32)"},
        },
        ("address",),
        _write_memory,
    ),
    Tool("xemu_read_registers", "The i386 core registers (eax .. gs).", {}, (), _read_registers),
    Tool(
        "xemu_write_register",
        "Set an i386 core register; pause first.",
        {"name": {"type": "string", "description": "eax, ecx, ... eip, eflags"}, "value": _ADDRESS},
        ("name", "value"),
        _write_register,
    ),
    Tool("xemu_set_breakpoint", "Set a breakpoint or a watchpoint.", _POINT_ARGS, ("address",), _set_breakpoint),
    Tool("xemu_remove_breakpoint", "Remove a point you set.", _POINT_ARGS, ("address",), _remove_breakpoint),
    Tool("xemu_pause", "Pause the game.", {}, (), _pause),
    Tool(
        "xemu_continue",
        "Let the game run; with wait_seconds, wait that long for a breakpoint or watchpoint and say where it stopped.",
        {"wait_seconds": {"type": "number", "description": f"0 (default) to {MAX_WAIT:g}"}},
        (),
        _continue,
    ),
    Tool(
        "xemu_step",
        "Run one instruction (or count) and stop.",
        {"count": {"type": "integer", "description": "1 to 1000"}},
        (),
        _step,
    ),
    Tool(
        "xemu_screenshot",
        "Write the screen as a PNG (default: the shared scratch's xemu/ folder; never inside the repository).",
        {"out": {"type": "string", "description": "the PNG's path, outside the repository"}},
        (),
        _screenshot,
    ),
    Tool("xemu_list_snapshots", "The snapshots (save states) in your copy's disk image.", {}, (), _list_snapshots),
    Tool(
        "xemu_save_snapshot",
        "Save the machine as a snapshot in your copy's disk image.",
        {"name": {"type": "string", "description": "letters, digits, '.', '_', '-'"}},
        ("name",),
        _save_snapshot,
    ),
    Tool(
        "xemu_load_snapshot",
        "Load a snapshot of your copy's disk image.",
        {"name": {"type": "string", "description": "a name xemu_list_snapshots shows"}},
        ("name",),
        _load_snapshot,
    ),
)

#: The tools that change nothing in the game: the reviewer's (a read stops a running game only for the read).
READ_ONLY_TOOLS: frozenset[str] = frozenset(
    {"xemu_status", "xemu_read_memory", "xemu_read_registers", "xemu_screenshot", "xemu_list_snapshots"}
)
#: The tool that closes a caller's connection; it needs no session.
DISCONNECT = "xemu_disconnect"


@dataclass
class _Held:
    """A caller's connection: the copy, the claim's time (a new claim is a new connection) and when it was used."""

    copy: str
    claimed: str
    session: XemuSession
    used: float


class Server:
    """The server's state: one connection per agent id, each to the copy that id holds."""

    def __init__(
        self,
        registry: Callable[[], xemu_claims.Registry],
        session_factory: Callable[..., XemuSession] = XemuSession,
        scratch: Path | None = None,
        idle_minutes: float = IDLE_MINUTES,
        clock: Callable[[], float] = time.monotonic,
    ) -> None:
        """`registry` makes the claims registry (read anew on each call); `scratch` is where screenshots go."""
        self.registry = registry
        self.session_factory = session_factory
        self.scratch = scratch
        self.idle = idle_minutes * 60
        self.clock = clock
        self.held: dict[str, _Held] = {}

    def capture_path(self, copy: str) -> Path:
        """Where a screenshot goes when the call names no file: `xemu/<copy>-<time>.png` in the shared scratch."""
        if self.scratch is None:
            raise ConfigError("no scratch folder is known: pass `out`, a path outside the repository")
        return self.scratch / "xemu" / f"{copy}-{datetime.now(UTC):%Y%m%d-%H%M%S}.png"

    def _drop(self, agent: str) -> None:
        held = self.held.pop(agent, None)
        if held is not None:
            held.session.close()

    def session(self, agent: str) -> XemuSession:
        """The caller's connection to the copy it holds now, made or remade as the claims say. A connection whose
        claim is gone or changed is closed, and so is another caller's connection to the copy (it no longer holds
        it, and the stub takes one debugger at a time)."""
        now = self.clock()
        for name, held in list(self.held.items()):
            if now - held.used > self.idle:
                self._drop(name)
        claim, copy = self.registry().require_claim(agent)
        mine = self.held.get(agent)
        if mine is not None and (mine.copy, mine.claimed) != (claim.copy, claim.time):
            self._drop(agent)
            mine = None
        if mine is None:
            for name, other in list(self.held.items()):
                if other.copy == claim.copy:
                    self._drop(name)
            mine = _Held(claim.copy, claim.time, self.session_factory(copy), now)
            self.held[agent] = mine
        mine.used = now
        return mine.session

    def call(self, name: str, arguments: dict[str, Any]) -> tuple[str, bool]:
        """Run a tool; its answer as text and whether it is an error."""
        agent = arguments.get("agent")
        if not isinstance(agent, str) or not agent:
            return (
                "pass `agent`: your id, the one you claimed a copy with (`coney-tools xemu claim --agent <id>`)",
                True,
            )
        if name == DISCONNECT:
            self._drop(agent)
            return f"closed {agent}'s connection", False
        tool = next((t for t in TOOLS if t.name == name), None)
        if tool is None:
            return f"no tool {name!r}", True
        try:
            session = self.session(agent)
            result = tool.handler(self, session, arguments)
        except ConfigError as error:
            if agent in self.held and self.held[agent].session.broken:
                self._drop(agent)
            return f"{name}: {error}", True
        except (OSError, ValueError, KeyError) as error:
            self._drop(agent)
            return f"{name}: {type(error).__name__}: {error}", True
        return (result if isinstance(result, str) else json.dumps(result, indent=1)), False

    def close(self) -> None:
        """Close every connection."""
        for name in list(self.held):
            self._drop(name)

    # -- the protocol

    def tools(self) -> list[dict[str, object]]:
        """What `tools/list` answers."""
        disconnect = Tool(
            DISCONNECT, "Close your connection (the game keeps running or paused as it is).", {}, (), _status
        )
        return [tool.schema() for tool in (*TOOLS, disconnect)]

    def handle(self, message: Mapping[str, Any]) -> dict[str, object] | None:
        """The answer to one JSON-RPC message, or None for a notification."""
        method = message.get("method")
        ident = message.get("id")
        if ident is None:
            return None
        params = message.get("params") or {}
        if method == "initialize":
            result: object = {
                "protocolVersion": params.get("protocolVersion", PROTOCOL_VERSION),
                "capabilities": {"tools": {"listChanged": False}},
                "serverInfo": {"name": SERVER_NAME, "version": "1"},
                "instructions": "Claim a copy first (`coney-tools xemu claim --agent <id>`) and pass that id as "
                "`agent` on every call. Guest addresses are the XBE's. docs/guides/coney-tools-xemu.md",
            }
        elif method == "ping":
            result = {}
        elif method == "tools/list":
            result = {"tools": self.tools()}
        elif method == "tools/call":
            arguments = params.get("arguments") or {}
            if not isinstance(arguments, dict):
                arguments = {}
            text, failed = self.call(str(params.get("name", "")), arguments)
            result = {"content": [{"type": "text", "text": text}], "isError": failed}
        else:
            return {"jsonrpc": "2.0", "id": ident, "error": {"code": -32601, "message": f"no method {method!r}"}}
        return {"jsonrpc": "2.0", "id": ident, "result": result}


def serve(stdin: IO[str], stdout: IO[str], server: Server) -> int:
    """Answer messages from `stdin` on `stdout`, one JSON object per line, until stdin ends."""
    try:
        for line in stdin:
            if not line.strip():
                continue
            try:
                message = json.loads(line)
            except ValueError:
                answer: dict[str, object] | None = {
                    "jsonrpc": "2.0",
                    "id": None,
                    "error": {"code": -32700, "message": "not JSON"},
                }
            else:
                answer = server.handle(message) if isinstance(message, dict) else None
            if answer is not None:
                stdout.write(json.dumps(answer) + "\n")
                stdout.flush()
    finally:
        server.close()
    return 0


def _scratch(start: Path) -> Path:
    """The shared scratch folder (`scratch_dir`, else the main checkout's `../../scratch`)."""
    root = find_repo_root(start)
    paths = load_config(root).paths if local_config_file(root) else {}
    return paths.get("scratch_dir") or _workspace_folder(main_checkout(root), "scratch")


def main() -> int:
    """Run the server for the checkout in `CLAUDE_PROJECT_DIR` (else the current folder) over stdin and stdout."""
    start = Path(os.environ.get("CLAUDE_PROJECT_DIR") or Path.cwd())
    idle = float(os.environ.get("XEMU_IDLE_MINUTES") or IDLE_MINUTES)
    server = Server(lambda: xemu_claims.default_registry(start), scratch=_scratch(start), idle_minutes=idle)
    return serve(sys.stdin, sys.stdout, server)


if __name__ == "__main__":
    sys.exit(main())
