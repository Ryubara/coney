# SPDX-License-Identifier: GPL-3.0-or-later
"""Claims on the portable PCSX2 copies, so several agents never share one.

Each copy (a `pcsx2*` folder with its own `PCSX2.ini` and PINE port) is held through a claim: a folder
`<claims dir>/<copy>.claim/` holding `owner.json`, built complete in a private folder and renamed into place, which
succeeds for exactly one caller and never shows a half-made claim. The
claims dir is one shared place every worktree sees (docs/guides/research-workflow.md#several-at-once). Everything
reported (`status`) is rebuilt from the claim folders and the live processes each time; no cached state file exists
to go stale, and nobody maintains one by hand.

A claim is stale, and may be taken over, when no process of it lives (the recorded PCSX2 and holder pids are dead and
nothing serves its PINE port) and it either names pids (all dead) or is older than the maximum age.
"""

from __future__ import annotations

import configparser
import json
import os
import re
import shutil
import socket
import subprocess
import time
import uuid
from collections.abc import Callable
from dataclasses import asdict, dataclass
from datetime import UTC, datetime
from pathlib import Path

from coney_tools import pcsx2_proc
from coney_tools.config import ConfigError, find_repo_root, load_config
from coney_tools.pine import DEFAULT_PORT

#: A claim with no process to show for it is stale after this many seconds (4 hours).
DEFAULT_MAX_AGE = 4 * 3600.0
#: A claim folder without its owner file is a claim being made; after this many seconds it was abandoned.
INCOMPLETE_SECONDS = 10.0

#: How long a takeover lock may stand before it counts as abandoned.
TAKEOVER_LOCK_SECONDS = 30.0

#: How often a takeover looks again after losing a step to a racer.
TAKEOVER_ATTEMPTS = 200
OWNER_FILE = "owner.json"
AGENT_NAME = re.compile(r"^[A-Za-z0-9._-]{1,64}$")


class ClaimError(ConfigError):
    """A claim cannot be made, found or released; the message says who holds what."""


@dataclass(frozen=True)
class Copy:
    """A portable PCSX2 folder: its name (the folder's), path and PINE port (None when PINE is off in its ini)."""

    name: str
    path: Path
    port: int | None
    #: Whether its ini lets the SDL gamepad source drive it (the owner's pad would then move the game).
    sdl: bool = False


@dataclass(frozen=True)
class Claim:
    """The owner file of a claim: who holds a copy, since when, and the processes that keep it alive."""

    copy: str
    agent: str
    time: str
    path: str
    port: int
    holder_pid: int | None = None
    pcsx2_pid: int | None = None

    def age(self, now: float) -> float:
        """Seconds since the claim was made."""
        return now - datetime.fromisoformat(self.time).timestamp()


@dataclass(frozen=True)
class Row:
    """One line of `status`: a copy, its claim (if any) and what is really running."""

    copy: Copy
    claim: Claim | None
    stale: bool
    running: bool
    pids: tuple[int, ...]


def read_pine_port(pcsx2_dir: Path) -> int | None:
    """The PINE port of a copy's ini (`[EmuCore]` `PINESlot`), None when PINE is not enabled. Raises ConfigError for
    an unreadable ini."""
    ini = pcsx2_dir / "inis" / "PCSX2.ini"
    parser = configparser.ConfigParser(interpolation=None, strict=False)
    try:
        parser.read(ini, encoding="utf-8")
    except configparser.Error as error:
        raise ConfigError(f"{ini}: {error}") from error
    if not parser.getboolean("EmuCore", "EnablePINE", fallback=False):
        return None
    return parser.getint("EmuCore", "PINESlot", fallback=DEFAULT_PORT)


def reads_gamepad(pcsx2_dir: Path) -> bool:
    """Whether the copy's ini has `[InputSources]` `SDL = true`, so a physical gamepad can drive it."""
    parser = configparser.ConfigParser(interpolation=None, strict=False)
    try:
        parser.read(pcsx2_dir / "inis" / "PCSX2.ini", encoding="utf-8")
        return parser.getboolean("InputSources", "SDL", fallback=False)
    except (configparser.Error, ValueError):
        return False


def port_open(port: int) -> bool:
    """Whether something listens on localhost:`port`."""
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=0.5):
            return True
    except OSError:
        return False


def discover_copies(root: Path) -> list[Copy]:
    """The `pcsx2*` folders of `root` that hold a PCSX2.ini, in name order (`pcsx2` first)."""
    found = [
        Copy(folder.name, folder.resolve(), read_pine_port(folder), reads_gamepad(folder))
        for folder in sorted(root.glob("pcsx2*"))
        if (folder / "inis" / "PCSX2.ini").is_file()
    ]
    return sorted(found, key=lambda copy: (copy.name != "pcsx2", copy.name))


