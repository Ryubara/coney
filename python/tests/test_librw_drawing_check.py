# SPDX-License-Identifier: GPL-3.0-or-later
"""The pre-commit hook that keeps librw's drawing in the GL3 renderer (`no-librw-drawing-outside-gl3`).

Only the GL3 renderer calls a render state, an `rw::gl3::` type or function, or an Im2D or Im3D draw
(docs/engine/renderer.md#frame-description): the rest of the game says what a frame draws in the frame description.
The hook excludes the files that still call, in two groups its comment names; these tests hold the pattern to what it
is meant to find, hold every file of `src/` to the hook, fail an exclusion whose file no longer calls, and pin the
excluded names, so the group of files waiting for the description grows only by a change here, which review sees
(item 10 of docs/engine/renderer.md#order).
"""

import functools
import re

import pytest
import yaml
from conventions_support import REPO

# Reads the checkout's own files of these classes (python/tests/repo_reads.py).
pytestmark = pytest.mark.reads("engine")

HOOK_ID = "no-librw-drawing-outside-gl3"

# The GL3 renderer and what it is made of: the files that call librw's drawing for good.
GL3_RENDERER = frozenset(
    {
        "character_skin",
        "draw_calls",
        "gl3_renderer",
        "gl3_screen_passes",
        "lit_atomic",
        "render_engine",
        "texture_dictionary",
        "texture_lod",
    }
)
# The files waiting for the frame description: #1622 (the scene states) and #1623 (the debug tools' viewers). Meant
# only to lose names; a name added here is a decision review sees.
WAITING = frozenset(
    {
        "client_level_scene",
        "parked_cars",
        "placed_objects",
        "play_level_mode",
        "play_lighting",
        "world_renderer",
        "character_viewer_mode",
        "mesh_views",
        "reference_renderer",
        "sandbox_renderer",
        "studio_level_renderer",
    }
)


def excluded_names() -> list[str]:
    """The file names (no folder, no extension) the hook's exclusion lists."""
    names: list[str] = re.findall(r"[a-z_0-9]+(?=\\\.(?:cpp|h))", hook()["exclude"])
    return names


@functools.cache
def hook() -> dict[str, str]:
    """The hook's entry from `.pre-commit-config.yaml`."""
    config = yaml.safe_load((REPO / ".pre-commit-config.yaml").read_text(encoding="utf-8"))
    found = [h for repo in config["repos"] for h in repo["hooks"] if h["id"] == HOOK_ID]
    assert len(found) == 1, f"the config has {len(found)} hooks called {HOOK_ID}"
    entry: dict[str, str] = found[0]
    return entry


@functools.cache
def pattern() -> re.Pattern[str]:
    """The hook's pattern, compiled once."""
    return re.compile(hook()["entry"])


def calls(line: str) -> bool:
    """Whether the hook's pattern finds a call on `line`, as pre-commit's pygrep tests each line."""
    return pattern().search(line) is not None


@functools.cache
def source_files() -> tuple[str, ...]:
    """Every header and source of `src/` the hook checks, as pre-commit names them (forward slashes)."""
    files = re.compile(hook()["files"])
    return tuple(
        sorted(
            path.relative_to(REPO).as_posix()
            for path in (REPO / "src").rglob("*")
            if path.is_file() and files.search(path.relative_to(REPO).as_posix())
        )
    )


@functools.cache
def file_calls(path: str) -> bool:
    """Whether any line of the file has a call the hook finds."""
    text = (REPO / path).read_text(encoding="utf-8", errors="replace")
    return any(calls(line) for line in text.splitlines())


@pytest.mark.parametrize(
    "line",
    [
        "    draw::setRenderState(rw::ZTESTENABLE, 1);",
        "    draw::setRenderStatePtr(rw::TEXTURERASTER, raster);",
        "    const bool fogged = draw::getRenderState(rw::FOGENABLE) != 0;",
        "    draw::im3dTransform(vertices.data(), count, nullptr, flags);",
        "    draw::im3dRenderIndexedPrimitive(rw::PRIMTYPETRILIST, indices.data(), count);",
        "    draw::im2dRenderPrimitive(rw::PRIMTYPETRILIST, vertices.data(), count);",
        "    std::vector<rw::gl3::Im3DVertex> vertices;",
        "    rw::SetRenderState(rw::ZTESTENABLE, 1);",
        "    rw::im3d::Transform(vertices, count, nullptr, flags);",
        "    const auto* native = PLUGINOFFSET(rw::gl3::Gl3Raster, raster, rw::gl3::nativeRasterOffset);",
    ],
)
def test_the_pattern_finds_a_render_state_a_gl3_type_and_an_immediate_mode_draw(line: str) -> None:
    assert calls(line)


@pytest.mark.parametrize(
    "line",
    [
        "    // draw::setRenderState(rw::ZTESTENABLE, 1) is the GL3 renderer's",
        "/// Draws the quads with librw's Im2D and Im3D through rw::gl3::Im3DVertex.",
        '    log("draw::setRenderState(rw::ZTESTENABLE, 1)");',
        '#include "platform/draw_calls.h"',
        "    draw::startRecording();",
        "    if (draw::recording()) {",
        "    engine.drawEffect(draw, vertices, indices, texture);",
        "    rw::Raster* raster = nullptr;",
    ],
)
def test_the_pattern_leaves_comments_strings_and_the_rest_of_the_draw_layer_alone(line: str) -> None:
    assert not calls(line)


def test_every_file_of_src_calls_nothing_or_is_excluded() -> None:
    # What pre-commit does, here, so a failure names the file and the way out (the description's effect or scene draw).
    excluded = re.compile(hook()["exclude"])
    found = [path for path in source_files() if file_calls(path) and not excluded.search(path)]
    assert not found, (
        f"these files call librw's drawing outside the GL3 renderer: {found}; "
        "draw through the frame description (docs/engine/renderer.md#frame-description)"
    )


def test_no_exclusion_names_a_file_that_calls_nothing() -> None:
    # A file that left the description's debt (or never had a call) is taken out of the exclusion.
    excluded = re.compile(hook()["exclude"])
    stale = [path for path in source_files() if excluded.search(path) and not file_calls(path)]
    assert not stale, f"these files no longer call, so the hook no longer excludes them: {stale}"
    # Every file the exclusion names exists, so a renamed file does not leave its old name behind.
    missing = [name for name in excluded_names() if not (REPO / "src" / "platform" / f"{name}.cpp").exists()]
    assert not missing, f"the exclusion names files that are gone: {missing}"


def test_the_exclusion_names_only_the_pinned_files() -> None:
    # A file joins the exclusion only by being added to GL3_RENDERER or WAITING above, which review sees.
    unpinned = [name for name in excluded_names() if name not in GL3_RENDERER | WAITING]
    assert not unpinned, f"the exclusion names files this test does not pin: {unpinned}"
    # And the pins name nothing the exclusion has dropped, so a file that leaves is unpinned too.
    unused = sorted((GL3_RENDERER | WAITING) - set(excluded_names()))
    assert not unused, f"pinned but no longer excluded, so take them out of the pins: {unused}"
