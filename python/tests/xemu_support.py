# SPDX-License-Identifier: GPL-3.0-or-later
"""Synthetic stand-ins for a running xemu: a fake GDB stub (QEMU's remote protocol, all-stop) and a fake QEMU monitor,
each on a localhost port, plus fake copies of the emulator folder. Nothing here holds or reads game data: the
"memory" is a bytearray of our own numbers."""

from __future__ import annotations

import contextlib
import re
import socket
import threading
import time
from collections.abc import Callable, Iterator
from pathlib import Path

#: The i386 core registers in the order QEMU's `g` packet sends them.
I386 = ("eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi", "eip", "eflags", "cs", "ss", "ds", "es", "fs", "gs")


def eventually(condition: Callable[[], bool], seconds: float = 2.0) -> bool:
    """Whether `condition` holds within `seconds`: the fake stub runs in its own thread, so a `c` the client sent is
    seen there a moment later (the client does not wait for it, as with QEMU, which answers `c` only when it stops)."""
    deadline = time.monotonic() + seconds
    while not condition():
        if time.monotonic() > deadline:
            return False
        time.sleep(0.005)
    return True


def checksum(data: bytes) -> bytes:
    """The two hex digits that end a packet."""
    return f"{sum(data) % 256:02x}".encode("ascii")


class FakeGdbStub:
    """A GDB stub that behaves like QEMU's in all-stop mode: the guest stops when a client attaches, any byte sent
    while it runs stops it (an interrupt, 0x03, answers with a stop reply), `c` runs it with no reply until it stops,
    and a breakpoint or watchpoint "hits" when the test calls `hit`. Memory is a sparse dict of bytes at addresses
    from `base`; reads outside `size` answer `E14`."""

    def __init__(
        self, base: int = 0x00010000, size: int = 0x10000, ack: bool = True, attach_notice: bool = False
    ) -> None:
        self.base = base
        self.size = size
        self.memory = bytearray(size)
        self.registers = dict.fromkeys(I386, 0)
        self.registers["eip"] = 0x00011000
        self.registers["esp"] = 0x0001F000
        self.running = False
        self.ack = ack
        self.attach_notice = attach_notice  # a running guest stopped by the attach sends `T02` unasked, as QEMU does
        self.no_ack = False
        self.breakpoints: set[tuple[int, int, int]] = set()
        self.packets: list[str] = []
        self.connections = 0
        self._server = socket.create_server(("127.0.0.1", 0))
        self._server.settimeout(0.1)  # before the thread starts: a close can then never beat it
        self.port: int = self._server.getsockname()[1]
        self._client: socket.socket | None = None
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._serve, daemon=True)
        self._thread.start()

    # -- the test's handles

    def poke(self, address: int, data: bytes) -> None:
        """Put bytes into the fake memory."""
        offset = address - self.base
        self.memory[offset : offset + len(data)] = data

    def peek(self, address: int, length: int) -> bytes:
        """Bytes of the fake memory."""
        offset = address - self.base
        return bytes(self.memory[offset : offset + length])

    def hit(self, reply: str = "T05thread:01;") -> None:
        """Stop the running guest as a breakpoint would and send `reply` (a stop packet) to the client."""
        with self._lock:
            self.running = False
            if self._client is not None:
                self._send(self._client, reply)

    def close(self) -> None:
        """Stop serving."""
        self._stop.set()
        with contextlib.suppress(OSError):
            self._server.close()
        if self._client is not None:
            with contextlib.suppress(OSError):
                self._client.close()
        self._thread.join(timeout=2)

    # -- the protocol

    def _send(self, client: socket.socket, data: str) -> None:
        raw = data.encode("ascii")
        client.sendall(b"$" + raw + b"#" + checksum(raw))

    def _serve(self) -> None:
        while not self._stop.is_set():
            try:
                client, _ = self._server.accept()
            except (TimeoutError, OSError):
                continue
            self.connections += 1
            self._client = client
            was_running, self.running = self.running, False  # QEMU stops the guest when a debugger attaches
            if was_running and self.attach_notice:
                self._send(client, "T02thread:01;")
            self._session(client)
            self._client = None

    def _session(self, client: socket.socket) -> None:
        buffer = b""
        client.settimeout(0.1)
        while not self._stop.is_set():
            try:
                chunk = client.recv(4096)
            except TimeoutError:
                continue
            except OSError:
                return
            if not chunk:
                return
            buffer += chunk
            while buffer:
                first = buffer[:1]
                if first in (b"+", b"-"):
                    buffer = buffer[1:]
                    continue
                if first == b"\x03" or self.running:
                    buffer = buffer[1:]
                    with self._lock:
                        if self.running:
                            self.running = False
                            self._send(client, "T02thread:01;")
                    continue
                if first != b"$":
                    buffer = buffer[1:]
                    continue
                match = re.match(rb"\$([^#]*)#([0-9a-fA-F]{2})", buffer)
                if match is None:
                    break
                buffer = buffer[match.end() :]
                if self.ack and not self.no_ack:
                    client.sendall(b"+")
                packet = match.group(1).decode("ascii")
                self.packets.append(packet)
                with self._lock:
                    reply = self._answer(packet)
                    if reply is not None:
                        self._send(client, reply)

    def _answer(self, packet: str) -> str | None:
        """The reply to one packet, or None for `c` (no reply until the guest stops)."""
        if packet == "QStartNoAckMode":
            if not self.ack:
                return ""
            self.no_ack = True
            return "OK"
        if packet.startswith("qSupported"):
            return "PacketSize=1000;QStartNoAckMode+" if self.ack else "PacketSize=1000"
        if packet == "?":
            return "T05thread:01;"
        if packet == "g":
            return "".join(self.registers[name].to_bytes(4, "little").hex() for name in I386) + "00" * 108
        if packet.startswith("P"):
            number, value = packet[1:].split("=")
            self.registers[I386[int(number, 16)]] = int.from_bytes(bytes.fromhex(value), "little")
            return "OK"
        if packet.startswith("m"):
            address, length = (int(x, 16) for x in packet[1:].split(","))
            if address < self.base or address + length > self.base + self.size:
                return "E14"
            return self.peek(address, length).hex()
        if packet.startswith("M"):
            head, data = packet[1:].split(":")
            address, length = (int(x, 16) for x in head.split(","))
            if address < self.base or address + length > self.base + self.size:
                return "E14"
            self.poke(address, bytes.fromhex(data))
            return "OK"
        if packet[:1] in ("Z", "z") and packet[1:2] in "01234":
            kind, where, size = packet[1:].split(",")
            key = (int(kind), int(where, 16), int(size, 16))
            if packet[0] == "Z":
                self.breakpoints.add(key)
            else:
                self.breakpoints.discard(key)
            return "OK"
        if packet == "c":
            self.running = True
            return None
        if packet == "s":
            self.registers["eip"] += 1
            return "T05thread:01;"
        return ""


