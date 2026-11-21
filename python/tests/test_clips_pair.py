# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `coney-tools clips pair`, `clips encode` and `clips scenarios`: the named scenarios, Coney's command line
and its record line, the base checkout kept by commit, the side-by-side compare and the encodes that fit GitHub (the
size ladders, the GIF budget, `--slow` and the cut message) and the busy marker. Every program is a fake, except FFmpeg
in the tests of a real compare (skipped without it); no Coney, PCSX2 or game data is needed, and the only frames are
synthetic ones drawn here."""

from __future__ import annotations

import dataclasses
import json
import os
import subprocess
from collections.abc import Sequence
from fractions import Fraction
from pathlib import Path

import pytest
from conventions_support import REPO

from coney_tools import clips, clips_pair
from coney_tools.conventions import ClipPair, Clips, load_conventions

PAIR = ClipPair(
    scenarios="tools/clips",
    base_prefix="clips-base-",
    preset="dev",
    upload_max_mb=10,
    mp4_heights=(720, 540, 432, 360),
    gif_widths=(800, 640, 480),
    gif_fps=10,
    gif_kb_per_second=512,
    gif_min_seconds=2,
)
RULES = Clips(folder="clips", max_seconds=10, warn_mb=10, keep_hours=24, pair=PAIR)

#: The line `coney --record` prints at the end of a clip (src/platform/main.cpp).
RECORD_LINE = (
    "clip {path}: {frames} frames ({seconds:.2f} s) of 640x448 as 960x720, with sound, 1.20 MB, frames hash {digest}\n"
)


def _done(args: Sequence[str], code: int = 0, out: str = "", err: str = "") -> subprocess.CompletedProcess[str]:
    """A finished process as subprocess.run returns it."""
    return subprocess.CompletedProcess(list(args), code, out, err)


def _git(folder: Path, *args: str) -> str:
    """Run git in `folder` with a made-up identity; its output."""
    env = {
        **os.environ,
        "GIT_AUTHOR_NAME": "t",
        "GIT_AUTHOR_EMAIL": "t@example.invalid",
        "GIT_COMMITTER_NAME": "t",
        "GIT_COMMITTER_EMAIL": "t@example.invalid",
    }
    done = subprocess.run(["git", "-C", str(folder), *args], check=True, capture_output=True, text=True, env=env)
    return done.stdout.strip()


def _scenario(folder: Path, name: str, text: str) -> Path:
    """A scenario file `name`.toml in `folder` holding `text`."""
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / f"{name}.toml"
    path.write_text(text, encoding="utf-8")
    return path


@pytest.fixture
def checkout(tmp_path: Path) -> Path:
    """A folder standing for a checkout: an input script and a scenarios folder."""
    root = tmp_path / "repo"
    (root / "tests" / "support").mkdir(parents=True)
    (root / "tests" / "support" / "walk.txt").write_text("150 stick left 0 60\n", encoding="utf-8")
    return root


# --- scenarios ---------------------------------------------------------------------------------------------------


def test_a_scenario_loads_with_its_input_script_from_the_checkout(checkout: Path) -> None:
    path = _scenario(
        checkout / "tools" / "clips",
        "walk",
        'description = "a walk"\nlevel = "level99"\ninput = "tests/support/walk.txt"\nstart_frame = 150\nseconds = 5\n',
    )
    run = clips_pair.load_scenario(path, checkout)
    assert run.name == "walk"
    assert run.level == "level99"
    assert run.input_script == (checkout / "tests" / "support" / "walk.txt").resolve()
    assert (run.start_frame, run.seconds, run.skip_movies, run.checkpoint, run.ai_seed) == (150, 5, True, None, None)


@pytest.mark.parametrize(
    ("extra", "message"),
    [
        ("speed = 2\n", "speed"),  # an unknown key
        ('checkpoint = "two"\n', "checkpoint"),
    ],
)
def test_a_scenario_refuses_an_unknown_key_or_a_wrong_type(checkout: Path, extra: str, message: str) -> None:
    text = 'description = "d"\nlevel = "level99"\ninput = "tests/support/walk.txt"\nseconds = 5\n' + extra
    path = _scenario(checkout / "tools" / "clips", "bad", text)
    with pytest.raises(clips.ClipError, match=message):
        clips_pair.load_scenario(path, checkout)


def test_a_scenario_refuses_a_missing_input_script(checkout: Path) -> None:
    path = _scenario(
        checkout / "tools" / "clips", "lost", 'description = "d"\nlevel = "l"\ninput = "nope.txt"\nseconds = 5\n'
    )
    with pytest.raises(clips.ClipError, match=r"nope.txt"):
        clips_pair.load_scenario(path, checkout)


def test_the_clip_is_capped_but_the_run_before_it_is_not(checkout: Path) -> None:
    script = checkout / "tests" / "support" / "walk.txt"
    clips_pair.check_run(clips_pair.PairRun("w", "level99", script, seconds=5, start_frame=150), RULES)
    with pytest.raises(clips.ClipError, match="10 s"):
        clips_pair.check_run(clips_pair.PairRun("w", "level99", script, seconds=11), RULES)
    # `--record-from-frame` starts the clip late, so only the seconds shown count against the cap: a clip of 8 s from
    # frame 2100 (70 s into the run) is fine.
    clips_pair.check_run(clips_pair.PairRun("w", "level99", script, seconds=8, start_frame=2100), RULES)
    with pytest.raises(clips.ClipError, match="start_frame"):
        clips_pair.check_run(clips_pair.PairRun("w", "level99", script, seconds=5, start_frame=-1), RULES)


def test_an_unknown_scenario_name_lists_the_known_ones(checkout: Path) -> None:
    folder = checkout / "tools" / "clips"
    _scenario(folder, "walk", 'description = "d"\nlevel = "l"\ninput = "tests/support/walk.txt"\nseconds = 5\n')
    assert clips_pair.scenario_path("walk", checkout, PAIR) == folder / "walk.toml"
    with pytest.raises(clips.ClipError, match="walk"):
        clips_pair.scenario_path("run", checkout, PAIR)


@pytest.mark.reads("engine")
def test_every_scenario_in_the_repository_loads_and_fits_the_cap() -> None:
    rules = load_conventions(REPO).workspace.clips
    runs = clips_pair.list_scenarios(REPO, rules.pair)
    assert runs, f"no scenario in {rules.pair.scenarios}"
    for run in runs:
        clips_pair.check_run(run, rules)
        assert run.description


# --- Coney's command line and its record line ----------------------------------------------------------------------


def test_coney_records_in_test_mode_with_a_window(checkout: Path) -> None:
    script = checkout / "tests" / "support" / "walk.txt"
    run = clips_pair.PairRun("w", "level99", script, seconds=5, start_frame=150, checkpoint=2, ai_seed=3)
    args = clips_pair.coney_arguments(Path("coney.exe"), "H:/", run, Path("/s/t/clips/before.mp4"))
    assert args[:3] == ["coney.exe", "--disc", "H:/"]
    joined = " ".join(args)
    for part in (
        "--play-level level99",
        "--checkpoint 2",
        "--ai-seed 3",
        "--skip-movies",
        "--record-seconds 5",
        "--record-from-frame 150",
    ):
        assert part in joined
    assert args[args.index("--input-script") + 1] == str(script)
    assert args[args.index("--record") + 1] == str(Path("/s/t/clips/before.mp4"))
    assert "--headless" not in args  # --record reads the frame buffer of a window
    # A run that starts its clip with the first frame of play passes no start.
    first = clips_pair.PairRun("w", "level99", script, seconds=5)
    assert "--record-from-frame" not in clips_pair.coney_arguments(Path("coney.exe"), "H:/", first, Path("/s/t/a.mp4"))
    assert "--renderer" not in args  # Coney's own choice (GL3 in test mode) unless one is given


def test_coney_records_on_the_renderer_it_is_given(checkout: Path) -> None:
    script = checkout / "tests" / "support" / "walk.txt"
    run = clips_pair.PairRun("w", "level99", script, seconds=5)
    args = clips_pair.coney_arguments(Path("coney.exe"), "H:/", run, Path("/s/t/clips/after.mp4"), renderer="gpu")
    assert args[args.index("--renderer") + 1] == "gpu"


def test_the_record_line_gives_the_frames_and_their_hash() -> None:
    text = "loading\n" + RECORD_LINE.format(path="C:/s/clips/after.mp4", frames=300, seconds=10, digest="0x00ab")
    recording = clips_pair.parse_record_line(text)
    assert (recording.frames, recording.frames_hash) == (300, "0x00ab")
    with pytest.raises(clips.ClipError, match="no clip line"):
        clips_pair.parse_record_line("coney: --record: no folder\n")


def test_two_recordings_must_have_as_many_frames() -> None:
    before = clips_pair.Recording(Path("b.mp4"), 300, "0x1")
    assert clips_pair.same_frames(before, clips_pair.Recording(Path("a.mp4"), 300, "0x2")) is False
    assert clips_pair.same_frames(before, clips_pair.Recording(Path("a.mp4"), 300, "0x1")) is True
    with pytest.raises(clips.ClipError, match="299"):
        clips_pair.same_frames(before, clips_pair.Recording(Path("a.mp4"), 299, "0x2"))


# --- the encodes ---------------------------------------------------------------------------------------------------


def test_a_gif_keeps_every_frame_when_its_delay_is_whole() -> None:
    assert clips_pair.gif_rate(Fraction(30), 10) == 10  # 30 fps cannot be a GIF's: one frame in three
    assert clips_pair.gif_rate(Fraction(10), 10) == 10  # --slow 3: every frame, 10 hundredths each
    assert clips_pair.gif_rate(Fraction(20), 10) == 20  # --slow 1.5: every frame, 5 hundredths each
    assert clips_pair.gif_rate(Fraction(15), 10) == 10  # --slow 2: 15 fps is 6.67 hundredths, an uneven step
    assert clips_pair.gif_rate(Fraction(30, 7), 10) == 4


def test_a_gifs_budget_is_a_rate_counted_between_its_bounds() -> None:
    assert clips_pair.gif_budget(5, RULES) == 5 * 512 * 1024
    assert clips_pair.gif_budget(1, RULES) == 2 * 512 * 1024
    assert clips_pair.gif_budget(30, RULES) == 10 * 512 * 1024


def test_slowing_counts_against_the_cap_and_says_what_it_cut() -> None:
    assert clips_pair.shown_seconds(5, 1.0, 10) == (5, None)
    seconds, message = clips_pair.shown_seconds(5, 4.0, 10)
    assert seconds == 2.5
    assert message is not None
    assert "cut at 10 s" in message
    assert "75 frames (2.50 s) of 150" in message


def _panels(folder: Path) -> list[clips_pair.Panel]:
    """Two Coney panels with labels, as files refuse_screen_capture accepts."""
    folder.mkdir(parents=True, exist_ok=True)
    panels = []
    for side in ("before", "after"):
        (folder / f"{side}.mp4").write_bytes(b"x")
        (folder / f"{side}.png").write_bytes(b"x")
        panels.append(clips_pair.Panel(folder / f"{side}.mp4", folder / f"{side}.png", start_frame=150))
    return panels


def test_the_compare_stacks_the_labelled_panels_side_by_side_without_sound(tmp_path: Path) -> None:
    panels = _panels(tmp_path)
    args = clips_pair.compose_arguments(
        Path("ffmpeg"), panels, tmp_path / "compare.mp4", seconds=5.0, slow=1.0, height=540
    )
    clips.refuse_screen_capture(args)
    graph = args[args.index("-filter_complex") + 1]
    assert "trim=start_frame=150:end_frame=300" in graph
    assert "scale=-2:540" in graph
    assert graph.count("overlay=") == 2
    assert "hstack=inputs=2" in graph
    for setting in ("-an", "-map_metadata", "+bitexact", "+faststart", "libx264"):
        assert setting in args
    assert args[args.index("-t") + 1] == "5.000"


def test_a_slowed_compare_keeps_every_frame_and_lasts_longer(tmp_path: Path) -> None:
    args = clips_pair.compose_arguments(
        Path("ffmpeg"), _panels(tmp_path), tmp_path / "compare.mp4", seconds=2.5, slow=4.0, height=720
    )
    graph = args[args.index("-filter_complex") + 1]
    assert "trim=start_frame=150:end_frame=225" in graph  # 2.5 s of source
    assert "setpts=(PTS-STARTPTS)*4" in graph
    assert args[args.index("-t") + 1] == "10.000"


def test_a_gif_has_a_small_palette_and_loops(tmp_path: Path) -> None:
    args = clips_pair.compose_arguments(
        Path("ffmpeg"), _panels(tmp_path), tmp_path / "compare.gif", seconds=5.0, slow=1.0, gif=(800, 10)
    )
    graph = args[args.index("-filter_complex") + 1]
    assert "palettegen=max_colors=128" in graph
    assert "fps=10" in graph
    assert "scale='min(800,iw)':-2" in graph
    assert args[args.index("-loop", args.index("-filter_complex")) + 1] == "0"
    assert "libx264" not in args


class FakeFfmpeg:
    """FFmpeg as a fake: each call writes its output file with the next size of `sizes`; the calls are kept."""

    def __init__(self, sizes: Sequence[int]) -> None:
        self.sizes = list(sizes)
        self.calls: list[list[str]] = []

    def __call__(self, args: Sequence[str]) -> subprocess.CompletedProcess[str]:
        self.calls.append(list(args))
        Path(args[-1]).write_bytes(b"\0" * self.sizes.pop(0))
        return _done(args)


def test_an_mp4_is_encoded_smaller_while_over_githubs_limit(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    fake = FakeFfmpeg([12_000_000, 9_000_000])
    out = tmp_path / "compare.mp4"
    size = clips_pair.encode_fitting(Path("ffmpeg"), _panels(tmp_path / "in"), out, RULES, 5.0, 1.0, runner=fake)
    assert size == 9_000_000
    assert out.stat().st_size == 9_000_000
    assert ["scale=-2:720" in " ".join(call) for call in fake.calls] == [True, False]
    assert "scale=-2:540" in " ".join(fake.calls[1])
    assert not list(tmp_path.glob("*.part*"))
    printed = capsys.readouterr().out
    assert "12.00 MB" in printed
    assert "9.00 MB" in printed


def test_an_encode_too_big_at_every_size_fails_and_keeps_the_clip_that_was_there(tmp_path: Path) -> None:
    out = tmp_path / "compare.mp4"
    out.write_bytes(b"good")
    fake = FakeFfmpeg([20_000_000] * 4)
    with pytest.raises(clips.ClipError, match="10 MB"):
        clips_pair.encode_fitting(Path("ffmpeg"), _panels(tmp_path / "in"), out, RULES, 5.0, 1.0, runner=fake)
    assert out.read_bytes() == b"good"
    assert not list(tmp_path.glob("*.part*"))


def test_a_gif_over_its_budget_at_every_width_is_kept_with_a_warning(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    fake = FakeFfmpeg([4_000_000, 3_500_000, 3_000_000])  # a 5 s GIF's budget is 2.6 MB
    out = tmp_path / "compare.gif"
    clips_pair.encode_fitting(Path("ffmpeg"), _panels(tmp_path / "in"), out, RULES, 5.0, 1.0, runner=fake)
    assert out.stat().st_size == 3_000_000
    assert "warning:" in capsys.readouterr().out


def test_a_slowed_gif_says_how_many_frames_it_keeps() -> None:
    # A GIF's delays are whole hundredths of a second, so at --slow 2 (15 frames a second) it plays at 10 and keeps
    # two frames of three; at --slow 3 (10 a second) it keeps them all; an unslowed GIF is never said to drop any.
    kept, total, line = clips_pair.gif_frames(5.0, 2.0, 10)
    assert (kept, total) == (100, 150)
    assert line is not None and "100 of 150" in line and "MP4" in line
    assert clips_pair.gif_frames(3.0, 3.0, 10)[2] is None
    assert clips_pair.gif_frames(5.0, 1.0, 10)[2] is None


def test_a_slowed_gif_encode_prints_the_frames_it_drops(tmp_path: Path) -> None:
    said: list[str] = []
    panels = _panels(tmp_path / "in")
    clips_pair.encode_fitting(
        Path("ffmpeg"), panels, tmp_path / "compare.gif", RULES, 5.0, 2.0, FakeFfmpeg([1000]), said.append
    )
    assert any("100 of 150" in line for line in said)
    said.clear()
    clips_pair.encode_fitting(
        Path("ffmpeg"), panels, tmp_path / "compare.mp4", RULES, 5.0, 2.0, FakeFfmpeg([1000]), said.append
    )
    assert not any("of 150" in line for line in said)  # an MP4 keeps every frame


def _real_tool(name: str) -> Path:
    """FFmpeg or ffprobe as the tools find them; the test is skipped on a machine without them."""
    try:
        return clips.find_tool(name, REPO)
    except clips.ClipError:
        pytest.skip(f"no {name} on this machine")


def _synthetic_clip(ffmpeg: Path, folder: Path, name: str, colour: tuple[int, int, int]) -> Path:
    """A 1 s, 30 frames a second, 64 x 48 clip of a white square moving across `colour`: frames drawn with PIL into
    files and encoded straight with FFmpeg (no lavfi source, which run_ffmpeg refuses)."""
    from PIL import Image, ImageDraw

    frames = folder / f"{name}-frames"
    frames.mkdir(parents=True)
    for index in range(30):
        image = Image.new("RGB", (64, 48), colour)
        ImageDraw.Draw(image).rectangle((index * 2, 16, index * 2 + 10, 26), fill=(255, 255, 255))
        image.save(frames / f"{index:03d}.png")
    out = folder / f"{name}.mp4"
    command = [str(ffmpeg), "-hide_banner", "-loglevel", "error", "-nostdin", "-y", "-framerate", "30"]
    command += ["-i", str(frames / "%03d.png"), "-pix_fmt", "yuv420p", str(out)]
    subprocess.run(command, check=True)
    return out


def _frame_count(ffprobe: Path, clip: Path) -> int:
    """How many frames the clip really holds (ffprobe decodes them all)."""
    command = [str(ffprobe), "-v", "error", "-count_frames", "-select_streams", "v:0"]
    command += ["-show_entries", "stream=nb_read_frames", "-of", "csv=p=0", str(clip)]
    return int(subprocess.run(command, check=True, capture_output=True, text=True).stdout.strip())


@pytest.mark.parametrize(("suffix", "slow"), [(".mp4", 1.0), (".mp4", 2.0), (".gif", 1.0), (".gif", 2.0)])
def test_a_real_compare_of_three_labelled_panels_is_a_valid_clip(tmp_path: Path, suffix: str, slow: float) -> None:
    # FFmpeg itself checks the filter graph here: the fakes above only read its text.
    ffmpeg, ffprobe = _real_tool("ffmpeg"), _real_tool("ffprobe")
    small = dataclasses.replace(PAIR, mp4_heights=(96,), gif_widths=(240,))
    rules = dataclasses.replace(RULES, pair=small)
    panels = [
        clips_pair.Panel(
            _synthetic_clip(ffmpeg, tmp_path, label.lower(), colour),
            clips_pair.label_image(label, 96, tmp_path / f"{label.lower()}.png"),
        )
        for label, colour in (("Original", (40, 40, 160)), ("Before", (160, 40, 40)), ("After", (40, 160, 40)))
    ]
    out = tmp_path / f"compare{suffix}"
    clips_pair.encode_fitting(ffmpeg, panels, out, rules, 1.0, slow, clips._run, lambda line: None)
    info = clips.probe(out, ffprobe)
    assert clips.clip_problems(info, rules) == []
    assert info.seconds == pytest.approx(slow, abs=0.15)
    assert not info.audio
    if suffix == ".mp4":
        assert (info.width, info.height) == (3 * 128, 96)  # three 4:3 panels side by side
        # 30 frames a second throughout: slowed, each source frame is shown `slow` times, none dropped (the -t cut
        # may take the last one)
        assert _frame_count(ffprobe, out) == pytest.approx(30 * slow, abs=1)
    else:
        assert info.width <= 240
        kept, _, _ = clips_pair.gif_frames(1.0, slow, small.gif_fps)
        assert _frame_count(ffprobe, out) == pytest.approx(kept, abs=1)


def test_an_encodes_bare_out_name_goes_beside_the_clip_and_a_path_stays_as_given(tmp_path: Path) -> None:
    clip = tmp_path / "task" / "clips" / "after.mp4"
    assert clips_pair.encode_target(clip, Path("compare.gif")) == tmp_path / "task" / "clips" / "compare.gif"
    elsewhere = tmp_path / "other" / "walk.mp4"
    assert clips_pair.encode_target(clip, elsewhere) == elsewhere


def test_a_label_is_a_small_picture_of_its_text(tmp_path: Path) -> None:
    from PIL import Image

    out = clips_pair.label_image("Before", 720, tmp_path / "before.png")
    with Image.open(out) as image:
        assert 0 < image.height < 720 / 4
        assert image.width > image.height


# --- the base checkout -----------------------------------------------------------------------------------------------


def test_the_base_checkout_is_a_detached_worktree_named_by_its_commit(tmp_path: Path) -> None:
    main = tmp_path / "repos" / "coney"
    main.mkdir(parents=True)
    _git(main, "init", "-q", "-b", "main")
    (main / "a.txt").write_text("a\n", encoding="utf-8")
    _git(main, "add", "-A")
    _git(main, "commit", "-q", "-m", "first")
    commit = _git(main, "rev-parse", "HEAD")
    path = clips_pair.base_checkout_path(main, "../../worktrees/coney/<branch>/", PAIR, commit)
    assert path == (tmp_path / "worktrees" / "coney" / f"clips-base-{commit[:12]}").resolve()
    clips_pair.ensure_base_checkout(main, path, commit, clips_pair.run_git)
    assert _git(path, "rev-parse", "HEAD") == commit
    assert _git(path, "rev-parse", "--abbrev-ref", "HEAD") == "HEAD"  # detached: workspace clean sees it merged
    clips_pair.ensure_base_checkout(main, path, commit, clips_pair.run_git)  # kept: made once per commit


def _history(tmp_path: Path) -> tuple[Path, dict[str, str]]:
    """A main checkout with three commits: `first`, `change` (the fix a pair takes out, in fix.txt) and `later` (an
    unrelated one after it, in later.txt); their commits by name."""
    main = tmp_path / "repos" / "coney"
    main.mkdir(parents=True)
    _git(main, "init", "-q", "-b", "main")
    commits = {}
    steps = (("first", "a.txt", "a\n"), ("change", "fix.txt", "fixed\n"), ("later", "later.txt", "l\n"))
    for name, file, text in steps:
        (main / file).write_text(text, encoding="utf-8")
        _git(main, "add", "-A")
        _git(main, "commit", "-q", "-m", name)
        commits[name] = _git(main, "rev-parse", "HEAD")
    return main, commits


def test_a_base_without_a_change_is_the_base_with_that_commit_taken_out(tmp_path: Path) -> None:
    # `clips pair --without` records the before on the merge base with one merged change taken out, so a fix that was
    # merged without clips gets its before from the same code minus the fix (and any tool the run needs, such as a
    # newer Coney option, is still there).
    main, commits = _history(tmp_path)
    worktrees = "../../worktrees/coney/<branch>/"
    path, commit = clips_pair.base_without(main, worktrees, PAIR, commits["later"], "HEAD~1", clips_pair.run_git)
    label = f"{commits['later'][:12]}-without-{commits['change'][:12]}"
    assert path == (tmp_path / "worktrees" / "coney" / f"clips-base-{label}").resolve()
    assert _git(path, "rev-parse", "HEAD") == commit
    assert _git(path, "rev-parse", "HEAD~1") == commits["later"]
    assert not (path / "fix.txt").exists()
    assert (path / "later.txt").is_file()
    assert _git(path, "rev-parse", "--abbrev-ref", "HEAD") == "HEAD"
    # The next pair from the same base and change finds it made and uses it as it is.
    again = clips_pair.base_without(main, worktrees, PAIR, commits["later"], commits["change"], clips_pair.run_git)
    assert again == (path, commit)


def test_a_base_without_a_change_refuses_one_not_in_the_base_or_one_that_does_not_come_out(tmp_path: Path) -> None:
    main, commits = _history(tmp_path)
    worktrees = "../../worktrees/coney/<branch>/"
    with pytest.raises(clips.ClipError, match="not in the base"):
        clips_pair.base_without(main, worktrees, PAIR, commits["first"], commits["change"], clips_pair.run_git)
    # A later commit that edits the change's own lines: taking the change out conflicts.
    (main / "fix.txt").write_text("fixed again\n", encoding="utf-8")
    _git(main, "commit", "-q", "-am", "again")
    tip = _git(main, "rev-parse", "HEAD")
    with pytest.raises(clips.ClipError, match="cannot be taken out"):
        clips_pair.base_without(main, worktrees, PAIR, tip, commits["change"], clips_pair.run_git)


def test_the_busy_marker_is_removed_only_when_it_is_ours(tmp_path: Path) -> None:
    with clips_pair.busy(tmp_path, "me", ".coney-busy"):
        assert "me" in (tmp_path / ".coney-busy").read_text(encoding="utf-8")
    assert not (tmp_path / ".coney-busy").exists()
    (tmp_path / ".coney-busy").write_text("someone\n", encoding="utf-8")
    with clips_pair.busy(tmp_path, "me", ".coney-busy"):
        pass
    assert (tmp_path / ".coney-busy").read_text(encoding="utf-8") == "someone\n"


def test_the_busy_marker_names_its_process_and_goes_when_the_block_raises(tmp_path: Path) -> None:
    # The pid lets `workspace clean` tell a marker whose process was killed from one in use (workspace_clean.py).
    with pytest.raises(RuntimeError), clips_pair.busy(tmp_path, "me", ".coney-busy"):
        assert f"pid={os.getpid()}" in (tmp_path / ".coney-busy").read_text(encoding="utf-8")
        raise RuntimeError("the build broke")
    assert not (tmp_path / ".coney-busy").exists()


def test_using_a_base_checkout_counts_as_activity(tmp_path: Path) -> None:
    # A no-op build and a run that writes to scratch touch nothing `workspace clean` reads, so a base used every day
    # would look idle and be rebuilt every `build_hours` without this.
    main = tmp_path / "repos" / "coney"
    main.mkdir(parents=True)
    _git(main, "init", "-q", "-b", "main")
    (main / "a.txt").write_text("a\n", encoding="utf-8")
    _git(main, "add", "-A")
    _git(main, "commit", "-q", "-m", "first")
    commit = _git(main, "rev-parse", "HEAD")
    path = tmp_path / "worktrees" / f"clips-base-{commit[:12]}"
    clips_pair.ensure_base_checkout(main, path, commit, clips_pair.run_git)
    head = Path(_git(path, "rev-parse", "--absolute-git-dir")) / "HEAD"
    os.utime(head, (1_000_000, 1_000_000))
    clips_pair.mark_used(path)
    assert head.stat().st_mtime > 1_000_000
    clips_pair.mark_used(tmp_path / "not-a-worktree")  # nothing to mark: no error


# --- the whole pair --------------------------------------------------------------------------------------------------


class FakeMachine:
    """git, the builds, Coney and FFmpeg as fakes. Coney writes its clip and prints the record line, with the frames
    and hash of `hashes` by side; every build and run is kept in order."""

    def __init__(self, base_commit: str, hashes: dict[str, str], frames: dict[str, int] | None = None) -> None:
        self.base_commit = base_commit
        self.hashes = hashes
        self.frames = frames or {"before": 300, "after": 300}
        self.events: list[str] = []
        self.busy_seen: list[bool] = []
        self.recorded_seconds: list[int] = []  # each --record-seconds Coney was given
        self.ffmpeg_graphs: list[str] = []  # each FFmpeg argument that trims a panel
        self.arguments: dict[str, list[str]] = {}

    def git(self, folder: Path, args: Sequence[str]) -> str:
        if args[0] == "merge-base":
            return self.base_commit + "\n"
        if args[:2] == ["rev-parse", "HEAD"]:
            return ("b" * 40 if folder.name.startswith("clips-base-") else "a" * 40) + "\n"
        if args[:2] == ["status", "--porcelain"]:
            return ""
        if args[:2] == ["worktree", "add"]:
            Path(args[-2]).mkdir(parents=True)
            self.events.append("worktree add")
            return ""
        raise AssertionError(f"unexpected git {args}")

    def build(self, checkout: Path, agent: str, preset: str) -> int:
        side = "base" if checkout.name.startswith("clips-base-") else "task"
        self.events.append(f"build {side} {preset}")
        if side == "base":
            self.busy_seen.append((checkout / ".coney-busy").exists())
        exe = clips_pair.coney_executable(checkout, preset, must_exist=False)
        exe.parent.mkdir(parents=True, exist_ok=True)
        exe.write_bytes(b"")
        return 0

    def coney(self, args: Sequence[str], cwd: Path, env: dict[str, str]) -> subprocess.CompletedProcess[str]:
        out = Path(args[args.index("--record") + 1])
        side = out.stem.rsplit("-", 1)[-1]
        self.arguments[side] = list(args)
        self.events.append(f"record {side} in {'base' if cwd.name.startswith('clips-base-') else 'task'}")
        assert "CONEY_FFMPEG" in env
        self.recorded_seconds.append(int(args[args.index("--record-seconds") + 1]))
        out.write_bytes(b"clip")
        line = RECORD_LINE.format(path=out, frames=self.frames[side], seconds=10, digest=self.hashes[side])
        return _done(args, out=line)

    def ffmpeg(self, args: Sequence[str]) -> subprocess.CompletedProcess[str]:
        self.events.append(f"ffmpeg {Path(args[-1]).name}")
        self.ffmpeg_graphs += [arg for arg in args if "trim=" in arg]
        Path(args[-1]).write_bytes(b"\0" * 1000)
        return _done(args)

    def original(self, state: Path, seconds: int, out: Path, agent: str) -> None:
        self.events.append(f"original {state.name} {seconds}")
        out.write_bytes(b"clip")


@pytest.fixture
def space(tmp_path: Path) -> dict[str, Path]:
    """A workspace's folders: the task's checkout and the scratch folder, and an input script."""
    task = tmp_path / "worktrees" / "coney" / "my-task"
    task.mkdir(parents=True)
    script = task / "walk.txt"
    script.write_text("150 stick left 0 60\n", encoding="utf-8")
    scratch = tmp_path / "scratch"
    scratch.mkdir()
    return {"task": task, "scratch": scratch, "script": script, "main": tmp_path / "repos" / "coney"}


def _pair(space: dict[str, Path], machine: FakeMachine, **options: object) -> int:
    """Run the pair of a 5 s walk from frame 150 on the fakes."""
    run = clips_pair.PairRun("walk", "level99", space["script"], seconds=5, start_frame=150)
    env = clips_pair.PairEnv(
        git=machine.git, build=machine.build, coney=machine.coney, ffmpeg=machine.ffmpeg, original=machine.original
    )
    settings = clips_pair.PairSettings(
        rules=RULES,
        worktrees="../../worktrees/coney/<branch>/",
        busy_file=".coney-busy",
        scratch=space["scratch"],
        main=space["main"],
        ffmpeg=Path("ffmpeg"),
    )
    return clips_pair.run_pair(space["task"], "my-task", run, "me", "H:/", settings, env, **options)  # type: ignore[arg-type]


def test_a_pair_builds_both_sides_records_the_same_run_and_stacks_them(
    space: dict[str, Path], capsys: pytest.CaptureFixture[str]
) -> None:
    machine = FakeMachine("c" * 40, {"before": "0x01", "after": "0x02"})
    assert _pair(space, machine) == 0
    assert machine.events == [
        "build task dev",
        "worktree add",
        "build base dev",
        "record before in base",
        "record after in task",
        "ffmpeg compare.part.mp4",
    ]
    assert machine.busy_seen == [True]
    base = space["task"].parent / f"clips-base-{'c' * 12}"
    assert not (base / ".coney-busy").exists()  # removed once the base build is no longer used
    folder = space["scratch"] / "my-task" / "clips"
    assert {path.name for path in folder.iterdir()} == {"before.mp4", "after.mp4", "compare.mp4", "pair.json"}
    record = json.loads((folder / "pair.json").read_text(encoding="utf-8"))
    assert record["before"]["frames_hash"] == "0x01"
    assert record["before"]["commit"] == "c" * 40
    assert record["after"]["frames_hash"] == "0x02"
    assert record["run"]["start_frame"] == 150
    assert machine.recorded_seconds == [5, 5]  # each clip is the 5 s shown, recorded from frame 150 on
    assert machine.ffmpeg_graphs and all("trim=start_frame=0:" in graph for graph in machine.ffmpeg_graphs)
    printed = capsys.readouterr().out
    assert "0x01" in printed
    assert "0x02" in printed


def test_a_pair_records_each_side_on_the_renderer_it_names(space: dict[str, Path]) -> None:
    # A change to the GPU renderer shows its picture after against GL3's before, the reference it matches, while the
    # merge base cannot record the GPU renderer's.
    machine = FakeMachine("c" * 40, {"before": "0x01", "after": "0x02"})
    assert _pair(space, machine, renderers=("gl3", "gpu")) == 0
    before = machine.arguments["before"]
    after = machine.arguments["after"]
    assert before[before.index("--renderer") + 1] == "gl3"
    assert after[after.index("--renderer") + 1] == "gpu"
    record = json.loads((space["scratch"] / "my-task" / "clips" / "pair.json").read_text(encoding="utf-8"))
    assert record["before"]["renderer"] == "gl3"
    assert record["after"]["renderer"] == "gpu"


class FakeMachineWithout(FakeMachine):
    """The fakes, with the git commands that take a change out of the base (`--without`): every one answers as if
    the change `d...` is in the base and comes out cleanly as tree `e...`, committed as `f...`."""

    def git(self, folder: Path, args: Sequence[str]) -> str:
        answers = {
            "rev-parse --verify": "d" * 40,
            "merge-base --is-ancestor": "",
            "merge-tree --write-tree": "e" * 40,
            "log -1": "someone\nsomeone@example.invalid",
        }
        for start, answer in answers.items():
            if " ".join(args).startswith(start):
                return answer + "\n"
        if "commit-tree" in args:
            self.events.append("commit-tree")
            return "f" * 40 + "\n"
        return super().git(folder, args)


def test_a_pair_without_a_merged_change_records_the_before_on_the_base_with_it_taken_out(
    space: dict[str, Path],
) -> None:
    machine = FakeMachineWithout("c" * 40, {"before": "0x01", "after": "0x02"})
    assert _pair(space, machine, without="1504") == 0
    assert machine.events[:4] == ["build task dev", "commit-tree", "worktree add", "build base dev"]
    assert "record before in base" in machine.events
    assert (space["task"].parent / f"clips-base-{'c' * 12}-without-{'d' * 12}").is_dir()
    record = json.loads((space["scratch"] / "my-task" / "clips" / "pair.json").read_text(encoding="utf-8"))
    assert record["before"]["commit"] == "f" * 40
    assert record["before"]["without"] == "1504"


def test_a_pair_with_the_original_and_a_gif_has_three_panels(space: dict[str, Path]) -> None:
    state = space["task"] / "street.p2s"
    state.write_bytes(b"")
    machine = FakeMachine("c" * 40, {"before": "0x01", "after": "0x02"})
    assert _pair(space, machine, original=state, gif=True) == 0
    assert "original street.p2s 5" in machine.events
    assert machine.events[-2:] == ["ffmpeg compare.part.mp4", "ffmpeg compare.part.gif"]
    folder = space["scratch"] / "my-task" / "clips"
    assert (folder / "original.mp4").is_file()
    assert (folder / "compare.gif").is_file()


def test_a_pair_whose_runs_differ_in_length_is_refused(space: dict[str, Path]) -> None:
    machine = FakeMachine("c" * 40, {"before": "0x01", "after": "0x02"}, frames={"before": 300, "after": 280})
    with pytest.raises(clips.ClipError, match="not the same run"):
        _pair(space, machine)
    assert not any(event.startswith("ffmpeg") for event in machine.events)


def test_a_pair_with_equal_frames_says_the_run_shows_no_change(
    space: dict[str, Path], capsys: pytest.CaptureFixture[str]
) -> None:
    machine = FakeMachine("c" * 40, {"before": "0x01", "after": "0x01"})
    assert _pair(space, machine) == 1
    assert "shows nothing" in capsys.readouterr().out
    assert not any(event.startswith("ffmpeg") for event in machine.events)


def test_a_failed_build_stops_the_pair_with_its_exit_code(space: dict[str, Path]) -> None:
    machine = FakeMachine("c" * 40, {"before": "0x01", "after": "0x02"})
    machine.build = lambda checkout, agent, preset: 75  # type: ignore[method-assign]
    assert _pair(space, machine) == 75


def _base(space: dict[str, Path]) -> Path:
    """The base checkout the fakes' pair uses."""
    return space["task"].parent / f"clips-base-{'c' * 12}"


