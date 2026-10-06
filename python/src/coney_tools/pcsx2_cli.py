# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools pcsx2 ...` commands: patched state copies, launching PCSX2 on one, and recording a scenario.

Paths come from flags or from `coney.local.toml` (`pcsx2_dir`, `game_dir`, `scratch_dir`); nothing here knows a
machine's own folders. Everything the commands write (state copies, the disc link, traces) is game data or measured
from it, so it goes to the scratch folder or a path the user gives outside the repository
(docs/guides/research-workflow.md#recording-a-trace).
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
import time
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

from coney_tools import hooks as hook_tools
from coney_tools import pcsx2_claims, pcsx2_proc, pcsx2_state
from coney_tools.config import ConfigError, find_repo_root, load_config
from coney_tools.game_memory import GAME_TIME_OFFSET, GAME_TIMER_POINTER, GameMemory
from coney_tools.hooks import Hook, HookError
from coney_tools.pcsx2_state import Patch, StateError
from coney_tools.pine import PineClient, PineError, Read
from coney_tools.recorder import Recorder, RecordError
from coney_tools.scenario import PATCHES_FILE, Scenario, ScenarioError, load_scenario
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
        if (root / "coney.local.toml").is_file():
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


class Emulator:
    """PCSX2 started on a state file; close() ends it and warns about new files in its sstates/ folder."""

    def __init__(
        self, paths: Paths, state: Path, patches: list[Patch], on_start: Callable[[int], None] | None = None
    ) -> None:
        """Start PCSX2 on `state` and wait until its game runs; `on_start` gets the process id as soon as it exists
        (the claim records it). Raises ConfigError when PCSX2 already runs (one PINE client at a time, and a run needs
        a fresh start for its patches) or PineError when it never answers."""
        self.paths = paths
        self.port = pine_port(paths.pcsx2_dir)
        if _port_open(self.port):
            raise ConfigError(f"something already serves PINE on port {self.port}; close PCSX2 first (or --attach)")
        self.before = _slot_states(paths.pcsx2_dir)
        disc = disc_link(paths.iso, paths.scratch)
        command = [str(_executable(paths.pcsx2_dir)), "-fastboot", "-statefile", str(state), "--", str(disc)]
        self.process = subprocess.Popen(
            command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, **pcsx2_proc.quiet_start()
        )
        if on_start is not None:
            on_start(self.process.pid)
        try:
            self.client = _wait_for_game(self.port, patches, time.monotonic() + LAUNCH_TIMEOUT)
        except PineError:
            self.close()
            raise

    def close(self) -> None:
        """End PCSX2 (no shutdown save) and report any state file that appeared in its sstates/ folder."""
        if getattr(self, "client", None) is not None:
            self.client.close()
        self.process.terminate()
        try:
            self.process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            self.process.kill()
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
    """The named patches and hooks of research/traces/patches.toml: the patches, with one more installing the hooks
    (ids in the order named), and the hooks."""
    root = find_repo_root(Path.cwd())
    try:
        known = pcsx2_state.load_patches(root / PATCHES_FILE)
        hooks = hook_tools.hooks_for(names, hook_tools.load_hooks(root / PATCHES_FILE), set(known))
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


def run_launch(
    state: Path, pcsx2_dir: Path | None, iso: Path | None, scratch: Path | None, agent: str | None = None
) -> int:
    """`pcsx2 launch`: start PCSX2 on a state file under `agent`'s claim, wait for its game, and leave it running.
    Without a claim of the agent's on that copy it makes one (kept: `pcsx2 release` ends it)."""
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
    print(f"PCSX2 runs {state} (pid {emulator.process.pid}) on {claim.copy}; PINE on port {emulator.port}")
    print(f"{claim.agent} keeps the claim on {claim.copy}; `coney-tools pcsx2 release --agent {claim.agent}` ends it")
    return 0


def run_record(
    scenario_path: Path,
    out: Path,
    state: str | None,
    attach: bool,
    keep_open: bool,
    flags: tuple[Path | None, Path | None, Path | None],
    agent: str | None = None,
) -> int:
    """`pcsx2 record`: play a scenario on the original and write its trace, under `agent`'s claim: the one it holds
    (kept for it afterwards), else one made here and released at the end, also on an error or Ctrl-C (unless
    `keep_open`)."""
    refuse_inside_repo(out)
    registry = pcsx2_claims.default_registry()
    claim, created = registry.claim_for_run(agent, flags[0], os.getpid())
    try:
        return _record(scenario_path, out, state, attach, keep_open, flags, registry, claim)
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
) -> int:
    """The recording of `run_record`, on the copy of `claim`."""
    try:
        scenario = load_scenario(scenario_path, find_repo_root(Path.cwd()))
    except ScenarioError as error:
        raise ConfigError(str(error)) from error
    emulator: Emulator | None = None
    client: PineClient | None = None
    try:
        patches, hooks = _patches(list(scenario.patches))
        if attach:
            client = PineClient(claim.port)
        else:
            paths = resolve_paths(Path(claim.path), flags[1], flags[2])
            source = _source_state(_scenario_state(scenario, state, paths.scratch), paths.pcsx2_dir)
            copy = paths.scratch / f"{scenario_path.stem}.p2s"
            _make_copy(source, copy, patches, paths.pcsx2_dir)
            emulator = Emulator(paths, copy, patches, on_start=lambda pid: registry.record_pid(claim, pid))
            client = emulator.client
        recording = Recorder(GameMemory(client), scenario, hooks=hooks).record()
    except (PineError, RecordError, StateError) as error:
        raise ConfigError(str(error)) from error
    finally:
        # PINE serves one client at a time: always let go of it; end PCSX2 unless asked to keep it.
        if client is not None:
            client.close()
        if emulator is not None and not keep_open:
            emulator.close()
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
    per_poll = 1000 * recording.seconds / max(recording.polls, 1)
    print(
        f"{out}: {len(recording.rows)} updates, {len(recording.header) - 1} columns; "
        f"{recording.words} reads per poll, {recording.polls} polls in {recording.seconds:.1f} s "
        f"({per_poll:.2f} ms each); missed updates: {len(recording.missed)}"
        + (f" ({', '.join(map(str, recording.missed[:20]))})" if recording.missed else "")
    )
    return 0
