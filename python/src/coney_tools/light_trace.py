# SPDX-License-Identifier: GPL-3.0-or-later
"""The original's light selection, traced per call in PCSX2 (`coney-tools trace lights`).

`LightManager_SelectLights` (`0x0017de10`) chooses the lights of each atomic the game draws: a sector, the sky, a world
object, a human or a car (docs/research/lighting.md#select). This module watches it through the PCSX2-MCP fork's debug
server (coney_tools.debug_server): a breakpoint on its entry reads the call's inputs, a breakpoint on the caller's
return address reads the lights it chose, and the game runs on. It is the method of run r-20261030-light-selection
made into a tool (docs/guides/realtime-lighting.md#test-selection).

At the entry (the EE's calling convention: `distSq` in f12, then a0 to a3, t0 and t1): the manager `a0`, the
atomic's bounding sphere at `a1` (centre and radius, four floats), `flags` `a2` (bit 0: objects), `pointLights` `a3`,
`pulse` `t0`, the human `t1` (with its glow byte `+0x647`), the caller's return address `ra`, and the lists the
selection reads: A, B and C at `0x00715324`, `0x00715348` and `0x0071536c` (eight RpLight pointers and a count each)
and the manager's point lights `+0x20` (objects) and `+0x30` (world), each a vector whose begin and end are at `+0x4`
and `+0x8`. At the return: the upload list `0x00715300`, eight pointers and its count at `0x00715320`. Each light is
read as values, never as a pointer: its RenderWare type (RpLight byte `+0x01`), radius (`+0x14`), colour (`+0x18`, four
floats) and its frame's position (the frame at `+0x04`, its matrix's position at `+0x40`). All confirmed (code) at
`0x0017de10`; the five callers and their return addresses are the page's.

What it writes stays in scratch (it measures the game): the selections in the columns of Coney's lab dump
(`lighting-<n>-selections.csv`, docs/guides/realtime-lighting.md#lab-built), one row per call, and beside them the
inputs a replay needs (#1252): `<stem>.calls.csv` (the game's tick, the caller, the glow byte and each list's length)
and `<stem>.lists.csv` (every light of every list of every call). A run record keeps only counts and the files' hashes.
"""

from __future__ import annotations

import hashlib
import struct
import time
from collections import Counter
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from decimal import Decimal
from pathlib import Path
from typing import Protocol

from coney_tools.config import ConfigError
from coney_tools.debug_server import DebugClient, DebugServerError
from coney_tools.wad import refuse_inside_repo

#: `LightManager_SelectLights`, docs/research/lighting.md#select.
ENTRY = 0x0017DE10
#: Each caller's return address (its `jal` + 8) and the name a row's caller gets, docs/research/lighting.md#select.
CALLERS: Mapping[int, str] = {
    0x001746F8: "human",
    0x00411AA8: "sector",
    0x001803A4: "object",
    0x00172CE8: "car",
    0x0040D1A8: "background",
}
#: The upload list the selection fills: eight RpLight pointers, the count after them.
UPLOAD_LIST = 0x00715300
#: The lists the selection copies: A (the world's), B (objects), C (objects, with the pulse).
COPIED_LISTS: Mapping[str, int] = {"a": 0x00715324, "b": 0x00715348, "c": 0x0071536C}
#: The manager's point-light vectors, by their offset in the manager: objects `+0x20`, world `+0x30`.
POINT_LISTS: Mapping[str, int] = {"objects": 0x20, "world": 0x30}
#: Every list of a call's inputs, in the order the files give them.
LIST_NAMES: tuple[str, ...] = (*COPIED_LISTS, *POINT_LISTS)
#: The game's tick count, which numbers its frames (docs/guides/research-traces.md#hooks).
TICK = 0x005104F4
#: A human's glow byte; the glow is added when it is above 10 (docs/research/lighting.md#glow).
GLOW_BYTE = 0x647
#: The most point lights a vector may hold before it is taken for a bad read: the game culls to far fewer.
MAX_POINT_LIGHTS = 512
#: A list of eight pointers and its count.
_LIST = struct.Struct("<9I")
_SLOTS = 8
#: The debugger's register categories and the registers the selection's arguments are in.
_GPR, _FPR = 0, 2
_A0, _A1, _A2, _A3, _T0, _T1, _RA = 4, 5, 6, 7, 8, 9, 31
_F12 = 12
#: How often the trace asks whether the game has stopped at a breakpoint.
_POLL_SECONDS = 0.0005


