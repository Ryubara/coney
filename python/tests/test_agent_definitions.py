# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests that every agent definition in .claude/agents/ is one Claude Code will load.

Claude Code skips an agent file whose frontmatter is not valid YAML, silently: the role is simply missing from the
session. On 2026-10-22 `tooling.md` had an unquoted colon in its description and never loaded. These tests read each
file the way Claude Code does (a YAML block between the first two `---` lines) and require the fields it needs.
"""

import subprocess
import tomllib
from pathlib import Path

import pytest
import yaml

from coney_tools.conventions import load_conventions

REPO = Path(__file__).resolve().parents[2]
AGENTS = REPO / ".claude" / "agents"


def _definitions() -> list[Path]:
    """Every agent definition file in the repository."""
    return sorted(AGENTS.glob("*.md"))


def _frontmatter(path: Path) -> object:
    """The parsed YAML block at the top of an agent file."""
    text = path.read_text(encoding="utf-8")
    assert text.startswith("---"), f"{path.name}: no frontmatter"
    return yaml.safe_load(text.split("---", 2)[1])


def test_there_are_agent_definitions() -> None:
    assert _definitions()


@pytest.mark.parametrize("path", _definitions(), ids=lambda p: p.name)
def test_agent_frontmatter_loads_with_a_name_and_description(path: Path) -> None:
    data = _frontmatter(path)
    assert isinstance(data, dict), f"{path.name}: frontmatter is not a mapping"
    assert data.get("name") == path.stem, f"{path.name}: name must match the file name"
    assert isinstance(data.get("description"), str) and data["description"], f"{path.name}: no description"


def test_the_coordinator_is_defined_for_the_main_session() -> None:
    data = _frontmatter(AGENTS / "coordinator.md")
    assert isinstance(data, dict) and data["name"] == "coordinator"
    # A main session needs every tool (it starts agents, runs the tools, edits guides); the hooks hold it to its role.
    assert "tools" not in data and "disallowedTools" not in data
    # No model: the person picks the one the session runs on.
    assert "model" not in data


def test_every_role_the_hooks_name_has_an_agent_definition() -> None:
    config = tomllib.loads((REPO / ".claude" / "hooks" / "hooks.toml").read_text(encoding="utf-8"))
    defined = {path.stem for path in _definitions()}
    named = set(config["roles"]) | {alias for rules in config["roles"].values() for alias in rules.get("aliases", [])}
    # The role of another agent tool's sessions (`[harnesses] default_role`) is no Claude Code agent.
    named -= {config["harnesses"]["default_role"]}
    assert named <= defined, f"hooks.toml names roles with no .claude/agents file: {sorted(named - defined)}"
    # And the other way: a defined role has rules, so a new role file is not forgotten by the hooks.
    assert defined <= named | set(config["helpers"]), f"roles the hooks do not know: {sorted(defined - named)}"


def test_the_committed_hook_config_is_the_safe_rollout() -> None:
    config = tomllib.loads((REPO / ".claude" / "hooks" / "hooks.toml").read_text(encoding="utf-8"))
    assert config["require_role"] is True, "sessions start through `coney-agent`; one with no role may only read"
    # The PR guard's switch is not in this file: it is `pull_requests.open_only` of the conventions, which the
    # maintainer turned on on 2026-10-27 (issue #865).
    assert load_conventions(REPO).pull_requests.open_only is True
    assert config["identity"]["powershell_modes"] == ["bypassPermissions"]


# --- the one reviewer ------------------------------------------------------------------------------------------------

#: The review types that were merged into `reviewer` (the earlier code, research, orig and PR-description reviewers).
MERGED = ("research-reviewer", "orig-reviewer", "pr-reviewer")
#: Ghidra tools that change the shared project: a reviewer reads only.
GHIDRA_WRITES = ("rename", "set_", "create_", "delete_", "apply_", "add_", "modify_", "batch_set", "import_")


def _reviewer() -> tuple[dict[str, object], str]:
    """The reviewer's front matter and its text."""
    path = AGENTS / "reviewer.md"
    data = _frontmatter(path)
    assert isinstance(data, dict)
    return data, path.read_text(encoding="utf-8")


