# SPDX-License-Identifier: GPL-3.0-or-later
"""In-game video clips for issues and pull requests: where they may live, what they may hold, and their upload.

A clip shows the game's own picture only: Coney's frames, read back from its own frame buffer (`coney --record`), or
the original's, from PCSX2's own capture of its GS output (`coney-tools clips record`). Nothing in the tools grabs the
screen or a window: `run_ffmpeg`, the one way the tools start FFmpeg, refuses any capture device as an input
(`refuse_screen_capture`). The rules (docs/guides/testing.md#clips), each enforced here:

* a clip lives only in `<scratch>/<task>/clips/` and never inside a git checkout (`check_clip_path`);
* it is at most `workspace.clips.max_seconds` long and carries no metadata (`clip_problems`, from ffprobe);
* it is uploaded only by `clips publish`, which deletes it once gh reports the upload done, and `clips clean --apply`
  deletes what is left after `workspace.clips.keep_hours` (without `--apply` it only lists it).

The limits are keys of `.github/conventions.yml` (`workspace.clips`), never constants here.
"""

from __future__ import annotations

import itertools
import json
import os
import re
import shutil
import subprocess
import time
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from pathlib import Path

from coney_tools.config import (
    ConfigError,
    find_repo_root,
    load_config,
    local_config_file,
    workspace_folder,
)
from coney_tools.conventions import Clips, load_conventions

#: A clip's file name: letters, digits, '.', '_' and '-', ending in .mp4 (no path, no user or machine name).
CLIP_NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,79}\.mp4$")
#: A task's folder name in the scratch area (a branch or track name).
TASK_NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,79}$")

#: FFmpeg input formats that read a screen, a window, a camera or a generated source: never an input of ours.
SCREEN_CAPTURE_FORMATS = frozenset(
    {
        "gdigrab",
        "ddagrab",
        "dshow",
        "vfwcap",
        "x11grab",
        "xcbgrab",
        "kmsgrab",
        "fbdev",
        "pipewiregrab",
        "avfoundation",
        "v4l2",
        "video4linux2",
        "lavfi",
    }
)

#: The output settings every clip is encoded with: H.264 in yuv420p at 30 frames a second, CRF 26 (preset slow) and
#: AAC at 96 kbit/s, the stream's encoder notes (SEI units) removed, no metadata or chapters, bit-exact headers (no
#: version strings) and the index at the front so the clip plays while it downloads. Coney's own encoder settings
#: (src/core/clip_recording.cpp) are the same.
ENCODE_SETTINGS: tuple[str, ...] = (
    "-c:v",
    "libx264",
    "-preset",
    "slow",
    "-crf",
    "26",
    "-r",
    "30",
    "-bsf:v",
    "filter_units=remove_types=6",
    "-c:a",
    "aac",
    "-b:a",
    "96k",
    "-map_metadata",
    "-1",
    "-map_chapters",
    "-1",
    "-fflags",
    "+bitexact",
    "-flags:v",
    "+bitexact",
    "-flags:a",
    "+bitexact",
    "-movflags",
    "+faststart",
)

#: The clip's picture: 960 x 720, the 4:3 shape a television gives the game's frame.
CLIP_SCALE = "scale=960:720:flags=lanczos,format=yuv420p"

#: Container tags an MP4 always has; nothing else may appear at the file level.
_FORMAT_TAGS = frozenset({"major_brand", "minor_version", "compatible_brands"})
#: Stream tags the MP4 boxes always carry, with the values a clip may show.
_STREAM_TAGS: dict[str, re.Pattern[str]] = {
    "language": re.compile(r"^und$"),
    "handler_name": re.compile(r"^(VideoHandler|SoundHandler)$"),
    "vendor_id": re.compile(r"^\[0\]\[0\]\[0\]\[0\]$"),
    # The sample description's compressor name; with +bitexact it carries no version.
    "encoder": re.compile(r"^Lavc [a-z0-9_]+$"),
}


class ClipError(ConfigError):
    """A clip that breaks a rule, or a step of making or publishing one that failed; the message says which."""


# --- where clips live ------------------------------------------------------------------------------------------------


def clip_rules(cwd: Path | None = None) -> Clips:
    """The `workspace.clips` rules of the checkout around `cwd`."""
    return load_conventions(find_repo_root(cwd or Path.cwd())).workspace.clips


def scratch_root(cwd: Path | None = None) -> Path:
    """The workspace's shared scratch folder: `../../scratch` beside the main checkout (worktrees share it), or the
    one in `<clone>-workspace` beside a plain clone (`config.workspace_root`)."""
    return workspace_folder(find_repo_root(cwd or Path.cwd()), "scratch")


