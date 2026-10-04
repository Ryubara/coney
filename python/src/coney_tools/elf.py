# SPDX-License-Identifier: GPL-3.0-or-later
"""Function sizes from the game's executable, which has no symbols, for the progress tracker.

`SLUS_212.15` is a little-endian 32-bit MIPS ELF with section headers (`.text`, `.data`, ...) but no symbol table,
so nothing in it says where a function ends. This module estimates it from where other functions start:

* every target of a `jal` instruction in `.text` starts a function;
* every 8-byte-aligned word in `.data` that points into `.text` is taken to start one too (vtables and other
  function pointers live there; switch jump tables, which point inside functions, live in `.rodata` instead).

A function's *span* runs from its address to the next known start (or the end of `.text`), including the zero
padding that aligns the next function. Together these find about as many starts (14,097) as Ghidra finds functions
(13,789), but the span is only an upper bound: a function that no call and no `.data` pointer reaches (one entered
only by a tail jump, say) is not seen, and the span of the function before it swallows it.
Research: docs/research/source-map.md#method.
"""

from __future__ import annotations

import bisect
import struct
from dataclasses import dataclass

from coney_tools.config import ConfigError

_ELF_MAGIC = b"\x7fELF"
_EM_MIPS = 8


class ElfError(ConfigError):
    """The file is not the 32-bit little-endian MIPS ELF the tools expect, or is damaged."""


@dataclass(frozen=True)
class Section:
    """A section with file contents: where it is loaded and where its bytes are in the file."""

    address: int
    offset: int
    size: int


@dataclass(frozen=True)
class Elf:
    """The parts of the executable the size estimate needs."""

    data: bytes
    sections: dict[str, Section]

    def section(self, name: str) -> Section:
        """The section called `name`. Raises ElfError when there is none."""
        try:
            return self.sections[name]
        except KeyError as error:
            raise ElfError(f"no {name} section in the executable") from error

    def words(self, name: str) -> tuple[int, ...]:
        """The little-endian 32-bit words of section `name`."""
        sec = self.section(name)
        return struct.unpack_from(f"<{sec.size // 4}I", self.data, sec.offset)

    def word_at(self, address: int) -> int:
        """The `.text` word at virtual `address`."""
        text = self.section(".text")
        return int(struct.unpack_from("<I", self.data, text.offset + address - text.address)[0])


def read_elf(data: bytes) -> Elf:
    """Parse the ELF header and section headers of `data`. Raises ElfError when it is not a 32-bit LE MIPS ELF."""
    if len(data) < 52 or data[:4] != _ELF_MAGIC:
        raise ElfError("not an ELF file")
    if data[4] != 1 or data[5] != 1:
        raise ElfError("not a 32-bit little-endian ELF")
    if struct.unpack_from("<H", data, 18)[0] != _EM_MIPS:
        raise ElfError("not a MIPS executable")
    shoff = struct.unpack_from("<I", data, 32)[0]
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 46)
    if shoff == 0 or shnum == 0 or shentsize < 40 or shoff + shnum * shentsize > len(data) or shstrndx >= shnum:
        raise ElfError("no usable section headers (the size estimate needs .text and .data by name)")
    headers = [struct.unpack_from("<10I", data, shoff + i * shentsize) for i in range(shnum)]
    names_offset = headers[shstrndx][4]
    sections = {}
    for name_index, kind, _flags, address, offset, size, *_ in headers:
        start = names_offset + name_index
        end = data.find(b"\0", start)
        name = data[start:end].decode("latin-1") if end >= 0 else ""
        if kind == 8 or not name:  # SHT_NOBITS (.bss) has no file contents
            continue
        if offset + size > len(data):
            raise ElfError(f"section {name} runs past the end of the file")
        sections[name] = Section(address, offset, size)
    elf = Elf(data, sections)
    elf.section(".text")
    return elf


