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
#: The committed settings with `require_role` off, the state before the maintainer's sessions had a role.
LENIENT = {**copy.deepcopy(CONFIG), "require_role": False}


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


def test_the_committed_config_turns_require_role_on() -> None:
    # The maintainer's sessions start through `coney-agent`, so a session with no role may only read.
    assert CONFIG["require_role"] is True


@pytest.mark.parametrize("agent_type", ["coordinator", "engineer", "tooling", "reviewer"])
def test_with_the_committed_config_a_session_started_as_a_role_is_not_locked_out(agent_type: str) -> None:
    # What `coney-agent <role>` starts: `claude --agent <role>`, so every call carries that `agent_type`.
    for tool, tool_input in (("Bash", {"command": "git status"}), ("Agent", {"subagent_type": "Explore"})):
        if (agent_type, tool) == ("reviewer", "Agent"):
            continue  # a reviewer starts no agent, a rule of its role and not of the gate
        assert roles.check(CONFIG, call(tool, tool_input, agent_type=agent_type)) is None, (agent_type, tool)


def test_with_the_committed_config_a_session_with_no_role_is_read_only() -> None:
    assert roles.check(CONFIG, call("Read", {"file_path": "x"})) is None
    for payload in (bash("git status"), call("Write", {"file_path": "x"}), call("Agent", {"subagent_type": "tooling"})):
        reason = roles.check(CONFIG, payload)
        assert reason is not None and "`coney-agent`" in reason, payload


def test_with_require_role_off_a_session_with_no_role_is_not_limited() -> None:
    assert roles.check(LENIENT, bash("git status")) is None
    assert roles.check(LENIENT, call("Write", {"file_path": "x"})) is None


@pytest.mark.parametrize("tool", ["Read", "Grep", "Glob"])
def test_with_require_role_on_a_session_with_no_role_may_read(tool: str) -> None:
    assert roles.check(STRICT, call(tool, {})) is None


@pytest.mark.parametrize(
    "tool", ["Bash", "PowerShell", "Edit", "Write", "Agent", "WebFetch", "mcp__pcsx2__pcsx2_status"]
)
def test_with_require_role_on_a_session_with_no_role_may_do_nothing_else(tool: str) -> None:
    reason = roles.check(STRICT, call(tool, {"command": "ls"}))
    assert reason is not None and reason.startswith("Blocked:") and "`coney-agent`" in reason


@pytest.mark.parametrize("agent_type", ["coordinator", "engineer", "tooling", "reviewer", "Explore"])
def test_with_require_role_on_every_known_role_and_helper_passes_the_gate(agent_type: str) -> None:
    assert roles.missing_role_refusal(STRICT, call("Bash", {"command": "ls"}, agent_type=agent_type)) is None


def test_a_generic_agent_has_no_role() -> None:
    generic = call("Bash", {"command": "ls"}, agent_type="general-purpose", agent_id="a1")
    assert roles.missing_role_refusal(STRICT, generic)


def test_session_start_names_a_missing_role_in_both_modes() -> None:
    off = roles.session_notice(LENIENT, {"hook_event_name": "SessionStart"})
    on = roles.session_notice(STRICT, {"hook_event_name": "SessionStart"})
    assert off and "`coney-agent`" in off and "do not apply" in off
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
        "coney-tools orig fails-first t1",
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
        "uv run --project python coney-tools pr hold 812 --list",
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


@pytest.mark.parametrize("agent_type", ["engineer", "tooling", "reviewer"])
def test_no_one_but_the_coordinator_opens_a_pull_request(agent_type: str) -> None:
    assert roles.check(CONFIG, bash("uv run --project python coney-tools pr open t1", agent_type=agent_type))


@pytest.mark.parametrize("agent_type", ["engineer", "tooling", "reviewer"])
def test_no_one_but_the_coordinator_holds_a_pull_requests_problems(agent_type: str) -> None:
    # a hold hides a pull request's problems from the coordinator's wake and stop guard, so it is the coordinator's call
    command = "uv run --project python coney-tools pr hold 812 --who engineer-2"
    assert roles.check(CONFIG, bash(command, agent_type=agent_type))
    assert roles.check(CONFIG, bash(command, agent_type="coordinator")) is None


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


