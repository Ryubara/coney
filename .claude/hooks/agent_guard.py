# SPDX-License-Identifier: GPL-3.0-or-later
"""Claude Code PreToolUse hook: refuse an Agent call that breaks how Coney starts agents.

Two rules, both for the Agent tool, registered in `.claude/settings.json`:

1. **No `isolation: "worktree"`.** That option makes Claude Code create the agent's worktree itself, under the main
   checkout's `.claude/worktrees/` with an automatic branch name. Coney's agents work in
   `../../worktrees/coney/<branch>/` on a branch the brief names: the Defender exclusion that keeps builds from
   starving the CPU covers only that folder, and the workspace tools (`coney-tools workspace clean`, the
   `.coney-busy` marker, the release queue) look only there (docs/guides/agents.md, "Where an agent works"). The
   agent creates its own worktree with `git worktree add`, as its brief says.
2. **A Coney role, never the generic agent.** A call with no `subagent_type` (which Claude Code runs as
   "general-purpose"), or with "general-purpose" or "claude", carries none of the role instructions the repository
   ships in `.claude/agents/`. The allowed types are the roles found in that folder at run time (so a new role
   works without editing this file), the read-only helpers "Explore", "Plan" and "claude-code-guide", and "fork"
   (docs/guides/agents.md, "Which role to start").

It is standard-library only. Exit code 2 blocks the call and shows stderr to the agent; every other outcome (also
input it cannot read) exits 0.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

AGENTS_DIR = Path(__file__).resolve().parent.parent / "agents"

# Built-in helpers that read and never write, and the fork of the caller; allowed beside the Coney roles.
BUILT_IN = ("Explore", "Plan", "fork", "claude-code-guide")

# Roles of the main session (started by the `coney-agent` session launcher), never a subagent.
MAIN_SESSION_ONLY = ("coordinator",)

# Types that mean "no role": a missing type runs as general-purpose.
GENERIC = ("", "general-purpose", "claude")

WORKTREE_MESSAGE = (
    'Blocked: do not start an agent with isolation: "worktree". It puts the worktree under .claude/worktrees/ with '
    "an automatic branch name, outside ../../worktrees/coney/<branch>/ (the layout every tool and the Defender "
    "exclusion assume). Start the agent without `isolation` and tell it, in its brief, to create its own worktree "
    "with `git worktree add ../../worktrees/coney/<branch> -b <branch> origin/main` "
    "(docs/guides/agents.md, 'Where an agent works').\n"
)

# Which role to pick, shown with the refusal; roles that do not exist in the folder are left out.
ADVICE = (
    ("engineer", "anything that writes code, tests, tools, research pages or docs, or opens Ghidra or PCSX2"),
    ("reviewer", "a review of code, tests, tools, docs or commits"),
    ("research-reviewer", "a review of research pages and reverse-engineering work"),
    ("orig-reviewer", "the check of @orig functions against the original"),
    ("pr-reviewer", "a pull request description (PR.md)"),
)


def role_name(path: Path) -> str:
    """The role a `.claude/agents/*.md` file defines: its front matter `name:`, else the file name."""
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeDecodeError):
        return path.stem
    if lines and lines[0].strip() == "---":
        for line in lines[1:]:
            if line.strip() == "---":
                break
            key, _, value = line.partition(":")
            if key.strip() == "name" and value.strip():
                return value.strip().strip("\"'")
    return path.stem


def roles(agents_dir: Path = AGENTS_DIR) -> list[str]:
    """The roles an Agent call may start, one per `.md` file in `agents_dir` (the main session's `coordinator` is none),
    sorted; empty when the folder cannot be read."""
    try:
        files = sorted(agents_dir.glob("*.md"))
    except OSError:
        return []
    return sorted({role_name(path) for path in files} - set(MAIN_SESSION_ONLY))


def worktree_isolation(tool_input: dict[str, object]) -> bool:
    """Whether the Agent call's input asks for worktree isolation."""
    isolation = tool_input.get("isolation")
    return isinstance(isolation, str) and isolation.strip().lower() == "worktree"


def agent_type(tool_input: dict[str, object]) -> str:
    """The `subagent_type` of the call, stripped; empty when missing or not a string."""
    value = tool_input.get("subagent_type")
    return value.strip() if isinstance(value, str) else ""


def role_message(requested: str, available: list[str]) -> str:
    """The refusal for a call whose type is not a Coney role, naming the allowed types and which to pick."""
    shown = f'"{requested}"' if requested else "no subagent_type (which runs as general-purpose)"
    allowed = [*available, *BUILT_IN]
    picks = [f"  - {name}: {use}" for name, use in ADVICE if name in available]
    return (
        f"Blocked: do not start an agent as {shown}. Coney's agents run as one of its roles, which carry the "
        "repository's rules for that kind of work. Set subagent_type to one of: "
        + ", ".join(allowed)
        + "\nPick:\n"
        + "\n".join(picks)
        + "\n  - Explore, Plan or claude-code-guide: a read-only search, a plan, a question about Claude Code; "
        '"fork": a short job that needs '
        "your conversation.\nWhen unsure, use engineer (docs/guides/agents.md, 'Which role to start').\n"
    )


def refusal(tool_input: object, available: list[str] | None = None) -> str | None:
    """The reason to refuse an Agent call with this input, or None to let it run.

    `available` is the list of Coney roles; the default reads `.claude/agents/`. When no role can be found the
    check refuses only the generic types, since it cannot tell which others are valid.
    """
    if not isinstance(tool_input, dict):
        return None
    if worktree_isolation(tool_input):
        return WORKTREE_MESSAGE
    requested = agent_type(tool_input)
    names = roles(AGENTS_DIR) if available is None else available
    generic = requested.casefold() in GENERIC
    known = {name.casefold() for name in (*names, *BUILT_IN)}
    if generic or (names and requested.casefold() not in known):
        return role_message(requested, names)
    return None


def main() -> int:
    """Read the hook's JSON from stdin; 2 (blocked, with the reason on stderr) or 0."""
    try:
        data = json.loads(sys.stdin.read() or "{}")
        tool_input = data["tool_input"]
    except (ValueError, KeyError, TypeError, OSError):
        return 0
    message = refusal(tool_input)
    if message is not None:
        sys.stderr.write(message)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
