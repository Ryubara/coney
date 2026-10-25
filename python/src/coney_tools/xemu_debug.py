# SPDX-License-Identifier: GPL-3.0-or-later
"""One debugging session on a claimed xemu copy: its GDB stub and its QEMU monitor together, kept in step
(docs/guides/coney-tools-xemu.md#mcp).

The stub reads and writes memory, registers and breakpoints but stops the guest for every request (QEMU's all-stop
stub, coney_tools.gdb_remote); the monitor says whether the guest runs without stopping it, pauses and resumes it, and
takes screenshots and snapshots. So the session asks the monitor first, lets the stub stop the guest only for as long
as a request takes, and leaves the guest running or paused as it found it: a read of a running game is a pause of a
few milliseconds, not a pause until someone resumes it.
"""

from __future__ import annotations

import re
import tempfile
from collections.abc import Callable
from dataclasses import dataclass, field
from pathlib import Path

from coney_tools import pcsx2_proc
from coney_tools.config import ConfigError
from coney_tools.gdb_remote import GdbClient, GdbError, Stop
from coney_tools.qemu_monitor import MonitorClient, MonitorError
from coney_tools.xemu_copies import Copy

#: A snapshot's name: what `savevm` and `loadvm` take, kept to plain characters so it is one word of the monitor.
SNAPSHOT_NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$")


@dataclass
class Point:
    """A breakpoint or watchpoint the session set."""

    kind: str
    address: int
    length: int


