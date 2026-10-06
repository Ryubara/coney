# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the script-binding masterlist, on a made-up YAML file: invented bindings, addresses and counts."""

from pathlib import Path

import pytest
import yaml

from coney_tools import natives, natives_cpp, natives_render
from coney_tools.cli import main
from coney_tools.config import ConfigError

# Three invented bindings: a described one with a table argument, an overloaded one, and an unused mechanical one.
GOOD = """
- name: MakeThing
  category: world
  wrapper: 0x00100000
  calls:
    - {addr: 0x00200000, name: Thing_Make}
  args:
    - {name: label, lua: string, desc: "Name of the thing."}
    - {name: pos, lua: table, elem: number, count: 3, written_back: true, desc: "Where, in metres."}
    - {name: visible, lua: boolean, default: true, desc: "Whether it is drawn."}
  results:
    - {lua: number, desc: "The thing's handle."}
  description: "Makes a thing."
  evidence: confirmed-code
  depth: thorough
  usage: {chunks: 2, calls: 5, boot: true, mission1: false, result_used: true}
- name: PlayIt
  category: sound
  wrapper: 0x00100100
  calls:
    - {addr: 0x00200100}
  args:
    - {name: id, lua: number, ctype: int, desc: "Track id."}
  description: "Plays a track."
  evidence: inferred
  depth: brief
  overloads:
    - wrapper: 0x00100080
      args:
        - {name: track, lua: string, desc: "Track name."}
      note: "Used when the argument is a string."
  usage: {chunks: 1, calls: 1, boot: false, mission1: true, result_used: false, levels: [80, 95]}
- name: unusedThing
  category: debug
  wrapper: 0x00100200
  args:
    - {name: n, lua: number, ctype: unsigned, desc: ""}
  description: ""
  evidence: confirmed-code
  depth: mechanical
  usage: {chunks: 0, calls: 0, boot: false, mission1: false, result_used: false}
"""


def _checkout(root: Path, text: str) -> None:
    """A made-up checkout holding the entries of `text`, one research/bindings/<category>.yaml per category."""
    (root / "coney.local.example.toml").write_text("", encoding="utf-8")
    folder = root / "research" / "bindings"
    folder.mkdir(parents=True)
    by_category: dict[str, list[object]] = {}
    for entry in yaml.safe_load(text):
        by_category.setdefault(entry.pop("category"), []).append(entry)
    for category, entries in by_category.items():
        (folder / f"{category}.yaml").write_text(yaml.safe_dump(entries, sort_keys=False), encoding="utf-8")


def test_parse_reads_every_field() -> None:
    """A valid file loads without problems, with defaults filled in."""
    masterlist = natives.parse(GOOD)
    assert masterlist.problems == []
    make, play, unused = masterlist.bindings
    assert make.main.wrapper == 0x00100000
    assert make.main.calls == (natives.Call(0x00200000, "Thing_Make"),)
    assert make.main.args[1].count == 3 and make.main.args[1].written_back
    assert make.main.args[2].default is True
    assert make.origin == "game" and make.coney == "not implemented" and make.registered_by == "RegisterBindings"
    assert play.overloads[0].args[0].lua == "string"
    assert unused.usage is not None and unused.usage.calls == 0
    assert unused.anchor == "unusedthing"


