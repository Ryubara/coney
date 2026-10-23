# SPDX-License-Identifier: GPL-3.0-or-later
"""A client for the debug server of the PCSX2-MCP fork, and the header of PCSX2's input recordings (`.p2m2`).

The fork (docs/guides/research-tools.md#driving-pcsx2) builds a JSON server into PCSX2 on the port its ini names
(`[EmuCore]` `EnableDebugServer`, `DebugServerPort`). The protocol is newline-delimited JSON over TCP: a request is one
object `{"cmd": NAME, ...}` and a line, the answer one object and a line with `"ok"` true, or false and an `"error"`.
Unlike PINE it serves several clients at once (a thread each), so `pcsx2 record` can use it beside an agent's MCP
server. Only the commands Coney's tools need are wrapped here: `status`, `emulation` and `input_recording`.

`input_recording` drives PCSX2's own input recording, as its "Input Recording" menu does: `play` loads the recording's
start savestate (`<file>_SaveState.p2s` beside it) or resets the game, then replays every frame's pad state; at its last
frame PCSX2 pauses and the fork stops the replay there (the file is unchanged), and `status` then says
`replay_ended`. A path is relative to PCSX2's data folder (a portable copy's own folder), so a recording is copied
into the copy's `recordings/` before it is played.
"""

from __future__ import annotations

import json
import socket
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING, Any

if TYPE_CHECKING:
    from coney_tools.pcsx2_claims import Registry

#: Seconds for a command to answer; `play` and `new` load or save a state first (the fork allows them 30 s).
DEFAULT_TIMEOUT = 10.0
RECORDING_TIMEOUT = 40.0

#: The suffix PCSX2 gives a recording's start savestate, after the recording's own name (`x.p2m2_SaveState.p2s`).
SAVESTATE_SUFFIX = "_SaveState.p2s"


class DebugServerError(Exception):
    """A debug server failure: nothing listening, a broken connection, or an answer with `"ok": false`."""


def savestate_of(recording: Path) -> Path:
    """The start savestate PCSX2 keeps beside `recording`."""
    return recording.with_name(recording.name + SAVESTATE_SUFFIX)


@dataclass(frozen=True)
class InputRecordingStatus:
    """What an `input_recording` answer says (the fork's field names, snake case kept)."""

    #: "off", "recording" or "replaying".
    mode: str
    #: PCSX2's emulation state ("running", "paused", ...) and frame counter when it answered.
    emulation: str
    frame: int
    path: str | None = None
    from_savestate: bool | None = None
    recording_frame: int | None = None
    total_frames: int | None = None
    #: The last replay started through the server reached its last frame and was closed there, unchanged.
    replay_ended: bool = False
    #: The recording a `stop` closed.
    stopped: str | None = None


def parse_input_recording(answer: dict[str, Any]) -> InputRecordingStatus:
    """The status in an `input_recording` answer. Raises DebugServerError for a refusal or a malformed answer."""
    _check(answer, "input_recording")
    try:
        origin = answer.get("from")
        return InputRecordingStatus(
            mode=str(answer["mode"]),
            emulation=str(answer.get("emulation", "")),
            frame=int(answer.get("frame", 0)),
            path=answer.get("path"),
            from_savestate=None if origin is None else origin == "savestate",
            recording_frame=_optional_int(answer.get("recording_frame")),
            total_frames=_optional_int(answer.get("total_frames")),
            replay_ended=bool(answer.get("replay_ended", False)),
            stopped=answer.get("stopped"),
        )
    except (KeyError, TypeError, ValueError) as error:
        raise DebugServerError(f"input_recording: malformed answer {answer!r}") from error


def _optional_int(value: Any) -> int | None:
    """`value` as an int, None when absent."""
    return None if value is None else int(value)


def _check(answer: dict[str, Any], command: str) -> None:
    """Raise DebugServerError when the answer is a refusal."""
    if not isinstance(answer, dict) or answer.get("ok") is not True:
        error = answer.get("error", "no error given") if isinstance(answer, dict) else repr(answer)
        raise DebugServerError(f"{command}: {error}")


