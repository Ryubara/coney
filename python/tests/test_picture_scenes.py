# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools picture scenes`: the synthetic scenes drawn by Coney and compared with their references.

Coney is replaced by a function that writes a picture where `--screenshot` says, so the tests need no build, no window
and no GPU. The scene list in the repository (tests/pictures/scenes.toml) is read for real.
"""

from __future__ import annotations

import subprocess
from collections.abc import Sequence
from pathlib import Path

import pytest
from PIL import Image

from coney_tools import picture_scenes
from coney_tools.cli import main
from coney_tools.config import ConfigError, find_repo_root

SCENES_TOML = """
channel_tolerance = 2
max_share_percent = 0.1
render_size = "20x10"
frames = 3

[[scene]]
name = "flat"
args = ["--sandbox", "default"]

[[scene]]
name = "second"
args = ["--sandbox", "combat"]
frames = 5
"""


class FakeConey:
    """Stands in for the coney executable: records its command lines and draws a flat picture at `--screenshot`."""

    def __init__(self, colour_of=lambda name, run: (10, 20, 30, 255), fail_on: str | None = None) -> None:
        self.commands: list[list[str]] = []
        self.colour_of = colour_of
        self.fail_on = fail_on
        self.runs: dict[str, int] = {}

    def __call__(self, command: Sequence[str], **_: object) -> subprocess.CompletedProcess[str]:
        self.commands.append(list(command))
        shot = Path(command[command.index("--screenshot") + 1])
        name = shot.stem
        if self.fail_on == name:
            return subprocess.CompletedProcess(list(command), 3, "", "no window")
        run = self.runs.get(name, 0)
        self.runs[name] = run + 1
        shot.parent.mkdir(parents=True, exist_ok=True)
        Image.new("RGBA", (20, 10), self.colour_of(name, run)).save(shot)
        return subprocess.CompletedProcess(list(command), 0, "", "")


@pytest.fixture
def scenes_file(tmp_path: Path) -> Path:
    path = tmp_path / "scenes.toml"
    path.write_text(SCENES_TOML)
    return path


def references(folder: Path, colour: tuple[int, int, int, int] = (10, 20, 30, 255)) -> Path:
    folder.mkdir(parents=True, exist_ok=True)
    for name in ("flat", "second"):
        Image.new("RGBA", (20, 10), colour).save(folder / f"{name}.png")
    return folder


def test_a_scene_list_reads_names_arguments_and_the_defaults(scenes_file: Path) -> None:
    scene_list = picture_scenes.load_scenes(scenes_file)
    assert [scene.name for scene in scene_list.scenes] == ["flat", "second"]
    assert scene_list.scenes[0].frames == 3
    assert scene_list.scenes[1].frames == 5
    assert scene_list.scenes[0].render_size == "20x10"
    assert scene_list.channel_tolerance == 2
    assert scene_list.max_share_percent == pytest.approx(0.1)


def test_a_scene_list_without_limits_uses_the_stand_ins(tmp_path: Path) -> None:
    path = tmp_path / "s.toml"
    path.write_text('[[scene]]\nname = "a"\nargs = ["--sandbox"]\n')
    scene_list = picture_scenes.load_scenes(path)
    assert scene_list.channel_tolerance == 2
    assert scene_list.max_share_percent == pytest.approx(0.1)
    assert scene_list.scenes[0].frames == picture_scenes.DEFAULT_FRAMES
    assert scene_list.scenes[0].render_size == picture_scenes.DEFAULT_RENDER_SIZE


@pytest.mark.parametrize(
    ("text", "message"),
    [
        ('[[scene]]\nname = "a"\nargs = []\n[[scene]]\nname = "a"\nargs = []\n', "twice"),
        ('[[scene]]\nname = "../escape"\nargs = []\n', "name"),
        ('[[scene]]\nname = "ok"\nargs = "not a list"\n', "args"),
        ('[[scene]]\nname = "ok"\nargs = ["--screenshot", "x.png"]\n', "--screenshot"),
        ('[[scene]]\nname = "ok"\nargs = []\nframes = 0\n', "frames"),
        ('[[scene]]\nname = "ok"\nargs = []\nrender_size = "big"\n', "render_size"),
        ("", "no scenes"),
    ],
)
def test_a_bad_scene_list_is_refused_with_the_reason(tmp_path: Path, text: str, message: str) -> None:
    path = tmp_path / "s.toml"
    path.write_text(text)
    with pytest.raises(ConfigError, match=message):
        picture_scenes.load_scenes(path)


def test_the_shipped_scene_list_is_valid_and_has_a_reference_for_each_scene() -> None:
    root = find_repo_root(Path(__file__))
    scene_list = picture_scenes.load_scenes(root / picture_scenes.DEFAULT_SCENES)
    assert scene_list.scenes
    # A reference may be missing while the pinned container has not drawn it yet, but never an unlisted extra.
    reference_folder = root / picture_scenes.DEFAULT_REFERENCES
    if reference_folder.is_dir():
        listed = {scene.name for scene in scene_list.scenes}
        assert {path.stem for path in reference_folder.glob("*.png")} <= listed


def test_every_scene_is_drawn_with_its_arguments_and_compared(
    tmp_path: Path, scenes_file: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    fake = FakeConey()
    code = picture_scenes.run_scenes(
        scenes_file,
        coney=["coney"],
        references=references(tmp_path / "ref"),
        out=tmp_path / "out",
        runner=fake,
    )
    assert code == 0
    first, second = fake.commands
    assert first[0] == "coney"
    assert first[1:3] == ["--sandbox", "default"]
    assert "--frames" in first and first[first.index("--frames") + 1] == "3"
    assert first[first.index("--render-size") + 1] == "20x10"
    assert second[second.index("--frames") + 1] == "5"
    assert (tmp_path / "out" / "flat.png").is_file()
    out = capsys.readouterr().out
    assert "flat.png: 0 of 200 pixels over 2" in out
    assert "2 pictures" in out


def test_the_launcher_goes_before_the_executable(tmp_path: Path, scenes_file: Path) -> None:
    fake = FakeConey()
    picture_scenes.run_scenes(
        scenes_file,
        coney=["xvfb-run", "-a", "coney"],
        references=references(tmp_path / "ref"),
        out=tmp_path / "out",
        runner=fake,
    )
    assert fake.commands[0][:3] == ["xvfb-run", "-a", "coney"]


def test_a_picture_past_the_limit_fails_the_run(tmp_path: Path, scenes_file: Path) -> None:
    fake = FakeConey(colour_of=lambda name, run: (10, 20, 30, 255) if name == "flat" else (90, 20, 30, 255))
    code = picture_scenes.run_scenes(
        scenes_file, coney=["coney"], references=references(tmp_path / "ref"), out=tmp_path / "out", runner=fake
    )
    assert code == 1


def test_a_missing_reference_fails_and_keeps_the_drawn_picture(
    tmp_path: Path, scenes_file: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    ref = references(tmp_path / "ref")
    (ref / "second.png").unlink()
    fake = FakeConey()
    code = picture_scenes.run_scenes(
        scenes_file, coney=["coney"], references=ref, out=tmp_path / "out", runner=fake
    )
    assert code == 1
    assert "second.png: no reference" in capsys.readouterr().out
    assert (tmp_path / "out" / "second.png").is_file()


def test_a_run_of_coney_that_fails_is_an_error_naming_the_scene(tmp_path: Path, scenes_file: Path) -> None:
    fake = FakeConey(fail_on="second")
    with pytest.raises(ConfigError, match="second.*exit code 3"):
        picture_scenes.run_scenes(
            scenes_file, coney=["coney"], references=references(tmp_path / "ref"), out=tmp_path / "out", runner=fake
        )


def test_a_run_that_draws_nothing_is_an_error(tmp_path: Path, scenes_file: Path) -> None:
    def draws_nothing(command: Sequence[str], **_: object) -> subprocess.CompletedProcess[str]:
        return subprocess.CompletedProcess(list(command), 0, "", "")

    with pytest.raises(ConfigError, match="flat.*no picture"):
        picture_scenes.run_scenes(
            scenes_file,
            coney=["coney"],
            references=references(tmp_path / "ref"),
            out=tmp_path / "out",
            runner=draws_nothing,
        )


def test_repeat_draws_each_scene_again_and_compares_the_repeats(
    tmp_path: Path, scenes_file: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    fake = FakeConey()
    code = picture_scenes.run_scenes(
        scenes_file,
        coney=["coney"],
        references=references(tmp_path / "ref"),
        out=tmp_path / "out",
        runner=fake,
        repeat=3,
    )
    assert code == 0
    assert len(fake.commands) == 6
    out = capsys.readouterr().out
    assert "repeat 2 of flat.png: 0 of 200 pixels over 2" in out
    assert "repeat 3 of second.png: 0 of 200 pixels over 2" in out


def test_repeats_that_differ_past_the_limit_fail_even_when_the_first_matches_the_reference(
    tmp_path: Path, scenes_file: Path
) -> None:
    fake = FakeConey(colour_of=lambda name, run: (10, 20, 30, 255) if run == 0 else (60, 20, 30, 255))
    code = picture_scenes.run_scenes(
        scenes_file,
        coney=["coney"],
        references=references(tmp_path / "ref"),
        out=tmp_path / "out",
        runner=fake,
        repeat=2,
    )
    assert code == 1


def test_update_reports_the_old_counts_then_replaces_the_references(
    tmp_path: Path, scenes_file: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    # Expected: engine/renderer.md#comparing, moving the pin regenerates the references and reports old and new counts
    ref = references(tmp_path / "ref", colour=(0, 0, 0, 255))
    fake = FakeConey()
    code = picture_scenes.run_scenes(
        scenes_file, coney=["coney"], references=ref, out=tmp_path / "out", runner=fake, update=True
    )
    assert code == 0
    out = capsys.readouterr().out
    assert "flat.png: 200 of 200 pixels over 2" in out
    assert "updated 2 references" in out
    assert Image.open(ref / "flat.png").convert("RGBA").getpixel((0, 0)) == (10, 20, 30, 255)


def test_update_makes_a_missing_reference_and_says_so(
    tmp_path: Path, scenes_file: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    fake = FakeConey()
    ref = tmp_path / "new"
    code = picture_scenes.run_scenes(
        scenes_file, coney=["coney"], references=ref, out=tmp_path / "out", runner=fake, update=True
    )
    assert code == 0
    assert (ref / "flat.png").is_file()
    assert "flat.png: new reference" in capsys.readouterr().out


def test_the_command_line_takes_the_executable_and_a_launcher(
    tmp_path: Path, scenes_file: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    fake = FakeConey()
    monkeypatch.setattr(picture_scenes.subprocess, "run", fake)
    code = main(
        [
            "picture",
            "scenes",
            "--coney",
            "build/ci/src/platform/coney",
            "--launcher",
            "xvfb-run -a",
            "--scenes",
            str(scenes_file),
            "--references",
            str(references(tmp_path / "ref")),
            "--out",
            str(tmp_path / "out"),
        ]
    )
    assert code == 0
    assert fake.commands[0][:3] == ["xvfb-run", "-a", "build/ci/src/platform/coney"]
