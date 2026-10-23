# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for .claude/hooks/roles.py, the role rules of the Claude Code hooks, against the committed `hooks.toml`.

The input is a synthetic hook payload (docs/guides/agents.md#hooks): `tool_name`, `tool_input`, `cwd`, `agent_type`
(every call) and `agent_id` (inside a subagent only). Checkouts are temporary folders holding a `.git` entry.
"""

import copy
from pathlib import Path
from typing import Any

import pytest
from hooks_support import HOOKS, load_hook_module

hooklib = load_hook_module("hooklib")
roles = load_hook_module("roles")

CONFIG = hooklib.load_config(HOOKS / "hooks.toml")
BACKSLASH = chr(92)
STRICT = {**copy.deepcopy(CONFIG), "require_role": True}


def call(
    tool: str,
    tool_input: dict[str, Any],
    agent_type: str = "",
    agent_id: str | None = None,
    cwd: str = "",
) -> dict[str, Any]:
    """A PreToolUse payload."""
    payload: dict[str, Any] = {"hook_event_name": "PreToolUse", "tool_name": tool, "tool_input": tool_input}
    if agent_type:
        payload["agent_type"] = agent_type
    if agent_id:
        payload["agent_id"] = agent_id
    if cwd:
        payload["cwd"] = cwd
    return payload


def bash(command: str, **kwargs: Any) -> dict[str, Any]:
    """A Bash call."""
    return call("Bash", {"command": command}, **kwargs)


@pytest.fixture
def checkout(tmp_path: Path) -> Path:
    """A folder that looks like a checkout (it holds `.git`)."""
    root = tmp_path / "repos" / "coney"
    (root / ".git").mkdir(parents=True)
    return root


def test_the_committed_config_leaves_require_role_off() -> None:
    # The coordinator session that exists today has no role; turning it on at merge would lock it out.
    assert CONFIG["require_role"] is False


def test_with_require_role_off_a_session_with_no_role_is_not_limited() -> None:
    assert roles.check(CONFIG, bash("git status")) is None
    assert roles.check(CONFIG, call("Write", {"file_path": "x"})) is None


@pytest.mark.parametrize("tool", ["Read", "Grep", "Glob"])
def test_with_require_role_on_a_session_with_no_role_may_read(tool: str) -> None:
    assert roles.check(STRICT, call(tool, {})) is None


@pytest.mark.parametrize(
    "tool", ["Bash", "PowerShell", "Edit", "Write", "Agent", "WebFetch", "mcp__pcsx2__pcsx2_status"]
)
def test_with_require_role_on_a_session_with_no_role_may_do_nothing_else(tool: str) -> None:
    reason = roles.check(STRICT, call(tool, {"command": "ls"}))
    assert reason is not None and reason.startswith("Blocked:") and "claude --agent coordinator" in reason


@pytest.mark.parametrize("agent_type", ["coordinator", "engineer", "tooling", "reviewer", "pr-reviewer", "Explore"])
def test_with_require_role_on_every_known_role_and_helper_passes_the_gate(agent_type: str) -> None:
    assert roles.missing_role_refusal(STRICT, call("Bash", {"command": "ls"}, agent_type=agent_type)) is None


def test_a_generic_agent_has_no_role() -> None:
    generic = call("Bash", {"command": "ls"}, agent_type="general-purpose", agent_id="a1")
    assert roles.missing_role_refusal(STRICT, generic)


def test_session_start_names_a_missing_role_in_both_modes() -> None:
    off = roles.session_notice(CONFIG, {"hook_event_name": "SessionStart"})
    on = roles.session_notice(STRICT, {"hook_event_name": "SessionStart"})
    assert off and "claude --agent coordinator" in off and "do not apply" in off
    assert on and "read-only" in on
    assert roles.session_notice(STRICT, {"agent_type": "coordinator"}) is None


# --- the coordinator ------------------------------------------------------------------------------------------------


@pytest.mark.parametrize(
    "relative", ["src/core/a.cpp", "tests/b.cpp", "python/src/x.py", "docs/research/page.md", "research/units/y.toml"]
)
def test_the_coordinator_may_not_write_engine_tests_python_or_research(checkout: Path, relative: str) -> None:
    for tool in ("Edit", "Write"):
        reason = roles.check(CONFIG, call(tool, {"file_path": str(checkout / relative)}, agent_type="coordinator"))
        assert reason is not None and "coordinator" in reason


def test_the_coordinator_may_write_guides_and_scratch(checkout: Path, tmp_path: Path) -> None:
    guide = call("Write", {"file_path": str(checkout / "docs/guides/a.md")}, "coordinator")
    scratch = call("Write", {"file_path": str(tmp_path / "scratch/x/TASK.md")}, "coordinator")
    # `research-notes.md` is not under `research/`.
    notes = call("Write", {"file_path": str(checkout / "research-notes.md")}, "coordinator")
    assert [roles.check(CONFIG, payload) for payload in (guide, scratch, notes)] == [None, None, None]


def test_the_coordinator_may_not_study_or_compare() -> None:
    assert roles.check(CONFIG, call("mcp__ghidra-mcp__decompile_function", {}, "coordinator"))
    assert roles.check(CONFIG, call("mcp__pcsx2__pcsx2_status", {}, "coordinator"))
    for command in (
        "uv run --project python coney-tools orig study 0x001490b8",
        "uv run --project python coney-tools original study 0x001490b8",
        "coney-tools original fails-first t1",
        "coney-tools orig compare 0x1",
        "cd x && coney-tools task check t1",
        "coney-tools task review t1",
    ):
        assert roles.check(CONFIG, bash(command, agent_type="coordinator")), command


def test_the_coordinator_may_write_about_those_commands_and_run_its_own() -> None:
    for command in (
        'git commit -m "document coney-tools task check"',
        "echo 'coney-tools orig study'",
        "uv run --project python coney-tools build status",
        "uv run --project python coney-tools pr open t1",
    ):
        assert roles.check(CONFIG, bash(command, agent_type="coordinator")) is None, command


def test_the_coordinator_may_not_commit_in_a_unit_worktree_but_may_in_the_main_checkout() -> None:
    unit = "C:/Repositories/Coney/worktrees/coney/some-branch"
    main = "C:/Repositories/Coney/repos/coney"
    assert roles.check(CONFIG, bash("git commit -m x", agent_type="coordinator", cwd=unit))
    assert roles.check(CONFIG, bash(f"git -C {unit} commit -m x", agent_type="coordinator", cwd=main))
    assert roles.check(CONFIG, bash(f"cd {unit} && git commit -m x", agent_type="coordinator", cwd=main))
    assert roles.check(CONFIG, bash("git commit -m x", agent_type="coordinator", cwd=main)) is None
    assert roles.check(CONFIG, bash("git log", agent_type="coordinator", cwd=unit)) is None
    # Only the folder the commit itself runs in counts, not one the command merely names.
    elsewhere = f"git -C {unit} log; git commit -m x"
    assert roles.check(CONFIG, bash(elsewhere, agent_type="coordinator", cwd=main)) is None
    assert (
        roles.check(CONFIG, bash(f"cd {unit} && git log; cd {main} && git commit -m x", agent_type="coordinator"))
        is None
    )
    assert roles.check(CONFIG, bash(f"cd {main}; git -C {unit} commit -m x", agent_type="coordinator", cwd=main))
    assert roles.check(CONFIG, bash(f"git -C {unit} commit-tree HEAD", agent_type="coordinator", cwd=main)) is None
    assert roles.check(CONFIG, bash("git commit -m x", agent_type="engineer", cwd=unit)) is None


@pytest.mark.parametrize(
    ("base", "text", "folder"),
    [
        # A drive path replaces the base, as on Windows: Linux and macOS would read `C:/x` as a name under it.
        ("C:/Repositories/unit", "C:/Repositories/main", "C:/Repositories/main"),
        ("C:/Repositories/unit", '"C:' + BACKSLASH + "Repositories" + BACKSLASH + 'main"', "C:/Repositories/main"),
        ("/home/me/unit", "C:/Repositories/main", "C:/Repositories/main"),
        ("C:/Repositories/unit", "../other", "C:/Repositories/other"),
        ("", "C:/Repositories/main", "C:/Repositories/main"),
        # Without a drive the system's own rules stay: a relative name joins, an absolute one replaces.
        ("/home/me/unit", "sub", "/home/me/unit/sub"),
        ("/home/me/unit", "/srv/main", "/srv/main"),
        ("/home/me/unit", "/c/x", "/c/x"),
        # A rooted POSIX path stays one even after a `cd` to a drive path.
        ("C:/Repositories/unit", "/srv/main", "/srv/main"),
    ],
)
def test_a_drive_path_is_absolute_on_every_system(base: str, text: str, folder: str) -> None:
    """The first case is the one the Linux and macOS CI hit when only Windows tested it."""
    assert hooklib._folder(base, text, windows=False).replace(BACKSLASH, "/") == folder


def test_git_bashs_drive_form_is_a_drive_path_on_windows_only() -> None:
    assert (
        hooklib._folder("C:/Repositories/unit", "/c/Repositories/main", windows=True).replace(BACKSLASH, "/")
        == "c:/Repositories/main"
    )
    assert hooklib._folder("/home/me", "/c/Repositories/main", windows=False) == "/c/Repositories/main"


def test_the_commit_folders_follow_a_windows_path_on_any_system() -> None:
    unit = "C:/Repositories/Coney/worktrees/coney/some-branch"
    main = "C:/Repositories/Coney/repos/coney"
    command = f"cd {unit} && git log; cd {main} && git commit -m x"
    assert hooklib.commit_folders(command, unit, windows=False) == [main.replace("/", "\\")]
    assert hooklib.commit_folders(f"git -C {main} commit -m x", unit, windows=False) == [main.replace("/", "\\")]
    assert hooklib.commit_folders("git commit -m x", unit, windows=False) == [unit.replace("/", "\\")]


# --- the engineer, the tooling agent and the reviewers --------------------------------------------------------------


@pytest.mark.parametrize("agent_type", ["engineer", "tooling", "reviewer", "orig-reviewer"])
def test_no_one_but_the_coordinator_opens_a_pull_request(agent_type: str) -> None:
    assert roles.check(CONFIG, bash("uv run --project python coney-tools pr open t1", agent_type=agent_type))


def test_a_main_session_started_as_an_engineer_gets_the_github_rules_a_subagent_gets() -> None:
    assert roles.check(CONFIG, bash("git push origin main", agent_type="engineer"))
    assert roles.check(CONFIG, bash("gh pr merge 5", agent_type="tooling"))
    assert roles.check(CONFIG, bash("git status", agent_type="engineer")) is None
    # Inside a subagent push_guard.py itself refuses; the role rules do not repeat it.
    assert roles.check(CONFIG, bash("git push origin main", agent_type="engineer", agent_id="a1")) is None
    # The coordinator pushes.
    assert roles.check(CONFIG, bash("git push origin main", agent_type="coordinator")) is None


def test_the_tooling_agent_may_edit_engine_sources_for_warnings(checkout: Path) -> None:
    edit = call("Edit", {"file_path": str(checkout / "src/core/a.cpp")}, "tooling", "a1")
    assert roles.check(CONFIG, edit) is None


@pytest.mark.parametrize("agent_type", ["reviewer", "research-reviewer", "orig-reviewer", "pr-reviewer"])
def test_a_reviewer_writes_only_its_verdict(agent_type: str, tmp_path: Path, checkout: Path) -> None:
    verdict = call("Write", {"file_path": str(tmp_path / "scratch/x/REVIEW.json")}, agent_type)
    report = call("Write", {"file_path": str(tmp_path / "scratch/x/orig-review.md")}, agent_type)
    assert roles.check(CONFIG, verdict) is None and roles.check(CONFIG, report) is None
    for path in (checkout / "src/a.cpp", tmp_path / "scratch/x/PR.md", checkout / "docs/a.md"):
        assert roles.check(CONFIG, call("Write", {"file_path": str(path)}, agent_type, "a1")), path
    assert roles.check(CONFIG, call("NotebookEdit", {"notebook_path": str(checkout / "n.ipynb")}, agent_type))


def test_a_reviewer_may_not_commit_or_stage() -> None:
    for command in ("git commit -m x", "git -C repo add -A", "git add ."):
        assert roles.check(CONFIG, bash(command, agent_type="reviewer", agent_id="a1")), command
    assert roles.check(CONFIG, bash("git diff main...HEAD", agent_type="reviewer", agent_id="a1")) is None


@pytest.mark.parametrize(
    "command",
    [
        "git --no-pager commit -m x",
        "git -P add -A",
        "git -c core.editor=true commit",
        "git -C repo -P add .",
        "git --git-dir=.git --work-tree=. add .",
        "/usr/bin/git.exe -P reset --hard",
        "git checkout -- src/a.cpp",
        "git checkout main",
        "git -P checkout -b x",
        "git restore src/a.cpp",
        "git apply fix.patch",
        "git clean -fd",
        "git -P clean -n -d",
        "git revert HEAD",
        "git worktree remove ../x",
        "git -C repo worktree remove --force ../x",
        "git switch main",
        "git am x.patch",
        "ls && git -P add x",
    ],
)
def test_a_reviewer_may_not_change_or_discard_the_tree(command: str) -> None:
    assert roles.check(CONFIG, bash(command, agent_type="reviewer", agent_id="a1")), command


@pytest.mark.parametrize(
    "command",
    [
        "git diff main...HEAD",
        "git --no-pager diff --stat",
        "git -P log --oneline -5",
        "git -C repo show HEAD:README.md",
        "git status --short",
        "git worktree list",
        "git branch --show-current",
        "git stash list",
        "git log --grep=commit",
        "git config --get commit.gpgsign",
        "git diff --name-only | grep -c add",
        "git blame -L1,5 file",
    ],
)
def test_a_reviewer_may_read_the_history(command: str) -> None:
    assert roles.check(CONFIG, bash(command, agent_type="reviewer", agent_id="a1")) is None, command


def test_a_helper_type_or_no_type_has_no_role_rules() -> None:
    assert roles.role_refusal(CONFIG, bash("git commit -m x", agent_type="Explore")) is None
    assert roles.role_refusal(CONFIG, bash("pr open")) is None


# --- the handover notes, the settings of the wrong shape ------------------------------------------------------------


def test_a_reviewer_may_write_its_handover_note(tmp_path: Path) -> None:
    note = call("Write", {"file_path": str(tmp_path / "scratch/x/HANDOVER.md")}, "orig-reviewer", "a1")
    assert roles.check(CONFIG, note) is None


def test_settings_of_the_wrong_shape_are_ignored_not_fatal() -> None:
    broken = {
        "roles": {"engineer": "x", "reviewer": {"aliases": "pr-reviewer", "write_only": 5, "deny_commands": ["(", 3]}},
        "helpers": "Explore",
        "readonly_tools": "Read",
        "require_role": True,
        "protect": {"key_folders": 4, "stamp_folders": None, "commands": "x"},
    }
    assert roles.check(broken, bash("ls", agent_type="reviewer")) is None
    assert roles.check(broken, bash("ls")) is not None  # no role, require_role on: read-only
    assert roles.has_role(broken, "Explore") is False
    assert roles.role_rules(broken, "pr-reviewer") is None  # a string is not a list of aliases
