# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools clips` and `clips record`: where clips may live, what they may hold, publishing (a fake gh:
success deletes the clips, failure keeps them), cleaning, and the PCSX2 path never touching a screen capture. No
FFmpeg, gh, PCSX2 or game data is needed: every program is a fake."""

from __future__ import annotations

import json
import os
import re
import subprocess
import time
from collections.abc import Sequence
from pathlib import Path
from typing import Any, ClassVar

import pytest
from conventions_support import REPO, conventions_with

from coney_tools import clips, pcsx2_claims, pcsx2_cli, pcsx2_clip, pcsx2_proc
from coney_tools.conventions import Clips

RULES = Clips(folder="clips", max_seconds=10, warn_mb=10, keep_hours=24)

#: What ffprobe says of a clean clip, as Coney's encoder writes it.
CLEAN_PROBE: dict[str, Any] = {
    "streams": [
        {
            "codec_type": "video",
            "width": 960,
            "height": 720,
            "tags": {
                "language": "und",
                "handler_name": "VideoHandler",
                "vendor_id": "[0][0][0][0]",
                "encoder": "Lavc libx264",
            },
        },
        {
            "codec_type": "audio",
            "tags": {"language": "und", "handler_name": "SoundHandler", "vendor_id": "[0][0][0][0]"},
        },
    ],
    "format": {
        "duration": "5.000000",
        "size": "1500000",
        "tags": {"major_brand": "isom", "minor_version": "512", "compatible_brands": "isomiso2avc1mp41"},
    },
}


def _done(args: Sequence[str], code: int = 0, out: str = "", err: str = "") -> subprocess.CompletedProcess[str]:
    """A finished process as subprocess.run returns it."""
    return subprocess.CompletedProcess(list(args), code, out, err)


class FakePrograms:
    """ffprobe and gh as fakes: ffprobe answers `probe`, gh exits with `gh_code`; every call is kept."""

    def __init__(self, gh_code: int = 0, probe: dict[str, Any] | None = None) -> None:
        self.gh_code = gh_code
        self.probe = probe or CLEAN_PROBE
        self.calls: list[list[str]] = []

    def __call__(self, args: Sequence[str]) -> subprocess.CompletedProcess[str]:
        """Run one fake program."""
        self.calls.append(list(args))
        if Path(args[0]).stem == "ffprobe":
            return _done(args, out=json.dumps(self.probe))
        if args[0] == "gh":
            return _done(
                args, self.gh_code, "https://example.invalid/comment/1", "upload failed" if self.gh_code else ""
            )
        raise AssertionError(f"unexpected program {args[0]}")

    def gh_calls(self) -> list[list[str]]:
        """The gh commands run."""
        return [call for call in self.calls if call[0] == "gh"]


@pytest.fixture
def scratch(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> Path:
    """A scratch folder outside any checkout, with the rules and tools of the clips module pointed at fakes."""
    root = tmp_path / "scratch"
    root.mkdir()
    monkeypatch.setattr(clips, "scratch_root", lambda cwd=None: root)
    monkeypatch.setattr(clips, "clip_rules", lambda cwd=None: RULES)
    monkeypatch.setattr(clips, "find_tool", lambda name, cwd=None: Path(name))
    return root


def _clip(scratch: Path, name: str = "after.mp4", task: str = "my-task", age_hours: float = 0.0) -> Path:
    """A file standing in for a clip in `task`'s clips folder, last changed `age_hours` ago."""
    folder = scratch / task / "clips"
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / name
    path.write_bytes(b"\0" * 64)
    if age_hours:
        stamp = time.time() - age_hours * 3600
        os.utime(path, (stamp, stamp))
    return path


# --- where clips live ------------------------------------------------------------------------------------------------


def test_a_clip_in_a_tasks_clips_folder_is_accepted(scratch: Path) -> None:
    path = scratch / "my-task" / "clips" / "before.mp4"
    assert clips.check_clip_path(path, scratch, RULES) == path.resolve()


