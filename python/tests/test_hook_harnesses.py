# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the hooks of agent tools other than Claude Code: `.claude/hooks/harness.py` and `dispatch.py --harness`.

Each harness gets a payload shaped as its vendor documents it (docs/guides/enforcement.md#harnesses, read 2026-10-27),
and the tests check that the same guards refuse in that harness's own deny format: a raw `cmake --build`, a hand-typed
`gh pr create` and a `git push` from a session that is not the coordinator; that ordinary commands pass with the
harness's "no objection"; that a Codex patch is judged file by file; that the role comes from `CONEY_AGENT_ROLE` or
defaults to `contributor`; that the committed config of every harness runs the dispatcher; and that Claude Code's own
payloads still take Claude Code's path.
"""

import json
import os
import shutil
import subprocess
from collections.abc import Callable
from pathlib import Path
from typing import Any

import pytest
from conventions_support import REPO
from hooks_support import DISPATCH, load_hook_module

harness = load_hook_module("harness")
hooklib = load_hook_module("hooklib")
ROOT = str(REPO)

BUILD = "cmake --build build/dev"
OPEN_PR = "gh pr create --fill"
PUSH = "git push origin HEAD"
#: A GitHub write for another repository: a contributor may not make it, the coordinator's role may, and the PR guard
#: (`pull_requests.open_only`) leaves it alone, since it holds this repository's pull requests only.
ELSEWHERE = "gh api -X DELETE repos/other/thing/git/refs/heads/x"
ORDINARY = ("git status", "git log --oneline -3", "ls docs")


@pytest.fixture(autouse=True)
def no_role_from_the_environment(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> Path:
    """No `CONEY_AGENT_ROLE` from the machine running the tests; identity files only in a temporary folder."""
    monkeypatch.delenv(harness.ROLE_VARIABLE, raising=False)
    folder = tmp_path / "identity"
    monkeypatch.setattr(load_hook_module("identity"), "default_folder", lambda: folder)
    return folder


# --- the payloads, as each vendor documents them ---------------------------------------------------------------------


def codex(command: str) -> dict[str, Any]:
    """Codex's PreToolUse for its shell tool (learn.chatgpt.com/docs/hooks)."""
    return {
        "session_id": "s1", "transcript_path": None, "cwd": ROOT, "hook_event_name": "PreToolUse",
        "model": "gpt-5.5-codex", "turn_id": "t1", "permission_mode": "default",
        "tool_name": "Bash", "tool_use_id": "call_1", "tool_input": {"command": command},
    }  # fmt: skip


def gemini(command: str) -> dict[str, Any]:
    """Gemini CLI's BeforeTool for `run_shell_command` (geminicli.com/docs/hooks/reference)."""
    return {
        "session_id": "s1", "transcript_path": "/tmp/t.json", "cwd": ROOT, "hook_event_name": "BeforeTool",
        "timestamp": "2026-10-27T10:00:00Z", "tool_name": "run_shell_command",
        "tool_input": {"command": command, "description": "run it", "dir_path": ROOT},
    }  # fmt: skip


def antigravity(command: str) -> dict[str, Any]:
    """Antigravity's PreToolUse for `run_command` (antigravity.google/docs/hooks): no event name, no cwd."""
    return {
        "toolCall": {"name": "run_command", "args": {"CommandLine": command, "Cwd": ROOT, "WaitMsBeforeAsync": 0}},
        "stepIdx": 3, "conversationId": "c1", "workspacePaths": [ROOT], "transcriptPath": "/tmp/t",
        "artifactDirectoryPath": "/tmp/a", "modelName": "gemini-4",
    }  # fmt: skip


def cursor(command: str) -> dict[str, Any]:
    """Cursor's preToolUse for its Shell tool (cursor.com/docs/agent/hooks); it passes with `permission: allow`."""
    return {
        "conversation_id": "c1", "generation_id": "g1", "model": "auto", "hook_event_name": "preToolUse",
        "cursor_version": "3.3.0", "workspace_roots": [ROOT], "user_email": None, "transcript_path": None,
        "tool_name": "Shell", "tool_input": {"command": command, "working_directory": ROOT},
        "tool_use_id": "u1", "cwd": ROOT,
    }  # fmt: skip


def copilot(command: str) -> dict[str, Any]:
    """Copilot's camelCase preToolUse (docs.github.com/en/copilot/reference/hooks-reference); arguments as JSON."""
    return {
        "sessionId": "s1", "timestamp": 1793095200000, "cwd": ROOT, "toolName": "bash",
        "toolArgs": json.dumps({"command": command, "description": "run it"}),
    }  # fmt: skip


def copilot_claude_form(command: str) -> dict[str, Any]:
    """Copilot's Claude-compatible form (a PascalCase event, as when it runs `.claude/settings.json`)."""
    return {
        "hook_event_name": "PreToolUse", "session_id": "s1", "timestamp": "2026-10-27T10:00:00Z", "cwd": ROOT,
        "tool_name": "Bash", "tool_input": {"command": command},
    }  # fmt: skip


def grok(command: str) -> dict[str, Any]:
    """Grok Build's PreToolUse (docs.x.ai/build/features/hooks): camelCase."""
    return {
        "hookEventName": "PreToolUse", "sessionId": "s1", "cwd": ROOT, "workspaceRoot": ROOT,
        "toolName": "Bash", "toolInput": {"command": command},
    }  # fmt: skip


#: (name, payload maker, whether its answer is a refusal, the answer it gives for "no objection").
SHAPES: dict[str, tuple[Callable[[str], dict[str, Any]], Callable[[dict[str, Any]], bool], str]] = {
    "codex": (codex, lambda out: out["hookSpecificOutput"]["permissionDecision"] == "deny", ""),
    "gemini": (gemini, lambda out: out["decision"] == "deny", "{}"),
    "antigravity": (antigravity, lambda out: out["decision"] == "deny", '{"decision": "allow"}'),
    "cursor": (cursor, lambda out: out["permission"] == "deny", '{"permission": "allow"}'),
    "copilot": (copilot, lambda out: out["permissionDecision"] == "deny", ""),
    "grok": (grok, lambda out: out["decision"] == "deny", ""),
}


def run(name: str, payload: dict[str, Any], named: str | None = None) -> tuple[int, str, str]:
    """The dispatcher's answer to a payload given with `--harness <named>` (the harness's own name by default)."""
    answer: tuple[int, str, str] = DISPATCH.answer_any(json.dumps(payload), name if named is None else named)
    return answer


def refused(name: str, payload: dict[str, Any]) -> str:
    """The reason of a refusal in the harness's format (exit code 0, its JSON on stdout); fails when it passes."""
    code, out, _ = run(name, payload)
    assert code == 0 and out, f"{name} let the call through"
    _, is_deny, _ = SHAPES[name]
    assert is_deny(json.loads(out)), out
    return out


# --- the three refusals and the ordinary commands, per harness ------------------------------------------------------


@pytest.mark.parametrize("name", list(SHAPES))
def test_a_raw_build_is_refused_in_the_harness_format(name: str) -> None:
    make, _, _ = SHAPES[name]
    assert "coney-tools build" in refused(name, make(BUILD))


@pytest.mark.parametrize("name", list(SHAPES))
def test_a_hand_typed_pull_request_is_refused_in_the_harness_format(name: str) -> None:
    make, _, _ = SHAPES[name]
    refused(name, make(OPEN_PR))


@pytest.mark.parametrize("name", list(SHAPES))
def test_a_push_from_a_contributor_is_refused_in_the_harness_format(name: str) -> None:
    make, _, _ = SHAPES[name]
    assert "git push" in refused(name, make(PUSH))


@pytest.mark.parametrize("name", list(SHAPES))
@pytest.mark.parametrize("command", ORDINARY)
def test_ordinary_commands_pass_with_the_harness_no_objection(name: str, command: str) -> None:
    make, _, allowed = SHAPES[name]
    assert run(name, make(command)) == (0, allowed, "")


@pytest.mark.parametrize("name", list(SHAPES))
def test_the_coordinator_role_from_the_environment_may_write_to_github(
    name: str, monkeypatch: pytest.MonkeyPatch
) -> None:
    # The role comes from the environment the person started the tool with; no command of the agent reaches it.
    monkeypatch.setenv(harness.ROLE_VARIABLE, "coordinator")
    make, _, allowed = SHAPES[name]
    assert run(name, make(ELSEWHERE)) == (0, allowed, "")
    refused(name, make(BUILD))  # the guards still run for every role
    refused(
        name, make(PUSH)
    )  # and so does the PR guard: only `pr open` pushes this repository, even for the coordinator


@pytest.mark.parametrize("name", list(SHAPES))
def test_a_github_write_from_a_contributor_is_refused_in_the_harness_format(name: str) -> None:
    make, _, _ = SHAPES[name]
    refused(name, make(ELSEWHERE))


def test_an_unknown_role_from_the_environment_may_only_read(monkeypatch: pytest.MonkeyPatch) -> None:
    # `require_role`: a name that is no role is no way around the rules.
    monkeypatch.setenv(harness.ROLE_VARIABLE, "boss")
    assert "no Coney role" in refused("codex", codex("git status"))


def test_the_copilot_claude_form_is_refused_in_the_copilot_format() -> None:
    code, out, _ = DISPATCH.answer_any(json.dumps(copilot_claude_form(PUSH)), "copilot")
    assert code == 0 and json.loads(out)["permissionDecision"] == "deny"


def test_other_harnesses_get_no_identity_token(no_role_from_the_environment: Path) -> None:
    # The token is Claude Code's: Codex would accept an updatedInput, but none is sent and no file is written.
    assert run("codex", codex("uv run --project python coney-tools config show")) == (0, "", "")
    assert not no_role_from_the_environment.exists() or not list(no_role_from_the_environment.iterdir())


# --- file edits ------------------------------------------------------------------------------------------------------

PATCH = """*** Begin Patch
*** Update File: docs/guides/agents.md
@@
-old line
+new line
*** Add File: src/core/new_file.cpp
+// SPDX-License-Identifier: GPL-3.0-or-later
*** Delete File: python/tests/old_test.py
*** End Patch
"""


def codex_patch(patch: str) -> dict[str, Any]:
    """Codex's PreToolUse for `apply_patch`: the patch is `tool_input.command`."""
    return {**codex(""), "tool_name": "apply_patch", "tool_input": {"command": patch}}


def test_a_codex_patch_is_one_write_per_file() -> None:
    calls = harness.claude_calls("codex", codex_patch(PATCH), "contributor")
    assert [
        (call["tool_name"], Path(call["tool_input"]["file_path"]).relative_to(REPO).as_posix()) for call in calls
    ] == [
        ("Write", "docs/guides/agents.md"),
        ("Write", "src/core/new_file.cpp"),
        ("Write", "python/tests/old_test.py"),
    ]
    assert calls[0]["tool_input"]["content"] == "new line\n"
    assert calls[2]["tool_input"]["content"] == ""


def test_a_codex_patch_is_refused_when_one_file_is(monkeypatch: pytest.MonkeyPatch) -> None:
    # The coordinator writes no engine code: one file under src/ refuses the whole patch.
    monkeypatch.setenv(harness.ROLE_VARIABLE, "coordinator")
    assert "src/" in refused("codex", codex_patch(PATCH))
    monkeypatch.setenv(harness.ROLE_VARIABLE, "contributor")
    assert run("codex", codex_patch(PATCH)) == (0, "", "")


@pytest.mark.parametrize(
    ("name", "payload"),
    [
        ("gemini", {**gemini(""), "tool_name": "write_file", "tool_input": {"file_path": "src/a.cpp", "content": "x"}}),
        ("antigravity", {**antigravity(""), "toolCall": {
            "name": "write_to_file", "args": {"TargetFile": f"{ROOT}/src/a.cpp", "CodeContent": "x"}}}),
        ("cursor", {**cursor(""), "tool_name": "Write",
                    "tool_input": {"file_path": f"{ROOT}/src/a.cpp", "content": "x"}}),
        ("copilot", {**copilot(""), "toolName": "create", "toolArgs": {"path": f"{ROOT}/src/a.cpp", "file_text": "x"}}),
        ("grok", {**grok(""), "toolName": "Edit", "toolInput": {"file_path": f"{ROOT}/src/a.cpp", "new_string": "x"}}),
    ],
)  # fmt: skip
def test_a_file_write_is_judged_by_the_role_rules(
    name: str, payload: dict[str, Any], monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setenv(harness.ROLE_VARIABLE, "coordinator")
    assert "src/" in refused(name, payload)


# --- which harness, and Claude Code's path --------------------------------------------------------------------------


@pytest.mark.parametrize("name", list(SHAPES))
def test_each_payload_is_recognised_without_a_name(name: str) -> None:
    make, _, _ = SHAPES[name]
    assert harness.detect(make("ls")) == name


def test_claude_code_payloads_are_claude_code_s() -> None:
    payload = {
        "session_id": "s1", "transcript_path": "/t.jsonl", "cwd": ROOT, "permission_mode": "default",
        "hook_event_name": "PreToolUse", "tool_name": "Bash", "tool_input": {"command": "ls"}, "tool_use_id": "t",
    }  # fmt: skip
    assert harness.detect(payload) == "claude"
    assert harness.detect({"hook_event_name": "PreToolUse", "tool_name": "Bash", "tool_input": {}}) == "claude"


def test_claude_code_without_a_role_stays_read_only() -> None:
    # Claude Code's behaviour is unchanged: with no agent type, Bash is refused in Claude Code's own format.
    raw = json.dumps({"hook_event_name": "PreToolUse", "tool_name": "Bash", "tool_input": {"command": "ls"}})
    code, out, _ = DISPATCH.answer_any(raw, None)
    assert code == 0 and json.loads(out)["hookSpecificOutput"]["permissionDecision"] == "deny"
    assert DISPATCH.answer_any(raw, None) == DISPATCH.answer(raw)


def test_the_payload_wins_over_the_name_given() -> None:
    # Grok Build also runs `.cursor/hooks.json` (whose command says cursor) and `.claude/settings.json` (none).
    for named in ("cursor", None):
        code, out, _ = DISPATCH.answer_any(json.dumps(grok(PUSH)), named)
        assert code == 0 and json.loads(out)["decision"] == "deny"


def test_events_after_the_tool_pass() -> None:
    after = {**codex(BUILD), "hook_event_name": "PostToolUse"}
    assert run("codex", after) == (0, "", "")
    assert run("copilot", {**copilot(BUILD), "toolResult": {"resultType": "success"}}) == (0, "", "")


# --- the committed configs -------------------------------------------------------------------------------------------

#: The committed config of each harness and the commands in it.
CONFIGS = {
    "codex": ".codex/hooks.json",
    "gemini": ".gemini/settings.json",
    "antigravity": ".agents/hooks.json",
    "cursor": ".cursor/hooks.json",
    "copilot": ".github/hooks/coney.json",
    "grok": ".grok/hooks/coney.json",
}


def _commands(value: Any) -> list[tuple[str, dict[str, Any]]]:
    """Every (command, handler) of a hook file, whatever its nesting."""
    found: list[tuple[str, dict[str, Any]]] = []
    if isinstance(value, dict):
        for key in ("command", "bash", "powershell"):
            if isinstance(value.get(key), str):
                found.append((value[key], value))
        for item in value.values():
            found += _commands(item)
    elif isinstance(value, list):
        for item in value:
            found += _commands(item)
    return found


@pytest.mark.parametrize("name", list(CONFIGS))
def test_each_harness_config_runs_the_dispatcher(name: str) -> None:
    data = json.loads((REPO / CONFIGS[name]).read_text(encoding="utf-8"))
    commands = _commands(data)
    assert commands, CONFIGS[name]
    pr_guard = hooklib.load_guard("pr_guard")
    for command, handler in commands:
        # The interpreter is pinned, the harness named, and the command always exits 0: a launcher that cannot start
        # must not block every call (Copilot denies on any other exit code), and refusals are JSON.
        assert command.startswith('uv run --no-project --python ">=3.12" python '), command
        assert ".claude/hooks/dispatch.py" in command and f"--harness {name}" in command
        assert command.endswith("; exit 0"), command
        timeout = handler.get("timeoutSec", handler.get("timeout"))
        assert isinstance(timeout, int), (name, timeout)
        seconds = timeout / 1000 if name == "gemini" else timeout
        assert seconds >= 2 * pr_guard.CHECK_TIMEOUT, (name, timeout)


def test_every_harness_with_an_adapter_has_a_config() -> None:
    assert set(CONFIGS) == set(harness.HARNESSES) - {"claude"}


def test_the_codex_command_finds_the_dispatcher_from_any_folder() -> None:
    # The command runs in the session's folder; `git rev-parse --show-toplevel` finds the checkout, with no
    # $CLAUDE_PROJECT_DIR.
    bash = shutil.which("bash")
    if bash is None or shutil.which("uv") is None or shutil.which("git") is None:
        pytest.skip("needs bash, uv and git")
    data = json.loads((REPO / CONFIGS["codex"]).read_text(encoding="utf-8"))
    ((command, _),) = _commands(data)
    environment = {
        key: value for key, value in os.environ.items() if key not in ("CLAUDE_PROJECT_DIR", harness.ROLE_VARIABLE)
    }
    done = subprocess.run(
        [bash, "-c", command],
        input=json.dumps(codex(BUILD)),
        capture_output=True,
        text=True,
        encoding="utf-8",
        cwd=REPO / "docs",
        env={**environment, "UV_PYTHON_DOWNLOADS": "never"},
        check=False,
        timeout=120,
    )
    assert done.returncode == 0, done.stderr
    assert json.loads(done.stdout)["hookSpecificOutput"]["permissionDecision"] == "deny", done.stdout


# --- tool names the adapter does not know, and the other tool forms ------------------------------------------------

#: A shell tool under a name in none of the adapter's lists, one per harness (VS Code's terminal tool among them).
UNLISTED_SHELLS: dict[str, Callable[[str], dict[str, Any]]] = {
    "codex": lambda command: {**codex(""), "tool_name": "shell_command", "tool_input": {"command": command}},
    "gemini": lambda command: {**gemini(""), "tool_name": "execute", "tool_input": {"command": command}},
    "antigravity": lambda command: {
        **antigravity(""), "toolCall": {"name": "bash_tool", "args": {"CommandLine": command}}},
    "cursor": lambda command: {**cursor(""), "tool_name": "execute", "tool_input": {"command": command}},
    "copilot": lambda command: {**copilot(""), "toolName": "run_in_terminal", "toolArgs": {"command": command}},
    "grok": lambda command: {**grok(""), "toolName": "run_terminal", "toolInput": {"command": command}},
}  # fmt: skip


@pytest.mark.parametrize("name", list(UNLISTED_SHELLS))
def test_an_unlisted_shell_tool_is_judged_by_its_arguments(name: str) -> None:
    make = UNLISTED_SHELLS[name]
    assert "coney-tools build" in refused(name, make(BUILD))
    refused(name, make(PUSH))
    _, _, allowed = SHAPES[name]
    assert run(name, make("git status")) == (0, allowed, "")


def test_an_unlisted_file_tool_is_judged_as_a_write(monkeypatch: pytest.MonkeyPatch) -> None:
    # VS Code's edit tool: a path and the code to write; a deletion: a path and a name that says it changes the file.
    monkeypatch.setenv(harness.ROLE_VARIABLE, "coordinator")
    edit = {
        **copilot(""),
        "toolName": "insert_edit_into_file",
        "toolArgs": {"filePath": f"{ROOT}/src/a.cpp", "code": "x"},
    }
    assert "src/" in refused("copilot", edit)
    remove = {**grok(""), "toolName": "remove_file", "toolInput": {"path": f"{ROOT}/src/a.cpp"}}
    assert "src/" in refused("grok", remove)


def test_an_unlisted_editor_with_a_command_word_is_judged_both_ways(monkeypatch: pytest.MonkeyPatch) -> None:
    # An editor tool whose `command` names the edit (`str_replace`) is read as a shell call and as a write.
    monkeypatch.setenv(harness.ROLE_VARIABLE, "coordinator")
    payload = {**grok(""), "toolName": "text_editor", "toolInput": {
        "command": "str_replace", "path": f"{ROOT}/src/a.cpp", "old_str": "a", "new_str": "b"}}  # fmt: skip
    tools = [call["tool_name"] for call in harness.claude_calls("grok", payload, "coordinator")]
    assert tools == ["Bash", "Write"]
    assert "src/" in refused("grok", payload)


def test_an_unlisted_tool_with_no_command_or_write_keeps_its_name() -> None:
    payload = {**gemini(""), "tool_name": "mcp_pcsx2_pcsx2_status", "tool_input": {}}
    (call,) = harness.claude_calls("gemini", payload, "contributor")
    assert call["tool_name"] == "mcp_pcsx2_pcsx2_status"
    search = {**copilot(""), "toolName": "grep", "toolArgs": {"pattern": "x", "path": ROOT}}
    assert harness.claude_calls("copilot", search, "contributor")[0]["tool_name"] == "grep"
    assert run("copilot", search) == (0, "", "")


def test_a_cursor_delete_and_a_copilot_edit_are_writes(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv(harness.ROLE_VARIABLE, "coordinator")
    delete = {**cursor(""), "tool_name": "Delete", "tool_input": {"path": f"{ROOT}/src/a.cpp"}}
    assert "src/" in refused("cursor", delete)
    edit = {
        **copilot(""),
        "toolName": "edit",
        "toolArgs": {"path": f"{ROOT}/src/a.cpp", "old_str": "a", "new_str": "b"},
    }
    assert "src/" in refused("copilot", edit)


def test_cursor_before_shell_execution_is_judged() -> None:
    payload = {
        "conversation_id": "c1", "hook_event_name": "beforeShellExecution", "cursor_version": "3.3.0",
        "workspace_roots": [ROOT], "command": PUSH, "cwd": ROOT,
    }  # fmt: skip
    assert "git push" in refused("cursor", payload)


def test_a_copilot_powershell_call_is_judged_as_powershell() -> None:
    payload = {**copilot(""), "toolName": "powershell", "toolArgs": {"command": BUILD}}
    assert harness.claude_calls("copilot", payload, "contributor")[0]["tool_name"] == "PowerShell"
    assert "coney-tools build" in refused("copilot", payload)


@pytest.mark.parametrize("name", harness.UNION_HARNESSES)
def test_the_tools_that_borrow_hook_files_get_every_deny_key(name: str) -> None:
    # Cursor, Copilot and Grok Build also run other tools' hook files: no wrong guess of the format lets a call through.
    make, _, _ = SHAPES[name]
    out = json.loads(refused(name, make(PUSH)))
    assert out["permission"] == out["permissionDecision"] == out["decision"] == "deny"
    assert out["hookSpecificOutput"]["permissionDecision"] == "deny"
    assert out["reason"] == out["permissionDecisionReason"] == out["agent_message"] == out["user_message"]


def test_a_vs_code_form_with_a_camel_case_event_is_refused() -> None:
    # A payload with `hookEventName` and Claude Code's field names (as an older VS Code page showed it) reads as Grok
    # Build's; the answer carries Copilot's and Claude Code's keys as well.
    payload = {"hookEventName": "PreToolUse", "sessionId": "s1", "timestamp": "2026-10-27T10:00:00Z", "cwd": ROOT,
               "tool_name": "Bash", "tool_input": {"command": PUSH}}  # fmt: skip
    code, out, _ = DISPATCH.answer_any(json.dumps(payload), None)
    answer = json.loads(out)
    assert code == 0 and answer["permissionDecision"] == "deny"
    assert answer["hookSpecificOutput"]["permissionDecision"] == "deny"


# --- the contributor's messages and its hook files -------------------------------------------------------------------


def test_a_contributors_push_names_its_person_not_a_coordinator() -> None:
    out = refused("codex", codex(PUSH))
    reason = json.loads(out)["hookSpecificOutput"]["permissionDecisionReason"]
    assert "Your person pushes" in reason and "coordinator" not in reason


def test_an_engineers_push_still_names_the_coordinator(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv(harness.ROLE_VARIABLE, "engineer")
    assert "coordinator" in refused("codex", codex(PUSH))


@pytest.mark.parametrize(
    "path", [".claude/hooks/hooks.toml", ".claude/settings.json", ".codex/hooks.json", ".github/hooks/coney.json"]
)
def test_a_contributor_may_not_edit_the_hook_files(path: str) -> None:
    payload = {**gemini(""), "tool_name": "write_file", "tool_input": {"file_path": f"{ROOT}/{path}", "content": "{}"}}
    assert "Ask your person" in refused("gemini", payload)


def test_an_unknown_role_in_another_tool_is_told_about_the_variable(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv(harness.ROLE_VARIABLE, "boss")
    out = refused("cursor", cursor("git status"))
    assert "CONEY_AGENT_ROLE" in out and "coney-agent" not in out


# --- the dispatcher around the adapters ------------------------------------------------------------------------------


def test_the_guards_see_this_checkout_as_the_project(monkeypatch: pytest.MonkeyPatch) -> None:
    # pr_guard.py and pcsx2_guard.py read $CLAUDE_PROJECT_DIR first; another tool may have one of another checkout.
    monkeypatch.setenv("CLAUDE_PROJECT_DIR", "/somewhere/else")
    seen: list[str | None] = []

    def judge(call: dict[str, Any], raw: str, settings: dict[str, Any]) -> tuple[int, str, str]:
        seen.append(os.environ.get("CLAUDE_PROJECT_DIR"))
        return 0, "", ""

    monkeypatch.setattr(DISPATCH, "pre_tool_use", judge)
    assert run("codex", codex("ls")) == (0, "", "")
    assert seen == [str(hooklib.CHECKOUT)]
    assert os.environ["CLAUDE_PROJECT_DIR"] == "/somewhere/else"


def test_an_adapter_that_does_not_import_lets_other_tools_through(monkeypatch: pytest.MonkeyPatch) -> None:
    # As for a broken guard in Claude Code: the call passes and the log says why (docs/guides/enforcement.md, Limits).
    lines: list[str] = []
    monkeypatch.setattr(DISPATCH, "HARNESS_ERROR", "ImportError: broken")
    monkeypatch.setattr(DISPATCH, "record", lines.append)
    assert run("codex", codex(PUSH)) == (0, "", "")
    assert lines == ["harness: ImportError: broken"]
    # Claude Code's own path does not need the adapters.
    raw = json.dumps({"hook_event_name": "PreToolUse", "tool_name": "Bash", "tool_input": {"command": "ls"}})
    assert DISPATCH.answer_any(raw, None) == DISPATCH.answer(raw)


def test_an_unknown_harness_name_is_logged_and_read_by_the_payload(monkeypatch: pytest.MonkeyPatch) -> None:
    lines: list[str] = []
    monkeypatch.setattr(DISPATCH, "record", lines.append)
    code, out, _ = DISPATCH.answer_any(json.dumps(grok(PUSH)), "windsurf")
    assert code == 0 and json.loads(out)["decision"] == "deny"
    assert lines == ["harness: unknown --harness 'windsurf', read as grok"]


# --- argument shapes the lists do not name: judged, never let through as nothing ------------------------------------


@pytest.mark.parametrize(
    "payload",
    [
        {**codex(""), "tool_name": "shell", "tool_input": {"command": ["bash", "-lc", PUSH]}},
        {**codex(""), "tool_name": "Bash", "tool_input": {"command": ["git", "push", "origin", "HEAD"]}},
        {**codex(""), "tool_name": "run_it", "tool_input": {"commands": ["git status", PUSH]}},
    ],
)  # fmt: skip
def test_a_command_as_a_list_is_judged(payload: dict[str, Any]) -> None:
    assert "git push" in refused("codex", payload)


@pytest.mark.parametrize("key", ["script", "input", "cmd_line", "command_line", "Command", "code"])
def test_a_shell_tool_s_command_under_another_key_is_judged(key: str) -> None:
    # A listed shell tool with no known command argument has every string argument judged as a command.
    payload = {**grok(""), "toolName": "Bash", "toolInput": {key: PUSH}}
    assert "git push" in refused("grok", payload)


@pytest.mark.parametrize("key", ["script", "input", "cmd_line"])
def test_an_unlisted_tool_s_command_under_another_key_is_judged(key: str) -> None:
    payload = {**copilot(""), "toolName": "terminal_run", "toolArgs": {key: PUSH}}
    assert "git push" in refused("copilot", payload)


def test_a_patch_under_any_argument_is_judged(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv(harness.ROLE_VARIABLE, "coordinator")
    listed = {**codex_patch(""), "tool_input": {"input": PATCH}}
    assert "src/" in refused("codex", listed)
    unlisted = {**gemini(""), "tool_name": "patcher", "tool_input": {"body": PATCH}}
    assert "src/" in refused("gemini", unlisted)


@pytest.mark.parametrize(
    "arguments",
    [
        {"filepath": f"{ROOT}/.claude/hooks/hooks.toml", "content": "x"},
        {"file": f"{ROOT}/.claude/hooks/hooks.toml", "content": "x"},
        {"uri": "file:///" + f"{ROOT}/.claude/hooks/hooks.toml".replace("\\", "/").lstrip("/"), "content": "x"},
        {"where": f"{ROOT}/.codex/hooks.json", "content": "x"},
    ],
)  # fmt: skip
def test_a_write_tool_s_path_under_another_key_is_judged(arguments: dict[str, Any]) -> None:
    payload = {**gemini(""), "tool_name": "write_file", "tool_input": arguments}
    assert "Ask your person" in refused("gemini", payload)


@pytest.mark.parametrize("key", ["patch", "diff", "edits", "data", "body", "new_text", "newText", "replacement"])
def test_an_unlisted_tool_with_a_path_and_an_edit_is_a_write(key: str) -> None:
    payload = {**grok(""), "toolName": "apply", "toolInput": {"path": f"{ROOT}/.grok/hooks/coney.json", key: "x"}}
    assert "Ask your person" in refused("grok", payload)


@pytest.mark.parametrize("name", ["update_file", "modify", "overwrite", "change_file"])
def test_more_names_say_a_tool_changes_a_file(name: str) -> None:
    payload = {**grok(""), "toolName": name, "toolInput": {"target": f"{ROOT}/.cursor/hooks.json"}}
    assert "Ask your person" in refused("grok", payload)


def test_a_move_is_judged_at_both_ends() -> None:
    payload = {**copilot(""), "toolName": "move_file",
               "toolArgs": {"source": f"{ROOT}/docs/x.md", "destination": f"{ROOT}/.claude/hooks/x.py"}}  # fmt: skip
    assert "Ask your person" in refused("copilot", payload)


def test_an_edit_s_old_text_is_no_path() -> None:
    edit = {"file_path": f"{ROOT}/docs/a.md", "old_string": ".claude/hooks/x", "new_string": "y"}
    payload = {**grok(""), "toolName": "Edit", "toolInput": edit}
    calls = harness.claude_calls("grok", payload, "contributor")
    assert [call["tool_input"]["file_path"] for call in calls] == [f"{ROOT}/docs/a.md"]


def test_cursor_s_pascal_case_event_is_judged() -> None:
    # Cursor also runs `.claude/settings.json`'s hook, whose command names no harness.
    payload = {**cursor(PUSH), "hook_event_name": "PreToolUse"}
    for named in ("cursor", None):
        code, out, _ = DISPATCH.answer_any(json.dumps(payload), named)
        assert code == 0 and json.loads(out)["permission"] == "deny"


# --- nested and non-object arguments: walked at any depth ----------------------------------------------------------


@pytest.mark.parametrize(
    "tool_input",
    [
        {"params": {"command": PUSH}},
        {"command": {"program": "git", "args": ["push", "origin", "HEAD"]}},
        {"command": ["git", "push", 1]},
        {"command": [["git", "push"], ["git", "status"]]},
        {"command": "git", "args": ["push", "origin", "HEAD"]},
    ],
)  # fmt: skip
def test_a_shell_tool_s_nested_or_split_command_is_judged(tool_input: dict[str, Any]) -> None:
    payload = {**codex(""), "tool_name": "shell", "tool_input": tool_input}
    assert "git push" in refused("codex", payload)


@pytest.mark.parametrize(
    ("name", "payload"),
    [
        ("codex", {**codex(""), "tool_input": [PUSH]}),
        ("antigravity", {**antigravity(""), "toolCall": {"name": "run_command", "args": [PUSH]}}),
        ("copilot", {**copilot(""), "toolArgs": json.dumps(PUSH)}),
        ("copilot", {**copilot(""), "toolArgs": json.dumps(["git", "push"])}),
        ("copilot", {**copilot(""), "toolName": "terminal_run", "toolArgs": json.dumps([PUSH])}),
    ],
)  # fmt: skip
def test_arguments_that_are_no_object_are_judged(name: str, payload: dict[str, Any]) -> None:
    assert "git push" in refused(name, payload)


@pytest.mark.parametrize("key", ["files", "entries"])
def test_a_write_tool_s_list_of_files_is_judged(key: str) -> None:
    files = [
        {"path": f"{ROOT}/docs/a.md", "content": "x"},
        {"file_path": f"{ROOT}/.claude/hooks/hooks.toml", "content": "x"},
    ]
    payload = {**gemini(""), "tool_name": "write_file", "tool_input": {key: files}}
    assert "Ask your person" in refused("gemini", payload)


def test_an_unlisted_tool_s_nested_command_is_judged() -> None:
    payload = {**grok(""), "toolName": "runner", "toolInput": {"args": {"command": PUSH}}}
    assert "git push" in refused("grok", payload)


def test_a_contributor_may_not_edit_the_local_settings_other_tools_read() -> None:
    # Cursor and Copilot read hooks from `.claude/settings.local.json`; Copilot's own settings hold `disableAllHooks`.
    for path in (".claude/settings.local.json", ".github/copilot/settings.json"):
        payload = {
            **gemini(""),
            "tool_name": "write_file",
            "tool_input": {"file_path": f"{ROOT}/{path}", "content": "{}"},
        }
        assert "Ask your person" in refused("gemini", payload)
