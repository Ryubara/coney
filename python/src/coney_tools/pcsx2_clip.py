# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools pcsx2 clip`: a short clip of the original, from PCSX2's own video capture of its GS output.

PCSX2 encodes the frames its GS renders (and the sound its SPU2 mixes) with its own FFmpeg, from inside the
emulator; nothing on the desktop is read. For the run, the claimed copy's ini switches that capture on, sends it to
a staging folder beside the clip, turns every on-screen display element off (so neither its recording indicator nor
a message can be drawn into the GS output) and binds the capture's toggle to a key, which is posted to PCSX2's
window by handle (never focusing it). The ini is put back when PCSX2 closes. The capture is then re-encoded into the
clip with `clips.encode_arguments` (the torn first frames dropped, cut to the cap, 960 x 720, no metadata) and the
staging file deleted.

Nothing here calls a screen or window capture: no PrintWindow, no FFmpeg capture device (`clips.run_ffmpeg` refuses
them), which tests/test_clips.py checks.
"""

from __future__ import annotations

import os
import shutil
import time
from pathlib import Path

from coney_tools import clips, pcsx2_claims, pcsx2_cli, pcsx2_proc
from coney_tools.config import ConfigError
from coney_tools.pine import PineError

#: The key PCSX2's "Toggle Video Capture" hotkey is bound to for the run (unbound in the copies' own inis).
TOGGLE_KEY = "f12"
#: Seconds dropped from the start of PCSX2's capture. Its first frames are torn (a black band along the bottom, seen
#: in frames 0 to 2 on PCSX2 2.9.94 as the capture starts), so the capture runs this much longer and the clip starts
#: after it, still lasting the seconds asked for.
CAPTURE_START_TRIM = 0.2
#: How long to wait for PCSX2 to finish writing the capture after it is stopped.
FINISH_TIMEOUT = 20.0

#: The settings of the copy's ini for a clip, as (section, key, value); `[Folders]` `Videos` is added per run.
CAPTURE_SETTINGS: tuple[tuple[str, str, str], ...] = (
    ("EmuCore/GS", "EnableVideoCapture", "true"),
    ("EmuCore/GS", "EnableAudioCapture", "true"),
    ("EmuCore/GS", "CaptureContainer", "mp4"),
    ("EmuCore/GS", "VideoCaptureAutoResolution", "true"),
    ("EmuCore/GS", "VideoCaptureBitrate", "20000"),
    ("EmuCore/GS", "EnableVideoCaptureParameters", "false"),
    ("EmuCore/GS", "EnableAudioCaptureParameters", "false"),
    ("EmuCore/GS", "OrganizeVideoCaptureByGame", "false"),
    # The on-screen display: nothing of it may reach a frame.
    ("EmuCore/GS", "OsdShowVideoCapture", "false"),
    ("EmuCore/GS", "OsdShowInputRec", "false"),
    ("EmuCore/GS", "OsdShowIndicators", "false"),
    ("EmuCore/GS", "OsdShowSpeed", "false"),
    ("EmuCore/GS", "OsdShowFPS", "false"),
    ("EmuCore/GS", "OsdShowVPS", "false"),
    ("EmuCore/GS", "OsdShowCPU", "false"),
    ("EmuCore/GS", "OsdShowGPU", "false"),
    ("EmuCore/GS", "OsdShowResolution", "false"),
    ("EmuCore/GS", "OsdShowGSStats", "false"),
    ("EmuCore/GS", "OsdShowSettings", "false"),
    ("EmuCore/GS", "OsdShowInputs", "false"),
    ("EmuCore/GS", "OsdShowFrameTimes", "false"),
    ("EmuCore/GS", "OsdShowVersion", "false"),
    ("EmuCore/GS", "OsdShowHardwareInfo", "false"),
    ("EmuCore/GS", "OsdMessagesPos", "0"),
    ("EmuCore/GS", "OsdPerformancePos", "0"),
    ("Hotkeys", "ToggleVideoCapture", f"Keyboard/{TOGGLE_KEY.upper()}"),
)


def capture_settings(staging: Path) -> list[tuple[str, str, str]]:
    """CAPTURE_SETTINGS with PCSX2's video folder set to `staging`."""
    return [*CAPTURE_SETTINGS, ("Folders", "Videos", str(staging))]