class FakeMonitor:
    """A QEMU human monitor on a localhost port: it greets, echoes each command as QEMU's readline does (with an
    escape sequence), answers the few commands the tools send and ends every answer with the `(qemu) ` prompt."""

    def __init__(self, stub: FakeGdbStub | None = None, version: str = "8.2.50 (xemu-v0.8.110)") -> None:
        self.stub = stub
        self.version = version
        self.commands: list[str] = []
        self.snapshots: list[str] = []
        self.paused = False
        self.screendump = True  # xemu 0.8.139 has no `screendump`: "unknown command"
        self._server = socket.create_server(("127.0.0.1", 0))
        self._server.settimeout(0.1)  # before the thread starts: a close can then never beat it
        self.port: int = self._server.getsockname()[1]
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._serve, daemon=True)
        self._thread.start()

    def _running(self) -> bool:
        return self.stub.running if self.stub is not None else not self.paused

    def _answer(self, command: str) -> str:
        words = command.split()
        if command == "info status":
            return f"VM status: {'running' if self._running() else 'paused'}\r\n"
        if command == "info version":
            return f"{self.version}\r\n"
        if command == "stop":
            if self.stub is not None:
                self.stub.running = False
            self.paused = True
            return ""
        if command == "cont":
            if self.stub is not None:
                self.stub.running = True
            self.paused = False
            return ""
        if words[:1] == ["screendump"] and not self.screendump:
            return "unknown command: 'screendump'\r\n"
        if words[:1] == ["screendump"] and len(words) >= 2:
            path = Path(words[1].strip('"'))
            # A 2 x 1 PPM: the format QEMU writes when no -f is given.
            path.write_bytes(b"P6\n2 1\n255\n" + bytes([10, 20, 30, 40, 50, 60]))
            return ""
        if words[:1] == ["savevm"] and len(words) == 2:
            self.snapshots.append(words[1])
            return ""
        if words[:1] == ["loadvm"] and len(words) == 2:
            return "" if words[1] in self.snapshots else f"Error: Snapshot '{words[1]}' does not exist\r\n"
        if command == "info snapshots":
            if not self.snapshots:
                return "There is no snapshot available.\r\n"
            rows = "".join(
                f"--        {name:<20}  1.2 MiB 2026-10-25 10:00:00 00:00:10.000          0\r\n"
                for name in self.snapshots
            )
            header = "ID        TAG               VM SIZE                DATE     VM CLOCK     ICOUNT"
            return f"List of snapshots present on all disks:\r\n{header}\r\n{rows}"
        return f"unknown command: '{words[0] if words else ''}'\r\n"

    def _serve(self) -> None:
        while not self._stop.is_set():
            try:
                client, _ = self._server.accept()
            except (TimeoutError, OSError):
                continue
            with client:
                client.sendall(b"QEMU 8.2.50 monitor - type 'help' for more information\r\n(qemu) ")
                buffer = b""
                client.settimeout(0.1)
                while not self._stop.is_set():
                    try:
                        chunk = client.recv(4096)
                    except TimeoutError:
                        continue
                    except OSError:
                        break
                    if not chunk:
                        break
                    buffer += chunk
                    while b"\n" in buffer:
                        line, buffer = buffer.split(b"\n", 1)
                        command = line.decode("ascii").strip()
                        self.commands.append(command)
                        echo = command.encode("ascii") + b"\x1b[K\r\n"
                        client.sendall(echo + self._answer(command).encode("ascii") + b"(qemu) ")

    def close(self) -> None:
        """Stop serving."""
        self._stop.set()
        with contextlib.suppress(OSError):
            self._server.close()
        self._thread.join(timeout=2)


