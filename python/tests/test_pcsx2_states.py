# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the PCSX2 state library (coney_tools.pcsx2_states and pcsx2_states_cli): the catalog, adding and handing
out states, verifying them, and reading a state's level and the player's place with the `where` and `position`
recipes. Every state here is a made-up zip around a few made-up bytes of RAM, and PINE is a fake: no PCSX2, no
BIOS and no game data."""

from __future__ import annotations

import json
import math
import os
import shutil
import stat
import types
import zipfile
from collections.abc import Sequence
from pathlib import Path
from typing import Any

import pytest

from coney_tools import (
    config,
    pcsx2_claims,
    pcsx2_cli,
    pcsx2_copies,
    pcsx2_state,
    pcsx2_states,
    pcsx2_states_cli,
    recipes,
    recipes_cli,
    recipes_render,
)
from coney_tools import hooks as hook_tools
from coney_tools.cli import main
from coney_tools.config import ConfigError, find_repo_root, load_config
from coney_tools.game_memory import GAME_STATE_POINTER, HUMAN_TABLE, TRANSFORM_TABLE, to_raw
from coney_tools.pcsx2_states import Entry, Library, LibraryError, StateMemory, read_scene
from coney_tools.pine import PineError, Read, Write

ROOT = find_repo_root(Path(__file__))
GAME_STATE = 0x00800000
LEVEL_RECORDS = 0x14D4


@pytest.fixture(autouse=True)
def no_git(monkeypatch: pytest.MonkeyPatch) -> None:
    """Every test checkout is a plain folder, so asking git for its main checkout (a process each time, and the bulk of
    the run time) can only say the folder itself."""
    for module in (config, pcsx2_states, pcsx2_copies, pcsx2_claims):
        monkeypatch.setattr(module, "main_checkout", lambda root: root)


# -- made-up states


def make_ram(
    level: str = "level99",
    checkpoint: int = 3,
    position: tuple[float, float, float] = (1.5, -20.0, 0.25),
    heading_deg: float = 90.0,
    in_level: bool = True,
) -> bytearray:
    """A made-up 32 MB of EE RAM holding a game state with one level record and a player, laid out as the `where` and
    `position` recipes read them (docs/guides/pcsx2-recipes.md); `in_level` False leaves the game state empty, as the
    front end does."""
    ram = bytearray(pcsx2_state.EE_MEMORY_SIZE)

    def put(address: int, size: int, value: int) -> None:
        ram[address : address + size] = (value & ((1 << (8 * size)) - 1)).to_bytes(size, "little")

    if not in_level:
        return ram
    put(GAME_STATE_POINTER, 4, GAME_STATE)
    put(GAME_STATE + 0x56DC, 2, 1)
    record = GAME_STATE + LEVEL_RECORDS + 1 * 0x84 + 0x14
    ram[record : record + len(level) + 1] = level.encode() + b"\0"
    put(GAME_STATE + 0x33A, 2, checkpoint)
    # The player: human 0 in use, player number 0, handle index 2.
    put(HUMAN_TABLE + 0xD4, 4, 0x0065A540)
    ram[HUMAN_TABLE + 0x80 : HUMAN_TABLE + 0x88] = b"Hero\0\0\0\0"
    put(HUMAN_TABLE + 0x1B0, 1, 0)
    put(HUMAN_TABLE + 0x92, 2, 2)
    transform = TRANSFORM_TABLE + 2 * 0x20
    for k, value in enumerate(position):
        put(transform + 4 * k, 4, to_raw(value, "f32"))
    half = math.radians(heading_deg) / 2
    put(transform + 0x18, 4, to_raw(math.sin(half), "f32"))
    put(transform + 0x1C, 4, to_raw(math.cos(half), "f32"))
    return ram


def make_state(
    path: Path, ram: bytes | bytearray, version: str = "v2.9.94", date: tuple[int, int, int] = (2026, 10, 5)
) -> Path:
    """Write a made-up save state: PCSX2's zip with a version entry, the RAM and a stand-in for a screenshot."""
    path.parent.mkdir(parents=True, exist_ok=True)
    version_entry = (b"\x00\x00Y\x9a" + version.encode() + b"\0").ljust(36, b"\xcc")
    with zipfile.ZipFile(path, "w") as state:
        for name, data in (
            (pcsx2_state.VERSION_ENTRY, version_entry),
            (pcsx2_state.EE_MEMORY, bytes(ram)),
            ("Screenshot.png", b"not a picture"),
        ):
            info = zipfile.ZipInfo(name, (*date, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED if name == pcsx2_state.EE_MEMORY else zipfile.ZIP_STORED
            state.writestr(info, data)
    return path


@pytest.fixture(scope="session")
def standard_state(tmp_path_factory: pytest.TempPathFactory) -> Path:
    """One made-up state in level99, checkpoint 3, that tests copy (writing 32 MB of zip is the slow part)."""
    return make_state(tmp_path_factory.mktemp("standard") / "standard.p2s", make_ram())


@pytest.fixture
def state_file(tmp_path: Path, standard_state: Path) -> Path:
    """A copy of the standard state in this test's folder."""
    target = tmp_path / "source" / "slot.p2s"
    target.parent.mkdir()
    shutil.copyfile(standard_state, target)
    return target


def make_checkout(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> Path:
    """A made-up workspace with a checkout that has the real recipes and patches, a library folder and a PCSX2 copy
    folder; the process is inside the checkout."""
    root = tmp_path / "ws" / "repos" / "coney"
    root.mkdir(parents=True)
    (root / "coney.local.example.toml").write_text("", encoding="utf-8")
    (root / "coney.local.toml").write_text(
        f'pcsx2_states_dir = "{(tmp_path / "library").as_posix()}"\n', encoding="utf-8"
    )
    (root / "research").mkdir()
    shutil.copy(ROOT / recipes.RECIPES_FILE, root / recipes.RECIPES_FILE)
    shutil.copytree(ROOT / recipes.PATCHES_DIR, root / recipes.PATCHES_DIR)
    monkeypatch.chdir(root)
    return root


def entry(state_id: str = "pedestrian-grab", **changes: Any) -> Entry:
    """A made-up catalog entry."""
    base = {
        "id": state_id,
        "level": "level99",
        "checkpoint": 3,
        "description": "facing a pedestrian, ready to grab",
        "tags": ("grab",),
        "author": "tester",
        "made": "2026-10-05",
        "added": "2026-10-21",
    }
    return Entry(**{**base, **changes})


# -- the state files


def test_a_state_says_which_pcsx2_saved_it_and_when(standard_state: Path) -> None:
    assert pcsx2_state.read_version(standard_state) == "v2.9.94"
    assert pcsx2_state.saved_on(standard_state) == "2026-10-05"
    assert len(pcsx2_state.read_ee_memory(standard_state)) == pcsx2_state.EE_MEMORY_SIZE


def test_a_file_that_is_not_a_state_is_refused_and_has_no_version(tmp_path: Path) -> None:
    other = tmp_path / "other.p2s"
    with zipfile.ZipFile(other, "w") as archive:
        archive.writestr("readme.txt", "no")
    assert pcsx2_state.read_version(other) is None
    assert pcsx2_state.saved_on(other) is None
    with pytest.raises(pcsx2_state.StateError, match=r"has no eeMemory\.bin"):
        pcsx2_state.read_ee_memory(other)
    (tmp_path / "junk.p2s").write_bytes(b"junk")
    with pytest.raises(pcsx2_state.StateError, match="not a readable PCSX2 save state"):
        pcsx2_state.read_ee_memory(tmp_path / "junk.p2s")


# -- reading a game with the recipes


class FakePine:
    """The fake PINE of these tests: a made-up game's RAM behind `Memory`, counting the writes it is sent."""

    def __init__(self, ram: bytes | bytearray) -> None:
        """A game whose RAM is `ram`."""
        self.ram = ram
        self.writes = 0

    def batch(self, reads: Sequence[Read], writes: Sequence[Write] = ()) -> list[int]:
        """Reads only: a write is counted and ignored."""
        self.writes += len(writes)
        return [int.from_bytes(self.ram[r.address : r.address + r.size], "little") for r in reads]

    def __enter__(self) -> FakePine:
        """Used as PineClient is, in a `with`."""
        return self

    def __exit__(self, *_: object) -> None:
        """Nothing to close."""


@pytest.fixture(scope="module")
def book() -> recipes.RecipeBook:
    """The repository's recipes."""
    return recipes.load(ROOT)


def test_the_scene_is_read_with_the_where_and_position_recipes(book: recipes.RecipeBook) -> None:
    ram = make_ram("level12", 5, (3.0, 4.5, -1.25), 135.0)
    for memory in (StateMemory(bytes(ram)), FakePine(ram)):
        scene = read_scene(memory, book)
        assert (scene.level, scene.checkpoint) == ("level12", 5)
        assert scene.position == pytest.approx((3.0, 4.5, -1.25))
        assert scene.heading_deg == pytest.approx(135.0, abs=1e-3)
        assert scene.notes == ()
    assert ram == make_ram("level12", 5, (3.0, 4.5, -1.25), 135.0)


def test_a_game_with_no_level_gives_notes_and_no_guesses(book: recipes.RecipeBook) -> None:
    scene = read_scene(StateMemory(bytes(make_ram(in_level=False))), book)
    assert (scene.level, scene.checkpoint, scene.position, scene.heading_deg) == (None, None, None, None)
    assert len(scene.notes) == 2


def test_a_state_memory_is_only_read() -> None:
    memory = StateMemory(bytes(16))
    assert memory.batch([Read(4, 4)]) == [0]
    with pytest.raises(PineError, match="only read"):
        memory.batch([], [Write(0, 4, 1)])
    with pytest.raises(PineError, match="outside"):
        memory.batch([Read(14, 4)])


def test_patches_and_hooks_in_a_state_are_found(book: recipes.RecipeBook) -> None:
    patches = pcsx2_state.load_patches(ROOT / recipes.PATCHES_DIR)
    hooks = hook_tools.load_hooks(ROOT / recipes.PATCHES_DIR)
    ram = make_ram()
    assert pcsx2_states.detect_patches(StateMemory(bytes(ram)), patches, hooks) == ()
    pad = patches["scripted-pad"]
    for edit in pad.edits:
        ram[edit.address : edit.address + len(edit.data)] = edit.data
    hook = hooks["call-brains"]
    for edit in hook_tools.build([hook]).edits:
        ram[edit.address : edit.address + len(edit.data)] = edit.data
    found = pcsx2_states.detect_patches(StateMemory(bytes(ram)), patches, hooks)
    assert "scripted-pad" in found
    assert "call-brains" in found
    assert "right-stick" not in found


# -- the library


def test_the_library_is_beside_the_repository_by_default_and_follows_the_config(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    root = tmp_path / "repos" / "coney"
    root.mkdir(parents=True)
    (root / "coney.local.example.toml").write_text("", encoding="utf-8")
    monkeypatch.chdir(root)
    assert pcsx2_states.library_dir() == (tmp_path / "emulators" / "state-library").resolve()
    (root / "coney.local.toml").write_text('pcsx2_states_dir = "../../elsewhere/states"\n', encoding="utf-8")
    assert pcsx2_states.library_dir() == (tmp_path / "elsewhere" / "states").resolve()
    assert load_config(root).paths["pcsx2_states_dir"] == (tmp_path / "elsewhere" / "states").resolve()


def test_adding_copies_the_file_hashes_it_and_makes_it_read_only(tmp_path: Path, state_file: Path) -> None:
    library = Library(tmp_path / "library")
    before = state_file.read_bytes()
    added = library.add(entry(), state_file)

    stored = library.state_path("pedestrian-grab")
    assert stored.read_bytes() == before
    assert (added.size, added.sha256) == (len(before), pcsx2_copies.sha256(stored))
    assert not stored.stat().st_mode & stat.S_IWUSR
    assert state_file.read_bytes() == before
    assert state_file.stat().st_mode & stat.S_IWUSR
    assert library.get("pedestrian-grab") == added
    assert not (tmp_path / "library" / pcsx2_states.LOCK_FILE).exists()


def test_an_id_is_never_added_twice_or_over_a_stray_file(tmp_path: Path, state_file: Path) -> None:
    library = Library(tmp_path / "library")
    library.add(entry(), state_file)
    with pytest.raises(LibraryError, match="already in the library"):
        library.add(entry(description="another"), state_file)
    (tmp_path / "library" / "stray.p2s").write_bytes(b"x")
    with pytest.raises(LibraryError, match="not in the catalog"):
        library.add(entry("stray"), state_file)
    assert [e.id for e in library.entries()] == ["pedestrian-grab"]


@pytest.mark.parametrize("name", ["", "Upper", "has space", "-lead", "a/b", "x" * 49, "dot.p2s"])
def test_ids_are_short_slugs(name: str) -> None:
    with pytest.raises(LibraryError, match="short slug"):
        pcsx2_states.check_slug(name)


def test_states_are_found_by_level_and_tag(tmp_path: Path, state_file: Path) -> None:
    library = Library(tmp_path / "library")
    library.add(entry("a", level="level99", tags=("grab", "street")), state_file)
    library.add(entry("b", level="level12", tags=("street",)), state_file)
    library.add(entry("c", level="level99", tags=()), state_file)
    assert [e.id for e in library.find()] == ["a", "b", "c"]
    assert [e.id for e in library.find(level="LEVEL99")] == ["a", "c"]
    assert [e.id for e in library.find(tag="street")] == ["a", "b"]
    assert [e.id for e in library.find(level="level99", tag="street")] == ["a"]
    with pytest.raises(LibraryError, match="there are: a, b, c"):
        library.get("d")


def test_the_catalog_round_trips_every_field(tmp_path: Path, state_file: Path) -> None:
    library = Library(tmp_path / "library")
    full = entry(
        position=(1.0, 2.0, 3.0), heading_deg=45.5, pcsx2="v2.9.94", patches=("scripted-pad",), made_by="slot 6"
    )
    library.add(full, state_file)
    plain = entry("plain", level=pcsx2_states.UNKNOWN, checkpoint=None)
    library.add(plain, state_file)
    again = Library(tmp_path / "library").entries()
    assert again[0] == library.get("pedestrian-grab")
    assert again[0].position == (1.0, 2.0, 3.0)
    assert (again[1].checkpoint, again[1].position, again[1].heading_deg) == (None, None, None)
    data = json.loads(library.catalog_path.read_text(encoding="utf-8"))
    assert data["format"] == 1
    assert data["states"][0]["patches"] == ["scripted-pad"]


@pytest.mark.parametrize(
    ("text", "message"),
    [
        ("{", "catalog.json"),
        ('{"format": 2, "states": []}', "not a state catalog"),
        ('{"format": 1, "states": {}}', "not a state catalog"),
        ('{"format": 1, "states": [5]}', "an entry is an object"),
        ('{"format": 1, "states": [{"id": "a"}]}', "missing"),
        (
            '{"format": 1, "states": [{"id": "a", "level": "l", "description": "d", "sha256": "", "size": 1, "x": 1}]}',
            "unknown field",
        ),
        (
            '{"format": 1, "states": [{"id": "A", "level": "l", "description": "d", "sha256": "", "size": 1}]}',
            "short slug",
        ),
        (
            '{"format": 1, "states": [{"id": "a", "level": "l", "description": "d", "sha256": "", "size": "big"}]}',
            "wrong type",
        ),
    ],
)
def test_a_bad_catalog_is_named(tmp_path: Path, text: str, message: str) -> None:
    (tmp_path / "catalog.json").write_text(text, encoding="utf-8")
    with pytest.raises(LibraryError, match=message):
        Library(tmp_path).entries()


def test_an_id_twice_in_the_catalog_is_refused(tmp_path: Path) -> None:
    row = {"id": "a", "level": "l", "description": "d", "sha256": "", "size": 1}
    (tmp_path / "catalog.json").write_text(json.dumps({"format": 1, "states": [row, row]}), encoding="utf-8")
    with pytest.raises(LibraryError, match="appears twice"):
        Library(tmp_path).entries()


def test_an_empty_library_lists_nothing(tmp_path: Path) -> None:
    assert Library(tmp_path / "nowhere").entries() == []


def test_a_held_lock_blocks_and_a_dead_one_is_taken_over(tmp_path: Path, state_file: Path) -> None:
    library = Library(tmp_path / "library")
    library.path.mkdir()
    lock = library.path / pcsx2_states.LOCK_FILE
    lock.write_text("", encoding="utf-8")
    with pytest.raises(LibraryError, match="held by another run"), library.locked(timeout=0.2):
        pass
    old = lock.stat().st_mtime - 3600
    os.utime(lock, (old, old))
    library.add(entry(), state_file)
    assert not lock.exists()


def test_verify_finds_a_changed_missing_or_unlisted_file(tmp_path: Path, state_file: Path) -> None:
    library = Library(tmp_path / "library")
    library.add(entry("a"), state_file)
    library.add(entry("b"), state_file)
    library.add(entry("c"), state_file)
    report = library.verify()
    assert (report.checked, report.problems, report.notes) == (3, [], [])

    library.state_path("a").chmod(stat.S_IWRITE | stat.S_IREAD)
    data = bytearray(library.state_path("a").read_bytes())
    data[100] ^= 1
    library.state_path("a").write_bytes(bytes(data))
    library.state_path("b").chmod(stat.S_IWRITE | stat.S_IREAD)
    library.state_path("b").unlink()
    library.state_path("c").chmod(stat.S_IWRITE | stat.S_IREAD)
    (tmp_path / "library" / "stray.p2s").write_bytes(b"x")
    report = library.verify()
    assert [p.split(":")[0] for p in report.problems] == ["a", "b", "stray.p2s"]
    assert "SHA-256 differs" in report.problems[0]
    assert "missing" in report.problems[1]
    assert any("c: the file is writable" in n for n in report.notes)

    assert library.verify(["c"]).problems == []
    with pytest.raises(LibraryError, match="no state zzz"):
        library.verify(["zzz"])


# -- the commands


def run(*argv: str) -> int:
    """`coney-tools pcsx2 states ...` through the real parser."""
    return main(["pcsx2", "states", *argv])


def test_add_reads_the_scene_from_the_state_and_list_and_show_report_it(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], state_file: Path
) -> None:
    make_checkout(tmp_path, monkeypatch)
    before = state_file.read_bytes()
    assert run("add", str(state_file), "--id", "street-end", "--desc", "street end,  clear path to run",
               "--tag", "Run", "--tag", "street", "--made-by", "the maintainer's quick-save slot 1",
               "--author", "maintainer") == 0  # fmt: skip
    out = capsys.readouterr().out
    assert "read from the state's own memory" in out
    assert state_file.read_bytes() == before

    library = Library(tmp_path / "library")
    added = library.get("street-end")
    assert (added.level, added.checkpoint) == ("level99", 3)
    assert added.position == pytest.approx((1.5, -20.0, 0.25))
    assert added.heading_deg == pytest.approx(90.0, abs=1e-3)
    assert (added.pcsx2, added.made, added.author, added.patches) == ("v2.9.94", "2026-10-05", "maintainer", ())
    assert added.tags == ("run", "street")
    assert added.description == "street end, clear path to run"

    assert run("list") == 0
    listed = capsys.readouterr().out
    assert "street-end" in listed
    assert "level99" in listed
    assert "cp 3" in listed
    assert run("list", "--level", "level12") == 0
    assert "no states" in capsys.readouterr().out
    assert run("list", "--tag", "run", "--json") == 0
    assert json.loads(capsys.readouterr().out)[0]["id"] == "street-end"
    assert run("show", "street-end") == 0
    shown = capsys.readouterr().out
    assert "x 1.500, y -20.000, z 0.250; heading 90.0 degrees" in shown
    assert added.sha256 in shown
    assert run("show", "street-end", "--json") == 0
    assert json.loads(capsys.readouterr().out)["sha256"] == added.sha256


def test_flags_override_what_the_state_says_and_unknown_is_allowed(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, state_file: Path
) -> None:
    make_checkout(tmp_path, monkeypatch)
    assert run("add", str(state_file), "--id", "given", "--desc", "d", "--author", "me", "--level", "level7",
               "--checkpoint", "unknown", "--position", "1,2,3", "--heading", "10", "--pcsx2", "v2.9.129",
               "--made", "2026-01-02") == 0  # fmt: skip
    given = Library(tmp_path / "library").get("given")
    assert (given.level, given.checkpoint, given.position, given.heading_deg) == ("level7", None, (1.0, 2.0, 3.0), 10.0)
    assert (given.pcsx2, given.made) == ("v2.9.129", "2026-01-02")
    assert run("add", str(state_file), "--id", "blind", "--desc", "d", "--author", "me", "--no-read",
               "--level", "unknown") == 0  # fmt: skip
    blind = Library(tmp_path / "library").get("blind")
    assert (blind.level, blind.checkpoint, blind.position, blind.patches) == ("unknown", None, None, ())


def test_a_position_is_kept_to_the_millimetre(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    make_checkout(tmp_path, monkeypatch)
    precise = make_state(
        tmp_path / "precise.p2s", make_ram(position=(75.15495300292969, -41.13535690307617, 0.2230907))
    )
    assert run("add", str(precise), "--id", "precise", "--desc", "d", "--author", "me") == 0
    assert Library(tmp_path / "library").get("precise").position == (75.155, -41.135, 0.223)


def test_add_refuses_what_it_cannot_describe_or_already_holds(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], state_file: Path
) -> None:
    make_checkout(tmp_path, monkeypatch)
    front_end = make_state(tmp_path / "front.p2s", make_ram(in_level=False))
    assert run("add", str(front_end), "--id", "front", "--desc", "d", "--author", "me") == 2
    assert "pass --level" in capsys.readouterr().err
    assert not Library(tmp_path / "library").entries()

    assert run("add", str(state_file), "--id", "one", "--desc", "d", "--author", "me") == 0
    assert run("add", str(state_file), "--id", "one", "--desc", "d", "--author", "me") == 2
    assert "already in the library" in capsys.readouterr().err
    assert run("add", str(state_file), "--id", "Bad Id", "--desc", "d", "--author", "me") == 2
    assert run("add", str(state_file), "--id", "two", "--desc", "d") == 2
    assert "say who made the state" in capsys.readouterr().err
    assert run("add", str(tmp_path / "missing.p2s"), "--id", "three", "--desc", "d", "--author", "me") == 2
    assert run("add", str(state_file), "--id", "four", "--desc", "d", "--author", "me", "--checkpoint", "x") == 2
    assert run("add", str(state_file), "--id", "five", "--desc", "d", "--author", "me", "--position", "1,2") == 2
    assert run("add", str(state_file), "--id", "six", "--desc", "d", "--author", "me", "--tag", "Bad Tag") == 2
    assert [e.id for e in Library(tmp_path / "library").entries()] == ["one"]
    notstate = tmp_path / "notstate.p2s"
    notstate.write_bytes(b"junk")
    assert run("add", str(notstate), "--id", "seven", "--desc", "d", "--author", "me", "--level", "level99") == 2


def test_add_records_the_patches_a_state_holds(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    make_checkout(tmp_path, monkeypatch)
    ram = make_ram()
    for edit in pcsx2_state.load_patches(ROOT / recipes.PATCHES_DIR)["scripted-pad"].edits:
        ram[edit.address : edit.address + len(edit.data)] = edit.data
    patched = make_state(tmp_path / "patched.p2s", ram)
    assert run("add", str(patched), "--id", "patched", "--desc", "d", "--author", "me") == 0
    assert "scripted-pad" in Library(tmp_path / "library").get("patched").patches
    assert "scripted-pad" in capsys.readouterr().out


class StubRegistry:
    """A claims registry that hands out one made-up copy to the agent `holder`, with PINE port 4242."""

    def __init__(self, folder: Path, holder: str = "me") -> None:
        """The copy lives in `folder`."""
        self.folder = folder
        self.holder = holder

    def require_claim(self, agent: str, name: str) -> types.SimpleNamespace:
        """The claim, or the refusal a real registry makes."""
        if agent != self.holder:
            raise ConfigError(f"{name} is held by {self.holder}, not {agent}")
        return types.SimpleNamespace(path=str(self.folder), copy=name)

    def copy(self, name: str) -> types.SimpleNamespace:
        """The copy's ports."""
        return types.SimpleNamespace(name=name, port=4242, debug=None)


def test_add_over_pine_reads_the_running_copy_under_a_claim(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, state_file: Path
) -> None:
    make_checkout(tmp_path, monkeypatch)
    live = make_ram("level50", 2, (9.0, 8.0, 7.0), 180.0)
    opened: list[int] = []

    def connect(port: int) -> FakePine:
        opened.append(port)
        return FakePine(live)

    monkeypatch.setattr(pcsx2_claims, "default_registry", lambda: StubRegistry(tmp_path / "pcsx2-x"))
    monkeypatch.setattr(pcsx2_states_cli, "PineClient", connect)
    assert run("add", str(state_file), "--id", "live", "--desc", "d", "--copy", "pcsx2-x", "--agent", "me") == 0
    got = Library(tmp_path / "library").get("live")
    assert opened == [4242]
    assert (got.level, got.checkpoint, got.author) == ("level50", 2, "me")
    assert got.position == pytest.approx((9.0, 8.0, 7.0))
    assert got.heading_deg == pytest.approx(180.0, abs=1e-3)

    # Not under your claim, or without an agent at all: nothing is read and nothing added.
    assert run("add", str(state_file), "--id", "other", "--desc", "d", "--copy", "pcsx2-x", "--agent", "you") == 2
    assert run("add", str(state_file), "--id", "other", "--desc", "d", "--copy", "pcsx2-x", "--author", "me") == 2
    assert [e.id for e in Library(tmp_path / "library").entries()] == ["live"]


def test_a_copy_that_cannot_be_reached_is_a_clean_error(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], state_file: Path
) -> None:
    make_checkout(tmp_path, monkeypatch)

    def refuse(port: int) -> FakePine:
        raise PineError("connection refused")

    monkeypatch.setattr(pcsx2_claims, "default_registry", lambda: StubRegistry(tmp_path / "pcsx2-x"))
    monkeypatch.setattr(pcsx2_states_cli, "PineClient", refuse)
    assert run("add", str(state_file), "--id", "x", "--desc", "d", "--copy", "pcsx2-x", "--agent", "me") == 2
    assert "pcsx2-x: connection refused" in capsys.readouterr().err
    assert not Library(tmp_path / "library").entries()


def test_a_game_that_answers_nothing_leaves_the_level_to_be_given(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], state_file: Path
) -> None:
    make_checkout(tmp_path, monkeypatch)

    class Broken(FakePine):
        def batch(self, reads: Sequence[Read], writes: Sequence[Write] = ()) -> list[int]:
            raise PineError("PCSX2 refused the command (is a game running?)")

    monkeypatch.setattr(pcsx2_claims, "default_registry", lambda: StubRegistry(tmp_path / "pcsx2-x"))
    monkeypatch.setattr(pcsx2_states_cli, "PineClient", lambda port: Broken(b""))
    # The recipes' reads fail, so the scene is empty and the level has to be given.
    assert run("add", str(state_file), "--id", "x", "--desc", "d", "--copy", "pcsx2-x", "--agent", "me") == 2
    assert "the level could not be read" in capsys.readouterr().err
    assert run("add", str(state_file), "--id", "x", "--desc", "d", "--copy", "pcsx2-x", "--agent", "me",
               "--level", "unknown") == 0  # fmt: skip
    assert Library(tmp_path / "library").get("x").position is None


def add_standard(tmp_path: Path, state_file: Path, state_id: str = "street") -> None:
    """Add the standard state to the checkout's library."""
    assert run("add", str(state_file), "--id", state_id, "--desc", "d", "--author", "me") == 0


def test_hand_out_hands_out_a_copy_and_leaves_the_library_file_alone(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], state_file: Path
) -> None:
    make_checkout(tmp_path, monkeypatch)
    add_standard(tmp_path, state_file)
    library = Library(tmp_path / "library")
    stored = library.state_path("street")
    before = (stored.read_bytes(), stored.stat().st_mtime_ns)
    copy_folder = tmp_path / "ws" / "emulators" / "pcsx2-x"
    monkeypatch.setattr(pcsx2_claims, "default_registry", lambda: StubRegistry(copy_folder))
    capsys.readouterr()

    assert run("hand-out", "street", "--copy", "pcsx2-x", "--agent", "me") == 0
    handed = copy_folder / "library-states" / "street.p2s"
    assert handed.read_bytes() == before[0]
    assert handed.stat().st_mode & stat.S_IWUSR
    assert (stored.read_bytes(), stored.stat().st_mtime_ns) == before
    out = capsys.readouterr().out
    assert str(handed) in out
    assert "saved by PCSX2 v2.9.94" in out
    assert "pcsx2 launch --agent me" in out

    # Played on and used again: the file is replaced by a fresh copy, the library's still the same.
    handed.write_bytes(b"played on")
    assert run("hand-out", "street", "--copy", "pcsx2-x", "--agent", "me") == 0
    assert handed.read_bytes() == before[0]
    assert stored.read_bytes() == before[0]


def test_hand_out_needs_the_claim_a_known_id_and_an_intact_file(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], state_file: Path
) -> None:
    make_checkout(tmp_path, monkeypatch)
    add_standard(tmp_path, state_file)
    copy_folder = tmp_path / "pcsx2-x"
    monkeypatch.setattr(pcsx2_claims, "default_registry", lambda: StubRegistry(copy_folder))
    capsys.readouterr()
    assert run("hand-out", "street", "--copy", "pcsx2-x", "--agent", "you") == 2
    assert "held by me" in capsys.readouterr().err
    assert run("hand-out", "nothing", "--copy", "pcsx2-x", "--agent", "me") == 2
    assert "no state 'nothing'" in capsys.readouterr().err

    stored = Library(tmp_path / "library").state_path("street")
    stored.chmod(stat.S_IWRITE | stat.S_IREAD)
    data = bytearray(stored.read_bytes())
    data[50] ^= 1
    stored.write_bytes(bytes(data))
    assert run("hand-out", "street", "--copy", "pcsx2-x", "--agent", "me") == 2
    assert "does not match the catalog" in capsys.readouterr().err
    assert not (copy_folder / "library-states").exists()


