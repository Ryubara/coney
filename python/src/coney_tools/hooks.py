# SPDX-License-Identifier: GPL-3.0-or-later
"""Call logging in the original: a hook at an instruction writes chosen registers and memory into a ring buffer in
spare EE memory each time the game runs it, and the recorder reads the ring as the game plays.

PINE has no breakpoints, so a question such as "who sets this field" or "which objects does the wheel update" is
answered by patching the game's code in a state copy (docs/guides/research-workflow.md#driving-pcsx2). A hook takes
two instructions at its address: they become `j cave; nop`, and the cave, in memory the game leaves empty,

1. saves the four registers it works with (`at`, `v1`, `t8`, `t9`, all 128 bits) below the stack pointer,
2. writes one 32-byte entry to the ring: the hook's id, then up to seven values,
3. counts the entry in the ring's header (after the entry is complete, so a reader never sees half of one),
4. restores the registers, runs the two displaced instructions and jumps back after them.

A displaced pair may be a jump and its delay slot (`jr ra` with a store, as in a two-instruction setter): the cave then
ends with the pair itself, and with a jump back when the jump links (`jal`, `jalr`). A pair holding a branch (which is
relative to where it stands) is refused. The hook's address must not be a delay slot or a branch target; check it in
the disassembly before declaring the hook.

Hooks are declared in `research/traces/patches.toml`:

```toml
[hook.state-code]
description = "Each call of the record's state-code setter: who called it, the human and the code."
research = "docs/research/tasks.md#locomotion-gate"
address = 0x002266ac
original = [0x03e00008, 0xac450014]     # the two instructions displaced, checked against the state
log = ["[0x005104f4]", "ra", "a0", "a1"]
```

A logged value is a register (`a0`, `ra`, ... their low 32 bits; `sp` as the hooked code saw it; a float register
`f0`-`f31`, read as `f32` unless the item gives a type), `count` (the EE's
cycle counter, COP0 register 9: 294,912,000 a second, 4,915,200 a 60 Hz tick), or a load `[base + offset]` whose base
is a register, an address or another load (`[[a0 + 0xd4] + 0x14]`). A load reads a word unless the item is a table
`{ value = "...", type = "s16", name = "code" }` (`u8 s8 u16 s16 u32 s32 f32`); the item's text is its column name
unless `name` gives one. A load from a bad address crashes the game: log only pointers the hooked code itself uses.

A hook with `call = true` logs nothing: it lets the recorder make the game **call one of its own functions** on the
game's thread, as a script binding would (a fight started as `GoalFight` starts it). The recorder writes the
arguments and then the function's address into the call block at CALL_BASE; the next time the game runs the hook,
the cave saves every register a call may change, clears the address (so the call is made once), calls the function
with up to six arguments (`a0`-`a3`, `t0`, `t1`), stores its result and counts the call, then goes on as any hook.
Put a call hook at the entry of a function that takes no floating-point arguments, so nothing the call changes is
still in use.
"""

from __future__ import annotations

import itertools
import re
import struct
import tomllib
from dataclasses import dataclass
from pathlib import Path

from coney_tools.game_memory import Number, convert
from coney_tools.pcsx2_state import Edit, Patch, StateError
from coney_tools.pine import Memory, Read

#: Where the caves go: one of CAVE_SIZE bytes per hook, in EE memory below the game's ELF (`0x00100000`) that the
#: kernel leaves unused; it held only zeros in every state checked (`0x00095100`-`0x00100000`).
CAVE_BASE = 0x000A0000
CAVE_SIZE = 0x200
MAX_HOOKS = 32
#: The ring: a u32 count of entries ever written, then RING_ENTRIES entries of ENTRY_SIZE bytes from RING_BASE + 0x10.
RING_BASE = 0x000B0000
RING_ENTRIES = 4096
ENTRY_SIZE = 32
MAX_VALUES = ENTRY_SIZE // 4 - 1
#: The call block: the function's address (0 when no call waits), six arguments, the result and a count of calls made.
CALL_BASE = 0x000AF000
CALL_ARGS = 6
CALL_RESULT = CALL_BASE + 4 + 4 * CALL_ARGS
CALL_DONE = CALL_RESULT + 4

