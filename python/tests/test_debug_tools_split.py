# SPDX-License-Identifier: GPL-3.0-or-later
"""The debug tools stay in their own targets (docs/guides/building.md#debug-tools).

Coney has two kinds of build (a maintainer decision, 2026-10-21): builds with the debug tools, for developers and
agents, and the release build a player gets, with none of them compiled in. The split is made at target level: the
debug menus, the developer overlay (Dear ImGui), the viewers and the sandbox live in `coney_debug` and
`coney_devtools`, and the game's own libraries neither list their sources nor link Dear ImGui. These tests read the
CMake files, so a source added to the wrong target, or `imgui` linked by a game library, fails before any build.
"""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

#: The libraries a player's build is made of.
GAME_TARGETS = ("coney_core", "coney_audio", "coney_movies", "coney_net", "coney_platform")
#: The debug tools' libraries.
DEBUG_TARGETS = ("coney_debug", "coney_devtools")

#: Sources that are debug tools wherever they sit: the menus, their renderers, the overlay, the viewers, the sandbox.
_DEBUG_SOURCE = re.compile(
    r"^(?:debug/.*|gui/debug_.*|graphics/bitmap_font\.cpp|sandbox/.*|imgui_.*|debug_menus\.cpp|"
    r"reference_renderer\.cpp|.*_viewer_mode\.cpp)$"
)
#: Debug sources still in a game library, each until the unit that moves it (scratch plan, units 2 and 3): the play
#: mode and the streamed scenery use them. Shrinks to nothing; never grows.
_NOT_MOVED_YET = {
    ("coney_platform", "world_viewer_mode.cpp"),
}

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


def test_only_the_devtools_link_dear_imgui() -> None:
    _, links = _targets()
    linking = sorted(name for name, libraries in links.items() if "imgui" in libraries)
    assert linking == ["coney_devtools"]


def test_the_debug_source_pattern() -> None:
    for debug in ("debug/menu_model.cpp", "gui/debug_menu_view.cpp", "sandbox/sandbox_world.cpp", "imgui_overlay.cpp",
                  "texture_viewer_mode.cpp", "debug_menus.cpp", "graphics/bitmap_font.cpp"):
        assert _DEBUG_SOURCE.match(debug), debug
    for game in ("human/human.cpp", "gui/menu.cpp", "play_level_mode.cpp", "world/debug_camera_x.cpp"):
        assert not _DEBUG_SOURCE.match(game), game