def test_hand_out_applies_patches_to_the_copy_only(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], state_file: Path
) -> None:
    root = make_checkout(tmp_path, monkeypatch)
    (root / recipes.PATCHES_DIR / "demo.toml").write_text(
        '[patch.demo]\ndescription = "a made-up patch"\nresearch = "docs/guides/research-workflow.md"\n'
        'edits = [ { address = 0x100, bytes = "01 02 03 04" } ]\n',
        encoding="utf-8",
    )
    add_standard(tmp_path, state_file)
    copy_folder = tmp_path / "pcsx2-x"
    monkeypatch.setattr(pcsx2_claims, "default_registry", lambda: StubRegistry(copy_folder))
    stored = Library(tmp_path / "library").state_path("street")
    before = stored.read_bytes()

    assert run("hand-out", "street", "--copy", "pcsx2-x", "--agent", "me", "--patch", "demo") == 0
    handed = copy_folder / "library-states" / "street.p2s"
    assert pcsx2_state.read_ee_memory(handed)[0x100:0x104] == bytes([1, 2, 3, 4])
    assert pcsx2_state.read_ee_memory(stored)[0x100:0x104] == bytes(4)
    assert stored.read_bytes() == before
    assert "1 edit(s) from demo" in capsys.readouterr().out

    # A patch the state cannot take (its original words are not there) is refused, and nothing is left behind.
    patched = handed.read_bytes()
    assert run("hand-out", "street", "--copy", "pcsx2-x", "--agent", "me", "--patch", "scripted-pad") == 2
    assert run("hand-out", "street", "--copy", "pcsx2-x", "--agent", "me", "--patch", "nosuch") == 2
    assert handed.read_bytes() == patched
    assert sorted(p.name for p in handed.parent.iterdir()) == ["street.p2s"]


