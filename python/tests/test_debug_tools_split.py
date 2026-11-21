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

import pytest

from coney_tools.build_cli import PRESETS

# Reads the checkout's own files of these classes (python/tests/repo_reads.py).
pytestmark = pytest.mark.reads("engine")

ROOT = Path(__file__).resolve().parents[2]

#: The libraries a player's build is made of: the game, and the player console's F8 bar (coney_console_ui).
GAME_TARGETS = (
    "coney_core",
    "coney_audio",
    "coney_install",
    "coney_movies",
    "coney_net",
    "coney_platform",
    "coney_console_ui",
)
#: The game's libraries whose sources are in src/platform/.
_PLATFORM_TARGETS = ("coney_platform", "coney_console_ui")
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
        folder = ROOT / "src" / ("platform" if name in _PLATFORM_TARGETS else "")
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


def test_only_the_devtools_and_the_console_bar_link_dear_imgui() -> None:
    # The player console's bar is the one part of the release build drawn with Dear ImGui
    # (docs/guides/studio/console.md#release); the debug tools' overlay is the other user.
    _, links = _targets()
    linking = sorted(name for name, libraries in links.items() if "imgui" in libraries)
    assert linking == ["coney_console_ui", "coney_devtools"]


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


_TEST_DEBUG_INCLUDE = re.compile(
    r'^#include "(?:debug/|sandbox/|gui/debug_|gamemodes/(?:sheet|text)_viewer_mode|'
    r"platform/(?:play_level_controls|sandbox_|imgui_|debug_menus|game_session_debug|audio_output_controls|"
    r"texture_viewer_mode|character_viewer_mode|reference_renderer))",
    re.MULTILINE,
)


def test_the_release_build_compiles_no_test_of_the_debug_tools() -> None:
    # The tests before the `if(CONEY_DEBUG_TOOLS)` block are the release build's: none may include a debug tools'
    # header, or the release build would not compile them.
    text = (ROOT / "tests" / "CMakeLists.txt").read_text(encoding="utf-8")
    game = text[: text.index("if(CONEY_DEBUG_TOOLS)")]
    sources = {word.rstrip(")") for word in re.findall(r"[\w/]+_test\.cpp\)?", game)}
    offenders = sorted(
        name for name in sources if _TEST_DEBUG_INCLUDE.search((ROOT / "tests" / name).read_text(encoding="utf-8"))
    )
    assert offenders == [], "list these in the if(CONEY_DEBUG_TOOLS) block of tests/CMakeLists.txt"


#: What a file of the console's core may include: itself, the engine's plumbing and the standard library
#: (docs/guides/studio/console.md#release). The console names nothing of the debug tools, the scripting runtime, the
#: entities or the game modes; it reaches the game only through the host interfaces it declares.
_CONSOLE_ALLOWED = re.compile(r"^(?:console|core)/")
_QUOTED_INCLUDE = re.compile(r'^#include "([^"]+)"', re.MULTILINE)


def _console_includes(text: str) -> list[str]:
    """The project headers a file includes that the console's core may not."""
    return [header for header in _QUOTED_INCLUDE.findall(text) if not _CONSOLE_ALLOWED.match(header)]


def test_the_console_core_includes_only_console_and_core() -> None:
    folder = ROOT / "src" / "console"
    files = sorted([*folder.glob("*.h"), *folder.glob("*.cpp")])
    assert files, "src/console/ holds the console's core"
    offenders = [
        f"{path.relative_to(ROOT).as_posix()}: {header}"
        for path in files
        for header in _console_includes(path.read_text(encoding="utf-8"))
    ]
    assert offenders == [], "the console reaches the game through console/hosts.h, never by including it"


def test_the_console_core_is_in_the_game_library() -> None:
    sources, _ = _targets()
    folder = ROOT / "src" / "console"
    expected = sorted(f"console/{path.name}" for path in folder.glob("*.cpp"))
    assert sorted(s for s in sources["coney_core"] if s.startswith("console/")) == expected


def test_the_console_include_rule() -> None:
    assert _console_includes('#include "console/registry.h"\n#include "core/log.h"\n#include <vector>\n') == []
    assert _console_includes('#include "debug/menu_model.h"\n#include "scripting/lua_state.h"\n') == [
        "debug/menu_model.h",
        "scripting/lua_state.h",
    ]


#: What a file of the console's bar may include besides Dear ImGui and the standard library: the console's model, the
#: engine's plumbing, its own headers and the platform's window (docs/guides/studio/console.md#release). Nothing of
#: the debug tools or the game itself.
_CONSOLE_UI_ALLOWED = re.compile(r"^(?:console|core)/|^platform/(?:console_[a-z_]+|ui_scale|render_engine|window)\.h$")


def _console_ui_includes(text: str) -> list[str]:
    """The project headers a file of the console's bar includes that it may not."""
    return [header for header in _QUOTED_INCLUDE.findall(text) if not _CONSOLE_UI_ALLOWED.match(header)]


def test_the_console_bar_includes_only_the_console_and_the_window() -> None:
    sources, _ = _targets()
    files: list[Path] = []
    for source in sources["coney_console_ui"]:
        path = ROOT / "src" / "platform" / source
        files += [p for p in (path, path.with_suffix(".h")) if p.exists()]
    assert files, "coney_console_ui holds the console's bar"
    offenders = [
        f"{path.relative_to(ROOT).as_posix()}: {header}"
        for path in sorted(set(files))
        for header in _console_ui_includes(path.read_text(encoding="utf-8"))
    ]
    assert offenders == [], "the bar reaches the game only through the console's model"


def test_the_console_bar_include_rule() -> None:
    allowed = '#include "console/console.h"\n#include "platform/console_view.h"\n#include "platform/render_engine.h"\n'
    assert _console_ui_includes(allowed) == []
    assert _console_ui_includes('#include "debug/debug_session.h"\n#include "platform/game_session.h"\n') == [
        "debug/debug_session.h",
        "platform/game_session.h",
    ]
