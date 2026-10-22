# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `original study`: a fake ghidra-mcp serving an invented function, files in a temporary scratch folder."""

import json
from datetime import UTC, datetime, timedelta
from pathlib import Path
from typing import Any
from urllib.parse import urlparse

import pytest
from stamp_support import rules

from coney_tools import orig_cut, orig_study, stamps
from coney_tools.orig_study import GhidraDown, StudyError
from coney_tools.progress import ProgressError
from coney_tools.unit import Unit

ADDRESS = "0x00501000"
COMMIT = "ab" * 20
WIDGET = """
/* Widget_Update: an invented plate comment. */

int Widget_Update(long param_1,int param_2)

{
  int iVar1;
  long lVar2;

  iVar1 = *(int *)(param_1 + 0x1c);
  if (param_2 < iVar1) {
    return 0;
  }
  lVar2 = Widget_GetOwner(param_1);
  if (lVar2 == 0) {
    return 1;
  }
  Widget_PlaySound(param_1,0x2a);
  DAT_00501234 = DAT_00501234 + 1;
  return 2;
}
""".replace("\n", "\r\n")


class FakeGhidra:
    """Serves the endpoints `original study` reads, for one invented function."""

    def __init__(self) -> None:
        self.text = WIDGET
        self.hash = "11" * 32
        self.program = "SLUS_212.15"
        self.start = "00501000"
        self.calls: list[str] = []
        self.down = False

    def __call__(self, path: str) -> Any:
        if self.down:
            raise ProgressError("ghidra-mcp at http://x: refused (is the server running?)")
        self.calls.append(path)
        url = urlparse(path)
        endpoint = url.path
        if endpoint == "/get_current_program_info":
            return {"name": self.program}
        if endpoint == "/get_function_by_address":
            return {"name": "Widget_Update", "address": self.start, "entry_point": self.start, "signature": "int f()"}
        if endpoint == "/decompile_function":
            return {"name": "Widget_Update", "address": self.start, "decompiled": self.text}
        if endpoint == "/get_function_hash":
            return {"hash": self.hash, "address": self.start}
        if endpoint == "/get_function_callers":
            return {"callers": [{"name": "Widget_Tick", "address": "00502000"}], "total": 1}
        if endpoint == "/get_function_callees":
            return {
                "callees": [
                    {"name": "Widget_GetOwner", "address": "00501100"},
                    {"name": "Widget_PlaySound", "address": "00501200"},
                ],
                "total": 2,
            }
        if endpoint == "/audit_globals_in_function":
            return {
                "globals": [
                    {"address": "00501234", "name": "DAT_00501234", "type": "undefined4", "length": 4, "xref_count": 3}
                ]
            }
        return {"error": f"unknown {endpoint}"}


class Clock:
    """A clock that moves a second per reading."""

    def __init__(self) -> None:
        self.now = datetime(2026, 10, 22, 12, 0, 0, tzinfo=UTC)

    def __call__(self) -> datetime:
        self.now += timedelta(seconds=1)
        return self.now


def run(tmp_path: Path, ghidra: FakeGhidra | None, **more: Any) -> orig_study.StudyResult:
    """Study the invented function into a unit under tmp_path."""
    unit = Unit("t", tmp_path / "t")
    return orig_study.study(
        unit,
        ADDRESS,
        ghidra,
        rules=rules(),
        commit=COMMIT,
        who={"kind": "tool", "tool": "test"},
        clock=Clock(),
        **more,
    )


def stamp_list(tmp_path: Path) -> list[Any]:
    """The stamps of the unit."""
    return stamps.verify(tmp_path / "t" / "stamps", rules()).stamps


def test_study_saves_the_function_and_stamps_it(tmp_path: Path) -> None:
    result = run(tmp_path, FakeGhidra())
    folder = tmp_path / "t" / "study" / ADDRESS
    saved = (folder / "decompile.c").read_bytes()
    assert b"\r" not in saved and saved.decode("utf-8") == orig_cut.normalise(WIDGET)
    meta = json.loads((folder / "study.json").read_text(encoding="utf-8"))
    assert meta["address"] == ADDRESS and meta["name"] == "Widget_Update" and meta["program"] == "SLUS_212.15"
    assert meta["statements"] == 9 and meta["pieces"] == [
        {"id": p.id, "kind": p.kind, "lines": orig_cut.format_lines(p.lines)} for p in result.cut.pieces
    ]
    assert meta["decompile_sha256"] == stamps.sha256_hex(saved)
    assert (folder / "numbered.txt").read_text(encoding="utf-8").count("P1") >= 1
    assert json.loads((folder / "callers.json").read_text(encoding="utf-8"))[0]["address"] == "00502000"
    [stamp] = stamp_list(tmp_path)
    assert (stamp.step, stamp.subject, stamp.result, stamp.commit) == ("studied", ADDRESS, "pass", COMMIT)
    assert stamp.covers["decompile_sha256"] == meta["decompile_sha256"]
    assert stamp.covers["ghidra_function_hash"] == "11" * 32
    assert stamp.covers["cut_sha256"] == meta["cut_sha256"] and stamp.covers["shape_sha256"] == meta["shape_sha256"]
    assert stamp.detail["callees"] == 2 and stamp.detail["globals"] == 1 and stamp.detail["statements"] == 9
    assert result.stamped and result.change == "new"