def test_hand_out_can_launch_the_copy(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, state_file: Path) -> None:
    make_checkout(tmp_path, monkeypatch)
    add_standard(tmp_path, state_file)
    copy_folder = tmp_path / "pcsx2-x"
    monkeypatch.setattr(pcsx2_claims, "default_registry", lambda: StubRegistry(copy_folder))
    launched: list[tuple[Any, ...]] = []

    def record_launch(*args: Any) -> int:
        launched.append(args)
        return 0

    monkeypatch.setattr(pcsx2_cli, "run_launch", record_launch)
    assert run("hand-out", "street", "--copy", "pcsx2-x", "--agent", "me", "--launch") == 0
    assert launched == [(copy_folder / "library-states" / "street.p2s", copy_folder, None, None, "me")]


def test_verify_exits_1_on_a_problem(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], state_file: Path
) -> None:
    make_checkout(tmp_path, monkeypatch)
    add_standard(tmp_path, state_file)
    assert run("verify") == 0
    assert "1 state(s) checked" in capsys.readouterr().out
    stored = Library(tmp_path / "library").state_path("street")
    stored.chmod(stat.S_IWRITE | stat.S_IREAD)
    stored.write_bytes(b"changed")
    assert run("verify") == 1
    assert "PROBLEM street" in capsys.readouterr().out
    assert run("verify", "other") == 2


