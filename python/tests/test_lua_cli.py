# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools lua decompile`'s command line (coney_tools.lua_cli): the arguments reach the command.

The command itself is tested in test_lua_decompile.py; here only how `[DISC] NAME OUT_DIR`, `--all` and the options
are read, through `coney_tools.cli.main`.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any

import pytest

from coney_tools import cli, lua_compare, lua_decompile

FIXTURES = Path(__file__).parent / "lua4" / "fixtures"


@pytest.fixture
def calls(monkeypatch: pytest.MonkeyPatch) -> list[tuple[str, tuple[Any, ...]]]:
    """Record the calls of run_one and run_all instead of running them."""
    found: list[tuple[str, tuple[Any, ...]]] = []

    def recorder(kind: str) -> Any:
        def record(*args: Any) -> int:
            found.append((kind, args))
            return 0

        return record

    monkeypatch.setattr(lua_decompile, "run_one", recorder("one"))
    monkeypatch.setattr(lua_decompile, "run_all", recorder("all"))
    return found


def test_one_chunk_with_and_without_a_disc(calls: list[tuple[str, tuple[Any, ...]]]) -> None:
    assert cli.main(["lua", "decompile", "level1.lua", "out"]) == 0
    assert cli.main(["lua", "decompile", "H:/", "level1.lua", "out", "--roundtrip", "--luac", "x/luac"]) == 0
    (_, first), (_, second) = calls
    assert first[1:4] == (None, "level1.lua", Path("out")) and first[4] == lua_decompile.Options()
    assert second[1:4] == ("H:/", "level1.lua", Path("out"))
    assert second[4] == lua_decompile.Options(luac=Path("x/luac"), roundtrip=True)


def test_all_chunks_with_the_options(calls: list[tuple[str, tuple[Any, ...]]]) -> None:
    assert cli.main(["lua", "decompile", "--all"]) == 0
    assert cli.main(["lua", "decompile", "--all", "H:/", "--out", "o", "--roundtrip"]) == 0
    assert cli.main(["lua", "decompile", "--all", "--chunks", "c"]) == 0
    (kind, first), (_, second), (_, third) = calls
    assert kind == "all" and first[1] is None and first[2] == lua_decompile.Options()
    assert second[1] == "H:/" and second[2] == lua_decompile.Options(out=Path("o"), roundtrip=True)
    assert third[1] is None and third[2] == lua_decompile.Options(chunks=Path("c"))


def test_the_frozen_form_options(calls: list[tuple[str, tuple[Any, ...]]]) -> None:
    assert cli.main(["lua", "decompile", "--all", "--hashes", "h.toml", "--update-hashes"]) == 0
    assert cli.main(["lua", "decompile", "--all", "--new-form"]) == 0
    (_, first), (_, second) = calls
    assert first[2] == lua_decompile.Options(hashes=Path("h.toml"), update_hashes=True)
    assert second[2] == lua_decompile.Options(new_form=True)


def test_the_xbox_disc_as_the_source(calls: list[tuple[str, tuple[Any, ...]]]) -> None:
    assert cli.main(["lua", "decompile", "--all", "--xbox-disc", "x.iso", "--out", "o"]) == 0
    assert cli.main(["lua", "decompile", "level1.lua", "out", "--xbox-disc", "x.iso"]) == 0
    (kind, first), (_, second) = calls
    assert kind == "all" and first[1] is None and first[2] == lua_decompile.Options(xbox=Path("x.iso"), out=Path("o"))
    assert second[1:4] == (None, "level1.lua", Path("out")) and second[4] == lua_decompile.Options(xbox=Path("x.iso"))


def test_compare_reaches_the_command(monkeypatch: pytest.MonkeyPatch) -> None:
    seen: list[tuple[Path, Path, bool]] = []

    def record(first: Path, second: Path, listing: bool) -> int:
        seen.append((first, second, listing))
        return 0

    monkeypatch.setattr(lua_compare, "run", record)
    assert cli.main(["lua", "compare", "ps2", "xbox", "--list"]) == 0
    assert seen == [(Path("ps2"), Path("xbox"), True)]


@pytest.mark.parametrize(
    "argv",
    [
        ["lua", "decompile"],
        ["lua", "decompile", "only-a-name"],
        ["lua", "decompile", "a", "b", "c", "d"],
        ["lua", "decompile", "--all", "a", "b"],
        ["lua", "decompile", "--chunks", "c", "H:/", "name", "out"],
        ["lua", "decompile", "name", "out", "--out", "o"],
        ["lua", "decompile", "name", "out", "--update-hashes"],
        ["lua", "decompile", "name", "out", "--hashes", "h.toml"],
        ["lua", "decompile", "--all", "--xbox-disc", "x.iso", "H:/"],
        ["lua", "decompile", "--xbox-disc", "x.iso", "H:/", "name", "out"],
        ["lua", "decompile", "--all", "--xbox-disc", "x.iso", "--chunks", "c"],
    ],
)
def test_wrong_arguments_exit_2_with_one_line(
    argv: list[str], calls: list[tuple[str, tuple[Any, ...]]], capsys: pytest.CaptureFixture[str]
) -> None:
    assert cli.main(argv) == 2
    assert calls == [] and capsys.readouterr().err.startswith("coney-tools: ")
