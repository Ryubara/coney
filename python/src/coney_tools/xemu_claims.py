# SPDX-License-Identifier: GPL-3.0-or-later
"""Claims on the xemu copies, so several agents never share one (docs/guides/coney-tools-xemu.md#claims).

The rules are those of the PCSX2 copies (coney_tools.pcsx2_claims), on the same folder mechanics (coney_tools.
claim_files): a claim is `<claims dir>/<copy>.claim/owner.json`, made complete and renamed into place, so exactly one
caller wins; everything `status` shows is rebuilt from the claim folders and the live processes each time. A claim is
stale, and the next `claim` takes it over, when nothing of it lives (the recorded xemu and holder pids are dead, no
xemu runs from the copy's folder and nothing serves its GDB or monitor port) and it either names pids or is older than
the maximum age. A live process always wins.

The claims sit in `xemu-claims/` of the main checkout's shared scratch folder (or `xemu_claims_dir`), never a
worktree's own `scratch_dir`, so every worktree sees the same claims.
"""

from __future__ import annotations

import json
import time
from collections.abc import Callable, Collection
from dataclasses import asdict, dataclass
from datetime import UTC, datetime
from pathlib import Path

from coney_tools import claim_files, pcsx2_proc, xemu_copies
from coney_tools.claim_files import OWNER_FILE, TAKEOVER_ATTEMPTS
from coney_tools.config import find_repo_root, load_config, local_config_file, main_checkout, workspace_folder
from coney_tools.pcsx2_claims import AGENT_NAME, DEFAULT_MAX_AGE, INCOMPLETE_SECONDS, ClaimError, port_open
from coney_tools.xemu_copies import Copy

#: What an xemu process's executable is named after.
PROCESS_NAME = "xemu"


@dataclass(frozen=True)
class Claim:
    """The owner file of a claim: who holds a copy, since when, its ports then, and the processes that keep it."""

    copy: str
    agent: str
    time: str
    path: str
    gdb_port: int
    monitor_port: int
    holder_pid: int | None = None
    xemu_pid: int | None = None

    def age(self, now: float) -> float:
        """Seconds since the claim was made."""
        return now - datetime.fromisoformat(self.time).timestamp()


@dataclass(frozen=True)
class Row:
    """One line of `status`: a copy, its claim (if any) and what really runs."""

    copy: Copy
    claim: Claim | None
    stale: bool
    running: bool
    pids: tuple[int, ...]


def xemu_processes() -> dict[int, Path]:
    """Every running xemu and its executable (the copy it belongs to is the folder holding it)."""
    return pcsx2_proc.processes_named(PROCESS_NAME)


def is_xemu(pid: int) -> bool:
    """Whether the process is an xemu (so a reused pid is never taken for one)."""
    image = pcsx2_proc.process_image(pid)
    return image is not None and PROCESS_NAME in image.name.lower()


def _serves(copy: Copy) -> bool:
    """Whether something listens on the copy's GDB or monitor port (an xemu started by hand records no pid)."""
    return port_open(copy.gdb) or port_open(copy.monitor)


def default_registry(start: Path | None = None) -> Registry:
    """The registry of this machine: `xemu_root` (default: the workspace's `emulators/`) and `xemu_claims_dir`
    (default: `xemu-claims` in the main checkout's shared scratch) from coney.local.toml."""
    root = find_repo_root(start or Path.cwd())
    main = main_checkout(root)
    paths = load_config(root).paths if local_config_file(root) else {}
    claims = paths.get("xemu_claims_dir") or workspace_folder(main, "scratch") / "xemu-claims"
    return Registry(paths.get("xemu_root") or workspace_folder(main, "emulators"), claims)