def test_the_list_block_is_rendered_between_the_markers(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, state_file: Path
) -> None:
    root = make_checkout(tmp_path, monkeypatch)
    assert run("add", str(state_file), "--id", "street", "--desc", "pipes | and text", "--author", "me") == 0
    page = tmp_path / "page.md"
    page.write_text(f"intro\n\n{pcsx2_states_cli.BEGIN}\nold\n{pcsx2_states_cli.END}\n\nafter\n", encoding="utf-8")
    assert run("render", "--out", str(page)) == 0
    text = page.read_text(encoding="utf-8")
    assert text.startswith("intro\n\n" + pcsx2_states_cli.BEGIN)
    assert "| `street` | level99 | 3 | pipes \\| and text |  |" in text
    assert "old" not in text
    assert text.endswith(f"{pcsx2_states_cli.END}\n\nafter\n")
    page.write_text("no markers\n", encoding="utf-8")
    assert run("render", "--out", str(page)) == 2
    assert not root.joinpath(pcsx2_states_cli.PAGE).exists()


def test_the_library_is_never_inside_the_repository(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, state_file: Path
) -> None:
    root = make_checkout(tmp_path, monkeypatch)
    with pytest.raises(ConfigError, match="inside the repository"):
        Library(root / "state-library").add(entry(), state_file)
    assert not (root / "state-library").exists()


