# SPDX-License-Identifier: GPL-3.0-or-later
"""What the decompiler knows about one function before it reads any statement.

* **Stack depth** before every instruction, counted the way Lua 4.0's code generator counts it (`lcode.c`): the
  count is linear in the code, dead instructions included, so it is known even where no jump reaches.
* **Active locals** at every pc, from the debug records (a local is active for `startpc <= pc < endpc`); their
  order of registration is their order on the stack.
* **Jump threading.** When the generator emits an unconditional jump at a pc that pending forward jumps target, it
  sends those jumps straight to the new jump's destination (`luaK_jump` with `jlt`). So no forward jump ever lands on
  such a jump; a structure that ends where a jump follows has its exits pointing past it. `equivalents(pc)` is the
  set of pcs a jump "to pc" may have been sent to.
* **Value regions.** An `and`/`or` or a comparison used as a value compiles to jumps that keep the tested value
  (JMPONT, JMPONF) or to the `PUSHNILJMP`/`PUSHINT 1` tail; every such region is found here, so the statement
  reader knows which conditional jumps are part of an expression and which belong to an `if`, `while` or `repeat`.

Research: docs/research/formats/lua-bytecode.md.
"""

from __future__ import annotations

from dataclasses import dataclass, field

from lua4.chunk import LocVar, Proto
from lua4.opcodes import CONDITIONAL, JUMPS, Instr, Op, decode, depth_after


class DecompileError(Exception):
    """The code does not follow a pattern the decompiler knows; the message names the pc and what was expected."""


@dataclass
class ValueRegion:
    """One expression whose value comes from jumps: an `and`/`or` chain or a comparison used as a value."""

    #: Where the value is complete (the jumps that keep a value land here).
    final: int
    #: The stack depth the expression starts at (its value lands in this slot).
    level: int
    #: The PUSHNILJMP's pc when the value comes from the nil/1 tail, else None.
    nil_pc: int | None = None
    #: Every conditional jump that ends one of the expression's operands, in pc order.
    jumps: list[int] = field(default_factory=list)
    #: Where the last operand's code ends when that operand is a value rather than a jump: the final pc, or the
    #: `JMP final` before the nil/1 tail (followed by a dead `POP 1`); None when the last operand is a jump.
    value_end: int | None = None


@dataclass
class FunctionInfo:
    """One prototype's decoded code and the facts the statement reader needs."""

    proto: Proto
    code: list[Instr]
    #: depth[pc] is the stack depth before pc; one more entry for the end.
    depth: list[int]
    lines: list[int]
    regions: dict[int, ValueRegion]
    #: Every pc a statement-level jump lands on: each is a statement boundary (in stripped code, where a `local`
    #: statement must have ended).
    boundaries: frozenset[int] = frozenset()
    #: The tests (JMPT, JMPF, NOT) to write as a comparison with nil (see `_nil_comparisons`).
    nil_tests: frozenset[int] = frozenset()

    @classmethod
    def build(cls, proto: Proto) -> FunctionInfo:
        """Decode `proto` and compute its depths, lines and value regions."""
        try:
            code = [decode(word) for word in proto.code]
        except ValueError as error:
            raise DecompileError(str(error)) from error
        depth = [proto.num_params + int(proto.is_vararg)]
        for instr in code:
            depth.append(depth_after(instr, depth[-1]))
        info = cls(proto, code, depth, proto.lines(), {})
        info.regions = _value_regions(info)
        tails = {region.value_end for region in info.regions.values() if region.nil_pc is not None}
        info.boundaries = frozenset(
            instr.target(pc)
            for pc, instr in enumerate(code)
            if instr.op in JUMPS and pc not in info.regions and pc not in tails
        )
        info.nil_tests = _nil_comparisons(info)
        return info

    # --- locals -------------------------------------------------------------------------------------------------

    def active(self, pc: int) -> list[LocVar]:
        """The locals active at `pc`, in stack order."""
        return [var for var in self.proto.locals if var.startpc <= pc < var.endpc]

    def starting(self, pc: int) -> list[LocVar]:
        """The locals that become active exactly at `pc`, in stack order."""
        return [var for var in self.proto.locals if var.startpc == pc and var.endpc > pc]

    def local_name(self, pc: int, slot: int) -> str:
        """The name of stack slot `slot` at `pc`."""
        active = self.active(pc)
        if slot >= len(active):
            raise DecompileError(f"pc {pc}: local slot {slot} is not active")
        return active[slot].name

    # --- jumps --------------------------------------------------------------------------------------------------

    def jump_target(self, pc: int) -> int:
        """Where the jump at `pc` goes."""
        return self.code[pc].target(pc)

    def equivalents(self, pc: int) -> frozenset[int]:
        """`pc` and every pc a jump meant for `pc` may have been threaded to (following unconditional jumps)."""
        seen = {pc}
        while 0 <= pc < len(self.code) and self.code[pc].op == Op.JMP:
            pc = self.jump_target(pc)
            if pc in seen:
                break
            seen.add(pc)
        return frozenset(seen)

    def backward_jumps_to(self, pc: int, limit: int) -> list[int]:
        """The jumps in [pc, limit) that go back to `pc` (a loop's head), in pc order (FORLOOPs left out)."""
        return [
            at
            for at in range(pc, min(limit, len(self.code)))
            if self.code[at].op in CONDITIONAL | {Op.JMP} and self.jump_target(at) == pc
        ]