class LightTraceError(ConfigError):
    """A read that cannot be a light list (a vector longer than any the game makes), or a stop nobody set."""


class Memory(Protocol):
    """Somewhere to read the game's memory from."""

    def read(self, address: int, length: int) -> bytes:
        """`length` bytes from `address`."""
        ...


class DebugMemory:
    """The game's memory through a debug server, read while the game is stopped."""

    def __init__(self, client: DebugClient) -> None:
        self.client = client

    def read(self, address: int, length: int) -> bytes:
        """`length` bytes from `address`."""
        return self.client.read_memory(address, length)


@dataclass(frozen=True)
class Light:
    """One light as the trace writes it: values only."""

    #: RenderWare's light type: 1 directional, 2 ambient, 0x80 point, 0x81 spot.
    type: int
    radius: float
    colour: tuple[float, float, float, float]
    #: Its frame's position, in RenderWare's axes.
    position: tuple[float, float, float]


@dataclass(frozen=True)
class Call:
    """One call of the selection: its inputs and the lights it chose, in order."""

    #: The caller's return address (a key of CALLERS, or another the page does not name).
    caller: int
    dist_sq: float
    #: The atomic's sphere: centre x, y, z and radius.
    sphere: tuple[float, float, float, float]
    flags: int
    point_lights: bool
    pulse: bool
    human: bool
    #: The human's glow byte, None for a call with no human.
    glow: int | None
    #: The game's tick count at the call.
    tick: int
    #: Each list of LIST_NAMES as the call found it.
    lists: Mapping[str, tuple[Light, ...]]
    chosen: tuple[Light, ...]


def _f32(data: bytes, offset: int = 0) -> float:
    """The float at `offset` of `data`."""
    return float(struct.unpack_from("<f", data, offset)[0])


def _u32(data: bytes, offset: int = 0) -> int:
    """The word at `offset` of `data`."""
    return int(struct.unpack_from("<I", data, offset)[0])


def read_light(memory: Memory, pointer: int) -> Light:
    """The RpLight at `pointer` as values: its type, radius, colour and its frame's position (0, 0, 0 with no frame)."""
    rp = memory.read(pointer, 0x28)
    frame = _u32(rp, 0x04)
    position = (0.0, 0.0, 0.0)
    if frame:
        at = memory.read(frame + 0x40, 12)
        position = (_f32(at, 0), _f32(at, 4), _f32(at, 8))
    colour = (_f32(rp, 0x18), _f32(rp, 0x1C), _f32(rp, 0x20), _f32(rp, 0x24))
    return Light(rp[1], _f32(rp, 0x14), colour, position)


def read_list(memory: Memory, address: int) -> tuple[int, ...]:
    """The pointers of the list of eight at `address` (its count after them, at most eight)."""
    words = _LIST.unpack(memory.read(address, _LIST.size))
    return tuple(words[: min(words[_SLOTS], _SLOTS)])


def read_vector(memory: Memory, header: int) -> tuple[int, ...]:
    """The pointers of the vector whose begin and end are at `header + 4` and `header + 8`. Raises LightTraceError for
    one that cannot be a list of lights."""
    ends = memory.read(header + 4, 8)
    begin, end = _u32(ends, 0), _u32(ends, 4)
    count, odd = divmod(end - begin, 4)
    if odd or not 0 <= count <= MAX_POINT_LIGHTS:
        raise LightTraceError(
            f"the point lights at 0x{header:08x}: {count} point lights from 0x{begin:08x} to 0x{end:08x}, "
            f"not a list of at most {MAX_POINT_LIGHTS}"
        )
    if count == 0:
        return ()
    data = memory.read(begin, count * 4)
    return tuple(_u32(data, 4 * i) for i in range(count))


class _Lights:
    """The lights read during one stop, each read once however many lists hold it."""

    def __init__(self, memory: Memory) -> None:
        self.memory = memory
        self.seen: dict[int, Light] = {}

    def of(self, pointers: Sequence[int]) -> tuple[Light, ...]:
        """The lights `pointers` name, in order."""
        out = []
        for pointer in pointers:
            if pointer not in self.seen:
                self.seen[pointer] = read_light(self.memory, pointer)
            out.append(self.seen[pointer])
        return tuple(out)