def test_a_lock_another_run_took_over_is_not_removed(tmp_path: Path) -> None:
    library = Library(tmp_path / "library")
    with library.locked():
        (tmp_path / "library" / pcsx2_states.LOCK_FILE).write_text("someone else's", encoding="ascii")
    assert (tmp_path / "library" / pcsx2_states.LOCK_FILE).read_text(encoding="ascii") == "someone else's"


def test_a_catalog_list_must_be_a_list(tmp_path: Path) -> None:
    row = {"id": "a", "level": "l", "description": "d", "sha256": "", "size": 1, "tags": "grab"}
    (tmp_path / "catalog.json").write_text(json.dumps({"format": 1, "states": [row]}), encoding="utf-8")
    with pytest.raises(LibraryError, match="tags is a list of strings"):
        Library(tmp_path).entries()


def test_a_game_with_no_player_gives_no_position(book: recipes.RecipeBook) -> None:
    ram = make_ram()
    # A player whose transform holds NaN: not a position.
    ram[TRANSFORM_TABLE + 2 * 0x20 : TRANSFORM_TABLE + 2 * 0x20 + 4] = (0x7FC00000).to_bytes(4, "little")
    scene = read_scene(StateMemory(bytes(ram)), book)
    assert (scene.level, scene.position, scene.heading_deg) == ("level99", None, None)
    assert "not finite" in scene.notes[0]


