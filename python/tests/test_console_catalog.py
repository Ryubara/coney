# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the console's command catalogs and their generators (`coney-tools console render-cpp` and `render`), on
made-up catalogs, and that the checkout's generated table and reference are current."""

from pathlib import Path

import pytest

from coney_tools import console_catalog, console_cpp, console_render
from coney_tools.cli import main

ROOT = Path(__file__).resolve().parents[2]

# Two invented commands: a session one with arguments of each kind of default, and a local reading.
GOOD = """
[[command]]
name = "start-wave"
group = "Waves"
access = "player"
scope = "session"
summary = "Starts a wave."
needs = ["game", "level"]
available = "waves"
args = [
  { name = "count", type = "int", help = "How many" },
  { name = "side", type = "choice", choices = ["left", "right"], default = "left", help = "Where from" },
  { name = "note", type = "text", optional = true, help = "A \\"quoted\\" note" },
]
examples = [{ line = "start-wave 3 right", output = ["wave of 3 from the right"] }]

[[command]]
name = "status"
group = "Waves"
access = "player"
scope = "local"
summary = "Says how the wave goes."
doubles = "none: a reading"
examples = [{ line = "status", output = [] }]
"""


def _parse(text: str, access: str = "player") -> console_catalog.Catalog:
    """The catalog `text` as the player file."""
    return console_catalog.parse(text, console_catalog.PLAYER_FILE, access)


def _checkout(root: Path, player: str, developer: str | None = None) -> None:
    """A made-up checkout holding the catalogs."""
    (root / "coney.local.example.toml").write_text("", encoding="utf-8")
    (root / "src" / "console").mkdir(parents=True)
    (root / console_catalog.PLAYER_FILE).write_text(player, encoding="utf-8")
    if developer is not None:
        (root / "src" / "debug").mkdir(parents=True)
        (root / console_catalog.DEVELOPER_FILE).write_text(developer, encoding="utf-8")


def test_parse_reads_every_field() -> None:
    catalog = _parse(GOOD)
    assert catalog.problems == []
    wave, status = catalog.commands
    assert wave.usage == "start-wave <count> [side] [note]"
    assert wave.queued and not status.queued
    assert wave.needs == ("game", "level")
    assert wave.args[1].optional and wave.args[1].default == "left"
    assert wave.args[1].choices == ("left", "right")
    assert status.examples[0].output == ()


@pytest.mark.parametrize(
    ("change", "problem"),
    [
        (('access = "player"', 'access = "developer"'), "this file holds the player set"),
        (('scope = "session"', 'scope = "world"'), "scope is local or session"),
        (('type = "int"', 'type = "integer"'), "type is one of"),
        (('name = "start-wave"', 'name = "Start Wave"'), "a name is lower-case"),
        (('needs = ["game", "level"]', 'needs = ["money"]'), "needs lists some of"),
        (('summary = "Starts a wave."', 'summary = "Starts a wave."\ncolour = "red"'), "unknown key(s) colour"),
        (('choices = ["left", "right"], ', ""), "a choice argument lists its choices"),
        (('line = "start-wave 3 right"', 'line = "wave 3"'), "does not start with the command's name"),
        (('default = "left"', 'default = "up"'), "the default 'up' is not a value of type choice"),
        (
            ('{ name = "count", type = "int", help', '{ name = "count", type = "int", default = "many", help'),
            "the default 'many' is not a value of type int",
        ),
    ],
)
def test_parse_refuses_mistakes(change: tuple[str, str], problem: str) -> None:
    catalog = _parse(GOOD.replace(*change, 1))
    assert any(problem in line for line in catalog.problems), catalog.problems


@pytest.mark.parametrize(
    ("kind", "default", "fits"),
    [
        ("int", "-5", True),
        ("int", "9223372036854775807", True),
        ("int", "-9223372036854775808", True),
        ("int", "9223372036854775808", False),
        ("int", "+1", False),
        ("int", "1_0", False),
        ("int", " 1", False),
        ("int", "\u0662\u0662", False),
        ("number", "-1.25", True),
        ("number", "2e-3", True),
        ("number", "3", True),
        ("number", "+1", False),
        ("number", " 1 ", False),
        ("number", "1_0", False),
        ("number", "\u0662", False),
        ("number", "0x10", False),
        ("number", "nan", False),
        ("number", "1e999", False),
        ("mission", "3", True),
        ("mission", "0", False),
        ("mission", "-1", False),
        ("mission", "+1", False),
        ("mission", "\u0662", False),
        ("checkpoint", "99999999999999999999", False),
        ("level", "docks_1", True),
        ("level", "d\u00f6cks", False),
    ],
)
def test_a_default_fits_exactly_what_the_registry_takes(kind: str, default: str, fits: bool) -> None:
    # The C++ check (arg_types.cpp isWholeNumber, parseDecimal, isName) refuses a default the generator let through,
    # and registerBuiltins stops there; so the two must agree.
    text = GOOD.replace(
        '{ name = "count", type = "int", help', f'{{ name = "count", type = "{kind}", default = "{default}", help'
    )
    problems = [p for p in _parse(text).problems if "is not a value of type" in p]
    assert (problems == []) is fits, problems