@dataclass(frozen=True)
class _Pending:
    """What the entry of a call read, waiting for its return."""

    caller: int
    dist_sq: float
    sphere: tuple[float, float, float, float]
    flags: int
    point_lights: bool
    pulse: bool
    human: bool
    glow: int | None
    tick: int
    lists: Mapping[str, tuple[Light, ...]]


def _at_entry(client: DebugClient, memory: Memory) -> _Pending:
    """The inputs of the call the game is stopped at the entry of."""
    gpr = [value & 0xFFFFFFFF for value in client.registers(_GPR)]
    fpr = client.registers(_FPR)
    dist_sq = float(struct.unpack("<f", struct.pack("<I", fpr[_F12] & 0xFFFFFFFF))[0])
    raw = memory.read(gpr[_A1], 16)
    sphere = (_f32(raw, 0), _f32(raw, 4), _f32(raw, 8), _f32(raw, 12))
    lights = _Lights(memory)
    lists = {name: lights.of(read_list(memory, address)) for name, address in COPIED_LISTS.items()}
    for name, offset in POINT_LISTS.items():
        lists[name] = lights.of(read_vector(memory, gpr[_A0] + offset))
    human = gpr[_T1]
    glow = memory.read(human + GLOW_BYTE, 1)[0] if human else None
    return _Pending(
        caller=gpr[_RA],
        dist_sq=dist_sq,
        sphere=sphere,
        flags=gpr[_A2],
        point_lights=gpr[_A3] != 0,
        pulse=gpr[_T0] != 0,
        human=human != 0,
        glow=glow,
        tick=_u32(memory.read(TICK, 4)),
        lists=lists,
    )


def _wait_for_stop(client: DebugClient, wait: float) -> int | None:
    """The pc where the game next stops, or None when it has not stopped within `wait` seconds."""
    deadline = time.monotonic() + wait
    while True:
        stopped, pc = client.cpu_status()
        if stopped:
            return pc
        if time.monotonic() >= deadline:
            return None
        time.sleep(_POLL_SECONDS)


def trace(client: DebugClient, calls: int, wait: float) -> list[Call]:
    """Trace `calls` calls of the selection in the game behind `client`, or fewer when `wait` seconds pass with no
    stop. Breakpoints go on the entry and on every return address, a caller the page does not name getting one as it
    is seen; they are all removed afterwards, whatever happens, and the game is left running. A return reached with
    no entry before it (the trace began during a call) is skipped."""
    memory = DebugMemory(client)
    placed: list[int] = []
    out: list[Call] = []
    pending: _Pending | None = None

    def place(address: int) -> None:
        client.set_breakpoint(address)
        placed.append(address)

    try:
        for address in (ENTRY, *CALLERS):
            place(address)
        while len(out) < calls:
            pc = _wait_for_stop(client, wait)
            if pc is None:
                break
            if pc == ENTRY:
                pending = _at_entry(client, memory)
                if pending.caller not in placed:
                    place(pending.caller)
            elif pc in placed:
                if pending is not None and pending.caller == pc:
                    chosen = _Lights(memory).of(read_list(memory, UPLOAD_LIST))
                    out.append(Call(**vars(pending), chosen=chosen))
                pending = None
            else:
                raise LightTraceError(f"the game stopped at 0x{pc:08x}, where the trace set no breakpoint")
            client.resume()
    finally:
        _clean_up(client, placed)
    return out


def _clean_up(client: DebugClient, placed: Sequence[int]) -> None:
    """Remove the trace's breakpoints and let the game run on if one of them holds it."""
    try:
        for address in placed:
            client.remove_breakpoint(address)
        stopped, pc = client.cpu_status()
        if stopped and pc in placed:
            client.resume()
    except DebugServerError as error:
        print(f"warning: the trace's breakpoints may still be set: {error}")


# --- the files --------------------------------------------------------------------------------------------------------


def number(value: float) -> str:
    """A float as Coney's lab dump writes it (C++'s `std::format("{}")`): the fewest digits that read back as the same
    32-bit float, written plain or in scientific notation, whichever is shorter (plain on a tie)."""
    if value == 0:
        return "0"
    single = struct.unpack("<f", struct.pack("<f", value))[0]
    scientific = repr(single)
    for digits in range(1, 10):
        text = f"{single:.{digits - 1}e}"
        if struct.unpack("<f", struct.pack("<f", float(text)))[0] == single:
            scientific = text
            break
    mantissa, _, exponent = scientific.partition("e")
    if "." in mantissa:
        mantissa = mantissa.rstrip("0").rstrip(".")
    scientific = f"{mantissa}e{exponent}"
    plain = format(Decimal(scientific).normalize(), "f")
    return plain if len(plain) <= len(scientific) else scientific