@dataclass
class XemuSession:
    """The connections to one copy. `gdb_factory` and `monitor_factory` make the clients (tests pass fakes)."""

    copy: Copy
    timeout: float = 5.0
    gdb_factory: Callable[[int, float], GdbClient] = field(
        default=lambda port, timeout: GdbClient(port, timeout=timeout)
    )
    monitor_factory: Callable[[int, float], MonitorClient] = field(
        default=lambda port, timeout: MonitorClient(port, timeout=timeout)
    )
    points: list[Point] = field(default_factory=list)
    _gdb: GdbClient | None = None
    _monitor: MonitorClient | None = None

    # -- connections

    def monitor(self) -> MonitorClient:
        """The monitor connection, made on first use."""
        if self._monitor is None or not self._monitor.connected:
            client = self.monitor_factory(self.copy.monitor, self.timeout)
            client.connect()
            self._monitor = client
        return self._monitor

    def gdb(self) -> GdbClient:
        """The stub connection, made on first use. Attaching stops the guest, so a guest that ran is let run on."""
        if self._gdb is not None and self._gdb.connected:
            return self._gdb
        was_running = self._monitor_running()
        client = self.gdb_factory(self.copy.gdb, self.timeout)
        client.connect()
        self._gdb = client
        if was_running:
            client.cont()
        return client

    @property
    def broken(self) -> bool:
        """Whether a connection that was made has closed (the stub or monitor went away): the session is remade."""
        return any(client is not None and not client.connected for client in (self._gdb, self._monitor))

    def close(self) -> None:
        """Close both connections, leaving the guest running or paused as it was before the stub detached."""
        wanted = self.running() if self._gdb is not None and self._gdb.connected else None
        for client in (self._gdb, self._monitor):
            if client is not None:
                client.close()
        self._gdb = None
        if wanted is not None:
            try:
                if self._monitor_running() != wanted:
                    self.monitor().checked("cont" if wanted else "stop")
            except (MonitorError, GdbError):
                pass
        if self._monitor is not None:
            self._monitor.close()
        self._monitor = None

    def _monitor_running(self) -> bool | None:
        """Whether the guest runs, by the monitor; None when there is no monitor to ask."""
        try:
            return self.monitor().running()
        except MonitorError:
            return None

    def _sync(self) -> GdbClient:
        """The stub connection, its idea of running or stopped made the monitor's (a `stop` or `cont` typed into the
        monitor, or xemu's own pause menu, changes the guest without telling the stub's client)."""
        gdb = self.gdb()
        actual = self._monitor_running()
        if actual is None:
            return gdb
        if gdb.running and not actual:
            if gdb.wait_stop(0.5) is None:
                gdb.running = False
        elif not gdb.running and actual:
            gdb.running = True
        return gdb

    # -- what the tools do

    def running(self) -> bool:
        """Whether the guest runs now."""
        actual = self._monitor_running()
        if actual is not None:
            return actual
        return self.gdb().running

    def status(self) -> dict[str, object]:
        """The copy, its ports, whether the guest runs, the emulator's version and, when stopped, why and where."""
        gdb = self._sync()
        out: dict[str, object] = {
            "copy": self.copy.name,
            "gdb_port": self.copy.gdb,
            "monitor_port": self.copy.monitor,
            "running": gdb.running,
        }
        try:
            out["version"] = self.monitor().version()
        except MonitorError as error:
            out["version"] = f"unknown ({error})"
        if not gdb.running:
            stop = gdb.last_stop
            out["stop"] = stop.text if stop is not None else ""
            out["eip"] = f"0x{gdb.registers()['eip']:08x}"
        out["points"] = [f"{p.kind} 0x{p.address:08x} ({p.length})" for p in self.points]
        return out

    def read(self, address: int, length: int) -> bytes:
        """Guest memory; a running guest is stopped for the read only."""
        return self._sync().read_memory(address, length, resume=True)

    def write(self, address: int, data: bytes) -> None:
        """Write guest memory; a running guest is stopped for the write only."""
        self._sync().write_memory(address, data, resume=True)

    def registers(self) -> dict[str, int]:
        """The i386 core registers (a running guest is stopped for the read only)."""
        return self._sync().registers(resume=True)

    def write_register(self, name: str, value: int) -> None:
        """Set a register; the guest must be paused (a register of a running guest changes under the write)."""
        gdb = self._sync()
        if gdb.running:
            raise GdbError("pause the guest first (xemu_pause): a register of a running guest changes under a write")
        gdb.write_register(name, value)

    def set_point(self, kind: str, address: int, length: int) -> None:
        """Set a breakpoint or watchpoint, leaving a running guest running."""
        gdb = self._sync()
        was_running = gdb.ensure_stopped()
        try:
            gdb.set_point(kind, address, length)
            self.points.append(Point(kind, address, length))
        finally:
            if was_running:
                gdb.cont()

    def remove_point(self, kind: str, address: int, length: int) -> None:
        """Remove a point this session set."""
        gdb = self._sync()
        was_running = gdb.ensure_stopped()
        try:
            gdb.remove_point(kind, address, length)
            self.points = [p for p in self.points if (p.kind, p.address, p.length) != (kind, address, length)]
        finally:
            if was_running:
                gdb.cont()

    def pause(self) -> Stop:
        """Pause the guest (through the stub, so it knows) and return the stop."""
        gdb = self._sync()
        if gdb.running:
            return gdb.interrupt()
        return gdb.last_stop or Stop(0, text="")

    def resume(self, wait: float = 0.0) -> Stop | None:
        """Let the guest run; with `wait` seconds, wait that long for it to stop (a breakpoint or watchpoint) and
        return the stop, or None while it runs."""
        gdb = self._sync()
        gdb.cont()
        return gdb.wait_stop(wait) if wait > 0 else None

    def step(self, count: int) -> Stop:
        """Run `count` instructions, the guest stopped afterwards."""
        gdb = self._sync()
        stop = gdb.last_stop or Stop(0)
        for _ in range(count):
            stop = gdb.step()
        return stop

    def screenshot(self, out: Path) -> tuple[int, int, str]:
        """Write the screen as a PNG: the monitor's `screendump`, else the copy's window read by handle (no focus
        change). Returns the size and which way it was taken."""
        out.parent.mkdir(parents=True, exist_ok=True)
        try:
            return (*_screendump(self.monitor(), out), "screendump")
        except (MonitorError, OSError, ValueError) as error:
            dump_error = error
        pids = [
            pid for pid, image in pcsx2_proc.processes_named("xemu").items() if image.parent.resolve() == self.copy.path
        ]
        if not pids:
            raise ConfigError(f"{self.copy.name}: no screenshot (the monitor said: {dump_error}; no xemu window found)")
        window = pcsx2_proc.render_window(pids[0])
        width, height, _ = pcsx2_proc.capture(window.hwnd, out)
        return width, height, "window"

    def snapshots(self) -> list[str]:
        """The snapshots in the copy's disk image."""
        return self.monitor().snapshots()

    def save_snapshot(self, name: str) -> None:
        """Save the machine as snapshot `name` in the copy's disk image (`savevm`)."""
        self.monitor().checked(f"savevm {_snapshot(name)}")

    def load_snapshot(self, name: str) -> None:
        """Load snapshot `name` (`loadvm`); the stub's idea of the guest is read again afterwards."""
        self.monitor().checked(f"loadvm {_snapshot(name)}")
        if self._gdb is not None and self._gdb.connected:
            self._sync()


def _snapshot(name: str) -> str:
    """A snapshot name the monitor takes as one word; ConfigError otherwise."""
    if not SNAPSHOT_NAME.match(name):
        raise ConfigError(f"snapshot {name!r}: letters, digits, '.', '_' and '-', at most 64")
    return name


def _screendump(monitor: MonitorClient, out: Path) -> tuple[int, int]:
    """Have QEMU write the screen (a PPM, the one format every version writes) and keep it as a PNG at `out`."""
    from PIL import Image  # Pillow is a dependency; imported here so the server starts without loading it

    with tempfile.TemporaryDirectory(prefix="coney-xemu-") as folder:
        ppm = Path(folder) / "screen.ppm"
        monitor.checked(f"screendump {ppm.as_posix()}")
        if not ppm.is_file():
            raise MonitorError("screendump wrote no file")
        with Image.open(ppm) as image:
            image.load()
            if image.getbbox() is None:
                raise MonitorError("screendump gave a black picture (xemu draws outside QEMU's console)")
            image.save(out, "PNG")
            return image.width, image.height