@pytest.mark.parametrize(
    ("relative", "message"),
    [
        ("my-task/before.mp4", "clips live only in"),
        ("my-task/shots/before.mp4", "clips live only in"),
        ("my-task/clips/deeper/before.mp4", "clips live only in"),
        ("before.mp4", "clips live only in"),
        ("my-task/clips/before.mkv", "ending in .mp4"),
        ("my-task/clips/my clip.mp4", "ending in .mp4"),
        ("my-task/clips/.hidden.mp4", "ending in .mp4"),
    ],
)
def test_a_clip_anywhere_else_is_refused(scratch: Path, relative: str, message: str) -> None:
    with pytest.raises(clips.ClipError, match=message):
        clips.check_clip_path(scratch / relative, scratch, RULES)


def test_a_clip_inside_a_git_checkout_is_refused(tmp_path: Path) -> None:
    # A worktree's `.git` is a file, a main checkout's a folder: both mark a checkout.
    scratch = tmp_path / "scratch"
    (scratch / "task" / "clips").mkdir(parents=True)
    (scratch / "task" / ".git").write_text("gitdir: elsewhere", encoding="utf-8")
    with pytest.raises(clips.ClipError, match="inside the git checkout"):
        clips.check_clip_path(scratch / "task" / "clips" / "a.mp4", scratch, RULES)


def test_the_real_repository_refuses_clips(tmp_path: Path) -> None:
    assert clips.git_checkout_around(REPO / "docs" / "clips" / "a.mp4") is not None
    with pytest.raises(clips.ClipError, match="inside the git checkout"):
        clips.check_clip_path(REPO / "clips" / "a.mp4", REPO.parent, RULES)


def test_clip_folder_makes_the_tasks_folder_and_refuses_odd_names(scratch: Path) -> None:
    folder = clips.clip_folder("tools-clips", scratch, RULES)
    assert folder == scratch / "tools-clips" / "clips"
    assert folder.is_dir()
    with pytest.raises(clips.ClipError):
        clips.clip_folder("../escape", scratch, RULES)


@pytest.mark.reads("engine")
def test_the_cap_is_the_same_in_coney_and_the_conventions() -> None:
    """Coney's --record-seconds cap (C++) and workspace.clips.max_seconds are one rule."""
    header = (REPO / "src" / "core" / "clip_recording.h").read_text(encoding="utf-8")
    found = re.search(r"kMaxClipSeconds = (\d+);", header)
    assert found is not None
    assert int(found.group(1)) == conventions_with().workspace.clips.max_seconds


# --- never the screen ------------------------------------------------------------------------------------------------


@pytest.mark.parametrize("device", ["gdigrab", "ddagrab", "x11grab", "avfoundation", "dshow", "lavfi", "kmsgrab"])
def test_ffmpeg_capture_devices_are_refused(device: str) -> None:
    with pytest.raises(clips.ClipError, match="refused"):
        clips.refuse_screen_capture(["ffmpeg", "-f", device, "-i", "desktop", "out.mp4"])


def test_an_input_that_is_not_a_file_is_refused(tmp_path: Path) -> None:
    with pytest.raises(clips.ClipError, match="not a file"):
        clips.refuse_screen_capture(["ffmpeg", "-i", "title=PCSX2", "out.mp4"])
    with pytest.raises(clips.ClipError, match="not a file"):
        clips.refuse_screen_capture(["ffmpeg", "-i", str(tmp_path), "out.mp4"])


def test_a_file_or_standard_input_is_accepted(tmp_path: Path) -> None:
    source = tmp_path / "capture.mp4"
    source.write_bytes(b"")
    clips.refuse_screen_capture(["ffmpeg", "-f", "rawvideo", "-i", "pipe:0", "-i", str(source), "out.mp4"])


def test_run_ffmpeg_never_starts_a_refused_command() -> None:
    started: list[Sequence[str]] = []

    def runner(args: Sequence[str]) -> subprocess.CompletedProcess[str]:
        started.append(args)
        return _done(args)

    with pytest.raises(clips.ClipError):
        clips.run_ffmpeg(["ffmpeg", "-f", "gdigrab", "-i", "desktop", "out.mp4"], runner)
    assert started == []


def test_the_encode_can_skip_the_start_of_a_recording(tmp_path: Path) -> None:
    source = tmp_path / "capture.mp4"
    source.write_bytes(b"")
    plain = clips.encode_arguments(Path("ffmpeg"), source, tmp_path / "out.mp4", 5)
    assert "-ss" not in plain
    trimmed = clips.encode_arguments(Path("ffmpeg"), source, tmp_path / "out.mp4", 5, skip_seconds=0.25)
    clips.refuse_screen_capture(trimmed)
    # The skip is an input option (before -i), and the cut at the length still follows it.
    assert trimmed[trimmed.index("-ss") + 1] == "0.25"
    assert trimmed.index("-ss") < trimmed.index("-i")
    assert trimmed[trimmed.index("-t") + 1] == "5"


