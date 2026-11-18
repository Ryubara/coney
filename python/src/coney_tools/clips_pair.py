# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools clips pair`, `clips encode` and `clips scenarios`: before and after clips of one run, and encodes that
fit GitHub.

A pull request that changes what a player sees shows it before and after, and much of a change (motion, timing, AI,
the camera, effects) cannot be seen in a still. `coney --record` already makes a clip of a run in test mode (the fixed
step, seeded randomness, scripted input), so the same command gives the same frames and the same frame hash. `clips
pair` records one run twice with the same arguments:

* **before**, on a build of the branch's merge base with `origin/main` (never `origin/main` itself, which carries other
  pull requests' changes the branch does not have). The base is a detached worktree beside the task's,
  `worktrees/<repo>/<base_prefix><commit>`, built with `coney-tools build --target coney` under the build slots; the
  next pair from the same base finds it built. `workspace clean` removes it once it is as idle as a build folder.
* **after**, on the task's own build of its worktree as it stands.

It checks the two runs recorded as many frames (the same run), prints both frame hashes (equal hashes: the change
shows nothing in that run, and no compare is made), then stacks them side by side, labelled, in `compare.mp4`, with a
third panel of the original from PCSX2 (`clips record`) first when asked. `--gif` adds `compare.gif`.

