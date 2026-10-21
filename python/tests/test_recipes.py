# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the PCSX2 recipes: the schema of research/pcsx2-recipes.toml, its generated page, and the runner over a
fake PINE server that plays a tiny game (humans, a tick count and the call hook's block)."""

from __future__ import annotations

import math
import socket
import struct
import threading
from collections.abc import Iterator
from pathlib import Path
from typing import Any

import pytest

from coney_tools import hooks as hook_tools
from coney_tools import pcsx2_claims as claims
from coney_tools import recipes, recipes_cli, recipes_render, recipes_run
from coney_tools.config import find_repo_root
from coney_tools.game_memory import HUMAN_TABLE, PER_PLAYER_TABLE, TRANSFORM_TABLE, ExpressionError, to_raw
from coney_tools.pcsx2_state import Edit, Patch
from coney_tools.pine import PineClient, Read

ROOT = find_repo_root(Path(__file__))


# --- a fake game behind PINE ------------------------------------------------------------------------------------------


class FakeGame:
    """Sparse EE memory with two humans (the player is human 1, handle index 5) and a game that ticks once per PINE
    message; at each even tick it makes a call waiting in the call hook's block, as the `call-brains` cave does."""

    def __init__(self) -> None:
        """Two humans, a tick count of 100 and an empty call block."""
        self.memory: dict[int, int] = {}
        self.calls: list[tuple[int, list[int]]] = []
        self.results: dict[int, int] = {}
        self.paused = False
        for i, (name, player, index) in enumerate([("Ped", -1, 4), ("Hero", 0, 5)]):
            human = HUMAN_TABLE + i * 0x6D0
            self.write(human + 0xD4, 4, 0x0065A540 + i * 0x180)
            for k, byte in enumerate(name.encode() + b"\0"):
                self.write(human + 0x80 + k, 1, byte)
            self.write(human + 0x1B0, 1, player & 0xFF)
            self.write(human + 0x92, 2, index)
            self.write(human + 0x90, 2, 0x1234 + i)
            transform = TRANSFORM_TABLE + index * 0x20
            for k, value in enumerate([1.0 + i, 2.0, 0.25]):
                self.write(transform + 4 * k, 4, to_raw(value, "f32"))
            self.write(transform + 0x1C, 4, to_raw(1.0, "f32"))
        self.write(recipes_run.TICK_COUNT, 4, 100)

    def read(self, address: int, size: int) -> int:
        """`size` bytes at `address`, little-endian; memory never written reads 0."""
        return sum(self.memory.get(address + k, 0) << (8 * k) for k in range(size))

    def write(self, address: int, size: int, value: int) -> None:
        """Store `size` bytes of `value` at `address`."""
        for k in range(size):
            self.memory[address + k] = (value >> (8 * k)) & 0xFF

    def tick(self) -> None:
        """One 60 Hz tick (none while paused); on an even count the call hook makes a waiting call."""
        if self.paused:
            return
        count = self.read(recipes_run.TICK_COUNT, 4) + 1
        self.write(recipes_run.TICK_COUNT, 4, count)
        function = self.read(hook_tools.CALL_BASE, 4)
        if count % 2 == 0 and function:
            args = [self.read(hook_tools.CALL_BASE + 4 + 4 * k, 4) for k in range(hook_tools.CALL_ARGS)]
            self.calls.append((function, args))
            self.write(hook_tools.CALL_BASE, 4, 0)
            self.write(hook_tools.CALL_RESULT, 4, self.results.get(function, 0))
            self.write(hook_tools.CALL_DONE, 4, self.read(hook_tools.CALL_DONE, 4) + 1)

    def text(self, address: int) -> str:
        """The C string at `address`."""
        out = bytearray()
        while self.memory.get(address + len(out), 0):
            out.append(self.memory[address + len(out)])
        return out.decode("latin-1")


def _serve(game: FakeGame, server: socket.socket) -> None:
    """Answer PINE messages (reads, writes, status) for clients until the server closes."""
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
                reads: list[tuple[int, int]] = []
                status, at = False, 0
                while at < len(body):
                    opcode = body[at]
                    if opcode == 0x0F:
                        status, at = True, at + 1
                        continue
                    address = struct.unpack_from("<I", body, at + 1)[0]
                    at += 5
                    if opcode <= 3:
                        reads.append((address, 1 << opcode))
                    else:
                        size = 1 << (opcode - 4)
                        game.write(address, size, int.from_bytes(body[at : at + size], "little"))
                        at += size
                game.tick()
                reply = bytearray([0])
                for address, size in reads:
                    reply += game.read(address, size).to_bytes(size, "little")
                if status:
                    reply += struct.pack("<I", 1 if game.paused else 0)
                connection.sendall(struct.pack("<I", len(reply) + 4) + bytes(reply))


@pytest.fixture
def fake_pine() -> Iterator[tuple[FakeGame, int]]:
    """A FakeGame behind a PINE server on a free local port."""
    game = FakeGame()
    server = socket.create_server(("127.0.0.1", 0))
    threading.Thread(target=_serve, args=(game, server), daemon=True).start()
    yield game, server.getsockname()[1]
    server.close()


def _recipe(steps: list[dict[str, Any]], params: list[dict[str, Any]] | None = None, **extra: Any) -> recipes.Recipe:
    """A recipe made of `steps`, checked by the schema."""
    raw = {
        "summary": "s",
        "how": "h",
        "research": ["docs/research/characters.md"],
        "ghidra": ["0x00228730 Humans_StaticInit"],
        "evidence": "confirmed-code",
        "confirmed_runtime": {"date": "2026-10-21", "copy": "pcsx2-d", "pcsx2": "2.9.129", "state": "test"},
        "params": params or [],
        "steps": steps,
        **extra,
    }
    return recipes.parse_recipe("test", raw, ["Other"], {"call-brains", "scripted-pad"})


def _run(port: int, recipe: recipes.Recipe, args: dict[str, str] | None = None) -> tuple[dict[str, Any], list[str]]:
    """Run `recipe` over PINE at `port`; returns what it showed and the lines it printed."""
    lines: list[str] = []
    with PineClient(port) as pine:
        runner = recipes_run.Runner(pine, out=lines.append, sleep=lambda _: None)
        shown = runner.run(recipe, recipes_run.bind_args(recipe, args or {}))
    return shown, lines


# --- the schema ------------------------------------------------------------------------------------------------------


def test_the_recipe_file_loads_and_every_recipe_is_justified_and_confirmed() -> None:
    book = recipes.load(ROOT)
    assert len(book.recipes) >= 20
    for recipe in book.recipes.values():
        assert recipe.evidence in recipes.EVIDENCE
        assert recipe.ghidra and all(g.startswith("0x00") for g in recipe.ghidra)
        assert recipe.confirmed.date and recipe.confirmed.copy and recipe.confirmed.state
        assert recipe.group in book.groups


def test_the_brief_s_recipes_are_all_there() -> None:
    names = set(recipes.load(ROOT).recipes)
    wanted = {"find-player", "find-human", "position", "teleport", "spawn-human", "set-health", "brain", "inventory",
              "stats", "puppet-command", "pad", "human-event", "lua", "call", "load-checkpoint", "pause", "step",
              "lua-globals"}  # fmt: skip
    assert wanted <= names


@pytest.mark.parametrize(
    ("change", "message"),
    [
        ({"evidence": "probably"}, "evidence must be one of"),
        ({"ghidra": []}, "ghidra lists at least one"),
        ({"ghidra": ["Humans_StaticInit"]}, "ghidra lists at least one"),
        ({"research": []}, "research lists at least one"),
        ({"confirmed_runtime": None}, "needs confirmed_runtime"),
        ({"confirmed_runtime": {"date": "21 Oct", "copy": "c", "pcsx2": "p", "state": "s"}}, "YYYY-MM-DD"),
        ({"patches": ["no-such-patch"]}, "unknown patch or hook"),
        ({"emulation": "sometimes"}, "emulation is one of"),
        ({"steps": [{"show": "1", "read": "2"}]}, "unknown key"),
        ({"steps": [{"show": "1", "write": "2"}]}, "exactly one of"),
        ({"steps": [{"show": "player.x"}]}, "not allowed"),
        ({"steps": [{"write": "player", "value": "1"}]}, "needs a type"),
        ({"steps": [{"wait": 1000}]}, "1 to 600"),
        ({"steps": [{"call": "0x10"}]}, "needs a call hook"),
        ({"steps": [{"lua": "x = 1"}]}, "needs a call hook"),
        ({"steps": [], "example": ""}, "gives an `example`"),
        ({"params": [{"name": "heading", "help": "h"}]}, "would hide"),
        ({"params": [{"name": "x", "help": "h", "kind": "colour"}]}, "kind is one of"),
        ({"surprise": 1}, "unknown key"),
    ],
)
def test_the_schema_refuses(change: dict[str, Any], message: str) -> None:
    raw: dict[str, Any] = {
        "summary": "s",
        "how": "h",
        "research": ["docs/research/characters.md"],
        "ghidra": ["0x00228730 Humans_StaticInit"],
        "evidence": "confirmed-code",
        "confirmed_runtime": {"date": "2026-10-21", "copy": "pcsx2-d", "pcsx2": "2.9.129", "state": "test"},
        "steps": [{"show": "1"}],
        **change,
    }
    with pytest.raises(recipes.RecipeError, match=message):
        recipes.parse_recipe("bad", raw, ["Other"], {"call-brains"})


def test_the_page_is_up_to_date() -> None:
    page = (ROOT / recipes_render.PAGE).read_text(encoding="utf-8").replace("\r\n", "\n")
    assert page == recipes_render.render(recipes.load(ROOT)), "run `coney-tools pcsx2 recipe render`"


def test_the_page_keeps_to_the_line_length_outside_tables_and_code() -> None:
    text = recipes_render.render(recipes.load(ROOT))
    fenced = False
    for line in text.splitlines():
        if line.startswith("```"):
            fenced = not fenced
        elif not fenced and not line.startswith("|"):
            assert len(line) <= recipes_render.WIDTH, line


def test_links_from_guides() -> None:
    assert recipes_render.link("docs/research/ai.md#brain") == "../research/ai.md#brain"
    assert recipes_render.link("docs/guides/testing.md#gamepad") == "testing.md#gamepad"


# --- arguments --------------------------------------------------------------------------------------------------------


def test_arguments_take_defaults_numbers_and_text() -> None:
    recipe = _recipe(
        [{"show": "n"}],
        [
            {"name": "n", "help": "a number", "default": "0x10"},
            {"name": "f", "help": "a float", "default": "1.5"},
            {"name": "t", "kind": "text", "help": "text", "default": ""},
        ],
    )
    assert recipes_run.bind_args(recipe, {}) == {"n": 16, "f": 1.5, "t": ""}
    with pytest.raises(recipes.RecipeError, match="unknown argument"):
        recipes_run.bind_args(recipe, {"m": "1"})
    with pytest.raises(recipes.RecipeError, match="not a number"):
        recipes_run.bind_args(recipe, {"n": "ten"})
    required = _recipe([{"show": "n"}], [{"name": "n", "help": "a number"}])
    with pytest.raises(recipes.RecipeError, match="needs --arg n="):
        recipes_run.bind_args(required, {})


def test_name_value_pairs() -> None:
    assert recipes_cli.parse_args(["a=1", "code=x = 2"]) == {"a": "1", "code": "x = 2"}
    with pytest.raises(recipes.RecipeError, match="NAME=VALUE"):
        recipes_cli.parse_args(["oops"])


def test_buttons_and_sticks_as_coney_makes_them() -> None:
    assert recipes_run.button_mask("cross+l2") == 0x0041
    assert recipes_run.button_mask("") == 0
    with pytest.raises(ExpressionError, match="no button"):
        recipes_run.button_mask("jump")


# --- running over PINE ------------------------------------------------------------------------------------------------


def test_find_the_player_and_a_human_by_name(fake_pine: tuple[FakeGame, int]) -> None:
    _, port = fake_pine
    recipe = _recipe(
        [{"let": {"h": "who"}}, {"show": "index(h)", "label": "index"}, {"text": "h + 0x80", "label": "name"}],
        [{"name": "who", "kind": "human", "default": "", "help": "a name"}],
    )
    shown, _ = _run(port, recipe)
    assert shown == {"index": 5, "name": "Hero"}
    shown, _ = _run(port, recipe, {"who": "Ped"})
    assert shown == {"index": 4, "name": "Ped"}


def test_a_teleport_writes_the_transform(fake_pine: tuple[FakeGame, int]) -> None:
    game, port = fake_pine
    book = recipes.load(ROOT)
    _run(port, book.recipes["teleport"], {"x": "10", "y": "-3.5", "z": "0.5", "facing": "90"})
    transform = TRANSFORM_TABLE + 5 * 0x20
    values = [struct.unpack("<f", game.read(transform + 4 * k, 4).to_bytes(4, "little"))[0] for k in range(8)]
    assert values[:3] == [10.0, -3.5, 0.5]
    assert values[6] == pytest.approx(math.sin(math.pi / 4))
    assert values[7] == pytest.approx(math.cos(math.pi / 4))


def test_a_write_held_for_updates_is_made_again_each_update(fake_pine: tuple[FakeGame, int]) -> None:
    game, port = fake_pine
    address = PER_PLAYER_TABLE + 4 * 0x2C + 0x20
    recipe = _recipe([{"write": f"{address}", "type": "u32", "value": "7", "updates": 3}])
    start = game.read(recipes_run.TICK_COUNT, 4)
    _run(port, recipe)
    assert game.read(address, 4) == 7
    assert game.read(recipes_run.TICK_COUNT, 4) - start >= 6


def test_a_text_write_is_terminated_and_padded(fake_pine: tuple[FakeGame, int]) -> None:
    game, port = fake_pine
    _run(port, _recipe([{"write": "0x000c8000", "type": "text", "value": "'level99'"}]))
    assert game.text(0x000C8000) == "level99"
    assert game.read(0x000C8007, 1) == 0


def test_a_call_goes_through_the_call_block(fake_pine: tuple[FakeGame, int]) -> None:
    game, port = fake_pine
    game.results[0x0041ABE8] = 2
    recipe = _recipe(
        [{"call": "0x0041abe8", "args": ["7", "player"]}, {"show": "result", "label": "result"}],
        patches=["call-brains"],
    )
    shown, _ = _run(port, recipe)
    assert shown == {"result": 2}
    assert game.calls == [(0x0041ABE8, [7, HUMAN_TABLE + 0x6D0, 0, 0, 0, 0])]


def test_lua_writes_the_chunk_and_calls_do_string(fake_pine: tuple[FakeGame, int]) -> None:
    game, port = fake_pine
    game.write(recipes_run.SCRIPT_SYSTEM, 4, 0x00BC9800)
    recipe = _recipe(
        [{"let": {"n": "41 + 1"}}, {"lua": 'x = {{{n}, "{t}"}}'}],
        [{"name": "t", "kind": "text", "default": "hi", "help": "text"}],
        patches=["call-brains"],
    )
    _, lines = _run(port, recipe)
    assert game.text(recipes_run.LUA_BUFFER) == 'x = {42, "hi"}'
    assert game.calls == [(recipes_run.SCRIPT_DO_STRING, [0x00BC9800, recipes_run.LUA_BUFFER, 0, 0, 0, 0])]
    assert 'lua: x = {42, "hi"}' in lines


def test_a_call_that_is_never_made_times_out_and_is_withdrawn(fake_pine: tuple[FakeGame, int]) -> None:
    game, port = fake_pine
    game.paused = True
    times = iter(range(100))
    with PineClient(port) as pine:
        runner = recipes_run.Runner(pine, out=lambda _: None, sleep=lambda _: None, clock=lambda: float(next(times)))
        with pytest.raises(recipes.RecipeError, match="did not make the call"):
            runner.call(0x0041ABE8, [], 5.0, "test")
    assert game.read(hook_tools.CALL_BASE, 4) == 0


def test_a_wait_on_a_paused_game_fails(fake_pine: tuple[FakeGame, int]) -> None:
    game, port = fake_pine
    game.paused = True
    times = iter(range(100))
    with PineClient(port) as pine:
        runner = recipes_run.Runner(pine, out=lambda _: None, sleep=lambda _: None, clock=lambda: float(next(times)))
        with pytest.raises(recipes.RecipeError, match="ran no update"):
            runner.wait_updates(1)


def test_require_stops_with_its_message(fake_pine: tuple[FakeGame, int]) -> None:
    _, port = fake_pine
    with pytest.raises(recipes.RecipeError, match="no such thing"):
        _run(port, _recipe([{"require": "0", "message": "no such thing"}]))


def test_the_humans_table(fake_pine: tuple[FakeGame, int]) -> None:
    _, port = fake_pine
    _, lines = _run(port, _recipe([{"humans": True}]))
    assert len(lines) == 3
    assert "Hero" in lines[2] and "  0  " in lines[2]


def test_lua_globals_walk_the_globals_table(fake_pine: tuple[FakeGame, int]) -> None:
    game, port = fake_pine
    system, state, table, nodes = 0x00100000, 0x00100100, 0x00100200, 0x00100400
    game.write(recipes_run.SCRIPT_SYSTEM, 4, system)
    game.write(system + 4, 4, state)
    game.write(state + 0x44, 4, table)
    game.write(table, 4, nodes)
    game.write(table + 4, 4, 4 | 3 << 16)  # tag 4, three nodes
    strings = {"bDone": 0x00101000, "sName": 0x00101100, "value": 0x00101200}
    for text, at in strings.items():
        for k, byte in enumerate(text.encode() + b"\0"):
            game.write(at + 0x10 + k, 1, byte)
    # bDone = 1, sName = "value", and a nil-valued key that is left out.
    game.write(nodes + 0x00, 4, 3)
    game.write(nodes + 0x08, 4, strings["bDone"])
    game.write(nodes + 0x10, 4, 2)
    game.write(nodes + 0x18, 8, struct.unpack("<Q", struct.pack("<d", 1.0))[0])
    game.write(nodes + 40 + 0x00, 4, 3)
    game.write(nodes + 40 + 0x08, 4, strings["sName"])
    game.write(nodes + 40 + 0x10, 4, 3)
    game.write(nodes + 40 + 0x18, 4, strings["value"])
    game.write(nodes + 80 + 0x00, 4, 3)
    game.write(nodes + 80 + 0x08, 4, strings["value"])
    game.write(nodes + 80 + 0x10, 4, 1)
    with PineClient(port) as pine:
        assert recipes_run.lua_globals(pine) == [("bDone", "number", "1"), ("sName", "string", "'value'")]


def test_patches_are_checked_in_the_running_game(fake_pine: tuple[FakeGame, int]) -> None:
    game, port = fake_pine
    patch = Patch("p", "d", "r", (Edit(0x00146030, bytes(4), (0xAC800020).to_bytes(4, "little")),))
    hooks = hook_tools.load_hooks(ROOT / recipes.PATCHES_DIR)
    with PineClient(port) as pine:
        game.write(0x00146030, 4, 0xAC800020)
        with pytest.raises(recipes.RecipeError, match="lacks patch p"):
            recipes_run.check_patches(pine, ["p"], {"p": patch}, hooks)
        game.write(0x00146030, 4, 0)
        recipes_run.check_patches(pine, ["p"], {"p": patch}, hooks)
        with pytest.raises(recipes.RecipeError, match="lacks hook call-brains"):
            recipes_run.check_patches(pine, ["call-brains"], {}, hooks)
        address = hooks["call-brains"].address
        game.write(address, 4, (2 << 26) | (hook_tools.CAVE_BASE >> 2))
        recipes_run.check_patches(pine, ["call-brains"], {}, hooks)
        assert pine.batch([Read(address, 4)]) == [(2 << 26) | (hook_tools.CAVE_BASE >> 2)]


class FakeDebug:
    """The debug server's emulation commands, recorded."""

    def __init__(self) -> None:
        """No commands yet."""
        self.commands: list[dict[str, Any]] = []

    def request(self, command: dict[str, Any], timeout: float = 10.0) -> dict[str, Any]:
        """Record `command` and answer as the fork does."""
        self.commands.append(command)
        return {"ok": True, "frame_before": 10, "frame": 10 + int(command.get("frames", 0))}


def test_emulation_steps_go_to_the_debug_server(fake_pine: tuple[FakeGame, int]) -> None:
    _, port = fake_pine
    debug = FakeDebug()
    recipe = _recipe([{"emulation": "pause"}, {"advance": "2 * 3"}, {"emulation": "resume"}])
    lines: list[str] = []
    with PineClient(port) as pine:
        recipes_run.Runner(pine, debug, out=lines.append, sleep=lambda _: None).run(recipe, {})
    assert debug.commands == [
        {"cmd": "emulation", "action": "pause"},
        {"cmd": "frame_advance", "frames": 6},
        {"cmd": "emulation", "action": "resume"},
    ]
    assert lines == ["advanced 6 frame(s): 10 -> 16"]
    with PineClient(port) as pine, pytest.raises(recipes.RecipeError, match="debug server"):
        recipes_run.Runner(pine, None, out=lines.append).run(recipe, {})


# --- the command: claims ----------------------------------------------------------------------------------------------


def _registry(tmp_path: Path, port: int) -> claims.Registry:
    """A registry of one copy, `pcsx2-z`, whose ini serves PINE on `port`."""
    ini = tmp_path / "root" / "pcsx2-z" / "inis" / "PCSX2.ini"
    ini.parent.mkdir(parents=True)
    ini.write_text(f"[EmuCore]\nEnablePINE = true\nPINESlot = {port}\n", encoding="utf-8")
    return claims.Registry(tmp_path / "root", tmp_path / "claims")


def test_run_refuses_a_copy_the_agent_does_not_hold(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(claims, "default_registry", lambda: _registry(tmp_path, 28999))
    book = recipes.load(ROOT)
    with pytest.raises(claims.ClaimError, match="held by nobody"):
        recipes_cli.run_recipe(book, "humans", "pcsx2-z", "me", [], False)


def test_run_makes_the_steps_on_the_claimed_copy(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, fake_pine: tuple[FakeGame, int], capsys: pytest.CaptureFixture[str]
) -> None:
    _, port = fake_pine
    registry = _registry(tmp_path, port)
    copy = registry.copy("pcsx2-z")
    claim = claims.Claim("pcsx2-z", "me", "now", str(copy.path), 0)
    monkeypatch.setattr(claims, "default_registry", lambda: registry)
    monkeypatch.setattr(claims.Registry, "require_claim", lambda self, agent, name: claim)
    monkeypatch.setattr(claims.Registry, "status", lambda self: [claims.Row(copy, claim, False, True, (1,))])
    book = recipes.load(ROOT)
    assert recipes_cli.run_recipe(book, "find-player", "pcsx2-z", "me", [], True) == 0
    out = capsys.readouterr().out
    assert "name = 'Hero'" in out
    assert '"index": 5' in out.splitlines()[-1]
    with pytest.raises(recipes.RecipeError, match="needs emulation paused"):
        recipes_cli.run_recipe(book, "step", "pcsx2-z", "me", [], False)
    with pytest.raises(recipes.RecipeError, match="lacks hook call-brains"):
        recipes_cli.run_recipe(book, "call", "pcsx2-z", "me", ["function=0x10"], False)