def _toggle(pid: int) -> None:
    """Press the capture's toggle key in PCSX2's window, by handle."""
    targets = [window.hwnd for window in pcsx2_proc.key_targets(pid)]
    pcsx2_proc.post_keys(targets, pcsx2_proc.parse_chords([TOGGLE_KEY]), 100, 100)


def _finished_capture(staging: Path, timeout: float) -> Path:
    """The capture PCSX2 wrote into `staging`, once its size has stopped changing. Raises ClipError when none
    appears (the key did not reach PCSX2, or its FFmpeg could not start)."""
    deadline = time.monotonic() + timeout
    last: tuple[Path, int] | None = None
    while time.monotonic() < deadline:
        files = sorted(path for path in staging.iterdir() if path.is_file())
        if len(files) == 1:
            size = files[0].stat().st_size
            if last is not None and last == (files[0], size) and size > 0:
                return files[0]
            last = (files[0], size)
        time.sleep(1.0)
    raise clips.ClipError(
        f"PCSX2 wrote no finished capture into {staging}: the {TOGGLE_KEY} key may not have reached it, or its video "
        "capture could not start (see its log)"
    )


def run_clip(
    state: Path,
    seconds: int,
    out: Path,
    flags: tuple[Path | None, Path | None, Path | None],
    agent: str | None,
) -> int:
    """`pcsx2 clip`: start PCSX2 on `state` under `agent`'s claim, capture `seconds` of its GS output and sound with
    PCSX2's own capture, and write the clip to `out` (checked as clips.check_clip_path says). The claim is released
    afterwards when this call made it."""
    rules = clips.clip_rules()
    if not 1 <= seconds <= rules.max_seconds:
        raise clips.ClipError(f"--seconds must be 1 to {rules.max_seconds}")
    target = clips.check_clip_path(out, clips.scratch_root(), rules)
    ffmpeg = clips.find_tool("ffmpeg")
    registry = pcsx2_claims.default_registry()
    claim, created = registry.claim_for_run(agent, flags[0], os.getpid())
    # The staging folder sits in the clips folder, so `clips clean` removes whatever a failed run leaves.
    staging = target.parent / f"{target.stem}.pcsx2-capture"
    shutil.rmtree(staging, ignore_errors=True)
    staging.mkdir()
    try:
        capture = _capture(state, seconds, staging, flags, registry, claim)
        clips.run_ffmpeg(clips.encode_arguments(ffmpeg, capture, target, seconds, CAPTURE_START_TRIM))
    finally:
        shutil.rmtree(staging, ignore_errors=True)
        if created:
            registry.release(claim.agent, claim.copy)
    info = clips.probe(target, clips.find_tool("ffprobe"))
    print(clips.describe(target, info, rules))
    problems = clips.clip_problems(info, rules)
    if problems:
        target.unlink(missing_ok=True)
        raise clips.ClipError(f"{target}: deleted; " + "; ".join(problems))
    return 0


def _capture(
    state: Path,
    seconds: int,
    staging: Path,
    flags: tuple[Path | None, Path | None, Path | None],
    registry: pcsx2_claims.Registry,
    claim: pcsx2_claims.Claim,
) -> Path:
    """Run PCSX2 on `state` with its capture going to `staging` for `seconds`; the capture file."""
    emulator: pcsx2_cli.Emulator | None = None
    try:
        paths = pcsx2_cli.resolve_paths(Path(claim.path), flags[1], flags[2])
        emulator = pcsx2_cli.Emulator(
            paths,
            state,
            [],
            on_start=lambda pid: registry.record_pid(claim, pid),
            ini=capture_settings(staging),
        )
        _toggle(emulator.pid)
        # The dropped start and a little past the clip's length: the encode cuts it to exactly `seconds`.
        time.sleep(CAPTURE_START_TRIM + seconds + 0.5)
        _toggle(emulator.pid)
        return _finished_capture(staging, FINISH_TIMEOUT)
    except PineError as error:
        raise ConfigError(str(error)) from error
    finally:
        if emulator is not None:
            emulator.close()
