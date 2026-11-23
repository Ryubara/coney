# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the helpers of xemu_support that give fake copies their ports: a copy beside the fake stub and monitor
must never be handed a port one of them listens on, or `discover_copies` refuses the pair (issue #1669)."""

from pathlib import Path

import pytest
from xemu_support import FakeGdbStub, FakeMonitor, fake_copies, spare_ports

from coney_tools import xemu_copies

#: The monitor port the fixture of test_xemu_mcp used to give its second copy as a constant; the OS hands the fakes
#: ports from this range too (Windows' dynamic range starts at 49152), which is how a run failed on `main`.
OLD_CONSTANT_PORT = 61002


def test_spare_ports_are_distinct_and_free_of_the_live_fakes() -> None:
    stub, monitor = FakeGdbStub(), FakeMonitor()
    try:
        spare = spare_ports(8)
        assert len(set(spare)) == 8
        assert stub.port not in spare and monitor.port not in spare
    finally:
        monitor.close()
        stub.close()


def test_a_fake_on_the_old_constant_port_does_not_clash_with_the_second_copy(tmp_path: Path) -> None:
    try:
        monitor = FakeMonitor(port=OLD_CONSTANT_PORT)
    except OSError:
        pytest.skip(f"port {OLD_CONSTANT_PORT} is in use on this machine")
    stub = FakeGdbStub()
    try:
        fake_copies(tmp_path / "root", stub, monitor)
        copies = xemu_copies.discover_copies(tmp_path / "root")
        assert [copy.name for copy in copies] == ["xemu", "xemu-b"]
        assert copies[0].monitor == OLD_CONSTANT_PORT and copies[1].monitor != OLD_CONSTANT_PORT
    finally:
        monitor.close()
        stub.close()
