# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for .claude/hooks/dispatch.py, the one entry point of Coney's Claude Code hooks, and its settings.

The guards' own tests (test_build_guard.py and the others) cover what each refuses; these cover that the dispatcher
runs them unchanged, collects their refusals, applies the role rules and the identity token, answers SessionStart,
that `.claude/settings.json` points at the dispatcher alone, and that nothing but the guards' own failure turns a guard
off: a settings file that cannot be read, a rule module that does not import and a rule that crashes all leave the
guards running. Input is the synthetic hook payload of docs/guides/agents.md#hooks.
"""

import json
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

import pytest
from conventions_support import REPO
from hooks_support import DISPATCH, HOOKS, load_hook_module, settings

from coney_tools.identity import file_name

hooklib = load_hook_module("hooklib")
CONFIG = hooklib.load_config(HOOKS / "hooks.toml")


@pytest.fixture(autouse=True)
def identity_folder(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> Path:
    """Identity files go to a temporary folder, never the real `~/.coney/identity`."""
    folder = tmp_path / "identity"
    monkeypatch.setattr(load_hook_module("identity"), "default_folder", lambda: folder)
    return folder


@pytest.fixture(autouse=True)
def no_slot_check(monkeypatch: pytest.MonkeyPatch) -> None:
    """The coordinator's Agent calls below must not start `coney-tools`: the slot check lets every start through."""
    monkeypatch.setattr(DISPATCH.guard_module("slot_guard"), "run_coney_tools", lambda args, stdin: (0, "ok"))


def pre(tool: str, tool_input: dict[str, Any], **extra: Any) -> str:
    """A PreToolUse payload as the JSON text Claude Code sends, from a session started as the coordinator unless
    `agent_type` says otherwise; `agent_type=None` is a session with no role (a plain `claude`), which `require_role`
    keeps to reading."""
    extra.setdefault("agent_type", "coordinator")
    if extra["agent_type"] is None:
        del extra["agent_type"]
    return json.dumps({"hook_event_name": "PreToolUse", "tool_name": tool, "tool_input": tool_input, **extra})


def bash(command: str, **extra: Any) -> str:
    """A Bash PreToolUse payload."""
    return pre("Bash", {"command": command}, **extra)


#: What `.claude/settings.json` runs before `|| exit 0`.
LAUNCH = 'uv run --no-project --python ">=3.12" python "$CLAUDE_PROJECT_DIR/.claude/hooks/dispatch.py"'

# --- the settings ---------------------------------------------------------------------------------------------------


def test_the_settings_run_only_the_dispatcher_for_every_event() -> None:
    hooks = settings()["hooks"]
    assert set(hooks) == {"PreToolUse", "PostToolUse", "PostToolUseFailure", "PermissionDenied", "SessionStart"}
    commands = [hook["command"] for entries in hooks.values() for entry in entries for hook in entry["hooks"]]
    assert len(commands) == 5
    for command in commands:
        # The interpreter is pinned: an old default Python must not fail an import and so turn the guards off. The
        # command ends in `|| exit 0`: if uv or Python cannot start (exit code 2 would block every tool call), the
        # call goes through.
        assert command == f"{LAUNCH} || exit 0"
    assert [entry["matcher"] for entry in hooks["PreToolUse"]] == ["*"]
    for event in ("PostToolUse", "PostToolUseFailure", "PermissionDenied"):
        assert [entry["matcher"] for entry in hooks[event]] == ["Bash|PowerShell"], event


def run_settings_command(project: Path, python: str | None = None) -> subprocess.CompletedProcess[str]:
    """Run the command `settings.json` gives PreToolUse, as Claude Code does, in a project folder, on a harmless call.

    `python` replaces the interpreter requirement (">=3.12"). No interpreter may be downloaded, and the shell is the
    platform's (the command is portable: `$CLAUDE_PROJECT_DIR` is put in by hand).
    """
    command = str(settings()["hooks"]["PreToolUse"][0]["hooks"][0]["command"])
    command = command.replace("$CLAUDE_PROJECT_DIR", project.as_posix())
    if python is not None:
        command = command.replace(">=3.12", python)
    if shutil.which("uv") is None:
        pytest.skip("uv is not installed")
    return subprocess.run(
        command,
        shell=True,
        input=bash("ls"),
        capture_output=True,
        text=True,
        encoding="utf-8",
        cwd=project,
        env={**os.environ, "UV_PYTHON_DOWNLOADS": "never"},
        check=False,
        timeout=120,
    )


