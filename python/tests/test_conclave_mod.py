# SPDX-License-Identifier: GPL-3.0-or-later
"""The Conclave mod keeps to its boundary (docs/guides/conclave.md#boundary): it draws and hides agent types, and leaves
the session's health (context window, prompt cache, /clear, HANDOFF, the idle wake) to Tether. A program checks that,
not a reader: conclave_boundary.cjs reads the mod with the TypeScript compiler (its tree and its types), and the tests
below run it over the real sources, which must pass, and over every way across the line that a reader or an earlier
version of the check missed, which must each fail."""

import functools
import json
import re
from pathlib import Path

import pytest
from conclave_boundary import CODE, EVENTS, MOD, TETHER_EVENTS, mod_sources, run_check, unavailable

Verdict = dict[str, dict[str, dict[str, list[str]]]]

SNAPSHOT_ARGV = "['uv', 'run', '-q', '--project', 'dir', 'coney-tools', 'conclave', 'snapshot', '--json']"

#: Each snippet is a way across the line that the check has to see, with a word its message must carry. They run as
#: modules of the mod, with the engine's `$`, its `on` and a few stubs in scope (conclave_boundary.PRELUDE).
CROSSINGS = [
    # The events the mod hooks: Tether's, in any quoting, and every shape an on(...) call can take.
    ('on("classic.UserPromptSubmit", async ($, e, next) => next(e))', "classic.UserPromptSubmit"),
    ("on('classic.UserPromptSubmit', async ($, e, next) => next(e))", "classic.UserPromptSubmit"),
    ("on(`classic.Stop`, async ($, e, next) => next(e))", "classic.Stop"),
    ("on(  'turn.complete' , fn)", "turn.complete"),
    ("on('session.end', fn)", "session.end"),
    ("on('classic.*', fn)", "classic.*"),
    ("on('classic.SubagentStart', fn)", "classic.SubagentStart"),
    ("on ('turn.abort', fn)", "turn.abort"),
    ("on\n('classic.SubagentStart', fn)", "classic.SubagentStart"),
    ("on?.('classic.SubagentStart', fn)", "classic.SubagentStart"),
    ("on<'x'>('classic.SubagentStart', fn)", "classic.SubagentStart"),
    ("(on)('classic.Stop', fn)", "classic.Stop"),
    ("(on as On)('classic.Stop', fn)", "classic.Stop"),
    ("on(EVENT, fn)", "plain string literal"),
    ("on(`tool.${name}`, fn)", "plain string literal"),
    ("const EVENT2 = 'classic.UserPromptSubmit'", "Tether's"),
    ("const e = ['turn.step']", "Tether's"),
    # `on` renamed, passed on or called indirectly.
    ("const listen = on\nlisten('classic.Stop', fn)", "`on` is used other than"),
    ("setup(on, fn)", "`on` is used other than"),
    ("on.call(null, 'classic.Stop', fn)", "`on` is used other than"),
    ("const hooks = { on }", "`on` is used other than"),
    ("export const register: Register = (on, options) => { helper(on) }", "`on` is used other than"),
    ("export const register: Register = hook => { hook('classic.SubagentStart', fn) }", "classic.SubagentStart"),
    (
        "export const register: Register = hook => { const again = hook; again('classic.Stop', fn) }",
        "`on` is used other",
    ),
    # The engine's other nouns, whatever `$` is called and however it is copied, spread, passed or cast.
    ("await $.session.usage()", "$.session.usage"),
    ("await $.session.send({ text: 'x' })", "$.session.send"),
    ("await $.model.complete(ask)", "$.model.complete"),
    ("const s = $.session", "$.session"),
    ("await $['session'].usage()", "computed name"),
    ("await $.fs.read('HANDOFF.md')", "$.fs.read"),
    ("await $.tool.register({ name: 'x' })", "$.tool.register"),
    ("await $.prompt.read()", "$.prompt.read"),
    ("await $.agent.list()", "$.agent.list"),
    ("await $.process.spawn(['x'])", "$.process.spawn"),
    ("const call = $.clock.now\ncall()", "other than by calling it"),
    (
        "on('session.start', async (api, e, next) => { await api.prompt.read(); await api.session.send({}) })",
        "$.prompt.read",
    ),
    (
        "async function f(engine: EngineInterface) { await engine.model.complete(x); await engine.tool.register(x) }",
        "$.model.complete",
    ),
    ("await $?.session.send({})", "$.session.send"),
    ("await $ .session.send({})", "$.session.send"),
    ("await $\n  .prompt.read()", "$.prompt.read"),
    ("await (($)).session.send({})", "$.session.send"),
    ("await $!.session.send({})", "$.session.send"),
    ("const { session } = $", "destructures"),
    ("function f({ session }: EngineInterface) {}", "destructures"),
    ("const ui = $\nui.session", "copies"),
    ("await Reflect.get($, 'session').send({})", "passes"),
    ("Object.values($)", "passes"),
    ("helper($)", "passes"),
    ("function keep(value: unknown) {}\nkeep($)", "passes"),
    ("function keep<T>(value: T) {}\nkeep($)", "passes"),
    ("const all = { ...$ }\nall.session.send({})", "spreads"),
    ("const list = [$]\nlist[0].session.send({})", "other than as $.noun.method"),
    ("const holder = { engine: $ }", "other than as $.noun.method"),
    ("const holder = { $ }", "other than as $.noun.method"),
    ("function f(): EngineInterface { return $ }", "other than as $.noun.method"),
    ("class Holder { api = $ }", "other than as $.noun.method"),
    ("let later: EngineInterface\nlater = $", "other than as $.noun.method"),
    ("const hidden = $ as unknown as { session: { send(x: object): void } }", "casts"),
    ("const hidden = $ as EngineInterface", "casts"),
    # The one command the mod runs.
    ("await $.process.run(['git', 'push'], {})", "snapshot"),
    ("const argv = ['git', 'push']\nawait $.process.run(argv, {})", "snapshot"),
    (f"const argv = {SNAPSHOT_ARGV}\nargv.push('--force')\nawait $.process.run(argv, {{}})", "snapshot command array"),
    (f"const argv = {SNAPSHOT_ARGV}\nargv[0] = 'git'\nawait $.process.run(argv, {{}})", "snapshot command array"),
    # Run-time code, other modules, globals, type escapes.
    ("const m = await import('./other')", "run time"),
    ("import fs from 'node:fs'", "node:fs"),
    ("import { evil } from '../tests/format.test'", "not one of the mod's checked sources"),
    ("import fixture from '../tests/fixtures/snapshot'", "not one of the mod's checked sources"),
    ("await fetch('http://x')", "fetch"),
    ("globalThis.process.env", "globalThis"),
    ("const f = new Function('return 1')", "Function"),
    ("eval('1')", "eval"),
    ("const v: any = 1", "`any`"),
    ("// @ts-ignore\nconst v: number = 'x'", "suppresses a type error"),
    # Strings and comments hide nothing from a tree, and Tether's words are refused wherever they are.
    ("const s = ' // '; await $.session.send({ text: 'x' })", "$.session.send"),
    ("const a = '/*'; await $.session.send({}); const b = '*/'", "$.session.send"),
    ("const word = '/clear'", "/clear"),
    ("const word = 'HANDOFF.md'", "HANDOFF"),
    ("const HANDOFF = 1", "HANDOFF"),
    ("const n = usage()", "usage("),
    ("const m = { context_tokens: 1 }", "context_tokens"),
]