@pytest.mark.parametrize("agent_type", ["reviewer"])
def test_a_reviewer_writes_only_its_verdict(agent_type: str, tmp_path: Path, checkout: Path) -> None:
    verdict = call("Write", {"file_path": str(tmp_path / "scratch/x/REVIEW.json")}, agent_type)
    report = call("Write", {"file_path": str(tmp_path / "scratch/x/orig-review.md")}, agent_type)
    assert roles.check(CONFIG, verdict) is None and roles.check(CONFIG, report) is None
    for path in (checkout / "src/a.cpp", tmp_path / "scratch/x/PR.md", checkout / "docs/a.md"):
        assert roles.check(CONFIG, call("Write", {"file_path": str(path)}, agent_type, "a1")), path
    assert roles.check(CONFIG, call("NotebookEdit", {"notebook_path": str(checkout / "n.ipynb")}, agent_type))


@pytest.mark.parametrize(
    "name", ["CODE-REVIEW.md", "CODE-REVIEW-2.md", "code-review-fix-round.md", "CODE-REVIEW-orig.MD"]
)
def test_a_reviewer_may_write_a_code_review_report(name: str, tmp_path: Path) -> None:
    # A review outside `task review` (a fix round, a conflict resolution) has no REVIEW.md to write; the report is named
    # CODE-REVIEW*.md so the coordinator need not copy it out of the reply.
    report = call("Write", {"file_path": str(tmp_path / "scratch/x" / name)}, "reviewer", "a1")
    assert roles.check(CONFIG, report) is None, name


@pytest.mark.parametrize(
    "name",
    ["CODE-REVIEW", "CODE-REVIEW.json", "CODE-REVIEW.py", "NOT-CODE-REVIEW.md", "CODE-REVIEWER.sh", "REVIEW-NOTES.md"],
)
def test_a_code_review_name_does_not_open_other_files(name: str, tmp_path: Path) -> None:
    report = call("Write", {"file_path": str(tmp_path / "scratch/x" / name)}, "reviewer", "a1")
    assert roles.check(CONFIG, report), name


@pytest.mark.parametrize("name", ["REVIEW.json", "REVIEW.md", "CODE-REVIEW.md", "FINDINGS.tsv", "orig-review.md"])
def test_a_reviewer_writes_its_files_in_no_checkout(name: str, checkout: Path, tmp_path: Path) -> None:
    # #823: a report inside a worktree (or the main checkout) is a file an engineer could commit by mistake.
    for folder in (checkout, checkout / "src" / "core", checkout / "docs"):
        assert roles.check(CONFIG, call("Write", {"file_path": str(folder / name)}, "reviewer", "a1")), folder
    worktree = tmp_path / "worktrees" / "coney" / "x"
    worktree.mkdir(parents=True)
    (worktree / ".git").write_text("gitdir: ../../repos/coney/.git/worktrees/x\n", encoding="utf-8")
    reason = roles.check(CONFIG, call("Write", {"file_path": str(worktree / name)}, "reviewer", "a1"))
    assert reason and "scratch" in reason
    assert roles.check(CONFIG, call("Write", {"file_path": str(tmp_path / "scratch" / "x" / name)}, "reviewer")) is None


@pytest.mark.parametrize("name", ["CODE-REVIEW:x.md", "CODE-REVIEW.md:stream", "REVIEW.md:x", "code-review-a:b.md"])
def test_a_reviewer_may_not_write_a_name_with_a_colon(name: str, tmp_path: Path) -> None:
    # `*` matches `:`, and on Windows `name:stream` writes an alternate data stream of another file.
    report = call("Write", {"file_path": str(tmp_path / "scratch" / "x") + "/" + name}, "reviewer", "a1")
    assert roles.check(CONFIG, report), name


