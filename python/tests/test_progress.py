# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the progress tracker, on a made-up checkout: invented ranges, functions and a tiny synthetic ELF."""

import json
import struct
from pathlib import Path

import pytest

from coney_tools import elf, progress, progress_cli, progress_render, progress_research
from coney_tools.cli import main
from coney_tools.config import ConfigError

# A made-up .text of 0x1000 bytes: 0xC00 of game code (two subsystems of 0x200, one with no ranges, the rest
# unattributed) and 0x400 of middleware at the end.
TOTALS = """
[text]
start = 0x1000
end = 0x2000

[[coverage]]
key = "file"
label = "File"
kind = "researched"
bytes = 0x400

[[coverage]]
key = "unknown"
label = "Unknown"
kind = "game"
bytes = 0x800

[[coverage]]
key = "libs"
label = "Libraries"
kind = "middleware"
bytes = 0x400

[[subsystem]]
name = "Alpha"
ranges = [[0x1000, 0x1200]]

[[subsystem]]
name = "Beta"
ranges = [[0x1200, 0x1400]]

[[subsystem]]
name = "Empty"
ranges = []

[[middleware]]
name = "SomeLib"
bytes = 0x400
replaced_by = "a new one"
ranges = [[0x1c00, 0x2000]]
"""

ROADMAP = """# Roadmap

| Milestone | Status |
| --- | --- |
| [Start](#start) | done |
| [Middle](#middle) | in progress |
| [End](#end) | not started |
"""

# The committed progress page: hand-written text around the marker the docs build fills.
BLOCK = f"before\n\n{progress_render.PAGE_MARKER}\n\nafter\n"
# The committed history page, with its own marker.
HISTORY_BLOCK = f"history\n\n{progress_render.HISTORY_MARKER}\n"


def make_checkout(root: Path, functions: str = "", tags: str = "", listing: str = "") -> Path:
    """A made-up checkout with every input of the tracker.

    `functions` is the text of a function database file (research/functions/alpha.toml), `tags` the text of a source
    file, `listing` the Ghidra listing that gives the functions their sizes (none: no listing file).
    """
    root.mkdir(parents=True, exist_ok=True)
    (root / "coney.local.example.toml").write_text("", encoding="utf-8")
    (root / "docs" / "progress").mkdir(parents=True)
    (root / "docs" / "progress" / "totals.toml").write_text(TOTALS, encoding="utf-8")
    if functions:
        (root / "research" / "functions").mkdir(parents=True)
        (root / "research" / "functions" / "alpha.toml").write_text(functions, encoding="utf-8")
    if listing:
        (root / "docs" / "progress" / "ghidra-functions.tsv").write_text(listing, encoding="utf-8")
    (root / "docs" / "progress" / "index.md").write_text(BLOCK, encoding="utf-8")
    (root / "docs" / "progress" / "history.md").write_text(HISTORY_BLOCK, encoding="utf-8")
    (root / "docs" / "roadmap.md").write_text(ROADMAP, encoding="utf-8")
    (root / "src" / "core").mkdir(parents=True)
    (root / "src" / "core" / "thing.h").write_text(tags, encoding="utf-8")
    return root


def entry(address: int, name: str, coney: str = "", extra: str = "") -> str:
    """A function database entry for the tests: a page and a level, `coney` when given, `extra` lines after them."""
    text = (
        f'[[function]]\naddress = {address:#010x}\nname = "{name}"\npage = "alpha.md#thing"\nlevel = "confirmed-code"\n'
    )
    if coney:
        text += f'coney = "{coney}"\nstatus = "legacy"\n'
    return text + extra + "\n"


# One ported function in Alpha, the header that tags it, and the listing that gives it 0x80 bytes.
ONE_FUNCTION = entry(0x1010, "Thing::Do", "src/core/thing.h:doThing")
ONE_LISTING = "0x00001010\t128\tThing::Do\t0\n"
ONE_TAG = "/// Does the thing.\n/// @orig 0x00001010 Thing::Do (Thing.cpp)\nvoid doThing();\n"


# --- rendering --------------------------------------------------------------------------------------------------------