class DebugClient:
    """A connection to one PCSX2's debug server on localhost."""

    def __init__(self, port: int, connect_timeout: float = 3.0) -> None:
        """Connect to the server on `port`. Raises DebugServerError when nothing answers."""
        self.port = port
        try:
            self.sock = socket.create_connection(("127.0.0.1", port), timeout=connect_timeout)
        except OSError as error:
            raise DebugServerError(f"no debug server on port {port} ({error}); is PCSX2 running a game?") from error
        self.buffer = b""

    @classmethod
    def claimed(cls, agent: str, copy: str | None = None, registry: Registry | None = None) -> DebugClient:
        """A client bound to the copy `agent` has claimed (`copy` names it when the agent holds several), the way to
        reach a debug server from your own script: it connects to the debug port of your claim only, and refuses a
        copy another agent holds with a ClaimError before it opens any connection. Raises DebugServerError when the
        copy's debug server is off or not answering."""
        from coney_tools import pcsx2_claims  # not at import: the claim layer is only needed here

        registry = registry or pcsx2_claims.default_registry()
        if copy is None:
            held = [claim.copy for claim in registry.held_by(agent)]
            if not held:
                raise pcsx2_claims.ClaimError(
                    f"{agent} holds no PCSX2 copy: run `coney-tools pcsx2 claim --agent {agent}` first"
                )
            if len(held) > 1:
                raise pcsx2_claims.ClaimError(f"{agent} holds {', '.join(held)}: name one (copy=...)")
            copy = held[0]
        port = registry.copy(copy).debug
        if port is None:
            raise DebugServerError(f"{copy}: the debug server is off in its PCSX2.ini (`pcsx2 setup-copies` sets it)")
        registry.require_ports([agent], debug_port=port)
        return cls(port)

    def close(self) -> None:
        """Close the connection."""
        self.sock.close()

    def __enter__(self) -> DebugClient:
        return self

    def __exit__(self, *_: object) -> None:
        self.close()

    def request(self, command: dict[str, Any], timeout: float = DEFAULT_TIMEOUT) -> dict[str, Any]:
        """Send one command and return its answer as a dict (a refusal too: `"ok"` is false). Raises
        DebugServerError for a broken connection, a timeout or an answer that is no JSON object."""
        self.sock.settimeout(timeout)
        try:
            self.sock.sendall(json.dumps(command).encode("utf-8") + b"\n")
            while b"\n" not in self.buffer:
                chunk = self.sock.recv(65536)
                if not chunk:
                    raise DebugServerError(f"{command.get('cmd')}: the debug server closed the connection")
                self.buffer += chunk
        except OSError as error:
            raise DebugServerError(f"{command.get('cmd')}: {error}") from error
        line, self.buffer = self.buffer.split(b"\n", 1)
        try:
            answer = json.loads(line)
        except ValueError as error:
            raise DebugServerError(f"{command.get('cmd')}: not JSON: {line[:200]!r}") from error
        if not isinstance(answer, dict):
            raise DebugServerError(f"{command.get('cmd')}: not a JSON object: {line[:200]!r}")
        return answer

    def emulation(self, action: str) -> str:
        """Pause or resume emulation as a whole (`action` pause, resume or status); returns the state after it."""
        answer = self.request({"cmd": "emulation", "action": action})
        _check(answer, "emulation")
        return str(answer.get("state", ""))

    def input_recording(self, action: str, path: str | None = None) -> InputRecordingStatus:
        """Run `input_recording` `action` (play, stop or status; play needs `path`, relative to PCSX2's data
        folder) and return the status it answers. Recording new files is left to the MCP server's tool."""
        command: dict[str, Any] = {"cmd": "input_recording", "action": action}
        if path is not None:
            command["path"] = path
        timeout = RECORDING_TIMEOUT if action == "play" else DEFAULT_TIMEOUT + 5
        return parse_input_recording(self.request(command, timeout))


@dataclass(frozen=True)
class P2m2Header:
    """The header of an input recording (`.p2m2`, file version 1)."""

    version: int
    emulator: str
    author: str
    game: str
    total_frames: int
    undo_count: int
    #: True when the recording starts by loading its savestate, False when it starts from a reset of the game.
    from_savestate: bool


#: The header's layout: version u8, emulator version char[50], author char[255], game char[255], total frames u32,
#: undo count u32, from-savestate bool (PCSX2's pcsx2/Recording/InputRecordingFile.h). 36 bytes a frame follow.
_P2M2_HEADER = struct.Struct("<B50s255s255sIIB")
P2M2_FRAME_BYTES = 36


def _text(raw: bytes) -> str:
    """A NUL-padded C string."""
    return raw.split(b"\0", 1)[0].decode("latin-1")


def parse_p2m2(data: bytes) -> P2m2Header:
    """The header of a recording's bytes. Raises ValueError for a file too short or of another version."""
    if len(data) < _P2M2_HEADER.size:
        raise ValueError(f"{len(data)} bytes: too short for an input recording's {_P2M2_HEADER.size}-byte header")
    version, emulator, author, game, frames, undo, savestate = _P2M2_HEADER.unpack_from(data)
    if version != 1:
        raise ValueError(f"input recording file version {version}; only version 1 is known")
    return P2m2Header(version, _text(emulator), _text(author), _text(game), frames, undo, savestate != 0)


def read_p2m2(path: Path) -> P2m2Header:
    """The header of the recording at `path`. Raises ValueError naming the file when it is not one."""
    with path.open("rb") as handle:
        data = handle.read(_P2M2_HEADER.size)
    try:
        return parse_p2m2(data)
    except ValueError as error:
        raise ValueError(f"{path}: {error}") from error