def function_starts(elf: Elf) -> list[int]:
    """Sorted addresses known to start a function: `jal` targets and `.data` pointers into `.text`."""
    text = elf.section(".text")
    end = text.address + text.size
    starts = set()
    for i, word in enumerate(elf.words(".text")):
        if word >> 26 == 3:  # jal: the 26-bit word index replaces the low 28 bits of the next instruction's address
            target = ((text.address + i * 4 + 4) & 0xF0000000) | ((word & 0x03FFFFFF) << 2)
            if text.address <= target < end:
                starts.add(target)
    if ".data" in elf.sections:
        starts.update(w for w in elf.words(".data") if text.address <= w < end and w % 8 == 0)
    return sorted(starts)


def is_jump_or_branch(word: int) -> bool:
    """Whether a MIPS instruction has a delay slot (a jump or a branch)."""
    op = word >> 26
    if op == 0:
        return word & 0x3F in (8, 9)  # jr, jalr
    if op == 1:
        return (word >> 16) & 0x1F in (0, 1, 2, 3, 0x10, 0x11, 0x12, 0x13)  # bltz, bgez and their -l/-al forms
    if op in (0x10, 0x11, 0x12):
        return (word >> 21) & 0x1F == 8  # bc0/bc1/bc2 condition branches
    return op in (2, 3, 4, 5, 6, 7, 0x14, 0x15, 0x16, 0x17)  # j, jal, beq, bne, blez, bgtz and the -likely forms


def span(elf: Elf, address: int, starts: list[int]) -> int:
    """Bytes from `address` to the next known function start (or the end of `.text`): an upper bound.

    The span includes the zero padding that aligns the next function, so the spans of consecutive functions tile
    `.text` and a fully reimplemented stretch of code adds up to 100%.
    """
    text = elf.section(".text")
    index = bisect.bisect_right(starts, address)
    end = starts[index] if index < len(starts) else text.address + text.size
    return end - address


def code_length(elf: Elf, address: int, length: int) -> int:
    """`length` less the zero padding at its end: where the function's last instruction ends.

    A zero word right after a jump or a branch is that instruction's delay slot (`nop`) and belongs to the function.
    """
    end = address + length
    while end - address >= 8 and elf.word_at(end - 4) == 0 and not is_jump_or_branch(elf.word_at(end - 8)):
        end -= 4
    return end - address


@dataclass(frozen=True)
class SizeCheck:
    """What the executable says about one function's size."""

    span: int  # the upper bound from `span`, padding included: the size `progress sizes --fill` writes
    code: int  # the span without its trailing padding: the least a size can be
    errors: tuple[str, ...]
    warnings: tuple[str, ...]


def check_size(elf: Elf, address: int, size: int | None, starts: list[int]) -> SizeCheck:
    """Compare a given size (or none) with the executable: errors make the size impossible, warnings unusual.

    A size is accepted from the end of the function's last instruction (`code`) up to the next known function
    (`span`); either end is right, depending on whether the alignment padding is counted.
    """
    text = elf.section(".text")
    if not text.address <= address < text.address + text.size:
        return SizeCheck(0, 0, (f"{address:#010x} is outside .text",), ())
    errors, warnings = [], []
    upper = span(elf, address, starts)
    code = code_length(elf, address, upper)
    if address % 8:
        warnings.append("not 8-byte aligned, unlike almost every function in the executable")
    index = bisect.bisect_left(starts, address)
    if index == len(starts) or starts[index] != address:
        warnings.append("no call or pointer reaches this address, so it may not be a function's entry")
    # No check that the code ends in `jr ra` and its delay slot: GCC often places a block after the epilogue.
    if size is not None:
        if size > upper:
            errors.append(f"size {size} runs past the next known function start ({upper} bytes on)")
        elif size < code:
            warnings.append(f"size {size} stops before the last instruction ({code} bytes on); a function there?")
    return SizeCheck(upper, code, tuple(errors), tuple(warnings))