def test_the_encode_strips_metadata_and_caps_the_length(tmp_path: Path) -> None:
    source = tmp_path / "capture.mp4"
    source.write_bytes(b"")
    args = clips.encode_arguments(Path("ffmpeg"), source, tmp_path / "out.mp4", 10)
    clips.refuse_screen_capture(args)  # a file input only
    joined = " ".join(args)
    for needed in (
        "-map_metadata -1",
        "-map_chapters -1",
        "-fflags +bitexact",
        "-flags:v +bitexact",
        "-bsf:v filter_units=remove_types=6",
        "-movflags +faststart",
        "-t 10",
        "-c:v libx264",
        "yuv420p",
        "scale=960:720",
    ):
        assert needed in joined, needed


# --- what a clip holds -----------------------------------------------------------------------------------------------


def test_a_clean_clip_has_no_problems() -> None:
    info = clips.parse_probe(json.dumps(CLEAN_PROBE))
    assert (info.width, info.height, info.audio, info.seconds) == (960, 720, True, 5.0)
    assert clips.clip_problems(info, RULES) == []


@pytest.mark.parametrize(
    ("where", "key", "value"),
    [
        ("format", "title", "C:/Users/someone/clip"),
        ("format", "comment", "anything"),
        ("format", "encoder", "Lavf61.7.100"),
        ("stream", "encoder", "Lavc61.19.101 libx264"),
        ("stream", "handler_name", "Recorded by someone"),
    ],
)
def test_metadata_is_a_problem(where: str, key: str, value: str) -> None:
    data = json.loads(json.dumps(CLEAN_PROBE))
    tags = data["format"]["tags"] if where == "format" else data["streams"][0]["tags"]
    tags[key] = value
    problems = clips.clip_problems(clips.parse_probe(json.dumps(data)), RULES)
    assert any(key in problem for problem in problems)


def test_a_clip_past_the_cap_is_a_problem() -> None:
    data = json.loads(json.dumps(CLEAN_PROBE))
    data["format"]["duration"] = "12.5"
    assert any("more than 10 s" in p for p in clips.clip_problems(clips.parse_probe(json.dumps(data)), RULES))


def test_a_big_clip_is_reported() -> None:
    data = json.loads(json.dumps(CLEAN_PROBE))
    data["format"]["size"] = "12000000"
    assert "warning" in clips.describe(Path("a.mp4"), clips.parse_probe(json.dumps(data)), RULES)


# --- publish ---------------------------------------------------------------------------------------------------------


def test_publish_uploads_then_deletes_the_clips(scratch: Path) -> None:
    before, after = _clip(scratch, "before.mp4"), _clip(scratch, "after.mp4")
    programs = FakePrograms()
    assert clips.run_publish(clips.Target("pr", 12), "Before and after", None, [before, after], programs) == 0
    (gh,) = programs.gh_calls()
    assert gh[:4] == ["gh", "pr", "comment", "12"]
    assert gh.count("--attach") == 2
    assert not before.exists()
    assert not after.exists()


def test_publish_keeps_the_clips_when_gh_fails(scratch: Path, capsys: pytest.CaptureFixture[str]) -> None:
    clip = _clip(scratch)
    programs = FakePrograms(gh_code=1)
    assert clips.run_publish(clips.Target("issue", 7), None, Path("body.md"), [clip], programs) == 1
    assert clip.exists()
    assert "kept" in capsys.readouterr().out
    assert programs.gh_calls()[0][:4] == ["gh", "issue", "comment", "7"]


def test_publish_into_a_description_edits_it(scratch: Path) -> None:
    clip = _clip(scratch)
    programs = FakePrograms()
    assert clips.run_publish(clips.Target("pr", 3, edit=True), None, Path("PR.md"), [clip], programs) == 0
    assert programs.gh_calls()[0][:6] == ["gh", "pr", "edit", "3", "--body-file", "PR.md"]


