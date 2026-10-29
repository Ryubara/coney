# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.lua_hashes and the hash check of `coney-tools lua decompile --all`.

The chunks are the lua4 package's synthetic fixtures (our own Lua source), never a game script. The committed list
(research/lua-decompile-hashes.toml) is checked here for what it may hold; its hashes against the disc are checked by
`lua decompile --all` on a machine with the disc, which these tests cannot do.
"""

from __future__ import annotations

import hashlib
import re
import tomllib
from pathlib import Path

import pytest
from conventions_support import REPO

import lua4
from coney_tools import lua_decompile, lua_hashes
from coney_tools.config import ConfigError

FIXTURES = Path(__file__).parent / "lua4" / "fixtures"
COMMITTED = REPO / lua_hashes.DEFAULT_LIST
SHA = re.compile(r"^[0-9a-f]{64}$")
A, B, C, D = "a" * 64, "b" * 64, "c" * 64, "d" * 64


def _chunks(tmp_path: Path, names: tuple[str, ...] = ("control", "closures")) -> Path:
    """A folder of stripped fixture chunks, named as the game's are (`<name>.lua`)."""
    folder = tmp_path / "chunks"
    folder.mkdir(exist_ok=True)
    for name in names:
        (folder / f"{name}.lua").write_bytes((FIXTURES / f"{name}.s.luac").read_bytes())
    return folder


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _expected(name: str) -> lua_hashes.Entry:
    """A fixture's entry worked out here from the committed expected text, not by the code under test."""
    chunk = (FIXTURES / f"{name}.s.luac").read_bytes()
    # A Windows checkout may hold the expected file with CRLF line ends; the command writes LF.
    text = b"\n".join((FIXTURES / f"{name}.s.expected.lua").read_bytes().splitlines()) + b"\n"
    return lua_hashes.Entry(f"{name}.lua", _sha(chunk), _sha(text))


def _listed(tmp_path: Path, entries: list[lua_hashes.Entry], form: int = 1) -> Path:
    path = tmp_path / "list.toml"
    path.write_text(lua_hashes.render(form, entries), encoding="utf-8")
    return path


def _options(
    tmp_path: Path, hashes: Path, names: tuple[str, ...], update_hashes: bool = False, new_form: bool = False
) -> lua_decompile.Options:
    return lua_decompile.Options(
        chunks=_chunks(tmp_path, names),
        out=tmp_path / "shared",
        hashes=hashes,
        update_hashes=update_hashes,
        new_form=new_form,
    )


def _run(
    tmp_path: Path,
    hashes: Path,
    names: tuple[str, ...] = ("control", "closures"),
    update_hashes: bool = False,
    new_form: bool = False,
) -> int:
    options = _options(tmp_path, hashes, names, update_hashes, new_form)
    return lua_decompile.run_all(REPO, None, options, known_natives=frozenset())


# --- the file ---------------------------------------------------------------------------------------------------


def test_a_fixture_chunks_hash_is_the_sha256_of_its_decompile() -> None:
    """The hash is of the decompile's text as written, without the guard's marker line (which holds the name)."""
    data = (FIXTURES / "control.s.luac").read_bytes()
    result = lua_decompile.decompile_chunk("control.lua", data)
    assert lua_hashes.entry_for("control.lua", data, result.body) == _expected("control")
    assert result.text == result.body + lua_decompile.guard().marker_line("control.lua", data) + "\n"


def test_the_list_round_trips(tmp_path: Path) -> None:
    entries = [_expected("control"), _expected("closures")]
    loaded = lua_hashes.load(_listed(tmp_path, entries))
    assert loaded.form == 1
    assert loaded.entries == sorted(entries, key=lambda item: item.name)


def test_a_name_with_odd_characters_survives(tmp_path: Path) -> None:
    entry = lua_hashes.Entry('we"ird\\ é.lua', A, B)
    assert lua_hashes.load(_listed(tmp_path, [entry])).entries == [entry]


def _script(name: str = "a.lua", chunk: str = A, text: str = B, extra: str = "") -> str:
    return f"[[script]]\nname = '{name}'\nchunk = '{chunk}'\ntext = '{text}'\n{extra}"


