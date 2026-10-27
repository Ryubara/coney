# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for .claude/hooks/identity.py, the identity token of the hooks (read back by coney_tools.identity).

The input is a synthetic hook payload; the model comes from a synthetic transcript with `message.model` on its
assistant entries, the main session's at `transcript_path` and a subagent's under `<name>/subagents/agent-<id>.jsonl`
(docs/guides/agents.md#trailer-guard). The identity folder is a temporary one.
"""

import hashlib
import json
import os
import re
import stat
from pathlib import Path
from typing import Any

import pytest
from hooks_support import HOOKS, load_hook_module

from coney_tools import identity as reader

hooklib = load_hook_module("hooklib")
identity = load_hook_module("identity")

CONFIG = hooklib.load_config(HOOKS / "hooks.toml")
COMMAND = "uv run --project python coney-tools build --agent x --preset ci --test"
NOW = 5_000_000.0


def write_transcript(path: Path, model: str) -> None:
    """A transcript whose latest assistant entry names `model`."""
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        {"message": {"role": "user", "content": "hi"}},
        {"message": {"role": "assistant", "model": "claude-opus-4-1"}},
        {"message": {"role": "assistant", "model": model}},
    ]
    path.write_text("\n".join(json.dumps(line) for line in lines) + "\n", encoding="utf-8")


def payload(
    tmp_path: Path,
    command: str = COMMAND,
    tool: str = "Bash",
    agent_id: str | None = None,
    agent_type: str | None = None,
    mode: str = "default",
    **tool_input: Any,
) -> dict[str, Any]:
    """A PreToolUse payload with a transcript naming Sonnet (the main one) and Opus (the subagent's)."""
    main = tmp_path / "session.jsonl"
    write_transcript(main, "claude-sonnet-5-5")
    data: dict[str, Any] = {
        "hook_event_name": "PreToolUse",
        "tool_name": tool,
        "tool_input": {"command": command, "description": "d", "timeout": 5, **tool_input},
        "transcript_path": str(main),
        "permission_mode": mode,
        "session_id": "sess-1",
        "tool_use_id": "toolu_1",
    }
    if agent_id:
        data["agent_id"] = agent_id
        write_transcript(tmp_path / "session" / "subagents" / f"agent-{agent_id}.jsonl", "claude-opus-5-5[1m]")
    if agent_type:
        data["agent_type"] = agent_type
    return data


def token_of(command: str) -> str:
    """The token in the prefix of an injected command."""
    found = re.search(r"CONEY_AGENT_TOKEN\s*=\s*'?([0-9a-f]{32})", command)
    assert found is not None, command
    return found.group(1)


def folder(tmp_path: Path) -> Path:
    """The temporary identity folder."""
    return tmp_path / "identity"


def test_a_coney_tools_command_gets_a_token_and_nothing_else_in_front_of_it(tmp_path: Path) -> None:
    updated = identity.updated_input(payload(tmp_path, agent_type="coordinator"), CONFIG, folder(tmp_path), NOW)
    assert updated is not None
    token = token_of(updated["command"])
    assert updated["command"] == f"export CONEY_AGENT_TOKEN={token}; {COMMAND}"
    assert len(token) == 32 and set(token) <= set("0123456789abcdef")
    # Everything else of the tool input is kept: updatedInput replaces the whole input.
    assert updated["description"] == "d" and updated["timeout"] == 5


def test_the_tokens_are_random(tmp_path: Path) -> None:
    tokens = {
        token_of(identity.updated_input(payload(tmp_path), CONFIG, folder(tmp_path), NOW)["command"]) for _ in range(20)
    }
    assert len(tokens) == 20


def test_the_file_holds_the_real_identity_and_where_it_came_from(tmp_path: Path) -> None:
    data = payload(tmp_path, agent_id="a3c1bc15", agent_type="engineer")
    updated = identity.updated_input(data, CONFIG, folder(tmp_path), NOW)
    assert updated is not None
    written = json.loads(
        (folder(tmp_path) / reader.file_name(token_of(updated["command"]))).read_text(encoding="utf-8")
    )
    assert written == {
        "version": 1,
        "id": "a3c1bc15",
        "type": "engineer",
        "model": "claude-opus-5-5",
        "session": "sess-1",
        "agent": "a3c1bc15",
        "tool_use_id": "toolu_1",
        "issued": NOW,
        "expires": NOW + 600,
    }


def test_the_tool_reads_back_what_the_hook_wrote(tmp_path: Path) -> None:
    where = folder(tmp_path)
    for kind, agent_id, expected in (
        ("coordinator", None, ("main", "coordinator", "claude-sonnet-5-5")),
        ("engineer", "a1", ("a1", "engineer", "claude-opus-5-5")),
        (None, None, ("main", "none", "claude-sonnet-5-5")),
    ):
        updated = identity.updated_input(payload(tmp_path, agent_id=agent_id, agent_type=kind), CONFIG, where, NOW)
        assert updated is not None
        env = {"CONEY_AGENT_TOKEN": token_of(updated["command"])}
        found = reader.load(env, folder=where, now=NOW + 1)
        assert found.verified and (found.id, found.type, found.model) == expected


def test_an_unknown_model_is_written_as_unknown_and_still_verified(tmp_path: Path) -> None:
    data = payload(tmp_path)
    data["transcript_path"] = str(tmp_path / "missing.jsonl")
    updated = identity.updated_input(data, CONFIG, folder(tmp_path), NOW)
    assert updated is not None
    found = reader.load({"CONEY_AGENT_TOKEN": token_of(updated["command"])}, folder=folder(tmp_path), now=NOW)
    assert found.verified and found.model == "unknown"


def test_a_typed_or_changed_token_is_worth_nothing(tmp_path: Path) -> None:
    where = folder(tmp_path)
    updated = identity.updated_input(payload(tmp_path, agent_type="reviewer", agent_id="a1"), CONFIG, where, NOW)
    assert updated is not None
    real = token_of(updated["command"])
    forged = [
        {"CONEY_AGENT_TOKEN": "0" * 32},
        {"CONEY_AGENT_TOKEN": real[:-1] + ("0" if real[-1] != "0" else "1")},
        {"CONEY_AGENT_TOKEN": "../" + real},
        {
            "CONEY_AGENT_ID": "a1",
            "CONEY_AGENT_TYPE": "coordinator",
            "CONEY_AGENT_MODEL": "x",
            "CONEY_AGENT_INJECTED": "1",
        },
    ]
    for env in forged:
        found = reader.load(env, folder=where, now=NOW)
        assert not found.verified and found.type == "unknown", env


def test_a_token_expires(tmp_path: Path) -> None:
    where = folder(tmp_path)
    updated = identity.updated_input(payload(tmp_path), CONFIG, where, NOW)
    assert updated is not None
    env = {"CONEY_AGENT_TOKEN": token_of(updated["command"])}
    assert reader.load(env, folder=where, now=NOW + 599).verified
    assert not reader.load(env, folder=where, now=NOW + 601).verified


def test_the_lifetime_is_a_setting(tmp_path: Path) -> None:
    short = {**CONFIG, "identity": {**CONFIG["identity"], "ttl_seconds": 30}}
    updated = identity.updated_input(payload(tmp_path), short, folder(tmp_path), NOW)
    assert updated is not None
    env = {"CONEY_AGENT_TOKEN": token_of(updated["command"])}
    assert reader.load(env, folder=folder(tmp_path), now=NOW + 29).verified
    assert not reader.load(env, folder=folder(tmp_path), now=NOW + 31).verified
    assert identity.ttl({"identity": {"ttl_seconds": "x"}}) == 600.0 and identity.ttl({}) == 600.0


def test_every_call_removes_the_expired_files_and_only_those(tmp_path: Path) -> None:
    where = folder(tmp_path)
    old = identity.updated_input(payload(tmp_path), CONFIG, where, NOW)
    assert old is not None
    (where / "stray.json").write_text("not ours", encoding="utf-8")
    fresh = identity.updated_input(payload(tmp_path), CONFIG, where, NOW + 3600)
    assert fresh is not None
    names = sorted(path.name for path in where.glob("*"))
    assert names == [reader.file_name(token_of(fresh["command"]))]


def test_release_removes_the_files_of_the_ended_call_only(tmp_path: Path) -> None:
    where = folder(tmp_path)
    mine = identity.updated_input(payload(tmp_path), CONFIG, where, NOW)
    other_payload = payload(tmp_path)
    other_payload["tool_use_id"] = "toolu_2"
    other = identity.updated_input(other_payload, CONFIG, where, NOW)
    assert mine is not None and other is not None
    identity.release(payload(tmp_path), where)
    assert not (where / reader.file_name(token_of(mine["command"]))).exists()
    assert (where / reader.file_name(token_of(other["command"]))).exists()


def test_release_leaves_a_background_commands_file_for_the_sweep(tmp_path: Path) -> None:
    where = folder(tmp_path)
    started = identity.updated_input(payload(tmp_path, run_in_background=True), CONFIG, where, NOW)
    assert started is not None and started["run_in_background"] is True
    identity.release(payload(tmp_path, run_in_background=True), where)
    assert (where / reader.file_name(token_of(started["command"]))).exists()


def test_release_does_nothing_without_a_tool_call_id(tmp_path: Path) -> None:
    where = folder(tmp_path)
    updated = identity.updated_input(payload(tmp_path), CONFIG, where, NOW)
    assert updated is not None
    data = payload(tmp_path)
    del data["tool_use_id"]
    identity.release(data, where)
    assert len(list(where.glob("*.json"))) == 1


def test_issuing_leaves_no_temporary_file(tmp_path: Path) -> None:
    updated = identity.updated_input(payload(tmp_path), CONFIG, folder(tmp_path), NOW)
    assert updated is not None
    assert [path.suffix for path in folder(tmp_path).iterdir()] == [".json"]  # no leftover temporary file


@pytest.mark.parametrize(
    "command",
    ["git commit -m x", "git -C ../x commit --amend", "git --no-pager commit -F msg", "coney-tools repo audit"],
)
def test_commits_and_coney_tools_commands_get_one(tmp_path: Path, command: str) -> None:
    assert identity.wanted(payload(tmp_path, command), CONFIG)


@pytest.mark.parametrize(
    "command",
    [
        "ls",
        "git status",
        "uv run --project python pytest",
        'echo "run coney-tools later"',
        "git log --grep commit",
        "git config commit.gpgsign false",
    ],
)
def test_other_commands_do_not(tmp_path: Path, command: str) -> None:
    assert identity.updated_input(payload(tmp_path, command), CONFIG, folder(tmp_path), NOW) is None
    assert not folder(tmp_path).exists()


def test_the_switch_turns_it_off(tmp_path: Path) -> None:
    off = {**CONFIG, "identity": {**CONFIG["identity"], "enabled": False}}
    assert identity.updated_input(payload(tmp_path), off, folder(tmp_path), NOW) is None
    assert identity.updated_input(payload(tmp_path), {}, folder(tmp_path), NOW) is None


def test_other_tools_are_left_alone(tmp_path: Path) -> None:
    assert identity.updated_input(payload(tmp_path, tool="Read"), CONFIG, folder(tmp_path), NOW) is None


def test_powershell_is_injected_only_in_the_modes_that_let_it_set_variables(tmp_path: Path) -> None:
    # Measured 2026-10-22: the PowerShell tool refuses `$env:X = ...` in default and acceptEdits mode.
    for mode in ("default", "acceptEdits"):
        assert identity.updated_input(payload(tmp_path, tool="PowerShell", mode=mode), CONFIG, folder(tmp_path)) is None
    updated = identity.updated_input(
        payload(tmp_path, tool="PowerShell", mode="bypassPermissions"), CONFIG, folder(tmp_path), NOW
    )
    assert updated is not None
    token = token_of(updated["command"])
    assert updated["command"] == f"$env:CONEY_AGENT_TOKEN = '{token}'; {COMMAND}"


def test_values_cannot_carry_shell_syntax(tmp_path: Path) -> None:
    data = payload(tmp_path, agent_id="a1; rm -rf x", agent_type="en gi$(x)")
    values = identity.values(data)
    assert values["id"] == "a1rm-rfx" and values["type"] == "engix"
    assert identity.safe("claude-opus-5-5[1m]") == "claude-opus-5-5"


@pytest.mark.parametrize(
    "command",
    [
        "CONEY_AGENT_TOKEN=other uv run --project python coney-tools build",
        "export CONEY_AGENT_TYPE=coordinator; coney-tools pr open t",
        "export CONEY_AGENT_ID=x CONEY_AGENT_MODEL=y",
        "$env:CONEY_AGENT_TOKEN = 'x'; coney-tools build",
        "$env:CONEY_AGENT_ID='x'",
        "set CONEY_AGENT_ID=x",
        "env CONEY_AGENT_INJECTED=1 coney-tools build",
        'bash -c "CONEY_AGENT_ID=x coney-tools build"',
        "unset CONEY_AGENT_ID",
        "[Environment]::SetEnvironmentVariable('CONEY_AGENT_ID','x')",
        "python -c \"import os; os.environ['CONEY_AGENT_ID'] = 'x'\"",
        "python -c \"import os; os.putenv('CONEY_AGENT_ID', 'x')\"",
        "export -n CONEY_AGENT_ID",
    ],
)
def test_a_command_that_sets_the_variables_is_refused_as_a_courtesy(command: str) -> None:
    reason = identity.assignment_refusal(command)
    assert reason is not None and reason.startswith("Blocked:")
    assert "worth nothing" in reason  # the text does not claim the refusal is what protects


@pytest.mark.parametrize(
    "command",
    [
        "echo $CONEY_AGENT_TOKEN",
        'echo "${CONEY_AGENT_TYPE}"',
        "echo $env:CONEY_AGENT_MODEL",
        "printenv CONEY_AGENT_ID",
        "python -c \"import os;print(os.environ.get('CONEY_AGENT_ID'), os.environ['CONEY_AGENT_TYPE'])\"",
        "echo $CONEY_AGENT_ID == $CONEY_AGENT_TYPE",
        "git commit -F - <<'EOF'\nuses CONEY_AGENT_ID=x in the text\nEOF",
        'git commit -m "document CONEY_AGENT_ID=x"',
        "ls",
    ],
)
def test_reading_the_variables_or_writing_about_them_is_not_refused(command: str) -> None:
    assert identity.assignment_refusal(command) is None


# --- the ways around the courtesy refusal: they find no identity file -----------------------------------------------


@pytest.mark.parametrize(
    "command",
    [
        'export "CONEY_AGENT_TYPE=reviewer"; coney-tools orig study 0x1',
        "export 'CONEY_AGENT_ID'=x; coney-tools build",
        'export CONEY_AGEN""T_ID=x; coney-tools build',
        "n=CONEY_AGENT_ID; export $n=x; coney-tools build",
        "read CONEY_AGENT_ID <<< x; coney-tools build",
    ],
)
def test_a_command_that_slips_past_the_refusal_still_gets_no_identity_it_made_up(tmp_path: Path, command: str) -> None:
    # The spelling check is not the protection (it cannot win against shell). Whatever the command sets, the tool takes
    # who ran it from the file the token names, and the token is the hook's: the command's own variables are ignored.
    updated = identity.updated_input(payload(tmp_path, command, agent_type="engineer"), CONFIG, folder(tmp_path), NOW)
    assert updated is not None
    env = {"CONEY_AGENT_TOKEN": token_of(updated["command"]), "CONEY_AGENT_TYPE": "coordinator", "CONEY_AGENT_ID": "x"}
    found = reader.load(env, folder=folder(tmp_path), now=NOW)
    assert (found.type, found.verified) == ("engineer", True)
    assert folder(tmp_path).exists()


# --- the file name hides the token; the folder is private -----------------------------------------------------------


def test_the_file_is_named_by_a_hash_of_the_token_not_by_the_token(tmp_path: Path) -> None:
    updated = identity.updated_input(payload(tmp_path), CONFIG, folder(tmp_path), NOW)
    assert updated is not None
    token = token_of(updated["command"])
    (name,) = [path.name for path in folder(tmp_path).iterdir()]
    assert token not in name and name == hashlib.sha256(token.encode()).hexdigest() + ".json"
    assert name == identity.file_name(token) == reader.file_name(token)  # the hook and the tool agree


def test_a_listing_of_the_folder_gives_no_one_another_commands_identity(tmp_path: Path) -> None:
    # The hole of the first version: the file was named by the token, so an engineer that listed the folder could
    # export a reviewer's token. Now what it can list is a hash, and a hash is not a token.
    where = folder(tmp_path)
    reviewer = identity.updated_input(payload(tmp_path, agent_type="reviewer", agent_id="r1"), CONFIG, where, NOW)
    engineer = identity.updated_input(payload(tmp_path, agent_type="engineer", agent_id="e1"), CONFIG, where, NOW)
    assert reviewer is not None and engineer is not None
    listed = [path.stem for path in where.iterdir()]
    assert len(listed) == 2
    for name in listed:
        env = {"CONEY_AGENT_TOKEN": name}
        found = reader.load(env, folder=where, now=NOW)
        assert not found.verified and found.type == "unknown", name
    mine = reader.load({"CONEY_AGENT_TOKEN": token_of(engineer["command"])}, folder=where, now=NOW)
    assert (mine.type, mine.verified) == ("engineer", True)


@pytest.mark.skipif(os.name == "nt", reason="POSIX permission bits")
def test_the_folder_is_private_and_the_file_is_owner_only(tmp_path: Path) -> None:
    updated = identity.updated_input(payload(tmp_path), CONFIG, folder(tmp_path), NOW)
    assert updated is not None
    assert stat.S_IMODE(folder(tmp_path).stat().st_mode) == 0o700
    (path,) = list(folder(tmp_path).iterdir())
    assert stat.S_IMODE(path.stat().st_mode) == 0o600


def test_leftover_temporary_files_are_swept_when_old_and_kept_when_fresh(tmp_path: Path) -> None:
    where = folder(tmp_path)
    where.mkdir(parents=True)
    old, fresh = where / "old.tmp", where / "fresh.tmp"
    old.write_text("x", encoding="utf-8")
    fresh.write_text("x", encoding="utf-8")
    os.utime(old, (NOW - 3600, NOW - 3600))
    os.utime(fresh, (NOW - 5, NOW - 5))
    identity.sweep(where, NOW)
    assert not old.exists() and fresh.exists()


def test_the_courtesy_refusal_leaves_text_that_merely_mentions_an_assignment() -> None:
    for command in (
        "sed -i s/CONEY_AGENT_ID=a/CONEY_AGENT_ID=b/ docs/guides/agents.md",
        "git diff -U0 -- docs | grep -c '^+CONEY_AGENT_ID=x'",
    ):
        assert identity.assignment_refusal(command) is None, command
