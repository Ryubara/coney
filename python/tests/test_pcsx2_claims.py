# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.pcsx2_claims and the claim commands: copy discovery, atomic claims, stale takeover, release,
status, the commands that start PCSX2 refusing without a claim, and key parsing. No PCSX2 and no Win32 calls: the
process layer is replaced by a fake."""

import json
import threading
from datetime import UTC, datetime
from pathlib import Path

import pytest

from coney_tools import pcsx2_claims, pcsx2_claims_cli, pcsx2_cli, pcsx2_proc
from coney_tools.config import ConfigError
from coney_tools.pcsx2_claims import ClaimError, Registry

NOW = 1_800_000_000.0


class FakeProcesses:
    """The processes of the machine: `pcsx2` maps pid to executable (all PCSX2s); `alive` lists other live pids."""

    def __init__(self) -> None:
        self.pcsx2: dict[int, Path] = {}
        self.alive: set[int] = set()
        self.terminated: list[int] = []

    def terminate(self, pid: int, wait: float = 15.0) -> bool:
        self.terminated.append(pid)
        self.pcsx2.pop(pid, None)
        return True


@pytest.fixture
def procs(monkeypatch: pytest.MonkeyPatch) -> FakeProcesses:
    fake = FakeProcesses()
    monkeypatch.setattr(pcsx2_proc, "pcsx2_processes", lambda: dict(fake.pcsx2))
    monkeypatch.setattr(pcsx2_proc, "pid_alive", lambda pid: pid in fake.pcsx2 or pid in fake.alive)
    monkeypatch.setattr(pcsx2_proc, "is_pcsx2", lambda pid: pid in fake.pcsx2)
    monkeypatch.setattr(pcsx2_proc, "terminate", fake.terminate)
    monkeypatch.setattr(pcsx2_claims, "port_open", lambda port: False)
    return fake


def make_copy(root: Path, name: str, port: int, sdl: bool = False) -> Path:
    """A fake portable copy: a folder with an ini enabling PINE on `port`."""
    ini = root / name / "inis" / "PCSX2.ini"
    ini.parent.mkdir(parents=True)
    ini.write_text(
        f"[EmuCore]\nEnablePINE = true\nPINESlot = {port}\n[InputSources]\nSDL = {str(sdl).lower()}\n",
        encoding="utf-8",
    )
    return root / name


@pytest.fixture
def registry(tmp_path: Path, procs: FakeProcesses) -> Registry:
    make_copy(tmp_path / "root", "pcsx2", 28011)
    make_copy(tmp_path / "root", "pcsx2-b", 28012)
    (tmp_path / "root" / "pcsx2-notacopy").mkdir()
    return Registry(tmp_path / "root", tmp_path / "claims", clock=lambda: NOW)


def race(tmp_path: Path, count: int, holder: str) -> list[str]:
    """`count` threads claim copy pcsx2 at once; return "won" or "lost" for each."""
    results: list[str] = []
    barrier = threading.Barrier(count)

    def attempt(n: int) -> None:
        mine = Registry(tmp_path / "root", tmp_path / "claims", clock=lambda: NOW)
        barrier.wait()
        try:
            mine.claim(f"{holder}{n}", "pcsx2")
            results.append("won")
        except ClaimError:
            results.append("lost")

    threads = [threading.Thread(target=attempt, args=(n,)) for n in range(count)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    return results


def test_discovery_lists_copies_with_an_ini(registry: Registry, tmp_path: Path) -> None:
    copies = registry.copies()
    assert [(c.name, c.port) for c in copies] == [("pcsx2", 28011), ("pcsx2-b", 28012)]
    assert copies[0].path == (tmp_path / "root" / "pcsx2").resolve()


def test_discovery_reads_pine_and_sdl(tmp_path: Path) -> None:
    make_copy(tmp_path, "pcsx2-x", 28020, sdl=True)
    (tmp_path / "pcsx2-x" / "inis" / "PCSX2.ini").write_text("[EmuCore]\nEnablePINE = false\n", encoding="utf-8")
    make_copy(tmp_path, "pcsx2-y", 28021, sdl=True)
    by_name = {c.name: c for c in pcsx2_claims.discover_copies(tmp_path)}
    assert by_name["pcsx2-x"].port is None
    assert by_name["pcsx2-y"].sdl is True


def test_claim_takes_the_first_free_copy_and_records_the_owner(registry: Registry) -> None:
    claim, took_over = registry.claim("alice")
    assert took_over is None
    assert (claim.copy, claim.port, claim.agent) == ("pcsx2", 28011, "alice")
    stored = json.loads((registry.claims_dir / "pcsx2.claim" / "owner.json").read_text(encoding="utf-8"))
    assert stored["agent"] == "alice" and stored["time"].startswith("2027-")
    assert registry.claim("bob")[0].copy == "pcsx2-b"
    # An agent that holds a copy gets it back rather than a second one.
    assert registry.claim("alice")[0].copy == "pcsx2"


def test_claim_fails_naming_the_holders_when_all_are_taken(registry: Registry) -> None:
    registry.claim("alice")
    registry.claim("bob")
    with pytest.raises(ClaimError, match="alice"):
        registry.claim("carol")


def test_named_copy_held_by_another_is_refused(registry: Registry) -> None:
    registry.claim("alice", "pcsx2-b")
    with pytest.raises(ClaimError, match="pcsx2-b"):
        registry.claim("bob", "pcsx2-b")


def test_a_bad_agent_id_is_refused(registry: Registry) -> None:
    with pytest.raises(ClaimError, match="agent id"):
        registry.claim("a b")


def test_racing_claims_on_one_copy_have_exactly_one_winner(tmp_path: Path, procs: FakeProcesses) -> None:
    make_copy(tmp_path / "root", "pcsx2", 28011)
    results = race(tmp_path, 8, "agent")
    assert results.count("won") == 1
    assert results.count("lost") == 7


def test_racing_takeovers_of_a_stale_claim_have_one_winner(tmp_path: Path, procs: FakeProcesses) -> None:
    make_copy(tmp_path / "root", "pcsx2", 28011)
    first = Registry(tmp_path / "root", tmp_path / "claims", clock=lambda: NOW)
    first.claim("old", holder_pid=4242)  # pid 4242 is not alive
    results = race(tmp_path, 6, "new")
    assert results.count("won") == 1
    owner = first.read("pcsx2")
    assert owner is not None and owner.agent.startswith("new")
    assert [p.name for p in (tmp_path / "claims").iterdir()] == ["pcsx2.claim"]


def test_a_claim_of_a_dead_holder_is_taken_over(registry: Registry, procs: FakeProcesses) -> None:
    registry.claim("alice", "pcsx2", holder_pid=4242)
    procs.alive.add(4242)
    with pytest.raises(ClaimError):
        registry.claim("bob", "pcsx2")  # the holder lives
    procs.alive.clear()
    claim, took_over = registry.claim("bob", "pcsx2")
    assert claim.agent == "bob" and took_over is not None and took_over.agent == "alice"


def test_a_dead_pcsx2_pid_or_old_age_makes_a_claim_stale(registry: Registry, procs: FakeProcesses) -> None:
    claim, _ = registry.claim("alice", "pcsx2")
    registry.record_pid(claim, 777)
    assert registry.status()[0].stale  # pid 777 is not a live PCSX2
    registry.claim("alice", "pcsx2-b")
    assert not registry.status()[1].stale  # young, no pids: held
    older = Registry(registry.root, registry.claims_dir, clock=lambda: NOW + pcsx2_claims.DEFAULT_MAX_AGE + 1)
    assert older.status()[1].stale


def test_a_live_pcsx2_keeps_an_old_claim(registry: Registry, procs: FakeProcesses) -> None:
    claim, _ = registry.claim("alice", "pcsx2")
    procs.pcsx2[900] = registry.copies()[0].path / "pcsx2-qt.exe"
    registry.record_pid(claim, 900)
    older = Registry(registry.root, registry.claims_dir, clock=lambda: NOW + 10 * pcsx2_claims.DEFAULT_MAX_AGE)
    assert not older.status()[0].stale


def test_an_unclaimed_running_copy_is_not_handed_out(registry: Registry, procs: FakeProcesses) -> None:
    procs.pcsx2[900] = registry.copies()[0].path / "pcsx2-qt.exe"
    assert registry.claim("alice")[0].copy == "pcsx2-b"
    with pytest.raises(ClaimError, match="without a claim"):
        registry.claim("bob", "pcsx2")


def test_release_closes_pcsx2_and_removes_the_claim(registry: Registry, procs: FakeProcesses) -> None:
    claim, _ = registry.claim("alice", "pcsx2")
    procs.pcsx2[900] = registry.copies()[0].path / "pcsx2-qt.exe"
    registry.record_pid(claim, 900)
    [(released, closed)] = registry.release("alice", "pcsx2")
    assert released.agent == "alice" and closed
    assert procs.terminated == [900]
    assert registry.read("pcsx2") is None
    assert list(registry.claims_dir.iterdir()) == []


def test_release_refuses_another_agents_claim_unless_forced(registry: Registry) -> None:
    registry.claim("alice", "pcsx2")
    with pytest.raises(ClaimError, match="held by alice"):
        registry.release("bob", "pcsx2")
    assert registry.read("pcsx2") is not None
    registry.release("bob", "pcsx2", force=True)
    assert registry.read("pcsx2") is None


def test_release_without_a_copy_drops_all_of_the_agents_claims(registry: Registry) -> None:
    registry.claim("alice", "pcsx2")
    registry.claim("alice", "pcsx2-b")
    assert len(registry.release("alice")) == 2
    with pytest.raises(ClaimError, match="nothing to release"):
        registry.release("alice")


def test_status_is_built_from_the_claim_folders(
    registry: Registry, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    registry.claim("alice", "pcsx2")
    # A claim folder written by hand (made by another worktree's tool) shows up at once.
    folder = registry.claims_dir / "pcsx2-b.claim"
    folder.mkdir()
    owner = {
        "copy": "pcsx2-b",
        "agent": "bob",
        "time": datetime.fromtimestamp(NOW - 60, UTC).isoformat(),
        "path": "x",
        "port": 28012,
    }
    (folder / "owner.json").write_text(json.dumps(owner), encoding="utf-8")
    monkeypatch.setattr(pcsx2_claims, "default_registry", lambda: registry)
    assert pcsx2_claims_cli.run_status(False, None) == 0
    lines = capsys.readouterr().out.splitlines()
    assert lines[0].split() == ["copy", "state", "agent", "since", "port", "running"]
    assert lines[1].split()[:3] == ["pcsx2", "held", "alice"]
    assert lines[2].split()[:3] == ["pcsx2-b", "held", "bob"]


def test_status_warns_when_sdl_is_on(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    make_copy(tmp_path / "root", "pcsx2", 28011, sdl=True)
    pcsx2_claims_cli._warn_sdl(Registry(tmp_path / "root", tmp_path / "claims").copies())
    assert "SDL = true" in capsys.readouterr().out


def test_incomplete_claim_folder_is_stale_only_after_a_while(registry: Registry) -> None:
    (registry.claims_dir / "pcsx2.claim").mkdir(parents=True)
    claim = registry.read("pcsx2")
    assert claim is not None and claim.agent == "?"
    far_future = Registry(registry.root, registry.claims_dir, clock=lambda: 1e12)
    assert far_future.status()[0].stale
    just_made = Registry(registry.root, registry.claims_dir)
    assert not just_made.status()[0].stale


# --- commands that start PCSX2 ---------------------------------------------------------------------------------


def test_launch_and_record_refuse_without_an_agent(registry: Registry, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(pcsx2_claims, "default_registry", lambda: registry)
    with pytest.raises(ClaimError, match="--agent"):
        pcsx2_cli.run_launch(Path("state.p2s"), None, None, None, None)
    with pytest.raises(ClaimError, match="--agent"):
        pcsx2_cli.run_record(Path("s.toml"), Path("/tmp/out.csv"), None, False, False, (None, None, None), None)
    assert registry.read("pcsx2") is None


def test_launch_refuses_a_copy_held_by_another_agent(registry: Registry, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(pcsx2_claims, "default_registry", lambda: registry)
    registry.claim("alice", "pcsx2")
    with pytest.raises(ClaimError, match="pcsx2"):
        pcsx2_cli.run_launch(Path("s.p2s"), registry.copies()[0].path, None, None, "bob")


def test_launch_runs_under_a_claim_and_records_the_pid(
    registry: Registry, monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    monkeypatch.setattr(pcsx2_claims, "default_registry", lambda: registry)
    iso = tmp_path / "game.iso"
    iso.write_bytes(b"")
    (registry.copies()[0].path / "pcsx2-qt.exe").write_bytes(b"")

    class FakeProcess:
        pid = 31337

    class FakeClient:
        def close(self) -> None:
            pass

    monkeypatch.setattr(pcsx2_cli.subprocess, "Popen", lambda *a, **k: FakeProcess())
    monkeypatch.setattr(pcsx2_cli, "_wait_for_game", lambda *a, **k: FakeClient())
    monkeypatch.setattr(pcsx2_cli, "disc_link", lambda iso, scratch: iso)
    assert pcsx2_cli.run_launch(Path("s.p2s"), None, iso, tmp_path / "scratch", "alice") == 0
    claim = registry.read("pcsx2")
    assert claim is not None and claim.agent == "alice" and claim.pcsx2_pid == 31337


def test_launch_failure_releases_a_claim_it_made(
    registry: Registry, monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    monkeypatch.setattr(pcsx2_claims, "default_registry", lambda: registry)
    with pytest.raises(ConfigError, match="disc image"):
        pcsx2_cli.run_launch(Path("s.p2s"), None, tmp_path / "missing.iso", tmp_path, "alice")
    assert registry.read("pcsx2") is None


def test_pcsx2_dir_outside_the_copies_is_refused(registry: Registry, tmp_path: Path) -> None:
    with pytest.raises(ClaimError, match="not one of the claimable"):
        registry.claim_for_run("alice", tmp_path, None)


# --- keys -------------------------------------------------------------------------------------------------------


def test_key_names_become_virtual_keys() -> None:
    assert pcsx2_proc.parse_chords(["space", "W+K", "f4", "Up"]) == [[0x20], [0x57, 0x4B], [0x73], [0x26]]
    with pytest.raises(ConfigError, match="unknown key"):
        pcsx2_proc.parse_key("banana")


def test_keys_need_a_claim_on_a_running_copy(registry: Registry, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(pcsx2_claims, "default_registry", lambda: registry)
    with pytest.raises(ClaimError, match="claim it first"):
        pcsx2_claims_cli.run_keys("alice", "pcsx2", ["space"], 100, 100)
    registry.claim("alice", "pcsx2")
    with pytest.raises(ClaimError, match="runs no PCSX2"):
        pcsx2_claims_cli.run_keys("alice", "pcsx2", ["space"], 100, 100)


def test_keys_post_to_the_window_without_focusing(
    registry: Registry, procs: FakeProcesses, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.setattr(pcsx2_claims, "default_registry", lambda: registry)
    registry.claim("alice", "pcsx2")
    procs.pcsx2[900] = registry.copies()[0].path / "pcsx2-qt.exe"
    window = pcsx2_proc.Window(1234, "The Warriors", "Qt", 640, 480, True, True)
    monkeypatch.setattr(pcsx2_proc, "key_targets", lambda pid: [window])
    sent: list[tuple[list[int], list[list[int]]]] = []
    monkeypatch.setattr(pcsx2_proc, "post_keys", lambda hwnds, chords, hold, gap: sent.append((hwnds, chords)))
    assert pcsx2_claims_cli.run_keys("alice", "pcsx2", ["space"], 100, 100) == 0
    assert sent == [([1234], [[0x20]])]
    assert "no focus changed" in capsys.readouterr().out