The encodes port the ideas of GD Studio's `gds capture clip`: an MP4 is encoded at the first size of a ladder and
again at the next while it is over GitHub's limit; a GIF has a budget per second of it (it plays while it downloads,
so the budget is a rate), a 128-colour palette, a width under GitHub's description column and a frame rate whose delay
is a whole number of hundredths; `--slow N` keeps every frame and the slowed clip counts against the cap, with a line
saying what was cut; and a failed encode never replaces the file that was there. The sizes are
`workspace.clips.pair` in `.github/conventions.yml`. Every FFmpeg command goes through `clips.run_ffmpeg`.
"""

from __future__ import annotations

import contextlib
import json
import math
import os
import shutil
import subprocess
import sys
import tomllib
from collections.abc import Callable, Iterator, Sequence
from dataclasses import asdict, dataclass, field
from fractions import Fraction
from pathlib import Path
from typing import Any

from coney_tools import build_root, clips
from coney_tools.clips import ClipError
from coney_tools.config import ConfigError, disc_for_coney, find_repo_root, load_config, main_checkout
from coney_tools.conventions import ClipPair, Clips, load_conventions

#: Coney's clip frame rate: one frame per fixed step (`kClipFps`, src/core/clip_recording.h).
FPS = 30
#: A GIF's frame rates whose delay is a whole number of hundredths of a second, fastest first.
GIF_RATES = (20, 10, 5, 4, 2, 1)
#: The fastest a GIF keeps every frame at; above it browsers slow the frames down.
GIF_MAX_RATE = 20
#: How long a Coney run may take before it is taken for hung (the tests' hang guard, testing.md#no-wall-clock-limits).
CONEY_TIMEOUT = 600
#: The keys a scenario file may hold, and the ones it must.
SCENARIO_KEYS = frozenset(
    {"description", "level", "input", "seconds", "start_frame", "checkpoint", "ai_seed", "skip_movies"}
)
SCENARIO_REQUIRED = ("description", "level", "input", "seconds")


# --- the run ---------------------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class PairRun:
    """One deterministic run of Coney, recorded the same way on both builds."""

    #: The scenario's name, or `custom` for a run given on the command line.
    name: str
    #: The level `--play-level` starts.
    level: str
    #: The input script, an absolute path; the task's copy is played on both sides.
    input_script: Path
    #: How many seconds the clips show, from `start_frame` on.
    seconds: int
    #: The frame of play the clips start at (Coney records from the level's first frame of play; the frames before it
    #: are cut from the compare and the encodes).
    start_frame: int = 0
    checkpoint: int | None = None
    ai_seed: int | None = None
    skip_movies: bool = True
    description: str = ""

    @property
    def record_seconds(self) -> int:
        """The `--record-seconds` that holds the start and the clip."""
        return math.ceil((self.start_frame + self.seconds * FPS) / FPS)


def _scenario_value(data: dict[str, Any], key: str, kind: type, path: Path) -> Any:
    """`data[key]` when it is a `kind` (a bool is no int), else ClipError naming the file and the key."""
    value = data[key]
    if not isinstance(value, kind) or (kind is int and isinstance(value, bool)):
        raise ClipError(f"{path}: {key} must be {'a whole number' if kind is int else kind.__name__}")
    return value


def load_scenario(path: Path, root: Path) -> PairRun:
    """The run a scenario file describes. Its `input` is a path in the checkout `root`. ClipError for an unknown or
    missing key, a value of the wrong type, or an input script that is not there."""
    try:
        data = tomllib.loads(path.read_text(encoding="utf-8"))
    except (OSError, tomllib.TOMLDecodeError) as error:
        raise ClipError(f"{path}: cannot be read ({error})") from error
    unknown = sorted(set(data) - SCENARIO_KEYS)
    if unknown:
        raise ClipError(
            f"{path}: unknown key(s) {', '.join(unknown)}; a scenario holds {', '.join(sorted(SCENARIO_KEYS))}"
        )
    missing = [key for key in SCENARIO_REQUIRED if key not in data]
    if missing:
        raise ClipError(f"{path}: missing {', '.join(missing)}")
    script = (root / _scenario_value(data, "input", str, path)).resolve()
    if not script.is_file():
        raise ClipError(f"{path}: input {data['input']} is not a file of the checkout")
    optional = {
        key: _scenario_value(data, key, int, path) for key in ("start_frame", "checkpoint", "ai_seed") if key in data
    }
    return PairRun(
        name=path.stem,
        level=_scenario_value(data, "level", str, path),
        input_script=script,
        seconds=_scenario_value(data, "seconds", int, path),
        start_frame=optional.get("start_frame", 0),
        checkpoint=optional.get("checkpoint"),
        ai_seed=optional.get("ai_seed"),
        skip_movies=_scenario_value(data, "skip_movies", bool, path) if "skip_movies" in data else True,
        description=_scenario_value(data, "description", str, path),
    )


def scenario_path(name: str, root: Path, rules: ClipPair) -> Path:
    """The file of the scenario `name` in the checkout `root`; ClipError listing the known ones when there is none."""
    folder = root / rules.scenarios
    path = folder / f"{name}.toml"
    if not clips.TASK_NAME.match(name) or not path.is_file():
        known = ", ".join(sorted(p.stem for p in folder.glob("*.toml"))) or "none"
        raise ClipError(f"no scenario {name!r} in {rules.scenarios}; known: {known}")
    return path


def list_scenarios(root: Path, rules: ClipPair) -> list[PairRun]:
    """Every scenario of the checkout `root`, by name."""
    return [load_scenario(path, root) for path in sorted((root / rules.scenarios).glob("*.toml"))]


def check_run(run: PairRun, rules: Clips) -> None:
    """ClipError unless the run fits the cap: 1 to `max_seconds` shown, and the start and the clip together within
    `max_seconds`, since Coney records from the level's first frame of play."""
    if not 1 <= run.seconds <= rules.max_seconds:
        raise ClipError(f"{run.name}: seconds must be 1 to {rules.max_seconds} s, not {run.seconds}")
    if run.start_frame < 0:
        raise ClipError(f"{run.name}: start_frame must be 0 or more")
    if run.record_seconds > rules.max_seconds:
        raise ClipError(
            f"{run.name}: the start (frame {run.start_frame}) and {run.seconds} s make {run.record_seconds} s, more "
            f"than the {rules.max_seconds} s Coney records; show fewer seconds or start earlier"
        )


# --- Coney -----------------------------------------------------------------------------------------------------------


def coney_arguments(executable: Path, disc: str, run: PairRun, out: Path) -> list[str]:
    """Coney's command line that plays `run` from the level's first frame of play and records it to `out`. `--record`
    puts Coney in test mode (one step and one render a frame, no clock), so the same line gives the same frames; it
    needs a window, so the run is never `--headless` (test mode does not take the focus)."""
    args = [str(executable), "--disc", disc, "--play-level", run.level]
    if run.checkpoint is not None:
        args += ["--checkpoint", str(run.checkpoint)]
    if run.skip_movies:
        args.append("--skip-movies")
    if run.ai_seed is not None:
        args += ["--ai-seed", str(run.ai_seed)]
    return [
        *args,
        "--input-script",
        str(run.input_script),
        "--record",
        str(out),
        "--record-seconds",
        str(run.record_seconds),
    ]


