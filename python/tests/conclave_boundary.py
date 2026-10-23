# SPDX-License-Identifier: GPL-3.0-or-later
"""Runs the Conclave boundary check (docs/guides/conclave.md#boundary) over the mod's sources and over snippets.

The rules are in `conclave_boundary.cjs`, which reads the TypeScript compiler's own tree and types; this module holds
the lists they use (what the mod hooks, what it may call, what is Tether's), finds Node and TypeScript, and hands the
script one request. The check needs Node and the engine's generated types (written beside the mod by
`claude plugin validate`); where either is missing, `unavailable()` says why and the tests that need it skip."""

import functools
import json
import os
import shutil
import subprocess
from pathlib import Path

from conventions_support import REPO

MOD = REPO / "tools" / "claude-mod"
SCRIPT = Path(__file__).with_name("conclave_boundary.cjs")
#: The engine's types for `claude-code`; the engine writes them beside a mod it loads or validates.
ENGINE_TYPES = MOD / ".claude-plugin" / "types" / "claude-code" / "index.d.ts"
#: The TypeScript the check (and `tsc` for the mod) uses; the version the mod was written against.
TYPESCRIPT = "typescript@5.6.3"
#: Code files of the mod's runtime; its tests, type contract and the engine's generated types are not part of it.
CODE = ("*.ts", "*.tsx", "*.js", "*.jsx", "*.mjs", "*.cjs", "*.mts", "*.cts")
NOT_RUNTIME = {"tests", "types", "node_modules", ".claude-plugin"}
#: The events the mod hooks, and nothing else: none of them is one Tether acts on (`classic.SessionStart` is only read).
EVENTS = {"session.start", "classic.SessionStart", "command.run", "agent.offer", "ui.render"}
#: Events Tether hooks or whose work is Tether's (the checkpoint cycle, the cache guard, the idle wake, the HANDOFF
#: message, the subagent limits). Named here so a crossing says which line it crosses.
TETHER_EVENTS = {
    "classic.UserPromptSubmit",
    "classic.Stop",
    "classic.SubagentStop",
    "classic.SessionEnd",
    "classic.PreCompact",
    "classic.PostCompact",
    "classic.PreToolUse",
    "classic.PostToolUse",
    "classic.Notification",
    "session.end",
    "session.usage",
    "turn.start",
    "turn.step",
    "turn.complete",
    "prompt.submit",
    "prompt.compose",
    "prompt.read",
    "tool.call",
    "tool.register",
    "agent.spawn",
    "agent.list",
}
#: Every `$.noun.method` the mod may call, and nothing else: the status line needs only the event's own `cwd` and
#: `agent_type`, so no `$.session.` or `$.model.` call is allowed, and neither is a tool, a file, a prompt or a model.
API_CALLS = {
    "clock.every",
    "clock.now",
    "command.register",
    "process.run",
    "store.get",
    "store.set",
    "store.delete",
    "ui.open",
    "ui.panes",
    "ui.resolve",
    "ui.status",
}
#: The one `$.noun.property` the mod reads (the folder it was loaded from).
API_PROPERTIES = {"plugin.root"}
#: What the engine's `claude-code` module offers that takes `$`: the mod's own state, read and written through atoms.
ENGINE_FUNCTIONS = {"read", "update"}
#: What the mod never names, in a string or an identifier: session figures, the prompt, a clear and Tether's messages.
WORDS = [
    "context_tokens",
    "seconds_since_last_response",
    "rateLimits",
    "/clear",
    "HANDOFF",
    "Continue from",
]
#: Globals a hooks module has no business with (the engine has none of these, and a type error says so too).
BANNED_GLOBALS = [
    "fetch",
    "globalThis",
    "process",
    "Bun",
    "Deno",
    "XMLHttpRequest",
    "WebSocket",
    "EventSource",
    "window",
    "self",
    "navigator",
    "require",
    "eval",
    "Function",
]
#: The one command the mod runs, through `$.process.run`:
#: `uv run -q --project <dir> coney-tools conclave snapshot --json`,
#: which may grow by `--github`.
RUN = {
    "head": ["uv", "run", "-q", "--project"],
    "tail": ["coney-tools", "conclave", "snapshot", "--json"],
    "flags": ["--github"],
}
#: What a snippet has in scope without declaring it: the engine, the registrar and a few stubs of the wrong type for
#: anything but being named.
PRELUDE = [
    "import type { EngineInterface, On, Register } from 'claude-code'",
    "declare const $: EngineInterface",
    "declare const on: On",
    "declare const fn: never",
    "declare const x: never",
    "declare const ask: never",
    "declare const name: string",
    "declare const EVENT: string",
    "declare function helper(...args: never[]): void",
    "declare function setup(...args: never[]): void",
    "declare function usage(): number",
]


