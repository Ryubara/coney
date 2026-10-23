# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools research render` and `research import-table`, end to end on a made-up checkout."""

from __future__ import annotations

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
