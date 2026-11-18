# SPDX-License-Identifier: GPL-3.0-or-later
"""The named scenes of `coney-tools perf bench --scene` (docs/engine/renderer.md#benchmark).

A scene is the same work on every run: a lockstep test-mode run of one level and checkpoint with a pinned camera, a
fixed seed (Coney's bench seeds its crowd) and a fixed number of fighters, frames and render size. The renderer work
reports every change against these scenes, so the table is the one place that says what each is. It names levels and
checkpoints only; nothing here comes from the game's files.
"""

from __future__ import annotations

from dataclasses import dataclass, replace

DEFAULT_FRAMES = 600
DEFAULT_WARMUP = 60

# The hub's street where the brawl of docs/guides/performance.md#crowd-1031 was measured: the player put there, and the
# view pinned on the spot from 18 m up, 23 m behind him, so every scene of the hub looks at the same street.
HUB_PLACE = "-176.60,102.09,0.24,178.9"
HUB_CAMERA = "-176.6,125,18,0,-0.3007,0.9537,0"
# A view down level99's street from docs/guides/running-play-level.md#matched-views.
STREET_CAMERA = "75.2,41.1,1.8,0,0,0.7071068,0.7071068,65"


@dataclass(frozen=True)
class Scene:
    """One benchmark scene: what `coney --bench` plays, and how much of it."""

    name: str  # on the command line and in the report files
    title: str  # in the tables
    level: str
    checkpoint: int
    camera: str  # `--camera X,Y,Z,QX,QY,QZ,QW[,FOV]`
    place: str | None  # `--bench-place X,Y,Z,HEADING`: where the player stands as the fighters spawn
    fighters: int
    frames: int  # measured, after `warmup` unmeasured
    warmup: int = DEFAULT_WARMUP
    render_size: str | None = None  # `--render-size WxH`; None: the default window
    headless: bool = False  # no window: the simulation's cost alone


HUB = Scene(
    name="hub",
    title="Hub",
    level="level95",
    checkpoint=1,
    camera=HUB_CAMERA,
    place=HUB_PLACE,
    fighters=0,
    frames=DEFAULT_FRAMES,
)

# In the order a table lists them: the three the renderer budget is stated on, then the render sizes and the
# simulation alone.
SCENES: tuple[Scene, ...] = (
    HUB,
    replace(HUB, name="hub-crowd", title="Hub, crowd", fighters=50),
    Scene(
        name="street",
        title="Street",
        level="level99",
        checkpoint=1,
        camera=STREET_CAMERA,
        place=None,
        fighters=0,
        frames=DEFAULT_FRAMES,
    ),
    replace(HUB, name="hub-640", title="Hub, 640x448", render_size="640x448"),
    replace(HUB, name="hub-2560", title="Hub, 2560x1920", render_size="2560x1920"),
    replace(HUB, name="hub-headless", title="Hub, headless", headless=True),
    replace(HUB, name="hub-crowd-headless", title="Hub, crowd, headless", fighters=50, headless=True),
)


def scene_names() -> tuple[str, ...]:
    """The scene names, in table order."""
    return tuple(scene.name for scene in SCENES)


def find_scenes(text: str) -> list[Scene]:
    """The scenes `--scene` names: `all`, or names separated by commas (each once, in the order given); ValueError for
    an empty list or a name the table lacks."""
    names = [part.strip() for part in text.split(",") if part.strip()]
    if not names:
        raise ValueError(f"no scene named; the scenes are: all, {', '.join(scene_names())}")
    if names == ["all"]:
        return list(SCENES)
    by_name = {scene.name: scene for scene in SCENES}
    found: list[Scene] = []
    for name in names:
        if name not in by_name:
            raise ValueError(f"no scene {name}; the scenes are: all, {', '.join(scene_names())}")
        if by_name[name] not in found:
            found.append(by_name[name])
    return found