@contextlib.contextmanager
def fake_xemu(ack: bool = True) -> Iterator[tuple[FakeGdbStub, FakeMonitor]]:
    """A fake stub and a monitor that drives it, closed afterwards."""
    stub = FakeGdbStub(ack=ack)
    monitor = FakeMonitor(stub)
    try:
        yield stub, monitor
    finally:
        monitor.close()
        stub.close()


def make_copy(root: Path, name: str, gdb: int, monitor: int) -> Path:
    """A fake xemu copy: a folder with xemu.exe (empty), its xemu.toml and coney-copy.toml naming its ports."""
    folder = root / name
    folder.mkdir(parents=True)
    (folder / "xemu.exe").write_bytes(b"")
    (folder / "xemu.toml").write_text("[general]\nshow_welcome = false\n", encoding="utf-8")
    (folder / "coney-copy.toml").write_text(f"gdb_port = {gdb}\nmonitor_port = {monitor}\n", encoding="utf-8")
    return folder


def make_install(root: Path, portable: bool = True) -> Path:
    """A synthetic xemu install: an empty xemu.exe and a dll, and the user's own files (stand-ins of a few bytes)
    named by its xemu.toml: a boot ROM and BIOS beside the program, a disk image and an EEPROM in a subfolder."""
    folder = root / "xemu-install"
    (folder / "console").mkdir(parents=True)
    (folder / "xemu.exe").write_bytes(b"MZ")
    (folder / "SDL2.dll").write_bytes(b"MZ")
    (folder / "mcpx.bin").write_bytes(b"\x01" * 4)
    (folder / "bios.exe").write_bytes(b"\x02" * 4)  # a console file with a program's suffix must still be left alone
    (folder / "console" / "xbox_hdd.qcow2").write_bytes(b"QFI\xfb")
    (folder / "console" / "eeprom.bin").write_bytes(b"\x03" * 4)
    settings = (
        "[general]\nshow_welcome = true\n\n[sys.files]\n"
        f"bootrom_path = '{(folder / 'mcpx.bin').as_posix()}'\n"
        "flashrom_path = 'bios.exe'\n"
        "hdd_path = 'console/xbox_hdd.qcow2'\n"
        "eeprom_path = 'console/eeprom.bin'\n"
        "\n[display.quality]\nsurface_scale = 3\n"
    )
    (folder / ("xemu.toml" if portable else "elsewhere.toml")).write_text(settings, encoding="utf-8")
    return folder
