# SPDX-License-Identifier: GPL-3.0-or-later
"""`check changed`'s clang-tidy under the machine's tidy slots and with the shared result cache (check_cli.py)."""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import time
from collections.abc import Callable, Iterator
from contextlib import contextmanager
from dataclasses import replace
from pathlib import Path

import pytest

from coney_tools import build_cli, check_cli, local_gate, pcsx2_proc, process_lifetime
from coney_tools.build_claims import FAST_LANE, TIDY_LANE, Slots, SlotTimeout
from coney_tools.check_cli import TidyGate
from coney_tools.cli import _build_parser, main
from coney_tools.config import ConfigError
from coney_tools.tidy_cache import TidyCache


def write(root: Path, name: str, text: str = "x\n") -> None:
    """Write a file below `root`, making its folders."""
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def database(build: Path, root: Path, sources: list[str]) -> None:
    """A cl.exe compile database for `sources` in `build`."""
    entries = [
        {
            "directory": build.as_posix(),
            "command": f"C:\\VS\\cl.exe /nologo -I{root / 'src'} /W4 -c {root / name}",
            "file": str(root / name),
        }
        for name in sources
    ]
    build.mkdir(parents=True, exist_ok=True)
    (build / "compile_commands.json").write_text(json.dumps(entries), encoding="utf-8")


@pytest.fixture
def repo(tmp_path: Path) -> Path:
    """A git checkout with a header included by two sources and a source apart."""
    root = tmp_path / "repo"
    root.mkdir()
    subprocess.run(["git", "init", "-q", "-b", "main"], cwd=root, check=True)
    write(root, "src/ai/brain.h", "// brain\n")
    write(root, "src/ai/brain.cpp", '#include "ai/brain.h"\n')
    write(root, "src/ai/walk.cpp", '#include "ai/brain.h"\n')
    write(root, "src/other.cpp", "int x;\n")
    write(root, ".clang-tidy", "Checks: '-*,bugprone-*'\n")
    return root


class FakeTidy:
    """A clang-tidy whose result per source comes from `outputs`, counting the files it was run on."""

    def __init__(self, monkeypatch: pytest.MonkeyPatch, outputs: dict[str, tuple[int, str]]) -> None:
        self.outputs = outputs
        self.ran: list[str] = []
        #: Called with each source as it is tidied, to look at the world from inside the run.
        self.on_run: Callable[[str], None] = lambda _name: None
        monkeypatch.setattr(local_gate, "find_clang_tidy", lambda _explicit: (Path("fake-tidy"), local_gate.TIDY_MAJOR))
        real_run = local_gate.run_command

        def fake_run(command: list[str], cwd: Path) -> tuple[int, str]:
            if command[0] == "git":
                return real_run(command, cwd)
            if command[-1] == "--version":
                return 0, "LLVM version 19.1.5\n"
            self.ran.append(command[-1])
            self.on_run(command[-1])
            return self.outputs.get(command[-1], (0, ""))

        monkeypatch.setattr(local_gate, "run_command", fake_run)


def gate(tmp_path: Path, slots: Slots | None = None, max_wait: float | None = None) -> TidyGate:
    """A gate with a cache in tmp_path, and the given slots (none: no slot is taken)."""
    return TidyGate("me", "repo", TidyCache(tmp_path / "tidy-cache"), slots, max_wait, lambda _line: None)