def test_bar_fills_in_eighths() -> None:
    assert progress_render.bar(0, 10, width=4) == "`░░░░`"
    assert progress_render.bar(10, 10, width=4) == "`████`"
    assert progress_render.bar(5, 10, width=4) == "`██░░`"
    assert progress_render.bar(1, 16, width=2) == "`▏░`"
    assert progress_render.bar(1, 10**9, width=4) == "`▏░░░`"  # any progress at all shows
    assert progress_render.bar(1, 0) == "`" + "░" * progress_render.BAR_WIDTH + "`"


def test_percent_and_badge() -> None:
    """Percentages show one decimal, "<0.1%" or "n/a"; badges escape `-` and `%` and colour by share."""
    assert progress_render.percent(0, 100) == "0.0%"
    assert progress_render.percent(1, 100_000) == "<0.1%"
    assert progress_render.percent(1, 0) == "n/a"
    assert progress_render.badge_url("re-done", 0, 10).endswith("/re--done-0.0%25-lightgrey")
    assert progress_render.badge_url("done", 5, 10).endswith("/done-50.0%25-yellow")
    assert progress_render.badge_url("done", 10, 10).endswith("-green")


def test_fill_page_needs_exactly_one_marker() -> None:
    filled = progress_render.fill_page(BLOCK, "new", "x.md")
    assert filled.startswith("before") and filled.endswith("after" + chr(10))
    assert progress_render.PAGE_MARKER not in filled and chr(10) + "new" + chr(10) in filled
    for text in ("no markers", BLOCK + BLOCK):
        with pytest.raises(progress.ProgressError, match=r"x\.md: needs exactly one <!-- progress:generated -->"):
            progress_render.fill_page(text, "new", "x.md")


# --- the model --------------------------------------------------------------------------------------------------------


def test_totals_and_subsystem_of(tmp_path: Path) -> None:
    """The totals add up from the coverage table, and an address maps to its subsystem, unattributed or none."""
    totals = progress.load_totals(make_checkout(tmp_path) / progress.TOTALS_FILE)
    assert totals.game_bytes == 0xC00
    assert totals.researched_bytes == 0x400
    assert totals.unattributed_bytes == 0xC00 - 0x400
    assert totals.subsystem_of(0x1100) == "Alpha"
    assert totals.subsystem_of(0x1500) == progress.UNATTRIBUTED
    assert totals.subsystem_of(0x1C00) is None  # middleware
    assert totals.subsystem_of(0x3000) is None  # outside .text


def test_totals_refuse_overlapping_ranges(tmp_path: Path) -> None:
    path = make_checkout(tmp_path) / progress.TOTALS_FILE
    path.write_text(TOTALS.replace("[[0x1200, 0x1400]]", "[[0x11f0, 0x1400]]"), encoding="utf-8")
    with pytest.raises(progress.ProgressError, match="Alpha and Beta overlap"):
        progress.load_totals(path)


def test_a_malformed_database_file_is_a_progress_error(tmp_path: Path) -> None:
    """The reimplemented list is the function database: a bad entry stops the tracker, naming the file and the key."""
    root = make_checkout(tmp_path, ONE_FUNCTION + "colour = 1\n", ONE_TAG, ONE_LISTING)
    with pytest.raises(progress.ProgressError, match=r"alpha\.toml.*unknown key 'colour'"):
        progress.load(root)


def test_milestones_come_from_the_roadmap_table() -> None:
    milestones = progress.parse_milestones(ROADMAP + "\n| [Elsewhere](other.md) | done |\n")
    assert [(m.name, m.anchor, m.status) for m in milestones] == [
        ("Start", "start", "done"),
        ("Middle", "middle", "in progress"),
        ("End", "end", "not started"),
    ]


def test_consistent_inputs_have_no_problems(tmp_path: Path) -> None:
    """Entries with `coney` are the reimplemented functions; the size is the listing's, the subsystem the range's."""
    state = progress.load(make_checkout(tmp_path, ONE_FUNCTION, ONE_TAG, ONE_LISTING))
    assert state.problems == [] and state.warnings == []
    assert state.reimplemented_bytes == 0x80
    assert [(f.address, f.subsystem, f.size, f.system, f.coney) for f in state.functions] == [
        (0x1010, "Alpha", 0x80, "alpha", "src/core/thing.h:doThing")
    ]
    rows = {name: (total, done, count) for name, total, done, count in state.subsystem_rows()}
    assert rows["Alpha"] == (0x200, 0x80, 1)
    assert rows[progress.UNATTRIBUTED] == (0x800, 0, 0)


