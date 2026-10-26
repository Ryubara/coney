# SPDX-License-Identifier: GPL-3.0-or-later
"""Runs the Conclave boundary check (docs/guides/conclave.md#boundary) over the mod's sources and over snippets.

The rules are in `conclave_boundary.cjs`, which reads the TypeScript compiler's own tree and types; this module holds
the lists they use (what the mod hooks, what it may call, what is Tether's), finds Node and TypeScript, and hands the
script one request. The check needs Node, TypeScript and a description of the engine's `claude-code` module: the
generated types a session that loads the mod writes beside it (`claude --plugin-dir tools/claude-mod`; neither
`claude plugin validate` nor `claude plugin test` writes them), or Coney's own stub of the module
(`conclave_engine_stub.d.ts`). Where one is missing, `unavailable()` says why."""

import functools
import json
import os
import shutil
import subprocess
from pathlib import Path
from typing import Any, NoReturn

import pytest
from conventions_support import REPO

MOD = REPO / "tools" / "claude-mod"
SCRIPT = Path(__file__).with_name("conclave_boundary.cjs")
#: The engine's types for `claude-code`; a session that loads the mod writes them beside it.
ENGINE_TYPES = MOD / ".claude-plugin" / "types" / "claude-code" / "index.d.ts"
#: Coney's own description of the part of `claude-code` the check needs, for where the engine's types are not written
#: (CI); `CONEY_ENGINE_STUB` names another file, for trying another stub.
ENGINE_STUB = Path(os.environ.get("CONEY_ENGINE_STUB") or Path(__file__).with_name("conclave_engine_stub.d.ts"))
#: The TypeScript the check (and `tsc` for the mod) uses; the version the mod was written against.
TYPESCRIPT = "typescript@5.6.3"
#: Code files of the mod's runtime; its tests, type contract and the engine's generated types are not part of it.
CODE = ("*.ts", "*.tsx", "*.js", "*.jsx", "*.mjs", "*.cjs", "*.mts", "*.cts")
NOT_RUNTIME = {"tests", "types", "node_modules", ".claude-plugin"}
#: The events the mod hooks, and nothing else. `classic.SessionStart` is only read; `classic.Stop` is the stop guard on
#: pull request problems nobody owns (the half of Tether's own Stop guard that Conclave took over with its pollers).
EVENTS = {"session.start", "classic.SessionStart", "classic.Stop", "command.run", "agent.offer", "ui.render"}
#: Events Tether hooks or whose work is Tether's (the checkpoint cycle, the cache guard, the idle wake, the HANDOFF
#: message, the subagent limits). Named here so a crossing says which line it crosses.
TETHER_EVENTS = {
    "classic.UserPromptSubmit",
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
#: `agent_type`, so no `$.session.` or `$.model.` call is allowed, and neither is a tool, a file or a model. The prompt
#: has two calls: `prompt.read` (is a person typing) and `prompt.submit`, which is the wake and is further confined
#: to the event-wake path (`wakeProblems` in conclave_boundary_syntax.cjs).
API_CALLS = {
    "clock.every",
    "clock.now",
    "command.register",
    "process.run",
    "prompt.read",
    "prompt.submit",
    "store.get",
    "store.set",
    "store.delete",
    "ui.open",
    "ui.panes",
    "ui.resolve",
    "ui.status",
    "ui.toast",
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
    "transcript_path",
]
#: Words refused only when they are a whole name or string, not a part of one: the ways to the `Function` constructor.
EXACT_WORDS = [
    "constructor",
    "getPrototypeOf",
    "setPrototypeOf",
    "getOwnPropertyDescriptor",
    "getOwnPropertyDescriptors",
    "__proto__",
    "__lookupGetter__",
    "__lookupSetter__",
    "__defineGetter__",
    "__defineSetter__",
    "defineProperty",
    "defineProperties",
]
#: The one name Conclave gives its command and its pane; it is also the filter of its `command.run` and `ui.render`.
NAME = "conclave"
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
    "setTimeout",
    "setInterval",
    "setImmediate",
    "queueMicrotask",
    "Reflect",
    "arguments",
]
#: The one command the mod runs, through `$.process.run`:
#: `uv run -q --project <dir> coney-tools conclave snapshot --json`,
#: which may grow by `--github`.
RUN = {
    "head": ["uv", "run", "-q", "--project"],
    "tail": ["coney-tools", "conclave", "snapshot", "--json"],
    "flags": ["--github"],
    #: The project folder is `${checkoutOf($.plugin.root)}/python`, the function being the mod's own in this file.
    "dir_function": "checkoutOf",
    "dir_file": "src/snapshot.ts",
    #: The private environment is `venvOf(<checkout>)` and the folder the command runs in is the checkout itself.
    "venv_function": "venvOf",
    #: The options `$.process.run` is given: the folder, the time limit and the private Python environment.
    "options": ["cwd", "timeoutMs", "env"],
    "env": ["UV_PROJECT_ENVIRONMENT"],
}
#: What a snippet has in scope without declaring it: the engine, the registrar and a few stubs of the wrong type for
#: anything but being named.
PRELUDE = [
    "import type { EngineInterface, On, Register } from 'claude-code'",
    "import { checkoutOf, startPolling, venvOf } from '../src/snapshot'",
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
    "declare function pending(found: string[], more?: unknown): string[]",
    "declare function wakeText(preamble: string, due: string[]): string",
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


def cannot_run(reason: str) -> NoReturn:
    """Ends a test whose check cannot run here: a failure under CI (the `CI` variable GitHub sets), a skip elsewhere.

    A skip cannot be told apart from a pass among a pull request's required checks, and this check is the only
    enforcement of the boundary to Tether, so CI must never pass without it."""
    if os.environ.get("CI"):
        pytest.fail(reason, pytrace=False)
    pytest.skip(reason)


def engine_types() -> str | None:
    """Which description of `claude-code` the check reads: "engine" (the generated types), "stub", or None."""
    if ENGINE_TYPES.is_file():
        return "engine"
    return "stub" if ENGINE_STUB.is_file() else None


@functools.cache
def unavailable() -> str | None:
    """Why the check cannot run here (no Node, no TypeScript, no description of the engine), or None when it can."""
    if shutil.which("node") is None:
        return "needs Node.js"
    if engine_types() is None:
        return (
            "needs the engine's types beside the mod (a session that loads it writes them: "
            "`claude --plugin-dir tools/claude-mod`) or Coney's stub of them (python/tests/conclave_engine_stub.d.ts)"
        )
    if typescript_module() is None:
        return f"needs {TYPESCRIPT} (npx could not fetch it)"
    return None


def _run_script(
    types: str | None, snippets: dict[str, str], files: list[Path], describe: list[str] | None
) -> dict[str, Any]:
    """Runs `conclave_boundary.cjs` over one request and returns its answer (`types` as in `run_check`)."""
    reason = unavailable()
    assert reason is None, reason
    script = typescript_module()
    chosen = types or engine_types()
    assert chosen in {"engine", "stub"}, chosen
    request = {
        "mod": str(MOD),
        "files": [str(path) for path in files],
        "runtime": [str(path) for path in mod_sources()],
        "snippets": snippets,
        "prelude": PRELUDE,
        "events": sorted(EVENTS),
        "tether_events": sorted(TETHER_EVENTS),
        "api": {"calls": sorted(API_CALLS), "properties": sorted(API_PROPERTIES)},
        "engine_functions": sorted(ENGINE_FUNCTIONS),
        "words": WORDS,
        "exact_words": EXACT_WORDS,
        "name": NAME,
        "banned_globals": BANNED_GLOBALS,
        "run": RUN,
        "stub": str(ENGINE_STUB) if chosen == "stub" else None,
        "describe": describe,
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
    answer: dict[str, Any] = json.loads(done.stdout)
    return answer


def run_check(
    snippets: dict[str, str], files: list[Path], types: str | None = None
) -> dict[str, dict[str, dict[str, list[str]]]]:
    """The script's verdict on `snippets` (name to text) and on `files`: per name, its `errors` and its `events`.

    `types` forces "engine" (the generated types) or "stub"; by default the generated ones are used when present."""
    verdict: dict[str, dict[str, dict[str, list[str]]]] = _run_script(types, snippets, files, None)
    return verdict


def describe_types(names: list[str], types: str) -> dict[str, Any]:
    """What `types` ("engine" or "stub") say of `names` (each one's kind, "value" or "type", or null when missing)
    and of the members of `EngineInterface` (`nouns`: each noun's method names)."""
    return _run_script(types, {}, [], names)