def main_checkout(root: Path) -> Path:
    """The main checkout of the repository at `root` (a worktree's `.git` points back to it); `root` itself when it
    is the main one or git cannot say."""
    result = subprocess.run(
        ["git", "-C", str(root), "rev-parse", "--path-format=absolute", "--git-common-dir"],
        capture_output=True,
        text=True,
        check=False,
    )
    common = Path(result.stdout.strip()) if result.returncode == 0 and result.stdout.strip() else None
    return common.parent if common is not None and common.name == ".git" else root


def default_registry() -> Registry:
    """The registry for this machine: the copies in `pcsx2_root` and the claims in `pcsx2_claims_dir` from
    coney.local.toml (this checkout's, else the main checkout's), else the main checkout's folder and
    `<scratch_dir>/pcsx2-claims` (`../../scratch` beside the main checkout when scratch_dir is unset too)."""
    root = find_repo_root(Path.cwd())
    main = main_checkout(root)
    paths: dict[str, Path | None] = {}
    for candidate in (root, main):
        if (candidate / "coney.local.toml").is_file():
            paths = load_config(candidate).paths
            break
    scratch = paths.get("scratch_dir") or (main / ".." / ".." / "scratch").resolve()
    claims = paths.get("pcsx2_claims_dir") or scratch / "pcsx2-claims"
    return Registry(paths.get("pcsx2_root") or main, claims)