def test_an_entry_without_coney_is_researched_not_reimplemented(tmp_path: Path) -> None:
    """The database lists researched functions too: only `coney` makes one count as reimplemented."""
    functions = ONE_FUNCTION + entry(0x1020, "Researched")
    state = progress.load(make_checkout(tmp_path, functions, ONE_TAG, ONE_LISTING))
    assert state.problems == [] and [f.name for f in state.functions] == ["Thing::Do"]


def test_a_function_the_listing_lacks_counts_with_no_size(tmp_path: Path) -> None:
    """A port is tagged where the original has a function that Ghidra may not define yet: it counts, adds no bytes."""
    state = progress.load(make_checkout(tmp_path, ONE_FUNCTION, ONE_TAG, "0x00001100\t8\tOther\t0\n"))
    assert state.problems == []
    assert [(f.size) for f in state.functions] == [None] and state.reimplemented_bytes == 0
    assert progress.load(make_checkout(tmp_path / "bare", ONE_FUNCTION, ONE_TAG)).reimplemented_bytes == 0  # no listing


def test_tags_and_entries_must_match(tmp_path: Path) -> None:
    """A tag with no entry, a tag whose entry lacks `coney`, and the other way round, are each a problem."""
    tags = ONE_TAG + "/// @orig 0x00001300 Other::Fn (unknown)\n/// @orig 0x00001020 Researched (unknown)\n"
    functions = ONE_FUNCTION + entry(0x1020, "Researched") + entry(0x1030, "Port", "src/core/thing.h:port")
    problems = progress.load(make_checkout(tmp_path, functions, tags, ONE_LISTING)).problems
    assert any("0x00001300 Other::Fn has no entry in the function database" in p and "thing.h:4" in p for p in problems)
    assert any("0x00001020 Researched" in p and "alpha.toml lists it without `coney`" in p for p in problems)
    assert any("alpha.toml: 0x00001030 Port: no @orig tag in src/ reimplements it" in p for p in problems)
    assert len(problems) == 3


def test_coney_must_name_a_file_that_carries_the_tag(tmp_path: Path) -> None:
    """The port's file is where the tag is; a tag in two files satisfies either."""
    wrong = ONE_FUNCTION.replace("src/core/thing.h", "src/core/other.h")
    problems = progress.load(make_checkout(tmp_path, wrong, ONE_TAG, ONE_LISTING)).problems
    assert problems and "coney names src/core/other.h, but its @orig tag is in src/core/thing.h" in problems[0]
    both = make_checkout(tmp_path / "two", wrong, ONE_TAG, ONE_LISTING)
    (both / "src" / "core" / "other.h").write_text(ONE_TAG, encoding="utf-8")
    assert progress.load(both).problems == []


def test_a_tag_that_spells_the_name_differently_is_a_warning(tmp_path: Path) -> None:
    """The database has Ghidra's name; an older spelling in the tag is reported, but does not fail the check."""
    state = progress.load(
        make_checkout(tmp_path, ONE_FUNCTION, ONE_TAG.replace("Thing::Do", "Thing::Did"), ONE_LISTING)
    )
    assert state.problems == []
    assert len(state.warnings) == 1 and "names 'Thing::Did', the database 'Thing::Do'" in state.warnings[0]


def test_ported_middleware_is_not_game_code(tmp_path: Path) -> None:
    """A port at an address in the middleware (or outside .text) cannot count as reimplemented game code."""
    functions = entry(0x1C10, "Lib", "src/core/thing.h:lib")
    state = progress.load(make_checkout(tmp_path, functions, "/// @orig 0x00001c10 Lib (x.cpp)\nvoid lib();\n"))
    assert any("alpha.toml: 0x00001c10 Lib: not game code" in p for p in state.problems)


# --- the commands -----------------------------------------------------------------------------------------------------


