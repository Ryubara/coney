# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `research render`, `research where` and `research import-table`, end to end on a made-up checkout."""

from __future__ import annotations

import json
from pathlib import Path

import pytest
from test_research_functions_check import ENTRY, make_root

from coney_tools import research_functions as rf
from coney_tools.cli import main

TABLE_PAGE = """\
# Throws

## Code index {#code-index}

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x00100200` | `Grab_Start` | plays the grab intro | confirmed (code) |
| `0x00100300` | `Grab_End` | ends it, see [grabs](grabs.md#grab) | inferred |
"""


def checkout(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, functions: str = ENTRY) -> Path:
    """A made-up checkout (one page, a listing, a `combat` file) that the command finds from the working directory."""
    root = make_root(tmp_path, functions, listing={0x100200: "Grab_Start", 0x100300: "Grab_End"})
    (root / "coney.local.example.toml").write_text("", encoding="utf-8")
    monkeypatch.chdir(root)
    return root


def test_check_passes_on_a_consistent_checkout(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """A clean database exits 0 and says how many functions it holds."""
    checkout(tmp_path, monkeypatch)
    assert main(["research", "render", "--check"]) == 0
    assert "1 function in 1 system" in capsys.readouterr().out


def test_check_fails_with_every_problem(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """Problems go to stderr, one per line, and the exit status is 1."""
    checkout(tmp_path, monkeypatch, ENTRY.replace("#grab", "#nowhere").replace("grab.cpp", "gone.cpp"))
    assert main(["research", "render", "--check"]) == 1
    err = capsys.readouterr().err
    assert "nowhere" in err
    assert "gone.cpp" in err


def test_a_warning_alone_does_not_fail_the_check(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """A name Ghidra has not got yet is printed and passes."""
    root = checkout(tmp_path, monkeypatch)
    listing = root / "docs" / "progress" / "ghidra-functions.tsv"
    listing.write_text("0x00100200\t64\t\t1\n", encoding="utf-8")
    assert main(["research", "render", "--check"]) == 0
    assert "warning" in capsys.readouterr().err


def test_render_writes_the_pages_below_the_folder(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """`render OUT` writes `OUT/research/<system>/functions.md`, the path the docs build gives it."""
    checkout(tmp_path / "repo", monkeypatch)
    out = tmp_path / "out"
    assert main(["research", "render", str(out)]) == 0
    page = (out / "research" / "combat" / "functions.md").read_text(encoding="utf-8")
    assert "# Function list: combat" in page
    assert "`Grab_Start`" in page


def test_render_writes_nothing_while_the_data_is_wrong(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """The pages are made from checked data only."""
    checkout(tmp_path / "repo", monkeypatch, ENTRY.replace("#grab", "#nowhere"))
    out = tmp_path / "out"
    assert main(["research", "render", str(out)]) == 1
    assert not out.exists()


def test_render_needs_a_folder_or_check(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """With neither a folder nor `--check` there is nothing to do, which is a usage error."""
    checkout(tmp_path, monkeypatch)
    assert main(["research", "render"]) == 2
    assert main(["research", "render", str(tmp_path / "o"), "--check"]) == 2


def import_args(*extra: str, system: str = "throws") -> list[str]:
    """The arguments of an import of the made-up throws page."""
    return ["research", "import-table", "docs/research/combat/throws.md", "--system", system, *extra]


def with_throws_page(root: Path) -> None:
    """Add the page whose table the tests import."""
    (root / "docs" / "research" / "combat" / "throws.md").write_text(TABLE_PAGE, encoding="utf-8")


def test_import_writes_the_system_file_and_reports(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """The rows land in `research/functions/<system>.toml`, in the canonical format, and the report counts them."""
    root = checkout(tmp_path, monkeypatch, "")
    with_throws_page(root)
    assert main(import_args()) == 0
    out = capsys.readouterr().out
    assert "2 rows imported" in out
    assert "2 new" in out
    system = rf.load_system(root / "research" / "functions" / "throws.toml")
    assert [(e.name, e.page) for e in system.entries] == [
        ("Grab_Start", "combat/throws.md#code-index"),
        ("Grab_End", "combat/throws.md#code-index"),
    ]
    assert system.entries[1].role == "ends it, see [grabs](combat/grabs.md#grab)"
    assert main(["research", "render", "--check"]) == 0


def test_import_twice_changes_nothing(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """Re-running the import on an unchanged page leaves the file as it was."""
    root = checkout(tmp_path, monkeypatch, "")
    with_throws_page(root)
    path = root / "research" / "functions" / "throws.toml"
    assert main(import_args()) == 0
    first = path.read_bytes()
    assert main(import_args()) == 0
    assert path.read_bytes() == first


def test_dry_run_writes_nothing(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """`--dry-run` prints the report and leaves the file alone."""
    root = checkout(tmp_path, monkeypatch, "")
    with_throws_page(root)
    assert main(import_args("--dry-run")) == 0
    assert "would write" in capsys.readouterr().out
    assert not (root / "research" / "functions" / "throws.toml").exists()


def test_import_keeps_the_header_and_a_hand_edit(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """A second import adds rows, keeps the file's comment header, and does not overwrite an edited entry."""
    root = checkout(tmp_path, monkeypatch, "")
    with_throws_page(root)
    assert main(import_args()) == 0
    path = root / "research" / "functions" / "throws.toml"
    text = path.read_text(encoding="utf-8").replace("# The researched", "# My header\n# The researched", 1)
    text = text.replace('role = "plays the grab intro"', 'role = "better role"')
    path.write_text(text, encoding="utf-8")
    assert main(import_args()) == 0
    again = path.read_text(encoding="utf-8")
    assert again.startswith("# My header")
    assert 'role = "better role"' in again


def test_import_leaves_out_a_function_another_system_owns(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """The `combat` file already lists Grab_Start, so importing it into `throws` skips it."""
    root = checkout(tmp_path, monkeypatch)
    with_throws_page(root)
    assert main(import_args()) == 0
    system = rf.load_system(root / "research" / "functions" / "throws.toml")
    assert [e.name for e in system.entries] == ["Grab_End"]
    assert "combat.toml" in capsys.readouterr().out


def test_import_reports_what_the_check_would_say(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """A row whose name Ghidra does not have is imported and flagged, so the engineer fixes it at once."""
    root = checkout(tmp_path, monkeypatch, "")
    (root / "docs" / "research" / "combat" / "throws.md").write_text(
        TABLE_PAGE.replace("Grab_End", "Grab_Finish"), encoding="utf-8"
    )
    assert main(import_args()) == 0
    out = capsys.readouterr().out
    assert "Grab_Finish" in out
    assert "Grab_End" in out


def test_import_section(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
    """`--section` that the page lacks is a usage error."""
    root = checkout(tmp_path, monkeypatch, "")
    with_throws_page(root)
    assert main(import_args("--section", "nowhere")) == 2
    assert "nowhere" in capsys.readouterr().err
    assert main(import_args("--section", "code-index")) == 0


@pytest.mark.parametrize(
    "args",
    [
        ["research", "import-table", "docs/research/combat/missing.md", "--system", "throws"],
        ["research", "import-table", "docs/guides/testing.md", "--system", "throws"],
        ["research", "import-table", "docs/research/combat/throws.md", "--system", "Bad Name"],
    ],
)
def test_import_refuses_a_page_or_system_it_cannot_use(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, args: list[str]
) -> None:
    """A missing page, a page outside docs/research and a bad system name are usage errors."""
    root = checkout(tmp_path, monkeypatch, "")
    with_throws_page(root)
    (root / "docs" / "guides").mkdir()
    (root / "docs" / "guides" / "testing.md").write_text("# T\n", encoding="utf-8")
    assert main(args) == 2


def test_import_of_a_page_without_a_function_table_fails(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """Nothing to import is reported, not written as an empty file."""
    root = checkout(tmp_path, monkeypatch, "")
    (root / "docs" / "research" / "combat" / "throws.md").write_text("# Throws\n\nProse only.\n", encoding="utf-8")
    assert main(import_args()) == 1
    assert "no function" in capsys.readouterr().err
    assert not (root / "research" / "functions" / "throws.toml").exists()


# --- the source files ------------------------------------------------------------------------------------------------

SOURCE_FILES = """\
[[file]]
directory = "Combat"
path = "Combat/Grab.cpp"
start = 0x00100000
end = 0x00100400
functions = 2
"""
TOTALS = """\
[text]
start = 0x00100000
end = 0x00300000

[[coverage]]
key = "k"
label = "k"
kind = "researched"
bytes = 2000000

[[subsystem]]
name = "Combat"
ranges = [[0x00100000, 0x00100800]]
"""


def with_source_files(root: Path, files: str = SOURCE_FILES, totals: str = TOTALS) -> None:
    """Add the source files data and the totals file it is held to."""
    (root / "research").mkdir(exist_ok=True)
    (root / "research" / "source-files.toml").write_text(files, encoding="utf-8")
    (root / "docs" / "progress").mkdir(parents=True, exist_ok=True)
    (root / "docs" / "progress" / "totals.toml").write_text(totals, encoding="utf-8")


def test_check_covers_the_source_files_too(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """A source file outside its directory's range in totals.toml fails the check, and a good one passes."""
    root = checkout(tmp_path, monkeypatch)
    with_source_files(root)
    assert main(["research", "render", "--check"]) == 0
    with_source_files(root, totals=TOTALS.replace("0x00100800", "0x00100200"))
    assert main(["research", "render", "--check"]) == 1
    assert "Combat/Grab.cpp" in capsys.readouterr().err


def test_check_reports_a_malformed_source_files_file(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """A schema problem in research/source-files.toml is an error line, like one in the function database."""
    root = checkout(tmp_path, monkeypatch)
    with_source_files(root, SOURCE_FILES.replace("functions = 2", "functions = 0"))
    assert main(["research", "render", "--check"]) == 1
    assert "source-files.toml" in capsys.readouterr().err


SOURCE_MAP = "# Source map\n\n## Files\n\n<!-- source-files:generated -->\n"


def test_render_writes_the_source_map_with_its_tables_filled(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """`render OUT` writes `OUT/research/source-map.md` as the docs build shows it, beside the function lists."""
    root = checkout(tmp_path / "repo", monkeypatch)
    with_source_files(root)
    (root / "docs" / "research" / "source-map.md").write_text(SOURCE_MAP, encoding="utf-8")
    out = tmp_path / "out"
    assert main(["research", "render", str(out)]) == 0
    page = (out / "research" / "source-map.md").read_text(encoding="utf-8")
    assert "#### Combat" in page
    assert "`Grab.cpp`" in page
    assert "source-files:generated" not in page
    assert (out / "research" / "combat" / "functions.md").is_file()


def test_check_wants_the_marker_in_the_source_map(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """A source map without its marker would show no files; the check says so."""
    root = checkout(tmp_path, monkeypatch)
    with_source_files(root)
    page = root / "docs" / "research" / "source-map.md"
    page.write_text(SOURCE_MAP, encoding="utf-8")
    assert main(["research", "render", "--check"]) == 0
    page.write_text("# Source map\n", encoding="utf-8")
    assert main(["research", "render", "--check"]) == 1
    assert "exactly one" in capsys.readouterr().err


def with_glossary(root: Path, text: str) -> None:
    """Add the glossary file."""
    (root / "research").mkdir(exist_ok=True)
    (root / "research" / "glossary.yaml").write_text(text, encoding="utf-8")


GLOSSARY = "- term: grab\n  page: combat/grabs.md#grab\n  functions: [0x00100200]\n  also: [hold]\n"


def test_render_writes_the_find_pages(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """`render OUT` writes the address page, and the words page once the glossary file exists."""
    root = checkout(tmp_path / "repo", monkeypatch)
    out = tmp_path / "out"
    assert main(["research", "render", str(out)]) == 0
    assert (out / "research" / "find" / "addresses.md").is_file()
    assert not (out / "research" / "find" / "words.md").exists()
    with_glossary(root, GLOSSARY)
    assert main(["research", "render", str(out)]) == 0
    words = (out / "research" / "find" / "words.md").read_text(encoding="utf-8")
    assert "| grab | [combat/grabs.md#grab](../combat/grabs.md#grab) | `0x00100200` `Grab_Start` | hold |" in words


def test_check_covers_the_glossary(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """A word whose page, section or function does not exist fails the check, naming the word."""
    root = checkout(tmp_path, monkeypatch)
    with_glossary(root, GLOSSARY)
    assert main(["research", "render", "--check"]) == 0
    with_glossary(root, GLOSSARY.replace("#grab", "#nowhere").replace("0x00100200", "0x00100999"))
    assert main(["research", "render", "--check"]) == 1
    err = capsys.readouterr().err
    assert "grab: the page docs/research/combat/grabs.md has no section 'nowhere'" in err
    assert "0x00100999 is no function entry" in err


def test_check_reports_a_malformed_glossary(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """A glossary that is no list of entries is an error line naming the file."""
    root = checkout(tmp_path, monkeypatch)
    with_glossary(root, "term: grab\n")
    assert main(["research", "render", "--check"]) == 1
    assert "research/glossary.yaml" in capsys.readouterr().err


def test_where_answers_an_address(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """An address gives the function, its owning page and its database entry."""
    checkout(tmp_path, monkeypatch)
    assert main(["research", "where", "0x00100200"]) == 0
    out = capsys.readouterr().out
    assert out.startswith("0x00100200 Grab_Start (function, 64 bytes)")
    assert "owning page: none" in out
    assert "function database: combat.toml: confirmed-code" in out


def test_where_answers_a_name_and_a_glossary_word(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """A Ghidra name is an address query, and a glossary word names its page and functions."""
    root = checkout(tmp_path, monkeypatch)
    with_glossary(root, GLOSSARY)
    assert main(["research", "where", "Grab_Start"]) == 0
    assert capsys.readouterr().out.startswith("0x00100200 Grab_Start")
    assert main(["research", "where", "hold"]) == 0
    out = capsys.readouterr().out
    assert "the glossary term 'grab'" in out
    assert "function: 0x00100200 Grab_Start" in out


def test_where_prints_json(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
    """`--json` prints one document with the same facts."""
    checkout(tmp_path, monkeypatch)
    assert main(["research", "where", "0x00100200", "--json"]) == 0
    data = json.loads(capsys.readouterr().out)
    assert data["query"] == "0x00100200"
    assert data["addresses"][0]["name"] == "Grab_Start"


def test_where_exits_1_with_a_hint_when_nothing_matches(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """Nothing found is exit 1 and a line saying what to give, in JSON mode too."""
    checkout(tmp_path, monkeypatch)
    assert main(["research", "where", "zzzz"]) == 1
    assert "nothing found for 'zzzz'" in capsys.readouterr().err
    assert main(["research", "where", "zzzz", "--json"]) == 1


def test_where_exits_2_for_a_query_that_is_no_address_and_for_bad_data(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """`0xzz` is a usage error, and a malformed data file is a configuration error."""
    root = checkout(tmp_path, monkeypatch)
    assert main(["research", "where", "0xzz"]) == 2
    assert "not an address" in capsys.readouterr().err
    with_glossary(root, "term: grab\n")
    assert main(["research", "where", "0x00100200"]) == 2
