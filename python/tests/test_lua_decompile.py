# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.lua_decompile: `coney-tools lua decompile`, its settings and the round trip.

The chunks are the lua4 package's synthetic fixtures (our own Lua source, compiled with a stock `luac`), never a game
script. The round trip runs here with a stand-in compiler, so it is tested without `luac`; the test with the real
`luac` skips when there is none.
"""

from __future__ import annotations

import json
import struct
import sys
import zlib
from pathlib import Path

import pytest
from conventions_support import REPO, real_data

import lua4
from coney_tools import lua_decompile, lua_index, workspace_fetch
from coney_tools.config import ConfigError
from coney_tools.lua4 import LuaError

FIXTURES = Path(__file__).parent / "lua4" / "fixtures"
XH_CLOSURES = zlib.crc32(b"ee_files\\closures.lua")
EXE = "luac.exe" if sys.platform == "win32" else "luac"


def _workspace(tmp_path: Path, local: str = "") -> Path:
    """A made-up checkout in the workspace layout (`<workspace>/repos/coney`) with `local` as its coney.local.toml."""
    root = tmp_path / "workspace" / "repos" / "coney"
    root.mkdir(parents=True)
    (root / "coney.local.example.toml").write_text("", encoding="utf-8")
    (root / "coney.local.toml").write_text(local, encoding="utf-8")
    return root


def _chunks(tmp_path: Path, names: tuple[str, ...] = ("control", "closures")) -> Path:
    """A folder of extracted chunks (stripped fixtures, as the game's are) and one file that is not a chunk."""
    folder = tmp_path / "chunks"
    folder.mkdir()
    for name in names:
        (folder / f"{name}.lua").write_bytes((FIXTURES / f"{name}.s.luac").read_bytes())
    (folder / "index.json").write_text("{}", encoding="utf-8")
    return folder


# --- settings ---------------------------------------------------------------------------------------------------


def test_luac_program_is_the_configured_one(tmp_path: Path) -> None:
    """`luac_program` in coney.local.toml wins; a configured path that is no file is an error (or None)."""
    root = _workspace(tmp_path, 'luac_program = "../../my/luac4"\n')
    with pytest.raises(ConfigError, match="luac_program"):
        lua_decompile.luac_program(root)
    assert lua_decompile.luac_program(root, required=False) is None
    program = tmp_path / "workspace" / "my" / "luac4"
    program.parent.mkdir(parents=True)
    program.write_bytes(b"")
    assert lua_decompile.luac_program(root) == program.resolve()


def test_luac_program_defaults_to_the_one_workspace_init_builds(tmp_path: Path) -> None:
    """Without the setting, the luac `workspace init --fetch lua` built in the workspace's tools folder; with none
    there, the error names that command."""
    root = _workspace(tmp_path)
    with pytest.raises(ConfigError, match="--fetch lua"):
        lua_decompile.luac_program(root)
    assert lua_decompile.luac_program(root, required=False) is None
    built = workspace_fetch.tool_destination("lua", tmp_path / "workspace") / "bin" / EXE
    built.parent.mkdir(parents=True)
    built.write_bytes(b"")
    assert lua_decompile.luac_program(root) == built
    assert built.parent.parent == tmp_path / "workspace" / "tools" / "lua-4.0.1"


def test_lua_dir_is_shared_by_every_checkout_of_the_workspace(tmp_path: Path) -> None:
    """The shared folder: `lua_dir` when set (relative to the file), else `scratch/lua` in the workspace."""
    root = _workspace(tmp_path)
    assert lua_decompile.lua_dir(root) == tmp_path / "workspace" / "scratch" / "lua"
    (root / "coney.local.toml").write_text('lua_dir = "../../elsewhere/lua"\n', encoding="utf-8")
    assert lua_decompile.lua_dir(root) == (tmp_path / "workspace" / "elsewhere" / "lua").resolve()


def test_the_shared_folder_is_a_workspace_folder_of_the_conventions() -> None:
    """The default shared folder is listed in `workspace.shared_scratch`, so the scratch check never reports it as a
    branch's leftovers."""
    folder = Path(lua_decompile.DEFAULT_DIR)
    assert folder.parent.name == "scratch"
    assert folder.name in real_data()["workspace"]["shared_scratch"]


def test_the_example_file_lists_both_settings() -> None:
    """coney.local.example.toml documents the two keys this command reads."""
    example = (REPO / "coney.local.example.toml").read_text(encoding="utf-8")
    assert "\nluac_program " in example and "\nlua_dir " in example


# --- one chunk -----------------------------------------------------------------------------------------------------


def test_a_decompile_ends_with_the_guards_marker() -> None:
    """The text is the lua4 package's, followed by the marker the guard refuses; nothing else is added, so every
    statement keeps its line."""
    data = (FIXTURES / "control.s.luac").read_bytes()
    result = lua_decompile.decompile_chunk("control.lua", data)
    guard = lua_decompile.guard()
    assert result.text == lua4.decompile(data) + guard.marker_line("control.lua", data) + "\n"
    assert guard.problem(result.text, frozenset()) is not None
    assert result.failures == [] and result.functions == len(lua4.load(data).walk())


@pytest.mark.parametrize(
    ("name", "written"),
    [("level1.lua", "level1.lua"), ("00f4b07a", "00f4b07a.lua"), ("sub/x.lua", "sub/x.lua")],
)
def test_output_names(tmp_path: Path, name: str, written: str) -> None:
    """A chunk's file is its name, with `.lua` added when the name has none."""
    assert lua_decompile.output_path(tmp_path, name) == tmp_path / written


@pytest.mark.parametrize("name", ["../x.lua", "/abs.lua", "a/../../b.lua", ""])
def test_an_output_name_never_leaves_the_folder(tmp_path: Path, name: str) -> None:
    with pytest.raises(ConfigError):
        lua_decompile.output_path(tmp_path, name)


def test_chunks_from_a_folder_skip_other_files(tmp_path: Path) -> None:
    """Only files that start with a Lua chunk's signature are chunks; they come in name order."""
    found = lua_decompile.chunks_from_folder(_chunks(tmp_path))
    assert [name for name, _ in found] == ["closures.lua", "control.lua"]


def _xbox_disc(tmp_path: Path) -> Path:
    """A made-up Xbox disc as a folder: `XBoxWad.idx` over one volume holding two stripped fixtures and an entry that
    is not a chunk, and a `default.xbe` that names one of the two (`control.lua`); the other stays unnamed."""
    blobs = [
        ((FIXTURES / "control.s.luac").read_bytes(), zlib.crc32(b"ee_files\\control.lua")),
        (b"not a chunk, 32 bytes of filler.", 0x0BADF00D),
        ((FIXTURES / "closures.s.luac").read_bytes(), 0x00C0FFEE),
    ]
    volume, rows = b"", []
    for data, hashed in blobs:
        rows.append(struct.pack("<4I", len(volume) // 1024, len(data), 0, hashed))
        volume += data + b"\0" * (-len(data) % 2048)
    header = struct.pack("<5I", 1, len(rows), 0x00100000, 0x14, 0x14 + 36)
    disc = tmp_path / "xbox"
    disc.mkdir()
    (disc / "XBoxWad.idx").write_bytes(header + b"Main.wad0".ljust(32, b"\0") + b"\xff" * 4 + b"".join(rows))
    (disc / "Main.wad0").write_bytes(volume)
    (disc / "default.xbe").write_bytes(b"XBEH\0D:\\ee_files\\control.lua\0")
    return disc


def test_chunks_from_the_xbox_disc_are_named_from_the_executable(tmp_path: Path) -> None:
    """The Xbox archive's chunks are found by their signature; one the executable names takes that name, the other
    its entry's hash in hex, and the entry that is no chunk is left out."""
    found = lua_decompile.chunks_from_xbox(_workspace(tmp_path), _xbox_disc(tmp_path))
    assert [name for name, _ in found] == ["00c0ffee", "control.lua"]
    assert found[1][1] == (FIXTURES / "control.s.luac").read_bytes()


def test_a_bare_identifier_names_an_xbox_script(tmp_path: Path) -> None:
    """A level list names its levels without an extension: an identifier `closures` names `closures.lua`."""
    found = lua_decompile._xbox_script_names(_workspace(tmp_path), {0x00C0FFEE, XH_CLOSURES}, [b"\0closures\0"])
    assert found == {XH_CLOSURES: "closures.lua"}


def test_decompile_all_and_one_from_the_xbox_disc(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    """`--all --xbox-disc` writes every Xbox chunk; `NAME OUT_DIR --xbox-disc` finds one by name or by hash."""
    root, disc = _workspace(tmp_path), _xbox_disc(tmp_path)
    out = tmp_path / "xbox-out"
    # A disc whose scripts the committed list lacks (the Xbox's own) is added to a list of its own with --update-hashes.
    options = lua_decompile.Options(xbox=disc, out=out, hashes=tmp_path / "hashes.toml", update_hashes=True)
    assert lua_decompile.run_all(root, None, options, frozenset()) == 0
    assert sorted(path.name for path in out.glob("*.lua")) == ["00c0ffee.lua", "control.lua"]
    assert "decompiled 2 of 2 chunks" in capsys.readouterr().out
    one = tmp_path / "one"
    assert lua_decompile.run_one(root, None, "CONTROL", one, lua_decompile.Options(xbox=disc)) == 0
    assert lua_decompile.run_one(root, None, "0x00c0ffee", one, lua_decompile.Options(xbox=disc)) == 0
    assert sorted(path.name for path in one.iterdir()) == ["0x00c0ffee.lua", "CONTROL.lua"]
    with pytest.raises(ConfigError, match="on the Xbox disc"):
        lua_decompile.run_one(root, None, "missing.lua", one, lua_decompile.Options(xbox=disc))


def test_decompile_all_from_the_xbox_disc_needs_its_own_folder(tmp_path: Path) -> None:
    """`--all --xbox-disc` without `--out` is refused before anything is read or written: the shared folder is the PS2
    disc's, and every worktree reads its index and fingerprints."""
    root, disc = _workspace(tmp_path), _xbox_disc(tmp_path)
    with pytest.raises(ConfigError, match="--out"):
        lua_decompile.run_all(root, None, lua_decompile.Options(xbox=disc), frozenset())
    assert not lua_decompile.lua_dir(root).exists() or not any(lua_decompile.lua_dir(root).iterdir())


def test_decompile_one_chunk_writes_it_outside_the_repository(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    """`lua decompile NAME OUT_DIR --chunks DIR` writes NAME's decompile into OUT_DIR, and prints no script text."""
    out = tmp_path / "out"
    options = lua_decompile.Options(chunks=_chunks(tmp_path))
    assert lua_decompile.run_one(REPO, None, "control.lua", out, options) == 0
    data = (FIXTURES / "control.s.luac").read_bytes()
    assert (out / "control.lua").read_bytes() == lua_decompile.decompile_chunk("control.lua", data).text.encode()
    printed = capsys.readouterr().out
    assert "control.lua" in printed
    assert not set(lua4.load(data).strings) & set(printed.split())


def test_decompile_refuses_an_output_folder_in_the_repository(tmp_path: Path) -> None:
    options = lua_decompile.Options(chunks=_chunks(tmp_path))
    with pytest.raises(ConfigError, match="inside the repository"):
        lua_decompile.run_one(REPO, None, "control.lua", REPO / "scratch-in-repo", options)
    assert not (REPO / "scratch-in-repo").exists()


def test_an_unknown_chunk_name_is_an_error(tmp_path: Path) -> None:
    options = lua_decompile.Options(chunks=_chunks(tmp_path))
    with pytest.raises(ConfigError, match="no chunk named"):
        lua_decompile.run_one(REPO, None, "missing.lua", tmp_path / "out", options)


# --- every chunk --------------------------------------------------------------------------------------------------


@pytest.mark.reads("docs_only")
def test_decompile_all_writes_the_scripts_fingerprints_and_index(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    """`--all` writes every chunk, the fingerprints the guard reads and the index, and prints counts."""
    out = tmp_path / "shared"
    options = lua_decompile.Options(chunks=_chunks(tmp_path), out=out)
    assert lua_decompile.run_all(REPO, None, options, known_natives=frozenset({"f", "g"})) == 0
    assert sorted(path.name for path in out.glob("*.lua")) == ["closures.lua", "control.lua"]
    guard = lua_decompile.guard()
    prints = frozenset((out / guard.FINGERPRINTS).read_text(encoding="ascii").split())
    text = (out / "control.lua").read_text(encoding="latin-1")
    assert guard.pasted(text, prints)
    expected = {
        guard.fingerprint(line) for path in out.glob("*.lua") for line in path.read_text("latin-1").splitlines()
    }
    assert prints == expected - {None}
    index = json.loads((out / lua_index.INDEX_JSON).read_text(encoding="utf-8"))
    assert sorted(index["scripts"]) == ["closures.lua", "control.lua"]
    printed = capsys.readouterr().out
    assert "decompiled 2 of 2 chunks" in printed and "0 functions could not be read" in printed


#: Chunks the loader refuses: a Lua 5.1 header, and a fixture cut short.
BAD_CHUNKS = {
    "newer.lua": b"\x1bLuaQ\x00\x01\x04\x04\x04\x08\x00",
    "truncated.lua": (FIXTURES / "logic.s.luac").read_bytes()[:40],
}


@pytest.mark.reads("docs_only")
def test_decompile_all_reports_a_bad_chunk_and_goes_on(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    """A chunk the loader refuses is named with the reason, and every other chunk is still written, fingerprinted and
    indexed; the command then exits 1."""
    chunks = _chunks(tmp_path)
    for name, data in BAD_CHUNKS.items():
        (chunks / name).write_bytes(data)
    out = tmp_path / "shared"
    assert lua_decompile.run_all(REPO, None, lua_decompile.Options(chunks=chunks, out=out), frozenset()) == 1
    assert sorted(path.name for path in out.glob("*.lua")) == ["closures.lua", "control.lua"]
    guard = lua_decompile.guard()
    prints = frozenset((out / guard.FINGERPRINTS).read_text(encoding="ascii").split())
    assert guard.pasted((out / "control.lua").read_text(encoding="latin-1"), prints)
    index = json.loads((out / lua_index.INDEX_JSON).read_text(encoding="utf-8"))
    assert sorted(index["scripts"]) == ["closures.lua", "control.lua"]
    printed = capsys.readouterr()
    assert "decompiled 2 of 4 chunks" in printed.out and "2 chunks could not be loaded" in printed.out
    for name in BAD_CHUNKS:
        assert f"{name}: not loaded (" in printed.err


def test_decompile_one_bad_chunk_is_one_line(tmp_path: Path) -> None:
    """One chunk the loader refuses is a ConfigError naming it (one line from the command), and nothing is written."""
    chunks = _chunks(tmp_path)
    (chunks / "newer.lua").write_bytes(BAD_CHUNKS["newer.lua"])
    out = tmp_path / "out"
    with pytest.raises(ConfigError, match=r"newer\.lua: not loaded \(Lua version"):
        lua_decompile.run_one(REPO, None, "newer.lua", out, lua_decompile.Options(chunks=chunks))
    assert not out.exists()


def test_round_trip_counts_without_quoting_the_script(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    """The round trip compiles each written file and compares it with its chunk; the report is counts and kinds.

    A stand-in compiler gives back the original chunk for one file and another fixture's chunk for the other."""
    chunks = _chunks(tmp_path)
    originals = {name: data for name, data in lua_decompile.chunks_from_folder(chunks)}
    other = (FIXTURES / "logic.s.luac").read_bytes()

    def compile_text(text: str) -> bytes:
        return originals["control.lua"] if "control.lua" in text.splitlines()[-1] else other

    result = lua_decompile.round_trip(
        [(name, data, lua_decompile.decompile_chunk(name, data).text) for name, data in originals.items()],
        compile_text,
    )
    assert (result.total, result.same, result.refused, result.differ) == (2, 1, 0, 1)
    assert result.failed == ["closures.lua"]
    lua_decompile.print_round_trip(result)
    printed = capsys.readouterr().out
    assert "round trip: 1 of 2 chunks compile back to the same code" in printed
    lua_decompile.print_round_trip(lua_decompile.RoundTrip())
    own = set(capsys.readouterr().out.split())  # the report's own words ("other" is also a fixture's string)
    strings = {text for name, data in originals.items() for _, f in lua4.load(data).walk() for text in f.strings}
    assert not {word for word in printed.replace(",", " ").split() if word in strings} - own


def test_round_trip_compares_the_natives_each_function_calls(capsys: pytest.CaptureFixture[str]) -> None:
    """Beside the code, the round trip compares the natives each function calls, and prints counts and hashes only.

    A stand-in compiler gives back the original chunk for one fixture and another fixture's chunk for the other, so
    one chunk calls the same natives and one does not; `f` and `g` stand for the bindings."""
    control = (FIXTURES / "control.s.luac").read_bytes()
    closures = (FIXTURES / "closures.s.luac").read_bytes()
    items = [("control.lua", control, "control"), ("closures.lua", closures, "closures")]
    result = lua_decompile.round_trip(items, lambda text: control, frozenset({"f", "g"}))
    assert (result.natives_compared, result.natives_same) == (2, 1)
    assert result.native_uses > 0
    lua_decompile.print_round_trip(result)
    printed = capsys.readouterr().out
    assert "natives: 1 of 2 chunks call the same natives, function by function" in printed
    assert "sha256" in printed
    assert " f," not in printed and " g " not in printed  # no native's name, only the hashes


def test_round_trip_counts_a_refusal_by_luac() -> None:
    """luac refusing the text is counted with the line it named, never its message (which may quote the source)."""
    data = (FIXTURES / "control.s.luac").read_bytes()

    def refuse(text: str) -> bytes:
        raise lua4.LuacError("luac: `end' expected; last token read: `\"secret\"' at line 7 in file `x.lua'")

    result = lua_decompile.round_trip([("control.lua", data, "x = 1\n")], refuse)
    assert (result.refused, result.same) == (1, 0)
    assert result.kinds == {"luac refused it (line 7)": 1}


def test_round_trip_with_the_real_luac() -> None:
    """With a stock luac, every fixture's decompile (marker included) compiles back to the same code."""
    luac = lua_decompile.luac_program(REPO, required=False)
    if luac is None:
        pytest.skip("no Lua 4.0 luac (coney.local.toml's luac_program)")
    items = []
    for path in sorted(FIXTURES.glob("*.s.luac")):
        data = path.read_bytes()
        items.append((path.name, data, lua_decompile.decompile_chunk(path.name, data).text))
    result = lua_decompile.round_trip(items, lambda text: lua4.compile_source(text, luac, strip=True))
    assert result.same == result.total == len(items) > 0


@pytest.mark.reads("docs_only")
def test_a_chunk_the_index_cannot_read_is_left_out_of_it(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """The index's reader takes only the game's layout (a 64-bit luac's chunk has 8-byte sizes): such a chunk is still
    decompiled, and only the index leaves it out, saying so."""

    def refuse(name: str, data: bytes, known: frozenset[str]) -> lua_index.Script:
        raise LuaError("size_t is 8 bytes")

    monkeypatch.setattr(lua_index, "index_script", refuse)
    out = tmp_path / "shared"
    assert lua_decompile.run_all(REPO, None, lua_decompile.Options(chunks=_chunks(tmp_path), out=out), frozenset()) == 0
    assert (out / "control.lua").is_file()
    assert json.loads((out / lua_index.INDEX_JSON).read_text(encoding="utf-8"))["scripts"] == {}
    assert "control.lua is not in the index" in capsys.readouterr().err
