# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the function database's schema (coney_tools.research_functions), on made-up systems and addresses."""

from __future__ import annotations

from pathlib import Path

import pytest
from conventions_support import real_data

from coney_tools import research_functions as rf
from coney_tools.conventions import parse_conventions

FULL = """\
# The researched functions of the combat system.

[[function]]
address = 0x00100200
name = "Grab_Start"
page = "combat/grabs.md#grab"
kind = "start"
role = "plays the grab intro (anim 71) and decides miss, counter, front or rear grab"
level = "confirmed-code"
note = "the anim id inferred"
coney = "src/core/combat/grab.cpp:Grab::start"
status = "compared"

[[function.gaps]]
what = "the counter roll uses a fixed chance"
relation = "stand-in"
question = "combat/grabs.md#q-counter"

[[function.gaps]]
what = "the mount's ground strikes"
relation = "not-ported"
issue = 123

[[function]]
address = 0x00100300
name = "DVDWadIndex::Find"
page = "file-io.md"
role = "a debug stub nothing calls"
level = "not-needed"
"""


def test_a_full_file_parses_into_entries() -> None:
    """Every field of the schema is read, and an entry with only the required ones has the defaults."""
    system = rf.parse_system("combat", FULL, Path("combat.toml"))
    first, second = system.entries
    assert (first.address, first.name, first.level) == (0x100200, "Grab_Start", "confirmed-code")
    assert first.page == "combat/grabs.md#grab"
    assert (first.page_path, first.anchor) == ("combat/grabs.md", "grab")
    assert first.kind == "start"
    assert first.note == "the anim id inferred"
    assert (first.coney, first.status) == ("src/core/combat/grab.cpp:Grab::start", "compared")
    assert [(g.relation, g.issue, g.question) for g in first.gaps] == [
        ("stand-in", None, "combat/grabs.md#q-counter"),
        ("not-ported", 123, ""),
    ]
    assert (second.kind, second.note, second.coney, second.status, second.gaps) == ("", "", "", "", ())
    assert (second.page_path, second.anchor) == ("file-io.md", "")
    assert system.header == "# The researched functions of the combat system."


def test_dumps_writes_the_canonical_text_and_reads_back_the_same() -> None:
    """The writer is the one format: what it writes parses back to equal entries, and writing again is a no-op."""
    system = rf.parse_system("combat", FULL, Path("combat.toml"))
    text = rf.dumps(system)
    assert text == FULL
    assert rf.parse_system("combat", text, Path("combat.toml")).entries == system.entries


def test_dumps_escapes_quotes_backslashes_and_controls() -> None:
    """A role may hold any text a table cell can: quotes, backslashes, tabs and non-ASCII."""
    entry = rf.Entry(0x100400, "A", "a.md", "inferred", role='say "hi" \\ \t é \x7f')
    system = rf.SystemFunctions("s", Path("s.toml"), "", (entry,))
    text = rf.dumps(system)
    assert rf.parse_system("s", text, Path("s.toml")).entries == (entry,)


def test_a_default_header_is_written_for_a_new_file() -> None:
    """A system with no header of its own gets the standard two comment lines."""
    system = rf.SystemFunctions("ai", Path("ai.toml"), "", ())
    assert rf.dumps(system).startswith("# The researched functions of the ai system")


def _one(**fields: str) -> str:
    """A file with one entry: the required fields, with `fields` replacing or adding to them."""
    values = {"address": "0x00100200", "name": '"Grab_Start"', "page": '"combat.md#grab"', "level": '"inferred"'}
    values.update(fields)
    return "[[function]]\n" + "".join(f"{key} = {value}\n" for key, value in values.items())