def test_a_pattern_in_write_only_matches_the_file_name_not_the_folder(tmp_path: Path, checkout: Path) -> None:
    # Only the last part counts, so a CODE-REVIEW folder does not open the files in it.
    inside = call("Write", {"file_path": str(checkout / "docs/CODE-REVIEW.md/a.cpp")}, "reviewer", "a1")
    assert roles.check(CONFIG, inside)


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
        # `merge` itself still mutates, and so do the other merge-* verbs that write the tree or a file.
        "git merge main",
        "git merge --no-ff feature",
        "git -C repo merge --abort",
        "git merge-file a b c",
        "git merge-index -o git-merge-one-file -a",
        "git merge-one-file a b c",
        "git merge-base a b && git merge main",
        "git merge-tree --write-tree a b && git merge a",
    ],
)
def test_a_reviewer_may_not_change_or_discard_the_tree(command: str) -> None:
    assert roles.check(CONFIG, bash(command, agent_type="reviewer", agent_id="a1")), command


@pytest.mark.parametrize(
    "command",
    [
        # #823: the verbs that change a ref, the index or a worktree and passed before.
        "git pull",
        "git -C repo pull --rebase origin main",
        "git stash",
        "git stash push -m x",
        "git -P stash pop",
        "git stash drop",
        "git read-tree -u -m HEAD",
        "git read-tree HEAD",
        "git update-ref refs/heads/x HEAD",
        "git update-index --assume-unchanged a",
        "git branch -f main HEAD",
        "git branch x",
        "git branch -D x",
        "git -P branch -m a b",
        "git branch --set-upstream-to=origin/main",
        "git branch -a -d x",
        "git branch --list x -D x",
        "git tag v1",
        "git tag -d v1",
        "git tag -a v1 -m 'x'",
        "git tag -f v1 HEAD",
        "git mergetool",
        "git -C repo mergetool --tool=x",
        # The other verbs of the same kind.
        "git worktree add ../x",
        "git worktree move a b",
        "git worktree prune",
        "git bisect start",
        "git fetch origin",
        "git stage a",
        "git checkout-index -a -f",
        "git sparse-checkout set src",
        "git notes add -m x",
        "git replace a b",
        "git reflog expire --all",
        "git gc",
        "git pack-refs --all",
        "git filter-branch HEAD",
        "git remote prune origin",
        "git submodule update",
        "git status && git stash",
        # #823 review: the ref writers that were still missing.
        "git symbolic-ref HEAD refs/heads/x",
        "git -C repo symbolic-ref HEAD refs/heads/x",
        "git symbolic-ref -m reason HEAD refs/heads/x",
        "git symbolic-ref --short HEAD refs/heads/x",
        "git symbolic-ref -d refs/remotes/origin/HEAD",
        "git symbolic-ref --delete HEAD",
        "git remote add up https://example.invalid/x.git",
        "git remote remove origin",
        "git remote rm origin",
        "git remote rename origin up",
        "git remote set-url origin https://example.invalid/x.git",
        "git remote set-head origin -a",
        "git remote set-branches origin main",
        "git remote update",
        "git -P remote add up x",
        "git prune",
        "git prune --expire now",
        "git repack -ad",
        "git maintenance run",
        "git maintenance run --task=gc",
        "git maintenance start",
        "git -C repo maintenance register",
    ],
)
def test_a_reviewer_may_not_change_a_ref_the_index_or_a_worktree(command: str) -> None:
    assert roles.check(CONFIG, bash(command, agent_type="reviewer", agent_id="a1")), command


@pytest.mark.parametrize(
    "command",
    [
        "git stash list",
        "git stash show -p stash@{0}",
        "git branch",
        "git branch -a",
        "git branch -vv",
        "git branch -avr",
        "git branch --contains HEAD",
        "git -P branch -r --merged origin/main",
        "git branch --list 'tools-*'",
        "git branch --format=%(refname) --sort=-committerdate",
        "git branch -a 2>/dev/null | grep tools",
        "git tag",
        "git tag -l",
        "git tag --list v1*",
        "git tag -n --contains HEAD",
        "git tag --points-at HEAD; git status",
        "git reflog",
        "git reflog show HEAD",
        "git notes list",
        "git remote -v",
        "git remote",
        "git remote show origin",
        "git remote get-url origin",
        "git symbolic-ref HEAD",
        "git symbolic-ref --short HEAD",
        "git symbolic-ref -q HEAD 2>/dev/null",
        "git -C repo symbolic-ref refs/remotes/origin/HEAD | cat",
        "git worktree list --porcelain",
    ],
)
def test_a_reviewer_may_still_list_refs_and_stashes(command: str) -> None:
    assert roles.check(CONFIG, bash(command, agent_type="reviewer", agent_id="a1")) is None, command


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
        # Read-only: the base of a stacked or rebased branch, and a merge computed without touching the tree.
        "git merge-base main HEAD",
        "git merge-base --is-ancestor a b",
        "git -C repo merge-base --fork-point origin/main branch",
        "git --no-pager merge-base origin/main HEAD",
        "git merge-tree --write-tree main HEAD",
        "git -P merge-tree --write-tree --name-only a b",
        "git merge-base a b && git merge-tree --write-tree a b",
    ],
)
def test_a_reviewer_may_read_the_history(command: str) -> None:
    assert roles.check(CONFIG, bash(command, agent_type="reviewer", agent_id="a1")) is None, command


