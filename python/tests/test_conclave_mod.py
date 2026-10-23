# SPDX-License-Identifier: GPL-3.0-or-later
"""The Conclave mod keeps to its boundary (docs/guides/conclave.md#boundary): it draws and hides agent types, and leaves
the session's health (context window, prompt cache, /clear, HANDOFF) to Tether. A program checks that, not a reader."""

import json
import re
from pathlib import Path

import pytest
from conventions_support import REPO

MOD = REPO / "tools" / "claude-mod"
SOURCES = sorted([*MOD.glob("hooks/*.ts*"), *MOD.glob("src/*.ts*")])
#: The events the mod hooks, and nothing else: none of them is one Tether acts on (`classic.SessionStart` is only read).
EVENTS = {"session.start", "classic.SessionStart", "command.run", "agent.offer", "ui.render"}
#: What the mod never calls or names: session figures, the prompt, the transcript, files, tools and a stop.
FORBIDDEN = [
    "usage(",
    "context_tokens",
    "seconds_since_last_response",
    "rateLimits",
    "$.prompt",
    "prompt.submit",
    "prompt.compose",
    "session.append",
    "classic.Stop",
    "classic.SubagentStop",
    "classic.PreCompact",
    "classic.PreToolUse",
    "tool.call",
    "$.tool.",
    "$.fs.",
    "$.agent.spawn",
    "$.agent.register",
    "$.http",
    "/clear",
    "HANDOFF",
    "Continue from",
]


def code(path: Path) -> str:
    """A source file without its comments (a comment may name what the mod leaves to Tether)."""
    text = path.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"(?m)^\s*//.*$|(?<=\s)//\s.*$", "", text)


def test_the_mod_has_sources() -> None:
    assert {path.name for path in SOURCES} >= {"register.tsx", "band.tsx", "pane.tsx", "snapshot.ts", "format.ts"}


@pytest.mark.parametrize("path", SOURCES, ids=lambda path: path.name)
def test_no_source_touches_what_tether_keeps(path: Path) -> None:
    text = code(path)
    found = [word for word in FORBIDDEN if word in text]
    assert not found, f"{path.name} names {found}: session health, prompts, tools and files are not Conclave's"


def test_the_mod_hooks_only_the_events_it_lists() -> None:
    hooked = {event for path in SOURCES for event in re.findall(r"\bon\(\s*'([\w.]+)'", code(path))}
    assert hooked == EVENTS


def test_the_only_command_the_mod_runs_is_the_read_only_snapshot() -> None:
    runs = [path.name for path in SOURCES if "$.process." in code(path)]
    assert runs == ["snapshot.ts"]
    text = code(MOD / "src" / "snapshot.ts")
    assert len(re.findall(r"\$\.process\.\w+", text)) == 1 and "$.process.run(argv" in text
    assert "'conclave', 'snapshot', '--json'" in text


def test_the_manifest_names_the_module_and_the_contract() -> None:
    manifest = json.loads((MOD / ".claude-plugin" / "plugin.json").read_text(encoding="utf-8"))
    assert manifest["name"] == "conclave" and re.fullmatch(r"\d+\.\d+\.\d+", manifest["version"])
    assert (MOD / manifest["types"]).is_file()
    hooks = json.loads((MOD / "hooks" / "hooks.json").read_text(encoding="utf-8"))
    assert hooks == {"modules": ["./register.tsx"]}