@pytest.mark.parametrize(
    ("text", "message"),
    [
        ("function = 3\n", "function must be an array of tables"),
        ("extra = 1\n", "unknown key"),
        (_one(unknown="1"), "unknown key"),
        (_one(address="3.5"), "address must be an integer"),
        (_one(address="-1"), "address must be an integer"),
        (_one(address='"0x100200"'), "address must be an integer"),
        (_one(name='"two words"'), "name"),
        (_one(name='""'), "name"),
        (_one(name="5"), "name"),
        (_one(page='"Combat.md"'), "page"),
        (_one(page='"combat.md#"'), "page"),
        (_one(page='"../x.md"'), "page"),
        (_one(page='"combat.txt"'), "page"),
        (_one(level='"confirmed"'), "level"),
        (_one(level='"confirmed (code)"'), "level"),
        (_one(role="5"), "role must be a string"),
        (_one(role='"two\\nlines"'), "role must be one line"),
        (_one(kind='"two\\nlines"'), "kind must be one line"),
        (_one(level='"not-needed"'), "not-needed needs a role"),
        (_one(level='""'), "level must be one of"),
        (_one(page='"../references/other.md#x"'), "page"),
        (_one(page='"../references/bindings/ai.md"'), "page"),
        (_one(page='"../references/bindings/AI.md#x"'), "page"),
        (_one(page='"../../x.md#x"'), "page"),
        (_one(status='"legacy"'), "status needs coney"),
        (_one(coney='"src/a.cpp"', status='"done"'), "status must be one of"),
        (_one(coney='"has space"'), "coney"),
        (_one(coney='""'), "coney"),
        (_one() + "[[function.gaps]]\nwhat = 'x'\nrelation = 'maybe'\nissue = 1\n", "relation must be one of"),
        (_one() + "[[function.gaps]]\nrelation = 'stand-in'\nissue = 1\n", "what"),
        (_one() + "[[function.gaps]]\nwhat = 'x'\nrelation = 'stand-in'\n", "needs an issue or a question"),
        (_one() + "[[function.gaps]]\nwhat = 'x'\nrelation = 'stand-in'\nissue = 0\n", "issue must be a positive"),
        (_one() + "[[function.gaps]]\nwhat = 'x'\nrelation = 'stand-in'\nissue = 1\nzzz = 2\n", "unknown key"),
        (_one() + "[[function.gaps]]\nwhat = 'x'\nrelation = 'stand-in'\nquestion = 'q'\n", "question"),
        (_one(role='"sets iVar1 to param_2"'), "decompiler"),
        (_one(note='"reads DAT_003a1f20"'), "decompiler"),
        (_one(kind='"undefined4 handler"'), "decompiler"),
        (_one(role='"*(int *)(this + 0x40) = 0"'), "decompiler"),
        (_one() + "[[function.gaps]]\nwhat = 'uses local_1c'\nrelation = 'stand-in'\nissue = 1\n", "decompiler"),
        (_one(address="0x00100300") + _one(), "ascending"),
        (_one() + _one(), "twice"),
        ("[[function]\n", "invalid"),
    ],
)
def test_a_broken_file_is_refused_with_its_problem_named(text: str, message: str) -> None:
    """Each rule of the schema has a message that names the file, the entry and the field."""
    with pytest.raises(rf.ResearchFunctionsError) as error:
        rf.parse_system("combat", text, Path("research/functions/combat.toml"))
    assert message in str(error.value)
    assert "research/functions/combat.toml" in str(error.value)


def test_prose_cites_an_unnamed_function_by_its_address() -> None:
    """`FUN_...` is refused in the text as on a research page (docs check's `decompile`): cite the address instead."""
    with pytest.raises(rf.ResearchFunctionsError, match="decompiler output"):
        rf.parse_system("combat", _one(role='"calls FUN_00123456 next"'), Path("c.toml"))


def test_prose_may_say_goto() -> None:
    """`goto` is a word in English text; only C++ refuses it."""
    [entry] = rf.parse_system("combat", _one(role='"the goto of the state machine"'), Path("c.toml")).entries
    assert "goto" in entry.role


def test_every_problem_is_reported_not_just_the_first() -> None:
    """A file with three bad entries lists all three, so one run is enough to fix them."""
    text = _one(level='"x"') + _one(address="0x00100300", name='"a b"') + _one(address="0x00100400", role="1")
    with pytest.raises(rf.ResearchFunctionsError) as error:
        rf.parse_system("combat", text, Path("combat.toml"))
    assert str(error.value).count("combat.toml: function") >= 3


@pytest.mark.parametrize("system", ["Combat", "ai goals", "", "-x", "a_b"])
def test_a_system_name_is_a_lower_case_slug(system: str) -> None:
    """The file name is the system: lower case with hyphens, like a research folder."""
    with pytest.raises(rf.ResearchFunctionsError, match="system name"):
        rf.parse_system(system, "", Path("x.toml"))


def test_an_empty_file_has_no_entries() -> None:
    """A system file may exist before its first entry."""
    assert rf.parse_system("combat", "# nothing yet\n", Path("combat.toml")).entries == ()