@pytest.mark.parametrize(
    ("text", "message"),
    [
        ("", "form"),
        ("form = 0\n", "form"),
        ("form = 1\nnote = 'x'\n", "note"),
        ("form = 1\n" + _script(chunk="xyz"), "chunk"),
        ("form = 1\n" + _script(text="xyz"), "text"),
        ("form = 1\n" + _script(extra="source = 'x'\n"), "source"),
        ("form = 1\n" + _script() + _script(name="b.lua", text=C), "two different texts"),
        ("form = 1\n" + _script() + _script(), "twice"),
        ("form = 1\nnot toml", "list.toml"),
    ],
)
def test_a_damaged_list_is_refused(tmp_path: Path, text: str, message: str) -> None:
    path = tmp_path / "list.toml"
    path.write_text(text, encoding="utf-8")
    with pytest.raises(ConfigError, match=message):
        lua_hashes.load(path)


def test_a_missing_list_is_an_error(tmp_path: Path) -> None:
    with pytest.raises(ConfigError, match="no list of decompile hashes"):
        lua_hashes.load(tmp_path / "nope.toml")


def test_the_committed_list_holds_hashes_and_names_only() -> None:
    """Every entry is a name and two hashes; no line of any script's text, nor the guard's marker, can be in it."""
    raw = COMMITTED.read_text(encoding="utf-8")
    data = tomllib.loads(raw)
    assert set(data) == {"form", "script"}
    assert not lua_decompile.guard().MARKER.search(raw)
    for script in data["script"]:
        assert set(script) == {"name", "chunk", "text"}
        assert SHA.match(script["chunk"]) and SHA.match(script["text"])
        assert re.fullmatch(r"[A-Za-z0-9_.\-]{1,64}", script["name"]), script["name"]
    # Every line is a comment, a table header, or a key with a short quoted value or the form number.
    for line in raw.splitlines():
        assert (
            not line or line.startswith(("#", "[[")) or re.fullmatch(r'(form = \d+|(name|chunk|text) = "[^"]*")', line)
        )


def test_the_committed_list_loads_and_is_sorted_without_duplicates() -> None:
    loaded = lua_hashes.load(COMMITTED)
    assert loaded.form == lua_hashes.FORM
    names = [entry.name for entry in loaded.entries]
    assert len(names) == len(set(names)) and names == sorted(names)


# --- the check --------------------------------------------------------------------------------------------------


def test_compare_names_the_scripts_whose_text_changed() -> None:
    listed = lua_hashes.HashList(
        1,
        [
            lua_hashes.Entry("same.lua", A, "1" * 64),
            lua_hashes.Entry("changed.lua", B, "2" * 64),
            lua_hashes.Entry("other-disc.lua", C, "3" * 64),
        ],
    )
    found = [
        lua_hashes.Entry("same.lua", A, "1" * 64),
        lua_hashes.Entry("changed.lua", B, "9" * 64),
        lua_hashes.Entry("new.lua", D, "4" * 64),
    ]
    result = lua_hashes.compare(listed, found)
    assert result.same == 1 and result.changed == ["changed.lua"]
    assert result.unlisted == ["new.lua"] and result.absent == 1
    assert not result.ok


def test_compare_matches_by_the_chunk_so_a_renamed_script_still_checks() -> None:
    listed = lua_hashes.HashList(1, [lua_hashes.Entry("old-name.lua", A, B)])
    result = lua_hashes.compare(listed, [lua_hashes.Entry("new-name.lua", A, B)])
    assert result.ok and result.same == 1 and result.unlisted == []


def test_the_default_list_is_the_checkouts() -> None:
    assert lua_hashes.list_path(REPO, None) == COMMITTED
    assert lua_hashes.list_path(REPO, Path("x.toml")) == Path("x.toml")


# --- the command ------------------------------------------------------------------------------------------------


def test_all_writes_the_hashes_beside_the_fingerprints(tmp_path: Path) -> None:
    listed = _listed(tmp_path, [_expected("control"), _expected("closures")])
    assert _run(tmp_path, listed) == 0
    out = tmp_path / "shared"
    written = lua_hashes.load(out / lua_hashes.OUTPUT)
    assert written.entries == [_expected("closures"), _expected("control")]
    assert (out / lua_decompile.guard().FINGERPRINTS).is_file()