def mod_sources(root: Path = MOD) -> list[Path]:
    """Every runtime code file of the mod, in any subfolder."""
    found = {path for pattern in CODE for path in root.rglob(pattern) if not path.name.endswith(".d.ts")}
    return sorted(path for path in found if not NOT_RUNTIME & set(path.relative_to(root).parts[:-1]))


def typescript_module() -> Path | None:
    """The `typescript.js` of `TYPESCRIPT`, from `CONEY_TYPESCRIPT` or npm's cache (fetched once with npx), or None."""
    given = os.environ.get("CONEY_TYPESCRIPT")
    if given:
        return Path(given)
    npx = shutil.which("npx")
    if npx is None:
        return None
    find = (
        "const p=require('path'),f=require('fs');"
        "console.log(process.env.PATH.split(p.delimiter)"
        ".map(d=>p.join(d,'..','typescript','lib','typescript.js')).find(x=>f.existsSync(x))||'')"
    )
    done = subprocess.run(
        [npx, "--yes", "-p", TYPESCRIPT, "node", "-e", find],
        capture_output=True,
        text=True,
        encoding="utf-8",
        timeout=300,
        check=False,
    )
    found = done.stdout.strip()
    return Path(found) if done.returncode == 0 and found else None


@functools.cache
def unavailable() -> str | None:
    """Why the check cannot run here (no Node, no TypeScript, no engine types), or None when it can."""
    if shutil.which("node") is None:
        return "needs Node.js"
    if not ENGINE_TYPES.is_file():
        claude = shutil.which("claude")
        if claude is not None:
            subprocess.run([claude, "plugin", "validate", str(MOD)], capture_output=True, timeout=300, check=False)
        if not ENGINE_TYPES.is_file():
            return "needs the engine's types beside the mod (run `claude plugin validate tools/claude-mod`)"
    if typescript_module() is None:
        return f"needs {TYPESCRIPT} (npx could not fetch it)"
    return None


def run_check(snippets: dict[str, str], files: list[Path]) -> dict[str, dict[str, dict[str, list[str]]]]:
    """The script's verdict on `snippets` (name to text) and on `files`: per name, its `errors` and its `events`."""
    reason = unavailable()
    assert reason is None, reason
    script = typescript_module()
    request = {
        "mod": str(MOD),
        "files": [str(path) for path in files],
        "snippets": snippets,
        "prelude": PRELUDE,
        "events": sorted(EVENTS),
        "tether_events": sorted(TETHER_EVENTS),
        "api": {"calls": sorted(API_CALLS), "properties": sorted(API_PROPERTIES)},
        "engine_functions": sorted(ENGINE_FUNCTIONS),
        "words": WORDS,
        "banned_globals": BANNED_GLOBALS,
        "run": RUN,
    }
    done = subprocess.run(
        [shutil.which("node") or "node", str(SCRIPT)],
        input=json.dumps(request),
        capture_output=True,
        text=True,
        encoding="utf-8",
        env={**os.environ, "CONEY_TYPESCRIPT": str(script)},
        timeout=600,
        check=False,
    )
    assert done.returncode == 0, done.stderr
    verdict: dict[str, dict[str, dict[str, list[str]]]] = json.loads(done.stdout)
    return verdict