def write_systems(root: Path, systems: dict[str, str]) -> None:
    """Write `research/functions/<system>.toml` under `root` for each system."""
    folder = root / "research" / "functions"
    folder.mkdir(parents=True, exist_ok=True)
    for name, text in systems.items():
        (folder / f"{name}.toml").write_text(text, encoding="utf-8")


def test_load_all_reads_every_file_in_order(tmp_path: Path) -> None:
    """The systems come back sorted by name; a checkout without the folder has none."""
    assert rf.load_all(tmp_path) == []
    write_systems(tmp_path, {"b": _one(), "a": _one(address="0x00100300")})
    assert [s.system for s in rf.load_all(tmp_path)] == ["a", "b"]


def test_an_address_is_owned_by_one_system(tmp_path: Path) -> None:
    """The same address in two systems is an error: one owner per function."""
    write_systems(tmp_path, {"a": _one(), "b": _one(name='"Other"')})
    with pytest.raises(rf.ResearchFunctionsError, match=r"0x00100200.*also in .*a\.toml"):
        rf.load_all(tmp_path)


def test_load_all_names_every_broken_file(tmp_path: Path) -> None:
    """Two broken files are both named in the one error."""
    write_systems(tmp_path, {"a": _one(level='"x"'), "b": _one(level='"y"')})
    with pytest.raises(rf.ResearchFunctionsError) as error:
        rf.load_all(tmp_path)
    assert "a.toml" in str(error.value)
    assert "b.toml" in str(error.value)


def test_a_file_that_is_not_utf8_is_a_clear_error(tmp_path: Path) -> None:
    """Bytes that are not UTF-8 are reported against the file, not as a traceback."""
    folder = tmp_path / "research" / "functions"
    folder.mkdir(parents=True)
    (folder / "a.toml").write_bytes(b"\xff\xfe")
    with pytest.raises(rf.ResearchFunctionsError, match=r"a\.toml"):
        rf.load_all(tmp_path)


def test_levels_have_one_label_each() -> None:
    """The levels are those of research-evidence.md#evidence-levels, plus `not needed`."""
    assert list(rf.LEVELS) == ["confirmed-code", "confirmed-runtime", "inferred", "speculative", "not-needed"]
    assert rf.LEVELS["confirmed-code"] == "confirmed (code)"
    assert rf.LEVELS["not-needed"] == "not needed"


def test_the_default_folder_is_the_conventions_one() -> None:
    """The progress tracker reads the database without the conventions; its default folder is `files.functions`."""
    research = parse_conventions(real_data()).research
    assert Path(research.files.functions) == rf.FUNCTIONS_DIR


NO_LEVEL = """[[function]]
address = 0x00100200
name = "Grab_Start"
page = "combat.md#grab"
note = "the page states no level for this function alone; read the section"
"""


def test_the_level_is_optional_and_a_missing_one_is_written_as_none() -> None:
    """An entry the page grades nowhere has no level; the writer does not invent one and reads it back the same."""
    system = rf.parse_system("combat", NO_LEVEL, Path("combat.toml"))
    [entry] = system.entries
    assert entry.level == ""
    assert rf.dumps(system).endswith(NO_LEVEL)
    assert rf.parse_system("combat", rf.dumps(system), Path("combat.toml")).entries == system.entries


def test_an_entry_with_no_level_must_say_why_in_its_note() -> None:
    """Leaving the level out is deliberate: the note says what the page states or where to read it."""
    text = NO_LEVEL.replace('note = "the page states no level for this function alone; read the section"\n', "")
    with pytest.raises(rf.ResearchFunctionsError, match="no level needs a note"):
        rf.parse_system("combat", text, Path("combat.toml"))


def test_a_binding_wrapper_may_point_at_its_generated_reference_page() -> None:
    """A binding page is named relative to docs/research, with the section of the binding."""
    [entry] = rf.parse_system(
        "scripting", _one(page='"../references/bindings/ai.md#actlookat"'), Path("scripting.toml")
    ).entries
    assert entry.is_binding
    assert entry.anchor == "actlookat"
    assert entry.doc_file == "docs/references/bindings/ai.md"
    other = rf.Entry(0x100200, "A", "combat/grabs.md#grab", "inferred")
    assert not other.is_binding
    assert other.doc_file == "docs/research/combat/grabs.md"