#: Code the mod may contain: each must pass the check.
ALLOWED = {
    "the hooks the mod writes": """
import { atom, read, update } from 'claude-code'
import { helper2 } from '../src/helper'
const count = atom({ plugin: 'conclave', key: 'count' } as const, 0)
export const register: Register = on => {
  on('session.start', async ($, e, next) => { await $.command.register({ name: 'x' }); return next(e) })
  on('ui.render', { component: 'AbovePrompt' }, async ($, e) => {
    await update($, count, n => n + 1)
    return $.ui.resolve(e)
  })
  on('agent.offer', async ($, e, next) => next(e)).catch(($, e, next) => next(e))
  on?.('command.run', { command: 'conclave' }, async $ => ({ text: String(await read($, count)) }))
  on('classic.SessionStart', async (api, e, next) => { await api.ui.status('x'); return next(e) })
  $.clock.every(1000, () => {})
}
""",
    "a helper that takes the engine": """
async function draw(api: EngineInterface, key: string): Promise<unknown> {
  return (await api.store.get(key)) ?? null
}
export async function again(engine: EngineInterface) { return draw(engine, 'a') }
""",
    "the snapshot command, with the flag it takes": f"""
const argv = {SNAPSHOT_ARGV}
if (name === 'x') argv.push('--github')
await $.process.run(argv, {{ cwd: $.plugin.root, timeoutMs: 1000 }})
""",
    "a comment that names what Tether keeps": (
        "// on('classic.Stop', fn) and $.session.usage() and HANDOFF\n/* /clear */\nconst quiet = 1\n"
    ),
    "a property that is called process": "const run = { process: { run: 1 } }\nconst again = run.process.run\n",
}


@functools.cache
def verdicts() -> Verdict:
    """One run of the check over every snippet above and every source of the mod (a program build costs seconds)."""
    snippets = {snippet: snippet for snippet, _ in CROSSINGS} | {
        f"allowed: {name}": text for name, text in ALLOWED.items()
    }
    return run_check(snippets, mod_sources())


