# SPDX-License-Identifier: GPL-3.0-or-later
"""A client for PINE, the TCP protocol PCSX2 serves for reading and writing a running game's memory.

A message is a little-endian `u32` total size (the four size bytes included) followed by one or more commands, each
an opcode byte and its arguments; the reply is a `u32` size, a result byte (0 for success) and the commands' results
back to back. Batching every read of a sample into one message is what lets a recorder keep up with the game: one
round trip per sample instead of one per field (docs/guides/research-workflow.md#driving-pcsx2).

The client sends only memory reads and writes and the status query. It has no way to save a state: PINE's save
writes a quick-save slot, and the slots belong to whoever made them.
"""

from __future__ import annotations

import socket
import struct
from collections.abc import Sequence
from dataclasses import dataclass
from typing import Protocol

#: PINE opcodes: reads, writes, then the status query. Save and load (9, 10) are deliberately absent.
READ_OPCODES = {1: 0, 2: 1, 4: 2, 8: 3}
WRITE_OPCODES = {1: 4, 2: 5, 4: 6, 8: 7}
OPCODE_STATUS = 0x0F

#: What MsgStatus answers.
STATUS_NAMES = {0: "running", 1: "paused", 2: "shutdown"}

#: The port PCSX2 uses when its ini names none (`PINESlot`).
DEFAULT_PORT = 28011


class PineError(Exception):
    """A PINE failure: no emulator listening, a refused command or a broken connection."""


@dataclass(frozen=True)
class Read:
    """One memory read of `size` bytes (1, 2, 4 or 8) at `address`."""

    address: int
    size: int


@dataclass(frozen=True)
class Write:
    """One memory write of `value`, `size` bytes (1, 2, 4 or 8), at `address`."""

    address: int
    size: int
    value: int


class Memory(Protocol):
    """What the tools need from a running game: batched reads and writes. PineClient is one; tests use fakes."""

    def batch(self, reads: Sequence[Read], writes: Sequence[Write] = ()) -> list[int]:
        """Apply `writes`, then do `reads`, in one exchange; returns each read's value, unsigned."""
        ...


def encode(reads: Sequence[Read], writes: Sequence[Write] = ()) -> bytes:
    """The message for `writes` then `reads`, with its size prefix. Raises PineError for an unsupported size."""
    body = bytearray()
    for write in writes:
        if write.size not in WRITE_OPCODES:
            raise PineError(f"a write of {write.size} bytes is not supported")
        body += struct.pack("<BI", WRITE_OPCODES[write.size], write.address)
        body += write.value.to_bytes(write.size, "little")
    for read in reads:
        if read.size not in READ_OPCODES:
            raise PineError(f"a read of {read.size} bytes is not supported")
        body += struct.pack("<BI", READ_OPCODES[read.size], read.address)
    return struct.pack("<I", len(body) + 4) + bytes(body)


def decode(reply: bytes, reads: Sequence[Read]) -> list[int]:
    """The values of `reads` from a reply's body (the bytes after its size). Raises PineError when it reports failure
    or is short."""
    if not reply or reply[0] != 0:
        raise PineError("PCSX2 refused the command (is a game running?)")
    values, at = [], 1
    for read in reads:
        chunk = reply[at : at + read.size]
        if len(chunk) != read.size:
            raise PineError("PCSX2's reply is shorter than the reads asked for")
        values.append(int.from_bytes(chunk, "little"))
        at += read.size
    return values


class PineClient:
    """A connection to PCSX2's PINE server. PCSX2 serves one client at a time, so close it (or use `with`)."""

    def __init__(self, port: int = DEFAULT_PORT, host: str = "127.0.0.1", timeout: float = 10.0) -> None:
        """Connect to `host:port`. Raises PineError when nothing listens there."""
        try:
            self._socket = socket.create_connection((host, port), timeout=timeout)
        except OSError as error:
            raise PineError(
                f"no PINE server at {host}:{port} ({error}); is PCSX2 running with PINE enabled?"
            ) from error
        self._socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    def __enter__(self) -> PineClient:
        """Use as a context manager that closes the connection."""
        return self

    def __exit__(self, *_: object) -> None:
        """Close the connection."""
        self.close()

    def close(self) -> None:
        """Close the connection; PCSX2 can then serve another client."""
        self._socket.close()

    def _receive(self, count: int) -> bytes:
        """Exactly `count` bytes from the socket. Raises PineError when the emulator goes away."""
        data = bytearray()
        while len(data) < count:
            try:
                chunk = self._socket.recv(count - len(data))
            except OSError as error:
                raise PineError(f"PINE connection failed: {error}") from error
            if not chunk:
                raise PineError("PCSX2 closed the PINE connection")
            data += chunk
        return bytes(data)

    def _exchange(self, message: bytes) -> bytes:
        """Send one message and return the reply's body (the result byte and the results)."""
        try:
            self._socket.sendall(message)
        except OSError as error:
            raise PineError(f"PINE connection failed: {error}") from error
        size = struct.unpack("<I", self._receive(4))[0]
        return self._receive(size - 4)

    def batch(self, reads: Sequence[Read], writes: Sequence[Write] = ()) -> list[int]:
        """Apply `writes`, then do `reads`, in one message; returns each read's value, unsigned."""
        return decode(self._exchange(encode(reads, writes)), reads)

    def status(self) -> str:
        """The emulator's state: `running`, `paused` or `shutdown`."""
        reply = self._exchange(struct.pack("<IB", 5, OPCODE_STATUS))
        if not reply or reply[0] != 0 or len(reply) < 5:
            raise PineError("PCSX2 refused the status query")
        code = struct.unpack_from("<I", reply, 1)[0]
        return STATUS_NAMES.get(code, f"unknown ({code})")
