# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for .claude/hooks/protect.py, what no session may touch whatever its role.

The input is a synthetic hook payload (docs/guides/agents.md#hooks). The rules are built in, so no settings file is
involved; test_dispatch.py checks that they survive one that cannot be read.
"""

from pathlib import Path
from typing import Any

import pytest
from hooks_support import load_hook_module

protect = load_hook_module("protect")


def call(tool: str, tool_input: dict[str, Any], agent_type: str = "") -> dict[str, Any]:
    """A PreToolUse payload."""
    payload: dict[str, Any] = {"hook_event_name": "PreToolUse", "tool_name": tool, "tool_input": tool_input}
    if agent_type:
        payload["agent_type"] = agent_type
    return payload


def bash(command: str, agent_type: str = "") -> dict[str, Any]:
    """A Bash call."""
    return call("Bash", {"command": command}, agent_type)


EVERYONE = ["", "coordinator", "engineer", "tooling", "reviewer", "Explore"]


# --- ~/.coney: the whole folder, however it is spelled --------------------------------------------------------------


@pytest.mark.parametrize("agent_type", EVERYONE)
@pytest.mark.parametrize(
    "command",
    [
        "ls ~/.coney/identity",
        "ls ~/.coney/*",
        "ls ~/.coney/ident*",
        "ls ~/.coney",
        "ls -la ~/.coney/",
        "cat ~/.coney/./identity/a.json",
        "cat ~/x/../.coney/identity/a.json",
        "cat ~/.CONEY/Identity/a.json",
        "cat $HOME/.coney/identity/a.json",
        'cat "${HOME}/.coney/identity/a.json"',
        "cat ~/.con*/identity/a.json",
        "cat ~/.c?ney/identity/a.json",
        "cat C:\\Users\\x\\.coney\\identity\\a.json",
        "type %USERPROFILE%\\.coney\\identity\\a.json",
        "Get-Content $env:USERPROFILE\\.coney\\identity\\a.json",
        "Get-ChildItem $HOME/.coney -Recurse",
        'echo x > "$HOME/.coney/identity/aaaa.json"',
        "cd ~/.coney && ls",
        "rm -rf ~/.coney",
    ],
)
def test_no_one_names_the_coney_folder(command: str, agent_type: str) -> None:
    reason = protect.refusal(bash(command, agent_type))
    assert reason is not None and "~/.coney" in reason, command


@pytest.mark.parametrize("agent_type", EVERYONE)
def test_no_tool_reads_it_either(agent_type: str) -> None:
    for tool, tool_input in (
        ("Read", {"file_path": "/home/x/.coney/identity/a.json"}),
        ("Read", {"file_path": "/home/x/.coney/./identity/a.json"}),
        ("Read", {"file_path": "C:\\Users\\x\\.coney\\identity\\a.json"}),
        ("Glob", {"pattern": "**/*.json", "path": "/home/x/.coney"}),
        ("Glob", {"pattern": "/home/x/.coney/**"}),
        ("Grep", {"pattern": "expires", "path": "/home/x/.coney"}),
        ("Write", {"file_path": "/home/x/.coney/identity/a.json", "content": "{}"}),
        ("Edit", {"file_path": "~/.coney/identity/a.json", "old_string": "a", "new_string": "b"}),
        ("NotebookEdit", {"notebook_path": "/home/x/.coney/n.ipynb"}),
    ):
        assert protect.refusal(call(tool, tool_input, agent_type)), (tool, tool_input)


def test_a_folder_that_only_looks_like_it_is_fine() -> None:
    for command in ("ls ~/.coneys", "ls coney-tools", "cat .coney-busy", "ls my.coney/x", "find . -name '.*'"):
        assert protect.refusal(bash(command)) is None, command
    assert protect.refusal(call("Read", {"file_path": "/home/x/coney.local.toml"})) is None


# --- search patterns and text are not paths -------------------------------------------------------------------------


def test_a_search_for_the_name_is_not_a_use_of_the_folder() -> None:
    for command in (
        'grep -rn "~/.ssh" docs',
        "grep -rn '.coney/identity' docs/guides",
        "grep -e '.coney/identity' -e x docs",
        'rg "\\.coney/identity" docs',
        'rg --regexp ".ssh/id_rsa" docs',
        'git log -S".coney/identity" --oneline',
        'git log -S ".coney/identity" -- docs',
        'git log --grep="~/.ssh" --oneline',
        'git grep -n "~/.coney" -- docs',
        'Select-String -Pattern ".coney/identity" -Path docs/guides/agents.md',
    ):
        assert protect.refusal(bash(command)) is None, command
    # The pattern fields of the search tools, and the text a file is given or changed to.
    assert protect.refusal(call("Grep", {"pattern": ".coney/identity", "path": "docs"})) is None
    write = call("Write", {"file_path": "/repo/docs/a.md", "content": "see ~/.coney/identity and ~/.ssh/id_rsa"})
    assert protect.refusal(write) is None
    edit = call("Edit", {"file_path": "/repo/docs/a.md", "old_string": "~/.ssh/id_rsa", "new_string": ".coney"})
    assert protect.refusal(edit) is None


def test_but_a_path_to_search_in_is_still_a_use() -> None:
    for command in ("grep -rn token ~/.coney", "grep -r x ~/.ssh", "rg token ~/.coney/identity", "git -C ~/.coney log"):
        assert protect.refusal(bash(command)), command


# --- ~/.ssh: private keys -------------------------------------------------------------------------------------------


@pytest.mark.parametrize("agent_type", EVERYONE)
def test_no_one_reads_a_private_key(agent_type: str) -> None:
    for command in (
        "cat ~/.ssh/id_ed25519",
        "cp ~/.ssh/id_ed25519 /tmp/k",
        "ls ~/.ssh",
        "ls ~/.ssh/",
        "cat ~/./.ssh/id_rsa",
        "type %USERPROFILE%\\.ssh\\id_ed25519",
        "cat $HOME/.ssh/coney-stamp",
        "cat ~/.s*/id_rsa",
    ):
        assert protect.refusal(bash(command, agent_type)), command
    assert protect.refusal(call("Read", {"file_path": "C:/Users/x/.ssh/id_ed25519"}, agent_type))
    assert protect.refusal(call("Grep", {"pattern": "BEGIN", "path": "/home/x/.ssh"}, agent_type))


def test_a_public_key_the_config_and_known_hosts_may_be_read() -> None:
    for command in (
        "cat ~/.ssh/coney-stamp.pub",
        "cat ~/.ssh/config",
        "cat ~/.ssh/known_hosts",
        "ssh-keygen -lf ~/.ssh/id_ed25519.pub",
    ):
        assert protect.refusal(bash(command)) is None, command
    assert protect.refusal(call("Read", {"file_path": "/home/x/.ssh/config"})) is None
    assert protect.refusal(call("Read", {"file_path": "/home/x/.ssh/id_ed25519.pub"})) is None
    assert protect.refusal(call("Read", {"file_path": "/home/x/my.ssh/id"})) is None


# --- MCP tools: every string of their input -------------------------------------------------------------------------


def test_an_mcp_tool_is_read_by_all_the_strings_of_its_input() -> None:
    script = {"code": 'print(new File(System.getProperty("user.home") + "/.coney/identity").list())'}
    assert protect.refusal(call("mcp__ghidra-mcp__run_script_inline", script))
    assert protect.refusal(call("mcp__ghidra-mcp__export_program", {"output_dir": "/home/x/.ssh/"}))
    assert protect.refusal(call("mcp__ghidra-mcp__export_program", {"nested": {"path": ["/home/x/.coney/a"]}}))
    assert protect.refusal(call("mcp__ghidra-mcp__decompile_function", {"address": "0x001490b8"})) is None
    assert protect.refusal(call("mcp__pcsx2__pcsx2_read_memory", {"address": 1, "length": 4})) is None


# --- stamp logs, signing, other processes' command lines ------------------------------------------------------------


@pytest.mark.parametrize("agent_type", EVERYONE)
def test_no_one_writes_a_stamp_log_by_hand(agent_type: str, tmp_path: Path) -> None:
    paths = [
        tmp_path / "scratch" / "task-1" / "stamps" / "0001.jsonl",
        tmp_path / "repo" / "research" / "units" / "task-1" / "stamps" / "0001.jsonl",
        tmp_path / "scratch" / "task-1" / "stamps" / "log.jsonl",
    ]
    for path in paths:
        for tool, field in (("Write", "file_path"), ("Edit", "file_path"), ("NotebookEdit", "notebook_path")):
            reason = protect.refusal(call(tool, {field: str(path)}, agent_type))
            assert reason is not None and "stage commands" in reason, (tool, path)
    for command in (
        "echo '{}' >> scratch/t/stamps/0001.jsonl",
        "tee research/units/t/stamps/0001.jsonl < x",
        "cp good.jsonl scratch/t/stamps/0001.jsonl",
        "sed -i s/a/b/ scratch/t/stamps/0001.jsonl",
        "rm scratch/t/stamps/0001.jsonl",
        "Set-Content scratch\\t\\stamps\\0001.jsonl 'x'",
    ):
        assert protect.refusal(bash(command, agent_type)), command


def test_reading_and_verifying_a_stamp_log_is_allowed(tmp_path: Path) -> None:
    log = str(tmp_path / "scratch" / "t" / "stamps" / "0001.jsonl")
    assert protect.refusal(call("Read", {"file_path": log}, "engineer")) is None
    assert protect.refusal(bash("uv run --project python coney-tools stamp verify scratch/t/stamps")) is None
    assert protect.refusal(bash("cat scratch/t/stamps/0001.jsonl | head")) is None
    # A file called stamps.md is not a stamp folder.
    assert protect.refusal(call("Write", {"file_path": str(tmp_path / "docs" / "guides" / "stamps.md")})) is None


@pytest.mark.parametrize("agent_type", EVERYONE)
def test_no_one_signs_or_loads_keys_or_reads_other_processes(agent_type: str) -> None:
    for command in (
        "ssh-keygen -Y sign -f k -n coney-stamp f",
        "ssh-keygen -U -Y sign -n coney-stamp -f k body",
        "ssh-add ~/key",
        "ssh-add -D",
        "cat /proc/1234/environ",
        "tr '\\0' '\\n' < /proc/self/cmdline",
        "wmic process get commandline",
        "Get-CimInstance Win32_Process | Select CommandLine",
    ):
        reason = protect.refusal(bash(command, agent_type))
        assert reason is not None and "not for agents" in reason, command
    # Verifying, listing keys and listing processes without their command lines are how a stamp or a hang is debugged.
    for command in (
        "ssh-keygen -Y verify -f signers -I a -n coney-stamp -s sig",
        "ssh-keygen -Y find-principals -f signers -s sig",
        "ssh-keygen -lf key.pub",
        "ssh-add -l",
        "ssh-add -L",
        "tasklist",
    ):
        assert protect.refusal(bash(command, agent_type)) is None, command


def test_normalise_folds_every_spelling_to_one() -> None:
    expected = "~/.coney/identity/a"
    for spelling in (
        "~/.coney/identity/a",
        "~\\.coney\\identity\\a",
        "$HOME/.coney/identity/a",
        "${HOME}/.coney/identity/a",
        "%USERPROFILE%\\.coney\\identity\\a",
        "$env:USERPROFILE\\.coney\\identity\\a",
        "~/./.coney/identity/a",
        "~/x/../.CONEY/Identity/A",
    ):
        assert protect.normalise(spelling) == expected, spelling


# --- searches and messages that only mention a pattern's words ------------------------------------------------------


@pytest.mark.parametrize("agent_type", EVERYONE)
def test_a_search_or_a_message_that_only_mentions_a_command_is_not_that_command(agent_type: str) -> None:
    for command in (
        'grep -rn "ssh-add" docs',
        "grep -rn ssh-add .claude",
        "grep -A 3 -B 1 'ssh-add' docs/guides/agents.md",
        "rg Win32_Process .claude",
        'rg -n "ssh-keygen -Y sign" docs',
        'git log -S "ssh-keygen -Y sign" --oneline',
        'git log --grep="ssh-add" --oneline',
        'git grep -n "/proc/self/environ" -- docs',
        'grep "/proc/self/environ" docs/guides/agents.md',
        'cat docs/a.md | grep -n "wmic process"',
        'Select-String -Pattern "Win32_Process" -Path docs/guides/agents.md',
        'pytest python/tests -k "ssh_add"',
        "uv run --project python pytest python/tests/test_hook_protect.py -k ssh_add -q",
        "python -m pytest -k 'Win32_Process or ssh_keygen'",
        'git commit -m "tools: Refuse ssh-add and ssh-keygen -Y sign for agents"',
        'git commit -am "tools: Refuse wmic process and Win32_Process"',
        'git commit -m"tools: Refuse ssh-add"',
        'git commit --message="tools: Refuse /proc/1/environ"',
        'git -C repo commit -m "tools: Refuse ssh-add; also wmic process | x"',
        'gh issue create --title "Refuse ssh-add" --body "ssh-keygen -Y sign is refused"',
    ):
        assert protect.refusal(bash(command, agent_type)) is None, command


def test_a_message_that_names_a_private_path_is_not_a_use_of_it() -> None:
    for command in (
        'git commit -m "tools: Refuse ~/.coney and ~/.ssh/id_ed25519 for every role"',
        'git commit -m"Hash the ~/.coney/identity files"',
        'gh pr create --title "Protect ~/.ssh" --body "no reads of ~/.ssh/id_rsa"',
    ):
        assert protect.refusal(bash(command)) is None, command


def test_the_command_itself_is_still_refused_when_text_surrounds_it() -> None:
    for command in (
        "grep -rn x docs; ssh-add ~/key",
        'echo "ssh-add" && ssh-add ~/key',
        'git commit -m "x" && ssh-keygen -Y sign -f k -n coney-stamp f',
        "git log -S x | cat /proc/1/environ",
        'bash -c "ssh-add ~/key"',
        'sh -c "ssh-keygen -Y sign -f k -n n f"',
        "pytest -k x; wmic process get commandline",
        'git commit -m "a" ; echo x > scratch/t/stamps/0001.jsonl',
        'git commit -m "ok" && cat ~/.coney/identity/a.json',
    ):
        assert protect.refusal(bash(command)), command


# --- Windows path aliases -------------------------------------------------------------------------------------------


def test_a_short_name_or_a_trailing_dot_names_the_same_folder() -> None:
    for path in (
        "C:/Users/Noah/SSH~1/id_ed25519",
        "C:/Users/Noah/ssh~1/id_ed25519",
        "C:/Users/Noah/.ssh./id_ed25519",
        "C:/Users/Noah/.ssh../id_ed25519",
        "C:/Users/Noah/.ssh /id_ed25519",
        "C:/Users/Noah/SSH~1",
        "C:/Users/Noah/CONEY~1/identity/a.json",
        "C:/Users/Noah/.coney./identity/a.json",
        "~/CONEY~1/identity",
        "~/.coney./identity",
    ):
        for tool, field in (("Read", "file_path"), ("Write", "file_path"), ("Glob", "path"), ("Grep", "path")):
            assert protect.refusal(call(tool, {field: path, "pattern": "*"})), (tool, path)
    for command in ("cat C:/Users/Noah/SSH~1/id_ed25519", "ls ~/CONEY~1/identity", "cat ~/.ssh./id_rsa"):
        assert protect.refusal(bash(command)), command


def test_names_that_only_look_like_a_short_name_are_fine() -> None:
    for path in (
        "C:/Users/Noah/SSHD~1/x",
        "C:/Users/Noah/CONSOLE~1/x",
        "C:/Repositories/CONEYT~1/x",
        "C:/Users/Noah/my.ssh/x",
        "C:/Users/Noah/ssh/x",
    ):
        assert protect.refusal(call("Read", {"file_path": path})) is None, path


def test_a_short_name_is_resolved_to_the_real_path_where_the_system_can(monkeypatch: pytest.MonkeyPatch) -> None:
    # A short name whose stem is not the folder's own (a name the system made for another reason) is looked up.
    monkeypatch.setattr(protect.os.path, "realpath", lambda path: "C:/Users/Noah/.ssh/id_ed25519")
    assert protect.refusal(call("Read", {"file_path": "C:/Users/Noah/KEYS~1/id_ed25519"}))
    assert protect.refusal(bash("cat C:/Users/Noah/KEYS~1/id_ed25519"))
    # A path without a short-name part is not looked up (the real path of a plain name would only slow the hook).
    assert protect.refusal(call("Read", {"file_path": "C:/Users/Noah/keys/id_ed25519"})) is None


def test_text_the_shell_would_run_is_not_skipped() -> None:
    for command in (
        'git commit -m "$(ssh-add ~/key)"',
        'git commit -m "`ssh-add ~/key`"',
        'grep "$(cat ~/.coney/identity/a.json)" docs',
        'pytest -k "$(ssh-add -D)"',
    ):
        assert protect.refusal(bash(command)), command


def test_an_unclosed_quote_does_not_hide_a_command() -> None:
    # The rest of the line belongs to the unclosed quote, and is still read as it was before.
    assert protect.refusal(bash('git commit -m "x; ssh-add ~/key'))
    assert protect.refusal(call("PowerShell", {"command": 'git commit -m "x"; ssh-add ~/key'}))
    # In PowerShell a backslash does not escape the quote: the message ends at the second quote and ssh-add runs.
    assert protect.refusal(call("PowerShell", {"command": 'git commit -m "a\\"; ssh-add ~/key; echo "b"'}))
