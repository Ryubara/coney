# SPDX-License-Identifier: GPL-3.0-or-later
"""A client for GDB's remote serial protocol as QEMU's stub speaks it, for the Xbox build running in xemu
(docs/guides/coney-tools-xemu.md#gdb).

xemu is QEMU, so `-gdb tcp:127.0.0.1:<port>` serves the guest (32-bit x86) over the protocol GDB uses: packets
`$<data>#<checksum>`, each acknowledged with `+` until both sides agree on `QStartNoAckMode`. Guest virtual addresses
are the XBE's (base 0x00010000), so an address from a research page needs no translation.

QEMU's stub is **all-stop**, and three of its habits shape this client:

* it stops the guest when a debugger attaches, so `connect` leaves the game paused (the caller resumes it);
* while the guest runs it reads nothing but treats any byte as a request to stop: a packet sent then is lost and the
  guest stops instead. So every request here first makes sure the guest is stopped (`ensure_stopped`), and a read made
  while the game ran resumes it afterwards when asked (`resume=True`);
* `c` (continue) has no reply until the guest stops again (a breakpoint, a watchpoint, an interrupt); that stop reply
  arrives later and is picked up by `poll_stop` or `wait_stop`.

Only one debugger may attach to the stub at a time: a second connection is refused or waits until the first closes.
"""

from __future__ import annotations

import re
import socket
import time
from dataclasses import dataclass
from types import TracebackType

from coney_tools.config import ConfigError

#: The i386 core registers, in the order of QEMU's `g` reply (each 32 bits, little-endian), and the register number
#: `P` takes.
I386_REGISTERS: tuple[str, ...] = (
    "eax",
    "ecx",
    "edx",
    "ebx",
    "esp",
    "ebp",
    "esi",
    "edi",
    "eip",
    "eflags",
    "cs",
    "ss",
    "ds",
    "es",
    "fs",
    "gs",
)

#: The bytes asked for in one `m` packet: QEMU's packet buffer holds 4 KiB, and each byte comes back as two hex digits.
READ_CHUNK = 0x400
#: The bytes written by one `M` packet.
WRITE_CHUNK = 0x400

#: The kinds of `Z`/`z` points: a software breakpoint, a hardware one, and the three watchpoints.
POINT_KINDS: dict[str, int] = {"break": 0, "hardware": 1, "write": 2, "read": 3, "access": 4}

_STOP_FIELD = re.compile(r"([^:;]+):([^;]*);")
#: The head of a stop reply: a signal letter and two hex digits (`T05...`, `S02`, `W00`).
_STOP_REPLY = re.compile(r"[STWX][0-9a-fA-F]{2}")


class GdbError(ConfigError):
    """The stub refused a request, did not answer in time, or cannot be reached; the message says which."""


@dataclass(frozen=True)
class Stop:
    """A stop reply: the signal (5 a trap: a breakpoint, a watchpoint or a step; 2 an interrupt), the watchpoint that
    fired (`watch`, `rwatch` or `awatch` and its address) and whether the guest has exited."""

    signal: int
    watch: tuple[str, int] | None = None
    exited: bool = False
    text: str = ""


def checksum(data: bytes) -> str:
    """The two hex digits after `#`: the sum of the data bytes, modulo 256."""
    return f"{sum(data) % 256:02x}"


def encode_packet(data: str) -> bytes:
    """`data` framed as a packet. The commands this client sends hold no `$`, `#`, `}` or `*`, so nothing is
    escaped."""
    raw = data.encode("ascii")
    return b"$" + raw + b"#" + checksum(raw).encode("ascii")


def decode_rle(data: str) -> str:
    """Expand the protocol's run-length encoding: `X*n` is X followed by ord(n) - 29 more copies of it."""
    out: list[str] = []
    index = 0
    while index < len(data):
        char = data[index]
        if char == "*" and out and index + 1 < len(data):
            out.append(out[-1] * (ord(data[index + 1]) - 29))
            index += 2
            continue
        out.append(char)
        index += 1
    return "".join(out)


