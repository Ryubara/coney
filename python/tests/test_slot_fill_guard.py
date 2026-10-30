# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for .claude/hooks/slot_fill_guard.py, the Stop hook that keeps the coordinator from ending a turn with an
engineer slot free for no reason, and for its place in the dispatcher and the settings (docs/guides/slot-fill.md).

The decision is `coney-tools task slots --fill-check` (tests/test_slot_fill.py); these tests give the hook a stand-in
for that command and check what it sends, whom it holds to the rule, when it blocks and what it does when the command
cannot run. One test runs the real command in a scratch folder with GitHub unreachable.
"""

import importlib.util
import json
import subprocess
import sys
from pathlib import Path
from types import ModuleType
from typing import Any

import pytest
from audit_support import CONVENTIONS
from conventions_support import REPO
from hooks_support import DISPATCH, HOOKS, load_hook_module, settings

SCRIPT = REPO / ".claude" / "hooks" / "slot_fill_guard.py"
roles = load_hook_module("roles")
hooklib = load_hook_module("hooklib")
CONFIG = hooklib.load_config(HOOKS / "hooks.toml")


def load() -> ModuleType:
    """The hook script as a module."""
    spec = importlib.util.spec_from_file_location("slot_fill_guard", SCRIPT)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module  # a dataclass looks its module up here
    spec.loader.exec_module(module)
    return module


GUARD = load()
BLOCKED = "Blocked: 2 task slot(s) and 1 fix slot(s) are free and work can be started.\n  - #7 [P-high] tools: Do it"


class Fake:
    """A stand-in for `coney-tools`: records what it was run with and answers as told."""

    def __init__(self, code: int = 0, out: str = "") -> None:
        self.code, self.out = code, out
        self.calls: list[list[str]] = []

    def __call__(self, args: list[str]) -> tuple[int, str]:
        self.calls.append(args)
        return self.code, self.out


def stop(agent_type: object = "coordinator", **extra: Any) -> dict[str, Any]:
    """The hook input of a Stop event."""
    payload: dict[str, Any] = {"hook_event_name": "Stop", "session_id": "sess-1", "stop_hook_active": False, **extra}
    if agent_type is not None:
        payload["agent_type"] = agent_type
    return payload


@pytest.fixture(autouse=True)
def log_folder(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> Path:
    """The hooks' log goes to a temporary folder."""
    monkeypatch.setattr("tempfile.gettempdir", lambda: str(tmp_path))
    return tmp_path


def log_text(folder: Path) -> str:
    path = folder / "coney-hooks.log"
    return path.read_text(encoding="utf-8") if path.exists() else ""


# --- what it asks and when it blocks --------------------------------------------------------------------------------


def test_the_coordinators_turn_end_is_sent_to_the_tool_with_its_session() -> None:
    fake = Fake()
    assert GUARD.judge(stop(), fake) == GUARD.Verdict()
    assert fake.calls == [["task", "slots", "--fill-check", "--session", "sess-1"]]


def test_a_session_without_an_id_is_counted_as_one_called_unknown() -> None:
    fake = Fake()
    payload = stop()
    del payload["session_id"]
    GUARD.judge(payload, fake)
    assert fake.calls[0][-2:] == ["--session", "unknown"]


def test_a_failing_check_blocks_with_the_tools_message() -> None:
    verdict = GUARD.judge(stop(), Fake(1, BLOCKED))
    assert verdict.block == BLOCKED and verdict.note is None


def test_a_crash_that_exits_with_1_blocks_nothing(log_folder: Path) -> None:
    verdict = GUARD.judge(stop(), Fake(1, "Traceback (most recent call last):\n  ValueError: boom"))
    assert verdict.block is None and "could not run (exit 1)" in (verdict.note or "")
    assert "could not check the places (exit 1)" in log_text(log_folder)


@pytest.mark.parametrize("code", [2, 3, 127, -9])
def test_a_tool_that_cannot_judge_passes_the_turn_with_a_note_and_a_log_line(log_folder: Path, code: int) -> None:
    verdict = GUARD.judge(stop(), Fake(code, "uv: command not found"))
    assert verdict.block is None and f"could not run (exit {code})" in (verdict.note or "")
    assert f"(exit {code})" in log_text(log_folder)


def test_a_passing_check_is_silent() -> None:
    verdict = GUARD.judge(stop(), Fake(0, "ok: every slot is full (task slots 3 of 3 in use)"))
    assert verdict == GUARD.Verdict()


def test_a_warning_of_the_tool_is_a_note_and_a_log_line(log_folder: Path) -> None:
    out = "ok, not checked: task slots 1 of 3 in use\nwarning: GitHub could not be read (offline)"
    verdict = GUARD.judge(stop(), Fake(0, out))
    assert verdict.block is None and "passed with a warning: GitHub could not be read (offline)" in (verdict.note or "")
    assert "let through with a check skipped: GitHub could not be read" in log_text(log_folder)