def test_a_helper_type_or_no_type_has_no_role_rules() -> None:
    assert roles.role_refusal(CONFIG, bash("git commit -m x", agent_type="Explore")) is None
    assert roles.role_refusal(CONFIG, bash("pr open")) is None


# --- the handover notes, the settings of the wrong shape ------------------------------------------------------------


def test_a_reviewer_may_write_its_handover_note(tmp_path: Path) -> None:
    note = call("Write", {"file_path": str(tmp_path / "scratch/x/HANDOVER.md")}, "reviewer", "a1")
    assert roles.check(CONFIG, note) is None


def test_settings_of_the_wrong_shape_are_ignored_not_fatal() -> None:
    broken = {
        "roles": {"engineer": "x", "reviewer": {"aliases": "old-name", "write_only": 5, "deny_commands": ["(", 3]}},
        "helpers": "Explore",
        "readonly_tools": "Read",
        "require_role": True,
        "protect": {"key_folders": 4, "stamp_folders": None, "commands": "x"},
    }
    assert roles.check(broken, bash("ls", agent_type="reviewer")) is None
    assert roles.check(broken, bash("ls")) is not None  # no role, require_role on: read-only
    assert roles.has_role(broken, "Explore") is False
    assert roles.role_rules(broken, "old-name") is None  # a string is not a list of aliases


# --- a reviewer starts no agent ------------------------------------------------------------------------------------


@pytest.mark.parametrize("kind", ["Explore", "Plan", "engineer", "tooling", "reviewer", "fork", "general-purpose", ""])
def test_a_reviewer_may_not_start_any_agent(kind: str) -> None:
    # A helper has Bash and no role rules, so it could write a stamp or a file for the reviewer.
    tool_input = {"subagent_type": kind, "prompt": "write"} if kind else {"prompt": "write"}
    for tool in ("Agent", "Task"):
        for agent_id in (None, "a1"):
            reason = roles.check(CONFIG, call(tool, tool_input, "reviewer", agent_id))
            assert reason is not None and "start no agent" in reason


def test_other_roles_may_still_start_agents() -> None:
    for role in ("engineer", "tooling", "coordinator"):
        assert roles.check(CONFIG, call("Agent", {"subagent_type": "engineer"}, role, "a1")) is None