def parse_stop(text: str) -> Stop:
    """A stop reply (`S05`, `T05thread:01;watch:addr;`, `W00`, `X09`) as a Stop. Raises GdbError for anything else."""
    if not text or text[0] not in "STWX" or len(text) < 3:
        raise GdbError(f"not a stop reply: {text!r}")
    try:
        signal = int(text[1:3], 16)
    except ValueError as error:
        raise GdbError(f"not a stop reply: {text!r}") from error
    watch = None
    if text[0] == "T":
        for key, value in _STOP_FIELD.findall(text[3:]):
            if key in ("watch", "rwatch", "awatch"):
                watch = (key, int(value, 16))
    return Stop(signal, watch, exited=text[0] in "WX", text=text)


class GdbClient:
    """One connection to a GDB stub on localhost. Use it as a context manager, or call `connect` and `close`."""

    def __init__(self, port: int, host: str = "127.0.0.1", timeout: float = 5.0) -> None:
        """`timeout` is how long a reply may take, in seconds."""
        self.port = port
        self.host = host
        self.timeout = timeout
        self.running = False
        self.last_stop: Stop | None = None
        self._socket: socket.socket | None = None
        self._buffer = b""
        self._ack = True

    def __enter__(self) -> GdbClient:
        self.connect()
        return self

    def __exit__(
        self, kind: type[BaseException] | None, error: BaseException | None, trace: TracebackType | None
    ) -> None:
        self.close()

    # -- the connection

    def connect(self) -> Stop:
        """Attach to the stub (QEMU stops the guest), turn acknowledgements off when it can, and return why the guest
        is stopped. Raises GdbError when nothing listens on the port or the stub does not answer."""
        try:
            self._socket = socket.create_connection((self.host, self.port), timeout=self.timeout)
        except OSError as error:
            raise GdbError(f"no GDB stub on {self.host}:{self.port} ({error}); is xemu running with -gdb?") from error
        self._socket.settimeout(self.timeout)
        supported = self._request_skipping_stops("qSupported:swbreak+;hwbreak+")
        if "QStartNoAckMode+" in supported and self._request_skipping_stops("QStartNoAckMode") == "OK":
            self._ack = False
        stop = parse_stop(self._request("?"))
        self.running = False
        self.last_stop = stop
        return stop

    def close(self) -> None:
        """Detach. The guest keeps the state it is in (QEMU resumes it only on `c`)."""
        if self._socket is not None:
            try:
                self._socket.close()
            finally:
                self._socket = None

    @property
    def connected(self) -> bool:
        """Whether the socket is open."""
        return self._socket is not None

    # -- framing

    def _sock(self) -> socket.socket:
        if self._socket is None:
            raise GdbError(f"not connected to the GDB stub on port {self.port}")
        return self._socket

    def _send(self, data: str) -> None:
        """Send one packet; in acknowledged mode wait for its `+` (and send it again on a `-`)."""
        packet = encode_packet(data)
        for _ in range(3):
            self._sock().sendall(packet)
            if not self._ack:
                return
            byte = self._read_byte(self.timeout)
            if byte == b"+":
                return
            if byte != b"-":
                self._buffer = byte + self._buffer
                return
        raise GdbError(f"the stub on port {self.port} refused the packet {data[:40]!r} three times")

    def _read_byte(self, timeout: float) -> bytes:
        """One byte from the stub, waiting up to `timeout` seconds; GdbError when none comes."""
        if not self._buffer:
            self._fill(timeout)
        byte, self._buffer = self._buffer[:1], self._buffer[1:]
        return byte

    def _fill(self, timeout: float) -> None:
        """Read what the stub has sent, waiting up to `timeout` seconds for the first byte."""
        sock = self._sock()
        sock.settimeout(timeout)
        try:
            chunk = sock.recv(65536)
        except TimeoutError as error:
            raise GdbError(f"the GDB stub on port {self.port} did not answer within {timeout:g} s") from error
        except OSError as error:
            self.close()
            raise GdbError(f"the GDB stub on port {self.port} closed the connection ({error})") from error
        if not chunk:
            self.close()
            raise GdbError(f"the GDB stub on port {self.port} closed the connection")
        self._buffer += chunk

    def _read_packet(self, timeout: float) -> str:
        """The next packet's data (acknowledged when the mode asks for it); stray acknowledgements are skipped."""
        deadline = time.monotonic() + timeout
        while True:
            start = self._buffer.find(b"$")
            if start >= 0:
                end = self._buffer.find(b"#", start)
                if end >= 0 and len(self._buffer) >= end + 3:
                    raw = self._buffer[start + 1 : end]
                    sent = self._buffer[end + 1 : end + 3].decode("ascii", "replace")
                    self._buffer = self._buffer[end + 3 :]
                    if self._ack:
                        self._sock().sendall(b"+" if sent.lower() == checksum(raw) else b"-")
                    if sent.lower() != checksum(raw):
                        continue
                    return decode_rle(raw.decode("ascii", "replace"))
            left = deadline - time.monotonic()
            if left <= 0:
                raise GdbError(f"the GDB stub on port {self.port} did not answer within {timeout:g} s")
            self._fill(left)

    def _request(self, data: str) -> str:
        """Send a packet and return the reply's data."""
        self._send(data)
        return self._read_packet(self.timeout)

    def _request_skipping_stops(self, data: str) -> str:
        """`_request` for the first packets of a connection, whose reply is not a stop reply: QEMU stops a running guest
        when a debugger attaches and sends the stop (`T02`) on its own, ahead of the first reply (seen on xemu 0.8.139),
        so a stop reply arriving here is that notice, not the answer, and is skipped."""
        self._send(data)
        for _ in range(4):
            reply = self._read_packet(self.timeout)
            if not _STOP_REPLY.fullmatch(reply[:3]):
                return reply
        raise GdbError(f"the stub on port {self.port} answered {data[:40]!r} with stop replies only")

    # -- running and stopping

    def ensure_stopped(self) -> bool:
        """Stop the guest if it runs (a stop reply that already came counts); return whether it was running."""
        if not self.running:
            return False
        if self.poll_stop() is not None:
            return False
        self.interrupt()
        return True

    def request(self, data: str) -> str:
        """Send one command to a stopped guest and return the reply; an `E..` reply raises GdbError."""
        if self.running:
            raise GdbError("the guest is running: stop it first (the stub reads nothing while it runs)")
        reply = self._request(data)
        if re.fullmatch(r"E[0-9a-fA-F]{2}", reply):
            raise GdbError(f"the stub refused {data[:40]!r} ({reply})")
        return reply

    def interrupt(self) -> Stop:
        """Stop the running guest (the protocol's 0x03) and return the stop reply."""
        self._sock().sendall(b"\x03")
        stop = parse_stop(self._read_packet(self.timeout))
        self.running = False
        self.last_stop = stop
        return stop

    def cont(self) -> None:
        """Let the guest run; its next stop is read by `poll_stop` or `wait_stop`."""
        if self.running:
            return
        self._send("c")
        self.running = True

    def step(self) -> Stop:
        """Run one instruction and return the stop reply."""
        self.ensure_stopped()
        stop = parse_stop(self._request("s"))
        self.last_stop = stop
        return stop

    def poll_stop(self) -> Stop | None:
        """The stop reply of a running guest when one has arrived, else None (never waits)."""
        return self.wait_stop(0.0) if self.running else None

    def wait_stop(self, timeout: float) -> Stop | None:
        """Wait up to `timeout` seconds for the running guest to stop; the stop reply, or None while it runs."""
        if not self.running:
            return self.last_stop
        try:
            if timeout <= 0:
                self._sock().setblocking(False)
                try:
                    self._buffer += self._sock().recv(65536)
                except (BlockingIOError, InterruptedError):
                    pass
                finally:
                    self._sock().setblocking(True)
                if b"#" not in self._buffer:
                    return None
                text = self._read_packet(self.timeout)
            else:
                text = self._read_packet(timeout)
        except GdbError:
            if self.connected:
                return None
            raise
        stop = parse_stop(text)
        self.running = False
        self.last_stop = stop
        return stop

    # -- memory and registers

    def read_memory(self, address: int, length: int, resume: bool = False) -> bytes:
        """`length` bytes of guest memory at virtual `address`. A running guest is stopped for the read and, with
        `resume`, let run again after it. Raises GdbError naming the address the stub refused."""
        was_running = self.ensure_stopped()
        try:
            out = bytearray()
            while len(out) < length:
                here = address + len(out)
                size = min(READ_CHUNK, length - len(out))
                try:
                    reply = self.request(f"m{here:x},{size:x}")
                except GdbError as error:
                    raise GdbError(f"cannot read 0x{here:08x} ({size} bytes): {error}") from error
                data = bytes.fromhex(reply)
                if not data:
                    raise GdbError(f"cannot read 0x{here:08x}: the stub returned nothing")
                out += data
            return bytes(out[:length])
        finally:
            if was_running and resume:
                self.cont()

    def write_memory(self, address: int, data: bytes, resume: bool = False) -> None:
        """Write `data` at virtual `address`, stopping a running guest for it (with `resume`, letting it run on)."""
        was_running = self.ensure_stopped()
        try:
            for offset in range(0, len(data), WRITE_CHUNK):
                part = data[offset : offset + WRITE_CHUNK]
                try:
                    self.request(f"M{address + offset:x},{len(part):x}:{part.hex()}")
                except GdbError as error:
                    raise GdbError(f"cannot write 0x{address + offset:08x}: {error}") from error
        finally:
            if was_running and resume:
                self.cont()

    def registers(self, resume: bool = False) -> dict[str, int]:
        """The i386 core registers by name (the x87 and SSE registers after them are left out)."""
        was_running = self.ensure_stopped()
        try:
            reply = self.request("g")
        finally:
            if was_running and resume:
                self.cont()
        raw = bytes.fromhex(reply[: 8 * len(I386_REGISTERS)])
        if len(raw) < 4 * len(I386_REGISTERS):
            raise GdbError(f"the register reply is too short ({len(raw)} bytes): not an i386 target?")
        return {name: int.from_bytes(raw[4 * i : 4 * i + 4], "little") for i, name in enumerate(I386_REGISTERS)}

    def write_register(self, name: str, value: int) -> None:
        """Set one core register (`P`), the guest stopped."""
        if name not in I386_REGISTERS:
            raise GdbError(f"no register {name!r}; the i386 core registers are {', '.join(I386_REGISTERS)}")
        self.ensure_stopped()
        number = I386_REGISTERS.index(name)
        self.request(f"P{number:x}={(value & 0xFFFFFFFF).to_bytes(4, 'little').hex()}")

    # -- breakpoints and watchpoints

    @staticmethod
    def point_packet(prefix: str, kind: str, address: int, length: int) -> str:
        """The `Z` (set) or `z` (remove) packet of a point. Raises GdbError for an unknown kind."""
        if kind not in POINT_KINDS:
            raise GdbError(f"point kind {kind!r}: one of {', '.join(POINT_KINDS)}")
        return f"{prefix}{POINT_KINDS[kind]},{address:x},{length:x}"

    def set_point(self, kind: str, address: int, length: int = 1) -> None:
        """Set a breakpoint (`break`, `hardware`) or a watchpoint (`write`, `read`, `access`) of `length` bytes."""
        self.ensure_stopped()
        if self.request(self.point_packet("Z", kind, address, length)) != "OK":
            raise GdbError(f"the stub does not support {kind} points")

    def remove_point(self, kind: str, address: int, length: int = 1) -> None:
        """Remove a point set with the same kind, address and length."""
        self.ensure_stopped()
        if self.request(self.point_packet("z", kind, address, length)) != "OK":
            raise GdbError(f"the stub does not support {kind} points")