def test_add_checks_what_the_flags_say(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, state_file: Path) -> None:
    make_checkout(tmp_path, monkeypatch)
    base = ["add", str(state_file), "--desc", "d", "--author", "me"]
    assert run(*base, "--id", "a", "--level", "not a level") == 2
    assert run(*base, "--id", "b", "--made", "5 October") == 2
    assert run(*base, "--id", "c", "--position", "nan,0,0") == 2
    assert run(*base, "--id", "d", "--tag", "grab", "--level", "unknown", "--no-read") == 0
    assert run("add", "slot:x", "--id", "e", "--desc", "d", "--author", "me") == 2
    assert [e.id for e in Library(tmp_path / "library").entries()] == ["d"]


def test_no_read_says_the_patches_were_not_looked_for(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], state_file: Path
) -> None:
    make_checkout(tmp_path, monkeypatch)
    assert (
        run("add", str(state_file), "--id", "a", "--desc", "d", "--author", "me", "--level", "unknown", "--no-read")
        == 0
    )
    assert "unchecked" in capsys.readouterr().out


def test_running_a_recipe_with_a_start_state_names_it(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], book: recipes.RecipeBook
) -> None:
    def no_registry() -> None:
        raise ConfigError("stop here")

    monkeypatch.setattr(pcsx2_claims, "default_registry", no_registry)
    with pytest.raises(ConfigError, match="stop here"):
        recipes_cli.run_recipe(book, "spawn-human", "pcsx2-z", "me", [], False)
    assert "states hand-out pedestrian-grab --copy pcsx2-z --agent me --launch" in capsys.readouterr().out


