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


def _serve(server: socket.socket, version: str, status: int) -> None:
    """Answer messages until the server closes: version, status, and zeros for every read."""
    while True:
        try:
            connection, _ = server.accept()
        except OSError:
            return
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
    threading.Thread(target=_serve, args=(server, version, status), daemon=True).start()
    try:
        yield server.getsockname()[1]
    finally:
        server.close()
