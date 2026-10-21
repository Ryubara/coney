# SPDX-License-Identifier: GPL-3.0-or-later
"""Running a PCSX2 recipe (coney_tools.recipes has the schema): its steps over PINE, the call hook's block and the
PCSX2-MCP fork's debug server, against the running original.

Research: docs/guides/pcsx2-recipes.md
"""

from __future__ import annotations

import math
import struct
import time
from collections.abc import Callable, Mapping, Sequence
from typing import Any, Protocol

from coney_tools import hooks as hook_tools
from coney_tools.game_memory import (
    BRAIN_SIZE,
    BRAIN_TABLE,
    HANDLE_TABLE,
    TYPES,
    ExpressionError,
    GameMemory,
    Number,
    parse,
    to_raw,
)
from coney_tools.input_script import BUTTONS, stick_byte
from coney_tools.pcsx2_state import Patch
from coney_tools.pine import Memory, Read, Write
from coney_tools.recipes import Recipe, RecipeError

#: The game's 60 Hz tick count; the characters step on each tick that makes it even (docs/research/tasks.md#tick).
TICK_COUNT = 0x005104F4
#: Lua 4's globals: ScriptSystem *0x00512b04, its lua_State at +4, the globals table at +0x44
#: (docs/guides/research-workflow.md#driving-pcsx2, "Following a script at runtime").
SCRIPT_SYSTEM = 0x00512B04
#: ScriptSystem vtable slot +0x2c: run a string (lua_dobuffer over its lua_State at +4), docs/research/scripting.md.
SCRIPT_DO_STRING = 0x00356A78
#: Where a `lua` step puts its text: spare EE memory below the ELF that held only zeros in every state checked
#: (docs/guides/research-workflow.md#driving-pcsx2), above the hook caves, the call block and the ring.
LUA_BUFFER = 0x000C0000
LUA_BUFFER_SIZE = 0x4000


def button_mask(names: str) -> int:
    """The mask of Coney's button names joined by `+` (`"cross+l2"`), 0 for an empty text. Raises ExpressionError for
    an unknown name."""
    mask = 0
    for name in filter(None, (part.strip().lower() for part in str(names).split("+"))):
        if name not in BUTTONS:
            raise ExpressionError(f"no button {name!r}; there are {', '.join(BUTTONS)}")
        mask |= BUTTONS[name]
    return mask


class Debug(Protocol):
    """What a run needs from the debug server (coney_tools.debug_server.DebugClient is one; tests use fakes)."""

    def request(self, command: dict[str, Any], timeout: float = ...) -> dict[str, Any]:
        """Send one command and return its answer."""
        ...


def bind_args(recipe: Recipe, args: Mapping[str, str]) -> dict[str, Any]:
    """The parameters' values: each `--arg` given, else the default. Raises RecipeError for an unknown or missing
    one, or a number that does not parse."""
    unknown = set(args) - {p.name for p in recipe.params}
    if unknown:
        known = ", ".join(p.name for p in recipe.params) or "none"
        raise RecipeError(f"{recipe.name}: unknown argument(s) {', '.join(sorted(unknown))}; it takes {known}")
    values: dict[str, Any] = {}
    for param in recipe.params:
        raw = args.get(param.name, param.default)
        if raw is None:
            raise RecipeError(f"{recipe.name}: needs --arg {param.name}=... ({param.help})")
        if param.kind == "text":
            values[param.name] = raw
        elif param.kind == "number":
            try:
                values[param.name] = int(raw, 0)
            except ValueError:
                try:
                    values[param.name] = float(raw)
                except ValueError as error:
                    raise RecipeError(f"{recipe.name}: {param.name}={raw!r} is not a number") from error
        else:
            values[param.name] = (param.kind, raw)
    return values