def test_only_read_endpoints_are_called(tmp_path: Path) -> None:
    ghidra = FakeGhidra()
    run(tmp_path, ghidra)
    allowed = {
        "/get_current_program_info",
        "/get_function_by_address",
        "/decompile_function",
        "/get_function_hash",
        "/get_function_callers",
        "/get_function_callees",
        "/audit_globals_in_function",
    }
    assert {urlparse(c).path for c in ghidra.calls} <= allowed


def test_studying_again_without_a_change_writes_no_stamp(tmp_path: Path) -> None:
    run(tmp_path, FakeGhidra())
    again = run(tmp_path, FakeGhidra())
    assert not again.stamped and again.change == "unchanged" and len(stamp_list(tmp_path)) == 1


def test_a_rename_restamps_but_keeps_the_cut(tmp_path: Path) -> None:
    run(tmp_path, FakeGhidra())
    renamed = FakeGhidra()
    renamed.text = WIDGET.replace("Widget_PlaySound", "Widget_Sound")
    result = run(tmp_path, renamed)
    assert result.stamped and result.change == "renamed"
    first, second = stamp_list(tmp_path)
    assert first.covers["cut_sha256"] == second.covers["cut_sha256"]
    assert first.covers["decompile_sha256"] != second.covers["decompile_sha256"]


def test_a_changed_function_changes_the_cut_and_says_so(tmp_path: Path) -> None:
    run(tmp_path, FakeGhidra())
    changed = FakeGhidra()
    changed.hash = "22" * 32
    changed.text = WIDGET.replace("return 1;", "Widget_Log(); return 1;")
    result = run(tmp_path, changed)
    assert result.stamped and result.change == "changed"


def test_ghidra_down_writes_nothing(tmp_path: Path) -> None:
    ghidra = FakeGhidra()
    ghidra.down = True
    with pytest.raises(GhidraDown, match="ghidra-mcp"):
        run(tmp_path, ghidra)
    assert not (tmp_path / "t").exists()


def test_the_wrong_program_is_refused(tmp_path: Path) -> None:
    ghidra = FakeGhidra()
    ghidra.program = "SLES_999.99"
    with pytest.raises(StudyError, match=r"SLUS_212\.15"):
        run(tmp_path, ghidra)
    assert not (tmp_path / "t").exists()


def test_an_address_that_is_no_function_start_names_the_real_one(tmp_path: Path) -> None:
    ghidra = FakeGhidra()

    class Inside(FakeGhidra):
        def __call__(self, path: str) -> Any:
            if urlparse(path).path == "/get_function_by_address":
                return {"name": "Widget_Update", "address": "00501000", "entry_point": "00500ff0"}
            return ghidra(path)

    with pytest.raises(StudyError, match="0x00500ff0"):
        run(tmp_path, Inside())


def test_an_address_with_no_function_is_refused(tmp_path: Path) -> None:
    class Nothing(FakeGhidra):
        def __call__(self, path: str) -> Any:
            if urlparse(path).path == "/get_function_by_address":
                return {"error": "No function found for 0x00501000"}
            return super().__call__(path)

    with pytest.raises(StudyError, match="no function"):
        run(tmp_path, Nothing())


def test_a_decompile_error_is_refused(tmp_path: Path) -> None:
    class Failed(FakeGhidra):
        def __call__(self, path: str) -> Any:
            if urlparse(path).path == "/decompile_function":
                return {"error": "Decompilation failed"}
            return super().__call__(path)

    with pytest.raises(StudyError, match="Decompilation failed"):
        run(tmp_path, Failed())
    assert not (tmp_path / "t" / "stamps").exists()


def test_recut_reads_the_saved_decompile_without_ghidra(tmp_path: Path) -> None:
    run(tmp_path, FakeGhidra())
    result = run(tmp_path, None)
    assert not result.stamped  # nothing changed
    saved = tmp_path / "t" / "study" / ADDRESS / "decompile.c"
    saved.write_bytes(saved.read_bytes() + b"\n")  # a changed file is a changed hash
    with pytest.raises(StudyError, match="hash"):
        orig_study.load_study(Unit("t", tmp_path / "t"), ADDRESS)


def test_recut_with_nothing_saved_is_refused(tmp_path: Path) -> None:
    with pytest.raises(StudyError, match="nothing saved"):
        run(tmp_path, None)


def test_load_study_returns_what_compare_needs(tmp_path: Path) -> None:
    run(tmp_path, FakeGhidra())
    loaded = orig_study.load_study(Unit("t", tmp_path / "t"), ADDRESS)
    assert loaded.address == ADDRESS and loaded.name == "Widget_Update"
    assert loaded.cut_sha256 == orig_cut.cut_sha256(loaded.cut) and len(loaded.cut.statements) == 9
    assert loaded.text.startswith("\n/* Widget_Update")


def test_freshness_against_live_ghidra(tmp_path: Path) -> None:
    run(tmp_path, FakeGhidra())
    saved = orig_study.load_study(Unit("t", tmp_path / "t"), ADDRESS)
    assert orig_study.freshness(saved, FakeGhidra()) == "fresh"
    renamed = FakeGhidra()
    renamed.text = WIDGET.replace("Widget_GetOwner", "Widget_Owner")
    assert orig_study.freshness(saved, renamed) == "renamed"
    moved = FakeGhidra()
    moved.hash = "33" * 32
    assert orig_study.freshness(saved, moved) == "stale"
    reshaped = FakeGhidra()
    reshaped.text = WIDGET.replace("return 2;", "Foo(); return 2;")
    assert orig_study.freshness(saved, reshaped) == "stale"