def test_parse_refuses_a_command_without_an_example() -> None:
    text = GOOD.replace('examples = [{ line = "status", output = [] }]', "")
    assert any("at least one example" in p for p in _parse(text).problems)
    text = GOOD.replace('examples = [{ line = "status", output = [] }]', "examples = []")
    assert any("at least one example" in p for p in _parse(text).problems)


def test_parse_refuses_a_required_argument_after_an_optional_one() -> None:
    text = GOOD.replace('{ name = "note", type = "text", optional = true,', '{ name = "note", type = "text",')
    assert any("follows an optional one" in p for p in _parse(text).problems)


def test_parse_refuses_a_name_used_twice() -> None:
    status = "[[command]]" + GOOD.split("[[command]]")[2]
    assert any("used twice" in p for p in _parse(GOOD + status).problems)


def test_load_refuses_a_name_in_both_catalogs(tmp_path: Path) -> None:
    developer = GOOD.replace('access = "player"', 'access = "developer"')
    _checkout(tmp_path, GOOD, developer)
    player, dev = console_catalog.load(tmp_path)
    assert player.problems == []
    assert any("start-wave is in both catalogs" in p for p in dev.problems)


def test_render_cpp_writes_the_table() -> None:
    text = console_cpp.render_cpp(_parse(GOOD))
    assert 'constexpr std::array<std::string_view, 2> kChoices_start_wave_side{"left", "right"};' in text
    assert '{"side", A::Choice, "Where from", true, "left", kChoices_start_wave_side},' in text
    assert r'"A \"quoted\" note"' in text
    assert "static_cast<Needs>(needBit(Need::Game) | needBit(Need::Level))" in text
    assert '{"status", "Waves", Access::Player, Scope::Local, false,' in text
    assert 'Starts a wave.", static_cast<Needs>' in text and '"waves", kArgs_start_wave},' in text
    assert "std::span<const CommandSpec> playerCatalog() { return kCommands; }" in text


def test_render_page_has_a_table_then_an_entry_with_an_example_per_command() -> None:
    text = console_render.page([_parse(GOOD)])
    assert "| [`start-wave`](#start-wave) | Waves | player | session | Starts a wave. |" in text
    assert "## start-wave {#start-wave}" in text
    assert "```text\nstart-wave <count> [side] [note]\n```" in text
    assert "| 2 | `side` | choice: `left`, `right` | `left` | Where from |" in text
    assert "**scope:** session, waits for the step boundary" in text
    assert "- **Needs:** a game in play, a level in play" in text
    assert "`waves`; absent until it is built" in text
    assert "```text\n> start-wave 3 right\nwave of 3 from the right\n```" in text
    assert "```text\n> status\n```\n\nIt prints nothing." in text
    fenced = False
    for line in text.splitlines():
        fenced ^= line.startswith("```")
        assert fenced or line.startswith(("|", "```")) or len(line) <= console_render.WIDTH, line


def test_render_and_check(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
    """Both generators write their file; `--check` passes on fresh output and fails on an edited or missing one."""
    _checkout(tmp_path, GOOD)
    monkeypatch.chdir(tmp_path)
    for command, path in (("render-cpp", console_cpp.CPP_FILE), ("render", console_render.PAGE)):
        assert main(["console", command, "--check"]) == 1
        assert main(["console", command]) == 0
        assert main(["console", command, "--check"]) == 0
        # A CRLF checkout of the same text is still current.
        full = tmp_path / path
        full.write_bytes(full.read_bytes().replace(b"\n", b"\r\n"))
        assert main(["console", command, "--check"]) == 0
        full.write_text("edited\n", encoding="utf-8")
        assert main(["console", command, "--check"]) == 1
        assert "is stale" in capsys.readouterr().out


def test_render_check_fails_on_a_command_without_an_example(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    _checkout(tmp_path, GOOD.replace('examples = [{ line = "status", output = [] }]', ""))
    monkeypatch.chdir(tmp_path)
    assert main(["console", "render", "--check"]) == 1
    assert "needs at least one example" in capsys.readouterr().out
    assert not (tmp_path / console_render.PAGE).exists()


@pytest.mark.reads("docs_only", "engine")
def test_the_checkouts_table_and_reference_are_current() -> None:
    """The generated table (src/console/commands_table.cpp) and the reference page match src/console/commands.toml,
    as CI's `--check` runs insist."""
    catalogs = console_catalog.load(ROOT)
    assert [p for c in catalogs for p in c.problems] == []
    table = (ROOT / console_cpp.CPP_FILE).read_bytes().decode("utf-8").replace("\r\n", "\n")
    assert table == console_cpp.render_cpp(catalogs[0]), "run `coney-tools console render-cpp`"
    page = (ROOT / console_render.PAGE).read_bytes().decode("utf-8").replace("\r\n", "\n")
    assert page == console_render.page(catalogs), "run `coney-tools console render`"