def test_a_rerun_takes_clean_files_from_the_cache(repo: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    build = repo / "build" / "dev"
    database(build, repo, ["src/ai/brain.cpp", "src/ai/walk.cpp", "src/other.cpp"])
    tidy = FakeTidy(monkeypatch, {})
    first = check_cli._step_tidy(repo, ["src/ai/brain.h", "src/other.cpp"], build, 2, None, gate(tmp_path))
    assert first.ok and sorted(tidy.ran) == ["src/ai/brain.cpp", "src/ai/walk.cpp", "src/other.cpp"]
    tidy.ran.clear()
    second = check_cli._step_tidy(repo, ["src/ai/brain.h", "src/other.cpp"], build, 2, None, gate(tmp_path))
    assert second.ok and tidy.ran == [] and "3 cached" in second.detail


def test_an_edit_reruns_only_the_files_it_reaches(repo: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    build = repo / "build" / "dev"
    database(build, repo, ["src/ai/brain.cpp", "src/ai/walk.cpp", "src/other.cpp"])
    tidy = FakeTidy(monkeypatch, {})
    changed = ["src/ai/brain.h", "src/other.cpp"]
    check_cli._step_tidy(repo, changed, build, 2, None, gate(tmp_path))
    tidy.ran.clear()
    write(repo, "src/other.cpp", "int y;\n")
    result = check_cli._step_tidy(repo, changed, build, 2, None, gate(tmp_path))
    assert result.ok and tidy.ran == ["src/other.cpp"] and "2 cached" in result.detail


def test_a_failing_file_is_never_cached(repo: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    build = repo / "build" / "dev"
    database(build, repo, ["src/other.cpp"])
    tidy = FakeTidy(monkeypatch, {"src/other.cpp": (1, "src/other.cpp:1:1: error: x [bugprone-y]\n")})
    assert not check_cli._step_tidy(repo, ["src/other.cpp"], build, 1, None, gate(tmp_path)).ok
    assert not check_cli._step_tidy(repo, ["src/other.cpp"], build, 1, None, gate(tmp_path)).ok
    assert tidy.ran == ["src/other.cpp", "src/other.cpp"]


def test_the_run_holds_a_tidy_slot_and_reports_its_progress(
    repo: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    build = repo / "build" / "dev"
    database(build, repo, ["src/other.cpp"])
    slots = Slots(tmp_path / "claims", 1, lane=TIDY_LANE)
    seen: list[str] = []
    tidy = FakeTidy(monkeypatch, {})

    def look(_name: str) -> None:
        claim = slots.status().slots[0].claim
        seen.append(f"{claim.agent} {claim.preset}" if claim else "free")

    tidy.on_run = look
    result = check_cli._step_tidy(repo, ["src/other.cpp"], build, 1, None, gate(tmp_path, slots))
    assert result.ok and seen == ["me 1 file"]
    assert slots.status().slots[0].claim is None  # released at the end


def test_a_full_tidy_lane_gives_up_after_the_wait(repo: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    build = repo / "build" / "dev"
    database(build, repo, ["src/other.cpp"])
    slots = Slots(tmp_path / "claims", 1, lane=TIDY_LANE, sleep=lambda _s: None)
    other = slots.join("someone", "wt", "9 files", False, os.getpid())
    assert slots.try_claim(other) is not None
    slots.leave(other)
    tidy = FakeTidy(monkeypatch, {})
    with pytest.raises(SlotTimeout):
        check_cli._step_tidy(repo, ["src/other.cpp"], build, 1, None, gate(tmp_path, slots, max_wait=0.0))
    assert tidy.ran == []


def test_an_all_cached_run_needs_no_slot(repo: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    build = repo / "build" / "dev"
    database(build, repo, ["src/other.cpp"])
    FakeTidy(monkeypatch, {})
    assert check_cli._step_tidy(repo, ["src/other.cpp"], build, 1, None, gate(tmp_path)).ok
    slots = Slots(tmp_path / "claims", 1, lane=TIDY_LANE, sleep=lambda _s: None)
    other = slots.join("someone", "wt", "9 files", False, os.getpid())
    assert slots.try_claim(other) is not None
    slots.leave(other)
    assert check_cli._step_tidy(repo, ["src/other.cpp"], build, 1, None, gate(tmp_path, slots, max_wait=0.0)).ok


def test_tidy_jobs_come_from_the_config_or_a_build_slots_share(tmp_path: Path) -> None:
    write(tmp_path, "coney.local.example.toml", "")
    assert check_cli.tidy_jobs(tmp_path, 16) == 8  # build_slots defaults to 2
    write(tmp_path, "coney.local.toml", "build_slots = 5\n")
    assert check_cli.tidy_jobs(tmp_path, 16) == 3
    write(tmp_path, "coney.local.toml", "build_slots = 5\ntidy_jobs = 6\n")
    assert check_cli.tidy_jobs(tmp_path, 16) == 6


def test_the_default_agent_is_the_worktrees_name(tmp_path: Path) -> None:
    assert check_cli.default_agent(tmp_path / "fix-fence-fade", {}) == "fix-fence-fade"
    assert check_cli.default_agent(tmp_path / "odd name!", {}) == "odd_name_"
    assert check_cli.default_agent(tmp_path / "wt", {"CONEY_AGENT": "reimpl-7"}) == "reimpl-7"


def test_check_changed_flags() -> None:
    args = _build_parser().parse_args(
        ["check", "changed", "--agent", "a", "--max-wait", "5", "--no-cache", "--detach", "--jobs", "2"]
    )
    assert (args.agent, args.max_wait, args.no_cache, args.detach, args.jobs) == ("a", 5.0, True, True, 2)
    defaults = _build_parser().parse_args(["check", "changed"])
    assert (defaults.agent, defaults.max_wait, defaults.no_cache, defaults.detach) == (None, None, False, False)
    assert _build_parser().parse_args(["check", "result"]).command == "result"


def test_a_wait_past_the_prompt_cache_needs_detach() -> None:
    assert check_cli.max_wait_minutes(None, detached=False) == build_cli.MAX_WAIT_CAP
    assert check_cli.max_wait_minutes(None, detached=True) == build_cli.DETACHED_MAX_WAIT
    with pytest.raises(ConfigError, match="--detach"):
        check_cli.max_wait_minutes(90, detached=False)


def test_check_result_without_a_run(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    root = tmp_path / "wt"
    write(root, "coney.local.example.toml", "")
    write(root, "coney.local.toml", f"scratch_dir = '{tmp_path / 'scratch'}'\n")
    monkeypatch.chdir(root)
    assert main(["check", "result"]) == 1
    assert "no detached check" in capsys.readouterr().out


def test_check_result_reports_a_finished_run(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    root = tmp_path / "wt"
    write(root, "coney.local.example.toml", "")
    write(root, "coney.local.toml", f"scratch_dir = '{tmp_path / 'scratch'}'\n")
    monkeypatch.chdir(root)
    folder = tmp_path / "scratch" / "wt"
    write(folder, check_cli.DETACHED_OUTPUT, "ok   clang-tidy: 3 files (3 cached)\n")
    build_cli._write_record(
        folder / check_cli.RESULT_FILE,
        {"state": "done", "agent": "a", "pid": 1, "started": 0.0, "exit_code": 0, "log": str(folder)},
    )
    assert main(["check", "result"]) == 0
    assert "3 cached" in capsys.readouterr().out
    running = {"state": "running", "agent": "a", "pid": 1, "process_started": 100.0, "started": 0.0, "log": ""}
    build_cli._write_record(folder / check_cli.RESULT_FILE, running)
    monkeypatch.setattr(pcsx2_proc, "pid_alive", lambda _pid: True)
    monkeypatch.setattr(process_lifetime, "process_started", lambda _pid: 100.0)
    assert main(["check", "result"]) == 75
    # The pid now belongs to a process that started an hour later: the gate is gone, not still running.
    monkeypatch.setattr(process_lifetime, "process_started", lambda _pid: 3700.0)
    assert main(["check", "result"]) == 1
    assert "ended without a result" in capsys.readouterr().out


def test_a_file_edited_during_its_run_is_not_stored_as_clean(
    repo: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    """clang-tidy saw the old contents; storing "clean" under the old key would hand a later gate a stale pass."""
    build = repo / "build" / "dev"
    database(build, repo, ["src/other.cpp"])
    tidy = FakeTidy(monkeypatch, {})
    tidy.on_run = lambda _name: write(repo, "src/other.cpp", "int edited;\n")
    assert check_cli._step_tidy(repo, ["src/other.cpp"], build, 1, None, gate(tmp_path)).ok
    write(repo, "src/other.cpp", "int x;\n")  # back to the contents that were tidied
    tidy.on_run = lambda _name: None
    tidy.ran.clear()
    assert check_cli._step_tidy(repo, ["src/other.cpp"], build, 1, None, gate(tmp_path)).ok
    assert tidy.ran == ["src/other.cpp"]


def test_an_interrupt_drops_the_files_not_started(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """Ctrl+C must let go of the slot now, not after every queued file has run."""
    ran: list[str] = []

    def slow(command: list[str], _root: Path, name: str) -> tuple[str, int, list[str], list[str]]:
        ran.append(name)
        time.sleep(0.2)
        return name, 0, [], []

    monkeypatch.setattr(check_cli, "_tidy_one", slow)

    def interrupt(_done: int) -> None:
        raise KeyboardInterrupt

    commands = [(["tidy", f"f{index}.cpp"], f"f{index}.cpp") for index in range(8)]
    started = time.monotonic()
    with pytest.raises(KeyboardInterrupt):
        check_cli._run_tidy(commands, tmp_path, 1, interrupt)
    assert time.monotonic() - started < 1.0
    assert len(ran) <= 2


def test_a_small_run_takes_the_fast_lane_past_a_busy_tidy_lane(
    repo: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    build = repo / "build" / "dev"
    database(build, repo, ["src/other.cpp"])
    tidy_lane = Slots(tmp_path / "claims", 1, lane=TIDY_LANE, sleep=lambda _s: None)
    other = tidy_lane.join("someone", "wt", "250 files", False, os.getpid())
    assert tidy_lane.try_claim(other) is not None
    tidy_lane.leave(other)
    fast = Slots(tmp_path / "claims", 1, lane=FAST_LANE, sleep=lambda _s: None)
    seen: list[str] = []
    tidy = FakeTidy(monkeypatch, {})

    def look(_name: str) -> None:
        claim = fast.status().slots[0].claim
        seen.append(claim.agent if claim else "free")

    tidy.on_run = look
    both = replace(gate(tmp_path, tidy_lane, max_wait=0.0), small_slots=fast)
    assert check_cli._step_tidy(repo, ["src/other.cpp"], build, 1, None, both).ok
    assert seen == ["me"]
    # A run past SMALL_TIDY_FILES waits for the tidy lane as before.
    monkeypatch.setattr(check_cli, "SMALL_TIDY_FILES", 0)
    write(repo, "src/other.cpp", "int big;\n")
    with pytest.raises(SlotTimeout):
        check_cli._step_tidy(repo, ["src/other.cpp"], build, 1, None, both)


def test_files_another_gate_tidied_during_the_wait_are_not_run_again(
    repo: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    build = repo / "build" / "dev"
    database(build, repo, ["src/ai/brain.cpp", "src/ai/walk.cpp"])
    tidy = FakeTidy(monkeypatch, {})
    elsewhere = TidyGate("other", "wt2", TidyCache(tmp_path / "other-cache"), None, None, lambda _line: None)
    assert check_cli._step_tidy(repo, ["src/ai/brain.h"], build, 1, None, elsewhere).ok
    tidy.ran.clear()
    real_slot = check_cli._tidy_slot

    @contextmanager
    def slot_after_the_other_gate(held: TidyGate | None, files: int) -> Iterator[Callable[[int], None]]:
        # While this gate waited, the other one finished and its results reached the shared cache.
        shutil.copytree(tmp_path / "other-cache", tmp_path / "tidy-cache", dirs_exist_ok=True)
        with real_slot(held, files) as progress:
            yield progress

    monkeypatch.setattr(check_cli, "_tidy_slot", slot_after_the_other_gate)
    result = check_cli._step_tidy(repo, ["src/ai/brain.h"], build, 1, None, gate(tmp_path))
    assert result.ok and tidy.ran == [] and "2 cached" in result.detail
