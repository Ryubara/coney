# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the source files data (coney_tools.source_files): made-up files and addresses, and the real data once."""

from __future__ import annotations

from pathlib import Path

import pytest
from conventions_support import REPO

from coney_tools import progress, source_files

WHERE = Path("research/source-files.toml")
FULL = """\
# The files of the executable.

[[file]]
directory = "Alpha"
path = "Alpha/One.cpp"
start = 0x00100200
end = 0x00100400
functions = 3
stub = 0x00100500
anchors = [
  {address = 0x00100200, tags = ["One", "Pool<One>"]},
  {address = 0x00100300, tags = []},
]
more_anchors = 2

[[file]]
directory = "Alpha"
path = "Alpha/Two.cpp"
path_string = 0x00540000
functions = 0

[[file]]
directory = "Alpha/Sub"
path = "Alpha/Sub/deep/Three.cpp"
start = 0x00100400
end = 0x00100600
functions = 1
anchors = [
  {address = 0x00100400, tags = []},
]

[[file]]
directory = "Beta"
path = "Beta/(no path string)"
start = 0x00200000
end = 0x00200100
functions = 5
note = "the unit before Four.cpp, placed by its callers (inferred)"
"""


def parse(text: str = FULL) -> source_files.SourceFiles:
    """The made-up file, parsed."""
    return source_files.parse(text, WHERE)


def problems(text: str) -> list[str]:
    """The problems `parse` reports for `text`, one per line."""
    with pytest.raises(source_files.SourceFilesError) as caught:
        parse(text)
    return str(caught.value).splitlines()


def test_a_full_file_parses_into_files_with_their_anchors() -> None:
    """Every field of the schema is read, and a file with only the required ones has the defaults."""
    data = parse()
    one, two, three, four = data.files
    assert (one.directory, one.path, one.name) == ("Alpha", "Alpha/One.cpp", "One.cpp")
    assert (one.start, one.end, one.functions, one.stub) == (0x100200, 0x100400, 3, 0x100500)
    assert [(a.address, a.tags) for a in one.anchors] == [(0x100200, ("One", "Pool<One>")), (0x100300, ())]
    assert one.more_anchors == 2
    assert (two.start, two.end, two.path_string, two.stub, two.anchors, two.more_anchors) == (
        None,
        None,
        0x540000,
        None,
        (),
        0,
    )
    assert three.name == "deep/Three.cpp"
    assert four.note.startswith("the unit before")
    assert data.header == "# The files of the executable."


def test_dumps_writes_the_canonical_text_and_reads_back_the_same() -> None:
    """The writer is the one format: what it writes parses back to equal files, and writing again is a no-op."""
    data = parse()
    assert source_files.dumps(data) == FULL
    assert parse(source_files.dumps(data)).files == data.files


def test_dumps_escapes_a_tag_with_quotes_and_backslashes() -> None:
    """An anchor's tag is a string the allocator took, which may hold any character."""
    anchor = source_files.Anchor(0x100200, ("cdrom0:\\%s;1", 'say "hi"'))
    entry = source_files.SourceFile("A", "A/x.cpp", 0x100200, 0x100300, 1, None, (anchor,))
    text = source_files.dumps(source_files.SourceFiles("", (entry,)))
    assert parse(text).files == (entry,)


def test_the_ranges_are_the_files_with_an_address_range_in_address_order() -> None:
    """The ranges are what the owner lookup and the progress picture use: a file with no code has none."""
    ranges = parse().ranges()
    assert [(r.path, r.start, r.end) for r in ranges] == [
        ("Alpha/One.cpp", 0x100200, 0x100400),
        ("Alpha/Sub/deep/Three.cpp", 0x100400, 0x100600),
        ("Beta/(no path string)", 0x200000, 0x200100),
    ]


def test_file_of_finds_the_file_that_holds_an_address() -> None:
    """The file whose range holds the address, and nothing for a gap or the end of a range (a range is half open)."""
    ranges = parse().ranges()
    starts = [r.start for r in ranges]
    assert source_files.file_of(ranges, starts, 0x1002FF) == "Alpha/One.cpp"
    assert source_files.file_of(ranges, starts, 0x100400) == "Alpha/Sub/deep/Three.cpp"
    assert source_files.file_of(ranges, starts, 0x100600) == ""
    assert source_files.file_of(ranges, starts, 0x0FFFFF) == ""