def test_a_checkout_without_the_dispatcher_does_not_block_the_tools(tmp_path: Path) -> None:
    # A session keeps the hooks it read at start; its checkout may move to a commit that has no dispatch.py. Python
    # then exits with code 2 ("can't open file"), which would block every tool call, Read included.
    done = run_settings_command(tmp_path)
    assert done.returncode == 0, done.stderr


def test_no_python_that_fits_does_not_block_the_tools(tmp_path: Path) -> None:
    # With no interpreter that satisfies the pin and no download, uv exits with code 2 ("No interpreter found").
    (tmp_path / ".claude" / "hooks").mkdir(parents=True)
    shutil.copy(HOOKS / "dispatch.py", tmp_path / ".claude" / "hooks" / "dispatch.py")
    done = run_settings_command(tmp_path, python=">=99.0")
    assert done.returncode == 0, done.stderr


def test_the_settings_command_still_refuses_through_json(tmp_path: Path) -> None:
    # The refusal is the JSON answer, with exit code 0, so that `|| exit 0` and a refusal agree.
    folder = tmp_path / ".claude" / "hooks"
    shutil.copytree(HOOKS, folder, ignore=shutil.ignore_patterns("__pycache__"))
    command = str(settings()["hooks"]["PreToolUse"][0]["hooks"][0]["command"]).replace(
        "$CLAUDE_PROJECT_DIR", tmp_path.as_posix()
    )
    done = subprocess.run(
        command,
        shell=True,
        input=bash("cmake --build build/dev"),
        capture_output=True,
        text=True,
        encoding="utf-8",
        cwd=tmp_path,
        check=False,
        timeout=120,
    )
    assert refused_by_build_guard(done), (done.stdout, done.stderr)


def test_the_pre_tool_use_timeout_covers_the_pr_guard_checks() -> None:
    # pr_guard runs coney-tools checks for up to CHECK_TIMEOUT seconds each; the one entry has to outlast two of them.
    pr_guard = hooklib.load_guard("pr_guard")
    (entry,) = settings()["hooks"]["PreToolUse"]
    assert entry["hooks"][0]["timeout"] >= 2 * pr_guard.CHECK_TIMEOUT


def test_every_script_in_the_hooks_folder_is_the_dispatcher_a_guard_or_a_helper() -> None:
    scripts = {path.stem for path in HOOKS.glob("*.py")}
    guards = {name for name, _ in DISPATCH.PRE_GUARDS + DISPATCH.POST_GUARDS}
    helpers = {
        "dispatch",
        "hooklib",
        "roles",
        "identity",
        "protect",
        "harness",
        "pr_guard_shell",
        "pr_guard_repo",
        "pr_guard_routes",
    }
    assert guards | helpers == scripts, "a hook script nothing runs"


def test_the_settings_allow_the_identity_prefix_and_nothing_wider() -> None:
    allowed = settings()["permissions"]["allow"]
    assert "Bash(export CONEY_AGENT_TOKEN=*)" in allowed
    # The prefix is `export CONEY_AGENT_TOKEN=<token>; <command>`: the rule must not cover other exports.
    assert not [rule for rule in allowed if rule in ("Bash(export *)", "Bash(*)", "Bash", "Bash(export CONEY_AGENT_*)")]


# --- the guards, unchanged ------------------------------------------------------------------------------------------


def test_a_refusal_of_a_guard_comes_back_as_exit_2_with_its_text() -> None:
    code, out, err = DISPATCH.dispatch(bash("cmake --build build/dev"), CONFIG)
    assert (code, out) == (2, "") and "coney-tools build" in err


@pytest.mark.parametrize("command", ["ls -la", "git status", "uv run --project python coney-tools build status"])
def test_other_commands_pass_silently(command: str) -> None:
    assert DISPATCH.dispatch(bash(command), CONFIG)[0] == 0


def test_the_refusals_of_several_guards_come_together() -> None:
    command = "cmake --build build/dev\nwhile true; do sleep 1; done"
    code, _, err = DISPATCH.dispatch(bash(command), CONFIG)
    assert code == 2 and "coney-tools build" in err and "bound" in err.lower()


