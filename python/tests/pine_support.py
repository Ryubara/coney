# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared by the tests that need a PCSX2 to talk to: a PINE server that answers the version and the status queries and
reads zeros, on a free local port. No test starts PCSX2."""

from __future__ import annotations

import socket
import struct
import threading
from collections.abc import Iterator
from contextlib import contextmanager

OPCODE_VERSION = 0x08
OPCODE_STATUS = 0x0F


def _serve(server: socket.socket, version: str, status: int, stop: threading.Event) -> None:
    """Answer messages until `stop` is set: version, status, and zeros for every read. `accept` polls with a short
    timeout because closing the socket does not wake a thread blocked in it on Linux: that thread would keep the port
    listening after the test thought the server gone."""
    server.settimeout(0.05)
    while not stop.is_set():
        try:
            connection, _ = server.accept()
        except TimeoutError:
            continue
        except OSError:
            return
        connection.settimeout(None)
        with connection:
            while True:
                head = connection.recv(4, socket.MSG_WAITALL)
                if len(head) < 4:
                    break
                body = connection.recv(struct.unpack("<I", head)[0] - 4, socket.MSG_WAITALL)
                reply = bytearray([0])
                at = 0
                while at < len(body):
                    opcode = body[at]
                    if opcode == OPCODE_VERSION:
                        text = version.encode("ascii") + b"\0"
                        reply += struct.pack("<I", len(text)) + text
                        at += 1
                    elif opcode == OPCODE_STATUS:
                        reply += struct.pack("<I", status)
                        at += 1
                    elif opcode <= 3:
                        reply += bytes(1 << opcode)
                        at += 5
                    else:
                        at += 5 + (1 << (opcode - 4))
                connection.sendall(struct.pack("<I", len(reply) + 4) + bytes(reply))


@contextmanager
def fake_pcsx2(version: str = "PCSX2 v2.9.94-5-gabc1234", status: int = 0) -> Iterator[int]:
    """A PINE server of a PCSX2 that says `version` and `status` (0 running, 1 paused, 2 shut down); yields its port."""
    server = socket.create_server(("127.0.0.1", 0))
    stop = threading.Event()
    thread = threading.Thread(target=_serve, args=(server, version, status, stop), daemon=True)
    thread.start()
    try:
        yield server.getsockname()[1]
    finally:
        stop.set()
        thread.join(timeout=5)
        server.close()


@contextmanager
def refused_port() -> Iterator[int]:
    """A local port on which a connection is refused for as long as the block runs: a socket is bound to it but never
    listens, and, bound without SO_REUSEADDR, it keeps any other server (another test, another program) off the port.
    A port taken from a server that was then closed is free for anyone to take again."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as holder:
        holder.bind(("127.0.0.1", 0))
        yield holder.getsockname()[1]