def test_publish_uploads_nothing_when_a_clip_breaks_a_rule(scratch: Path, tmp_path: Path) -> None:
    good = _clip(scratch)
    stray = tmp_path / "stray.mp4"
    stray.write_bytes(b"")
    programs = FakePrograms()
    with pytest.raises(clips.ClipError, match="clips live only in"):
        clips.run_publish(clips.Target("pr", 1), "x", None, [good, stray], programs)
    assert programs.gh_calls() == []
    assert good.exists()


def test_publish_uploads_nothing_with_metadata_in_a_clip(scratch: Path) -> None:
    clip = _clip(scratch)
    data = json.loads(json.dumps(CLEAN_PROBE))
    data["format"]["tags"]["title"] = "secret"
    programs = FakePrograms(probe=data)
    with pytest.raises(clips.ClipError, match="metadata"):
        clips.run_publish(clips.Target("pr", 1), "x", None, [clip], programs)
    assert programs.gh_calls() == []
    assert clip.exists()


def test_a_comment_needs_text(scratch: Path) -> None:
    with pytest.raises(clips.ClipError, match="--body"):
        clips.run_publish(clips.Target("pr", 1), None, None, [_clip(scratch)], FakePrograms())


# --- clean -----------------------------------------------------------------------------------------------------------


def test_clean_deletes_old_clips_and_keeps_new_ones(scratch: Path) -> None:
    old = _clip(scratch, "old.mp4", age_hours=30)
    new = _clip(scratch, "new.mp4", age_hours=1)
    other = _clip(scratch, "other.mp4", task="another", age_hours=48)
    elsewhere = scratch / "my-task" / "shots" / "keep.png"
    elsewhere.parent.mkdir()
    elsewhere.write_bytes(b"")
    os.utime(elsewhere, (0, 0))
    assert clips.run_clean(None, dry_run=False) == 0
    assert not old.exists()
    assert not other.exists()
    assert new.exists()
    assert elsewhere.exists()


def test_clean_removes_a_failed_recordings_staging_folder(scratch: Path) -> None:
    staging = scratch / "my-task" / "clips" / "a.pcsx2-capture"
    staging.mkdir(parents=True)
    leftover = staging / "capture.mp4"
    leftover.write_bytes(b"")
    os.utime(leftover, (0, 0))
    clips.run_clean(None, dry_run=False)
    assert not staging.exists()
    assert (scratch / "my-task" / "clips").is_dir()


def test_clean_dry_run_deletes_nothing(scratch: Path) -> None:
    old = _clip(scratch, "old.mp4", age_hours=30)
    clips.run_clean(None, dry_run=True)
    assert old.exists()
    clips.run_clean(0, dry_run=False)
    assert not old.exists()


# --- clips record ----------------------------------------------------------------------------------------------------


class FakeEmulator:
    """Stands in for pcsx2_cli.Emulator: keeps the ini settings it was given and whether it was closed."""

    made: ClassVar[list[FakeEmulator]] = []

    def __init__(self, paths: Any, state: Path, patches: list[Any], on_start: Any = None, ini: Any = ()) -> None:
        self.state = state
        self.ini = list(ini)
        self.pid = 4242
        self.closed = False
        FakeEmulator.made.append(self)

    def close(self) -> None:
        """Note the close."""
        self.closed = True


class FakeRegistry:
    """A claim registry with one claim, made by the run."""

    def __init__(self) -> None:
        self.released: list[str] = []

    def claim_for_run(self, agent: str | None, pcsx2_dir: Path | None, pid: int | None) -> tuple[Any, bool]:
        """The run's claim, made now."""
        return type("Claim", (), {"agent": agent, "copy": "pcsx2-b", "path": "copy"})(), True

    def record_pid(self, claim: Any, pid: int) -> None:
        """Nothing to record."""

    def release(self, agent: str, copy: str) -> None:
        """Note the release."""
        self.released.append(copy)