def test_check_passes_on_consistent_inputs_and_fails_otherwise(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """`check` writes nothing; it is 0 for consistent inputs and 1 with each problem listed otherwise."""
    root = make_checkout(tmp_path, ONE_FUNCTION, ONE_TAG, ONE_LISTING)
    monkeypatch.chdir(root)
    assert main(["progress", "check"]) == 0
    assert "inputs are consistent (1 functions, 3 milestones)" in capsys.readouterr().out
    assert (root / "docs" / "progress" / "index.md").read_text(encoding="utf-8") == BLOCK  # nothing generated
    # A tag with no entry is a problem.
    (root / "src" / "core" / "thing.h").write_text(ONE_TAG + "/// @orig 0x00001300 Other::Fn (unknown)\n")
    assert main(["progress", "check"]) == 1
    assert "0x00001300 Other::Fn has no entry in the function database" in capsys.readouterr().out


def test_check_prints_name_warnings_and_still_passes(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """A tag naming the function differently is printed (the first few, then a count) and does not fail the check."""
    tags = "".join(f"/// @orig {0x1010 + 4 * i:#010x} Old{i} (x.cpp)\nvoid f{i}();\n" for i in range(8))
    functions = "".join(entry(0x1010 + 4 * i, f"New{i}", "src/core/thing.h:f") for i in range(8))
    monkeypatch.chdir(make_checkout(tmp_path, functions, tags, ONE_LISTING))
    assert main(["progress", "check"]) == 0
    out = capsys.readouterr().out
    assert out.count("warning: src/core/thing.h:") == progress_cli.SHOWN_WARNINGS
    assert "warning: ... and 3 more like these" in out and "inputs are consistent (8 functions" in out


def test_render_writes_the_page_and_the_badges(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """`render` fills the page's marker and writes one endpoint JSON per badge, leaving the checkout alone."""
    root = make_checkout(tmp_path / "repo", ONE_FUNCTION, ONE_TAG, ONE_LISTING)
    monkeypatch.chdir(root)
    out = tmp_path / "out"
    assert main(["progress", "render", str(out)]) == 0
    page = (out / "progress" / "index.md").read_text(encoding="utf-8")
    assert page.startswith("before") and page.endswith("after" + chr(10)) and progress_render.PAGE_MARKER not in page
    assert "| `0x00001010` | `Thing::Do` | `Alpha` | 128 |" in page
    assert "| `Empty` |" in page and "not placed yet" in page
    assert "4.2% of the game's own code (128 of 3,072 bytes, 1 function)" in page
    assert "[End](../roadmap.md#end) | not started" in page
    badges = out / "progress" / "badges"
    expected = ["milestones.json", "reimplemented.json", "researched.json", "understood.json"]
    assert sorted(p.name for p in badges.iterdir()) == expected
    assert json.loads((badges / "reimplemented.json").read_text(encoding="utf-8")) == {
        "schemaVersion": 1,
        "label": "reimplemented",
        "message": "4.2%",
        "color": "red",
    }
    assert json.loads((badges / "milestones.json").read_text(encoding="utf-8"))["message"] == "1 of 3 done"
    assert (root / "docs" / "progress" / "index.md").read_text(encoding="utf-8") == BLOCK
    # The history page and the numbers: the listing places the one function, and today's history point is drawn.
    history = (out / "progress" / "history.md").read_text(encoding="utf-8")
    assert history.startswith("history") and progress_render.HISTORY_MARKER not in history and "<svg" in history
    assert json.loads((out / "progress" / "progress.json").read_text(encoding="utf-8"))["functions"] == 1
    assert (out / "progress" / "history.svg").is_file() and (out / "progress" / "progress.svg").is_file()
    assert (root / "docs" / "progress" / "history.md").read_text(encoding="utf-8") == HISTORY_BLOCK


def test_render_follows_the_inputs(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """The numbers come from the inputs at render time: a roadmap change moves the milestones badge."""
    root = make_checkout(tmp_path / "repo", ONE_FUNCTION, ONE_TAG, ONE_LISTING)
    monkeypatch.chdir(root)
    (root / "docs" / "roadmap.md").write_text(ROADMAP.replace("in progress", "done"), encoding="utf-8")
    assert main(["progress", "render", str(tmp_path / "out")]) == 0
    milestones = json.loads((tmp_path / "out" / "progress" / "badges" / "milestones.json").read_text(encoding="utf-8"))
    assert milestones["message"] == "2 of 3 done"


def test_render_refuses_inconsistent_inputs(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(make_checkout(tmp_path / "repo", tags=ONE_TAG))
    assert main(["progress", "render", str(tmp_path / "out")]) == 1
    assert "has no entry in the function database" in capsys.readouterr().out
    assert not (tmp_path / "out").exists()  # nothing written


def test_show_json(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
    """`show --json` reports the shares, milestones and subsystems computed from the inputs."""
    monkeypatch.chdir(make_checkout(tmp_path, ONE_FUNCTION, ONE_TAG, ONE_LISTING))
    assert main(["progress", "show", "--json"]) == 0
    data = json.loads(capsys.readouterr().out)
    assert data["reimplemented"] == {"bytes": 128, "total": 3072, "percent": 4.167, "functions": 1}
    assert data["researched"]["bytes"] == 1024
    assert [m["status"] for m in data["milestones"]] == ["done", "in progress", "not started"]
    assert data["subsystems"][-1]["name"] == "unattributed"


def test_render_can_skip_the_history_walk(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """`--no-history` leaves the history at today's point without reading git."""
    root = make_checkout(tmp_path / "repo", ONE_FUNCTION, ONE_TAG, ONE_LISTING)
    monkeypatch.chdir(root)
    assert main(["progress", "render", str(tmp_path / "out"), "--no-history"]) == 0
    history = json.loads((tmp_path / "out" / "progress" / "history.json").read_text(encoding="utf-8"))
    assert len(history["points"]) == 1 and history["skipped"] == []


def test_missing_history_marker_is_exit_2(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(make_checkout(tmp_path))
    (tmp_path / "docs" / "progress" / "history.md").write_text("no marker\n", encoding="utf-8")
    assert main(["progress", "check"]) == 2
    assert "history.md: needs exactly one <!-- progress:history -->" in capsys.readouterr().err


def test_missing_marker_is_exit_2(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(make_checkout(tmp_path))
    (tmp_path / "docs" / "progress" / "index.md").write_text("no marker\n", encoding="utf-8")
    assert main(["progress", "check"]) == 2
    assert "index.md: needs exactly one <!-- progress:generated -->" in capsys.readouterr().err


# --- the executable ---------------------------------------------------------------------------------------------------

JR_RA = 0x03E00008
NOP = 0


def jal(target: int) -> int:
    """A `jal target` instruction word."""
    return (3 << 26) | ((target >> 2) & 0x03FFFFFF)


def make_elf(text_words: list[int], data_words: list[int], text_address: int = 0x1000) -> bytes:
    """A minimal 32-bit little-endian MIPS ELF with .text, .data and .shstrtab, and no symbols."""
    names = b"\0.text\0.data\0.shstrtab\0"
    text = struct.pack(f"<{len(text_words)}I", *text_words)
    data = struct.pack(f"<{len(data_words)}I", *data_words)
    text_offset = 52
    data_offset = text_offset + len(text)
    names_offset = data_offset + len(data)
    shoff = names_offset + len(names)
    # ELF header: ELFCLASS32, little-endian, version 1; an executable for MIPS with four section headers at shoff,
    # the last (.shstrtab) naming them.
    header = b"\x7fELF" + bytes([1, 1, 1]) + bytes(9)
    header += struct.pack("<HHIIIIIHHHHHH", 2, 8, 1, text_address, 0, shoff, 0, 52, 32, 0, 40, 4, 3)
    sections = struct.pack("<10I", *([0] * 10))
    sections += struct.pack("<10I", 1, 1, 6, text_address, text_offset, len(text), 0, 0, 8, 0)
    sections += struct.pack("<10I", 7, 1, 3, 0x9000, data_offset, len(data), 0, 0, 8, 0)
    sections += struct.pack("<10I", 13, 3, 0, 0, names_offset, len(names), 0, 0, 1, 0)
    return header + text + data + names + sections


# Three functions: 0x1000 (calls 0x1010), 0x1010 (12 bytes of code, then a padding word), 0x1020 (reached only
# through a pointer in .data, then two words up to the end of .text).
TEXT = [jal(0x1010), NOP, JR_RA, NOP, 0x24020001, JR_RA, NOP, NOP, 0x24020002, JR_RA, NOP, NOP]
DATA = [0x1020, 0x1024, 0x12345678]  # 0x1024 is not 8-byte aligned and is ignored


def test_function_starts_and_spans() -> None:
    """Starts come from `jal` targets and aligned .data pointers; a span runs to the next start or the end of .text."""
    executable = elf.read_elf(make_elf(TEXT, DATA))
    starts = elf.function_starts(executable)
    assert starts == [0x1010, 0x1020]
    assert elf.span(executable, 0x1010, starts) == 16
    assert elf.code_length(executable, 0x1010, 16) == 12  # a padding word, but the delay slot nop stays
    assert elf.span(executable, 0x1020, starts) == 16  # to the end of .text


def lui(reg: int, value: int) -> int:
    """A `lui reg, value >> 16` instruction word."""
    return (15 << 26) | (reg << 16) | (value >> 16)


def addiu(dest: int, src: int, imm: int) -> int:
    """An `addiu dest, src, imm` instruction word."""
    return (9 << 26) | (src << 21) | (dest << 16) | (imm & 0xFFFF)


def test_function_starts_from_code_built_pointers() -> None:
    """A `lui` / `addiu` pair into .text starts a function, even split; a call between the two breaks the pair."""
    a2, a3 = 6, 7
    text = [
        lui(a3, 0x100000),
        lui(a2, 0x5000),
        addiu(a3, a3, 0x20),  # 0x100020: a start, though the lui is two words back
        addiu(a2, a2, 0x10),  # 0x5010: outside .text, ignored
        lui(a3, 0x100000),
        jal(0x100000),
        NOP,
        addiu(a3, a3, 0x30),  # after a call: the lui is forgotten
        JR_RA,
        NOP,
        NOP,
        NOP,
        0x24020002,
        JR_RA,
        NOP,
        NOP,
    ]
    starts = elf.function_starts(elf.read_elf(make_elf(text, [], text_address=0x100000)))
    assert starts == [0x100000, 0x100020]


def test_check_size_bounds() -> None:
    """A size between the code length and the span passes; past the span is an error, short of the code a warning."""
    executable = elf.read_elf(make_elf(TEXT, DATA))
    starts = elf.function_starts(executable)
    assert elf.check_size(executable, 0x1010, 16, starts).errors == ()
    assert elf.check_size(executable, 0x1010, 12, starts).warnings == ()
    assert "runs past" in elf.check_size(executable, 0x1010, 20, starts).errors[0]
    assert "before the last instruction" in elf.check_size(executable, 0x1010, 8, starts).warnings[0]
    unreached = elf.check_size(executable, 0x1004, None, starts).warnings
    assert any("not 8-byte aligned" in w for w in unreached) and any("no call" in w for w in unreached)
    assert elf.check_size(executable, 0x5000, None, starts).errors == ("0x00005000 is outside .text",)


def test_read_elf_refuses_other_files() -> None:
    with pytest.raises(elf.ElfError, match="not an ELF"):
        elf.read_elf(b"MZ" + bytes(100))
    big_endian = bytearray(make_elf(TEXT, DATA))
    big_endian[5] = 2
    with pytest.raises(elf.ElfError, match="little-endian"):
        elf.read_elf(bytes(big_endian))


def sizes_checkout(tmp_path: Path, listing: str, addresses: tuple[int, ...] = (0x1010, 0x1020)) -> tuple[Path, Path]:
    """A checkout whose database ports the functions at `addresses` (tagged, with `listing`), and a disc with an ELF."""
    functions = "".join(entry(a, f"F{a:x}", f"src/core/thing.h:f{a:x}") for a in addresses)
    tags = "".join(f"/// @orig {a:#010x} F{a:x} (x.cpp)\nvoid f{a:x}();\n" for a in addresses)
    root = make_checkout(tmp_path / "repo", functions, tags, listing)
    disc = tmp_path / "disc"
    disc.mkdir()
    (disc / "SLUS_212.15").write_bytes(make_elf(TEXT, DATA))
    return root, disc


def test_sizes_command_checks_the_listings_sizes(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """The size of a reimplemented function is the Ghidra listing's; the command holds it to the disc's code."""
    root, disc = sizes_checkout(tmp_path, "0x00001010\t16\tF1010\t0\n")
    monkeypatch.chdir(root)
    assert main(["progress", "sizes", str(disc)]) == 0
    out = capsys.readouterr().out
    assert "0x00001010 F1010: size 16, span to the next known function 16" in out
    assert "0x00001020 F1020: size -, span to the next known function 16" in out  # the listing has no function here
    # A size past the next function is an error.
    (root / "docs" / "progress" / "ghidra-functions.tsv").write_text("0x00001020\t32\tF1020\t0\n", encoding="utf-8")
    assert main(["progress", "sizes", str(disc)]) == 1
    assert "error: size 32 runs past" in capsys.readouterr().out


def test_sizes_span_stops_at_the_next_reimplemented_function(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """A reimplemented function nothing calls (a virtual method) still ends the span of the one before it."""
    root, disc = sizes_checkout(tmp_path, "0x00001010\t8\tF1010\t0\n", (0x1010, 0x1018))
    monkeypatch.chdir(root)
    assert main(["progress", "sizes", str(disc)]) == 0
    out = capsys.readouterr().out
    assert "0x00001010 F1010: size 8, span to the next known function 8" in out
    assert "0x00001018 F1018: size -, span to the next known function 8" in out


def test_sizes_command_with_nothing_reimplemented(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(make_checkout(tmp_path / "repo"))
    assert main(["progress", "sizes", str(tmp_path)]) == 0
    assert "nothing to check" in capsys.readouterr().out


# --- the "Understood" measure -----------------------------------------------------------------------------------------

# Four game functions and one in the middleware: Alpha's first is named and cited with evidence, its second is cited
# without evidence, Beta's is named but uncited, the unattributed one is unnamed but cited with evidence.
LISTING = (
    "# a comment\n"
    "0x00001000\t256\tAlpha_Init\t1\n"
    "0x00001100\t64\tAlpha_Step\t0\n"
    "0x00001200\t128\tBeta_Run\t0\n"
    "0x00001500\t32\t\t0\n"
    "0x00001c00\t512\tlib_thing\t0\n"
)
RESEARCH_PAGE = """# Alpha

| Address | Name | Evidence |
| --- | --- | --- |
| `0x00001000` | `Alpha_Init` | confirmed (code) |
| `0x00001100` | `Alpha_Step` | |

The helper at `0x00001500` (`Odd_Helper`) is dead code: not needed: never called.
"""


def make_research_checkout(root: Path) -> Path:
    """A checkout with a Ghidra listing and a research page; the source map cites Beta, which must not count."""
    make_checkout(root)
    (root / "docs" / "progress" / "ghidra-functions.tsv").write_text(LISTING, encoding="utf-8")
    (root / "docs" / "research").mkdir(parents=True)
    (root / "docs" / "research" / "alpha.md").write_text(RESEARCH_PAGE, encoding="utf-8")
    (root / "docs" / "research" / "source-map.md").write_text("`0x00001200` inferred\n", encoding="utf-8")
    return root


def test_listing_round_trips_and_refuses_bad_lines() -> None:
    functions = progress_research.parse_listing(LISTING)
    assert [f.address for f in functions] == [0x1000, 0x1100, 0x1200, 0x1500, 0x1C00]
    assert functions[0].plate and functions[0].named and not functions[3].named
    assert progress_research.parse_listing(progress_research.format_listing(functions)) == functions
    default = progress_research.ListedFunction(0x1500, 32, "FUN_00001500", False)
    assert "\t\t0" in progress_research.format_listing([default])  # the default name is left out
    assert not progress_research.ListedFunction(0x10, 4, "thunk_FUN_00000010", False).named
    with pytest.raises(progress.ProgressError, match="expected"):
        progress_research.parse_listing("0x1000\t4\tx\t0\n")
    with pytest.raises(progress.ProgressError, match="ascending"):
        progress_research.parse_listing("0x00002000\t4\ta\t0\n0x00001000\t4\tb\t0\n")


def test_citations_need_evidence_and_skip_the_source_map(tmp_path: Path) -> None:
    root = make_research_checkout(tmp_path)
    citations = progress_research.scan_citations(root)
    assert citations[0x1000].evidence and citations[0x1000].page_name == "Alpha_Init"
    assert not citations[0x1100].evidence  # its table row states no level
    assert citations[0x1500].evidence  # "not needed" counts
    assert 0x1200 not in citations  # only the source map cites it


def test_binding_wrappers_count_with_evidence(tmp_path: Path) -> None:
    root = make_research_checkout(tmp_path)
    (root / "research" / "bindings").mkdir(parents=True)
    (root / "research" / "bindings" / "x.yaml").write_text(
        "- name: DoIt\n  wrapper: 0x00001200\n  evidence: inferred\n\n- name: Other\n  wrapper: 0x00001300\n",
        encoding="utf-8",
    )
    citations = progress_research.scan_citations(root)
    assert citations[0x1200].evidence and citations[0x1200].page_name == "lua_DoIt"
    assert not citations[0x1300].evidence


def test_measure_by_subsystem_and_backlog(tmp_path: Path) -> None:
    root = make_research_checkout(tmp_path)
    u = progress.load(root).understanding
    assert u is not None
    assert u.functions == 4 and u.bytes == 256 + 64 + 128 + 32  # the middleware function is left out
    assert u.understood_functions == 1 and u.understood_bytes == 256
    rows = {r.name: r for r in u.by_subsystem(["Alpha", "Beta", "Empty"])}
    assert list(rows) == ["Alpha", "Beta", progress.UNATTRIBUTED]
    assert (rows["Alpha"].understood_functions, rows["Alpha"].named, rows["Alpha"].cited) == (1, 2, 1)
    assert [s.missing() for s in u.backlog("Alpha")] == [["evidence"]]
    assert [s.missing() for s in u.backlog("Beta")] == [["cite"]]
    assert [s.missing() for s in u.backlog(progress.UNATTRIBUTED)] == [["name"]]
    text = progress_research.render_backlog(u, rows[progress.UNATTRIBUTED])
    assert "| `0x00001500` | 32 | `FUN_00001500` | name | no | `Odd_Helper` | research/alpha.md |" in text
    assert progress_research.backlog_file_name("Device/ps2") == "Device-ps2.md"
    assert progress_research.backlog_file_name("Maths (unnamed)") == "Maths.md"


def test_understood_is_rendered(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """The bar and its badge appear once there is a listing, and a new citation moves them."""
    root = make_research_checkout(tmp_path / "repo")
    monkeypatch.chdir(root)
    out = tmp_path / "out"
    assert main(["progress", "render", str(out)]) == 0
    page = (out / "progress" / "index.md").read_text(encoding="utf-8")
    assert "| **Understood** |" in page and "## Understood by subsystem" in page
    assert "1 of 4 functions" in page and "badge/understood-" in page
    understood = out / "progress" / "badges" / "understood.json"
    assert json.loads(understood.read_text(encoding="utf-8"))["label"] == "understood"
    before = understood.read_text(encoding="utf-8")
    page_path = root / "docs" / "research" / "alpha.md"
    page_path.write_text(RESEARCH_PAGE.replace("| `Alpha_Step` | |", "| `Alpha_Step` | inferred |"), encoding="utf-8")
    assert main(["progress", "render", str(out)]) == 0
    assert understood.read_text(encoding="utf-8") != before
    assert main(["progress", "show", "--json"]) == 0


def test_without_a_listing_nothing_is_measured(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    root = make_checkout(tmp_path / "repo")
    monkeypatch.chdir(root)
    assert progress.load(root).understanding is None
    out = tmp_path / "out"
    assert main(["progress", "render", str(out)]) == 0
    page = (out / "progress" / "index.md").read_text(encoding="utf-8")
    assert "| **Understood** |" not in page and "Not measured yet" in page
    assert not (out / "progress" / "badges" / "understood.json").exists()


def test_backlog_command_writes_one_file_per_subsystem(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    root = make_research_checkout(tmp_path / "repo")
    monkeypatch.chdir(root)
    with pytest.raises(ConfigError):
        progress_cli.run_backlog(root / "inside")
    out = tmp_path / "backlog"
    assert main(["progress", "backlog", str(out)]) == 0
    assert sorted(p.name for p in out.iterdir()) == ["Alpha.md", "Beta.md", "Unattributed.md"]


def test_export_listing_reads_through_fetch() -> None:
    """The export asks for the functions, each one's body and the plate comments, and skips external functions."""
    answers = {
        "/list_functions_enhanced?limit=100000&offset=0": {
            "functions": [
                {"address": "00001000", "name": "Alpha_Init", "isThunk": False, "isExternal": False},
                {"address": "00001100", "name": "FUN_00001100", "isThunk": False, "isExternal": False},
                {"address": "00009000", "name": "ext", "isThunk": False, "isExternal": True},
            ]
        },
        "/get_function_by_address?address=0x00001000": {"body_start": "00001000", "body_end": "000010ff"},
        "/get_function_by_address?address=0x00001100": {"body_start": "00001100", "body_end": "0000113f"},
        "/batch_get_comments?addresses=0x00001000,0x00001100": {
            "results": [{"address": "00001000", "plate": "Sets up."}, {"address": "00001100", "plate": None}]
        },
    }
    functions = progress_research.export_listing(answers.__getitem__)
    assert functions == [
        progress_research.ListedFunction(0x1000, 256, "Alpha_Init", True),
        progress_research.ListedFunction(0x1100, 64, "FUN_00001100", False),
    ]
