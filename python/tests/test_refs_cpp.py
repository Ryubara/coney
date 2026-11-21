# SPDX-License-Identifier: GPL-3.0-or-later
"""The C++ tables `refs render` writes from the reference lists (coney_tools.refs_cpp): the scene preview's casts.

Everything is synthetic: made-up scenes in the scenes topic.
"""

from pathlib import Path

import pytest

from coney_tools import refs, refs_cli, refs_cpp
from coney_tools.cli import main
from coney_tools.refs_topics import TOPICS, topic


def _scenes(entries: list[dict[str, object]]) -> refs.RefList:
    """The scenes list holding `entries`."""
    reflist = refs_cli.new_list(topic("scenes"))
    reflist.entries = entries
    return reflist


def test_the_scene_casts_table_holds_each_scene_with_a_cast_or_props_in_order() -> None:
    text = refs_cpp.scene_casts_cpp(
        _scenes(
            [
                {"id": "movie:X", "kind": "movie", "name": "X"},
                {"id": "t1_a", "kind": "scene", "name": "t1_a"},
                {"id": "t1_c1", "kind": "scene", "name": "t1_c1", "cast": ["civl_a", "player", "-", "?", "a|b"]},
                {"id": "t1_sub", "kind": "scene", "name": "t1_sub", "props": ["dyn_car", "door (left leaf)", "-"]},
            ]
        )
    )
    assert "std::array<ScriptedSceneCast, 2> kCasts{{" in text
    first, second = text.index('{"t1_c1",'), text.index('{"t1_sub",')
    assert first < second
    assert '"civl_a player - ? a|b",' in text
    # A door's leaf is no object type the preview can spawn: written `?`.
    assert '"dyn_car ? -"},' in text
    assert '"t1_a"' not in text and '"X"' not in text
    assert "// clang-format off" in text and "Do not edit." in text


def test_render_writes_the_scene_casts_table_and_check_finds_it_stale(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    (tmp_path / "coney.local.example.toml").write_text("", encoding="utf-8")
    for item in TOPICS:
        reflist = refs_cli.new_list(item)
        if item.key == "scenes":
            reflist.entries = [{"id": "t1_c1", "kind": "scene", "name": "t1_c1", "scene": 0, "cast": ["civl_a"]}]
        refs.write(tmp_path / refs_cli.REFS_DIR / f"{item.key}.yaml", reflist)
    monkeypatch.chdir(tmp_path)
    assert main(["refs", "render"]) == 0
    table = tmp_path / refs_cpp.SCENE_CASTS_FILE
    assert '{"t1_c1",' in table.read_text(encoding="utf-8")
    assert main(["refs", "render", "--check"]) == 0
    table.write_text("edited\n", encoding="utf-8")
    assert main(["refs", "render", "--check"]) == 1
