# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the commands that write run records: `pcsx2 record-run`, `pcsx2 recipes run --record` and
`trace original --run-record`, over a fake PINE server (no test starts PCSX2) and a made-up checkout."""

from __future__ import annotations

import hashlib
import json
import shutil
import socket
import time
from datetime import date
from functools import partial
from pathlib import Path
from typing import Any

import pytest
from conventions_support import REPO
from pine_support import dead_port, fake_pcsx2

from coney_tools import identity, pcsx2_cli, recipes, recipes_cli, recipes_run, run_records, run_records_cli
from coney_tools import pcsx2_claims as claims
from coney_tools.cli import main
from coney_tools.config import ConfigError
from coney_tools.identity import Identity
from coney_tools.pcsx2_states import Entry
from coney_tools.pine import PineClient
from coney_tools.recorder import Recording

DAY = date(2026, 10, 23)
ID = "r-20261023-grab-reach"


@pytest.fixture
def checkout(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> Path:
    """A made-up checkout with the real conventions, recipes and patches and a state library folder; the process is
    inside it, the clock says 2026-10-23 and the hook vouched for the agent (its identity file)."""
    root = tmp_path / "ws" / "repos" / "coney"
    (root / ".github").mkdir(parents=True)
    (root / "coney.local.example.toml").write_text("", encoding="utf-8")
    (root / "coney.local.toml").write_text(
        f'pcsx2_states_dir = "{(tmp_path / "library").as_posix()}"\n', encoding="utf-8"
    )
    shutil.copy(REPO / ".github" / "conventions.yml", root / ".github" / "conventions.yml")
    (root / "research" / "traces").mkdir(parents=True)
    shutil.copy(REPO / recipes.RECIPES_FILE, root / recipes.RECIPES_FILE)
    shutil.copytree(REPO / recipes.PATCHES_DIR, root / recipes.PATCHES_DIR)
    shutil.copytree(REPO / "research" / "traces" / "scenarios", root / "research" / "traces" / "scenarios")
    shutil.copy(REPO / "research" / "traces" / "fields.toml", root / "research" / "traces" / "fields.toml")
    monkeypatch.chdir(root)
    monkeypatch.setattr(run_records_cli, "today", lambda: DAY)
    monkeypatch.setattr(identity, "_current", Identity("tooling-1", "engineer", "claude-opus-5-5", verified=True))
    return root


def make_library(root: Path, *entries: Entry) -> None:
    """A state library with `entries` in its catalog (no state files: only the catalog is read)."""
    folder = root.parent.parent.parent / "library"
    folder.mkdir(exist_ok=True)
    (folder / "catalog.json").write_text(
        json.dumps({"format": 1, "states": [e.to_json() for e in entries]}), encoding="utf-8"
    )


def hold_copy(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, port: int, *, running: bool = True, holder: str = "tooling-1"
) -> claims.Copy:
    """Make `pcsx2-z`, serving PINE on `port`, a copy `holder` has claimed, running unless `running` is false."""
    ini = tmp_path / "root" / "pcsx2-z" / "inis" / "PCSX2.ini"
    ini.parent.mkdir(parents=True, exist_ok=True)
    ini.write_text(f"[EmuCore]\nEnablePINE = true\nPINESlot = {port}\n", encoding="utf-8")
    registry = claims.Registry(tmp_path / "root", tmp_path / "claims")
    copy = registry.copy("pcsx2-z")
    claim = claims.Claim("pcsx2-z", holder, "now", str(copy.path), 0)

    def require(self: claims.Registry, agent: str, name: str) -> claims.Claim:
        if agent != holder:
            raise claims.ClaimError(f"{name} is held by {holder}, not {agent}; claim it first")
        return claim

    monkeypatch.setattr(claims, "default_registry", lambda: registry)
    monkeypatch.setattr(claims.Registry, "require_claim", require)
    monkeypatch.setattr(
        claims.Registry, "status", lambda self: [claims.Row(copy, claim, False, running, (1,) if running else ())]
    )
    return copy


def record_run(*extra: str, state: str = "unknown", name: str = "grab reach") -> list[str]:
    """The arguments of a `pcsx2 record-run` of the claimed copy."""
    return [
        "pcsx2",
        "record-run",
        "--agent",
        "tooling-1",
        "--copy",
        "pcsx2-z",
        "--state",
        state,
        "--name",
        name,
        "--method",
        "a watch on the grab's result over 50 grabs",
        *extra,
    ]


# --- PINE: the version ----------------------------------------------------------------------------------------------


def test_pcsx2_says_its_version_over_pine() -> None:
    with fake_pcsx2("PCSX2 v2.9.94-5-gabc1234") as port, PineClient(port) as pine:
        assert pine.version() == "PCSX2 v2.9.94-5-gabc1234"


def test_the_fake_pcsx2_stops_answering_when_its_block_ends() -> None:
    with fake_pcsx2() as port:
        with PineClient(port) as pine:
            pine.version()
        time.sleep(0.2)  # the server thread is back in `accept`, where closing the socket does not wake it on Linux
    # A server that still listened would hold the port, so the bind would fail; binding is quick on every system, where
    # a connect to a dead port waits 2 s on Windows. Only another program taking the port in this instant could fail it.
    socket.create_server(("127.0.0.1", port)).close()


def test_a_dead_port_takes_no_connection_and_keeps_other_servers_off() -> None:
    with dead_port() as port:
        # Any OSError: Linux and Windows refuse the connect, macOS may let it time out (see `dead_port`).
        with pytest.raises(OSError):
            socket.create_connection(("127.0.0.1", port), timeout=0.3).close()
        with pytest.raises(OSError):
            socket.create_server(("127.0.0.1", port)).close()


def test_the_version_is_cut_to_what_a_record_allows() -> None:
    assert run_records_cli.short_version("PCSX2 nightly build") == "PCSX2 nightly build"
    assert run_records_cli.short_version("") == "unknown"
    assert len(run_records_cli.short_version("x" * 90)) <= 40
    assert run_records_cli.short_version("PCSX2 2.9.129 (Qt)") == "v2.9.129"
    assert run_records_cli.short_version("PCSX2 v2.9.94-5-gabc1234") == "v2.9.94"


# --- pcsx2 record-run -------------------------------------------------------------------------------------------------


def test_record_run_writes_a_record_with_what_the_tool_knows(
    checkout: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    with fake_pcsx2() as port:
        hold_copy(tmp_path, monkeypatch, port)
        status = main(record_run("--value", "reach_m=1.1", "--value", "grabs=50", "--address", "result=0x00271f40"))
    assert status == 0
    record = run_records.load(checkout, ID)
    assert (record.agent, record.agent_type, record.model, record.source) == (
        "tooling-1",
        "engineer",
        "claude-opus-5-5",
        "hook",
    )
    assert (record.pcsx2_version, record.copy, record.date, record.kind) == (
        "v2.9.94",
        "pcsx2-z",
        "2026-10-23",
        "method",
    )
    assert record.tool == "pcsx2 record-run" and record.method == "a watch on the grab's result over 50 grabs"
    assert record.values == {"reach_m": 1.1, "grabs": 50} and record.addresses == {"result": 0x00271F40}
    assert record.state == "unknown" and record.patches == ()
    out = capsys.readouterr().out
    assert ID in out and "research/runs/" in out.replace("\\", "/")


def test_a_run_by_an_agent_the_hook_did_not_name_is_marked_unverified(
    checkout: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr(identity, "_current", Identity())
    with fake_pcsx2() as port:
        hold_copy(tmp_path, monkeypatch, port)
        assert main(record_run()) == 0
    assert run_records.load(checkout, ID).source == "unverified"


def test_record_run_takes_the_patches_of_the_library_state(
    checkout: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    make_library(checkout, Entry("pedestrian-grab", "level99", 3, "facing a pedestrian", patches=("scripted-pad",)))
    with fake_pcsx2() as port:
        hold_copy(tmp_path, monkeypatch, port)
        assert main(record_run("--patch", "right-stick", state="pedestrian-grab")) == 0
    record = run_records.load(checkout, ID)
    assert record.state == "pedestrian-grab" and record.patches == ("right-stick", "scripted-pad")


def test_a_state_the_library_does_not_hold_is_refused(
    checkout: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    make_library(checkout)
    with fake_pcsx2() as port:
        hold_copy(tmp_path, monkeypatch, port)
        assert main(record_run(state="no-such-state")) == 2
    assert "no state 'no-such-state'" in capsys.readouterr().err
    assert not (checkout / "research" / "runs").exists()


def test_record_run_refuses_a_copy_the_agent_does_not_hold(
    checkout: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    with fake_pcsx2() as port:
        hold_copy(tmp_path, monkeypatch, port, holder="someone-else")
        assert main(record_run()) == 2
    assert "held by someone-else, not tooling-1" in capsys.readouterr().err
    assert not (checkout / "research" / "runs").exists()


def test_record_run_refuses_a_copy_that_runs_nothing(
    checkout: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    with fake_pcsx2() as port:
        hold_copy(tmp_path, monkeypatch, port, running=False)
        assert main(record_run()) == 2
    assert "pcsx2-z runs no PCSX2" in capsys.readouterr().err


def test_record_run_writes_nothing_when_pcsx2_does_not_answer(
    checkout: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    # A short connect timeout: the dead port refuses only after about 2 s on Windows and may time out on macOS.
    monkeypatch.setattr(run_records_cli, "PineClient", partial(PineClient, timeout=0.3))
    with dead_port() as port:
        hold_copy(tmp_path, monkeypatch, port)
        assert main(record_run()) == 2
    assert "no PINE server" in capsys.readouterr().err
    assert not (checkout / "research" / "runs").exists()


def test_record_run_needs_the_state_to_be_named(checkout: Path, capsys: pytest.CaptureFixture[str]) -> None:
    arguments = record_run()
    del arguments[arguments.index("--state") : arguments.index("--state") + 2]
    with pytest.raises(SystemExit) as stop:
        main(arguments)
    assert stop.value.code == 2 and "--state" in capsys.readouterr().err


BAD_PAIRS = [
    (("--value", "reach"), "NAME=NUMBER"),
    (("--value", "reach=far"), "not a number"),
    (("--value", "reach=nan"), "finite"),
    (("--address", "a=0x1ffffffff"), "an address from 0 to 0xffffffff"),
    (("--address", "a=zz"), "not an address"),
    (("--value", "a=1", "--value", "a=2"), "given twice"),
]


@pytest.mark.parametrize(("extra", "message"), BAD_PAIRS)
def test_a_bad_value_or_address_is_refused_before_anything_is_written(
    checkout: Path,
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    extra: tuple[str, ...],
    message: str,
) -> None:
    with fake_pcsx2() as port:
        hold_copy(tmp_path, monkeypatch, port)
        assert main(record_run(*extra)) == 2
    assert message in capsys.readouterr().err
    assert not (checkout / "research" / "runs").exists()


def test_two_runs_of_one_name_on_one_day_are_not_merged(
    checkout: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    with fake_pcsx2() as port:
        hold_copy(tmp_path, monkeypatch, port)
        assert main(record_run("--value", "n=1")) == 0
        assert main(record_run("--value", "n=2")) == 2
    assert "already exists" in capsys.readouterr().err
    assert run_records.load(checkout, ID).values == {"n": 1}


def test_numbers_and_addresses_are_read_as_written() -> None:
    assert run_records_cli.parse_values(["a=1", "b=-2.5", "c=0x10"]) == {"a": 1, "b": -2.5, "c": 16}
    assert run_records_cli.parse_addresses(["a=0x00271f40", "b=0x1000"]) == {"a": 0x271F40, "b": 0x1000}


# --- pcsx2 recipes run --record ---------------------------------------------------------------------------------------


def one_recipe(**changes: Any) -> recipes.RecipeBook:
    """A book of one recipe that shows a number, a computed number and a text, and needs no memory."""
    raw = {
        "summary": "s",
        "how": "h",
        "research": ["docs/research/characters.md"],
        "ghidra": ["0x00228730 Humans_StaticInit"],
        "evidence": "confirmed-code",
        "confirmed_runtime": {"date": "2026-10-21", "copy": "pcsx2-d", "pcsx2": "2.9.129", "state": "test"},
        "params": [{"name": "n", "kind": "number", "default": "3", "help": "a number"}],
        "steps": [
            {"let": {"twice": "n * 2"}},
            {"show": "twice", "label": "twice"},
            {"show": "n / 4", "label": "quarter", "format": "float"},
            *changes.pop("steps", []),
        ],
        **changes,
    }
    book = recipes.RecipeBook(groups=("Other",))
    book.recipes["count"] = recipes.parse_recipe("count", raw, ["Other"], {"scripted-pad", "right-stick"})
    return book


def run_recipe(
    root: Path, port: int, *, record: bool, state: str = "unknown", book: recipes.RecipeBook | None = None
) -> int:
    """`recipe run count --arg n=6` on the claimed copy, recording as asked."""
    request = run_records_cli.RecordRequest(state=state, name=None) if record else None
    return recipes_cli.run_recipe(book or one_recipe(), "count", "pcsx2-z", "tooling-1", ["n=6"], False, request)


def test_a_recipe_run_with_record_writes_what_it_showed(
    checkout: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    make_library(checkout, Entry("pedestrian-grab", "level99", 3, "facing a pedestrian", patches=("scripted-pad",)))
    with fake_pcsx2() as port:
        hold_copy(tmp_path, monkeypatch, port)
        assert run_recipe(checkout, port, record=True, state="pedestrian-grab") == 0
    record = run_records.load(checkout, "r-20261023-count")
    assert (record.kind, record.name, record.tool) == ("recipe", "count", "pcsx2 recipes run")
    assert record.args == {"n": "6"} and record.values == {"twice": 12, "quarter": 1.5}
    assert record.state == "pedestrian-grab" and record.patches == ("scripted-pad",)
    assert (record.pcsx2_version, record.copy, record.agent) == ("v2.9.94", "pcsx2-z", "tooling-1")
    assert "r-20261023-count" in capsys.readouterr().out


def test_a_recipe_run_without_record_writes_nothing(
    checkout: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    with fake_pcsx2() as port:
        hold_copy(tmp_path, monkeypatch, port)
        assert run_recipe(checkout, port, record=False) == 0
    assert not (checkout / "research" / "runs").exists()


def test_a_text_the_recipe_showed_is_not_recorded(
    checkout: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    book = one_recipe(steps=[{"text": "0x000c0000", "label": "name"}])
    with fake_pcsx2() as port:
        hold_copy(tmp_path, monkeypatch, port)
        assert run_recipe(checkout, port, record=True, book=book) == 0
    assert set(run_records.load(checkout, "r-20261023-count").values) == {"twice", "quarter"}
    assert "1 shown text left out" in capsys.readouterr().out


def test_a_recipe_that_fails_writes_no_record(checkout: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    book = one_recipe(steps=[{"require": "0", "message": "no such human"}])
    with fake_pcsx2() as port:
        hold_copy(tmp_path, monkeypatch, port)
        with pytest.raises(recipes.RecipeError, match="no such human"):
            run_recipe(checkout, port, record=True, book=book)
    assert not (checkout / "research" / "runs").exists()


def test_record_needs_the_state_and_state_needs_record(checkout: Path, capsys: pytest.CaptureFixture[str]) -> None:
    main(["pcsx2", "recipe", "run", "where", "--copy", "pcsx2-z", "--agent", "tooling-1", "--record"])
    assert "--record needs --state" in capsys.readouterr().err
    main(["pcsx2", "recipe", "run", "where", "--copy", "pcsx2-z", "--agent", "tooling-1", "--state", "unknown"])
    assert "--state and --name belong to --record" in capsys.readouterr().err


# --- pcsx2 record --run-record ----------------------------------------------------------------------------------------


def fake_recording() -> Recording:
    """A recording of 600 updates, two of them missed, with a header of three columns."""
    return Recording(header=["step", "player.x", "player.y"], rows=[[0, "1.0", "2.0"]], missed=[4, 9], updates=600)


def test_a_trace_record_holds_the_counts_and_the_hash_of_the_csv() -> None:
    csv = b"step,player.x\r\n0,1.0\r\n"
    values, hashes = run_records_cli.trace_facts(fake_recording(), csv)
    assert values == {"updates": 600, "missed_updates": 2, "columns": 2}
    assert hashes == {"trace_csv": hashlib.sha256(csv).hexdigest()}


def test_a_traced_run_writes_its_record_beside_the_trace(
    checkout: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    class FakeRecorder:
        def __init__(self, *_: object, **__: object) -> None: ...

        def record(self) -> Recording:
            return fake_recording()

    monkeypatch.setattr(pcsx2_cli, "Recorder", FakeRecorder)
    with fake_pcsx2() as port:
        copy = hold_copy(tmp_path, monkeypatch, port)
        claim = claims.Claim("pcsx2-z", "tooling-1", "now", str(copy.path), port)
        monkeypatch.setattr(claims.Registry, "claim_for_run", lambda self, agent, d, pid: (claim, False))
        request = run_records_cli.RecordRequest(state="unknown", name="bat cross")
        scenario = checkout / "research" / "traces" / "scenarios" / "bat_cross_held.toml"
        out = tmp_path / "scratch" / "bat.csv"
        status = pcsx2_cli.run_record(
            scenario, out, None, True, False, (None, None, None), "tooling-1", ["x=1"], None, request
        )
    assert status == 0 and out.is_file()
    record = run_records.load(checkout, "r-20261023-bat-cross")
    assert (record.kind, record.name, record.tool) == ("trace", "bat_cross_held", "trace original")
    assert record.args == {"x": "1"} and record.values["updates"] == 600 and record.values["missed_updates"] == 2
    assert record.hashes == {"trace_csv": hashlib.sha256(out.read_bytes()).hexdigest()}
    assert record.patches == ("call-brains", "right-stick", "scripted-pad") and record.pcsx2_version == "v2.9.94"
    assert "r-20261023-bat-cross" in capsys.readouterr().out


# --- every rule is checked before the run -----------------------------------------------------------------------------


def test_a_recipe_run_is_refused_before_it_runs_when_its_record_would_break_a_rule(
    checkout: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr(identity, "_current", Identity("tooling-1", "engineer", "m" * 65, verified=True))
    ran: list[bool] = []

    def run(*_: object) -> dict[str, object]:
        ran.append(True)
        return {}

    monkeypatch.setattr(recipes_run.Runner, "run", run)
    with fake_pcsx2() as port:
        hold_copy(tmp_path, monkeypatch, port)
        with pytest.raises(ConfigError, match=r"who.model"):
            run_recipe(checkout, port, record=True)
    assert not ran and not (checkout / "research" / "runs").exists()


def test_a_recipe_run_keeps_the_numbers_a_record_can_hold(
    checkout: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    shown = {"café": 1, **{f"n{n}": n for n in range(70)}}
    monkeypatch.setattr(recipes_run.Runner, "run", lambda *_: dict(shown))
    with fake_pcsx2() as port:
        hold_copy(tmp_path, monkeypatch, port)
        assert run_recipe(checkout, port, record=True) == 0
    record = run_records.load(checkout, "r-20261023-count")
    assert "café" not in record.values and len(record.values) + len(record.args) == 64
    assert "8 shown numbers left out" in capsys.readouterr().out


def test_a_trace_is_refused_before_it_runs_when_its_record_would_break_a_rule(
    checkout: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    made: list[bool] = []

    class FakeRecorder:
        def __init__(self, *_: object, **__: object) -> None:
            made.append(True)

        def record(self) -> Recording:
            return fake_recording()

    monkeypatch.setattr(pcsx2_cli, "Recorder", FakeRecorder)
    with fake_pcsx2() as port:
        copy = hold_copy(tmp_path, monkeypatch, port)
        claim = claims.Claim("pcsx2-z", "tooling-1", "now", str(copy.path), port)
        monkeypatch.setattr(claims.Registry, "claim_for_run", lambda self, agent, d, pid: (claim, False))
        request = run_records_cli.RecordRequest(state="unknown", name="bat cross")
        scenario = checkout / "research" / "traces" / "scenarios" / "bat_cross_held.toml"
        out = tmp_path / "scratch" / "bat.csv"
        with pytest.raises(ConfigError, match="printable ASCII"):
            pcsx2_cli.run_record(
                scenario, out, None, True, False, (None, None, None), "tooling-1", ["x=é"], None, request
            )
    assert not made and not out.exists()