@pytest.mark.parametrize(
    "command",
    [
        'claude -p --agent engineer "run coney-tools original compare T1"',
        "claude",
        "claude.exe --agent engineer",
        ".venv/Scripts/claude -p x",
        r"C:\Users\x\.local\bin\claude.exe -p x",
        "/usr/local/bin/claude --resume",
        "uvx claude -p x",
        "npx @anthropic-ai/claude-code -p x",
        "uv run --project python claude -p x",
        "ls && claude -p x",
        "ls; claude -p x",
        "echo x | claude -p",
        "out=$(claude -p x)",
        "out=`claude -p x`",
        "CLAUDE_X=1 claude -p x",
        "env A=1 claude -p x",
        "cmd /c claude -p x",
        "& claude -p x",
        "cd w\nclaude -p x",
        "coney-agent engineer",
        "uv run --project python coney-agent --role engineer",
        "ls && .venv/Scripts/coney-agent.exe engineer",
        "python -m coney_tools.session_launcher engineer",
        "coney-tools task review T1 --run",
        "uv run --project python coney-tools task review T1 --run",
        "uv run --project python coney-tools task review T1 --base origin/main --run --model opus",
        "uvx --from ./python coney-tools task review T1 --ru",
        ".venv/Scripts/coney-tools.exe task review --run",
        "ls && coney-tools repo review --run",
        "uv run --project python coney-tools repo review ../scratch/x/PR.md",
        "coney-tools repo review",
        "ls; Coney-Tools.exe repo review PR.md --worktree .",
        "uv run --project python coney-tools legacy pr-review ../scratch/x/PR.md --worktree .",
        "coney-tools legacy pr-review",
        "x=$(coney-tools task review T1 --run)",
        "python -m coney_tools.cli task review T1 --run",
        "python python/src/coney_tools/cli.py task review T1 --run",
        "coney-tools task review T1 --run>log",
        # Letter case, which Windows ignores.
        "Claude -p x",
        "CLAUDE -p x",
        "Claude.exe -p x",
        "claude.CMD -p x",
        # Launchers with options, and PowerShell's.
        "Start-Process claude -ArgumentList x",
        "saps -FilePath claude",
        "npx -y @anthropic-ai/claude-code -p x",
        "npm exec @anthropic-ai/claude-code",
        "pnpm dlx @anthropic-ai/claude-code",
        "uvx --from x claude -p x",
        "sudo -u x claude",
        "env -i claude",
        "time -p claude",
        "xargs -n1 claude < list",
        "cmd.exe /C claude -p x",
        "cmd /s /c claude",
        "bash -lc claude",
        "sh -xc claude",
        "find . -name x -exec claude {} ;",
        # After a shell keyword or a group.
        "if true; then claude; fi",
        "for i in 1; do claude; done",
        "{ claude; }",
        "! claude -p x",
        "claude>log",
        # The session launcher by its file.
        "python python/src/coney_tools/session_launcher.py engineer",
        r"python python\src\coney_tools\session_launcher.py engineer",
    ],
)
def test_a_reviewer_may_not_start_a_session_from_the_shell(command: str) -> None:
    # A session the reviewer starts is a main session the hooks vouch for, under an id of its own: it could write a
    # `compared` stamp for the reviewer, as an Agent helper could.
    for agent_id in (None, "a1"):
        for tool in ("Bash", "PowerShell"):
            reason = roles.check(CONFIG, call(tool, {"command": command}, "reviewer", agent_id))
            assert reason is not None and "run that command" in reason, command


@pytest.mark.parametrize(
    "command",
    [
        "uv run --project python coney-tools task review T1",
        "uv run --project python coney-tools task review T1 --record",
        "uv run --project python coney-tools repo review-stamp ../scratch/x/PR.md",
        "cat CLAUDE.md",
        "which claude",
        "cat .claude/agents/reviewer.md",
        "grep -rn claude docs/guides",
        "ls ~/.claude/projects",
        "git log --grep claude",
        "rg coney-agent python/src",
        "coney-tools task review T1 --record && grep -c --run log.txt",
        'echo "claude -p x"',
    ],
)
def test_a_reviewer_may_still_run_its_checks_and_name_claude(command: str) -> None:
    assert roles.check(CONFIG, bash(command, agent_type="reviewer", agent_id="a1")) is None, command


def test_other_roles_may_start_a_session_from_the_shell() -> None:
    for role in ("engineer", "tooling"):
        assert (
            roles.check(CONFIG, bash("uv run --project python coney-tools task review T1 --run", agent_type=role))
            is None
        )
        assert roles.check(CONFIG, bash("claude -p x", agent_type=role, agent_id="a1")) is None


def test_a_named_allow_list_lets_only_those_types_through_in_any_case() -> None:
    # No committed role names types now; the next one that does runs this branch.
    config = {"roles": {"x": {"allow_agent_types": ["Explore", "Plan"], "hint": "Ask."}}}
    for kind in ("Explore", "explore", " EXPLORE ", "plan"):
        assert roles.check(config, call("Agent", {"subagent_type": kind}, "x", "a1")) is None, kind
    for kind, shown in (("tooling", "a tooling agent"), ("", "an agent with no type")):
        tool_input = {"subagent_type": kind} if kind else {}
        reason = roles.check(config, call("Task", tool_input, "x"))
        assert reason == f"Blocked: as x you may not start {shown}: you may start only Explore, Plan. Ask. " + (
            "(docs/guides/agents.md#roles)\n"
        )