def test_pcsx2_clip_uses_pcsx2s_own_capture_and_never_the_screen(
    scratch: Path, monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    FakeEmulator.made = []
    registry = FakeRegistry()
    toggles: list[int] = []
    encodes: list[list[str]] = []

    def toggle(hwnds: list[int], chords: list[list[int]], hold: int, gap: int) -> None:
        # The second press stops PCSX2's capture: it has then written its file into the Videos folder.
        toggles.append(chords[0][0])
        if len(toggles) == 2:
            videos = Path(dict(((s, k), v) for s, k, v in FakeEmulator.made[0].ini)[("Folders", "Videos")])
            (videos / "The Warriors 2026-10-12.mp4").write_bytes(b"\0" * 100)

    def no_screen(*args: Any, **kwargs: Any) -> Any:
        raise AssertionError("clips record read the screen or a window")

    def encode(args: Sequence[str], runner: Any = None) -> None:
        clips.refuse_screen_capture(args)  # its input is PCSX2's own capture file
        encodes.append(list(args))
        Path(args[-1]).write_bytes(b"\0" * 10)

    monkeypatch.setattr(pcsx2_cli, "Emulator", FakeEmulator)
    monkeypatch.setattr(pcsx2_cli, "resolve_paths", lambda *a: None)
    monkeypatch.setattr(pcsx2_claims, "default_registry", lambda: registry)
    monkeypatch.setattr(pcsx2_proc, "key_targets", lambda pid: [type("W", (), {"hwnd": 1})()])
    monkeypatch.setattr(pcsx2_proc, "post_keys", toggle)
    monkeypatch.setattr(pcsx2_proc, "capture", no_screen)
    monkeypatch.setattr(pcsx2_proc, "render_window", no_screen)
    sleeps: list[float] = []
    monkeypatch.setattr(time, "sleep", sleeps.append)
    monkeypatch.setattr(clips, "run_ffmpeg", encode)
    monkeypatch.setattr(clips, "probe", lambda path, ffprobe, runner=None: clips.parse_probe(json.dumps(CLEAN_PROBE)))

    out = scratch / "my-task" / "clips" / "original.mp4"
    out.parent.mkdir(parents=True)
    assert pcsx2_clip.run_clip(tmp_path / "state.p2s", 5, out, (None, None, None), "agent-x") == 0

    (emulator,) = FakeEmulator.made
    settings = {(section, key): value for section, key, value in emulator.ini}
    assert settings[("EmuCore/GS", "EnableVideoCapture")] == "true"
    assert settings[("Hotkeys", "ToggleVideoCapture")] == "Keyboard/F12"
    osd = [key for (section, key), value in settings.items() if key.startswith("OsdShow")]
    assert osd
    assert all(settings[("EmuCore/GS", key)] == "false" for key in osd)
    assert len(toggles) == 2
    assert emulator.closed
    assert registry.released == ["pcsx2-b"]
    (encode_args,) = encodes
    assert "-map_metadata" in encode_args
    # PCSX2's capture starts with torn frames: the clip drops its first CAPTURE_START_TRIM seconds, the capture runs
    # that much longer, and the clip still lasts the 5 seconds asked for.
    # Measured on PCSX2 2.9.94: the raw capture is torn for its first 1.15 s, so the trim must clear that.
    assert pcsx2_clip.CAPTURE_START_TRIM >= 1.15
    assert encode_args[encode_args.index("-ss") + 1] == str(pcsx2_clip.CAPTURE_START_TRIM)
    assert encode_args[encode_args.index("-t") + 1] == "5"
    assert max(sleeps) >= 5 + pcsx2_clip.CAPTURE_START_TRIM
    assert encode_args[-1] == str(out.resolve())
    assert out.exists()
    assert not (out.parent / "original.pcsx2-capture").exists()


def test_pcsx2_clip_refuses_more_than_the_cap(scratch: Path, tmp_path: Path) -> None:
    with pytest.raises(clips.ClipError, match="1 to 10"):
        pcsx2_clip.run_clip(tmp_path / "s.p2s", 11, scratch / "t" / "clips" / "a.mp4", (None, None, None), "a")


def test_pcsx2_clip_code_holds_no_screen_capture() -> None:
    """The PCSX2 clip path names no screen or window capture at all."""
    source = Path(pcsx2_clip.__file__).read_text(encoding="utf-8")
    code = "\n".join(line for line in source.splitlines() if not line.lstrip().startswith("#"))
    code = re.sub(r'"""(.|\n)*?"""', "", code)
    for banned in ("PrintWindow", "pcsx2_proc.capture", "render_window", "grab", "ImageGrab", "BitBlt", "mss"):
        assert banned not in code, banned
