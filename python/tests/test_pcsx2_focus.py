# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests that PCSX2 started by the tools never keeps the owner's keyboard focus: the FocusGuard logic against a fake
desktop, and that Emulator starts PCSX2 through the no-focus launcher (never a plain Popen)."""

from __future__ import annotations

import subprocess
from pathlib import Path
from typing import Any

import pytest

from coney_tools import pcsx2_cli, pcsx2_proc
from coney_tools.pcsx2_proc import FocusCalls, FocusGuard

OWNER_WINDOW, OTHER_WINDOW, PCSX2_WINDOW = 0x100, 0x200, 0x300
PCSX2_PID = 4242


class FakeDesktop:
    """A desktop of three windows; give_foreground records each hand-back and makes it real."""

    def __init__(self) -> None:
        """The owner's window starts in the foreground."""
        self.foreground = OWNER_WINDOW
        self.owners = {OWNER_WINDOW: 1, OTHER_WINDOW: 2, PCSX2_WINDOW: PCSX2_PID}
        self.alive = {OWNER_WINDOW, OTHER_WINDOW, PCSX2_WINDOW}
        self.given: list[int] = []

    def give(self, hwnd: int) -> bool:
        """Hand the foreground to `hwnd`."""
        self.given.append(hwnd)
        self.foreground = hwnd
        return True

    def calls(self) -> FocusCalls:
        """The guard's calls on this desktop."""
        return FocusCalls(
            foreground=lambda: self.foreground,
            owner_pid=lambda hwnd: self.owners.get(hwnd, 0),
            exists=lambda hwnd: hwnd in self.alive,
            give_foreground=self.give,
        )


def _guard(desktop: FakeDesktop) -> FocusGuard:
    """A guard on the fake desktop guarding PCSX2_PID, without its thread (tests call check())."""
    guard = FocusGuard(desktop.calls())
    guard.pid = PCSX2_PID
    return guard


def test_guard_hands_the_foreground_back_to_the_owner() -> None:
    desktop = FakeDesktop()
    guard = _guard(desktop)
    assert not guard.check()
    desktop.foreground = PCSX2_WINDOW
    assert guard.check()
    assert desktop.given == [OWNER_WINDOW]
    assert desktop.foreground == OWNER_WINDOW


def test_guard_follows_the_owner_to_another_window() -> None:
    desktop = FakeDesktop()
    guard = _guard(desktop)
    desktop.foreground = OTHER_WINDOW
    assert not guard.check()
    desktop.foreground = PCSX2_WINDOW
    assert guard.check()
    assert desktop.given == [OTHER_WINDOW]


def test_guard_leaves_pcsx2_alone_when_the_old_window_is_gone() -> None:
    desktop = FakeDesktop()
    guard = _guard(desktop)
    desktop.alive.discard(OWNER_WINDOW)
    desktop.foreground = PCSX2_WINDOW
    assert not guard.check()
    assert desktop.given == []


def test_guard_does_nothing_before_it_has_a_process() -> None:
    desktop = FakeDesktop()
    guard = FocusGuard(desktop.calls())
    desktop.foreground = PCSX2_WINDOW
    assert not guard.check()


def test_guard_thread_runs_and_stops() -> None:
    desktop = FakeDesktop()
    guard = FocusGuard(desktop.calls(), interval=0.001)
    desktop.foreground = PCSX2_WINDOW
    guard.start(PCSX2_PID)
    for _ in range(1000):
        if desktop.given:
            break
        guard._stop.wait(0.005)
    guard.stop()
    assert desktop.given and desktop.given[0] == OWNER_WINDOW


def test_emulator_starts_pcsx2_through_the_no_focus_launcher(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    """Emulator must use start_detached and the guard; a plain Popen would let PCSX2 take the owner's focus."""
    started: list[list[str]] = []
    guards: list[int] = []

    class Guard:
        """A guard that records the pid it was started on."""

        def start(self, pid: int) -> None:
            guards.append(pid)

        def stop(self) -> None:
            pass

    def no_popen(*args: Any, **kwargs: Any) -> None:
        raise AssertionError("Emulator must not start PCSX2 with Popen; use pcsx2_proc.start_detached")

    def fake_start(command: list[str]) -> int:
        started.append(command)
        return PCSX2_PID

    monkeypatch.setattr(subprocess, "Popen", no_popen)
    monkeypatch.setattr(pcsx2_proc, "start_detached", fake_start)
    monkeypatch.setattr(pcsx2_proc, "focus_guard", Guard)
    monkeypatch.setattr(pcsx2_cli, "pine_port", lambda _: 28099)
    monkeypatch.setattr(pcsx2_cli, "_port_open", lambda _: False)
    monkeypatch.setattr(pcsx2_cli, "disc_link", lambda iso, _: iso)
    monkeypatch.setattr(pcsx2_cli, "_executable", lambda folder: folder / "pcsx2-qt.exe")
    monkeypatch.setattr(pcsx2_cli, "_wait_for_game", lambda *_: object())
    paths = pcsx2_cli.Paths(tmp_path, tmp_path / "game.iso", tmp_path)
    recorded: list[int] = []
    emulator = pcsx2_cli.Emulator(paths, tmp_path / "state.p2s", [], on_start=recorded.append)
    assert started and started[0][0].endswith("pcsx2-qt.exe")
    assert guards == [PCSX2_PID]
    assert recorded == [PCSX2_PID]
    assert emulator.pid == PCSX2_PID