def test_the_merged_review_types_are_gone() -> None:
    assert {path.stem for path in _definitions()} == {"coordinator", "engineer", "reviewer", "tooling"}


@pytest.mark.reads("docs_only")
def test_no_guide_hook_tool_test_or_definition_names_a_merged_type() -> None:
    # The files git knows (tracked or not yet added, never ignored: node_modules and caches), in every folder that can
    # name a role: the guides, the hooks, the tools, the tests and the mod with its fixtures.
    roots = [".claude", "docs", ".github", "python", "tools", "AGENTS.md", "CONTRIBUTING.md"]
    listing = subprocess.run(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard", "--", *roots],
        cwd=REPO,
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    # This file names the types it forbids.
    here = Path(__file__).resolve().relative_to(REPO).as_posix()
    found = []
    for name in sorted(set(listing.split(chr(0))) - {"", here}):
        path = REPO / name
        if path.suffix in {".md", ".py", ".toml", ".yml", ".yaml", ".json", ".cjs", ".ts", ".tsx"} and path.is_file():
            text = path.read_text(encoding="utf-8", errors="replace")
            found += [f"{name}: {merged}" for merged in MERGED if merged in text]
    assert found == []


def test_the_reviewer_reads_and_writes_but_never_edits() -> None:
    data, _ = _reviewer()
    tools = [tool.strip() for tool in str(data["tools"]).split(",")]
    assert {"Read", "Glob", "Grep", "Bash", "Write"} <= set(tools)
    assert not {"Edit", "NotebookEdit"} & set(tools)
    ghidra = [t for t in tools if t.startswith("mcp__ghidra-mcp__")]
    assert ghidra and not [t for t in ghidra if any(w in t.removeprefix("mcp__ghidra-mcp__") for w in GHIDRA_WRITES)]
    assert "mcp__ghidra-mcp__decompile_function" in tools and "mcp__pcsx2__pcsx2_read_memory" in tools
    assert not [
        t for t in tools if t.startswith("mcp__pcsx2__") and any(w in t for w in ("write", "load_state", "save_state"))
    ]


def test_the_reviewer_starts_no_agent_and_the_role_rules_refuse_the_call() -> None:
    config = tomllib.loads((REPO / ".claude" / "hooks" / "hooks.toml").read_text(encoding="utf-8"))
    assert config["roles"]["reviewer"]["allow_agent_types"] == []
    assert "allow_agent_types" not in config["roles"]["engineer"]
    assert "Agent" not in [tool.strip() for tool in str(_reviewer()[0]["tools"]).split(",")]


def test_the_reviewer_runs_on_opus_for_the_reverse_engineering_it_covers() -> None:
    data, _ = _reviewer()
    assert data["model"] == "opus"


@pytest.mark.reads("docs_only")
def test_the_reviewer_sends_each_kind_of_review_to_its_checklist() -> None:
    _, text = _reviewer()
    for page in ("index", "task", "code", "docs", "research", "orig", "text", "commits"):
        assert f"docs/guides/review/{page}.md" in text, page
        assert (REPO / "docs" / "guides" / "review" / f"{page}.md").is_file(), page
    # The checks the four earlier reviewers made, each still named: Ghidra fidelity, the pictures and claims of a
    # description, the research page spot checks, and the report format.
    for phrase in ("task review", "Ghidra", "picture", "finding format", "never edit", "--record"):
        assert phrase in text, phrase


def test_the_reviewer_gets_exactly_the_read_only_xemu_tools() -> None:
    # The xemu server's own list of the tools that change nothing in the game is the one source.
    from coney_tools.xemu_mcp import READ_ONLY_TOOLS

    data, _ = _reviewer()
    tools = {tool.strip() for tool in str(data["tools"]).split(",")}
    assert {t.removeprefix("mcp__xemu__") for t in tools if t.startswith("mcp__xemu__")} == set(READ_ONLY_TOOLS)