class Registry:
    """The xemu copies under `root` and their claims under `claims_dir`."""

    def __init__(
        self, root: Path, claims_dir: Path, max_age: float = DEFAULT_MAX_AGE, clock: Callable[[], float] = time.time
    ) -> None:
        """`clock` returns epoch seconds (tests pass a fake one)."""
        self.root = root
        self.claims_dir = claims_dir
        self.max_age = max_age
        self.clock = clock

    def copies(self) -> list[Copy]:
        """The copies under the root; ClaimError when there are none."""
        copies = xemu_copies.discover_copies(self.root) if self.root.is_dir() else []
        if not copies:
            raise ClaimError(
                f"no xemu copies (xemu*/{xemu_copies.COPY_FILE}) under {self.root}; run `coney-tools xemu "
                "setup-copies` (docs/guides/coney-tools-xemu.md#copies)"
            )
        return copies

    def copy(self, name: str) -> Copy:
        """The named copy, or ClaimError listing the known names."""
        copies = self.copies()
        for copy in copies:
            if copy.name == name:
                return copy
        raise ClaimError(f"no xemu copy named {name!r}; known: {', '.join(c.name for c in copies)}")

    def _folder(self, copy: str) -> Path:
        return self.claims_dir / f"{copy}.claim"

    def read(self, copy: str) -> Claim | None:
        """The claim on a copy, None when it is free; one with no readable owner file reads as agent "?"."""
        folder = self._folder(copy)
        if not folder.is_dir():
            return None
        try:
            return Claim(**json.loads((folder / OWNER_FILE).read_text(encoding="utf-8")))
        except (OSError, ValueError, TypeError):
            when = datetime.fromtimestamp(folder.stat().st_mtime, UTC).isoformat() if folder.exists() else ""
            return Claim(copy, "?", when, "", 0, 0)

    # -- liveness

    def pids_of(self, copy: Copy, claim: Claim | None, processes: dict[int, Path]) -> tuple[int, ...]:
        """The live xemu processes of a copy: any running from its folder, and the recorded one if still an xemu."""
        pids = {pid for pid, image in processes.items() if image.parent.resolve() == copy.path}
        if claim is not None and claim.xemu_pid and is_xemu(claim.xemu_pid):
            pids.add(claim.xemu_pid)
        return tuple(sorted(pids))

    def is_stale(self, copy: Copy, claim: Claim, pids: tuple[int, ...]) -> bool:
        """Whether a claim's holder is gone (the module comment): a live process of the copy keeps any claim."""
        if pids or _serves(copy):
            return False
        if claim.holder_pid is not None and pcsx2_proc.pid_alive(claim.holder_pid):
            return False
        now = self.clock()
        if claim.agent == "?":
            return not claim.time or claim.age(now) > INCOMPLETE_SECONDS
        return claim.holder_pid is not None or claim.xemu_pid is not None or claim.age(now) > self.max_age

    def status(self, only: Callable[[Copy, Claim | None], bool] | None = None) -> list[Row]:
        """Every copy with its claim and live processes, from the claim folders and the process list now; with `only`,
        the copies it picks by their claim (the liveness of the others is not looked at: on Windows a probe of a port
        nobody serves takes half a second, which every MCP call would pay for every copy)."""
        processes = xemu_processes()
        rows = []
        for copy in self.copies():
            claim = self.read(copy.name)
            if only is not None and not only(copy, claim):
                continue
            pids = self.pids_of(copy, claim, processes)
            running = bool(pids) or _serves(copy)
            rows.append(Row(copy, claim, claim is not None and self.is_stale(copy, claim, pids), running, pids))
        return rows

    # -- claiming and releasing

    def held_by(self, agent: str) -> list[Claim]:
        """The live claims of `agent`."""
        rows = self.status(only=lambda copy, claim: claim is not None and claim.agent == agent)
        return [r.claim for r in rows if r.claim is not None and not r.stale]

    def claim(self, agent: str, name: str | None = None, holder_pid: int | None = None) -> tuple[Claim, Claim | None]:
        """Claim the named copy, or the first free one; return the claim and the stale claim taken over (or None). An
        agent that holds the copy (or, with no name, any copy) gets that claim back. ClaimError names the holders
        when nothing is free, and refuses a copy that runs without a claim."""
        if not AGENT_NAME.match(agent):
            raise ClaimError(f"agent id {agent!r}: use letters, digits, '.', '_' and '-' (at most 64)")
        rows = self.status()
        wanted = [r for r in rows if name is None or r.copy.name == name]
        if name is not None and not wanted:
            raise ClaimError(f"no xemu copy named {name!r}; known: {', '.join(r.copy.name for r in rows)}")
        for row in rows:
            if row.claim is not None and row.claim.agent == agent and (name is None or row.copy.name == name):
                return row.claim, None
        for row in wanted:
            if row.claim is None and row.running:
                if name is not None:
                    raise ClaimError(f"{row.copy.name} runs xemu (pid {row.pids}) without a claim: someone uses it")
                continue
            made = self._take(row, agent, holder_pid)
            if made is not None:
                return made
        held = ", ".join(f"{r.copy.name}: {r.claim.agent if r.claim else 'running unclaimed'}" for r in wanted)
        raise ClaimError(f"no free xemu copy ({held}); see `coney-tools xemu status`")

    def _take(self, row: Row, agent: str, holder_pid: int | None) -> tuple[Claim, Claim | None] | None:
        """Make the claim folder; over a stale claim, remove that one under the takeover lock first. None when someone
        else won."""
        self.claims_dir.mkdir(parents=True, exist_ok=True)
        copy = row.copy
        claim = Claim(
            copy.name,
            agent,
            datetime.fromtimestamp(self.clock(), UTC).isoformat(timespec="seconds"),
            str(copy.path),
            copy.gdb,
            copy.monitor,
            holder_pid,
        )
        took_over: Claim | None = None
        for _ in range(TAKEOVER_ATTEMPTS):
            if claim_files.publish(self._folder(copy.name), json.dumps(asdict(claim), indent=1)):
                return claim, took_over
            seen = self.read(copy.name)
            if seen is None:
                continue
            if not (row.stale and seen == row.claim):
                return None

            def unchanged(held: Claim = seen) -> bool:
                return self.read(copy.name) == held

            outcome = claim_files.remove_under_lock(self._folder(copy.name), unchanged)
            if outcome == "changed":
                return None
            if outcome == "removed":
                took_over = seen
        return None

    def record_pid(self, claim: Claim, pid: int) -> None:
        """Note the xemu process of a claim, so liveness and `release` use it; ignored when the claim moved on."""
        current = self.read(claim.copy)
        if current is not None and current.agent == claim.agent and current.time == claim.time:
            folder = self._folder(claim.copy)
            temporary = folder / f"{OWNER_FILE}.new"
            temporary.write_text(json.dumps({**asdict(current), "xemu_pid": pid}, indent=1), encoding="utf-8")
            temporary.replace(folder / OWNER_FILE)

    def release(self, agent: str, name: str | None = None, force: bool = False) -> list[tuple[Claim, bool]]:
        """Release the named copy's claim, or all of `agent`'s: close its xemu (only a process that is still an xemu)
        and remove the claim. Another agent's live claim needs `force`. Returns each claim and whether xemu was
        closed."""
        rows = {row.copy.name: row for row in self.status()}
        if name is not None and name not in rows:
            raise ClaimError(f"no xemu copy named {name!r}; known: {', '.join(rows)}")
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
            claim_files.remove(self._folder(row.copy.name))
            released.append((row.claim, closed))
        return released

    def require_claim(self, agent: str, name: str | None = None) -> tuple[Claim, Copy]:
        """The live claim `agent` holds (on the named copy, or its only one) and that copy, or ClaimError: nothing
        touches a copy its caller has not claimed."""

        def wanted(copy: Copy, claim: Claim | None) -> bool:
            return claim is not None and claim.agent == agent and (name is None or copy.name == name)

        mine = self.status(only=wanted)  # only this agent's copies: every MCP call comes through here
        if not mine:
            if name is not None:
                copy = self.copy(name)
                held = self.read(copy.name)
                holder = held.agent if held is not None else "nobody"
                raise ClaimError(
                    f"{name} is held by {holder}, not {agent}; claim one with `coney-tools xemu claim --agent {agent}`"
                )
            raise ClaimError(f"{agent} holds no xemu copy; run `coney-tools xemu claim --agent {agent}` first")
        if len(mine) > 1:
            raise ClaimError(f"{agent} holds {', '.join(r.copy.name for r in mine)}; name the copy")
        row = mine[0]
        assert row.claim is not None
        if row.stale:
            raise ClaimError(
                f"{agent}'s claim on {row.copy.name} is stale (no process of it lives); start it with "
                f"`coney-tools xemu launch --agent {agent}`, or release it with `coney-tools xemu release --agent "
                f"{agent}` and claim again"
            )
        return row.claim, row.copy

    def require_ports(self, agents: Collection[str], gdb_port: int | None, monitor_port: int | None) -> Claim:
        """The live claim one of `agents` holds on the copy the given ports belong to, or ClaimError: the check a
        script makes before pointing a client at ports it was given (`xemu check-ports`)."""
        if gdb_port is None and monitor_port is None:
            raise ClaimError("no port given: give the GDB port and the monitor port of your claimed copy")
        rows = self.status()
        found = {
            r.copy.name: r
            for r in rows
            for port, own in ((gdb_port, r.copy.gdb), (monitor_port, r.copy.monitor))
            if port is not None and port == own
        }
        if not found:
            known = ", ".join(f"{r.copy.name} {r.copy.gdb}/{r.copy.monitor}" for r in rows)
            raise ClaimError(f"the ports are those of no xemu copy (copies, GDB/monitor: {known})")
        if len(found) > 1 or any(
            port is not None and port not in (r.copy.gdb, r.copy.monitor)
            for r in found.values()
            for port in (gdb_port, monitor_port)
        ):
            raise ClaimError("the GDB and monitor ports belong to different copies; use the ports `xemu claim` prints")
        (row,) = found.values()
        if row.claim is None or row.stale or row.claim.agent not in agents:
            holder = "nobody" if row.claim is None else row.claim.agent + (" (stale)" if row.stale else "")
            raise ClaimError(f"{row.copy.name} is held by {holder}, not {', '.join(sorted(agents)) or 'nobody'}")
        return row.claim