def check_patches(memory: Memory, names: Sequence[str], patches: Mapping[str, Patch], hooks: Mapping[str, Any]) -> None:
    """RecipeError unless the running game holds each named patch (its replacement words) and hook (a jump into the
    hook caves). A patch goes into a state copy before PCSX2 starts (`pcsx2 prepare-state`), so a missing one means
    PCSX2 runs the wrong state."""
    for name in names:
        if name in patches:
            for edit in patches[name].edits:
                if edit.expected is None:
                    continue
                words = [int.from_bytes(edit.data[k : k + 4], "little") for k in range(0, len(edit.data), 4)]
                held = memory.batch([Read(edit.address + 4 * k, 4) for k in range(len(words))])
                if held != words:
                    raise RecipeError(
                        f"the running state lacks patch {name} at {edit.address:#010x}; make the state with "
                        f"`coney-tools pcsx2 prepare-state SOURCE OUT --patch {name}` and launch that"
                    )
        else:
            hook = hooks[name]
            word = memory.batch([Read(hook.address, 4)])[0]
            target = (word & 0x03FFFFFF) << 2
            end = hook_tools.CAVE_BASE + hook_tools.MAX_HOOKS * hook_tools.CAVE_SIZE
            if word >> 26 != 2 or not hook_tools.CAVE_BASE <= target < end:
                raise RecipeError(
                    f"the running state lacks hook {name} at {hook.address:#010x}; make the state with "
                    f"`coney-tools pcsx2 prepare-state SOURCE OUT --patch {name}` and launch that"
                )


def stick_percent(value: float) -> int:
    """The raw stick byte for a deflection of `value` percent, rounded to a whole percent first, so a direction worked
    out with sin and cos (`100 * sin(rad(60))`) can be given as it is. Raises ValueError outside -100 to 100."""
    return stick_byte(round(value))


def _format(value: Any, fmt: str | None) -> str:
    """A shown value: floats with four decimals, integers in hex unless `fmt` says otherwise."""
    if isinstance(value, str):
        return value
    if isinstance(value, float) or fmt == "float":
        return "nan" if math.isnan(float(value)) else f"{float(value):.4f}"
    if fmt == "int":
        return str(int(value))
    return f"{int(value):#x}" if int(value) >= 0 else str(int(value))


