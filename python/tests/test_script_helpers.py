# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the reference of `global.lua`'s helpers (coney_tools.script_helpers and its page).

The chunks are our own made-up scripts (`script_helpers_fixtures/*.lua`), compiled with a stock Lua 4.0.1 `luac -s`
and kept in the game's layout (4-byte sizes: a 64-bit `luac` writes 8, and its string lengths were narrowed to 4,
docs/guides/coney-tools-fidelity.md#lua); the "natives" are a set the tests name (`Bind*`), not the game's bindings,
and the YAML is invented too.
"""

from __future__ import annotations

from pathlib import Path

import pytest
import yaml

from coney_tools import natives, script_helpers, script_helpers_render
from coney_tools.cli import main

FIXTURES = Path(__file__).parent / "script_helpers_fixtures"
NATIVES = frozenset({"BindWake", "BindLevel", "BindSchedule", "BindShow", "BindRegister"})
#: The usage line every made-up binding entry needs.
USAGE = "  usage: {chunks: 1, calls: 1, boot: false, mission1: false, result_used: false}\n"


def _chunk(name: str) -> bytes:
    """One compiled fixture."""
    return (FIXTURES / f"{name}.s.luac").read_bytes()


def _survey() -> script_helpers.Survey:
    """The survey of the fixture `global.lua` against the two fixture level scripts."""
    others = [("mission.lua", _chunk("mission")), ("intro.lua", _chunk("intro"))]
    return script_helpers.survey(_chunk("global"), others, NATIVES)


# Hand-written entries for every helper and global of the fixture, without their `scan` lines.
DESCRIBED = """\
# A made-up data file for the tests.
chunk_sha1: 0000000000000000000000000000000000000000
helpers:
  - name: Count
    group: tables
    params: [list]
    description: >-
      Counts a table's entries.

  - name: Wake
    group: squad
    params: [who, quick]
    description: Wakes someone up.
    evidence: inferred

  - name: Boot
    group: setup
    params: []
    description: Runs once.

  - name: Later
    group: missions
    params: []
    description: Wakes later.

  - name: Menu.Open
    group: tables
    params: [page]
    description: Opens a page.

globals:
  - name: "true"
    group: values
    description: The number 1.

  - name: Level
    group: missions
    description: The level's number.

  - name: Hidden
    group: values
    description: Set, then cleared.

  - name: Menu
    group: tables
    description: The menu helpers' table.
"""


def test_survey_reads_each_helper_from_the_chunk() -> None:
    """Every function `global.lua` stores in a global or a global table's field is a helper, with its prototype
    number, its parameter count and the natives and other helpers it calls (callback names included)."""
    helpers = _survey().helpers
    assert list(helpers) == ["Count", "Wake", "Boot", "Later", "Menu.Open"]
    assert helpers["Count"] == script_helpers.HelperScan(function=0, params=1)
    wake = helpers["Wake"]
    assert (wake.function, wake.params, wake.natives, wake.helpers) == (1, 2, ("BindWake",), ("Count",))
    later = helpers["Later"]
    assert (later.natives, later.helpers) == (("BindSchedule",), ("Wake",))
    assert helpers["Menu.Open"].helpers == ("Wake",)


def test_survey_counts_the_other_scripts_that_call_a_helper() -> None:
    """A script calls a helper by its name, or passes the name as a callback; `global.lua` itself is not counted."""
    helpers = _survey().helpers
    assert (helpers["Wake"].scripts, helpers["Wake"].calls) == (2, 3)
    assert (helpers["Later"].scripts, helpers["Later"].calls) == (1, 1)
    assert (helpers["Menu.Open"].scripts, helpers["Menu.Open"].calls) == (1, 1)
    assert (helpers["Count"].scripts, helpers["Count"].calls) == (0, 0)


def test_survey_marks_what_the_main_chunk_runs_registers_and_deletes() -> None:
    """A helper the main chunk calls runs at load, one whose name it passes to a native is registered, and one set to
    nil anywhere in the chunk lists who deletes it."""
    helpers = _survey().helpers
    assert helpers["Boot"].at_load and helpers["Boot"].deleted_by == ("Boot",)
    assert helpers["Later"].registered and not helpers["Later"].at_load
    assert not helpers["Wake"].at_load and not helpers["Wake"].registered and helpers["Wake"].deleted_by == ()


def test_survey_lists_the_globals_other_scripts_read() -> None:
    """A global the main chunk sets is listed when another script reads it, with the kind of value it ends with."""
    found = _survey().globals
    assert set(found) == {"true", "Level", "Hidden", "Menu"}  # `false` and `Scratch` are never read
    assert found["true"] == script_helpers.GlobalScan(scripts=1, value="number")
    assert found["Hidden"] == script_helpers.GlobalScan(scripts=1, value="nil", cleared=True)
    assert found["Menu"].value == "table"


def test_survey_hashes_the_chunk() -> None:
    """The chunk is identified by its SHA-1, which the page prints; nothing else of it is kept."""
    import hashlib

    assert _survey().chunk_sha1 == hashlib.sha1(_chunk("global")).hexdigest()


def test_apply_writes_scan_lines_and_parse_reads_them() -> None:
    """The survey becomes one `scan:` line per entry and the chunk's hash; hand-written text is kept as it was."""
    text, added, gone = script_helpers.apply_survey(DESCRIBED, _survey())
    assert (added, gone) == ([], [])
    assert "    scan: {function: 1, params: 2, natives: [BindWake], helpers: [Count], scripts: 2, calls: 3}\n" in text
    assert "    scan: {function: 2, params: 0, natives: [BindLevel], scripts: 0, calls: 0, at_load: true," in text
    assert "    scan: {scripts: 1, value: nil, cleared: true}\n" in text
    assert "      Counts a table's entries.\n" in text
    loaded = script_helpers.parse(text)
    assert loaded.problems == []
    assert loaded.chunk_sha1 == _survey().chunk_sha1
    assert [h.name for h in loaded.helpers] == ["Count", "Wake", "Boot", "Later", "Menu.Open"]
    assert loaded.helpers[1].scan == _survey().helpers["Wake"]
    assert loaded.helpers[1].evidence == "inferred" and loaded.helpers[0].evidence == "confirmed-code"
    assert loaded.globals[0].scan == _survey().globals["true"]
    # A second run changes nothing.
    assert script_helpers.apply_survey(text, _survey())[0] == text


