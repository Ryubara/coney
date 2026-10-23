# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools pcsx2 ...` commands: patched state copies, launching PCSX2 on one, and recording a scenario.

Paths come from flags or from `coney.local.toml` (`pcsx2_dir`, `game_dir`, `scratch_dir`); nothing here knows a
machine's own folders. Everything the commands write (state copies, the disc link, traces) is game data or measured
from it, so it goes to the scratch folder or a path the user gives outside the repository
(docs/guides/research-traces.md#recording-a-trace).
"""

from __future__ import annotations

import os
import re
import shutil
import sys
import time
from collections.abc import Callable, Sequence
from dataclasses import dataclass, replace
from pathlib import Path

from coney_tools import hooks as hook_tools
from coney_tools import pcsx2_claims, pcsx2_proc, pcsx2_state, run_records_cli
from coney_tools.config import ConfigError, find_repo_root, load_config, local_config_file
from coney_tools.debug_server import DebugClient, DebugServerError, P2m2Header, read_p2m2, savestate_of
from coney_tools.game_memory import GAME_TIME_OFFSET, GAME_TIMER_POINTER, GameMemory
from coney_tools.hooks import Hook, HookError
from coney_tools.pcsx2_state import Patch, StateError
from coney_tools.pine import PineClient, PineError, Read
from coney_tools.recorder import Recorder, RecordError, Recording
from coney_tools.run_records import RunRecord
from coney_tools.scenario import PATCHES_DIR, Scenario, ScenarioError, load_scenario, with_let
from coney_tools.wad import refuse_inside_repo

#: Seconds to wait for PCSX2 to boot, load the state and run the game.
LAUNCH_TIMEOUT = 60.0
#: A disc path PCSX2 2.9.94 opens; others (commas, parentheses) need the hard link.
PLAIN_PATH = re.compile(r"^[A-Za-z0-9 ._\\/:-]+$")


@dataclass(frozen=True)
class Paths:
    """The machine's folders for a PCSX2 run."""

    pcsx2_dir: Path
    iso: Path
    scratch: Path


def _config_paths() -> dict[str, Path | None]:
    """coney.local.toml's paths, or all unset when there is no file (flags may give everything)."""
    root = find_repo_root(Path.cwd())
    try:
        return load_config(root).paths
    except ConfigError:
        if local_config_file(root) is not None:
            raise
        return {}


def _find_iso(game: Path) -> Path:
    """The disc image: `game` itself, or the one .iso in the folder `game`."""
    if game.is_file():
        return game
    images = sorted(game.glob("*.iso"))
    if len(images) != 1:
        raise ConfigError(f"{game}: expected one .iso in it, found {len(images)}; pass --iso")
    return images[0]


def resolve_paths(pcsx2_dir: Path | None, iso: Path | None, scratch: Path | None) -> Paths:
    """The folders from the flags, else from coney.local.toml. Raises ConfigError naming what is missing."""
    config = _config_paths()
    pcsx2 = pcsx2_dir or config.get("pcsx2_dir")
    game = iso or config.get("game_dir")
    work = scratch or config.get("scratch_dir")
    if pcsx2 is None or not pcsx2.is_dir():
        raise ConfigError("no PCSX2 folder: pass --pcsx2-dir or set pcsx2_dir in coney.local.toml")
    if game is None or not game.exists():
        raise ConfigError("no disc image: pass --iso or set game_dir in coney.local.toml")
    if work is None:
        raise ConfigError("no scratch folder: pass --scratch or set scratch_dir in coney.local.toml")
    return Paths(pcsx2, _find_iso(game), work / "pcsx2")


def pine_port(pcsx2_dir: Path) -> int:
    """The PINE port of PCSX2's ini (`[EmuCore]` `PINESlot`). Raises ConfigError when PINE is not enabled."""
    port = pcsx2_claims.read_pine_port(pcsx2_dir)
    if port is None:
        ini = pcsx2_dir / "inis" / "PCSX2.ini"
        raise ConfigError(f"{ini}: PINE is off; set EnablePINE = true under [EmuCore] (and PINESlot) first")
    return port


def set_ini_value(text: str, section: str, key: str, value: str) -> str:
    """`text`, an ini file, with `key = value` in `[section]`: the line replaced where it is, else added after the
    section's last setting (a new section at the end when there is none)."""
    lines = text.splitlines()
    header = f"[{section}]"
    start = next((i for i, line in enumerate(lines) if line.strip() == header), None)
    if start is None:
        return text.rstrip("\n") + f"\n\n{header}\n{key} = {value}\n"
    end = next((i for i in range(start + 1, len(lines)) if lines[i].lstrip().startswith("[")), len(lines))
    for i in range(start + 1, end):
        if lines[i].split("=", 1)[0].strip() == key:
            lines[i] = f"{key} = {value}"
            break
    else:
        # After the section's last setting, before the blank lines that end it.
        at = end
        while at > start + 1 and not lines[at - 1].strip():
            at -= 1
        lines.insert(at, f"{key} = {value}")
    return "\n".join(lines) + "\n"


def disc_link(iso: Path, scratch: Path) -> Path:
    """A path PCSX2 opens for the disc: the image itself when its path is plain, else a hard link to it in the
    scratch folder (PCSX2 2.9.94 refused a path with commas and parentheses; a link copies nothing)."""
    if PLAIN_PATH.match(str(iso)):
        return iso
    link = scratch / "warriors.iso"
    scratch.mkdir(parents=True, exist_ok=True)
    if link.exists() and link.samefile(iso):
        return link
    link.unlink(missing_ok=True)
    try:
        os.link(iso, link)
    except OSError as error:
        raise ConfigError(
            f"cannot hard-link {iso} to {link} ({error}); keep the scratch folder on the disc's drive"
        ) from error
    return link


def _executable(pcsx2_dir: Path) -> Path:
    """PCSX2's program in its folder."""
    for name in ("pcsx2-qt.exe", "pcsx2-qt", "pcsx2.AppImage", "PCSX2.AppImage"):
        if (pcsx2_dir / name).is_file():
            return pcsx2_dir / name
    raise ConfigError(f"{pcsx2_dir}: no pcsx2-qt executable in it")


_port_open = pcsx2_claims.port_open


def _slot_states(pcsx2_dir: Path) -> set[Path]:
    """The state files in PCSX2's sstates/ folder now."""
    folder = pcsx2_dir / "sstates"
    return set(folder.glob("*.p2s")) if folder.is_dir() else set()


def _wait_for_game(port: int, patches: list[Patch], deadline: float) -> PineClient:
    """Connect over PINE once PCSX2 answers, the state's patches are in memory and the game time runs."""
    while time.monotonic() < deadline:
        if not _port_open(port):
            time.sleep(0.5)
            continue
        client = PineClient(port)
        try:
            # The first edit of each patch, read back, says the patched state (not a boot) is loaded.
            checks = [p.edits[0] for p in patches]
            reads = [Read(e.address, 4) for e in checks if len(e.data) >= 4]
            times = []
            while time.monotonic() < deadline:
                values = client.batch([Read(GAME_TIMER_POINTER, 4), *reads])
                patched = all(
                    value == int.from_bytes(e.data[:4], "little")
                    for value, e in zip(values[1:], [e for e in checks if len(e.data) >= 4], strict=True)
                )
                if patched and values[0]:
                    times.append(client.batch([Read(values[0] + GAME_TIME_OFFSET, 4)])[0])
                    # Running: the game time has moved on over a few polls.
                    if len(times) >= 3 and times[-1] > times[0]:
                        return client
                time.sleep(0.25)
        except PineError:
            client.close()
            time.sleep(0.5)
            continue
        client.close()
    raise PineError(f"PCSX2 did not run the state within {LAUNCH_TIMEOUT:.0f} s")


def _wait_for_pine(port: int, deadline: float) -> PineClient:
    """Connect over PINE once PCSX2 answers (a plain boot: the game's timer is not running yet)."""
    while time.monotonic() < deadline:
        if _port_open(port):
            return PineClient(port)
        time.sleep(0.5)
    raise PineError(f"PCSX2 did not open PINE within {LAUNCH_TIMEOUT:.0f} s")


def _wait_for_port(port: int, what: str, deadline: float) -> None:
    """Return once something listens on `port`; PineError naming `what` at the deadline."""
    while time.monotonic() < deadline:
        if _port_open(port):
            return
        time.sleep(0.25)
    raise PineError(f"PCSX2 did not open its {what} on port {port} within {LAUNCH_TIMEOUT:.0f} s")


class Emulator:
    """PCSX2 started on a state file (or a plain boot of the disc), never taking the focus; close() ends it and
    warns about new files in its sstates/ folder."""

    def __init__(
        self,
        paths: Paths,
        state: Path | None,
        patches: list[Patch],
        on_start: Callable[[int], None] | None = None,
        speed: float = 1.0,
        ini: Sequence[tuple[str, str, str]] = (),
    ) -> None:
        """Start PCSX2 on `state` and wait until its game runs (no state: boot the disc and wait for PINE); `on_start`
        gets the process id as soon as it exists (the claim records it). `speed` below 1 runs the emulation slower
        (the copy's `[Framerate]` `NominalScalar`); `ini` gives more settings of the copy's ini for this run, each
        (section, key, value). close() puts the ini back as it was. When the copy's ini enables the PCSX2-MCP
        fork's debug server, the start also waits for its port (`debug_port`; None when it is off). Raises
        ConfigError when PCSX2 already runs (one PINE client at a time, and a run needs a fresh start for its
        patches) or PineError when it never answers."""
        self.paths = paths
        self.port = pine_port(paths.pcsx2_dir)
        self.debug_port = pcsx2_claims.read_debug_port(paths.pcsx2_dir)
        if _port_open(self.port):
            raise ConfigError(f"something already serves PINE on port {self.port}; close PCSX2 first (or --attach)")
        if self.debug_port is not None and _port_open(self.debug_port):
            raise ConfigError(f"something already serves port {self.debug_port} (this copy's debug server); close it")
        self.before = _slot_states(paths.pcsx2_dir)
        self.ini = paths.pcsx2_dir / "inis" / "PCSX2.ini"
        self.ini_text: str | None = None
        settings = list(ini) + ([("Framerate", "NominalScalar", f"{speed:g}")] if speed != 1.0 else [])
        if settings:
            # The copy is ours under the claim; its ini is put back as it was when PCSX2 closes.
            self.ini_text = self.ini.read_text(encoding="utf-8")
            changed = self.ini_text
            for section, key, value in settings:
                changed = set_ini_value(changed, section, key, value)
            self.ini.write_text(changed, encoding="utf-8")
        disc = disc_link(paths.iso, paths.scratch)
        load = ["-statefile", str(state)] if state is not None else []
        command = [str(_executable(paths.pcsx2_dir)), "-fastboot", *load, "--", str(disc)]
        # Never a plain Popen: a child of ours inherits the right to take the user's focus (pcsx2_proc's docstring).
        self.guard = pcsx2_proc.focus_guard()
        # CONEY_WINDOW_DISPLAY: each PCSX2 window goes to that display as it appears (docs/guides/running.md).
        self.placer = pcsx2_proc.window_placer()
        self.pid = pcsx2_proc.start_detached(command)
        if self.guard is not None:
            self.guard.start(self.pid)
        if self.placer is not None:
            self.placer.start(self.pid)
        if on_start is not None:
            on_start(self.pid)
        try:
            deadline = time.monotonic() + LAUNCH_TIMEOUT
            self.client = (
                _wait_for_game(self.port, patches, deadline)
                if state is not None
                else _wait_for_pine(self.port, deadline)
            )
            if self.debug_port is not None:
                # The fork starts its debug server with the game (PINE opens at the same time).
                _wait_for_port(self.debug_port, "debug server", deadline)
        except PineError:
            self.close()
            raise

    def close(self) -> None:
        """End PCSX2 (no shutdown save) and report any state file that appeared in its sstates/ folder."""
        if getattr(self, "client", None) is not None:
            self.client.close()
        if self.guard is not None:
            self.guard.stop()
        if self.placer is not None:
            self.placer.stop()
        if pcsx2_proc.is_pcsx2(self.pid):  # a pid of a PCSX2 that already ended may belong to something else now
            pcsx2_proc.terminate(self.pid)
        if self.ini_text is not None:
            self.ini.write_text(self.ini_text, encoding="utf-8")
            self.ini_text = None
        for path in sorted(_slot_states(self.paths.pcsx2_dir) - self.before):
            print(f"warning: {path} appeared while PCSX2 ran; move it to your scratch folder", file=sys.stderr)


def _source_state(source: str, pcsx2_dir: Path | None) -> Path:
    """A state named on the command line: a file, or `slot:N` for quick-save slot N (read only)."""
    if source.startswith("slot:"):
        if pcsx2_dir is None:
            raise ConfigError("slot:N needs the PCSX2 folder: pass --pcsx2-dir or set pcsx2_dir")
        path = pcsx2_state.slot_path(pcsx2_dir, int(source[5:]))
    else:
        path = Path(source)
    if not path.is_file():
        raise ConfigError(f"{path}: no such state file")
    return path


def _scenario_state(scenario: Scenario, state: str | None, scratch: Path) -> str:
    """The state a recording copies: `--state` when given, else the scenario's slot or its state file (a path under
    the scratch folder, made by hand as its research page says). Raises ConfigError when there is none."""
    if state is not None:
        return state
    if scenario.slot is not None:
        return f"slot:{scenario.slot}"
    if scenario.state is not None:
        path = scratch / scenario.state
        if not path.is_file():
            raise ConfigError(
                f"{scenario.path}: its state {path} does not exist; make it as the scenario's research page says, "
                "or pass --state"
            )
        return str(path)
    raise ConfigError(f"{scenario.path}: names no slot or state; pass --state")


def _patches(names: list[str]) -> tuple[list[Patch], list[Hook]]:
    """The named patches and hooks of research/traces/patches/: the patches, with one more installing the hooks
    (ids in the order named), and the hooks."""
    root = find_repo_root(Path.cwd())
    try:
        known = pcsx2_state.load_patches(root / PATCHES_DIR)
        hooks = hook_tools.hooks_for(names, hook_tools.load_hooks(root / PATCHES_DIR), set(known))
        patches = pcsx2_state.pick(known, [name for name in names if name in known])
        return [*patches, hook_tools.build(hooks)] if hooks else patches, hooks
    except HookError as error:
        raise StateError(str(error)) from error


def _make_copy(source: Path, out: Path, patches: list[Patch], pcsx2_dir: Path | None) -> int:
    """Write the patched copy, refusing the repository and PCSX2's sstates/."""
    refuse_inside_repo(out)
    forbidden = [pcsx2_dir / "sstates"] if pcsx2_dir is not None else []
    return pcsx2_state.prepare(source, out, patches, forbidden)


def run_prepare_state(source: str, out: Path, patch_names: list[str], pcsx2_dir: Path | None) -> int:
    """`pcsx2 prepare-state`: copy a state with patches applied."""
    try:
        pcsx2 = pcsx2_dir or _config_paths().get("pcsx2_dir")
        state = _source_state(source, pcsx2)
        patches, _ = _patches(patch_names)
        count = _make_copy(state, out, patches, pcsx2)
    except StateError as error:
        raise ConfigError(str(error)) from error
    print(f"{out}: {count} edit(s) from {', '.join(patch_names) or 'no patches'}")
    return 0


def run_repack_state(source: str, out: Path, pcsx2_dir: Path | None) -> int:
    """`pcsx2 repack-state`: copy a state with plain deflate in place of zstd."""
    refuse_inside_repo(out)
    try:
        pcsx2 = pcsx2_dir or _config_paths().get("pcsx2_dir")
        state = _source_state(source, pcsx2)
        forbidden = [pcsx2 / "sstates"] if pcsx2 is not None else []
        count = pcsx2_state.repack(state, out, forbidden)
    except StateError as error:
        raise ConfigError(str(error)) from error
    print(f"{out}: {count} entries rewritten with deflate ({out.stat().st_size:,} bytes)")
    return 0


#: Where `pcsx2 record --recording` puts the recording it replays, below the copy's folder (the debug server plays
#: only files inside PCSX2's data folder).
REPLAY_FOLDER = Path("recordings") / "coney-record"


def _recording_header(path: Path) -> P2m2Header:
    """The header of the input recording at `path`, or ConfigError."""
    if not path.is_file():
        raise ConfigError(f"{path}: no such input recording")
    try:
        return read_p2m2(path)
    except (OSError, ValueError) as error:
        raise ConfigError(str(error)) from error


def _stage_recording(source: Path, header: P2m2Header, pcsx2_dir: Path, patches: list[Patch]) -> list[Path]:
    """Copy the recording into the copy's REPLAY_FOLDER, its start savestate patched as the scenario says; returns
    the recording's copy and, when it starts from a savestate, the savestate's. Raises ConfigError for a savestate
    recording without its savestate, and for patches on a power-on recording (it loads no state to patch)."""
    folder = pcsx2_dir / REPLAY_FOLDER
    target = folder / (re.sub(r"[^A-Za-z0-9._-]", "_", source.stem) + ".p2m2")
    state = savestate_of(source)
    if header.from_savestate and not state.is_file():
        raise ConfigError(f"{source} starts from a savestate, and {state} is missing")
    if not header.from_savestate and patches:
        raise ConfigError(f"{source} starts from power-on: it loads no state the scenario's patches could go into")
    folder.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)
    if not header.from_savestate:
        return [target]
    if patches:
        _make_copy(state, savestate_of(target), patches, pcsx2_dir)
    else:
        shutil.copyfile(state, savestate_of(target))
    return [target, savestate_of(target)]


