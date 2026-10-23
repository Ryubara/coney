# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.debug_server and `pcsx2 record --recording`: the PCSX2-MCP fork's JSON protocol against a
fake server, the answers of its input_recording command, the `.p2m2` header, and a replay recorded against a fake
debug server and a fake PINE game. No PCSX2 and no game data: every recording here is made up."""

from __future__ import annotations

import json
import socket
import struct
import threading
from collections.abc import Callable, Iterator
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import pytest
from pine_support import dead_port
from test_pcsx2 import FakeGame, _repo, _serve

from coney_tools import pcsx2_cli
from coney_tools.config import ConfigError
from coney_tools.debug_server import (
    DebugClient,
    DebugServerError,
    parse_input_recording,
    parse_p2m2,
    read_p2m2,
    savestate_of,
)
from coney_tools.pcsx2_state import Edit, Patch
from coney_tools.pine import PineClient
from coney_tools.recorder import Recorder
from coney_tools.scenario import load_scenario

Handler = Callable[[dict[str, Any]], dict[str, Any]]


def make_p2m2(frames: int = 120, savestate: bool = True, version: int = 1, author: bytes = b"coney") -> bytes:
    """A made-up input recording: its header and `frames` frames of pads at rest."""
    header = struct.pack(
        "<B50s255s255sIIB", version, b"PCSX2-v2.9.129", author, b"Made-up Game (SLUS-00000)", frames, 2, savestate
    )
    return header + bytes(36) * frames


class FakeDebugServer:
    """A debug server on a free local port: each line is a JSON command, answered by `handler`, one client at a
    time; every command received is kept in `commands`. With `split`, each answer goes out in two pieces."""

    def __init__(self, handler: Handler, split: bool = False) -> None:
        self.handler = handler
        self.split = split
        self.commands: list[dict[str, Any]] = []
        self.server = socket.create_server(("127.0.0.1", 0))
        self.port = self.server.getsockname()[1]
        threading.Thread(target=self._serve, daemon=True).start()

    def _serve(self) -> None:
        """Answer clients until the server closes."""
        while True:
            try:
                connection, _ = self.server.accept()
            except OSError:
                return
            with connection, connection.makefile("rb") as lines:
                for line in lines:
                    command = json.loads(line)
                    self.commands.append(command)
                    answer = json.dumps(self.handler(command)).encode() + b"\n"
                    if self.split:
                        connection.sendall(answer[:5])
                        connection.sendall(answer[5:])
                    else:
                        connection.sendall(answer)

    def close(self) -> None:
        self.server.close()


@pytest.fixture
def debug_server() -> Iterator[Callable[..., FakeDebugServer]]:
    """Makes FakeDebugServers and closes them afterwards."""
    made: list[FakeDebugServer] = []

    def make(handler: Handler, split: bool = False) -> FakeDebugServer:
        made.append(FakeDebugServer(handler, split))
        return made[-1]

    yield make
    for server in made:
        server.close()


# --- the .p2m2 header -------------------------------------------------------------------------------------------------


def test_a_recordings_header_is_read(tmp_path: Path) -> None:
    path = tmp_path / "fight.p2m2"
    path.write_bytes(make_p2m2(frames=300, savestate=True))
    header = read_p2m2(path)
    assert (header.version, header.emulator, header.author) == (1, "PCSX2-v2.9.129", "coney")
    assert (header.game, header.total_frames, header.undo_count) == ("Made-up Game (SLUS-00000)", 300, 2)
    assert header.from_savestate is True
    assert parse_p2m2(make_p2m2(savestate=False)).from_savestate is False


def test_a_file_that_is_not_a_recording_is_refused(tmp_path: Path) -> None:
    with pytest.raises(ValueError, match="too short"):
        parse_p2m2(b"\x01" * 100)
    with pytest.raises(ValueError, match="version 2"):
        parse_p2m2(make_p2m2(version=2))
    path = tmp_path / "short.p2m2"
    path.write_bytes(b"x")
    with pytest.raises(ValueError, match=r"short.p2m2"):
        read_p2m2(path)


def test_the_savestate_sits_beside_the_recording_under_pcsx2s_name() -> None:
    assert savestate_of(Path("recordings/a.p2m2")) == Path("recordings/a.p2m2_SaveState.p2s")


# --- input_recording answers ------------------------------------------------------------------------------------------


def test_a_replaying_status_is_parsed() -> None:
    answer = {
        "ok": True,
        "mode": "replaying",
        "path": "C:/emu/pcsx2/recordings/a.p2m2",
        "from": "savestate",
        "savestate": "C:/emu/pcsx2/recordings/a.p2m2_SaveState.p2s",
        "savestate_exists": True,
        "author": "coney",
        "game": "x",
        "recording_frame": 12,
        "total_frames": 300,
        "undo_count": 0,
        "starting_frame": 4000,
        "emulation": "running",
        "frame": 4012,
    }
    status = parse_input_recording(answer)
    assert (status.mode, status.from_savestate, status.recording_frame, status.total_frames) == (
        "replaying",
        True,
        12,
        300,
    )
    assert (status.emulation, status.frame, status.replay_ended) == ("running", 4012, False)


def test_an_ended_replay_and_a_stop_are_parsed() -> None:
    ended = parse_input_recording(
        {
            "ok": True,
            "mode": "off",
            "replay_ended": True,
            "path": "a.p2m2",
            "recording_frame": 300,
            "total_frames": 300,
            "ended_at_frame": 4300,
            "emulation": "paused",
            "frame": 4300,
        }
    )
    assert (ended.mode, ended.replay_ended, ended.total_frames, ended.from_savestate) == ("off", True, 300, None)
    stop = parse_input_recording(
        {
            "ok": True,
            "mode": "off",
            "stopped": "a.p2m2",
            "replay_ended": False,
            "recording_frame": 40,
            "total_frames": 300,
            "emulation": "running",
            "frame": 50,
        }
    )
    assert (stop.stopped, stop.recording_frame) == ("a.p2m2", 40)


def test_a_refusal_or_a_malformed_answer_raises() -> None:
    with pytest.raises(DebugServerError, match="input_recording: no such recording: x"):
        parse_input_recording({"ok": False, "error": "no such recording: x"})
    with pytest.raises(DebugServerError, match="malformed"):
        parse_input_recording({"ok": True, "frame": 3})


# --- the client -------------------------------------------------------------------------------------------------------


def test_the_client_sends_a_line_and_reads_one_back(debug_server: Callable[..., FakeDebugServer]) -> None:
    server = debug_server(lambda c: {"ok": True, "state": "paused", "frame": 7}, split=True)
    with DebugClient(server.port) as client:
        assert client.emulation("pause") == "paused"
        assert client.request({"cmd": "status"})["frame"] == 7
    assert server.commands == [{"cmd": "emulation", "action": "pause"}, {"cmd": "status"}]


def test_the_client_names_a_refusal(debug_server: Callable[..., FakeDebugServer]) -> None:
    server = debug_server(lambda c: {"ok": False, "error": "no game is running"})
    with DebugClient(server.port) as client, pytest.raises(DebugServerError, match="emulation: no game is running"):
        client.emulation("resume")


def test_no_debug_server_is_a_clear_error() -> None:
    with dead_port() as port, pytest.raises(DebugServerError, match=f"no debug server on port {port}"):
        DebugClient(port, connect_timeout=0.3)


def test_play_sends_the_path_relative_to_the_data_folder(debug_server: Callable[..., FakeDebugServer]) -> None:
    server = debug_server(lambda c: {"ok": True, "mode": "replaying", "emulation": "paused", "frame": 1})
    with DebugClient(server.port) as client:
        assert client.input_recording("play", "recordings/coney-record/a.p2m2").mode == "replaying"
    assert server.commands == [{"cmd": "input_recording", "action": "play", "path": "recordings/coney-record/a.p2m2"}]


# --- pcsx2 record --recording -----------------------------------------------------------------------------------------


def test_a_savestate_recording_is_staged_with_its_patched_state(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    source = tmp_path / "scratch" / "fight 1.p2m2"
    source.parent.mkdir()
    source.write_bytes(make_p2m2())
    savestate_of(source).write_bytes(b"state")
    made: list[tuple[Path, Path, list[Patch]]] = []

    def make_copy(source: Path, out: Path, patches: list[Patch], pcsx2_dir: Path | None) -> int:
        made.append((source, out, patches))
        return out.write_bytes(b"patched")

    monkeypatch.setattr(pcsx2_cli, "_make_copy", make_copy)
    copy = tmp_path / "pcsx2"
    patch = Patch("x", "", "", (Edit(0x100, b"\1\0\0\0", None),))
    staged = pcsx2_cli._stage_recording(source, read_p2m2(source), copy, [patch])
    folder = copy / "recordings" / "coney-record"
    assert staged == [folder / "fight_1.p2m2", folder / "fight_1.p2m2_SaveState.p2s"]
    assert staged[0].read_bytes() == source.read_bytes()
    assert made == [(savestate_of(source), staged[1], [patch])]
    # Without patches the savestate is copied as it is.
    staged = pcsx2_cli._stage_recording(source, read_p2m2(source), copy, [])
    assert staged[1].read_bytes() == b"state"


def test_staging_refuses_a_missing_savestate_and_patches_on_power_on(tmp_path: Path) -> None:
    source = tmp_path / "a.p2m2"
    source.write_bytes(make_p2m2(savestate=True))
    with pytest.raises(ConfigError, match="is missing"):
        pcsx2_cli._stage_recording(source, read_p2m2(source), tmp_path / "pcsx2", [])
    source.write_bytes(make_p2m2(savestate=False))
    patch = Patch("x", "", "", (Edit(0x100, b"\1", None),))
    with pytest.raises(ConfigError, match="power-on"):
        pcsx2_cli._stage_recording(source, read_p2m2(source), tmp_path / "pcsx2", [patch])
    assert pcsx2_cli._stage_recording(source, read_p2m2(source), tmp_path / "pcsx2", []) == [
        tmp_path / "pcsx2" / "recordings" / "coney-record" / "a.p2m2"
    ]


def test_record_refuses_a_recording_with_attach_or_state(tmp_path: Path) -> None:
    for attach, state in ((True, None), (False, "slot:1")):
        with pytest.raises(ConfigError, match="--recording starts from the recording"):
            pcsx2_cli.run_record(
                Path("s.toml"), tmp_path / "t.csv", state, attach, False, (None, None, None), "a", None, Path("r.p2m2")
            )


@dataclass
class FakeEmulator:
    """What _record_replay uses of an Emulator."""

    paths: pcsx2_cli.Paths
    client: PineClient
    debug_port: int | None


def _replay_handler(game: FakeGame, ended: bool = False) -> Handler:
    """A debug server for one replay: paused until resumed, replaying until stopped (or ended at once)."""
    state = {"mode": "off"}

    def handle(command: dict[str, Any]) -> dict[str, Any]:
        if command["cmd"] == "emulation":
            if command["action"] == "resume":
                game.polls_per_update = 2
            return {"ok": True, "state": "running" if command["action"] == "resume" else "paused", "frame": 0}
        if command["action"] == "play":
            state["mode"] = "replaying"
            return {"ok": True, "mode": "replaying", "emulation": "paused", "frame": 0}
        if command["action"] == "stop":
            state["mode"] = "off"
            return {"ok": True, "mode": "off", "stopped": "a.p2m2", "emulation": "running", "frame": 9}
        if ended:
            return {
                "ok": True,
                "mode": "off",
                "replay_ended": True,
                "total_frames": 3,
                "emulation": "paused",
                "frame": 3,
            }
        return {"ok": True, "mode": state["mode"], "emulation": "running", "frame": 9}

    return handle


def _pine_game(game: FakeGame) -> Iterator[int]:
    """A PINE server for `game`; yields its port."""
    server = socket.create_server(("127.0.0.1", 0))
    threading.Thread(target=_serve, args=(game, server), daemon=True).start()
    yield server.getsockname()[1]
    server.close()


def test_a_replay_pauses_plays_samples_step_0_then_resumes_and_stops(
    tmp_path: Path, debug_server: Callable[..., FakeDebugServer]
) -> None:
    game = FakeGame()
    game.polls_per_update = 10**9  # paused until the debug server's resume
    server = debug_server(_replay_handler(game))
    scenario = load_scenario(_repo(tmp_path, ""), tmp_path)
    copy = tmp_path / "pcsx2"
    staged = copy / "recordings" / "coney-record" / "a.p2m2"
    for port in _pine_game(game):
        with PineClient(port) as client:
            emulator = FakeEmulator(pcsx2_cli.Paths(copy, tmp_path / "x.iso", tmp_path), client, server.port)
            recording = pcsx2_cli._record_replay(emulator, staged, scenario, [])  # type: ignore[arg-type]
    assert [(c["cmd"], c.get("action")) for c in server.commands] == [
        ("emulation", "pause"),
        ("input_recording", "play"),
        ("emulation", "resume"),
        ("input_recording", "status"),
        ("input_recording", "stop"),
    ]
    assert server.commands[1]["path"] == "recordings/coney-record/a.p2m2"
    assert [row[0] for row in recording.rows] == [0, 1, 2, 3, 4, 5, 6]
    assert recording.rows[0][2] == 0


def test_a_recording_shorter_than_the_scenario_says_so(
    tmp_path: Path, debug_server: Callable[..., FakeDebugServer], monkeypatch: pytest.MonkeyPatch
) -> None:
    game = FakeGame()
    game.polls_per_update = 10**9
    handler = _replay_handler(game, ended=True)

    def never_resume(command: dict[str, Any]) -> dict[str, Any]:
        answer = handler(command)
        if command["cmd"] == "emulation":
            game.polls_per_update = 10**9  # the replay ended: PCSX2 stays paused at its last frame
        return answer

    server = debug_server(never_resume)
    scenario = load_scenario(_repo(tmp_path, ""), tmp_path)
    original = Recorder.record
    monkeypatch.setattr(Recorder, "record", lambda self, timeout=None: original(self, timeout=0.5))
    for port in _pine_game(game):
        with PineClient(port) as client:
            emulator = FakeEmulator(pcsx2_cli.Paths(tmp_path, tmp_path / "x.iso", tmp_path), client, server.port)
            with pytest.raises(ConfigError, match="the recording ended at its frame 3"):
                pcsx2_cli._record_replay(emulator, tmp_path / "recordings" / "a.p2m2", scenario, [])  # type: ignore[arg-type]


def test_a_copy_without_a_debug_server_cannot_replay(tmp_path: Path) -> None:
    emulator = FakeEmulator(pcsx2_cli.Paths(tmp_path, tmp_path / "x.iso", tmp_path), None, None)  # type: ignore[arg-type]
    with pytest.raises(ConfigError, match="debug server is off"):
        pcsx2_cli._record_replay(emulator, tmp_path / "a.p2m2", None, [])  # type: ignore[arg-type]