def test_push_guard_refuses_only_in_a_subagent() -> None:
    assert DISPATCH.dispatch(bash("git push origin main"), CONFIG)[0] == 0
    assert DISPATCH.dispatch(bash("git push origin main", agent_id="a1", agent_type="engineer"), CONFIG)[0] == 2


def test_the_agent_guard_runs_for_the_agent_tool() -> None:
    code, _, err = DISPATCH.dispatch(pre("Agent", {"prompt": "x", "subagent_type": "general-purpose"}), CONFIG)
    assert code == 2 and "engineer" in err
    assert DISPATCH.dispatch(pre("Agent", {"prompt": "x", "subagent_type": "Explore"}), CONFIG)[0] == 0
    assert DISPATCH.dispatch(pre("Agent", {"prompt": "x", "subagent_type": "engineer"}), CONFIG)[0] == 0


def test_a_guard_that_crashes_passes_the_call_and_leaves_a_line(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    def broken(name: str) -> Any:
        raise RuntimeError("boom")

    monkeypatch.setattr(DISPATCH, "guard_module", broken)
    monkeypatch.setattr("tempfile.gettempdir", lambda: str(tmp_path))
    assert DISPATCH.dispatch(bash("cmake --build x"), CONFIG)[0] == 0
    assert "boom" in (tmp_path / DISPATCH.LOG_FILE).read_text(encoding="utf-8")


@pytest.mark.parametrize("raw", ["", "not json", "[]", "null", "{}", json.dumps({"hook_event_name": "Stop"})])
def test_input_it_cannot_read_never_blocks(raw: str) -> None:
    assert DISPATCH.dispatch(raw, CONFIG) == (0, "", "")


# --- nothing turns the guards off -----------------------------------------------------------------------------------


@pytest.fixture
def copy_of_hooks(tmp_path: Path) -> Path:
    """A copy of the hooks folder to break, so the real one stays whole."""
    folder = tmp_path / "hooks"
    shutil.copytree(HOOKS, folder, ignore=shutil.ignore_patterns("__pycache__"))
    return folder


def run_copy(folder: Path, command: str = "cmake --build build/dev", **extra: Any) -> subprocess.CompletedProcess[str]:
    """Run a copy's dispatch.py as Claude Code does, on a shell command that build_guard refuses."""
    return subprocess.run(
        [sys.executable, str(folder / "dispatch.py")],
        input=bash(command, **extra),
        capture_output=True,
        text=True,
        encoding="utf-8",
        cwd=folder,
        check=False,
        timeout=60,
    )


def denial(done: subprocess.CompletedProcess[str]) -> str | None:
    """The reason of a refusal a run sent as Claude Code's JSON answer (exit code 0), or None when it did not refuse."""
    if done.returncode != 0 or not done.stdout.strip():
        return None
    output = json.loads(done.stdout)["hookSpecificOutput"]
    if output.get("permissionDecision") != "deny":
        return None
    return str(output["permissionDecisionReason"])


def refused_by_build_guard(done: subprocess.CompletedProcess[str]) -> bool:
    """Whether the run refused the build with build_guard's message."""
    return "coney-tools build" in (denial(done) or "")


def test_the_copy_works_unbroken(copy_of_hooks: Path) -> None:
    assert refused_by_build_guard(run_copy(copy_of_hooks))


@pytest.mark.parametrize(
    "content",
    [
        "require_role = tru",  # not TOML
        "x".encode("utf-16"),  # not UTF-8
        b"\xff\xfe\x00",  # not text at all
        b"",  # empty
        "roles = 3\nprotect = 'x'\nidentity = []\n",  # wrong shapes
    ],
    ids=["invalid-toml", "utf-16", "binary", "empty", "wrong-shapes"],
)
def test_an_unreadable_settings_file_leaves_every_guard_on(copy_of_hooks: Path, content: str | bytes) -> None:
    data = content.encode("utf-8") if isinstance(content, str) else content
    (copy_of_hooks / "hooks.toml").write_bytes(data)
    assert refused_by_build_guard(run_copy(copy_of_hooks))


def test_a_missing_settings_file_leaves_every_guard_on(copy_of_hooks: Path) -> None:
    (copy_of_hooks / "hooks.toml").unlink()
    assert refused_by_build_guard(run_copy(copy_of_hooks))


@pytest.mark.parametrize("module", ["hooklib", "roles", "identity"])
def test_a_rule_module_that_does_not_import_leaves_every_guard_on(copy_of_hooks: Path, module: str) -> None:
    (copy_of_hooks / f"{module}.py").write_text("import a_module_that_is_not_there\n", encoding="utf-8")
    assert refused_by_build_guard(run_copy(copy_of_hooks))
    # ... and says so where the person will see it.
    start = subprocess.run(
        [sys.executable, str(copy_of_hooks / "dispatch.py")],
        input=json.dumps({"hook_event_name": "SessionStart"}),
        capture_output=True,
        text=True,
        encoding="utf-8",
        check=False,
        timeout=60,
    )
    assert "could not be read" in json.loads(start.stdout)["systemMessage"]


def test_a_syntax_error_in_a_rule_module_leaves_every_guard_on(copy_of_hooks: Path) -> None:
    (copy_of_hooks / "roles.py").write_text("def broken(:\n", encoding="utf-8")
    assert refused_by_build_guard(run_copy(copy_of_hooks))


def key_read_refused(done: subprocess.CompletedProcess[str]) -> bool:
    """Whether the run refused the read of a private key."""
    return "private keys" in (denial(done) or "")


def test_what_no_one_may_touch_survives_a_settings_file_that_cannot_be_read(copy_of_hooks: Path) -> None:
    for content in (b"\xff\xfe\x00", b"", b"protect = 3"):
        (copy_of_hooks / "hooks.toml").write_bytes(content)
        assert key_read_refused(run_copy(copy_of_hooks, "cat ~/.ssh/id_ed25519"))
        assert refused_by_build_guard(run_copy(copy_of_hooks))
    (copy_of_hooks / "hooks.toml").unlink()
    assert key_read_refused(run_copy(copy_of_hooks, "cat ~/.ssh/id_ed25519"))


@pytest.mark.parametrize("module", ["roles", "identity"])
def test_a_broken_roles_or_identity_module_leaves_protect_and_the_guards_on(copy_of_hooks: Path, module: str) -> None:
    (copy_of_hooks / f"{module}.py").write_text("import a_module_that_is_not_there\n", encoding="utf-8")
    assert key_read_refused(run_copy(copy_of_hooks, "cat ~/.ssh/id_ed25519"))
    assert refused_by_build_guard(run_copy(copy_of_hooks))


def test_a_broken_protect_module_leaves_the_roles_and_the_guards_on(copy_of_hooks: Path) -> None:
    (copy_of_hooks / "protect.py").write_text("def broken(:\n", encoding="utf-8")
    assert refused_by_build_guard(run_copy(copy_of_hooks))
    done = run_copy(copy_of_hooks, "uv run --project python coney-tools pr open t", agent_type="engineer")
    assert "engineer" in (denial(done) or "")


def test_a_rule_that_crashes_does_not_stop_the_other_rules_or_the_guards(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    def broken(config: dict[str, Any], payload: dict[str, Any]) -> str | None:
        raise RuntimeError("roles broke")

    monkeypatch.setattr(DISPATCH.roles, "check", broken)
    monkeypatch.setattr("tempfile.gettempdir", lambda: str(tmp_path))
    # The identity refusal and a guard still judge the command.
    code, _, err = DISPATCH.dispatch(bash("cmake --build x; CONEY_AGENT_TOKEN=f coney-tools build"), CONFIG)
    assert code == 2 and "coney-tools build" in err and "CONEY_AGENT_*" in err
    assert "roles broke" in (tmp_path / DISPATCH.LOG_FILE).read_text(encoding="utf-8")


def test_a_broken_roles_table_keeps_the_identity_refusal_and_the_guards() -> None:
    broken = {
        "roles": {"engineer": "not a table", "tooling": {"aliases": "tooling-x", "deny_commands": ["("]}},
        "protect": {"commands": [{"pattern": 3}, "x"], "key_folders": "oops"},
        "helpers": "Explore",
    }
    code, _, err = DISPATCH.dispatch(bash("cmake --build x; export CONEY_AGENT_TOKEN=f", agent_type="tooling"), broken)
    assert code == 2 and "coney-tools build" in err and "CONEY_AGENT_*" in err


def test_the_defaults_after_a_failure_turn_on_no_role_rule(monkeypatch: pytest.MonkeyPatch) -> None:
    assert DISPATCH.dispatch(bash("ls"), {})[0] == 0  # require_role off, no roles
    assert DISPATCH.dispatch(pre("Write", {"file_path": "x"}), {})[0] == 0


# --- the role rules and the identity --------------------------------------------------------------------------------


def test_the_role_rules_run_in_the_dispatcher() -> None:
    command = "uv run --project python coney-tools pr open t"
    code, _, err = DISPATCH.dispatch(bash(command, agent_type="engineer"), CONFIG)
    assert code == 2 and "engineer" in err
    strict = {**CONFIG, "require_role": True}
    assert DISPATCH.dispatch(bash("ls", agent_type=None), strict)[0] == 2
    assert DISPATCH.dispatch(pre("Read", {"file_path": "x"}, agent_type=None), strict)[0] == 0


@pytest.mark.parametrize("agent_type", ["coordinator", "engineer", "tooling", "reviewer"])
def test_with_the_committed_settings_a_role_works_and_a_session_with_none_only_reads(agent_type: str) -> None:
    # `coney-agent <role>` starts `claude --agent <role>`; a plain `claude` gives no `agent_type`.
    assert DISPATCH.dispatch(bash("git status", agent_type=agent_type), CONFIG)[0] == 0
    code, _, err = DISPATCH.dispatch(bash("git status", agent_type=None), CONFIG)
    assert code == 2 and "no Coney role" in err and "`coney-agent`" in err
    assert DISPATCH.dispatch(pre("Read", {"file_path": "x"}, agent_type=None), CONFIG)[0] == 0


def test_a_coney_tools_command_is_returned_with_an_identity_token(identity_folder: Path) -> None:
    command = "uv run --project python coney-tools build --agent x --preset ci"
    payload = bash(command, agent_type="engineer", agent_id="a1", session_id="s1", tool_use_id="t1")
    code, out, err = DISPATCH.dispatch(payload, CONFIG)
    assert (code, err) == (0, "")
    result = json.loads(out)["hookSpecificOutput"]
    assert result["hookEventName"] == "PreToolUse"
    new = result["updatedInput"]["command"]
    token = new.split("export CONEY_AGENT_TOKEN=")[1].split(";")[0]
    assert new == f"export CONEY_AGENT_TOKEN={token}; {command}" and len(token) == 32
    written = json.loads((identity_folder / file_name(token)).read_text(encoding="utf-8"))
    assert (written["id"], written["type"], written["session"], written["tool_use_id"]) == (
        "a1",
        "engineer",
        "s1",
        "t1",
    )
    # No permission decision: Claude Code still checks the rewritten command against the allow rules.
    assert "permissionDecision" not in result


def test_a_command_that_sets_the_identity_is_refused_before_it_gets_one() -> None:
    code, out, err = DISPATCH.dispatch(bash("CONEY_AGENT_ID=other coney-tools build"), CONFIG)
    assert code == 2 and out == "" and "CONEY_AGENT_" in err


def test_a_command_that_is_refused_gets_no_identity(identity_folder: Path) -> None:
    code, out, _ = DISPATCH.dispatch(bash("cmake --build x && coney-tools repo audit"), CONFIG)
    assert code == 2 and out == "" and not identity_folder.exists()


def test_no_one_may_name_the_identity_folder_or_a_private_key() -> None:
    for command in ("ls ~/.coney/identity", "cat ~/.ssh/id_ed25519", "type C:\\Users\\x\\.coney\\identity\\a.json"):
        for kind in (None, "engineer", "coordinator"):  # None: a session with no role
            assert DISPATCH.dispatch(bash(command, agent_type=kind), CONFIG)[0] == 2, (command, kind)
    assert DISPATCH.dispatch(pre("Read", {"file_path": "/home/x/.ssh/id_ed25519"}), CONFIG)[0] == 2
    assert DISPATCH.dispatch(pre("Glob", {"pattern": "*", "path": "/home/x/.coney/identity"}), CONFIG)[0] == 2
    assert DISPATCH.dispatch(pre("Read", {"file_path": "/home/x/.ssh/id_ed25519.pub"}), CONFIG)[0] == 0


# --- PostToolUse and SessionStart -----------------------------------------------------------------------------------


def issued(identity_folder: Path, **extra: Any) -> tuple[str, str]:
    """Run a coney-tools call through the dispatcher; (the token in front of it, the input that went in)."""
    payload = bash("coney-tools build status", tool_use_id="t9", **extra)
    out = json.loads(DISPATCH.dispatch(payload, CONFIG)[1])
    token = out["hookSpecificOutput"]["updatedInput"]["command"].split("=")[1].split(";")[0]
    assert (identity_folder / file_name(token)).exists()
    return token, payload


def post(payload_text: str) -> str:
    """The same call as a PostToolUse input."""
    return json.dumps({**json.loads(payload_text), "hook_event_name": "PostToolUse", "tool_response": {}})


def test_the_identity_file_is_removed_when_the_command_ends(identity_folder: Path) -> None:
    token, payload = issued(identity_folder)
    assert DISPATCH.dispatch(post(payload), CONFIG)[0] == 0
    assert not (identity_folder / file_name(token)).exists()


@pytest.mark.parametrize("event", ["PostToolUseFailure", "PermissionDenied"])
def test_the_identity_file_is_removed_when_the_command_fails_or_is_denied(identity_folder: Path, event: str) -> None:
    token, payload = issued(identity_folder)
    failed = json.dumps({**json.loads(payload), "hook_event_name": event, "error": "exit 1"})
    assert DISPATCH.dispatch(failed, CONFIG) == (0, "", "")
    assert not (identity_folder / file_name(token)).exists()


def test_a_background_command_keeps_its_file_for_the_sweep(identity_folder: Path) -> None:
    # PostToolUse comes as soon as a background process is started, before uv has started the tool that reads the file.
    payload = json.dumps(
        {
            "hook_event_name": "PreToolUse",
            "tool_name": "Bash",
            "tool_use_id": "t7",
            "agent_type": "engineer",
            "tool_input": {"command": "coney-tools build --detach", "run_in_background": True},
        }
    )
    out = json.loads(DISPATCH.dispatch(payload, CONFIG)[1])
    token = out["hookSpecificOutput"]["updatedInput"]["command"].split("=")[1].split(";")[0]
    assert out["hookSpecificOutput"]["updatedInput"]["run_in_background"] is True
    DISPATCH.dispatch(post(payload), CONFIG)
    assert (identity_folder / file_name(token)).exists()


def test_session_start_warns_about_a_missing_role_and_is_quiet_with_one() -> None:
    code, out, _ = DISPATCH.dispatch(json.dumps({"hook_event_name": "SessionStart", "source": "startup"}), CONFIG)
    result = json.loads(out)
    assert code == 0 and "coney-agent" in result["systemMessage"]
    assert result["hookSpecificOutput"]["hookEventName"] == "SessionStart"
    quiet = json.dumps({"hook_event_name": "SessionStart", "agent_type": "coordinator"})
    assert DISPATCH.dispatch(quiet, CONFIG) == (0, "", "")


def test_session_start_says_when_the_settings_could_not_be_read(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    (tmp_path / "hooks.toml").write_bytes(b"\xff\xfe")
    monkeypatch.setattr(hooklib, "CONFIG_FILE", tmp_path / "hooks.toml")
    monkeypatch.setattr(hooklib, "load_config_checked", lambda path=tmp_path / "hooks.toml": ({}, "hooks.toml: bad"))
    monkeypatch.setattr("tempfile.gettempdir", lambda: str(tmp_path))
    code, out, _ = DISPATCH.dispatch(json.dumps({"hook_event_name": "SessionStart", "agent_type": "coordinator"}))
    assert code == 0 and "could not be read" in json.loads(out)["systemMessage"]


def test_post_tool_use_runs_the_trailer_guard_for_commits_only() -> None:
    payload = {"hook_event_name": "PostToolUse", "tool_name": "Bash", "tool_input": {"command": "ls"}}
    assert DISPATCH.dispatch(json.dumps(payload), CONFIG) == (0, "", "")
    payload["tool_input"] = {"command": "git commit -m x"}
    payload["tool_response"] = {"stdout": "", "stderr": ""}
    assert DISPATCH.dispatch(json.dumps(payload), CONFIG)[0] == 0  # nothing to judge: it fails open


@pytest.mark.parametrize(
    "command",
    ["git config commit.gpgsign false", "git log --grep commit", "git commit-tree HEAD^{tree}", "echo commit"],
)
def test_the_trailer_guard_is_not_started_for_a_command_that_only_says_commit(command: str) -> None:
    payload = {"tool_name": "Bash", "tool_input": {"command": command}}
    assert [name for name, applies in DISPATCH.POST_GUARDS if applies(payload)] == []


def test_a_refusal_is_the_json_answer_with_exit_code_0() -> None:
    # Exit code 2 is also what uv and Python give when they cannot start, and it blocks every tool call, so the
    # dispatcher never uses it: a refusal is Claude Code's JSON answer.
    code, out, err = DISPATCH.answer(bash("cmake --build build/dev"), CONFIG)
    assert (code, err) == (0, "")
    output = json.loads(out)["hookSpecificOutput"]
    assert output["hookEventName"] == "PreToolUse" and output["permissionDecision"] == "deny"
    assert "coney-tools build" in output["permissionDecisionReason"]
    assert DISPATCH.answer(bash("ls"), CONFIG) == (0, "", "")


def test_a_refusal_after_a_tool_ran_is_a_block_in_json(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(DISPATCH, "run_guard", lambda name, raw, config: "The trailer is wrong.\n")
    code, out, err = DISPATCH.answer(post(bash('git commit -m "x"')), CONFIG)
    assert (code, err) == (0, "")
    assert json.loads(out) == {"decision": "block", "reason": "The trailer is wrong.\n"}


# --- the real process -----------------------------------------------------------------------------------------------


def test_the_script_runs_as_a_process() -> None:
    refused = run_copy(HOOKS, "ninja -j4")
    assert refused_by_build_guard(refused) and refused.stderr == ""
    passed = run_copy(HOOKS, "ls")
    assert (passed.returncode, passed.stdout, passed.stderr) == (0, "", "")


def test_the_script_answers_input_it_cannot_read() -> None:
    done = subprocess.run(
        [sys.executable, str(HOOKS / "dispatch.py")],
        input="not json",
        capture_output=True,
        text=True,
        encoding="utf-8",
        cwd=REPO.parent,
        check=False,
        timeout=60,
    )
    assert done.returncode == 0


# --- the PR guard's switch is read from the conventions, not from hooks.toml -----------------------------------------


def checkout_with_switch(root: Path, value: str) -> Path:
    """A made-up checkout: a copy of the hooks and a conventions file whose `pull_requests.open_only` is `value`."""
    shutil.copytree(HOOKS, root / ".claude" / "hooks", ignore=shutil.ignore_patterns("__pycache__"))
    (root / ".github").mkdir()
    (root / ".github" / "conventions.yml").write_text(f"pull_requests:\n  open_only: {value}\n", encoding="utf-8")
    return root / ".claude" / "hooks"


def test_open_only_in_the_conventions_makes_the_dispatcher_refuse_gh_pr_create(tmp_path: Path) -> None:
    folder = checkout_with_switch(tmp_path, "true")
    reason = denial(run_copy(folder, "gh pr create --title 'tools: Add x' --body-file PR.md"))
    assert reason is not None and reason.startswith("Blocked: pull requests are opened only by coney-tools pr open")


def test_open_only_off_in_the_conventions_leaves_the_checks_of_a_hand_typed_command(tmp_path: Path) -> None:
    folder = checkout_with_switch(tmp_path, "false")
    reason = denial(run_copy(folder, "gh pr create --fill"))
    assert reason is not None and "takes the description from somewhere" in reason and "opened only by" not in reason


@pytest.mark.parametrize(
    ("command", "read"),
    [
        ("uv run --project python coney-tools pcsx2 claim --agent a", True),
        ("coney-tools trace original s.toml --out o.csv --agent a", True),
        ("coney-tools trace events --side original s.toml --out o --agent a", True),
        ("coney-tools trace mission --side original s.toml --out o --agent a", True),
        ("coney-tools clips record x.p2s --out c.mp4 --agent a", True),
        ("coney-tools trace coney s.toml --out o.csv", False),
        ("coney-tools clips publish --pr 1 c.mp4", False),
    ],
)
def test_the_pcsx2_guard_reads_every_command_that_drives_pcsx2(command: str, read: bool) -> None:
    """The commands that drive PCSX2 from other groups (`trace original`, `clips record`) reach the PCSX2 guard too."""
    dispatch = load_hook_module("dispatch")
    assert dispatch._pcsx2({"tool_name": "Bash", "tool_input": {"command": command}}) is read