def _record_replay(emulator: Emulator, staged: Path, scenario: Scenario, hooks: list[Hook]) -> Recording:
    """Replay the staged recording through the copy's debug server while the Recorder samples the game.

    Emulation is paused before `play`, which loads the recording's savestate (or resets the game), so the Recorder's
    step 0 is the recording's first frame; it resumes right after that sample. A recording shorter than the
    scenario ends with PCSX2 paused at its last frame, and the error says so."""
    if emulator.debug_port is None:
        raise ConfigError(
            f"{emulator.paths.pcsx2_dir}: its debug server is off; --recording needs the PCSX2-MCP fork's "
            "(`coney-tools pcsx2 setup-copies`)"
        )
    with DebugClient(emulator.debug_port) as debug:
        debug.emulation("pause")
        status = debug.input_recording("play", staged.relative_to(emulator.paths.pcsx2_dir).as_posix())
        if status.mode != "replaying":
            raise ConfigError(f"{staged}: PCSX2 did not start replaying it (mode {status.mode})")
        recorder = Recorder(
            GameMemory(emulator.client), scenario, hooks=hooks, started=lambda: debug.emulation("resume")
        )
        try:
            recording = recorder.record()
        except RecordError as error:
            ended = debug.input_recording("status")
            if ended.replay_ended:
                raise ConfigError(
                    f"the recording ended at its frame {ended.total_frames} (PCSX2 paused there) before the "
                    f"scenario's {scenario.updates} updates were recorded; record fewer updates or a longer recording"
                ) from error
            raise
        if debug.input_recording("status").mode == "replaying":
            debug.input_recording("stop")
    return recording