def _light_fields(light: Light) -> list[str]:
    """A light's type, radius, colour and position as text."""
    return [str(light.type), number(light.radius), *map(number, light.colour), *map(number, light.position)]


def selection_columns() -> list[str]:
    """The columns of Coney's lab dump, src/debug/lighting_lab.cpp `selectionColumns`."""
    columns = ["caller", "dist_sq", "sphere_x", "sphere_y", "sphere_z", "sphere_radius"]
    columns += ["flags", "point_lights", "pulse", "human", "count"]
    for slot in range(_SLOTS):
        columns += [f"light{slot}_{f}" for f in ("type", "radius", "r", "g", "b", "a", "x", "y", "z")]
    return columns


def selections_csv(calls: Sequence[Call]) -> str:
    """One row per call in the columns of Coney's lab dump, the calls in order."""
    lines = [",".join(selection_columns())]
    for call in calls:
        row = [f"0x{call.caller:08x}", number(call.dist_sq), *map(number, call.sphere), str(call.flags)]
        row += [str(int(call.point_lights)), str(int(call.pulse)), str(int(call.human)), str(len(call.chosen))]
        for slot in range(_SLOTS):
            row += _light_fields(call.chosen[slot]) if slot < len(call.chosen) else [""] * 9
        lines.append(",".join(row))
    return "\n".join(lines) + "\n"


def calls_csv(calls: Sequence[Call]) -> str:
    """Per call: its number (the selections' row less one), tick, caller, glow byte and the length of each list."""
    lines = [",".join(["call", "tick", "caller", "glow", *LIST_NAMES])]
    for index, call in enumerate(calls):
        glow = "" if call.glow is None else str(call.glow)
        lengths = [str(len(call.lists[name])) for name in LIST_NAMES]
        lines.append(",".join([str(index), str(call.tick), f"0x{call.caller:08x}", glow, *lengths]))
    return "\n".join(lines) + "\n"


def lists_csv(calls: Sequence[Call]) -> str:
    """Every light of every list of every call: the call's number, the list, its slot and the light's values."""
    lines = ["call,list,slot,type,radius,r,g,b,a,x,y,z"]
    for index, call in enumerate(calls):
        for name in LIST_NAMES:
            for slot, light in enumerate(call.lists[name]):
                lines.append(",".join([str(index), name, str(slot), *_light_fields(light)]))
    return "\n".join(lines) + "\n"


def facts(calls: Sequence[Call]) -> dict[str, int]:
    """What a run record keeps of a trace: counts only (docs/guides/research-evidence.md#run-records)."""
    by_caller = Counter(CALLERS.get(call.caller, "other") for call in calls)
    kinds = Counter(light.type for call in calls for light in call.chosen)
    out = {"calls": len(calls), "frames": len({call.tick for call in calls})}
    out |= {f"calls_{name}": by_caller[name] for name in (*CALLERS.values(), "other")}
    out |= {
        "chosen_directional": kinds[1],
        "chosen_ambient": kinds[2],
        "chosen_point": kinds[0x80],
        "chosen_spot": kinds[0x81] + kinds[0x82],
        "glow_calls": sum(1 for call in calls if call.glow is not None and call.glow > 10),
        "max_count": max((len(call.chosen) for call in calls), default=0),
    }
    return out


def write_files(calls: Sequence[Call], out: Path) -> dict[str, str]:
    """Write the selections to `out` and the inputs beside it (`<stem>.calls.csv`, `<stem>.lists.csv`); returns each
    file's SHA-256 by name. Raises ConfigError for a place inside the repository: a trace measures the game."""
    files = {
        "selections_csv": (out, selections_csv(calls)),
        "calls_csv": (out.with_name(f"{out.stem}.calls.csv"), calls_csv(calls)),
        "lists_csv": (out.with_name(f"{out.stem}.lists.csv"), lists_csv(calls)),
    }
    for path, _ in files.values():
        refuse_inside_repo(path)
    out.parent.mkdir(parents=True, exist_ok=True)
    hashes = {}
    for name, (path, text) in files.items():
        data = text.encode("utf-8")
        path.write_bytes(data)
        hashes[name] = hashlib.sha256(data).hexdigest()
    return hashes
