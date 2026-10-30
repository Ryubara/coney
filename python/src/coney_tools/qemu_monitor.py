# SPDX-License-Identifier: GPL-3.0-or-later
"""A client for QEMU's human monitor on a TCP port, as xemu serves it with
`-monitor tcp:127.0.0.1:<port>,server,nowait` (docs/guides/coney-tools-xemu.md#monitor).

The monitor answers what the GDB stub cannot: whether the guest runs (`info status`) without stopping it, pausing and
resuming (`stop`, `cont`), QEMU's version (xemu's own is in its log), a picture of the screen (`screendump`, which xemu
0.8.139 lacks) and the snapshots that are xemu's save states (`savevm`, `loadvm`, `info snapshots`, kept inside the
copy's hard-disk image). It is a line
protocol: a greeting, then for each command an echo of it, the answer and the prompt `(qemu) `. QEMU's line editor
puts terminal escapes into the echo; they are removed.
"""

from __future__ import annotations

import re
import socket
import time
from types import TracebackType

from coney_tools.config import ConfigError

PROMPT = b"(qemu) "
_ESCAPE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]|\x1b[()][A-Za-z0-9]|\x1b.")
#: Words that open a monitor answer reporting a failure.
_FAILURE = re.compile(r"^(?:Error|error|unknown command|Could not|could not|Invalid|invalid)\b", re.MULTILINE)


class MonitorError(ConfigError):
    """The monitor cannot be reached, did not answer, or reported an error."""


def strip_terminal(text: str) -> str:
    """`text` without terminal escapes and carriage returns."""
    return _ESCAPE.sub("", text).replace("\r", "")


class MonitorClient:
    """One connection to a QEMU monitor on localhost. Use it as a context manager."""

    def __init__(self, port: int, host: str = "127.0.0.1", timeout: float = 5.0) -> None:
        """`timeout` is how long an answer may take, in seconds."""
        self.port = port
        self.host = host
        self.timeout = timeout
        self._socket: socket.socket | None = None

    def __enter__(self) -> MonitorClient:
        self.connect()
        return self

    def __exit__(
        self, kind: type[BaseException] | None, error: BaseException | None, trace: TracebackType | None
    ) -> None:
        self.close()

    def connect(self) -> None:
        """Connect and read the greeting up to the first prompt."""
        try:
            self._socket = socket.create_connection((self.host, self.port), timeout=self.timeout)
        except OSError as error:
            raise MonitorError(
                f"no QEMU monitor on {self.host}:{self.port} ({error}); is xemu running with -monitor?"
            ) from error
        self._until_prompt()

    def close(self) -> None:
        """Close the connection (the guest is not affected)."""
        if self._socket is not None:
            try:
                self._socket.close()
            finally:
                self._socket = None

    @property
    def connected(self) -> bool:
        """Whether the socket is open."""
        return self._socket is not None

    def _until_prompt(self) -> str:
        """What the monitor sends up to its next prompt."""
        if self._socket is None:
            raise MonitorError(f"not connected to the monitor on port {self.port}")
        data = b""
        deadline = time.monotonic() + self.timeout
        while not data.endswith(PROMPT):
            left = deadline - time.monotonic()
            if left <= 0:
                raise MonitorError(f"the monitor on port {self.port} did not answer within {self.timeout:g} s")
            self._socket.settimeout(left)
            try:
                chunk = self._socket.recv(65536)
            except TimeoutError:
                continue
            except OSError as error:
                self.close()
                raise MonitorError(f"the monitor on port {self.port} closed the connection ({error})") from error
            if not chunk:
                self.close()
                raise MonitorError(f"the monitor on port {self.port} closed the connection")
            data += chunk
        return data[: -len(PROMPT)].decode("utf-8", "replace")

    def command(self, line: str) -> str:
        """Send one command and return its answer: the text before the prompt, without the echo of the command."""
        if "\n" in line or "\r" in line:
            raise MonitorError("a monitor command is one line")
        if self._socket is None:
            raise MonitorError(f"not connected to the monitor on port {self.port}")
        self._socket.sendall(line.encode("utf-8") + b"\n")
        lines = strip_terminal(self._until_prompt()).split("\n")
        # The first line is the echo of the command (QEMU's line editor redraws it, so compare without spaces).
        if lines and lines[0].replace(" ", "").endswith(line.replace(" ", "")):
            lines = lines[1:]
        return "\n".join(lines).strip()

    def checked(self, line: str) -> str:
        """`command`, raising MonitorError when the answer reports a failure."""
        answer = self.command(line)
        if _FAILURE.search(answer):
            raise MonitorError(f"`{line}`: {answer}")
        return answer

    def running(self) -> bool:
        """Whether the guest runs (`info status`), asked without stopping it."""
        answer = self.command("info status")
        if "running" in answer:
            return True
        if "paused" in answer or "stopped" in answer or "debug" in answer:
            return False
        raise MonitorError(f"`info status` gave {answer!r}")

    def version(self) -> str:
        """The emulator's version line (`info version`)."""
        return self.checked("info version").splitlines()[0].strip()

    def snapshots(self) -> list[str]:
        """The tags of the snapshots in the copy's disk image (`info snapshots`), in the monitor's order."""
        answer = self.command("info snapshots")
        tags = []
        header = False
        for row in answer.splitlines():
            words = row.split()
            if not words:
                continue
            if words[0] == "ID":
                header = True
                continue
            if header and len(words) >= 2:
                tags.append(words[1])
        return tags