def git_checkout_around(path: Path) -> Path | None:
    """The git checkout (a folder holding `.git`, a main checkout or a worktree) that `path` lies in, or None."""
    resolved = path.resolve()
    for folder in (resolved, *resolved.parents):
        if (folder / ".git").exists():
            return folder
    return None


def check_clip_path(path: Path, scratch: Path, rules: Clips) -> Path:
    """`path`, resolved, when it may hold a clip: `<scratch>/<task>/<rules.folder>/<name>.mp4`, outside every git
    checkout, its name plain (CLIP_NAME). Raises ClipError naming the rule it breaks."""
    resolved = path.resolve()
    checkout = git_checkout_around(resolved)
    if checkout is not None:
        raise ClipError(f"{path}: is inside the git checkout {checkout}; clips stay in the scratch folder")
    if not CLIP_NAME.match(resolved.name):
        raise ClipError(f"{path}: a clip's name is letters, digits, '.', '_' and '-' ending in .mp4")
    folder = resolved.parent
    if folder.name != rules.folder or folder.parent.parent != scratch.resolve():
        raise ClipError(f"{path}: clips live only in {scratch / '<task>' / rules.folder}")
    if not TASK_NAME.match(folder.parent.name):
        raise ClipError(f"{path}: the task folder's name is letters, digits, '.', '_' and '-'")
    return resolved


def clip_folder(task: str, scratch: Path, rules: Clips) -> Path:
    """The clips folder of `task` (`<scratch>/<task>/clips/`), made if missing. Raises ClipError for a bad name."""
    if not TASK_NAME.match(task):
        raise ClipError(f"{task!r}: a task's name is letters, digits, '.', '_' and '-'")
    folder = scratch / task / rules.folder
    if git_checkout_around(folder) is not None:
        raise ClipError(f"{folder}: the scratch folder is inside a git checkout")
    folder.mkdir(parents=True, exist_ok=True)
    return folder


# --- FFmpeg ----------------------------------------------------------------------------------------------------------


def find_tool(name: str, cwd: Path | None = None) -> Path:
    """`ffmpeg` or `ffprobe`: in coney.local.toml's `ffmpeg_dir`, else beside `CONEY_FFMPEG` (Coney's own setting),
    else on PATH. Raises ClipError when it is nowhere."""
    exe = name + (".exe" if os.name == "nt" else "")
    candidates: list[Path] = []
    try:
        root = find_repo_root(cwd or Path.cwd())
        if local_config_file(root) is not None:
            folder = load_config(root).paths.get("ffmpeg_dir")
            if folder is not None:
                candidates.append(folder / exe)
    except ConfigError:
        pass
    if os.environ.get("CONEY_FFMPEG"):
        candidates.append(Path(os.environ["CONEY_FFMPEG"]).parent / exe)
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    found = shutil.which(name)
    if found is None:
        raise ClipError(f"{name} not found: set ffmpeg_dir in coney.local.toml or put FFmpeg on PATH")
    return Path(found)


def refuse_screen_capture(args: Sequence[str]) -> None:
    """Raise ClipError unless every input of the FFmpeg command `args` is a file or standard input: no capture
    device (SCREEN_CAPTURE_FORMATS), no generated source, no URL. This is what keeps a clip to the game's own image."""
    for flag, value in itertools.pairwise(args):
        if flag == "-f" and value.lower() in SCREEN_CAPTURE_FORMATS:
            raise ClipError(f"refused: '-f {value}' captures a device or the screen; clips hold the game's image only")
        if flag == "-i":
            if value in ("pipe:0", "-"):
                continue
            if not Path(value).is_file():
                raise ClipError(f"refused: input {value!r} is not a file")


#: Runs a command and returns its exit code and output; tests replace it.
Runner = Callable[[Sequence[str]], subprocess.CompletedProcess[str]]


def _run(args: Sequence[str]) -> subprocess.CompletedProcess[str]:
    """Run `args`, capturing its text output."""
    return subprocess.run(list(args), capture_output=True, text=True, check=False)


def run_ffmpeg(args: Sequence[str], runner: Runner = _run) -> None:
    """Run FFmpeg with `args` (the executable first) after refuse_screen_capture(); raise ClipError on failure."""
    refuse_screen_capture(args)
    done = runner(args)
    if done.returncode != 0:
        raise ClipError(f"ffmpeg failed (exit {done.returncode}): {done.stderr.strip()[-400:]}")