#: MIPS register numbers by name.
_NAMES = "zero at v0 v1 a0 a1 a2 a3 t0 t1 t2 t3 t4 t5 t6 t7 s0 s1 s2 s3 s4 s5 s6 s7 t8 t9 k0 k1 gp sp fp ra"
REGISTERS = {name: number for number, name in enumerate(_NAMES.split(" "))}
REGISTERS["s8"] = REGISTERS["fp"]
#: A float register, `f0`-`f31`, moved to t9 with mfc1.
_FLOAT = re.compile(r"f\d{1,2}")
AT, V1, T8, T9, SP = (REGISTERS[name] for name in ("at", "v1", "t8", "t9", "sp"))
#: The registers the cave works with, and where their saved values sit above its stack pointer.
SAVED = {AT: 0, V1: 16, T8: 32, T9: 48}
FRAME = 64
#: The load instruction's opcode for each type.
LOADS = {"u8": 0x24, "s8": 0x20, "u16": 0x25, "s16": 0x21, "u32": 0x23, "s32": 0x23, "f32": 0x23}

_LOAD = re.compile(r"^\[(.*)\]$", re.S)


class HookError(Exception):
    """A hook that cannot be built; the message names the hook and the problem."""


# --- instruction encoding ------------------------------------------------------------------------------------------


def _i(opcode: int, rs: int, rt: int, immediate: int) -> int:
    """An I-type instruction."""
    return (opcode << 26) | (rs << 21) | (rt << 16) | (immediate & 0xFFFF)


def _addiu(rt: int, rs: int, immediate: int) -> int:
    """`addiu rt, rs, immediate`."""
    return _i(0x09, rs, rt, immediate)


def _lui(rt: int, immediate: int) -> int:
    """`lui rt, immediate`."""
    return _i(0x0F, 0, rt, immediate)


def _load(opcode: int, rt: int, base: int, offset: int) -> int:
    """A load or store `op rt, offset(base)`."""
    return _i(opcode, base, rt, offset)


def _sw(rt: int, base: int, offset: int) -> int:
    """`sw rt, offset(base)`."""
    return _i(0x2B, base, rt, offset)


def _andi(rt: int, rs: int, immediate: int) -> int:
    """`andi rt, rs, immediate`."""
    return _i(0x0C, rs, rt, immediate)


def _sll(rd: int, rt: int, shift: int) -> int:
    """`sll rd, rt, shift`."""
    return (rt << 16) | (rd << 11) | (shift << 6)


def _addu(rd: int, rs: int, rt: int) -> int:
    """`addu rd, rs, rt`."""
    return (rs << 21) | (rt << 16) | (rd << 11) | 0x21


def _beq(rs: int, rt: int, words_ahead: int) -> int:
    """`beq rs, rt` to the instruction `words_ahead` words after its delay slot."""
    return _i(0x04, rs, rt, words_ahead)


def _jalr(rs: int) -> int:
    """`jalr rs` (the return address in `ra`)."""
    return (rs << 21) | (REGISTERS["ra"] << 11) | 0x09


def _j(target: int) -> int:
    """`j target` (same 256 MB region)."""
    return (0x02 << 26) | ((target >> 2) & 0x03FFFFFF)


NOP = 0
#: `mfc0 t9, Count`.
MFC0_T9_COUNT = 0x40000000 | (T9 << 16) | (9 << 11)


def _split(address: int) -> tuple[int, int]:
    """The `lui` half and the signed low half that add up to `address`."""
    low = address & 0xFFFF
    high = ((address + 0x8000) >> 16) & 0xFFFF
    return high, low - 0x10000 if low >= 0x8000 else low


