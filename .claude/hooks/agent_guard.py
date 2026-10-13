# SPDX-License-Identifier: GPL-3.0-or-later
"""Claude Code PreToolUse hook: refuse an Agent call that asks for `isolation: "worktree"`.

That option makes Claude Code create the agent's worktree itself, under the main checkout's `.claude/worktrees/` with
an automatic branch name. Coney's agents work in `../../worktrees/coney/<branch>/` on a branch the brief names: the
Defender exclusion that keeps builds from starving the CPU covers only that folder, and the workspace tools
(`coney-tools workspace clean`, the `.coney-busy` marker, the release queue) look only there
(docs/guides/agents.md, "Where an agent works"). The agent creates its own worktree with `git worktree add`, as its
brief says.

This hook, registered in `.claude/settings.json` for the Agent tool, refuses a call whose `tool_input.isolation` is
`worktree`. It is standard-library only. Exit code 2 blocks the call and shows stderr to the agent; every other
outcome (also input it cannot read) exits 0.
"""

from __future__ import annotations

import json
import sys

MESSAGE = (
    'Blocked: do not start an agent with isolation: "worktree". It puts the worktree under .claude/worktrees/ with '
    "an automatic branch name, outside ../../worktrees/coney/<branch>/ (the layout every tool and the Defender "
    "exclusion assume). Start the agent without `isolation` and tell it, in its brief, to create its own worktree "
    "with `git worktree add ../../worktrees/coney/<branch> -b <branch> origin/main` "
    "(docs/guides/agents.md, 'Where an agent works').\n"
)


def blocked(tool_input: object) -> bool:
    """Whether the Agent call's input asks for worktree isolation."""
    if not isinstance(tool_input, dict):
        return False
    isolation = tool_input.get("isolation")
    return isinstance(isolation, str) and isolation.strip().lower() == "worktree"


def main() -> int:
    """Read the hook's JSON from stdin; 2 (blocked, with the reason on stderr) or 0."""
    try:
        data = json.loads(sys.stdin.read() or "{}")
        tool_input = data["tool_input"]
    except (ValueError, KeyError, TypeError, OSError):
        return 0
    if blocked(tool_input):
        sys.stderr.write(MESSAGE)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