def run_launch(
    state: Path | None, pcsx2_dir: Path | None, iso: Path | None, scratch: Path | None, agent: str | None = None
) -> int:
    """`pcsx2 launch`: start PCSX2 on a state file (None: boot the disc) under `agent`'s claim, wait for its game,
    and leave it running. Without a claim of the agent's on that copy it makes one (kept: `pcsx2 release` ends it)."""
    registry = pcsx2_claims.default_registry()
    claim, created = registry.claim_for_run(agent, pcsx2_dir, None)
    try:
        paths = resolve_paths(Path(claim.path), iso, scratch)
        emulator = Emulator(paths, state, [], on_start=lambda pid: registry.record_pid(claim, pid))
    except (PineError, ConfigError) as error:
        if created:
            registry.release(claim.agent, claim.copy)
        raise ConfigError(str(error)) from error
    emulator.client.close()
    debug = f"debug server on port {emulator.debug_port}" if emulator.debug_port is not None else "no debug server"
    print(
        f"PCSX2 runs {state or 'the disc'} (pid {emulator.pid}) on {claim.copy}; PINE on port {emulator.port}, {debug}"
    )
    print(f"{claim.agent} keeps the claim on {claim.copy}; `coney-tools pcsx2 release --agent {claim.agent}` ends it")
    return 0