def _jump_kind(word: int) -> str | None:
    """What a control-flow instruction is: "branch" (relative to where it stands), "jump", "link" (a jump that
    sets `ra`), or None for any other instruction."""
    opcode = word >> 26
    if opcode == 0:
        function = word & 0x3F
        return {0x08: "jump", 0x09: "link"}.get(function)
    if opcode == 0x02:
        return "jump"
    if opcode == 0x03:
        return "link"
    if opcode in (0x01, 0x04, 0x05, 0x06, 0x07, 0x14, 0x15, 0x16, 0x17):
        return "branch"
    if opcode in (0x10, 0x11, 0x12) and ((word >> 21) & 0x1F) == 0x08:
        return "branch"
    return None


# --- logged values -------------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class LogItem:
    """One logged value: `value` as written (`a0`, `count`, `[a0 + 0x14]`), its column `name` and its `type`."""

    value: str
    name: str
    type: str = "u32"


def _parse_base(text: str, where: str) -> tuple[str, int | str | None, int]:
    """Split a load's inside `base + offset` into (kind, base, offset): kind "reg" (base a register number), "abs"
    (base None, offset the address) or "load" (base the inner load's text)."""
    text = text.strip()
    offset = 0
    # The offset is the last top-level `+ n` or `- n`.
    match = re.match(r"^(.*?)([+-])\s*(0x[0-9a-fA-F]+|\d+)\s*$", text, re.S)
    if match and match.group(1).strip() and match.group(1).count("[") == match.group(1).count("]"):
        text = match.group(1).strip()
        offset = int(match.group(3), 0) * (-1 if match.group(2) == "-" else 1)
    if _LOAD.match(text):
        return "load", text, offset
    if text in REGISTERS:
        return "reg", REGISTERS[text], offset
    try:
        return "abs", None, int(text, 0) + offset
    except ValueError:
        raise HookError(f"{where}: {text!r} is not a register, an address or a load") from None


def _value_code(text: str, where: str, kind: str = "u32") -> list[int]:
    """Instructions that leave the value of `text` in t9 (the cave's stack pointer is FRAME below the hooked code's)."""
    text = text.strip()
    if text == "count":
        return [MFC0_T9_COUNT]
    if _FLOAT.fullmatch(text) and int(text[1:]) < 32:
        # mfc1 t9,fN: the cave does not touch the float registers, so they still hold the hooked code's values.
        return [0x44000000 | (T9 << 16) | (int(text[1:]) << 11)]
    if text in REGISTERS:
        number = REGISTERS[text]
        if number in SAVED:
            return [_load(0x23, T9, SP, SAVED[number])]
        if number == SP:
            return [_addiu(T9, SP, FRAME)]
        return [_addu(T9, number, 0)]
    match = _LOAD.match(text)
    if not match:
        raise HookError(f"{where}: {text!r} is not a register, `count` or a load [base + offset]")
    base_kind, base, offset = _parse_base(match.group(1), where)
    opcode = LOADS[kind]
    if base_kind == "abs":
        high, low = _split(offset)
        return [_lui(T9, high), _load(opcode, T9, T9, low)]
    if not -0x8000 <= offset < 0x8000:
        raise HookError(f"{where}: offset {offset:#x} does not fit a load")
    if base_kind == "reg":
        assert isinstance(base, int)
        prefix = _value_code(next(k for k, v in REGISTERS.items() if v == base), where)
    else:
        assert isinstance(base, str)
        prefix = _value_code(base, where)
    return [*prefix, _load(opcode, T9, T9, offset)]