@dataclass(frozen=True)
class Recording:
    """What `coney --record` said of its clip."""

    path: Path
    frames: int
    frames_hash: str


def parse_record_line(text: str) -> Recording:
    """The clip line Coney prints when its clip is done (`clip PATH: N frames (...), frames hash 0x...`); ClipError
    when the output has none."""
    for line in reversed(text.splitlines()):
        if line.startswith("clip ") and " frames hash " in line:
            head, _, digest = line.rpartition(" frames hash ")
            path, _, rest = head[len("clip ") :].rpartition(": ")
            frames = rest.split(" ", 1)[0]
            if frames.isdigit():
                return Recording(Path(path), int(frames), digest.strip())
    raise ClipError("Coney printed no clip line; its output ends: " + " | ".join(text.strip().splitlines()[-5:]))


def same_frames(before: Recording, after: Recording) -> bool:
    """Whether the two recordings hold the very same frames (equal hashes). ClipError when they hold a different number
    of frames: then they are not the same run."""
    if before.frames != after.frames:
        raise ClipError(
            f"not the same run: before recorded {before.frames} frames and after {after.frames}; "
            "both sides play the same arguments, so one of them stopped early (read its output)"
        )
    return before.frames_hash == after.frames_hash


def coney_executable(checkout: Path, preset: str, must_exist: bool = True) -> Path:
    """Coney's program in the checkout's build of `preset`. ClipError when `must_exist` and it is not there."""
    folder = build_root.preset_folder(checkout, preset) / "src" / "platform"
    for name in ("coney.exe", "coney"):
        if (folder / name).is_file():
            return folder / name
    if must_exist:
        raise ClipError(f"no Coney program in {folder}; the build did not make one")
    return folder / ("coney.exe" if os.name == "nt" else "coney")


# --- the encodes -----------------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class Panel:
    """One clip of a compare: its file, its label picture (None for none) and the frame it starts at."""

    clip: Path
    label: Path | None
    start_frame: int = 0


def gif_rate(rate: Fraction, preferred: int) -> Fraction:
    """A GIF's frame rate for frames played at `rate`: `rate` itself when its delay is a whole number of hundredths
    and it is no faster than GIF_MAX_RATE (a slowed clip keeps every frame), else `preferred` when it divides `rate`,
    else the fastest of GIF_RATES under it. Delays of 8, 8 and 9 hundredths (12 fps) play as a judder."""
    if rate <= GIF_MAX_RATE and (100 / rate).denominator == 1:
        return rate
    if preferred <= rate and (rate / preferred).denominator == 1:
        return Fraction(preferred)
    ceiling = rate if rate < preferred else Fraction(preferred)
    return Fraction(next((choice for choice in GIF_RATES if choice <= ceiling), 1))


def gif_budget(seconds: float, rules: Clips) -> int:
    """The most bytes a GIF of `seconds` should take: `gif_kb_per_second` for each second, counted for at least
    `gif_min_seconds` and at most `max_seconds`."""
    counted = min(max(seconds, rules.pair.gif_min_seconds), rules.max_seconds)
    return int(rules.pair.gif_kb_per_second * 1024 * counted)


def shown_seconds(available: float, slow: float, max_seconds: int) -> tuple[float, str | None]:
    """How many seconds of source a clip slowed `slow` times shows within `max_seconds`, and the line to print when
    that cuts some of the `available` seconds off (read it: the moment meant may be in the part cut)."""
    if available * slow <= max_seconds:
        return available, None
    kept = max_seconds / slow
    total = round(available * FPS)
    left = total - round(kept * FPS)
    return kept, (
        f"clip cut at {max_seconds} s: {left} frames ({left / FPS:.2f} s) of {total} left out at --slow {slow:g}; "
        "show fewer seconds or slow it less"
    )


def _rate_text(rate: Fraction) -> str:
    """A rate as FFmpeg reads it: `10`, or `10/3`."""
    return str(rate.numerator) if rate.denominator == 1 else f"{rate.numerator}/{rate.denominator}"


