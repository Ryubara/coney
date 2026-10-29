# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.xemu_claims: claiming, handing a claim back, stale takeover, release, and the checks that keep
an agent off a copy it has not claimed. The process layer is a fake; no xemu runs."""

import threading
from pathlib import Path

import pytest
from xemu_support import make_copy

from coney_tools import pcsx2_proc, xemu_claims
from coney_tools.pcsx2_claims import ClaimError
from coney_tools.xemu_claims import Registry

NOW = 1_800_000_000.0


class FakeProcesses:
    """The xemu processes of the machine (pid to executable) and the other live pids."""

    def __init__(self) -> None:
        self.xemu: dict[int, Path] = {}
        self.alive: set[int] = set()
        self.terminated: list[int] = []
        self.ports: set[int] = set()

    def terminate(self, pid: int, wait: float = 15.0) -> bool:
        self.terminated.append(pid)
        self.xemu.pop(pid, None)
        return True


@pytest.fixture
def procs(monkeypatch: pytest.MonkeyPatch) -> FakeProcesses:
    fake = FakeProcesses()
    monkeypatch.setattr(xemu_claims, "xemu_processes", lambda: dict(fake.xemu))
    monkeypatch.setattr(xemu_claims, "is_xemu", lambda pid: pid in fake.xemu)
    monkeypatch.setattr(xemu_claims, "port_open", lambda port: port in fake.ports)
    monkeypatch.setattr(pcsx2_proc, "pid_alive", lambda pid: pid in fake.xemu or pid in fake.alive)
    monkeypatch.setattr(pcsx2_proc, "terminate", fake.terminate)
    return fake


@pytest.fixture
def registry(tmp_path: Path, procs: FakeProcesses) -> Registry:
    make_copy(tmp_path / "root", "xemu", 1234, 4444)
    make_copy(tmp_path / "root", "xemu-b", 1235, 4445)
    (tmp_path / "root" / "xemu-notacopy").mkdir()
    return Registry(tmp_path / "root", tmp_path / "claims", clock=lambda: NOW)


def test_the_first_free_copy_is_claimed_with_its_ports(registry: Registry) -> None:
    claim, took_over = registry.claim("engineer-1")
    assert (claim.copy, claim.gdb_port, claim.monitor_port, took_over) == ("xemu", 1234, 4444, None)
    assert registry.claim("engineer-2")[0].copy == "xemu-b"


def test_a_holder_gets_its_claim_back(registry: Registry) -> None:
    first, _ = registry.claim("engineer-1")
    assert registry.claim("engineer-1")[0] == first


def test_nothing_free_names_the_holders(registry: Registry) -> None:
    registry.claim("a")
    registry.claim("b")
    with pytest.raises(ClaimError, match="xemu: a, xemu-b: b"):
        registry.claim("c")


def test_a_copy_running_without_a_claim_is_not_handed_out(registry: Registry, procs: FakeProcesses) -> None:
    procs.xemu[50] = registry.copy("xemu").path / "xemu.exe"
    assert registry.claim("a")[0].copy == "xemu-b"
    with pytest.raises(ClaimError, match="without a claim"):
        registry.claim("b", "xemu")


def test_a_stale_claim_is_taken_over(registry: Registry, procs: FakeProcesses) -> None:
    registry.claim("gone", "xemu", holder_pid=99)  # pid 99 is dead
    claim, took_over = registry.claim("new", "xemu")
    assert claim.agent == "new" and took_over is not None and took_over.agent == "gone"


def test_a_live_holder_keeps_its_claim(registry: Registry, procs: FakeProcesses) -> None:
    procs.alive.add(98)
    registry.claim("alive", "xemu", holder_pid=98)
    with pytest.raises(ClaimError):
        registry.claim("other", "xemu")


def test_a_served_port_keeps_a_claim(registry: Registry, procs: FakeProcesses) -> None:
    registry.claim("by-hand", "xemu", holder_pid=97)
    procs.ports.add(4444)
    assert not registry.status()[0].stale


def test_only_one_of_many_racers_wins(tmp_path: Path, registry: Registry) -> None:
    results: list[str] = []
    barrier = threading.Barrier(8)

    def attempt(n: int) -> None:
        mine = Registry(tmp_path / "root", tmp_path / "claims", clock=lambda: NOW)
        barrier.wait()
        try:
            mine.claim(f"racer{n}", "xemu")
            results.append("won")
        except ClaimError:
            results.append("lost")

    threads = [threading.Thread(target=attempt, args=(n,)) for n in range(8)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    assert results.count("won") == 1


def test_release_closes_the_copy_xemu(registry: Registry, procs: FakeProcesses) -> None:
    claim, _ = registry.claim("a", "xemu")
    procs.xemu[60] = registry.copy("xemu").path / "xemu.exe"
    registry.record_pid(claim, 60)
    assert registry.read("xemu") is not None and registry.read("xemu").xemu_pid == 60  # type: ignore[union-attr]
    released = registry.release("a")
    assert [(c.copy, closed) for c, closed in released] == [("xemu", True)] and procs.terminated == [60]
    assert registry.read("xemu") is None


def test_another_agent_claim_needs_force(registry: Registry, procs: FakeProcesses) -> None:
    procs.alive.add(5)
    registry.claim("a", "xemu", holder_pid=5)
    with pytest.raises(ClaimError, match="held by a"):
        registry.release("b", "xemu")
    assert registry.release("b", "xemu", force=True)[0][0].agent == "a"


def test_require_claim_refuses_another_agent_copy(registry: Registry, procs: FakeProcesses) -> None:
    procs.alive.add(5)
    registry.claim("a", "xemu", holder_pid=5)
    with pytest.raises(ClaimError, match="held by a, not b"):
        registry.require_claim("b", "xemu")
    with pytest.raises(ClaimError, match="holds no xemu copy"):
        registry.require_claim("b")
    claim, copy = registry.require_claim("a")
    assert claim.agent == "a" and copy.gdb == 1234


def test_a_stale_claim_of_your_own_must_be_made_again(registry: Registry) -> None:
    registry.claim("a", "xemu", holder_pid=99)
    with pytest.raises(ClaimError, match=r"stale.*xemu launch --agent a") as refusal:
        registry.require_claim("a")
    assert "xemu release --agent a" in str(refusal.value)


def test_ports_are_checked_against_the_claim(registry: Registry, procs: FakeProcesses) -> None:
    procs.alive.add(5)
    registry.claim("a", "xemu", holder_pid=5)
    assert registry.require_ports(["a"], 1234, 4444).copy == "xemu"
    with pytest.raises(ClaimError, match="held by a, not b"):
        registry.require_ports(["b"], 1234, None)
    with pytest.raises(ClaimError, match="different copies"):
        registry.require_ports(["a"], 1234, 4445)
    with pytest.raises(ClaimError, match="no xemu copy"):
        registry.require_ports(["a"], 9999, None)


def test_no_copies_says_how_to_make_them(tmp_path: Path, procs: FakeProcesses) -> None:
    with pytest.raises(ClaimError, match="setup-copies"):
        Registry(tmp_path / "none", tmp_path / "claims").copies()


def test_a_caller_own_claim_is_checked_without_probing_the_other_copies(
    registry: Registry, procs: FakeProcesses, monkeypatch: pytest.MonkeyPatch
) -> None:
    # On Windows a probe of a port nobody serves takes half a second, and every MCP call checks the caller's claim.
    procs.alive.add(5)
    registry.claim("a", "xemu", holder_pid=5)
    probed: list[int] = []

    def probe(port: int) -> bool:
        probed.append(port)
        return False

    monkeypatch.setattr(xemu_claims, "port_open", probe)
    registry.require_claim("a")
    assert registry.held_by("a")[0].copy == "xemu"
    assert set(probed) <= {1234, 4444}