def test_the_busy_marker_is_gone_after_a_failed_base_build(space: dict[str, Path]) -> None:
    machine = FakeMachine("c" * 40, {"before": "0x01", "after": "0x02"})
    task_build = machine.build
    machine.build = lambda checkout, agent, preset: (  # type: ignore[method-assign]
        2 if checkout.name.startswith("clips-base-") else task_build(checkout, agent, preset)
    )
    assert _pair(space, machine) == 2
    assert _base(space).is_dir() and not (_base(space) / ".coney-busy").exists()


def test_the_busy_marker_is_gone_after_coney_fails_on_the_base(space: dict[str, Path]) -> None:
    machine = FakeMachine("c" * 40, {"before": "0x01", "after": "0x02"})
    machine.coney = lambda args, cwd, env: _done(args, code=3, err="crashed")  # type: ignore[method-assign]
    with pytest.raises(clips.ClipError, match="before: Coney failed"):
        _pair(space, machine)
    assert not (_base(space) / ".coney-busy").exists()


def test_the_busy_marker_is_gone_after_runs_that_differ(space: dict[str, Path]) -> None:
    machine = FakeMachine("c" * 40, {"before": "0x01", "after": "0x02"}, frames={"before": 300, "after": 280})
    with pytest.raises(clips.ClipError):
        _pair(space, machine)
    assert not (_base(space) / ".coney-busy").exists()


def test_a_rerun_clears_the_last_pairs_clips_first(space: dict[str, Path]) -> None:
    # A rerun whose frames are equal makes no compare: the last pair's compare must not stay to be published.
    folder = space["scratch"] / "my-task" / "clips"
    folder.mkdir(parents=True)
    for name in ("compare.mp4", "compare.gif", "original.mp4", "pair.json", "walk-compare.mp4"):
        (folder / name).write_bytes(b"old")
    machine = FakeMachine("c" * 40, {"before": "0x01", "after": "0x01"})
    assert _pair(space, machine) == 1
    names = {path.name for path in folder.iterdir()}
    assert not names & {"compare.mp4", "compare.gif", "original.mp4"}
    assert "walk-compare.mp4" in names  # another prefix's pair is left alone
    assert json.loads((folder / "pair.json").read_text(encoding="utf-8"))["same"] is True


def test_clip_names_may_be_gifs() -> None:
    assert clips.CLIP_NAME.match("compare.gif")
    assert clips.CLIP_NAME.match("compare.mp4")
    assert not clips.CLIP_NAME.match("compare.png")