# -- recipes name a library state


def recipe_raw(**extra: Any) -> dict[str, Any]:
    """A made-up recipe table."""
    return {
        "summary": "s",
        "how": "h",
        "research": ["docs/research/characters.md"],
        "ghidra": ["0x00228730 Humans_StaticInit"],
        "evidence": "confirmed-code",
        "confirmed_runtime": {"date": "2026-10-21", "copy": "pcsx2-d", "pcsx2": "2.9.129", "state": "test"},
        "steps": [{"show": "1"}],
        **extra,
    }


def test_a_recipe_can_name_its_starting_state_and_the_page_says_so() -> None:
    recipe = recipes.parse_recipe("r", recipe_raw(state="pedestrian-grab"), ["Other"], set())
    assert recipe.state == "pedestrian-grab"
    assert recipes.parse_recipe("r", recipe_raw(), ["Other"], set()).state == ""
    assert "start state: `pedestrian-grab`" in recipes_render._preconditions(recipe)
    assert "start state" not in recipes_render._preconditions(recipes.parse_recipe("r", recipe_raw(), ["Other"], set()))


@pytest.mark.parametrize("bad", ["Not A Slug", "x" * 60, 5, "-a"])
def test_a_recipe_state_must_be_a_library_id(bad: object) -> None:
    with pytest.raises(recipes.RecipeError, match="id of a library state"):
        recipes.parse_recipe("r", recipe_raw(state=bad), ["Other"], set())


def test_the_repository_recipes_name_valid_states() -> None:
    named = [r for r in recipes.load(ROOT).recipes.values() if r.state]
    assert named, "at least one recipe starts from a library state"
    assert all(pcsx2_states.SLUG.match(r.state) for r in named)


def test_the_states_page_exists_and_the_recipe_page_links_it() -> None:
    page = (ROOT / "docs" / "guides" / "pcsx2-states.md").read_text(encoding="utf-8")
    assert pcsx2_states_cli.BEGIN in page
    assert pcsx2_states_cli.END in page
    listed = {line.split("`")[1] for line in page.splitlines() if line.startswith("| `")}
    assert {r.state for r in recipes.load(ROOT).recipes.values() if r.state} <= listed