# --- hooks ---------------------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class Hook:
    """A declared hook: where it goes, the two instructions it displaces and what it logs."""

    name: str
    description: str
    research: str
    address: int
    original: tuple[int, int]
    log: tuple[LogItem, ...]
    call: bool = False

    def _tail(self) -> list[int]:
        """The displaced pair, then the jump back after it (unless the pair is a jump that does not come back)."""
        first, second = self.original
        code = [first, second]
        if _jump_kind(first) in (None, "link"):
            code += [_j(self.address + 8), NOP]
        return code

    def _call_cave(self) -> list[int]:
        """The cave of a call hook: when the call block names a function, call it once with its arguments."""
        saved = [
            REGISTERS[name]
            for name in [
                "at",
                "v0",
                "v1",
                "a0",
                "a1",
                "a2",
                "a3",
                "t0",
                "t1",
                "t2",
                "t3",
                "t4",
                "t5",
                "t6",
                "t7",
                "t8",
                "t9",
                "ra",
            ]
        ]
        frame = 16 * len(saved)
        high, low = _split(CALL_BASE)
        code = [_addiu(SP, SP, -frame)]
        code += [_load(0x1F, register, SP, 16 * k) for k, register in enumerate(saved)]
        call = [
            *(
                _load(0x23, REGISTERS[name], AT, low + 4 + 4 * k)
                for k, name in enumerate(["a0", "a1", "a2", "a3", "t0", "t1"])
            ),
            _sw(0, AT, low),
            _jalr(T9),
            NOP,
            _lui(AT, high),
            _sw(REGISTERS["v0"], AT, low + CALL_RESULT - CALL_BASE),
            _load(0x23, T8, AT, low + CALL_DONE - CALL_BASE),
            _addiu(T8, T8, 1),
            _sw(T8, AT, low + CALL_DONE - CALL_BASE),
        ]
        code += [_lui(AT, high), _load(0x23, T9, AT, low), _beq(T9, 0, len(call) + 1), NOP, *call]
        code += [_load(0x1E, register, SP, 16 * k) for k, register in reversed(list(enumerate(saved)))]
        code.append(_addiu(SP, SP, frame))
        return code + self._tail()

    def cave(self, at: int, hook_id: int) -> list[int]:
        """The cave's instructions when it is placed at `at` and logs as `hook_id`."""
        where = f"hook {self.name}"
        if self.call:
            return self._call_cave()
        ring_high, _ = _split(RING_BASE)
        code = [_addiu(SP, SP, -FRAME)]
        code += [_load(0x1F, register, SP, slot) for register, slot in SAVED.items()]
        # t8 = the entry's address less RING_BASE + 0x10: at + (count & mask) * ENTRY_SIZE.
        code += [
            _lui(AT, ring_high),
            _load(0x23, T8, AT, RING_BASE & 0xFFFF),
            _andi(T8, T8, RING_ENTRIES - 1),
            _sll(T8, T8, (ENTRY_SIZE - 1).bit_length()),
            _addu(T8, T8, AT),
            _addiu(T9, 0, hook_id),
            _sw(T9, T8, (RING_BASE & 0xFFFF) + 0x10),
        ]
        for k, item in enumerate(self.log, start=1):
            code += _value_code(item.value, f"{where} ({item.name})", item.type)
            code.append(_sw(T9, T8, (RING_BASE & 0xFFFF) + 0x10 + 4 * k))
        # The entry is complete: count it.
        code += [
            _load(0x23, T8, AT, RING_BASE & 0xFFFF),
            _addiu(T8, T8, 1),
            _sw(T8, AT, RING_BASE & 0xFFFF),
        ]
        code += [_load(0x1E, register, SP, slot) for register, slot in reversed(SAVED.items())]
        code.append(_addiu(SP, SP, FRAME))
        code += self._tail()
        if len(code) * 4 > CAVE_SIZE:
            raise HookError(f"{where}: its cave needs {len(code) * 4} bytes, more than {CAVE_SIZE}")
        return code