@pytest.mark.parametrize(
    ("change", "expected"),
    [
        (("category: world", "category: weather"), "`category` is 'weather'"),
        (("evidence: inferred", "evidence: likely"), "`evidence` is 'likely'"),
        (('description: "Plays a track."', 'description: ""'), "needs a `description`"),
        (('{name: label, lua: string, desc: "Name of the thing."}', "{name: label, lua: string}"), "has no `desc`"),
        (('lua: string, desc: "Name', 'lua: text, desc: "Name'), "`lua` is 'text'"),
        (("{name: id, lua: number, ctype: int", "{name: id, lua: string, ctype: int"), "only for number arguments"),
        (("elem: number, count: 3,", "count: 3,"), "needs `elem: number`"),
        (("chunks: 2, calls: 5", "chunks: 6, calls: 5"), "more chunks than calls"),
        (("chunks: 0, calls: 0, boot: false", "chunks: 0, calls: 0, boot: true"), "never called"),
        (("name: PlayIt", "name: makething"), "listed twice"),
        (("wrapper: 0x00100200", "wrapper: banana"), "must be an address"),
        (("depth: brief", "depth: brief\n  colour: red"), "unknown entry key `colour`"),
        (("{name: visible,", "{name: label,"), "`label` is used twice"),
        (("{name: n, lua: number", "{name: on, lua: number"), "`name` must be a string"),
    ],
)
def test_parse_reports_problems(change: tuple[str, str], expected: str) -> None:
    """Each kind of mistake is reported with the entry it is in."""
    old, new = change
    assert old in GOOD
    problems = natives.parse(GOOD.replace(old, new, 1)).problems
    assert any(expected in p for p in problems), problems


def test_coney_origin_needs_no_wrapper_or_usage() -> None:
    """A Coney-added binding has no wrapper and no game usage; giving it usage is a problem."""
    entry = """
- name: ModLog
  origin: coney
  category: debug
  args:
    - {name: text, lua: string, desc: "What to log."}
  description: "Writes a line to Coney's log."
  evidence: confirmed-code
  depth: thorough
  coney: implemented
"""
    masterlist = natives.parse(entry)
    assert masterlist.problems == []
    assert masterlist.bindings[0].main.wrapper is None
    bad = natives.parse(entry + "  usage: {chunks: 0, calls: 0, boot: false, mission1: false, result_used: false}\n")
    assert any("no game `usage`" in p for p in bad.problems)


def test_parse_rejects_non_lists() -> None:
    """Text that is not YAML, or not a list, is a configuration error."""
    with pytest.raises(ConfigError):
        natives.parse("- [unclosed")
    with pytest.raises(ConfigError):
        natives.parse("name: x\n")


def test_signature_and_sections() -> None:
    """The signature line, the argument table, the overload and the facts all appear."""
    masterlist = natives.parse(GOOD)
    make, play, unused = masterlist.bindings
    assert natives_render.signature(make.name, make.main) == "MakeThing(label, pos, visible) -> number"
    assert natives_render.signature(play.name, play.main) == "PlayIt(id)"
    section = "\n".join(natives_render.binding_section(make))
    assert section.startswith("## MakeThing {#makething}")
    assert "| 2 | `pos` | table of 3 numbers (t[1]..t[3]) | Where, in metres. |" in section
    assert "default true" in section
    assert "`0x00200000` `Thing_Make`" in section
    assert "confirmed (code) at `0x00200000`; detail: traced" in section
    assert "boot to menu: yes; mission 1: no; result used: yes" in section
    assert "**Coney:** not implemented" in section
    overloaded = "\n".join(natives_render.binding_section(play))
    assert "**Overload** (registered first" in overloaded and "Used when the argument is a string." in overloaded
    assert "```lua" + chr(10) + "PlayIt(track)" + chr(10) + "```" in overloaded
    quiet = "\n".join(natives_render.binding_section(unused))
    assert "Not described yet" in quiet and "no script on the disc" in quiet