def test_apply_adds_skeletons_and_reports_what_the_disc_lacks() -> None:
    """A helper the file lacks gets an entry to fill in (which `parse` refuses until it is); an entry the disc no
    longer has is reported."""
    shorter = DESCRIBED.replace(
        "  - name: Count\n    group: tables\n    params: [list]\n",
        "  - name: Gone\n    group: tables\n    params: [list]\n",
    )
    text, added, gone = script_helpers.apply_survey(shorter, _survey())
    assert (added, gone) == (["Count"], ["Gone"])
    assert '  - name: Count\n    group: ""\n    params: [p1]\n' in text
    problems = script_helpers.parse(text).problems
    assert any("Count" in p and "group" in p for p in problems)
    assert any("Gone" in p and "scan" in p for p in problems)


@pytest.mark.parametrize(
    ("change", "expected"),
    [
        (("group: squad", "group: weather"), "`group` is 'weather'"),
        (("params: [who, quick]", "params: [who]"), "1 parameter(s) named, the chunk has 2"),
        (("params: [who, quick]", "params: [who, who]"), "parameter `who` named twice"),
        (("evidence: inferred", "evidence: probably"), "`evidence` is 'probably'"),
        (("description: Wakes someone up.", 'description: ""'), "missing `description`"),
        (("    evidence: inferred\n", "    evidence: inferred\n    colour: red\n"), "unknown helper key `colour`"),
        (("  - name: Boot", "  - name: Wake"), "Wake listed twice"),
    ],
)
def test_parse_reports_problems(change: tuple[str, str], expected: str) -> None:
    """Each mistake in the file is a problem naming the entry; nothing is raised."""
    text = script_helpers.apply_survey(DESCRIBED, _survey())[0].replace(*change)
    problems = script_helpers.parse(text).problems
    assert any(expected in p for p in problems), problems


def _masterlist() -> natives.Masterlist:
    """Bindings for the fixture's natives: two described ones and the rest left out."""
    entries = [
        {
            "name": "BindWake",
            "category": "character",
            "wrapper": 0x00100000,
            "description": "Wakes.",
            "evidence": "confirmed-code",
            "depth": "brief",
        },
        {
            "name": "BindShow",
            "category": "hud",
            "wrapper": 0x00100100,
            "description": "Shows.",
            "evidence": "confirmed-code",
            "depth": "brief",
        },
    ]
    return natives.parse(yaml.safe_dump(entries))


def _page() -> str:
    """The page of the described fixture."""
    loaded = script_helpers.parse(script_helpers.apply_survey(DESCRIBED, _survey())[0])
    return script_helpers_render.page(loaded, _masterlist())