def _log_item(raw: object, where: str) -> LogItem:
    """One entry of a hook's `log`: a string, or a table with value, name and type."""
    if isinstance(raw, str):
        item = LogItem(raw, raw.replace(" ", ""))
    elif isinstance(raw, dict) and isinstance(raw.get("value"), str):
        unknown = set(raw) - {"value", "name", "type"}
        if unknown:
            raise HookError(f"{where}: unknown key(s) {', '.join(sorted(unknown))}")
        kind = str(raw.get("type", "u32"))
        if kind not in LOADS:
            raise HookError(f"{where}: type must be one of {', '.join(LOADS)}")
        item = LogItem(raw["value"], str(raw.get("name", raw["value"].replace(" ", ""))), kind)
    else:
        raise HookError(f"{where}: a logged value is a string or a table with a value")
    is_float = bool(_FLOAT.fullmatch(item.value.strip()))
    if is_float and isinstance(raw, str):
        item = LogItem(item.value, item.name, "f32")
    if item.type != "u32" and not _LOAD.match(item.value.strip()) and not is_float:
        raise HookError(f"{where}: only a load [...] or a float register takes a type")
    _value_code(item.value, where, item.type)
    return item


def parse_hook(name: str, raw: object, where: str) -> Hook:
    """A hook from its TOML table. Raises HookError for a bad address, displaced pair or logged value."""
    if not isinstance(raw, dict):
        raise HookError(f"{where}: must be a table")
    unknown = set(raw) - {"description", "research", "address", "original", "log", "call"}
    if unknown:
        raise HookError(f"{where}: unknown key(s) {', '.join(sorted(unknown))}")
    address = raw.get("address")
    if not isinstance(address, int) or address % 4 or not 0 <= address < 0x02000000:
        raise HookError(f"{where}: address must be a word-aligned EE address")
    original = raw.get("original")
    if not isinstance(original, list) or len(original) != 2 or not all(isinstance(w, int) for w in original):
        raise HookError(f"{where}: original must be the two instruction words the hook displaces")
    first, second = (w & 0xFFFFFFFF for w in original)
    if _jump_kind(first) == "branch" or _jump_kind(second) is not None:
        raise HookError(
            f"{where}: the displaced pair holds a branch, or a jump in its second word; hook another address"
        )
    log = raw.get("log", [])
    if not isinstance(log, list) or len(log) > MAX_VALUES:
        raise HookError(f"{where}: log must be a list of at most {MAX_VALUES} values")
    items = tuple(_log_item(item, f"{where} log {i + 1}") for i, item in enumerate(log))
    if len({item.name for item in items}) != len(items):
        raise HookError(f"{where}: two logged values have the same name")
    call = bool(raw.get("call", False))
    if call and items:
        raise HookError(f"{where}: a call hook logs nothing")
    description, research = str(raw.get("description", "")), str(raw.get("research", ""))
    return Hook(name, description, research, address, (first, second), items, call)


def load_hooks(path: Path) -> dict[str, Hook]:
    """Every `[hook.NAME]` of the patch file at `path`. Raises HookError for a missing file or a bad hook."""
    try:
        with path.open("rb") as handle:
            data = tomllib.load(handle)
    except (OSError, tomllib.TOMLDecodeError, UnicodeDecodeError) as error:
        raise HookError(f"{path}: {error}") from error
    raw = data.get("hook", {})
    return {name: parse_hook(name, table, f"{path}: hook {name}") for name, table in raw.items()}


def build(hooks: list[Hook]) -> Patch:
    """One patch installing `hooks`, with ids 1, 2, ... in their order: the caves, a cleared ring header and the jump
    at each hook's address (checked against its displaced pair). Raises HookError for too many hooks or two that
    overlap."""
    if len(hooks) > MAX_HOOKS:
        raise HookError(f"at most {MAX_HOOKS} hooks at once")
    if sum(hook.call for hook in hooks) > 1:
        raise HookError("at most one call hook at once (they share the call block)")
    spans = sorted((hook.address, hook.name) for hook in hooks)
    for (a, first), (b, second) in itertools.pairwise(spans):
        if b - a < 8:
            raise HookError(f"hooks {first} and {second} overlap")
    jumps, caves = [], []
    for hook_id, hook in enumerate(hooks, start=1):
        at = CAVE_BASE + (hook_id - 1) * CAVE_SIZE
        code = hook.cave(at, hook_id)
        caves.append(Edit(at, b"".join(struct.pack("<I", w) for w in code), None))
        original = b"".join(struct.pack("<I", w) for w in hook.original)
        jumps.append(Edit(hook.address, struct.pack("<II", _j(at), NOP), original))
    header = Edit(RING_BASE, bytes(16), None)
    if any(hook.call for hook in hooks):
        caves.append(Edit(CALL_BASE, bytes(CALL_DONE + 4 - CALL_BASE), None))
    names = ", ".join(hook.name for hook in hooks)
    # The jumps come first: the launcher reads a patch's first edit back to know the patched state runs.
    return Patch("hooks", f"call logging: {names}", "docs/guides/research-workflow.md#hooks", (*jumps, header, *caves))


