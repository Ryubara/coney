# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the progress tracker, on a made-up checkout: invented ranges, functions and a tiny synthetic ELF."""

import json
import struct
from pathlib import Path

import pytest

from coney_tools import elf, progress, progress_cli, progress_render, progress_research
from coney_tools.cli import main
from coney_tools.config import ConfigError
from coney_tools.progress_cli import set_size

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

# A README or docs page with an empty progress block between hand-written text.
BLOCK = f"before\n\n{progress_render.MARKER_START}\nold\n{progress_render.MARKER_END}\n\nafter\n"


def make_checkout(root: Path, functions: str = "", tags: str = "") -> Path:
    """A made-up checkout with every input of the tracker."""
    root.mkdir(parents=True, exist_ok=True)
    (root / "coney.local.example.toml").write_text("", encoding="utf-8")
    (root / "docs" / "progress").mkdir(parents=True)
    (root / "docs" / "progress" / "totals.toml").write_text(TOTALS, encoding="utf-8")
    (root / "docs" / "progress" / "functions.toml").write_text(functions, encoding="utf-8")
    (root / "docs" / "progress" / "index.md").write_text(BLOCK, encoding="utf-8")
    (root / "docs" / "roadmap.md").write_text(ROADMAP, encoding="utf-8")
    (root / "README.md").write_text(BLOCK, encoding="utf-8")
    (root / "src" / "core").mkdir(parents=True)
    (root / "src" / "core" / "thing.h").write_text(tags, encoding="utf-8")
    return root


# One function of 0x80 bytes in Alpha, and the header that tags it.
ONE_FUNCTION = '[[function]]\naddress = 0x00001010\nname = "Thing::Do"\nsubsystem = "Alpha"\nsize = 0x80\n'
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


def test_replace_block_needs_markers() -> None:
    replaced = progress_render.replace_block(BLOCK, "new", Path("x.md"))
    assert replaced.startswith("before\n") and replaced.endswith("after\n")
    assert "old" not in replaced and "\nnew\n" in replaced
    with pytest.raises(progress.ProgressError, match="no <!-- progress:start -->"):
        progress_render.replace_block("no markers", "new", Path("x.md"))


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


def test_functions_refuse_unknown_keys(tmp_path: Path) -> None:
    path = make_checkout(tmp_path, ONE_FUNCTION + "colour = 1\n") / progress.FUNCTIONS_FILE
    with pytest.raises(progress.ProgressError, match="unknown key"):
        progress.load_functions(path)


def test_milestones_come_from_the_roadmap_table() -> None:
    milestones = progress.parse_milestones(ROADMAP + "\n| [Elsewhere](other.md) | done |\n")
    assert [(m.name, m.anchor, m.status) for m in milestones] == [
        ("Start", "start", "done"),
        ("Middle", "middle", "in progress"),
        ("End", "end", "not started"),
    ]


def test_consistent_inputs_have_no_problems(tmp_path: Path) -> None:
    state = progress.load(make_checkout(tmp_path, ONE_FUNCTION, ONE_TAG))
    assert state.problems == []
    assert state.reimplemented_bytes == 0x80
    rows = {name: (total, done, count) for name, total, done, count in state.subsystem_rows()}
    assert rows["Alpha"] == (0x200, 0x80, 1)
    assert rows[progress.UNATTRIBUTED] == (0x800, 0, 0)


def test_tags_and_entries_must_match(tmp_path: Path) -> None:
    tags = ONE_TAG + "/// @orig 0x00001300 Other::Fn (unknown)\n"
    problems = progress.load(make_checkout(tmp_path, ONE_FUNCTION.replace("Thing::Do", "Thing::Did"), tags)).problems
    assert any("0x00001300 Other::Fn has no entry" in p and "src/core/thing.h:4" in p for p in problems)
    assert any("names 'Thing::Do', functions.toml 'Thing::Did'" in p for p in problems)


def test_entry_without_tag_and_bad_entries(tmp_path: Path) -> None:
    """A wrong subsystem, an overlapping or odd size, middleware and a missing tag are each reported."""
    functions = (
        ONE_FUNCTION.replace('"Alpha"', '"Beta"')
        + '[[function]]\naddress = 0x1050\nname = "Overlap"\nsubsystem = "Alpha"\nsize = 6\n'
        + '[[function]]\naddress = 0x1c10\nname = "Lib"\nsubsystem = "SomeLib"\n'
    )
    problems = progress.load(make_checkout(tmp_path, functions, ONE_TAG)).problems
    assert any("subsystem is 'Beta'; the source map puts it in 'Alpha'" in p for p in problems)
    assert any("Thing::Do: its size runs into 0x00001050" in p for p in problems)
    assert any("size 6 is not a positive multiple of 4" in p for p in problems)
    assert any("Lib: not game code" in p for p in problems)
    assert any("Overlap: no @orig tag" in p for p in problems)


# --- the commands -----------------------------------------------------------------------------------------------------