@pytest.mark.parametrize("agent_type", ["engineer", "tooling", "reviewer", None, "general-purpose", ""])
def test_only_the_coordinator_is_held_to_the_rule(agent_type: object) -> None:
    fake = Fake(1, BLOCKED)
    assert GUARD.judge(stop(agent_type), fake) == GUARD.Verdict() and fake.calls == []


@pytest.mark.parametrize("payload", [None, "x", [], 3, {"hook_event_name": "PreToolUse", "agent_type": "coordinator"}])
def test_input_that_is_no_stop_event_passes(payload: object) -> None:
    fake = Fake(1, BLOCKED)
    assert GUARD.judge(payload, fake) == GUARD.Verdict() and fake.calls == []


def test_an_event_name_that_is_missing_is_still_judged_for_a_coordinator() -> None:
    payload = stop()
    del payload["hook_event_name"]
    assert GUARD.judge(payload, Fake(1, BLOCKED)).block == BLOCKED


def test_the_real_runner_reports_a_tool_that_cannot_start(monkeypatch: pytest.MonkeyPatch) -> None:
    def broken(*args: Any, **kwargs: Any) -> Any:
        raise OSError("uv is not installed")

    monkeypatch.setattr(subprocess, "run", broken)
    assert GUARD.run_coney_tools(["task", "slots"]) == (2, "uv is not installed")
    assert "exit 2" in (GUARD.judge(stop()).note or "")


def test_the_real_runner_keeps_a_refusal_whole_and_a_crash_with_its_error(monkeypatch: pytest.MonkeyPatch) -> None:
    def answer(code: int, out: str, err: str) -> Any:
        def run(command: list[str], **options: Any) -> subprocess.CompletedProcess[bytes]:
            run.seen = (command, options)  # type: ignore[attr-defined]
            return subprocess.CompletedProcess(command, code, out.encode(), err.encode())

        return run

    monkeypatch.setattr(subprocess, "run", answer(1, BLOCKED, "uv noise"))
    assert GUARD.run_coney_tools(["task", "slots", "--fill-check"]) == (1, BLOCKED)
    run = answer(1, "", "Traceback: boom")
    monkeypatch.setattr(subprocess, "run", run)
    assert GUARD.run_coney_tools(["x"]) == (1, "Traceback: boom")
    command, options = run.seen
    assert command[:3] == ["uv", "run", "--quiet"] and command[-1] == "x" and options["timeout"] == GUARD.TIMEOUT
    assert "VIRTUAL_ENV" not in options["env"]


# --- the script -----------------------------------------------------------------------------------------------------


def run_main(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], raw: str, fake: Fake
) -> tuple[int, str]:
    monkeypatch.setattr(GUARD, "run_coney_tools", fake)
    monkeypatch.setattr("sys.stdin", __import__("io").StringIO(raw))
    code = GUARD.main()
    return code, capsys.readouterr().out


def test_main_answers_a_block_as_claude_code_json(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    code, out = run_main(monkeypatch, capsys, json.dumps(stop()), Fake(1, BLOCKED))
    assert code == 0 and json.loads(out) == {"decision": "block", "reason": BLOCKED}


def test_main_answers_a_note_as_a_system_message(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    code, out = run_main(monkeypatch, capsys, json.dumps(stop()), Fake(2, "no uv"))
    assert code == 0 and "systemMessage" in json.loads(out)


@pytest.mark.parametrize("raw", ["", "{", "[]", "null"])
def test_main_passes_input_it_cannot_read(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], raw: str
) -> None:
    code, out = run_main(monkeypatch, capsys, raw, Fake(1, BLOCKED))
    assert code == 0 and out == ""


def test_main_passes_when_the_hook_itself_breaks(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], log_folder: Path
) -> None:
    def explode(payload: object) -> None:
        raise RuntimeError("boom")

    monkeypatch.setattr(GUARD, "judge", explode)
    monkeypatch.setattr("sys.stdin", __import__("io").StringIO(json.dumps(stop())))
    assert GUARD.main() == 0 and capsys.readouterr().out == ""
    assert "RuntimeError: boom" in log_text(log_folder)


# --- the dispatcher and the settings --------------------------------------------------------------------------------


@pytest.fixture
def fake_tool(monkeypatch: pytest.MonkeyPatch) -> Fake:
    """The dispatcher's copy of the hook, with the tool replaced."""
    fake = Fake(1, BLOCKED)
    monkeypatch.setattr(DISPATCH.guard_module("slot_fill_guard"), "run_coney_tools", fake)
    return fake


def test_the_dispatcher_blocks_the_stop_event_with_claude_codes_json(fake_tool: Fake) -> None:
    code, out, err = DISPATCH.answer(json.dumps(stop()))
    assert (code, err) == (0, "") and json.loads(out) == {"decision": "block", "reason": BLOCKED + "\n"}
    assert fake_tool.calls == [["task", "slots", "--fill-check", "--session", "sess-1"]]