# --- reading the ring ----------------------------------------------------------------------------------------------


@dataclass
class HookLog:
    """What the ring gave: each hook's entries (as `(sequence, step, values)`), and the entries lost to overflow."""

    hooks: list[Hook]
    entries: dict[str, list[tuple[int, int, list[Number]]]]
    lost: int = 0
    seen: int = 0

    def csv(self, hook: Hook) -> str:
        """One hook's entries as CSV: the ring's sequence number, the recorder's step, then each logged value."""
        lines = [",".join(["seq", "step", *(item.name for item in hook.log)])]
        for sequence, step, values in self.entries[hook.name]:
            cells = [_cell(value, item) for item, value in zip(hook.log, values, strict=True)]
            lines.append(",".join([str(sequence), str(step), *cells]))
        return "\n".join(lines) + "\n"


def _cell(value: Number, item: LogItem) -> str:
    """A logged value as it goes into the CSV: a float with four decimals, a word in hex, other integers as they are."""
    if isinstance(value, float):
        return f"{value:.4f}"
    return f"{value:#x}" if item.type == "u32" else str(value)


class RingReader:
    """Reads the entries hooks add to the ring, a batch at a time, keeping them in order."""

    def __init__(self, memory: Memory, hooks: list[Hook]) -> None:
        """Read the ring of `hooks` (installed by build() in that order) over `memory`."""
        self.memory = memory
        self.log = HookLog(hooks, {hook.name: [] for hook in hooks})
        self.done = 0

    def drain(self, count: int, step: int) -> None:
        """Read every entry written since the last drain, given the ring's `count` now; tag them with `step`."""
        if count <= self.done:
            return
        first = max(self.done, count - RING_ENTRIES)
        self.log.lost += first - self.done
        reads = [
            Read(RING_BASE + 0x10 + (n % RING_ENTRIES) * ENTRY_SIZE + 8 * k, 8)
            for n in range(first, count)
            for k in range(ENTRY_SIZE // 8)
        ]
        values = self.memory.batch(reads) if reads else []
        for n in range(first, count):
            chunk = values[(n - first) * 4 : (n - first) * 4 + 4]
            raw = b"".join(v.to_bytes(8, "little") for v in chunk)
            words = struct.unpack("<8I", raw)
            hook_id = words[0]
            if not 1 <= hook_id <= len(self.log.hooks):
                continue
            hook = self.log.hooks[hook_id - 1]
            decoded = [_decode(word, item.type) for word, item in zip(words[1:], hook.log, strict=False)]
            self.log.entries[hook.name].append((n, step, decoded))
        self.log.seen += count - first
        self.done = count


def _decode(word: int, kind: str) -> Number:
    """A logged word as its type (a load of a smaller type was sign- or zero-extended by the load)."""
    if kind in ("u8", "u16", "u32"):
        return word
    if kind == "f32":
        return convert(word, "f32")
    return convert(word, "s32") if kind in ("s8", "s16", "s32") else word


def hooks_for(names: list[str], known: dict[str, Hook], patches: set[str]) -> list[Hook]:
    """The hooks among `names` (the others must be patches). Raises StateError naming an unknown name."""
    unknown = [name for name in names if name not in known and name not in patches]
    if unknown:
        raise StateError(
            f"unknown patch(es) or hook(s) {', '.join(unknown)}; known: {', '.join(sorted(set(known) | patches))}"
        )
    return [known[name] for name in names if name in known]
