# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools picture parity`: every benchmark scene and the named `--play-level` frames drawn by both
renderers and compared whole (docs/engine/renderer-pictures.md#parity).

Coney is replaced by a function that writes a picture where `--screenshot` says, so the tests need no build, no disc,
no window and no GPU. The tolerance is read from a scene list of the tests' own, in the form of
tests/pictures/scenes.toml, and the benchmark's scene table (perf_scenes.py) is read for real; the command-line tests
read the repository's list.
"""

from __future__ import annotations

import subprocess
from collections.abc import Callable, Sequence
from pathlib import Path

import pytest
from PIL import Image

from coney_tools import perf_scenes, picture_parity
from coney_tools.cli import main
from coney_tools.config import ConfigError

DISC = "game.iso"
SIZE = (40, 20)
# The cross-renderer limits of tests/pictures/scenes.toml, as a scene list of the tests' own.
SCENES_TOML = """
channel_tolerance = 2
max_share_percent = 0.1
cross_channel_tolerance = 4
cross_max_share_percent = 0.2

[[scene]]
name = "card"
args = ["--test-card", "shapes"]
"""


class FakeConey:
    """Stands in for the coney executable: records its command lines, draws a picture at `--screenshot` and prints
    what a real run prints when the GPU renderer starts."""

    def __init__(
        self,
        picture_of: Callable[[str, str], Image.Image] | None = None,
        gpu_starts: bool = True,
    ) -> None:
        self.commands: list[list[str]] = []
        self.cwds: list[object] = []
        self.picture_of = picture_of or (lambda name, renderer: striped(0))
        self.gpu_starts = gpu_starts

    def __call__(self, command: Sequence[str], **options: object) -> subprocess.CompletedProcess[str]:
        self.commands.append(list(command))
        self.cwds.append(options.get("cwd"))
        shot = Path(command[command.index("--screenshot") + 1])
        renderer = command[command.index("--renderer") + 1]
        # A picture is NAME.RENDERER.png, so the frame's name is what comes before the renderer.
        name = shot.name.removesuffix(f".{renderer}.png")
        shot.parent.mkdir(parents=True, exist_ok=True)
        self.picture_of(name, renderer).save(shot)
        said = "renderer: gpu on Vulkan\n" if renderer == "gpu" and self.gpu_starts else ""
        return subprocess.CompletedProcess(list(command), 0, said, "")


def striped(shift: int) -> Image.Image:
    """A picture with some structure (not one colour); every red value is moved by `shift`."""
    picture = Image.new("RGBA", SIZE, (0, 0, 0, 255))
    for x in range(SIZE[0]):
        for y in range(SIZE[1]):
            picture.putpixel((x, y), (min(255, (x * 6 + shift) % 256), y * 10, 90, 255))
    return picture


def run(tmp_path: Path, fake: FakeConey, **options: object) -> int:
    """`run_parity` with the fake in place of Coney, writing to tmp_path / "out"."""
    scenes = tmp_path / "scenes.toml"
    scenes.write_text(SCENES_TOML, encoding="utf-8")
    return picture_parity.run_parity(
        ["coney"],
        DISC,
        tmp_path / "out",
        runner=fake,
        scenes_path=scenes,
        **options,  # type: ignore[arg-type]
    )


# -- the frames


def test_the_frames_are_every_drawn_benchmark_scene_and_the_named_level_frames() -> None:
    names = [frame.name for frame in picture_parity.frames(DISC)]
    drawn = [scene.name for scene in perf_scenes.SCENES if not scene.headless]
    assert names == [*drawn, "level99-street", "level99-line-blend", "level95-hub"]
    assert len(set(names)) == len(names)


def test_a_headless_benchmark_scene_has_no_picture_to_compare() -> None:
    # The two headless scenes measure the simulation alone: they draw nothing, so they are not frames.
    headless = {scene.name for scene in perf_scenes.SCENES if scene.headless}
    assert headless
    assert headless.isdisjoint(frame.name for frame in picture_parity.frames(DISC))


def test_a_benchmark_frame_is_the_scene_with_its_camera_crowd_and_size() -> None:
    by_name = {frame.name: frame for frame in picture_parity.frames(DISC)}
    crowd = perf_scenes.find_scenes("hub-crowd")[0]
    args = by_name["hub-crowd"].args
    assert args[args.index("--camera") + 1] == crowd.camera
    assert args[args.index("--bench-fighters") + 1] == "50"
    assert args[args.index("--bench-place") + 1] == crowd.place
    assert "--bench-god" in args
    assert args[args.index("--disc") + 1] == DISC
    assert by_name["hub-640"].args[by_name["hub-640"].args.index("--render-size") + 1] == "640x448"
    assert "--headless" not in args


def test_a_benchmark_frame_runs_to_the_same_frame_whatever_the_scene() -> None:
    # The frame is taken after the spawn and the warm-up, as the earlier comparisons took the crowd at frame 300.
    for frame in picture_parity.frames(DISC):
        if frame.name in {scene.name for scene in perf_scenes.SCENES}:
            assert frame.args[frame.args.index("--frames") + 1] == str(picture_parity.BENCH_FRAME)


def test_the_named_level_frames_are_the_commands_of_the_design() -> None:
    by_name = {frame.name: frame for frame in picture_parity.frames(DISC)}
    street = by_name["level99-street"].args
    assert street[street.index("--play-level") + 1] == "level99"
    assert street[street.index("--frames") + 1] == "90"
    assert street[street.index("--render-size") + 1] == "640x448"
    assert "--freeze-world" in street and "--skip-movies" in street
    blend = by_name["level99-line-blend"].args
    assert blend[: len(street)] == street and blend[len(street) :] == ("--line-blend", "on")
    hub = by_name["level95-hub"].args
    assert hub[hub.index("--play-level") + 1] == "level95"
    assert "--freeze-world" in hub
    # Neither draws only the scene: the whole frame is compared, effects and screen passes included.
    assert "--scene-only" not in street + hub


def test_a_frame_never_sets_what_the_command_sets_itself() -> None:
    for frame in picture_parity.frames(DISC):
        for own in ("--screenshot", "--renderer"):
            assert own not in frame.args


# -- the runs


def test_every_frame_is_drawn_by_both_renderers_and_compared(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    fake = FakeConey()
    assert run(tmp_path, fake) == 0
    frames = picture_parity.frames(DISC)
    assert len(fake.commands) == 2 * len(frames)
    for frame in frames:
        assert (tmp_path / "out" / f"{frame.name}.gl3.png").is_file()
        assert (tmp_path / "out" / f"{frame.name}.gpu.png").is_file()
    renderers = [command[command.index("--renderer") + 1] for command in fake.commands]
    assert renderers == ["gl3", "gpu"] * len(frames)
    out = capsys.readouterr().out
    assert f"{len(frames)} frames" in out
    for frame in frames:
        assert f"| {frame.name} |" in out


def test_the_report_has_a_row_per_frame_with_the_counts(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    def picture_of(name: str, renderer: str) -> Image.Image:
        # The GPU's picture of the crowd differs in every pixel of the first row by 40 of 255.
        picture = striped(0)
        if name == "hub-crowd" and renderer == "gpu":
            for x in range(SIZE[0]):
                picture.putpixel((x, 0), (255, 255, 255, 255))
        return picture

    fake = FakeConey(picture_of)
    code = run(tmp_path, fake, only=["hub-crowd", "street"])
    out = capsys.readouterr().out
    assert code == 1
    row = next(line for line in out.splitlines() if line.startswith("| hub-crowd |"))
    # The first row is 40 pixels of 800 (5 %), every one 255 off in green.
    assert row == "| hub-crowd | 40x20 | 40 | 5.000 % | 255 | past the limit |"
    assert next(line for line in out.splitlines() if line.startswith("| street |")).endswith("within |")


def test_a_frame_within_the_cross_renderer_tolerance_passes(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    # 4 of 255 in a channel is the cross-renderer tolerance of tests/pictures/scenes.toml; 3 is under it.
    def picture_of(name: str, renderer: str) -> Image.Image:
        return striped(3 if renderer == "gpu" else 0)

    assert run(tmp_path, FakeConey(picture_of), only=["street"]) == 0
    out = capsys.readouterr().out
    assert "| street |" in out and "past the limit" not in out


def test_a_frame_past_the_tolerance_fails_the_run_and_the_others_are_still_reported(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    def picture_of(name: str, renderer: str) -> Image.Image:
        return striped(90 if renderer == "gpu" and name == "hub" else 0)

    assert run(tmp_path, FakeConey(picture_of), only=["hub", "street"]) == 1
    out = capsys.readouterr().out
    assert "| hub |" in out and "| street |" in out
    assert "1 of 2 frames past the limit" in out


def test_the_limits_come_from_the_scene_list_and_can_be_given(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    def picture_of(name: str, renderer: str) -> Image.Image:
        return striped(10 if renderer == "gpu" else 0)

    fake = FakeConey(picture_of)
    assert run(tmp_path, fake, only=["street"]) == 1
    assert "over 4" in capsys.readouterr().out
    assert run(tmp_path, FakeConey(picture_of), only=["street"], channel_tolerance=12) == 0
    assert "over 12" in capsys.readouterr().out
    assert run(tmp_path, FakeConey(picture_of), only=["street"], channel_tolerance=2, max_share_percent=100) == 0


def test_a_blank_pair_is_named_so_it_is_not_taken_for_a_match(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    # level95's frame is black in every build: two black pictures are equal, and say nothing about the renderers.
    def picture_of(name: str, renderer: str) -> Image.Image:
        return Image.new("RGBA", SIZE, (0, 0, 0, 255)) if name == "level95-hub" else striped(0)

    assert run(tmp_path, FakeConey(picture_of), only=["level95-hub", "level99-street"]) == 0
    out = capsys.readouterr().out
    assert "blank" in next(line for line in out.splitlines() if line.startswith("| level95-hub |"))
    assert "blank" not in next(line for line in out.splitlines() if line.startswith("| level99-street |"))
    assert "1 blank" in out


def test_a_size_difference_fails_with_its_reason(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    def picture_of(name: str, renderer: str) -> Image.Image:
        return striped(0) if renderer == "gl3" else Image.new("RGBA", (SIZE[0], SIZE[1] + 1), (1, 2, 3, 255))

    assert run(tmp_path, FakeConey(picture_of), only=["street"]) == 1
    assert "size differs" in capsys.readouterr().out


def test_only_draws_the_frames_it_names_and_refuses_an_unknown_one(tmp_path: Path) -> None:
    fake = FakeConey()
    assert run(tmp_path, fake, only=["street"]) == 0
    assert len(fake.commands) == 2
    with pytest.raises(ConfigError, match="no frame named nope"):
        run(tmp_path, FakeConey(), only=["nope"])


def test_a_gpu_run_that_fell_back_to_gl3_is_an_error(tmp_path: Path) -> None:
    with pytest.raises(ConfigError, match="GPU renderer"):
        run(tmp_path, FakeConey(gpu_starts=False), only=["street"])


def test_a_difference_picture_is_written_for_a_frame_over_the_tolerance(tmp_path: Path) -> None:
    def picture_of(name: str, renderer: str) -> Image.Image:
        return striped(90 if renderer == "gpu" else 0)

    assert run(tmp_path, FakeConey(picture_of), only=["street"], diff_out=tmp_path / "diff") == 1
    assert (tmp_path / "diff" / "street.diff.png").is_file()


def test_coney_runs_in_the_given_folder_and_every_run_keeps_its_log(tmp_path: Path) -> None:
    fake = FakeConey()
    assert run(tmp_path, fake, only=["street"], cwd=tmp_path) == 0
    assert {Path(str(cwd)) for cwd in fake.cwds} == {tmp_path}
    assert (tmp_path / "out" / "street.gl3.log").is_file()
    assert (tmp_path / "out" / "street.gpu.log").is_file()


def test_a_benchmark_frame_also_writes_the_benchmark_report_beside_its_picture(tmp_path: Path) -> None:
    # `--bench` is what sets the crowd and the spot, and it writes a report; it goes beside the picture, not in the
    # folder Coney runs in.
    fake = FakeConey()
    assert run(tmp_path, fake, only=["hub"]) == 0
    first = fake.commands[0]
    assert Path(first[first.index("--bench") + 1]).parent == (tmp_path / "out").resolve()


# -- the command line


@pytest.mark.reads("engine")
def test_the_command_takes_the_disc_and_the_frames_from_the_command_line(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    fake = FakeConey()
    monkeypatch.setattr(subprocess, "run", fake)
    args = [
        "picture",
        "parity",
        "--coney",
        "coney",
        "--disc",
        "d.iso",
        "--out",
        str(tmp_path / "o"),
        "--only",
        "street",
    ]
    assert main(args) == 0
    assert len(fake.commands) == 2
    assert fake.commands[0][fake.commands[0].index("--disc") + 1] == "d.iso"


@pytest.mark.reads("engine")
def test_the_command_refuses_an_unknown_frame_with_exit_2(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(subprocess, "run", FakeConey())
    args = ["picture", "parity", "--coney", "coney", "--disc", "d.iso", "--out", str(tmp_path), "--only", "nope"]
    assert main(args) == 2