def test_the_dispatcher_lets_the_stop_event_end_when_the_check_passes(fake_tool: Fake) -> None:
    fake_tool.code, fake_tool.out = 0, "ok: every slot is full"
    assert DISPATCH.answer(json.dumps(stop())) == (0, "", "")


def test_the_dispatcher_shows_the_note_of_a_check_that_could_not_run(fake_tool: Fake) -> None:
    fake_tool.code, fake_tool.out = 2, "uv: not found"
    code, out, _ = DISPATCH.answer(json.dumps(stop()))
    assert code == 0 and "could not run (exit 2)" in json.loads(out)["systemMessage"]


def test_the_dispatcher_passes_a_stop_event_of_another_role(fake_tool: Fake) -> None:
    assert DISPATCH.answer(json.dumps(stop("engineer"))) == (0, "", "") and fake_tool.calls == []


def test_a_guard_that_crashes_passes_the_stop_event_and_leaves_a_line(
    monkeypatch: pytest.MonkeyPatch, log_folder: Path
) -> None:
    def crash(payload: object) -> None:
        raise RuntimeError("boom")

    monkeypatch.setattr(DISPATCH.guard_module("slot_fill_guard"), "judge", crash)
    assert DISPATCH.answer(json.dumps(stop())) == (0, "", "")
    assert "slot_fill_guard: RuntimeError: boom" in log_text(log_folder)


def test_a_guard_that_does_not_load_passes_the_stop_event(monkeypatch: pytest.MonkeyPatch, log_folder: Path) -> None:
    def missing(name: str) -> ModuleType:
        raise ImportError(name)

    monkeypatch.setattr(DISPATCH, "guard_module", missing)
    assert DISPATCH.answer(json.dumps(stop())) == (0, "", "")
    assert "slot_fill_guard: ImportError" in log_text(log_folder)


def test_the_settings_register_the_dispatcher_for_the_stop_event() -> None:
    (entry,) = settings()["hooks"]["Stop"]
    (hook,) = entry["hooks"]
    assert "matcher" not in entry
    assert hook["command"] == f"{LAUNCH} || exit 0"
    assert hook["timeout"] > GUARD.TIMEOUT


LAUNCH = 'uv run --no-project --python ">=3.12" python "$CLAUDE_PROJECT_DIR/.claude/hooks/dispatch.py"'


def test_the_hook_waits_longer_than_the_check_can_take() -> None:
    # The check reads GitHub for at most `budget_seconds`, plus the call in flight, then Python starts: a hook that
    # timed out is a failed hook, not a pass.
    fill = CONVENTIONS.tasks.start.fill
    assert fill.budget_seconds + fill.github_seconds <= GUARD.TIMEOUT - 20


def test_the_hook_and_the_tool_agree_on_the_exit_code_and_the_word() -> None:
    from coney_tools import slot_fill

    assert GUARD.BLOCKED == slot_fill.BLOCKED and GUARD.FAILED == 1


def test_the_stop_guard_is_the_one_the_dispatcher_runs() -> None:
    assert DISPATCH.STOP_GUARDS == ("slot_fill_guard",)


# --- who may waive --------------------------------------------------------------------------------------------------


def bash(command: str, agent_type: str) -> dict[str, Any]:
    return {
        "hook_event_name": "PreToolUse",
        "tool_name": "Bash",
        "tool_input": {"command": command},
        "agent_type": agent_type,
    }


WAIVE = 'uv run --project python coney-tools task slots --waive 2 --reason maintainer-pause --hours 4 --note "away"'


@pytest.mark.parametrize("agent_type", ["engineer", "tooling", "reviewer", "contributor"])
@pytest.mark.parametrize(
    "command",
    [
        WAIVE,
        "coney-tools task slots --reason machine-load --waive 1",
        "coney-tools.exe task slots --hours 2 --wai 1",
        "cd x && coney-tools task slots --waive=1",
        "CONEY-TOOLS.EXE task slots --waive 1",
        "python -m coney_tools.cli task slots --waive 1 --reason machine-load",
        "uv run --project python python python/src/coney_tools/cli.py task slots --wai 1",
        "uv run python -m coney_tools.cli task slots --hours 1 --waive 1",
    ],
)
def test_no_other_role_may_waive_a_slot(agent_type: str, command: str) -> None:
    assert roles.role_refusal(CONFIG, bash(command, agent_type)) is not None


@pytest.mark.parametrize("agent_type", ["engineer", "tooling", "reviewer", "contributor"])
@pytest.mark.parametrize(
    "command",
    [
        "uv run --project python coney-tools task slots",
        "coney-tools task slots --json",
        "coney-tools task slots --fill-check",
        "python -m coney_tools.cli task slots --fill-check",
        "coney-tools task start 1049-slot-fill-guard --dry-run",
    ],
)
def test_every_role_may_read_the_slots(agent_type: str, command: str) -> None:
    assert roles.role_refusal(CONFIG, bash(command, agent_type)) is None


def test_the_coordinator_may_waive() -> None:
    assert roles.role_refusal(CONFIG, bash(WAIVE, "coordinator")) is None