def _nil_comparisons(info: FunctionInfo) -> frozenset[int]:
    """The tests that must be written `x ~= nil` / `x == nil` rather than `x` / `not x` for the function to need
    the stack size it records.

    The generator compiles `x ~= nil` by pushing the nil and then replacing the push and the comparison by one
    JMPT (`x == nil` becomes NOT): the code is that of `x`, but the stack briefly held one more value, and the
    function's maximum stack size remembers it. When the recorded size is one more than anything else in the code
    reaches (a field read `t.k` and an `x + 1` also push a value that is then folded away, and are counted), every
    test at the depth that explains it is written as a comparison with nil, which reads the same.
    """
    code, depth = info.code, info.depth
    reached = max(depth)
    for pc, instr in enumerate(code):
        if instr.op in (Op.GETDOTTED, Op.GETINDEXED, Op.ADDI):
            reached = max(reached, depth[pc] + 1)
    if info.proto.max_stack != reached + 1:
        return frozenset()
    tests = (Op.JMPT, Op.JMPF, Op.NOT)
    return frozenset(pc for pc, instr in enumerate(code) if instr.op in tests and depth[pc] + 1 == reached + 1)


def _value_regions(info: FunctionInfo) -> dict[int, ValueRegion]:
    """Find every value region and map each of its jumps (and its final pc) to it."""
    code = info.code
    finals: dict[int, ValueRegion] = {}
    for pc, instr in enumerate(code):
        if instr.op in (Op.JMPONT, Op.JMPONF):
            final = instr.target(pc)
            region = finals.setdefault(final, ValueRegion(final, info.depth[final] - 1))
            region.jumps.append(pc)
        elif instr.op == Op.PUSHNILJMP and pc + 2 <= len(code):
            final = pc + 2
            region = finals.setdefault(final, ValueRegion(final, info.depth[final] - 1))
            region.nil_pc = pc
    for region in finals.values():
        if region.nil_pc is None:
            region.value_end = region.final
        elif region.nil_pc >= 2:
            jump, pop = code[region.nil_pc - 2], code[region.nil_pc - 1]
            if jump.op == Op.JMP and jump.target(region.nil_pc - 2) == region.final and pop.op == Op.POP:
                region.value_end = region.nil_pc - 2
        exits = {region.final}
        if region.nil_pc is not None:
            exits |= {region.nil_pc, region.nil_pc + 1}
        members = set(region.jumps)
        # Every jump at the region's level that lands on an exit or on the start of one of its operands belongs to
        # it; the operand starts grow as jumps are added, so repeat until nothing changes.
        changed = True
        while changed:
            changed = False
            starts = {pc + 1 for pc in members}
            for pc in range(region.final - 1, -1, -1):
                instr = code[pc]
                if pc in members or instr.op not in CONDITIONAL or info.depth[pc + 1] != region.level:
                    continue
                target = instr.target(pc)
                keeps = instr.op in (Op.JMPONT, Op.JMPONF)
                if (target in exits and (keeps or target != region.final)) or (pc < target and target in starts):
                    members.add(pc)
                    changed = True
        region.jumps = sorted(members)
    regions: dict[int, ValueRegion] = {}
    for region in finals.values():
        for pc in region.jumps:
            regions[pc] = region
    return regions