@pytest.mark.parametrize(
    ("old", "new", "message"),
    [
        ('directory = "Alpha"\npath = "Alpha/One.cpp"', 'path = "Alpha/One.cpp"', "directory"),
        ('path = "Alpha/One.cpp"', 'path = "Other/One.cpp"', "must start with its directory"),
        ('path = "Alpha/One.cpp"', 'path = "Alpha/Two.cpp"', "listed twice"),
        ("end = 0x00100400", "end = 0x00100200", "ends before it starts"),
        ("start = 0x00100200\nend = 0x00100400\n", "start = 0x00100200\n", "start and end go together"),
        ("functions = 3", "functions = 0", "has an address range, so at least one function"),
        ("functions = 3", 'functions = "3"', "functions must be"),
        ("stub = 0x00100500", "stub = 0x00100100", "stub"),
        ("more_anchors = 2", "more_anchors = -1", "more_anchors"),
        ("{address = 0x00100300, tags = []}", "{address = 0x00100800, tags = []}", "outside the file's range"),
        ("{address = 0x00100300, tags = []}", "{address = 0x00100300, tags = [1]}", "tags must be"),
        ("{address = 0x00100300, tags = []}", "{addr = 0x00100300}", "unknown key"),
        ("functions = 3", "functions = 3\nsurprise = 1", "unknown key 'surprise'"),
        ("path_string = 0x00540000\nfunctions = 0", "functions = 0", "path_string"),
        ("path_string = 0x00540000\nfunctions = 0", "path_string = 0x00540000\nfunctions = 2", "no address range"),
        ('note = "the unit before Four.cpp, placed by its callers (inferred)"', 'note = ["x"]', "note must be"),
        ("0x00100200,", "0x1ffffffff,", "address must be"),
    ],
)
def test_a_file_that_breaks_the_schema_is_refused_with_its_name(old: str, new: str, message: str) -> None:
    """Each fault names the file and the field, so the author sees which row to fix."""
    found = problems(FULL.replace(old, new, 1))
    assert any(message in line for line in found), found


def test_two_files_may_not_share_an_address() -> None:
    """A file's code is its own: overlapping ranges are an error naming both files."""
    text = FULL.replace("start = 0x00100400\nend = 0x00100600", "start = 0x00100300\nend = 0x00100600")
    found = problems(text)
    assert any("Alpha/One.cpp" in line and "Alpha/Sub/deep/Three.cpp" in line and "overlap" in line for line in found)


def test_invalid_toml_and_unknown_top_level_keys_are_refused() -> None:
    """A syntax error is reported once with the file's name; a table that is not [[file]] is unknown."""
    assert "not valid TOML" in problems("[[file]\n")[0]
    assert any("unknown key 'surprise'" in line for line in problems("surprise = 1\n"))


def test_load_reads_the_committed_path_and_a_missing_file_is_no_files(tmp_path: Path) -> None:
    """A checkout without the data has no files; with it, load reads the path the conventions name."""
    assert source_files.load(tmp_path).files == ()
    (tmp_path / "research").mkdir()
    (tmp_path / WHERE).write_text(FULL, encoding="utf-8")
    assert len(source_files.load(tmp_path).files) == 4
    assert next(r.path for r in source_files.load_ranges(tmp_path)) == "Alpha/One.cpp"


def totals(alpha_end: int = 0x100800) -> progress.Totals:
    """Made-up totals: two subsystems and one middleware range."""
    return progress.Totals(
        progress.Range(0x100000, 0x300000),
        (progress.Coverage("k", "k", "researched", 1_000_000),),
        (
            progress.Subsystem("Alpha", (progress.Range(0x100200, alpha_end),)),
            progress.Subsystem("Beta", (progress.Range(0x200000, 0x200200),)),
        ),
        (progress.Middleware("Lib", 16, "x", (progress.Range(0x280000, 0x280100),)),),
    )


def test_every_file_lies_in_its_directorys_subsystem_or_a_middleware_range() -> None:
    """The check that ties the file data to totals.toml, which stays hand-kept."""
    assert source_files.check_totals(parse(), totals()) == []


def test_a_file_outside_its_directorys_range_is_named() -> None:
    """A file moved out of its subsystem's range is the drift the check exists to catch."""
    found = source_files.check_totals(parse(), totals(alpha_end=0x100500))
    assert len(found) == 1
    assert "Alpha/Sub/deep/Three.cpp" in found[0]
    assert "Alpha" in found[0]