def test_pages_cover_every_category() -> None:
    """Every category gets a page, the index counts each, and no prose line passes 120 characters."""
    masterlist = natives.parse(GOOD)
    pages = natives_render.render(masterlist)
    assert set(pages) == {"index.md", "mission1.md", "story.md"} | {f"{c}.md" for c in natives.CATEGORIES}
    assert "[`MakeThing`](#makething)" in pages["world.md"]
    # The mission page lists only PlayIt, the one entry marked mission1, with its detail and Coney status.
    row = "| [`PlayIt`](sound.md#playit) | Sound and music | brief | inferred | not implemented |"
    assert row in pages["mission1.md"]
    assert "MakeThing" not in pages["mission1.md"]
    # PlayIt, already used by mission 1, is not new at level80; the story page counts it and lists nothing new there.
    assert "| [`level80`](#level80) | mission 2 | 1 | 0 | 0 | 0 |" in pages["story.md"]
    assert "PlayIt" not in pages["story.md"]
    section = "\n".join(natives_render.binding_section(masterlist.bindings[1]))
    assert "first [`level80`](story.md#level80) (mission 2)" in section
    assert "| **All** | **3** | **2** | **1** | **1** | **1** |" in pages["index.md"]
    assert "../../roadmap.md#script-mods" in pages["index.md"]
    for name, text in pages.items():
        fenced = False
        for line in text.splitlines():
            fenced ^= line.startswith("```")
            # Tables and code blocks are exempt from the line length, as in .markdownlint.yaml.
            assert fenced or line.startswith(("|", "```")) or len(line) <= natives_render.WIDTH, (name, line)