def _version_for_record(client: PineClient) -> str:
    """PCSX2's version for a run record, read while the recording's connection is still open. A trace is long to make,
    so a PCSX2 that no longer answers gives `unknown` and a warning rather than losing the trace."""
    try:
        return run_records_cli.pcsx2_version(client)
    except PineError as error:
        print(f"warning: PCSX2's version could not be read for the run record ({error})")
        return "unknown"


def _trace_draft(scenario_path: Path, lets: list[str], request: run_records_cli.RecordRequest) -> RunRecord:
    """The run record a trace is to write, checked before PCSX2 starts (`run_records_cli.precheck`): the scenario, its
    patches and `--let`s as given, with stand-ins for the counts, the CSV's hash and PCSX2's version, which only the run
    can tell, and for the copy, which is claimed only after this check."""
    root = find_repo_root(Path.cwd())
    try:
        scenario = with_let(load_scenario(scenario_path, root), lets)
    except ScenarioError as error:
        raise ConfigError(str(error)) from error
    values, hashes = run_records_cli.TRACE_STAND_IN
    pending = run_records_cli.draft(
        root,
        kind="trace",
        tool="pcsx2 record",
        run_name=request.name or scenario_path.stem,
        copy=run_records_cli.COPY_STAND_IN,
        state=request.state,
        name=scenario_path.stem,
        args=dict(text.partition("=")[::2] for text in lets),
        values=values,
        hashes=hashes,
        patches=scenario.patches,
    )
    run_records_cli.precheck(root, pending)
    return pending