def test_a_file_in_a_middleware_range_is_fine_and_a_directory_with_no_subsystem_is_not() -> None:
    """Lua and tolua files are the middleware's; any other directory without a subsystem is a mistake."""
    lib = FULL + (
        '\n[[file]]\ndirectory = "Lib"\npath = "Lib/lua.cpp"\nstart = 0x00280000\nend = 0x00280080\nfunctions = 1\n'
    )
    assert source_files.check_totals(parse(lib), totals()) == []
    stray = FULL + (
        '\n[[file]]\ndirectory = "Stray"\npath = "Stray/x.cpp"\nstart = 0x00290000\nend = 0x00290080\nfunctions = 1\n'
    )
    found = source_files.check_totals(parse(stray), totals())
    assert len(found) == 1
    assert "Stray/x.cpp" in found[0]


def with_subsystems(*subsystems: progress.Subsystem) -> progress.Totals:
    """The made-up totals with the subsystems replaced."""
    base = totals()
    return progress.Totals(base.text, base.coverage, subsystems, base.middleware)


def test_a_subsystem_range_grown_over_another_directorys_file_is_named() -> None:
    """The other direction: a range that takes in a neighbour's file (here Alpha reaching into Beta) is too wide.

    This builds `Totals` directly, with overlapping ranges `progress.load_totals` would refuse, to check the message
    that names the range that grew.
    """
    alpha = progress.Subsystem("Alpha", (progress.Range(0x100200, 0x200080),))
    beta = totals().subsystems[1]
    found = source_files.check_totals(parse(), with_subsystems(alpha, beta))
    assert len(found) == 1
    assert "Alpha" in found[0] and "Beta/(no path string)" in found[0] and "too wide" in found[0]


def test_a_subsystem_range_grown_over_a_middleware_file_is_named() -> None:
    """A file no subsystem owns (Lua, tolua) is the library's: a game range may not reach into it either.

    Like the test above, this builds `Totals` the loader would refuse (the range overlaps a middleware range).
    """
    lib = FULL + (
        '\n[[file]]\ndirectory = "Lib"\npath = "Lib/lua.cpp"\nstart = 0x00280000\nend = 0x00280080\nfunctions = 1\n'
    )
    alpha, _ = totals().subsystems
    beta = progress.Subsystem("Beta", (progress.Range(0x200000, 0x280040),))
    found = source_files.check_totals(parse(lib), with_subsystems(alpha, beta))
    assert len(found) == 1
    assert "Lib/lua.cpp" in found[0] and "Beta" in found[0]


def test_a_subsystem_range_that_holds_none_of_its_files_is_named() -> None:
    """A subsystem with files whose second range holds none of them points at the wrong place."""
    alpha, _ = totals().subsystems
    beta = progress.Subsystem("Beta", (progress.Range(0x200000, 0x200200), progress.Range(0x250000, 0x250100)))
    found = source_files.check_totals(parse(), with_subsystems(alpha, beta))
    assert len(found) == 1
    assert "Beta" in found[0] and "0x00250000" in found[0] and "none of its files" in found[0]


def test_a_subsystem_with_no_files_is_placed_by_position_and_may_hold_none() -> None:
    """Maths and link-once have no file with an address: their ranges are placed by position, so only overlap counts."""
    alpha, beta = totals().subsystems
    maths = progress.Subsystem("Maths", (progress.Range(0x210000, 0x210400),))
    assert source_files.check_totals(parse(), with_subsystems(alpha, beta, maths)) == []


@pytest.mark.reads("docs_only")
def test_the_committed_data_passes_its_own_checks() -> None:
    """The file in the repository parses, matches totals.toml and keeps the figures the source map states."""
    root = REPO
    data = source_files.load(root)
    assert len(data.files) == 154
    assert source_files.check_totals(data, progress.load_totals(root / progress.TOTALS_FILE)) == []
    assert source_files.dumps(data) == (root / source_files.SOURCE_FILES).read_bytes().decode("utf-8").replace(
        "\r\n", "\n"
    )
    ranged = [f for f in data.files if f.start is not None]
    assert len(ranged) == 136
    assert len(data.files) - len(ranged) == 18


def test_the_default_path_is_the_one_the_conventions_name() -> None:
    """Tools that cannot read the conventions (the progress picture) use `SOURCE_FILES`; it must be the same path."""
    from coney_tools.conventions import load_conventions

    assert Path(load_conventions(REPO).research.files.source_files) == source_files.SOURCE_FILES
