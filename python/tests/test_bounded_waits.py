# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for .claude/hooks/bounded_waits.py, the Claude Code hook that refuses a shell command with a wait loop that
has no bound.

A background shell that loops `until grep -q done log; do sleep 20; done` never ends when the run it waits for dies,
and keeps its finished agent listed for hours. The hook blocks such a loop unless something bounds it: a counter or a
deadline compared in the loop, or `timeout N` around it. The script is standalone and standard-library only (the hook
runs it on every shell command), so it is loaded by path.
"""

import importlib.util
import io
import json
from types import ModuleType

import pytest
from conventions_support import REPO
from hooks_support import guards_run

SCRIPT = REPO / ".claude" / "hooks" / "bounded_waits.py"


def load() -> ModuleType:
    """The hook script as a module."""
    spec = importlib.util.spec_from_file_location("bounded_waits", SCRIPT)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


HOOK = load()

UNBOUNDED = [
    "until grep -q done /c/x/build.log; do sleep 20; done",
    "until [ -f /tmp/ready ]; do sleep 5; done; echo ready",
    "while true; do sleep 30; tail -1 log.txt; done",
    "while :; do sleep 1; done",
    "while ! grep -q PASSED out.txt; do sleep 10; done",
    "while kill -0 1234 2>/dev/null; do sleep 5; done",
    "cd /c/x && until test -s result.json\ndo\n  sleep 15\ndone",
    "until grep -q X log; do\n  echo waiting\n  sleep 1m\ndone",
    "bash -c 'until grep -q X log; do sleep 20; done'",
    "eval 'while true; do sleep 20; done'",
    # one of the twenty found running for hours (2026-10-12)
    'f=/c/x/build1.log; last=""; while true; do l=$(grep -E "building|FAILED" "$f" | tail -1); '
    'if [ "$l" != "$last" ]; then echo "$l"; last="$l"; fi; sleep 20; done',
    "while ($true) { Start-Sleep -Seconds 20 }",
    "while (-not (Test-Path C:\\x\\done.txt)) { Start-Sleep 10 }",
    "do { Start-Sleep 5 } until (Test-Path done.txt)",
    "do { Start-Sleep -s 5; $x = Get-Content log } while ($x -notmatch 'done')",
    "while (!(Select-String -Quiet -Path log.txt -Pattern done)) { sleep 5 }",
]

BOUNDED = [
    # a counter or a deadline in the loop
    "for i in $(seq 1 30); do grep -q done log && break; sleep 20; done",
    "for i in {1..30}; do grep -q done log && break; sleep 20; done",
    "for ((i = 0; i < 30; i++)); do sleep 1; done",
    "n=0; until grep -q done log || [ $n -ge 30 ]; do sleep 20; n=$((n+1)); done",
    "while [ $SECONDS -lt 600 ] && ! grep -q done log; do sleep 10; done",
    "i=0; while ! grep -q done log; do i=$((i+1)); [ $i -gt 30 ] && break; sleep 10; done",
    "while (( i < 30 )); do sleep 1; ((i++)); done",
    "for ($i = 0; $i -lt 30; $i++) { if (Test-Path done) { break }; Start-Sleep 10 }",
    "while (-not (Test-Path done) -and $i -lt 30) { Start-Sleep 10; $i++ }",
    "$deadline = (Get-Date).AddMinutes(10); while ((Get-Date) -lt $deadline) { Start-Sleep 5 }",
    # timeout around the loop
    "timeout 600 bash -c 'until grep -q done log; do sleep 20; done'",
    "timeout -k 5 10m sh -c 'while true; do sleep 1; done'",
    # loops with no sleep, and text that only mentions a loop
    'while read -r line; do echo "$line"; done < file.txt',
    "until false; do break; done",
    "echo 'until grep -q X log; do sleep 20; done'",
    'git commit -m "Document why until ...; do sleep loops are refused"',
    "cat <<'EOF' > notes.md\nuntil grep -q X log; do sleep 20; done\nEOF\necho ok",
    "git commit -F - <<EOF\nwhile true; do sleep 1; done\nEOF",
    "$text = @'\nwhile ($true) { Start-Sleep 1 }\n'@\n$text",
    "grep -rn 'while true' docs/",
    "sleep 5",
    "uv run --project python coney-tools build --agent a --preset dev --test",
    "",
]


@pytest.mark.parametrize("command", UNBOUNDED)
def test_an_unbounded_wait_loop_is_blocked(command: str) -> None:
    assert HOOK.blocked(command), command


@pytest.mark.parametrize("command", BOUNDED)
def test_a_bounded_loop_or_text_passes(command: str) -> None:
    assert not HOOK.blocked(command), command


def run(monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], payload: object) -> tuple[int, str]:
    """Run the hook's main on a JSON payload; its exit code and stderr."""
    text = payload if isinstance(payload, str) else json.dumps(payload)
    monkeypatch.setattr("sys.stdin", io.StringIO(text))
    code = HOOK.main()
    return code, capsys.readouterr().err


def test_the_hook_exits_2_with_the_bounded_alternatives(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    code, err = run(
        monkeypatch, capsys, {"tool_name": "Bash", "tool_input": {"command": "until grep -q X l; do sleep 9; done"}}
    )
    assert code == 2
    for alternative in ("seq 1", "timeout", "--max-wait", "run_in_background"):
        assert alternative in err


def test_the_hook_lets_everything_else_through(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    assert run(monkeypatch, capsys, {"tool_input": {"command": "ls"}}) == (0, "")
    assert run(monkeypatch, capsys, "not json") == (0, "")
    assert run(monkeypatch, capsys, {"tool_input": {}}) == (0, "")
    assert run(monkeypatch, capsys, {"tool_input": {"command": 3}}) == (0, "")


def test_the_dispatcher_runs_the_hook_before_shell_commands() -> None:
    assert "bounded_waits" in guards_run("Bash", "sleep 1") and "bounded_waits" in guards_run("PowerShell", "ls")
    assert "bounded_waits" not in guards_run("Read")