def encode_arguments(ffmpeg: Path, source: Path, out: Path, max_seconds: int, skip_seconds: float = 0.0) -> list[str]:
    """FFmpeg's arguments to turn a recording file `source` into a clip at `out`: the first `max_seconds` after the
    first `skip_seconds` of it are dropped, scaled to 960 x 720, encoded with ENCODE_SETTINGS (its sound too, when it
    has any)."""
    skip = ["-ss", str(skip_seconds)] if skip_seconds > 0 else []
    return [
        str(ffmpeg),
        "-hide_banner",
        "-loglevel",
        "error",
        "-nostdin",
        "-y",
        *skip,
        "-i",
        str(source),
        "-t",
        str(max_seconds),
        "-map",
        "0:v:0",
        "-map",
        "0:a:0?",
        "-vf",
        CLIP_SCALE,
        *ENCODE_SETTINGS,
        str(out),
    ]


# --- what a clip holds -----------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class ClipInfo:
    """What ffprobe says of a clip."""

    seconds: float
    width: int
    height: int
    size: int
    audio: bool
    #: Every tag: `format:key=value` and `stream N:key=value`.
    tags: tuple[str, ...]


def parse_probe(text: str) -> ClipInfo:
    """A ClipInfo from ffprobe's JSON (`-show_format -show_streams -of json`). Raises ClipError when it is not."""
    try:
        data = json.loads(text)
        fmt = data["format"]
        streams = data["streams"]
    except (ValueError, KeyError, TypeError) as error:
        raise ClipError(f"ffprobe gave no usable answer ({error})") from error
    tags = [f"format:{k}={v}" for k, v in (fmt.get("tags") or {}).items()]
    video = next((s for s in streams if s.get("codec_type") == "video"), None)
    for index, stream in enumerate(streams):
        tags += [f"stream {index}:{k}={v}" for k, v in (stream.get("tags") or {}).items()]
    return ClipInfo(
        seconds=float(fmt.get("duration", 0.0)),
        width=int(video.get("width", 0)) if video else 0,
        height=int(video.get("height", 0)) if video else 0,
        size=int(fmt.get("size", 0)),
        audio=any(s.get("codec_type") == "audio" for s in streams),
        tags=tuple(tags),
    )


def probe(path: Path, ffprobe: Path, runner: Runner = _run) -> ClipInfo:
    """ffprobe's account of the clip at `path`."""
    done = runner([str(ffprobe), "-v", "error", "-show_format", "-show_streams", "-of", "json", str(path)])
    if done.returncode != 0:
        raise ClipError(f"{path}: ffprobe could not read it ({done.stderr.strip()[-200:]})")
    return parse_probe(done.stdout)


def metadata_problems(info: ClipInfo) -> list[str]:
    """Every tag beyond the fixed ones an MP4's boxes carry: a title, a comment, an encoder version, a path..."""
    problems = []
    for tag in info.tags:
        where, _, pair = tag.partition(":")
        key, _, value = pair.partition("=")
        allowed = key in _FORMAT_TAGS if where == "format" else key in _STREAM_TAGS and _STREAM_TAGS[key].match(value)
        if not allowed:
            problems.append(f"metadata {tag!r}")
    return problems


def clip_problems(info: ClipInfo, rules: Clips) -> list[str]:
    """What breaks the rules: too long, or metadata (metadata_problems)."""
    problems = metadata_problems(info)
    # One frame of slack: a 10 s clip at 30 frames a second may report 10.033 s.
    if info.seconds > rules.max_seconds + 0.05:
        problems.append(f"{info.seconds:.2f} s long, more than {rules.max_seconds} s")
    return problems


def describe(path: Path, info: ClipInfo, rules: Clips) -> str:
    """One line on a clip: its name, length, picture, sound and size, with a warning past `rules.warn_mb`."""
    megabytes = info.size / 1_000_000
    line = (
        f"{path.name}: {info.seconds:.2f} s, {info.width}x{info.height}, "
        f"{'with' if info.audio else 'no'} sound, {megabytes:.2f} MB"
    )
    if megabytes > rules.warn_mb:
        line += f" (warning: more than {rules.warn_mb} MB; record fewer seconds)"
    return line


def verify(paths: Sequence[Path], scratch: Path, rules: Clips, ffprobe: Path, runner: Runner = _run) -> list[Path]:
    """The clips at `paths`, resolved, once each passes check_clip_path and clip_problems; prints a line on each.
    Raises ClipError listing every problem found."""
    problems: list[str] = []
    resolved: list[Path] = []
    for path in paths:
        try:
            clip = check_clip_path(path, scratch, rules)
            if not clip.is_file():
                raise ClipError(f"{path}: no such file")
            info = probe(clip, ffprobe, runner)
        except ClipError as error:
            problems.append(str(error))
            continue
        problems += [f"{path}: {problem}" for problem in clip_problems(info, rules)]
        print(describe(clip, info, rules))
        resolved.append(clip)
    if problems:
        raise ClipError("; ".join(problems))
    return resolved