@pytest.fixture
def verdict() -> Verdict:
    """The check's answer, or a skip naming what is missing (Node, TypeScript or the engine's types)."""
    reason = unavailable()
    if reason is not None:
        pytest.skip(reason)
    return verdicts()


def test_a_missing_tool_is_named_where_the_check_skips(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    # CI has no Claude Code, so there the check skips: it says why, and never skips for a reason of the mod's.
    unchecked = unavailable.__wrapped__
    monkeypatch.setattr("conclave_boundary.shutil.which", lambda _name: None)
    assert unchecked() == "needs Node.js"
    monkeypatch.setattr("conclave_boundary.shutil.which", lambda name: "node" if name == "node" else None)
    monkeypatch.setattr("conclave_boundary.ENGINE_TYPES", tmp_path / "missing" / "index.d.ts")
    reason = unchecked()
    assert reason is not None and reason.startswith("needs the engine's types")


def test_the_mod_has_sources() -> None:
    names = {path.name for path in mod_sources()}
    assert names >= {"register.tsx", "band.tsx", "pane.tsx", "snapshot.ts", "format.ts", "state.ts"}


def test_sources_in_every_subfolder_are_scanned(tmp_path: Path) -> None:
    (tmp_path / "src" / "deep" / "er").mkdir(parents=True)
    (tmp_path / "lib").mkdir()
    (tmp_path / "tests").mkdir()
    for name in ("src/deep/er/a.ts", "lib/b.tsx", "c.mjs", "tests/skipped.ts", "types/skipped.d.ts"):
        (tmp_path / name).parent.mkdir(exist_ok=True)
        (tmp_path / name).write_text("export {}\n", encoding="utf-8")
    assert [path.relative_to(tmp_path).as_posix() for path in mod_sources(tmp_path)] == [
        "c.mjs",
        "lib/b.tsx",
        "src/deep/er/a.ts",
    ]


def test_the_mod_is_typescript_only() -> None:
    # The check reads TypeScript; a plain script in the mod would be code it cannot see into.
    assert [path.name for path in mod_sources() if path.suffix not in {".ts", ".tsx"}] == []


@pytest.mark.parametrize("path", mod_sources(), ids=lambda path: path.relative_to(MOD).as_posix())
def test_no_source_crosses_into_what_tether_keeps(path: Path, verdict: Verdict) -> None:
    assert verdict["files"][str(path)]["errors"] == []


def test_the_mod_hooks_only_the_events_it_lists(verdict: Verdict) -> None:
    hooked = {event for entry in verdict["files"].values() for event in entry["events"]}
    assert hooked == EVENTS
    assert not EVENTS & TETHER_EVENTS


@pytest.mark.parametrize(("snippet", "word"), CROSSINGS, ids=[f"crossing-{n:02d}" for n in range(len(CROSSINGS))])
def test_a_crossing_is_found(snippet: str, word: str, verdict: Verdict) -> None:
    found = verdict["snippets"][snippet]["errors"]
    assert found, f"not seen: {snippet}"
    assert any(word in line for line in found), f"{word!r} not in {found}"


@pytest.mark.parametrize("name", list(ALLOWED))
def test_what_the_mod_does_is_not_a_crossing(name: str, verdict: Verdict) -> None:
    assert verdict["snippets"][f"allowed: {name}"]["errors"] == []


def test_the_snapshot_is_the_command_the_mod_runs() -> None:
    text = (MOD / "src" / "snapshot.ts").read_text(encoding="utf-8")
    assert "'coney-tools', 'conclave', 'snapshot', '--json'" in text


def test_the_manifest_names_the_module_and_the_contract() -> None:
    manifest = json.loads((MOD / ".claude-plugin" / "plugin.json").read_text(encoding="utf-8"))
    assert manifest["name"] == "conclave" and re.fullmatch(r"\d+\.\d+\.\d+", manifest["version"])
    assert (MOD / manifest["types"]).is_file()
    hooks = json.loads((MOD / "hooks" / "hooks.json").read_text(encoding="utf-8"))
    assert hooks == {"modules": ["./register.tsx"]}


def test_every_code_file_of_the_mod_has_the_licence_line() -> None:
    files = [
        path
        for pattern in CODE
        for path in MOD.rglob(pattern)
        if not {"node_modules", ".claude-plugin"} & set(path.parts)
    ]
    assert files
    for path in files:
        first = path.read_text(encoding="utf-8").splitlines()[0]
        assert first == "// SPDX-License-Identifier: GPL-3.0-or-later", f"{path.relative_to(MOD)}: {first!r}"