def _write_run_record(pending: RunRecord, out: Path, recording: Recording, copy: str, version: str) -> None:
    """Write the run record of a recorded trace: the counts and the hash of the CSV in place of the stand-ins."""
    values, hashes = run_records_cli.trace_facts(recording, out.read_bytes())
    run_records_cli.save(
        find_repo_root(Path.cwd()),
        replace(pending, copy=copy, pcsx2_version=version, values=values, hashes=hashes),
    )


def run_record(
    scenario_path: Path,
    out: Path,
    state: str | None,
    attach: bool,
    keep_open: bool,
    flags: tuple[Path | None, Path | None, Path | None],
    agent: str | None = None,
    lets: list[str] | None = None,
    recording: Path | None = None,
    record: run_records_cli.RecordRequest | None = None,
) -> int:
    """`pcsx2 record`: play a scenario on the original and write its trace, under `agent`'s claim: the one it holds
    (kept for it afterwards), else one made here and released at the end, also on an error or Ctrl-C (unless
    `keep_open`). `lets` (`NAME=EXPRESSION`) give or replace names of the scenario's `let`. With `recording` (a
    `.p2m2` input recording), the pad comes from replaying it through the debug server, from its own start. With
    `record`, the run also writes its run record (research/runs/) beside the trace's CSV."""
    refuse_inside_repo(out)
    pending = None
    if record is not None:
        pending = _trace_draft(scenario_path, lets or [], record)
    if recording is not None and (attach or state is not None):
        raise ConfigError("--recording starts from the recording's own savestate: drop --attach and --state")
    registry = pcsx2_claims.default_registry()
    claim, created = registry.claim_for_run(agent, flags[0], os.getpid())
    try:
        return _record(
            scenario_path, out, state, attach, keep_open, flags, registry, claim, (lets or [], recording, pending)
        )
    finally:
        if created and not keep_open:
            registry.release(claim.agent, claim.copy)