def test_all_passes_when_every_hash_matches(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    listed = _listed(tmp_path, [_expected("control"), _expected("closures")])
    assert _run(tmp_path, listed) == 0
    assert "decompile hashes: 2 of 2 match the list (form 1)" in capsys.readouterr().out


def test_a_changed_renderer_fails_the_check_and_names_the_scripts(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    listed = _listed(tmp_path, [_expected("control"), _expected("closures")])
    real = lua4.render
    monkeypatch.setattr(lua4, "render", lambda tree: real(tree) + "-- one more line\n")
    assert _run(tmp_path, listed, names=("control",)) == 1
    printed = capsys.readouterr()
    assert "text changed: control.lua" in printed.err
    assert "decompile hashes: 0 of 1 match" in printed.out
    assert "one more line" not in printed.out + printed.err


def test_a_chunk_not_in_the_list_is_counted_not_failed(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    listed = _listed(tmp_path, [_expected("control"), _expected("logic")])
    assert _run(tmp_path, listed) == 0
    printed = capsys.readouterr().out
    assert "1 of 2 match" in printed and "1 not in the list" in printed and "1 listed but not here" in printed


def test_a_missing_list_is_an_error_naming_the_update_flag(tmp_path: Path) -> None:
    with pytest.raises(ConfigError, match="--update-hashes"):
        _run(tmp_path, tmp_path / "missing.toml")


def test_update_adds_the_unlisted_and_keeps_the_rest(tmp_path: Path) -> None:
    keep = lua_hashes.Entry("xbox-only.lua", C, D)
    listed = _listed(tmp_path, [_expected("control"), keep])
    assert _run(tmp_path, listed, update_hashes=True) == 0
    loaded = lua_hashes.load(listed)
    assert loaded.form == 1
    assert [entry.name for entry in loaded.entries] == ["closures.lua", "control.lua", "xbox-only.lua"]


def test_update_creates_the_list(tmp_path: Path) -> None:
    listed = tmp_path / "new" / "list.toml"
    assert _run(tmp_path, listed, update_hashes=True) == 0
    assert lua_hashes.load(listed).entries == [_expected("closures"), _expected("control")]


def test_update_refuses_to_change_a_listed_hash(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    listed = _listed(tmp_path, [_expected("control")])
    before = listed.read_text(encoding="utf-8")
    monkeypatch.setattr(lua4, "render", lambda tree: "x = 1\n")
    assert _run(tmp_path, listed, names=("control",), update_hashes=True) == 1
    assert listed.read_text(encoding="utf-8") == before
    assert "--new-form" in capsys.readouterr().err


def test_new_form_rewrites_the_list_with_the_next_form_number(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    listed = _listed(tmp_path, [_expected("control"), _expected("logic")])
    monkeypatch.setattr(lua4, "render", lambda tree: "x = 1\n")
    assert _run(tmp_path, listed, names=("control",), new_form=True) == 0
    loaded = lua_hashes.load(listed)
    assert loaded.form == 2
    # Only what this run decompiled is in the new form; the rest is added by running on the disc it came from.
    assert [entry.name for entry in loaded.entries] == ["control.lua"]
    assert loaded.entries[0].text == _sha(b"x = 1\n")


def test_the_flags_exclude_each_other(tmp_path: Path) -> None:
    with pytest.raises(ConfigError, match="not both"):
        _run(tmp_path, tmp_path / "list.toml", update_hashes=True, new_form=True)


def test_update_is_refused_when_a_chunk_could_not_be_loaded(tmp_path: Path) -> None:
    listed = _listed(tmp_path, [_expected("control")])
    before = listed.read_text(encoding="utf-8")
    options = _options(tmp_path, listed, ("control",), update_hashes=True)
    assert options.chunks is not None
    (options.chunks / "newer.lua").write_bytes(b"\x1bLuaQ\x00\x01\x04\x04\x04\x08\x00")
    assert lua_decompile.run_all(REPO, None, options, frozenset()) == 1
    assert listed.read_text(encoding="utf-8") == before