# --- publishing and cleaning -----------------------------------------------------------------------------------------


@dataclass(frozen=True)
class Target:
    """Where `clips publish` posts: a pull request or an issue, as a comment or into its description."""

    kind: str  # "pr" or "issue"
    number: int
    edit: bool = False


def gh_arguments(target: Target, body: str | None, body_file: Path | None, attach: Sequence[str]) -> list[str]:
    """gh's command for `target`: `gh pr|issue comment|edit N` with the body and one `--attach` per clip."""
    args = ["gh", target.kind, "edit" if target.edit else "comment", str(target.number)]
    if body_file is not None:
        args += ["--body-file", str(body_file)]
    elif body is not None:
        args += ["--body", body]
    for clip in attach:
        args += ["--attach", clip]
    return args


def run_publish(
    target: Target,
    body: str | None,
    body_file: Path | None,
    clips: Sequence[Path],
    runner: Runner = _run,
    cwd: Path | None = None,
) -> int:
    """`clips publish`: check every clip, upload them with gh in one comment (or description edit), then delete them
    once gh reports success. On any failure every clip is kept and the command says so (exit 1)."""
    if not clips:
        raise ClipError("name at least one clip")
    if not target.edit and body is None and body_file is None:
        raise ClipError("a comment needs --body or --body-file (gh would otherwise ask for one)")
    rules = clip_rules(cwd)
    verified = verify(clips, scratch_root(cwd), rules, find_tool("ffprobe", cwd), runner)
    # gh rewrites a body's `./clips/x.mp4` reference only when --attach names it the same way: pass them as given.
    done = runner(gh_arguments(target, body, body_file, [str(path) for path in clips]))
    if done.returncode != 0:
        print(done.stderr.strip())
        print(f"gh failed (exit {done.returncode}); kept the {len(verified)} clip(s): " + ", ".join(map(str, clips)))
        return 1
    if done.stdout.strip():
        print(done.stdout.strip())
    for clip in verified:
        clip.unlink(missing_ok=True)
    print(f"uploaded {len(verified)} clip(s) to {target.kind} #{target.number} and deleted them")
    return 0


def run_check(clips: Sequence[Path], runner: Runner = _run, cwd: Path | None = None) -> int:
    """`clips check`: the checks `clips publish` makes, without uploading anything."""
    rules = clip_rules(cwd)
    verify(clips, scratch_root(cwd), rules, find_tool("ffprobe", cwd), runner)
    return 0


def stale_clips(scratch: Path, rules: Clips, older_than_hours: float, now: float) -> list[Path]:
    """Every file in a `<scratch>/<task>/<rules.folder>/` folder (at any depth) last changed more than
    `older_than_hours` ago."""
    cutoff = now - older_than_hours * 3600
    found = []
    for folder in sorted(scratch.glob(f"*/{rules.folder}")):
        if not folder.is_dir():
            continue
        for path in sorted(folder.rglob("*")):
            if path.is_file() and path.stat().st_mtime < cutoff:
                found.append(path)
    return found


def run_clean(older_than_hours: float | None, dry_run: bool, cwd: Path | None = None, now: float | None = None) -> int:
    """`clips clean`: list the clips (and leftovers of a recording) older than `older_than_hours` (default
    `workspace.clips.keep_hours`) in every task's clips folder in the scratch area, and delete them unless `dry_run`."""
    rules = clip_rules(cwd)
    hours = rules.keep_hours if older_than_hours is None else older_than_hours
    scratch = scratch_root(cwd)
    found = stale_clips(scratch, rules, hours, time.time() if now is None else now)
    size = 0
    for path in found:
        size += path.stat().st_size
        if not dry_run:
            path.unlink(missing_ok=True)
    for folder in sorted({path.parent for path in found}, key=lambda p: len(p.parts), reverse=True):
        if not dry_run and folder.name != rules.folder and not any(folder.iterdir()):
            folder.rmdir()  # a recording's staging folder, now empty
    verb = "would delete" if dry_run else "deleted"
    print(f"{verb} {len(found)} file(s), {size / 1_000_000:.1f} MB, older than {hours:g} h in {scratch}")
    return 0


def run_folder(task: str, cwd: Path | None = None) -> int:
    """`clips folder`: make and print the clips folder of `task`."""
    print(clip_folder(task, scratch_root(cwd), clip_rules(cwd)))
    return 0