def test_page_has_an_entry_per_helper_grouped() -> None:
    """Each helper gets a section under its group with its signature, description, evidence and facts; the natives
    link to their bindings, and each helper lists the helpers that call it."""
    page = _page()
    assert page.startswith(script_helpers_render.GENERATED)
    assert "## Squad {#squad}" in page
    assert "### Wake {#wake}" in page
    assert "Wake(who, quick)" in page
    assert "- **Evidence:** inferred at `global.lua:f1`" in page
    assert "- **Evidence:** confirmed (code) at `global.lua:f0`" in page
    assert "[`BindWake`](character.md#bindwake)" in page
    assert "`BindSchedule`" in page and "(scheduled" not in page
    assert "**Called by helpers:** [`Later`](#later), [`Menu.Open`](#menuopen)" in page
    assert "**Called by** 2 scripts (3 calls)" in page
    assert "### Menu.Open {#menuopen}" in page


def test_page_flags_load_time_registered_and_deleted_helpers() -> None:
    """A helper that runs while `global.lua` loads, one the engine calls by name and one that is deleted say so."""
    page = _page()
    assert "runs while `global.lua` loads" in page
    assert "deleted by [`Boot`](#boot)" in page
    assert "registered by `global.lua` as a callback" in page


def test_page_lists_the_globals() -> None:
    """The globals table gives each one's value, the scripts that read it and our description."""
    page = _page()
    assert "| `true` | number | 1 | The number 1. |" in page
    assert "| `Hidden` | nil (cleared at the end of `global.lua`) | 1 | Set, then cleared. |" in page


def test_bindings_render_writes_the_helper_page(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """`bindings render` writes the helper page beside the binding pages when the data file exists, and `--check`
    fails when it is stale; the masterlist's index links it."""
    (tmp_path / "coney.local.example.toml").write_text("", encoding="utf-8")
    (tmp_path / "research" / "bindings").mkdir(parents=True)
    (tmp_path / natives.DATA_DIR / "character.yaml").write_text(
        "- name: BindWake\n  wrapper: 0x00100000\n  description: Wakes.\n  evidence: confirmed-code\n  depth: brief\n"
        + USAGE,
        encoding="utf-8",
    )
    (tmp_path / script_helpers.DATA_FILE).write_text(
        script_helpers.apply_survey(DESCRIBED, _survey())[0], encoding="utf-8"
    )
    monkeypatch.chdir(tmp_path)
    assert main(["bindings", "render"]) == 0
    page = tmp_path / natives.PAGES_DIR / script_helpers.PAGE
    assert "### Wake {#wake}" in page.read_text(encoding="utf-8")
    assert "(helpers.md)" in (tmp_path / natives.PAGES_DIR / "index.md").read_text(encoding="utf-8")
    page.write_text("edited\n", encoding="utf-8")
    assert main(["bindings", "render", "--check"]) == 1
    assert "helpers.md" in capsys.readouterr().out


def test_bindings_render_refuses_a_broken_helper_file(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """Problems in the helper file are printed and nothing is written."""
    (tmp_path / "coney.local.example.toml").write_text("", encoding="utf-8")
    (tmp_path / natives.DATA_DIR).mkdir(parents=True)
    (tmp_path / script_helpers.DATA_FILE).write_text(DESCRIBED.replace("group: squad", "group: weather"), "utf-8")
    monkeypatch.chdir(tmp_path)
    assert main(["bindings", "render"]) == 1
    assert "weather" in capsys.readouterr().out
    assert not (tmp_path / natives.PAGES_DIR).exists()


def test_scan_helpers_reads_the_chunks_and_rewrites_the_file(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """`bindings scan-helpers` with a folder of chunks sets the scan lines; `--check` fails until it has."""
    chunks = tmp_path / "chunks"
    chunks.mkdir()
    for name in ("global", "mission", "intro"):
        (chunks / f"{name}.lua").write_bytes(_chunk(name))
    (tmp_path / "coney.local.example.toml").write_text("", encoding="utf-8")
    (tmp_path / natives.DATA_DIR).mkdir(parents=True)
    (tmp_path / natives.DATA_DIR / "util.yaml").write_text(
        "".join(
            f"- name: {name}\n  wrapper: 0x00100000\n  description: x\n  evidence: inferred\n  depth: brief\n{USAGE}"
            for name in sorted(NATIVES)
        ),
        encoding="utf-8",
    )
    data = tmp_path / script_helpers.DATA_FILE
    data.write_text(DESCRIBED, encoding="utf-8")
    monkeypatch.chdir(tmp_path)
    assert main(["bindings", "scan-helpers", "--chunks", str(chunks), "--check"]) == 1
    assert main(["bindings", "scan-helpers", "--chunks", str(chunks)]) == 0
    assert "scripts: 2, calls: 3" in data.read_text(encoding="utf-8")
    assert main(["bindings", "scan-helpers", "--chunks", str(chunks), "--check"]) == 0
    assert "5 helpers, 4 globals" in capsys.readouterr().out