class Registry:
    """The copies under `root` and their claims under `claims_dir`."""

    def __init__(
        self,
        root: Path,
        claims_dir: Path,
        max_age: float = DEFAULT_MAX_AGE,
        clock: Callable[[], float] = time.time,
    ) -> None:
        """`clock` returns epoch seconds (tests pass a fake one)."""
        self.root = root
        self.claims_dir = claims_dir
        self.max_age = max_age
        self.clock = clock

    # -- copies and claim files

    def copies(self) -> list[Copy]:
        """The copies found under the root; raises ClaimError when there are none."""
        copies = discover_copies(self.root)
        if not copies:
            raise ClaimError(f"no PCSX2 copies (pcsx2*/inis/PCSX2.ini) under {self.root}; set pcsx2_root")
        return copies

    def copy(self, name: str) -> Copy:
        """The named copy, or ClaimError listing the known names."""
        copies = self.copies()
        for copy in copies:
            if copy.name == name:
                return copy
        raise ClaimError(f"no PCSX2 copy named {name!r}; known: {', '.join(c.name for c in copies)}")

    def copy_at(self, path: Path) -> Copy:
        """The copy whose folder is `path`, or ClaimError (a --pcsx2-dir outside the claimable copies)."""
        for copy in self.copies():
            if copy.path == path.resolve():
                return copy
        raise ClaimError(f"{path} is not one of the claimable PCSX2 copies under {self.root}")

    def _folder(self, copy: str) -> Path:
        """The claim folder of a copy."""
        return self.claims_dir / f"{copy}.claim"

    def read(self, copy: str) -> Claim | None:
        """The claim on a copy, None when it is free. A claim folder with no readable owner file (one being made, or
        damaged) reads as held by agent "?"."""
        folder = self._folder(copy)
        if not folder.is_dir():
            return None
        try:
            return Claim(**json.loads((folder / OWNER_FILE).read_text(encoding="utf-8")))
        except (OSError, ValueError, TypeError):
            when = datetime.fromtimestamp(folder.stat().st_mtime, UTC).isoformat() if folder.exists() else ""
            return Claim(copy, "?", when, "", 0)

    def _write(self, claim: Claim) -> None:
        """Write the owner file whole (a temporary file, then an atomic replace)."""
        folder = self._folder(claim.copy)
        temporary = folder / f"{OWNER_FILE}.{os.getpid()}.{uuid.uuid4().hex[:6]}"
        temporary.write_text(json.dumps(asdict(claim), indent=1), encoding="utf-8")
        temporary.replace(folder / OWNER_FILE)

    # -- liveness

    def pids_of(self, copy: Copy, claim: Claim | None, processes: dict[int, Path]) -> tuple[int, ...]:
        """The live PCSX2 processes of a copy: the recorded one (if it is still a PCSX2) and any running from the
        copy's folder (a launch by hand records nothing)."""
        pids = {pid for pid, image in processes.items() if image.parent.resolve() == copy.path}
        if claim is not None and claim.pcsx2_pid and pcsx2_proc.is_pcsx2(claim.pcsx2_pid):
            pids.add(claim.pcsx2_pid)
        return tuple(sorted(pids))

    def is_stale(self, copy: Copy, claim: Claim, pids: tuple[int, ...]) -> bool:
        """Whether a claim's holder is gone (see the module comment)."""
        now = self.clock()
        if claim.agent == "?":
            return not claim.time or claim.age(now) > INCOMPLETE_SECONDS
        holder = claim.holder_pid is not None and pcsx2_proc.pid_alive(claim.holder_pid)
        if pids or holder or (copy.port is not None and port_open(copy.port)):
            return False
        return claim.holder_pid is not None or claim.pcsx2_pid is not None or claim.age(now) > self.max_age

    def status(self) -> list[Row]:
        """Every copy with its claim and live processes, from the claim folders and the process list now."""
        processes = pcsx2_proc.pcsx2_processes()
        rows = []
        for copy in self.copies():
            claim = self.read(copy.name)
            pids = self.pids_of(copy, claim, processes)
            running = bool(pids) or (copy.port is not None and port_open(copy.port))
            stale = claim is not None and self.is_stale(copy, claim, pids)
            rows.append(Row(copy, claim, stale, running, pids))
        return rows

    # -- claiming and releasing

    def held_by(self, agent: str) -> list[Claim]:
        """The live claims of `agent`."""
        return [row.claim for row in self.status() if row.claim is not None and row.claim.agent == agent]

    def claim(self, agent: str, name: str | None = None, holder_pid: int | None = None) -> tuple[Claim, Claim | None]:
        """Claim the named copy, or the first free one, for `agent`; return the claim and the stale claim taken over
        (None normally). An agent that already holds the copy (or, with no name, any copy) gets that claim back.
        Raises ClaimError naming the holders when nothing is free, and for a copy that runs without a claim."""
        if not AGENT_NAME.match(agent):
            raise ClaimError(f"agent id {agent!r}: use letters, digits, '.', '_' and '-' (at most 64)")
        rows = self.status()
        mine = [r.claim for r in rows if r.claim is not None and r.claim.agent == agent]
        wanted = [r for r in rows if name is None or r.copy.name == name]
        if name is not None and not wanted:
            raise ClaimError(f"no PCSX2 copy named {name!r}; known: {', '.join(r.copy.name for r in rows)}")
        for claim in mine:
            if name is None or claim.copy == name:
                return claim, None
        for row in wanted:
            if row.copy.port is None:
                if name is not None:
                    raise ClaimError(f"{row.copy.name}: PINE is off in its PCSX2.ini; enable it first")
                continue
            if row.claim is None and row.running:
                if name is not None:
                    raise ClaimError(f"{row.copy.name} runs PCSX2 (pid {row.pids}) without a claim: someone uses it")
                continue
            made = self._take(row, agent, holder_pid)
            if made is not None:
                return made
        held = ", ".join(f"{r.copy.name}: {r.claim.agent if r.claim else 'running unclaimed'}" for r in wanted)
        raise ClaimError(f"no free PCSX2 copy ({held}); see `coney-tools pcsx2 status`")

    def _take(self, row: Row, agent: str, holder_pid: int | None) -> tuple[Claim, Claim | None] | None:
        """Make the claim folder; over a stale claim, move that one away first. None when someone else won."""
        copy, port = row.copy, row.copy.port
        assert port is not None  # claim() skips copies without PINE
        self.claims_dir.mkdir(parents=True, exist_ok=True)
        folder = self._folder(copy.name)
        took_over: Claim | None = None
        claim = Claim(
            copy.name,
            agent,
            datetime.fromtimestamp(self.clock(), UTC).isoformat(timespec="seconds"),
            str(copy.path),
            port,
            holder_pid,
        )
        for _ in range(TAKEOVER_ATTEMPTS):
            if self._publish(claim):
                return claim, took_over
            seen = self.read(copy.name)
            if seen is None:
                continue
            if not (row.stale and seen == row.claim):
                return None
            # Takeovers are serialised by a lock folder (mkdir succeeds for one racer), and the claim is looked at
            # again under it: only a lock holder removes a stale claim, so a fresh claim a racer made meanwhile is
            # never moved or deleted (renaming it away and back let two agents win).
            lock = folder.with_name(f".{folder.name}.takeover")
            try:
                lock.mkdir()
            except OSError:
                # Another racer takes over (or Windows is still deleting its lock): wait, then look again, so
                # a holder that fails does not leave every racer empty-handed.
                self._break_old_lock(lock)
                time.sleep(0.01)
                continue
            try:
                if self.read(copy.name) != seen:
                    return None
                grave = folder.with_name(f"{folder.name}.stale-{uuid.uuid4().hex[:8]}")
                folder.rename(grave)
                shutil.rmtree(grave, ignore_errors=True)
                took_over = seen
            except OSError:
                time.sleep(0.01)  # Windows refuses to move a folder someone is reading; try again
                continue
            finally:
                shutil.rmtree(lock, ignore_errors=True)
        return None

    @staticmethod
    def _break_old_lock(lock: Path) -> None:
        """Remove a takeover lock its owner abandoned (a crash between making and removing it, seconds apart)."""
        try:
            if time.time() - lock.stat().st_mtime > TAKEOVER_LOCK_SECONDS:
                shutil.rmtree(lock, ignore_errors=True)
        except OSError:
            pass

    def _publish(self, claim: Claim) -> bool:
        """Create the claim folder complete and atomically: the owner file is written in a private folder that is
        then renamed to the claim's name. A claim is thus never visible half-made (an owner-less folder reads as an
        abandoned one, which a racer may take over; that once let two agents win). The rename cannot replace an
        existing claim, which always holds an owner file. False when the copy is already claimed."""
        folder = self._folder(claim.copy)
        staging = folder.with_name(f".{folder.name}.new-{uuid.uuid4().hex[:8]}")
        staging.mkdir()
        try:
            (staging / OWNER_FILE).write_text(json.dumps(asdict(claim), indent=1), encoding="utf-8")
            staging.rename(folder)
            return True
        except (FileExistsError, PermissionError):
            # The name is taken (Windows may say PermissionError too), or is being moved away for a takeover; the
            # caller looks again.
            return False
        finally:
            shutil.rmtree(staging, ignore_errors=True)

    def _owner_at(self, folder: Path) -> Claim | None:
        """The claim stored in a (moved) claim folder."""
        try:
            return Claim(**json.loads((folder / OWNER_FILE).read_text(encoding="utf-8")))
        except (OSError, ValueError, TypeError):
            return Claim(folder.name.split(".claim")[0], "?", "", "", 0)

    def record_pid(self, claim: Claim, pid: int) -> None:
        """Note the PCSX2 process of a claim, so liveness and `release` use it. Ignored when the claim moved on."""
        current = self.read(claim.copy)
        if current is not None and current.agent == claim.agent and current.time == claim.time:
            self._write(Claim(**{**asdict(current), "pcsx2_pid": pid}))

    def release(self, agent: str, name: str | None = None, force: bool = False) -> list[tuple[Claim, bool]]:
        """Release the named copy's claim, or all of `agent`'s when no name is given: close its PCSX2 if one runs
        (only a process that is still a PCSX2) and remove the claim. Another agent's claim needs `force` (a stale
        one does not). Returns each released claim and whether a PCSX2 was closed."""
        rows = {row.copy.name: row for row in self.status()}
        if name is not None and name not in rows:
            raise ClaimError(f"no PCSX2 copy named {name!r}; known: {', '.join(rows)}")
        chosen = [
            row
            for row in rows.values()
            if row.claim is not None and (row.copy.name == name if name else row.claim.agent == agent)
        ]
        if not chosen:
            raise ClaimError(f"{name or agent}: nothing to release (no claim held by {agent})")
        released = []
        for row in chosen:
            assert row.claim is not None
            if row.claim.agent != agent and not row.stale and not force:
                raise ClaimError(f"{row.copy.name} is held by {row.claim.agent}, not {agent}; --force overrides")
            closed = False
            for pid in row.pids:
                closed = pcsx2_proc.terminate(pid) or closed
            self._remove(row.copy.name)
            released.append((row.claim, closed))
        return released

    def _remove(self, name: str) -> None:
        """Remove a claim folder: renamed away first, so it vanishes from its name in one step."""
        folder = self._folder(name)
        grave = folder.with_name(f"{folder.name}.gone-{uuid.uuid4().hex[:8]}")
        try:
            folder.rename(grave)
        except FileNotFoundError:
            return
        shutil.rmtree(grave, ignore_errors=True)

    def claim_for_run(self, agent: str | None, pcsx2_dir: Path | None, holder_pid: int | None) -> tuple[Claim, bool]:
        """The claim a command that starts PCSX2 runs under, and whether this call made it (so the caller releases it
        on failure). `agent` is required. With `pcsx2_dir`, that copy must be the agent's claim or claimable now;
        without, the agent's claim or any free copy."""
        if not agent:
            raise ClaimError("starting PCSX2 needs a claim: pass --agent <your id> (see `coney-tools pcsx2 claim`)")
        name = self.copy_at(pcsx2_dir).name if pcsx2_dir is not None else None
        before = {c.copy: c for c in self.held_by(agent)}
        claim, _ = self.claim(agent, name, holder_pid)
        return claim, claim.copy not in before

    def require_claim(self, agent: str, name: str) -> Claim:
        """The claim `agent` holds on the named copy, or ClaimError (keys and screenshots of a copy you don't hold)."""
        claim = self.read(self.copy(name).name)
        if claim is None or claim.agent != agent:
            holder = "nobody" if claim is None else claim.agent
            raise ClaimError(f"{name} is held by {holder}, not {agent}; claim it first")
        return claim