class Runner:
    """Makes a recipe's steps over a running game: PINE for memory, the debug server for the emulation pause."""

    def __init__(
        self,
        memory: Memory,
        debug: Debug | None = None,
        out: Callable[[str], None] = print,
        sleep: Callable[[float], None] = time.sleep,
        clock: Callable[[], float] = time.monotonic,
        goal_types: Mapping[int, int] | None = None,
    ) -> None:
        """Run over `memory` (and `debug` for emulation steps), printing through `out`; `goal_types` maps goal vtables
        to type ids for goal_at() (research/references/goal-types.yaml)."""
        self.memory = memory
        self.game = GameMemory(memory, goal_types)
        self.debug = debug
        self.out = out
        self.sleep = sleep
        self.clock = clock
        self.shown: dict[str, Any] = {}

    def run(self, recipe: Recipe, values: Mapping[str, Any]) -> dict[str, Any]:
        """Make every step of `recipe` with the parameters `values`; returns what the show steps showed."""
        names: dict[str, Any] = {}
        for key, value in values.items():
            if isinstance(value, tuple) and value[0] == "human":
                # A human parameter: a name, or empty for the player.
                names[key] = self.game.human(value[1]) if value[1] else self.game.player()
            elif isinstance(value, tuple):
                names[key] = self._eval(value[1], names)
            else:
                names[key] = value
        for number, step in enumerate(recipe.steps):
            where = f"{recipe.name} step {number + 1}"
            try:
                self._step(step, names, where)
            except ExpressionError as error:
                raise RecipeError(f"{where}: {error}") from error
        return self.shown

    def _eval(self, text: str, names: Mapping[str, Any]) -> Any:
        """One expression over the game and the names bound so far, with the pad helpers: `stick(percent)`, the raw
        stick byte Coney makes from a percentage (y up as an input script's, so pass -y for a y byte), and
        `buttons("cross+l2")`, the mask of Coney's button names (coney_tools.input_script)."""
        helpers: dict[str, Any] = {"stick": stick_percent, "buttons": button_mask, **names}
        return parse(text).evaluate(self.game.names(helpers))

    def _step(self, step: dict[str, Any], names: dict[str, Any], where: str) -> None:
        """One step; see the module docstring."""
        if "let" in step:
            for name, text in step["let"].items():
                names[name] = self._eval(text, names)
        elif "require" in step:
            if not self._eval(step["require"], names):
                raise RecipeError(f"{where}: {step.get('message', 'a requirement failed: ' + step['require'])}")
        elif "show" in step:
            value = self._eval(step["show"], names)
            label = step.get("label", step["show"])
            self.shown[label] = value
            self.out(f"{label} = {_format(value, step.get('format'))}")
        elif "text" in step:
            address = int(self._eval(step["text"], names))
            text = self.read_text(address, int(step.get("size", 32)))
            label = step.get("label", step["text"])
            self.shown[label] = text
            self.out(f"{label} = {text!r}")
        elif "write" in step:
            self._write(step, names)
        elif "humans" in step:
            self._humans()
        elif "members" in step:
            self._members(int(self._eval(step["members"], names)), int(step.get("count", 16)))
        elif "globals" in step:
            self._globals(str(self._eval(step["globals"], names)), int(step.get("limit", 200)))
        elif "call" in step:
            function = int(self._eval(step["call"], names)) & 0xFFFFFFFF
            args = [int(self._eval(a, names)) & 0xFFFFFFFF for a in step.get("args", [])]
            names[step.get("result", "result")] = self.call(function, args, float(step.get("timeout", 5.0)), where)
        elif "lua" in step:
            names[step.get("result", "result")] = self.lua(step["lua"], names, float(step.get("timeout", 5.0)), where)
        elif "wait" in step:
            self.wait_updates(self._count(step["wait"], names, where))
        elif "emulation" in step:
            self._debug({"cmd": "emulation", "action": step["emulation"]}, where)
        elif "advance" in step:
            frames = self._count(step["advance"], names, where)
            answer = self._debug({"cmd": "frame_advance", "frames": frames}, where, timeout=frames * 0.05 + 10)
            self.out(f"advanced {frames} frame(s): {answer.get('frame_before', '?')} -> {answer.get('frame', '?')}")

    def _count(self, value: int | str, names: Mapping[str, Any], where: str) -> int:
        """A wait or advance count: a number, or an expression worked out now; RecipeError outside 0 to 600."""
        count = int(self._eval(value, names)) if isinstance(value, str) else value
        if not 0 <= count <= 600:
            raise RecipeError(f"{where}: a count of {count} is outside 0 to 600")
        return count

    def _debug(self, command: dict[str, Any], where: str, timeout: float = 10.0) -> dict[str, Any]:
        """A debug server command; RecipeError when the copy has none."""
        if self.debug is None:
            raise RecipeError(f"{where}: needs the copy's debug server (the PCSX2-MCP fork, `pcsx2 setup-copies`)")
        return self.debug.request(command, timeout)

    def _write(self, step: dict[str, Any], names: dict[str, Any]) -> None:
        """A write step: once, or at each of the next `updates` updates (a value the game would otherwise reset)."""
        kind = step["type"]
        address = int(self._eval(step["write"], names))
        value: Number | str = self._eval(step["value"], names)
        if kind == "text":
            # A NUL-terminated string, padded to whole words (a name or a script for a call's argument).
            data = str(value).encode("latin-1") + bytes(1)
            data += bytes(-len(data) % 4)
            writes = [Write(address + k, 4, int.from_bytes(data[k : k + 4], "little")) for k in range(0, len(data), 4)]
        else:
            if isinstance(value, str):
                raise RecipeError(f"a {kind} write needs a number, not {value!r}")
            writes = [Write(address, TYPES[kind][0], to_raw(value, kind))]
        self.memory.batch([], writes)
        for _ in range(int(step.get("updates", 0))):
            self.wait_updates(1)
            self.memory.batch([], writes)
        shown = repr(value) if kind == "text" else _format(value, "float" if kind == "f32" else "int")
        self.out(f"wrote {shown} ({kind}) at {address:#010x}")

    def update_count(self) -> int:
        """Character updates since boot: the tick count halved (an update runs on each even tick)."""
        return self.memory.batch([Read(TICK_COUNT, 4)])[0] // 2

    def wait_updates(self, count: int) -> None:
        """Wait until `count` updates have run (30 a second at full speed). RecipeError when the game does not
        advance (paused, or no level)."""
        start = self.update_count()
        timeout = 5.0 + count * 0.1
        deadline = self.clock() + timeout
        while self.update_count() - start < count:
            if self.clock() > deadline:
                raise RecipeError(f"the game ran no update in {timeout:.0f} s: is emulation paused?")
            self.sleep(0.002)

    def read_text(self, address: int, size: int) -> str:
        """A C string of at most `size` bytes at `address` (read as Latin-1)."""
        start = address & ~3
        words = self.memory.batch([Read(start + 4 * k, 4) for k in range((address - start + size + 3) // 4)])
        data = b"".join(w.to_bytes(4, "little") for w in words)[address - start : address - start + size]
        return hook_tools.decode_text(data)

    def _members(self, address: int, count: int) -> None:
        """Print the humans a handle list names: each live handle's human (the handle table's pointer, `0x006ebd38`
        + index * 8), its name and player number, its brain's goal stack and its target."""
        self.out(f"{'idx':>3}  {'name':<15} {'pl':>3}  {'goals (bottom first)':<24} target")
        for handle in self.memory.batch([Read(address + 4 * k, 4) for k in range(count)]):
            index = self.game.hidx(handle)
            if index < 0:
                continue
            human = self.memory.batch([Read(HANDLE_TABLE + index * 8, 4)])[0]
            brain = BRAIN_TABLE + index * BRAIN_SIZE
            top = int(self.game.read(brain + 0x2C, "s8"))
            goals = " ".join(str(self.game.goal_at(brain, k)) for k in range(top + 1)) or "-"
            target = self.game.hidx(int(self.game.read(brain + 0x124, "u32")))
            player = int(self.game.read(human + 0x1B0, "s8"))
            self.out(f"{index:>3}  {self.game.name(human):<15} {player:>3}  {goals:<24} {target}")

    def _humans(self) -> None:
        """Print every human in use with its index, name, player number, position and heading."""
        self.out(f"{'human':>10}  {'idx':>3}  {'name':<15} {'pl':>3}  {'x':>9} {'y':>9} {'z':>9}  {'heading':>7}")
        for human in self.game.humans():
            index = self.game.index(human)
            base = 0x00714B00 + index * 0x20
            x, y, z = (float(self.game.read(base + 4 * k, "f32")) for k in range(3))
            player = int(self.game.read(human + 0x1B0, "s8"))
            heading = math.degrees(math.remainder(self.game.heading(human), math.tau))
            name = self.game.name(human)
            self.out(f"{human:#010x}  {index:>3}  {name:<15} {player:>3}  {x:9.3f} {y:9.3f} {z:9.3f}  {heading:7.1f}")

    def lua(self, code: str, names: Mapping[str, Any], timeout: float, where: str) -> int:
        """Run `code` (its `{name}` fields filled in from `names`) in the game's Lua state; returns lua_dobuffer's
        result, 0 when the chunk ran without an error."""
        try:
            text = code.format_map(dict(names))
        except (KeyError, ValueError, IndexError) as error:
            raise RecipeError(f"{where}: the Lua text's field {error} has no value") from error
        data = text.encode("latin-1") + bytes(1)
        if len(data) > LUA_BUFFER_SIZE:
            raise RecipeError(f"{where}: the Lua text is longer than {LUA_BUFFER_SIZE} bytes")
        data += bytes(-len(data) % 4)
        self.memory.batch(
            [], [Write(LUA_BUFFER + k, 4, int.from_bytes(data[k : k + 4], "little")) for k in range(0, len(data), 4)]
        )
        system = self.memory.batch([Read(SCRIPT_SYSTEM, 4)])[0]
        if not system:
            raise RecipeError(f"{where}: no script system (is a level running?)")
        self.out(f"lua: {text.strip()}")
        result = self.call(SCRIPT_DO_STRING, [system, LUA_BUFFER, 0], timeout, where)
        if result:
            self.out(f"lua_dobuffer returned {result}: the chunk failed (a syntax or run-time error)")
        return result

    def call(self, function: int, args: Sequence[int], timeout: float, where: str) -> int:
        """Make the game call `function(args...)` through the call hook's block (coney_tools.hooks): the arguments,
        then the address, which the hook clears when it makes the call; returns the call's result (v0)."""
        shown = len(args)
        args = [*args, *[0] * (hook_tools.CALL_ARGS - len(args))]
        done = self.memory.batch([Read(hook_tools.CALL_DONE, 4)])[0]
        writes = [Write(hook_tools.CALL_BASE + 4 + 4 * k, 4, a) for k, a in enumerate(args)]
        self.memory.batch([], [*writes, Write(hook_tools.CALL_BASE, 4, function)])
        deadline = self.clock() + timeout
        while True:
            pending, count, result = self.memory.batch(
                [Read(hook_tools.CALL_BASE, 4), Read(hook_tools.CALL_DONE, 4), Read(hook_tools.CALL_RESULT, 4)]
            )
            if not pending and count != done:
                self.out(f"called {function:#010x}({', '.join(hex(a) for a in args[:shown])}) -> {result:#x}")
                return int(result)
            if self.clock() > deadline:
                self.memory.batch([], [Write(hook_tools.CALL_BASE, 4, 0)])
                raise RecipeError(f"{where}: the call hook did not make the call (is a level running, unpaused?)")
            self.sleep(0.002)

    def _globals(self, pattern: str, limit: int) -> None:
        """Print the Lua globals whose names hold `pattern`: the globals table's nodes, key and value."""
        for name, kind, value in lua_globals(self.memory, limit=4096):
            if pattern.lower() in name.lower():
                self.out(f"{name} = {value} ({kind})")
                limit -= 1
                if limit <= 0:
                    self.out("... (more; narrow the pattern)")
                    return


#: Lua 4's type tags as the original's TObjects carry them (docs/guides/research-workflow.md#driving-pcsx2):
#: the order of lua.h's LUA_T* (user data, nil, number, string, table, function).
LUA_TYPES = {0: "userdata", 1: "nil", 2: "number", 3: "string", 4: "table", 5: "function"}
#: A Lua 4 hash node: key TObject (16 bytes), value TObject (16), next pointer, padded to 40. The table (Hash) holds
#: the node array at +0x0, then a u16 tag and a u16 node count at +0x4 / +0x6 (runtime, the lua-globals recipe).
NODE_SIZE = 40


def lua_globals(memory: Memory, limit: int = 4096) -> list[tuple[str, str, str]]:
    """Every global of the script's Lua state with a string key: (name, type, value as text)."""
    state = memory.batch([Read(SCRIPT_SYSTEM, 4)])[0]
    if not state:
        return []
    lua = memory.batch([Read(state + 4, 4)])[0]
    table = memory.batch([Read(lua + 0x44, 4)])[0]
    if not lua or not table:
        return []
    node, tag_size = memory.batch([Read(table + 0x0, 4), Read(table + 0x4, 4)])
    size = min(tag_size >> 16, limit)
    found: list[tuple[str, str, str]] = []
    reads = [Read(node + n * NODE_SIZE + k, 4) for n in range(size) for k in (0x0, 0x8, 0x10, 0x18, 0x1C)]
    words = memory.batch(reads) if reads else []
    texts: list[tuple[int, int, int, int]] = []
    for n in range(size):
        key_type, key, value_type, low, high = words[5 * n : 5 * n + 5]
        if key_type == 3 and key and value_type != 1:
            texts.append((key, value_type, low, high))
    runner = Runner(memory)
    for key, value_type, low, high in texts:
        name = runner.read_text(key + 0x10, 64)
        kind = LUA_TYPES.get(value_type, f"type {value_type}")
        if value_type == 2:
            text = f"{struct_double(low, high):g}"
        elif value_type == 3 and low:
            text = repr(runner.read_text(low + 0x10, 64))
        else:
            text = f"{low:#x}"
        found.append((name, kind, text))
    return sorted(found)


def struct_double(low: int, high: int) -> float:
    """The double whose little-endian words are `low` and `high`."""
    return float(struct.unpack("<d", ((high << 32) | low).to_bytes(8, "little"))[0])