def compose_arguments(
    ffmpeg: Path,
    panels: Sequence[Panel],
    out: Path,
    *,
    seconds: float,
    slow: float,
    height: int | None = None,
    gif: tuple[int, int] | None = None,
) -> list[str]:
    """FFmpeg's arguments to put `panels` side by side in `out`: each cut to `seconds` of source from its start frame,
    slowed `slow` times (every frame kept), scaled to `height` (an MP4's panel height) with its label in the top left
    corner, and stacked left to right. With `gif` = (width, preferred rate) the stack is scaled to at most `width`
    and written as a looping GIF with a 128-colour palette at gif_rate(); else as an H.264 MP4 without sound or
    metadata (the clips' own settings, `clips.ENCODE_SETTINGS`, less the sound)."""
    panel_height = height or 720
    frames = round(seconds * FPS)
    inputs: list[str] = []
    for panel in panels:
        inputs += ["-i", str(panel.clip)]
    labels = [panel.label for panel in panels if panel.label is not None]
    for label in labels:
        inputs += ["-loop", "1", "-i", str(label)]
    margin = max(4, panel_height // 40)
    steps: list[str] = []
    label_index = len(panels)
    for index, panel in enumerate(panels):
        start = panel.start_frame
        step = (
            f"[{index}:v]trim=start_frame={start}:end_frame={start + frames},setpts=(PTS-STARTPTS)*{slow:g},"
            f"scale=-2:{panel_height}:flags=lanczos,setsar=1"
        )
        if panel.label is not None:
            steps.append(f"{step}[s{index}]")
            steps.append(f"[s{index}][{label_index}:v]overlay={margin}:{margin}:shortest=1[p{index}]")
            label_index += 1
        else:
            steps.append(f"{step}[p{index}]")
    stacked = "".join(f"[p{index}]" for index in range(len(panels)))
    if len(panels) > 1:
        steps.append(f"{stacked}hstack=inputs={len(panels)}:shortest=1[c]")
    else:
        steps.append(f"{stacked}null[c]")
    length = f"{seconds * slow:.3f}"
    head = [str(ffmpeg), "-hide_banner", "-loglevel", "error", "-nostdin", "-y", *inputs]
    if gif is not None:
        width, preferred = gif
        rate = gif_rate(Fraction(FPS) / Fraction(str(slow)).limit_denominator(1000), preferred)
        palette = "split[a][b];[a]palettegen=max_colors=128[pal];[b][pal]paletteuse=dither=bayer:bayer_scale=4"
        steps.append(f"[c]fps={_rate_text(rate)},scale='min({width},iw)':-2:flags=lanczos,{palette}[out]")
        tail = ["-map_metadata", "-1", "-fflags", "+bitexact", "-loop", "0", str(out)]
        return [*head, "-filter_complex", ";".join(steps), "-map", "[out]", "-t", length, *tail]
    steps.append("[c]format=yuv420p[out]")
    settings = list(clips.ENCODE_SETTINGS)
    for audio in ("-c:a", "-b:a", "-flags:a"):
        at = settings.index(audio)
        del settings[at : at + 2]
    return [*head, "-filter_complex", ";".join(steps), "-map", "[out]", "-t", length, "-an", *settings, str(out)]


def label_image(text: str, height: int, out: Path) -> Path:
    """A small picture of `text`, white on a dark box, sized for a panel `height` pixels high; written to `out`."""
    from PIL import Image, ImageDraw, ImageFont

    size = max(12, height // 18)
    font = ImageFont.load_default(size=size)
    left, top, right, bottom = (math.ceil(edge) for edge in font.getbbox(text))
    pad = max(4, size // 3)
    image = Image.new("RGBA", (right - left + 2 * pad, bottom - top + 2 * pad), (0, 0, 0, 170))
    ImageDraw.Draw(image).text((pad - left, pad - top), text, font=font, fill=(255, 255, 255, 255))
    image.save(out)
    return out


def _megabytes(size: int) -> str:
    """A size in decimal megabytes."""
    return f"{size / 1_000_000:.2f} MB"


def encode_fitting(
    ffmpeg: Path,
    panels: Sequence[Panel],
    out: Path,
    rules: Clips,
    seconds: float,
    slow: float,
    runner: clips.Runner = clips._run,
    say: Callable[[str], None] = print,
) -> int:
    """Encode `panels` into `out` (a GIF for a `.gif`, else an MP4) at the first size of its ladder, and again at the
    next while the file is too big: an MP4 over `upload_max_mb` (ClipError when it is still over at the last), a GIF
    over gif_budget() (kept at the last width with a warning, unless it is over `upload_max_mb` too). Each encoding
    goes to a part file first, so a failure leaves the file that was there. Returns the size."""
    gif = out.suffix == ".gif"
    limit = rules.pair.upload_max_mb * 1_000_000
    budget = gif_budget(seconds * slow, rules) if gif else limit
    sizes = rules.pair.gif_widths if gif else rules.pair.mp4_heights
    part = out.with_name(f"{out.stem}.part{out.suffix}")
    size = 0
    try:
        for step in sizes:
            options: dict[str, Any] = {"gif": (step, rules.pair.gif_fps)} if gif else {"height": step}
            args = compose_arguments(ffmpeg, panels, part, seconds=seconds, slow=slow, **options)
            clips.run_ffmpeg(args, runner)
            size = part.stat().st_size
            say(f"{out.name}: {_megabytes(size)} at {step} px {'wide' if gif else 'high a panel'}")
            if size <= budget:
                break
        else:
            if not gif or size > limit:
                what = "GIF" if gif else "clip"
                raise ClipError(
                    f"{out.name}: the {what} is still {_megabytes(size)} at {sizes[-1]} px, over GitHub's "
                    f"{rules.pair.upload_max_mb} MB; show fewer seconds"
                )
            say(
                f"warning: {out.name} is {_megabytes(size)}, over the {_megabytes(budget)} a {seconds * slow:g} s GIF "
                "plays smoothly in on GitHub; use the MP4, or show fewer seconds"
            )
        part.replace(out)
        return size
    finally:
        part.unlink(missing_ok=True)


# --- the base checkout -----------------------------------------------------------------------------------------------


def run_git(folder: Path, args: Sequence[str]) -> str:
    """Run git in `folder`; its output. ClipError with git's message when it fails."""
    done = subprocess.run(["git", "-C", str(folder), *args], capture_output=True, text=True, check=False)
    if done.returncode != 0:
        raise ClipError(f"git {' '.join(args)}: {done.stderr.strip() or 'failed'}")
    return done.stdout


def base_checkout_path(main: Path, worktrees: str, rules: ClipPair, commit: str) -> Path:
    """Where the build of `commit` is kept: `workspace.worktrees` with `<base_prefix><commit, 12 digits>` as its
    branch, from the main checkout `main`."""
    if "<branch>" not in worktrees:
        raise ConfigError(f"workspace.worktrees ({worktrees}) must contain <branch>")
    return (main / worktrees.replace("<branch>", f"{rules.base_prefix}{commit[:12]}")).resolve()


def ensure_base_checkout(main: Path, path: Path, commit: str, git: Callable[[Path, Sequence[str]], str]) -> None:
    """Make the detached worktree of `commit` at `path` unless it is there (made by an earlier pair from the same
    base). ClipError when one is there at another commit (moved by hand)."""
    if path.is_dir():
        head = git(path, ["rev-parse", "HEAD"]).strip()
        if head != commit:
            raise ClipError(f"{path} is at {head[:12]}, not the base {commit[:12]}; remove it and run again")
        return
    try:
        git(main, ["worktree", "add", "--detach", str(path), commit])
    except ClipError:
        if not path.is_dir():  # another pair from the same base made it at the same moment: use theirs
            raise


@contextlib.contextmanager
def busy(checkout: Path, agent: str, marker: str) -> Iterator[None]:
    """Hold `checkout`'s busy marker while the block runs, so `workspace clean` leaves the base build alone; a marker
    another run wrote is left as it is, and only ours is removed."""
    path = checkout / marker
    mine = f"{agent} clips pair {os.getpid()}\n"
    made = False
    try:
        with path.open("x", encoding="utf-8") as written:
            written.write(mine)
        made = True
    except FileExistsError:
        pass
    try:
        yield
    finally:
        with contextlib.suppress(OSError):
            if made and path.read_text(encoding="utf-8") == mine:
                path.unlink()


# --- the whole pair --------------------------------------------------------------------------------------------------


def _build(checkout: Path, agent: str, preset: str) -> int:
    """`coney-tools build --target coney` of `checkout` under the build slots; its exit code (75: it gave up
    waiting)."""
    command = [sys.executable, "-m", "coney_tools.cli", "build", "--agent", agent, "--preset", preset]
    return subprocess.run([*command, "--target", "coney", "--dir", str(checkout)], check=False).returncode


def _coney(args: Sequence[str], cwd: Path, env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    """Run Coney in `cwd` with `env`, its output kept; a run past CONEY_TIMEOUT is taken for hung."""
    try:
        return subprocess.run(
            list(args), cwd=cwd, env=env, capture_output=True, text=True, check=False, timeout=CONEY_TIMEOUT
        )
    except subprocess.TimeoutExpired as error:
        raise ClipError(f"Coney ran past {CONEY_TIMEOUT} s and was stopped: {' '.join(args)}") from error


def _original(state: Path, seconds: int, out: Path, agent: str) -> None:
    """The original's clip, through `clips record` (PCSX2's own capture), under `agent`'s claim."""
    from coney_tools import pcsx2_clip

    pcsx2_clip.run_clip(state, seconds, out, (None, None, None), agent)


@dataclass(frozen=True)
class PairEnv:
    """The programs a pair runs: git, the build, Coney, FFmpeg and the original's recording; tests replace them."""

    git: Callable[[Path, Sequence[str]], str] = run_git
    build: Callable[[Path, str, str], int] = _build
    coney: Callable[[Sequence[str], Path, dict[str, str]], subprocess.CompletedProcess[str]] = _coney
    ffmpeg: clips.Runner = clips._run
    original: Callable[[Path, int, Path, str], None] = _original
    say: Callable[[str], None] = field(default=print)


@dataclass(frozen=True)
class PairSettings:
    """Where a pair works and by which rules."""

    rules: Clips
    #: `workspace.worktrees` of the conventions.
    worktrees: str
    #: `workspace.busy_marker` of the conventions.
    busy_file: str
    #: The workspace's shared scratch folder.
    scratch: Path
    #: The main checkout (worktrees are added from it).
    main: Path
    ffmpeg: Path


def _record(
    side: str, executable: Path, checkout: Path, disc: str, run: PairRun, out: Path, ffmpeg: Path, env: PairEnv
) -> Recording:
    """Record `run` with Coney `executable`, run from `checkout`, into `out`; the recording its line describes. Coney
    finds FFmpeg through CONEY_FFMPEG, set to the one the tools found when it is not set."""
    variables = dict(os.environ)
    if not variables.get("CONEY_FFMPEG"):
        variables["CONEY_FFMPEG"] = str(ffmpeg)
    done = env.coney(coney_arguments(executable, disc, run, out), checkout, variables)
    if done.returncode != 0 or not out.is_file():
        tail = " | ".join((done.stdout + done.stderr).strip().splitlines()[-8:])
        raise ClipError(f"{side}: Coney failed (exit {done.returncode}): {tail}")
    recording = parse_record_line(done.stdout)
    env.say(f"{side}: {recording.frames} frames, frames hash {recording.frames_hash} ({checkout.name})")
    return Recording(out, recording.frames, recording.frames_hash)


def run_pair(
    checkout: Path,
    task: str,
    run: PairRun,
    agent: str,
    disc: str,
    settings: PairSettings,
    env: PairEnv | None = None,
    *,
    original: Path | None = None,
    gif: bool = False,
    slow: float = 1.0,
    prefix: str = "",
    base_ref: str = "origin/main",
) -> int:
    """`clips pair`: build the task's checkout and its merge base with `base_ref`, record `run` on both into the task's
    clips folder, check they are the same run, and stack them (with the original first when `original` names a PCSX2
    state) into `compare.mp4` and, with `gif`, `compare.gif`. Returns 0, the build's exit code when a build fails, or
    1 when both recordings hold the same frames (the change shows nothing in that run). ClipError for a run that
    breaks the rules or two recordings that are not the same run."""
    env = env or PairEnv()
    rules = settings.rules
    check_run(run, rules)
    if slow <= 0:
        raise ClipError(f"--slow must be more than 0, not {slow:g}")
    folder = clips.clip_folder(task, settings.scratch, rules)
    named = f"{prefix}-" if prefix else ""
    paths = {side: folder / f"{named}{side}.mp4" for side in ("before", "after", "original", "compare")}
    for path in paths.values():
        clips.check_clip_path(path, settings.scratch, rules)
    preset = rules.pair.preset

    # The after first: the task's own build is the one most likely to fail, and it is needed anyway.
    tip = env.git(checkout, ["rev-parse", "HEAD"]).strip()
    dirty = bool(env.git(checkout, ["status", "--porcelain"]).strip())
    code = env.build(checkout, agent, preset)
    if code != 0:
        env.say(f"the task's build failed (exit {code})")
        return code
    commit = env.git(checkout, ["merge-base", "HEAD", base_ref]).strip()
    base = base_checkout_path(settings.main, settings.worktrees, rules.pair, commit)
    env.say(f"before: the merge base with {base_ref}, {commit[:12]}, in {base}")
    ensure_base_checkout(settings.main, base, commit, env.git)
    with busy(base, agent, settings.busy_file):
        code = env.build(base, agent, preset)
        if code != 0:
            env.say(f"the base build failed (exit {code})")
            return code
        before = _record(
            "before", coney_executable(base, preset), base, disc, run, paths["before"], settings.ffmpeg, env
        )
    after = _record(
        "after", coney_executable(checkout, preset), checkout, disc, run, paths["after"], settings.ffmpeg, env
    )
    same = same_frames(before, after)
    record = {
        "run": {**asdict(run), "input_script": str(run.input_script)},
        "before": {"commit": commit, "frames": before.frames, "frames_hash": before.frames_hash},
        "after": {"commit": tip, "uncommitted": dirty, "frames": after.frames, "frames_hash": after.frames_hash},
        "same": same,
    }
    (folder / f"{named}pair.json").write_text(json.dumps(record, indent=1) + "\n", encoding="utf-8")
    if same:
        env.say(
            f"the frames are the same (hash {after.frames_hash}): the change shows nothing in this run; "
            "pick a scenario that shows it (no compare made)"
        )
        return 1

    seconds, cut = shown_seconds(run.seconds, slow, rules.max_seconds)
    if cut:
        env.say(cut)
    panels: list[tuple[str, Path, int]] = []
    if original is not None:
        env.original(original, math.ceil(seconds), paths["original"], agent)
        panels.append(("Original", paths["original"], 0))
    panels += [("Before", paths["before"], run.start_frame), ("After", paths["after"], run.start_frame)]
    _compose(panels, paths["compare"], settings, seconds, slow, gif, env)
    return 0


def _compose(
    panels: Sequence[tuple[str, Path, int]],
    out: Path,
    settings: PairSettings,
    seconds: float,
    slow: float,
    gif: bool,
    env: PairEnv,
) -> None:
    """Write the labelled compare `out` (and its GIF beside it with `gif`) from `panels` (label, clip, start frame).
    The labels are drawn into a parts folder in the clips folder, removed afterwards."""
    parts = out.with_name(f"{out.stem}.parts")
    parts.mkdir(exist_ok=True)
    try:
        height = settings.rules.pair.mp4_heights[0]
        made = [
            Panel(clip, label_image(label, height, parts / f"{label.lower()}.png"), start)
            for label, clip, start in panels
        ]
        targets = [out, out.with_suffix(".gif")] if gif else [out]
        for target in targets:
            encode_fitting(settings.ffmpeg, made, target, settings.rules, seconds, slow, env.ffmpeg, env.say)
    finally:
        shutil.rmtree(parts, ignore_errors=True)


# --- the commands ----------------------------------------------------------------------------------------------------


def _disc(given: str | None, root: Path) -> str:
    """The disc for Coney: the one given, else game_dir of coney.local.toml."""
    if given:
        return given
    game = load_config(root).paths.get("game_dir")
    if game is None:
        raise ClipError("no disc: pass --disc or set game_dir in coney.local.toml")
    return str(disc_for_coney(game))


def run_from_flags(
    scenario: str | None,
    level: str | None,
    input_script: Path | None,
    numbers: tuple[int | None, int | None, int | None, int | None],
    root: Path,
    rules: ClipPair,
) -> PairRun:
    """The run of `clips pair`: the named scenario, or the one given by `--level`, `--input-script` and the numbers
    (seconds, start frame, checkpoint, AI seed). A scenario takes no other run flag."""
    seconds, start, checkpoint, seed = numbers
    if scenario is not None:
        if level or input_script or any(number is not None for number in numbers):
            raise ClipError("a scenario names its own run: give either a scenario or --level and --input-script")
        return load_scenario(scenario_path(scenario, root, rules), root)
    if not level or input_script is None:
        raise ClipError("name a scenario (`clips scenarios` lists them), or give --level and --input-script")
    if not input_script.is_file():
        raise ClipError(f"{input_script}: no such input script")
    return PairRun(
        "custom", level, input_script.resolve(), seconds or 10, start or 0, checkpoint, seed, True, "from the flags"
    )


def run_pair_command(
    run: PairRun,
    agent: str,
    *,
    disc: str | None,
    original: Path | None,
    gif: bool,
    slow: float,
    prefix: str,
    base_ref: str | None,
    task: str | None,
    cwd: Path | None = None,
) -> int:
    """`clips pair` from the command line: the checkout around `cwd`, its task's clips folder, the conventions."""
    from coney_tools import unit

    root = find_repo_root(cwd or Path.cwd())
    conventions = load_conventions(root)
    settings = PairSettings(
        rules=conventions.workspace.clips,
        worktrees=conventions.workspace.worktrees,
        busy_file=conventions.workspace.busy_marker,
        scratch=clips.scratch_root(root),
        main=main_checkout(root),
        ffmpeg=clips.find_tool("ffmpeg", root),
    )
    base = base_ref or f"origin/{conventions.repo.default_branch}"
    return run_pair(
        root,
        unit.task_name(root, task),
        run,
        agent,
        _disc(disc, root),
        settings,
        original=original.resolve() if original else None,
        gif=gif,
        slow=slow,
        prefix=prefix,
        base_ref=base,
    )


def encode_target(clip: Path, out: Path) -> Path:
    """Where `clips encode` writes: a bare `--out` name (no folder) beside the clip it encodes, as the usage
    `--out NAME.gif` reads; a path with a folder as given. A bare name is never taken from the current folder, which
    is usually the checkout clips may not be written in."""
    return clip.parent / out.name if out.parent == Path() else out


def run_encode(
    clip: Path,
    out: Path,
    *,
    start_frame: int,
    seconds: float | None,
    slow: float,
    label: str | None,
    cwd: Path | None = None,
) -> int:
    """`clips encode`: re-encode one clip into `out` for GitHub (a GIF for a `.gif`, else a silent MP4), from
    `start_frame`, `seconds` long (default: the rest of the clip), slowed `slow` times, with an optional label."""
    root = find_repo_root(cwd or Path.cwd())
    rules = clips.clip_rules(root)
    scratch = clips.scratch_root(root)
    source = clips.check_clip_path(clip, scratch, rules)
    target = clips.check_clip_path(encode_target(source, out), scratch, rules)
    if slow <= 0:
        raise ClipError(f"--slow must be more than 0, not {slow:g}")
    if source == target:
        raise ClipError("--out must name another file than the clip")
    info = clips.probe(source, clips.find_tool("ffprobe", root))
    available = info.seconds - start_frame / FPS if seconds is None else seconds
    if available <= 0:
        raise ClipError(f"{source.name} is {info.seconds:.2f} s long: nothing is left after frame {start_frame}")
    shown, cut = shown_seconds(available, slow, rules.max_seconds)
    if cut:
        print(cut)
    parts = target.with_name(f"{target.stem}.parts")
    parts.mkdir(exist_ok=True)
    try:
        picture = label_image(label, rules.pair.mp4_heights[0], parts / "label.png") if label else None
        panel = Panel(source, picture, start_frame)
        encode_fitting(clips.find_tool("ffmpeg", root), [panel], target, rules, shown, slow)
    finally:
        shutil.rmtree(parts, ignore_errors=True)
    return 0


def run_scenarios(cwd: Path | None = None) -> int:
    """`clips scenarios`: list the named scenarios with their runs."""
    root = find_repo_root(cwd or Path.cwd())
    rules = load_conventions(root).workspace.clips
    for run in list_scenarios(root, rules.pair):
        start = f" from frame {run.start_frame}" if run.start_frame else ""
        print(f"{run.name}: {run.level}{start}, {run.seconds} s: {run.description}")
    return 0
