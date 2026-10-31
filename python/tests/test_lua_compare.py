# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools lua compare` (coney_tools.lua_compare): two decompile folders compared script by script.

The folders are written by `lua decompile --all` from our own fixture chunks (python/tests/lua4/fixtures), so every
script here is our own Lua; nothing comes from a game disc.
"""

from __future__ import annotations

from pathlib import Path

import pytest
from conventions_support import REPO

from coney_tools import lua_compare, lua_decompile
from coney_tools.config import ConfigError

# Reads the checkout's own files of these classes (python/tests/repo_reads.py).
pytestmark = pytest.mark.reads("docs_only")

FIXTURES = Path(__file__).parent / "lua4" / "fixtures"


def _decompiled(tmp_path: Path, label: str, chunks: dict[str, str]) -> Path:
    """A `decompile --all` folder of the fixture chunks `chunks` (file name -> fixture name)."""
    source = tmp_path / f"{label}-chunks"
    source.mkdir()
    for name, fixture in chunks.items():
        (source / name).write_bytes((FIXTURES / f"{fixture}.s.luac").read_bytes())
    out = tmp_path / label
    assert lua_decompile.run_all(REPO, None, lua_decompile.Options(chunks=source, out=out), frozenset()) == 0
    return out


def test_identical_folders_have_every_chunk_in_both(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    """The same chunks under the same names: all paired, nothing only on one side, exit 0."""
    chunks = {"control.lua": "control", "closures.lua": "closures"}
    a, b = _decompiled(tmp_path, "a", chunks), _decompiled(tmp_path, "b", chunks)
    capsys.readouterr()
    assert lua_compare.run(a, b) == 0
    printed = capsys.readouterr().out
    assert (
        "the same chunk in both: 2 (decompiles identical: 2; under the same names: 2, under other names: 0)" in printed
    )
    assert "only in A: 0" in printed and "only in B: 0" in printed


def test_extra_scripts_renamed_and_changed_ones(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    """B holds A's `control` under another name (a hash, as for an unnamed entry), a changed `logic` and an extra
    script: the renamed one is paired by its chunk, the changed one by its name, and A not held whole is exit 1."""
    a = _decompiled(tmp_path, "a", {"control.lua": "control", "logic.lua": "logic"})
    b = _decompiled(tmp_path, "b", {"0badf00d": "control", "logic.lua": "closures", "extra.lua": "assign"})
    capsys.readouterr()
    assert lua_compare.run(a, b, listing=True) == 1
    printed = capsys.readouterr().out
    assert (
        "the same chunk in both: 1 (decompiles identical: 1; under the same names: 0, under other names: 1)" in printed
    )
    assert "the same name, another chunk: 1 (decompiles identical: 0)" in printed
    assert "only in A: 0" in printed and "only in B: 1" in printed
    assert " extra.lua" in printed and "  changed logic.lua: " in printed
    assert "control.lua | 0badf00d" in printed
    text = (b / "extra.lua").read_text(encoding="latin-1").splitlines()
    assert not {line for line in text[:-1] if len(line.strip()) > 8} & set(printed.splitlines())


def test_a_script_missing_from_b_is_exit_1(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    a = _decompiled(tmp_path, "a", {"control.lua": "control", "logic.lua": "logic"})
    b = _decompiled(tmp_path, "b", {"control.lua": "control"})
    assert lua_compare.run(a, b) == 1
    assert "only in A: 1" in capsys.readouterr().out


def test_a_file_without_the_marker_is_refused(tmp_path: Path) -> None:
    a = _decompiled(tmp_path, "a", {"control.lua": "control"})
    (a / "notes.lua").write_text("print('mine')\n", encoding="utf-8")
    with pytest.raises(ConfigError, match="marker"):
        lua_compare.read_folder(a)
    with pytest.raises(ConfigError, match="no such folder"):
        lua_compare.read_folder(tmp_path / "missing")
    (tmp_path / "empty").mkdir()
    with pytest.raises(ConfigError, match="no decompiled scripts"):
        lua_compare.read_folder(tmp_path / "empty")