def test_render_and_check(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
    """`render` writes the pages; `--check` passes on fresh pages and fails on edited, missing or stray ones."""
    _checkout(tmp_path, GOOD)
    monkeypatch.chdir(tmp_path)
    folder = tmp_path / natives.PAGES_DIR
    assert main(["natives", "render", "--check"]) == 1
    assert not folder.exists()
    assert main(["natives", "render"]) == 0
    assert main(["natives", "render", "--check"]) == 0
    # A CRLF checkout of the same text is still up to date.
    index = folder / "index.md"
    index.write_bytes(index.read_bytes().replace(b"\n", b"\r\n"))
    assert main(["natives", "render", "--check"]) == 0
    (folder / "world.md").write_text("edited\n", encoding="utf-8")
    (folder / "old.md").write_text("a removed category\n", encoding="utf-8")
    assert main(["natives", "render", "--check"]) == 1
    assert "world.md, old.md" in capsys.readouterr().out
    assert main(["natives", "render"]) == 0
    assert not (folder / "old.md").exists()
    assert main(["natives", "render", "--check"]) == 0


def test_render_refuses_a_broken_file(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """Problems are printed and nothing is written."""
    _checkout(tmp_path, GOOD.replace("category: world", "category: weather"))
    monkeypatch.chdir(tmp_path)
    assert main(["natives", "render"]) == 1
    assert "weather.yaml: not a category" in capsys.readouterr().out
    assert not (tmp_path / natives.PAGES_DIR).exists()


def test_stats(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
    """`stats` prints the counts."""
    _checkout(tmp_path, GOOD)
    monkeypatch.chdir(tmp_path)
    assert main(["natives", "stats"]) == 0
    out = capsys.readouterr().out
    assert "bindings: 3 (4 registrations)" in out
    assert "used by scripts: 2; boot to menu: 1; mission 1: 1" in out


def test_load_checks_the_file_category(tmp_path: Path) -> None:
    """An entry whose category differs from its file's is a problem; entries in one file get the file's category."""
    _checkout(tmp_path, GOOD)
    path = tmp_path / "research" / "bindings" / "debug.yaml"
    assert natives.load(tmp_path).problems == []
    assert {b.category for b in natives.load(tmp_path).bindings} == {"world", "sound", "debug"}
    original = path.read_text(encoding="utf-8")
    path.write_text(original.replace("- name: unusedThing", "- name: unusedThing\n  category: hud"), encoding="utf-8")
    assert any("move it to hud.yaml" in p for p in natives.load(tmp_path).problems)
    # The same entry in two files is listed twice.
    path.write_text(original, encoding="utf-8")
    world = tmp_path / "research" / "bindings" / "world.yaml"
    world.write_text(world.read_text(encoding="utf-8") + original, encoding="utf-8")
    problems = natives.load(tmp_path).problems
    assert any("unusedThing: listed twice" in p for p in problems)


TABLE = """\
// The binding table.
constexpr std::array kBindings{
    // The script system.
    real("GetLevelId"),
    routed("PlayMovie"),

    stub("CameraReset"),
    stub("ObjSpawn", StubResult::Handle),
    recording("CfgChar"),
};
"""


def test_parse_coney_table_maps_kinds_to_statuses() -> None:
    """Real is implemented, routed partial, stubs (recording or not) not implemented."""
    assert natives.parse_coney_table(TABLE) == {
        "GetLevelId": "implemented",
        "PlayMovie": "partial",
        "CameraReset": "not implemented",
        "ObjSpawn": "not implemented",
        "CfgChar": "not implemented",
    }


@pytest.mark.parametrize(
    "text",
    [
        "int x;\n",  # no table
        TABLE.replace('    recording("CfgChar"),\n', '    BindingInfo{"CfgChar"},\n'),  # a line of another shape
        TABLE.replace('stub("CameraReset")', 'real("GetLevelId")'),  # a name twice
        TABLE.replace("};\n", ""),  # no end
    ],
)
def test_parse_coney_table_refuses_what_it_cannot_read(text: str) -> None:
    """A table it cannot read in full is an error, never a partial result."""
    with pytest.raises(ConfigError):
        natives.parse_coney_table(text)


def test_set_coney_statuses_writes_only_non_defaults() -> None:
    """A status other than the default goes last in its entry; a stale line is removed; the rest is kept."""
    text = "# header\n\n- name: A\n  depth: brief\n  coney: partial\n\n- name: B  # note\n  depth: brief\n"
    assert natives.set_coney_statuses(text, {"B": "implemented"}) == (
        "# header\n\n- name: A\n  depth: brief\n\n- name: B  # note\n  depth: brief\n  coney: implemented\n"
    )
    assert natives.set_coney_statuses(text, {"A": "partial"}) == text


def test_cpp_table(tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
    """`cpp` writes the C++ signature table with each argument's editor type and default; `--check` finds it stale."""
    _checkout(tmp_path, GOOD)
    monkeypatch.chdir(tmp_path)
    (tmp_path / "src" / "debug").mkdir(parents=True)
    assert main(["natives", "cpp", "--check"]) == 1
    assert main(["natives", "cpp"]) == 0
    assert main(["natives", "cpp", "--check"]) == 0
    text = (tmp_path / natives_cpp.CPP_FILE).read_text(encoding="utf-8")
    assert (
        'kArgs_MakeThing{{{"label", A::String, "", 0}, {"pos", A::NumberTable, "", 3}, '
        '{"visible", A::Boolean, "true", 0}}}' in text
    )
    assert '{"PlayIt", "sound", kArgs_PlayIt, {}, 1},' in text
    assert "A::Integer" in text and "kResults_MakeThing{R::Number}" in text
    # Categories in page order: world before sound before debug.
    assert text.index('{"MakeThing"') < text.index('{"PlayIt"') < text.index('{"unusedThing"')
    (tmp_path / natives_cpp.CPP_FILE).write_text("stale\n", encoding="utf-8")
    assert main(["natives", "cpp", "--check"]) == 1
    assert "stale" in capsys.readouterr().out


def test_cpp_marks_handles() -> None:
    """A number argument whose description says handle gets the handle editor."""
    masterlist = natives.parse(GOOD.replace('desc: "Track id."', 'desc: "Handle of the track."'))
    assert "A::Handle" in natives_cpp.render_cpp(masterlist)


@pytest.mark.parametrize(
    ("levels", "problem"),
    [("[7]", "must list story levels"), ("[95, 80]", "story order"), ("[80, 80]", "story order")],
)
def test_usage_levels_must_be_story_levels_in_order(levels: str, problem: str) -> None:
    """`usage.levels` holds story levels only, in story order, each once."""
    text = GOOD.replace("levels: [80, 95]", f"levels: {levels}")
    assert any(problem in p for p in natives.parse(text).problems)