def test_update_then_check(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
    """`--check` finds stale blocks, `update` fills them, and a changed input makes them stale again."""
    monkeypatch.chdir(make_checkout(tmp_path, ONE_FUNCTION, ONE_TAG))
    assert main(["progress", "update", "--check"]) == 1
    assert "stale: README.md, docs/progress/index.md" in capsys.readouterr().out
    assert main(["progress", "update"]) == 0
    assert main(["progress", "update", "--check"]) == 0
    readme = (tmp_path / "README.md").read_text(encoding="utf-8")
    assert "1 of 3 done" in readme and "[Middle](docs/roadmap.md#middle)" in readme
    assert "4.2% of the game's own code (128 of 3,072 bytes, 1 function)" in readme
    page = (tmp_path / "docs" / "progress" / "index.md").read_text(encoding="utf-8")
    assert "| `0x00001010` | `Thing::Do` | `Alpha` | 128 |" in page
    assert "| `Empty` |" in page and "not placed yet" in page
    assert "[End](../roadmap.md#end) | not started" in page
    # A roadmap change makes both blocks stale again.
    roadmap = tmp_path / "docs" / "roadmap.md"
    roadmap.write_text(ROADMAP.replace("in progress", "done"), encoding="utf-8")
    assert main(["progress", "update", "--check"]) == 1


def test_update_keeps_crlf(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.chdir(make_checkout(tmp_path))
    readme = tmp_path / "README.md"
    readme.write_bytes(BLOCK.replace("\n", "\r\n").encode())
    assert main(["progress", "update"]) == 0
    data = readme.read_bytes()
    assert data.count(b"\r\n") == data.count(b"\n")
    assert main(["progress", "update", "--check"]) == 0


def test_update_refuses_inconsistent_inputs(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(make_checkout(tmp_path, tags=ONE_TAG))
    assert main(["progress", "update"]) == 1
    assert "has no entry in functions.toml" in capsys.readouterr().out
    assert "old" in (tmp_path / "README.md").read_text(encoding="utf-8")  # nothing written


def test_show_json(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
    """`show --json` reports the shares, milestones and subsystems computed from the inputs."""
    monkeypatch.chdir(make_checkout(tmp_path, ONE_FUNCTION, ONE_TAG))
    assert main(["progress", "show", "--json"]) == 0
    data = json.loads(capsys.readouterr().out)
    assert data["reimplemented"] == {"bytes": 128, "total": 3072, "percent": 4.167, "functions": 1}
    assert data["researched"]["bytes"] == 1024
    assert [m["status"] for m in data["milestones"]] == ["done", "in progress", "not started"]
    assert data["subsystems"][-1]["name"] == "unattributed"


def test_missing_markers_is_exit_2(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.chdir(make_checkout(tmp_path))
    (tmp_path / "README.md").write_text("no block\n", encoding="utf-8")
    assert main(["progress", "update"]) == 2
    assert "README.md: no <!-- progress:start -->" in capsys.readouterr().err


def test_set_size_replaces_or_adds() -> None:
    text = "# header\n[[function]]\naddress = 0x00001010\nname = 'a'\nsize = 4\n\n"
    text += "[[function]]\naddress = 0x1020\nname = 'b'\n"
    replaced = set_size(text, 0x1010, 8)
    assert "size = 8" in replaced and "size = 4" not in replaced
    added = set_size(text, 0x1020, 12)
    assert added.endswith("name = 'b'\nsize = 12\n")
    with pytest.raises(ConfigError, match="no \\[\\[function\\]\\]"):
        set_size(text, 0x2000, 4)


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


def test_sizes_command_checks_and_fills(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    functions = '[[function]]\naddress = 0x00001010\nname = "A"\nsubsystem = "Alpha"\n\n'
    functions += '[[function]]\naddress = 0x00001020\nname = "B"\nsubsystem = "Alpha"\nsize = 8\n'
    root = make_checkout(tmp_path / "repo", functions)
    disc = tmp_path / "disc"
    disc.mkdir()
    (disc / "SLUS_212.15").write_bytes(make_elf(TEXT, DATA))
    monkeypatch.chdir(root)
    assert main(["progress", "sizes", str(disc), "--fill"]) == 0
    out = capsys.readouterr().out
    assert "0x00001010 A: size -, span to the next known function 16" in out and "filled 1 size(s)" in out
    loaded = progress.load_functions(root / progress.FUNCTIONS_FILE)
    assert [(f.name, f.size) for f in loaded] == [("A", 16), ("B", 8)]
    # A size past the next function is an error.
    (root / progress.FUNCTIONS_FILE).write_text(functions.replace("size = 8", "size = 32"), encoding="utf-8")
    assert main(["progress", "sizes", str(disc)]) == 1
    assert "error: size 32 runs past" in capsys.readouterr().out


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


def test_understood_is_rendered_and_checked(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """The bar appears once there is a listing, and a new citation makes the page stale."""
    root = make_research_checkout(tmp_path)
    monkeypatch.chdir(root)
    assert main(["progress", "update"]) == 0
    page = (root / "docs" / "progress" / "index.md").read_text(encoding="utf-8")
    assert "| **Understood** |" in page and "## Understood by subsystem" in page
    assert "1 of 4 functions" in page and "badge/understood-" in page
    assert main(["progress", "update", "--check"]) == 0
    page_path = root / "docs" / "research" / "alpha.md"
    page_path.write_text(RESEARCH_PAGE.replace("| `Alpha_Step` | |", "| `Alpha_Step` | inferred |"), encoding="utf-8")
    assert main(["progress", "update", "--check"]) == 1
    capsys.readouterr()
    assert main(["progress", "show", "--json"]) == 0
    assert json.loads(capsys.readouterr().out)["understood"]["functions"] == 2


def test_without_a_listing_nothing_is_measured(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    root = make_checkout(tmp_path)
    monkeypatch.chdir(root)
    assert progress.load(root).understanding is None
    assert main(["progress", "update"]) == 0
    assert "Understood" not in (root / "README.md").read_text(encoding="utf-8")


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