def _record(
    scenario_path: Path,
    out: Path,
    state: str | None,
    attach: bool,
    keep_open: bool,
    flags: tuple[Path | None, Path | None, Path | None],
    registry: pcsx2_claims.Registry,
    claim: pcsx2_claims.Claim,
    options: tuple[list[str], Path | None, RunRecord | None],
) -> int:
    """The recording of `run_record`, on the copy of `claim`; `options` are its `lets`, the input recording to replay
    and the run record to write (checked before the run, its measured fields still stand-ins)."""
    lets, replay, pending = options
    version = ""
    try:
        scenario = with_let(load_scenario(scenario_path, find_repo_root(Path.cwd())), lets)
    except ScenarioError as error:
        raise ConfigError(str(error)) from error
    emulator: Emulator | None = None
    client: PineClient | None = None
    staged: list[Path] = []
    try:
        patches, hooks = _patches(list(scenario.patches))
        if replay is not None:
            paths = resolve_paths(Path(claim.path), flags[1], flags[2])
            header = _recording_header(replay)
            if scenario.events:
                print("warning: the scenario has input lines; with --recording the pad comes from the recording")
            staged = _stage_recording(replay, header, paths.pcsx2_dir, patches)
            emulator = Emulator(
                paths,
                staged[1] if header.from_savestate else None,
                patches if header.from_savestate else [],
                on_start=lambda pid: registry.record_pid(claim, pid),
                speed=scenario.speed,
            )
            client = emulator.client
            recording = _record_replay(emulator, staged[0], scenario, hooks)
        elif attach:
            if scenario.speed != 1.0:
                print("warning: --attach records PCSX2 at the speed it runs; the scenario's speed is not set")
            client = PineClient(claim.port)
        else:
            paths = resolve_paths(Path(claim.path), flags[1], flags[2])
            source = _source_state(_scenario_state(scenario, state, paths.scratch), paths.pcsx2_dir)
            copy = paths.scratch / f"{scenario_path.stem}.p2s"
            _make_copy(source, copy, patches, paths.pcsx2_dir)
            emulator = Emulator(
                paths, copy, patches, on_start=lambda pid: registry.record_pid(claim, pid), speed=scenario.speed
            )
            client = emulator.client
        if replay is None:
            recording = Recorder(GameMemory(client), scenario, hooks=hooks).record()
        if pending is not None:
            version = _version_for_record(client)
    except (PineError, RecordError, StateError, DebugServerError) as error:
        raise ConfigError(str(error)) from error
    finally:
        # PINE serves one client at a time: always let go of it; end PCSX2 unless asked to keep it.
        if client is not None:
            client.close()
        if emulator is not None and not keep_open:
            emulator.close()
        if not keep_open:
            # The replayed copy of a recording is only this run's: remove it once PCSX2 has let go of it.
            for path in staged:
                try:
                    path.unlink(missing_ok=True)
                except OSError as error:
                    print(f"warning: could not remove {path} ({error})", file=sys.stderr)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(recording.csv(), encoding="utf-8")
    if recording.hooks is not None:
        # One CSV per hook beside the trace: <trace>.<hook>.csv.
        for hook in recording.hooks.hooks:
            if hook.call:
                continue
            path = out.with_name(f"{out.stem}.{hook.name}.csv")
            path.write_text(recording.hooks.csv(hook), encoding="utf-8")
            print(f"{path}: {len(recording.hooks.entries[hook.name])} calls logged")
        if recording.hooks.lost:
            print(f"warning: {recording.hooks.lost} logged calls were lost (the ring overflowed between polls)")
    if recording.missed or recording.torn:
        # Every step to distrust, beside the trace; the line below lists only the first.
        out.with_name(f"{out.stem}.quality.txt").write_text(recording.quality(), encoding="utf-8")
    if pending is not None:
        # After the trace's files: a record that still failed must not cost the measurement.
        _write_run_record(pending, out, recording, claim.copy, version)
    per_poll = 1000 * recording.seconds / max(recording.polls, 1)
    print(
        f"{out}: {recording.updates} updates"
        + (f" ({len(recording.lines)} human rows, {len(recording.torn)} torn)" if recording.lines else "")
        + f", {len(recording.header) - 1} columns; "
        f"{recording.words} reads per poll, {recording.polls} polls in {recording.seconds:.1f} s "
        f"({per_poll:.2f} ms each); missed updates: {len(recording.missed)}"
        + (f" ({', '.join(map(str, recording.missed[:20]))})" if recording.missed else "")
    )
    return 0
