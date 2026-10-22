# SPDX-License-Identifier: GPL-3.0-or-later
"""The debug tools stay in their own targets (docs/guides/building.md#debug-tools).

Coney has two kinds of build (a maintainer decision, 2026-10-21): builds with the debug tools, for developers and
agents, and the release build a player gets, with none of them compiled in. The split is made at target level: the
debug menus, the developer overlay (Dear ImGui), the viewers and the sandbox live in `coney_debug` and
`coney_devtools`, and the game's own libraries neither list their sources nor link Dear ImGui. These tests read the
CMake files, so a source added to the wrong target, or `imgui` linked by a game library, fails before any build.
"""

import json
import re
from pathlib import Path

from coney_tools.build_cli import PRESETS

ROOT = Path(__file__).resolve().parents[2]

#: The libraries a player's build is made of.
GAME_TARGETS = ("coney_core", "coney_audio", "coney_movies", "coney_net", "coney_platform")
#: The debug tools' libraries.
DEBUG_TARGETS = ("coney_debug", "coney_devtools")

#: Sources that are debug tools wherever they sit: the menus, their renderers, the overlay, the viewers, the sandbox.
#: Matched by name, so it misses debug code whose file name does not say so (sandbox_renderer.cpp,
#: play_level_inspect.cpp, pad_pipe.cpp, humans_trace.cpp, world/debug_camera.cpp): the later units move those.
_DEBUG_SOURCE = re.compile(
    r"^(?:debug/.*|gui/debug_.*|graphics/bitmap_font\.cpp|sandbox/.*|imgui_.*|debug_menus\.cpp|"
    r"reference_renderer\.cpp|.*_viewer_mode\.cpp)$"
)
#: Debug sources still in a game library, each until the unit that moves it (scratch plan, units 2 and 3): the play
#: mode and the streamed scenery use them. It never grows.
_NOT_MOVED_YET = {
    ("coney_platform", "world_viewer_mode.cpp"),
}

#: A CMake command and its arguments, up to the first `)`: a generator expression inside one would cut it short, so
#: the targets' lists use none.
_COMMAND = re.compile(r"\b(add_library|target_sources|target_link_libraries)\s*\(([^)]*)\)", re.DOTALL)


def _words(body: str) -> list[str]:
    """A CMake command's arguments, comments dropped."""
    return re.sub(r"#[^\n]*", "", body).split()


def _targets() -> tuple[dict[str, list[str]], dict[str, list[str]]]:
    """Each Coney library's sources (relative to its folder) and the libraries it links, from src/'s CMake files."""
    sources: dict[str, list[str]] = {}
    links: dict[str, list[str]] = {}
    for path in sorted((ROOT / "src").rglob("CMakeLists.txt")):
        for command, body in _COMMAND.findall(path.read_text(encoding="utf-8")):
            words = _words(body)
            if not words:
                continue
            name, rest = words[0], [w for w in words[1:] if w not in ("STATIC", "PUBLIC", "PRIVATE", "INTERFACE")]
            if command == "target_link_libraries":
                links.setdefault(name, []).extend(rest)
            else:
                sources.setdefault(name, []).extend(w for w in rest if w.endswith(".cpp"))
    return sources, links


def test_every_target_named_here_exists() -> None:
    sources, _ = _targets()
    missing = [name for name in (*GAME_TARGETS, *DEBUG_TARGETS) if name not in sources]
    assert missing == []


def test_no_game_library_compiles_a_debug_source() -> None:
    sources, _ = _targets()
    offenders = [
        f"{name}: {source}"
        for name in GAME_TARGETS
        for source in sources.get(name, [])
        if _DEBUG_SOURCE.match(source) and (name, source) not in _NOT_MOVED_YET
    ]
    assert offenders == [], "debug tools go in coney_debug (src/) or coney_devtools (src/platform/)"


def test_the_not_moved_yet_list_holds_only_what_is_still_there() -> None:
    sources, _ = _targets()
    stale = [entry for entry in _NOT_MOVED_YET if entry[1] not in sources.get(entry[0], [])]
    assert stale == [], "drop the entry: the source has moved"


#: Game sources that still include a debug tools' header, each until the unit that unties it. Empty: keep it so.
_STILL_INCLUDES_DEBUG: set[str] = set()

_DEBUG_INCLUDE = re.compile(r'^#include "(?:debug|sandbox)/', re.MULTILINE)


def _game_files() -> list[Path]:
    """The game libraries' sources and the headers beside them."""
    sources, _ = _targets()
    files: list[Path] = []
    for name in GAME_TARGETS:
        folder = ROOT / "src" / ("platform" if name == "coney_platform" else "")
        for source in sources.get(name, []):
            path = folder / source
            files += [p for p in (path, path.with_suffix(".h")) if p.exists()]
    # The release build's entry point, and what it shares with the debug tools' one.
    files.append(ROOT / "src" / "platform" / "main_release.cpp")
    return sorted(set(files))


def test_no_game_source_includes_a_debug_header() -> None:
    offenders = [
        path.relative_to(ROOT).as_posix()
        for path in _game_files()
        if _DEBUG_INCLUDE.search(path.read_text(encoding="utf-8"))
        and path.relative_to(ROOT).as_posix() not in _STILL_INCLUDES_DEBUG
    ]
    assert offenders == [], "reach the game through its own API; debug code goes in coney_debug or coney_devtools"


def test_the_still_includes_list_holds_only_files_that_do() -> None:
    stale = [
        name
        for name in sorted(_STILL_INCLUDES_DEBUG)
        if not _DEBUG_INCLUDE.search((ROOT / name).read_text(encoding="utf-8"))
    ]
    assert stale == [], "drop the entry: the file no longer includes a debug header"


def test_no_game_library_links_the_debug_tools() -> None:
    _, links = _targets()
    offenders = sorted(
        f"{name}: {library}" for name in GAME_TARGETS for library in links.get(name, []) if library in DEBUG_TARGETS
    )
    assert offenders == []


def test_only_the_devtools_link_dear_imgui() -> None:
    _, links = _targets()
    linking = sorted(name for name, libraries in links.items() if "imgui" in libraries)
    assert linking == ["coney_devtools"]


def test_the_debug_source_pattern() -> None:
    for debug in (
        "debug/menu_model.cpp",
        "gui/debug_menu_view.cpp",
        "sandbox/sandbox_world.cpp",
        "imgui_overlay.cpp",
        "texture_viewer_mode.cpp",
        "debug_menus.cpp",
        "graphics/bitmap_font.cpp",
    ):
        assert _DEBUG_SOURCE.match(debug), debug
    for game in ("human/human.cpp", "gui/menu.cpp", "play_level_mode.cpp", "world/debug_camera_x.cpp"):
        assert not _DEBUG_SOURCE.match(game), game


def test_the_build_tool_offers_every_preset() -> None:
    presets = json.loads((ROOT / "CMakePresets.json").read_text(encoding="utf-8"))["configurePresets"]
    visible = [preset["name"] for preset in presets if not preset.get("hidden")]
    assert sorted(PRESETS) == sorted(visible)


def test_the_release_presets_leave_the_debug_tools_out() -> None:
    presets = json.loads((ROOT / "CMakePresets.json").read_text(encoding="utf-8"))["configurePresets"]
    tools = {
        p["name"]: p.get("cacheVariables", {}).get("CONEY_DEBUG_TOOLS", "ON") for p in presets if not p.get("hidden")
    }
    assert tools["release"] == "OFF"
    assert tools["ci-release"] == "OFF"
    assert all(tools[name] == "ON" for name in ("dev", "playtest", "ci", "asan"))
