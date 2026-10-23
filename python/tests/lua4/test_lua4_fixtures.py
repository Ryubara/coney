# SPDX-License-Identifier: GPL-3.0-or-later
"""The decompiler on synthetic chunks compiled from our own Lua 4.0 source (lua4_support explains the fixtures).

Each source is compiled with and without debug information; both chunks must decompile to their committed expected
output, and, when a `luac` is found, that output must compile back to the same instructions and constants (the round
trip the disc check runs on the game's chunks).
"""

from __future__ import annotations

import pytest
from lua4_support import FIXTURES, NAMES, VARIANTS, find_luac, regenerate

import lua4
from lua4.opcodes import Op, decode

LUAC = find_luac()
needs_luac = pytest.mark.skipif(LUAC is None, reason="no Lua 4.0 luac (LUA4_LUAC or coney.local.toml's luac_program)")
CASES = [(name, variant) for name in NAMES for variant in VARIANTS]


def _chunk(name: str, variant: str) -> bytes:
    return (FIXTURES / f"{name}{variant}.luac").read_bytes()


@pytest.mark.skipif(not regenerate(), reason="set LUA4_REGENERATE=1 to rewrite the fixtures")
@needs_luac
@pytest.mark.parametrize("name", NAMES)
def test_regenerate(name: str) -> None:
    """Rewrite a fixture's chunks and expected outputs (only with LUA4_REGENERATE=1)."""
    assert LUAC is not None
    source = (FIXTURES / f"{name}.lua").read_bytes()
    for variant in VARIANTS:
        chunk = lua4.compile_source(source, LUAC, strip=variant == ".s")
        (FIXTURES / f"{name}{variant}.luac").write_bytes(chunk)
        (FIXTURES / f"{name}{variant}.expected.lua").write_text(lua4.decompile(chunk), encoding="utf-8", newline="\n")


def test_there_are_fixtures() -> None:
    """The fixtures are found (a renamed folder would otherwise pass every parametrized test vacuously)."""
    assert {"expressions", "control", "closures", "logic", "assign"} <= set(NAMES)


@pytest.mark.parametrize(("name", "variant"), CASES)
def test_decompiles_to_the_expected_source(name: str, variant: str) -> None:
    """Each chunk decompiles to its committed expected output, every function read whole."""
    expected = (FIXTURES / f"{name}{variant}.expected.lua").read_text(encoding="utf-8")
    text = lua4.decompile(_chunk(name, variant))
    assert "could not decompile" not in text
    assert text == expected


@needs_luac
@pytest.mark.parametrize(("name", "variant"), CASES)
def test_round_trip(name: str, variant: str) -> None:
    """The expected output compiles back to the chunk's instructions and constants, in every function."""
    assert LUAC is not None
    original = lua4.load(_chunk(name, variant))
    text = (FIXTURES / f"{name}{variant}.expected.lua").read_text(encoding="utf-8")
    again = lua4.load(lua4.compile_source(text, LUAC, strip=variant == ".s"))
    assert lua4.compare(original, again) == []


@needs_luac
@pytest.mark.parametrize(("name", "variant"), CASES)
def test_committed_chunks_match_their_source(name: str, variant: str) -> None:
    """The committed chunk is what luac makes of the committed source (regenerate after editing a source)."""
    assert LUAC is not None
    fresh = lua4.compile_source((FIXTURES / f"{name}.lua").read_bytes(), LUAC, strip=variant == ".s")
    assert lua4.compare(lua4.load(_chunk(name, variant)), lua4.load(fresh)) == []


def test_fixtures_use_every_opcode() -> None:
    """Between them the fixtures exercise all 49 Lua 4.0 instructions."""
    used = {
        decode(word).op
        for name in NAMES
        for variant in VARIANTS
        for _, function in lua4.load(_chunk(name, variant)).walk()
        for word in function.code
    }
    assert set(Op) - used == set()


@pytest.mark.parametrize("name", NAMES)
def test_stripped_and_full_chunks_hold_the_same_code(name: str) -> None:
    """Stripping removes only debug information: the code the decompiler reads is the same."""
    assert lua4.compare(lua4.load(_chunk(name, "")), lua4.load(_chunk(name, ".s"))) == []
    stripped = lua4.load(_chunk(name, ".s"))
    assert all(not function.locals and not function.line_info for _, function in stripped.walk())
