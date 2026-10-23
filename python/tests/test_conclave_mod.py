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
from conclave_boundary import CODE, EVENTS, MOD, TETHER_EVENTS, engine_types, mod_sources, run_check, unavailable

Verdict = dict[str, dict[str, dict[str, list[str]]]]

#: The command as the mod writes it: the project folder is built from the checkout the mod's own folder sits in.
SNAPSHOT_ARGV = (
    "['uv', 'run', '-q', '--project', `${checkoutOf($.plugin.root)}/python`, "
    "'coney-tools', 'conclave', 'snapshot', '--json']"
)


def argv_with(project: str) -> str:
    """The snapshot command with another `--project` value, for a crossing."""
    return SNAPSHOT_ARGV.replace("`${checkoutOf($.plugin.root)}/python`", project)


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
    # What the five hooks do: the filters that keep command.run and ui.render off Tether's commands and panes, and an
    # answer that passes the event on (docs/guides/conclave.md#boundary).
    ("on('command.run', { command: 'tether-fleet' }, async () => ({ text: 'x' }))", "command: 'conclave'"),
    ("on('command.run', async ($, e, next) => ({ text: 'swallowed' }))", "command: 'conclave'"),
    ("on('command.run', { command: name }, async () => ({ text: 'x' }))", "command: 'conclave'"),
    ("on('command.run', { command: 'conclave', ...x }, async () => ({ text: 'x' }))", "command: 'conclave'"),
    ("on('command.run', { command: 'conclave', args: 'x' }, async () => ({ text: 'x' }))", "command: 'conclave'"),
    (
        "const filter = { command: 'conclave' } as const\non('command.run', filter, async () => ({ text: 'x' }))",
        "command: 'conclave'",
    ),
    ("on('ui.render', { component: 'Pane', requestId: 'tether' }, () => null)", "requestId: 'conclave'"),
    ("on('ui.render', { component: 'Pane' }, () => null)", "requestId: 'conclave'"),
    ("on('ui.render', { component: 'Pane', requestId: name }, () => null)", "requestId: 'conclave'"),
    ("on('ui.render', { component: 'AbovePrompt', requestId: 'tether' }, () => null)", "component: 'AbovePrompt'"),
    ("on('ui.render', async ($, e, next) => null)", "component: 'AbovePrompt'"),
    ("on('ui.render', { component: 'Message' }, () => null)", "component: 'AbovePrompt'"),
    ("on('ui.render', { component: name }, () => null)", "component: 'AbovePrompt'"),
    ("on('session.start', async () => ({ cwd: '' }))", "session.start must end"),
    ("on('session.start', async ($, e, next) => { if (name) return { cwd: '' }; return next(e) })", "session.start"),
    ("on('session.start', async ($, e, next) => { await next(e) })", "session.start must end"),
    ("on('session.start', async ($, e, next) => next({ ...e, cwd: '' }))", "session.start must end"),
    ("on('session.start', async ($, e, next) => { const again = e; return next(again) })", "session.start must end"),
    ("on('session.start', fn)", "inline function"),
    ("on('session.start', async ($, e, next) => next(e)).catch(() => ({ cwd: '' }))", "must end every path in next(e)"),
    ("const reg = on('session.start', async ($, e, next) => next(e))", "registration"),
    ("on('agent.offer', async () => ({ isOffered: false }))", "agent.offer must end"),
    ("on('agent.offer', async () => ({ isOffered: true }))", "agent.offer must end"),
    ("on('agent.offer', async ($, e, next) => ({ isOffered: false, agent: 'x' }))", "agent.offer must end"),
    ("on('agent.offer', async ($, e, next) => next(e)).catch(() => ({ isOffered: false }))", "next(e)"),
    (
        "on('classic.SessionStart', async ($, e, next) => ({ hookSpecificOutput: { hookEventName: 'SessionStart' } }))",
        "const r = await next(e)",
    ),
    ("on('classic.SessionStart', async ($, e, next) => next(e))", "const r = await next(e)"),
    (
        "on('classic.SessionStart', async ($, e, next) => {"
        " await $.ui.status('x'); const r = await next(e); return r })",
        "const r = await next(e)",
    ),
    (
        "on('classic.SessionStart', async ($, e, next) => { const r = await next(e); return { ...r } })",
        "const r = await next(e)",
    ),
    (
        "on('classic.SessionStart', async ($, e, next) => { const r = await next(e); if (name) return; return r })",
        "const r = await next(e)",
    ),
    (
        "on('classic.SessionStart', async ($, e, next) => { const r = await next(e); const q = r; return q })",
        "const r = await next(e)",
    ),
    (
        "on('classic.SessionStart', async ($, e, next) => { const r = await next(e); helper(r); return r })",
        "const r = await next(e)",
    ),
    ("on('classic.SessionStart', async ($, e, next) => { const r = await next(e) })", "const r = await next(e)"),
    (
        "on('classic.SessionStart', async ($, e, next) => { const r = await next(e); return r }).catch(() => ({}))",
        "must end every path in next(e)",
    ),
    ("$.command.register({ name: 'tether-fleet', description: 'x' })", "'conclave'"),
    ("$.command.register({ name, description: 'x' })", "'conclave'"),
    ("$.command.register({ description: 'x' })", "'conclave'"),
    ("$.command.register({ name: 'conclave', ...x })", "'conclave'"),
    ("const opts = { name: 'conclave' }\n$.command.register(opts)", "'conclave'"),
    ("$.ui.open({ id: 'tether', title: 't' })", "'conclave'"),
    ("$.ui.open({ title: 't' })", "'conclave'"),
    ("$.ui.open({ id: name })", "'conclave'"),
    ("const opts = { id: 'conclave' }\n$.ui.open(opts)", "'conclave'"),
    # What `$.process.run` is given besides the command: the project folder, the options.
    (f"await $.process.run({argv_with(chr(39) + '--with=evil' + chr(39))}, {{}})", "project folder"),
    (f"await $.process.run({argv_with(chr(39) + 'dir' + chr(39))}, {{}})", "project folder"),
    (f"await $.process.run({argv_with('`${name}/python`')}, {{}})", "project folder"),
    (f"await $.process.run({argv_with('`${checkoutOf(name)}/python`')}, {{}})", "project folder"),
    (
        "async function again() {\n  function checkoutOf(root: string) { return '--with=evil' }\n"
        f"  await $.process.run({argv_with('`${checkoutOf($.plugin.root)}/python`')}, {{}})\n}}",
        "project folder",
    ),
    (f"await $.process.run({SNAPSHOT_ARGV}, {{ env: {{ PYTHONSTARTUP: 'evil.py' }} }})", "options"),
    (f"await $.process.run({SNAPSHOT_ARGV}, {{ env: {{ PATH: '/tmp' }} }})", "options"),
    (f"await $.process.run({SNAPSHOT_ARGV}, {{ env: {{ UV_PROJECT_ENVIRONMENT: name, PATH: '/tmp' }} }})", "options"),
    (f"await $.process.run({SNAPSHOT_ARGV}, {{ stdin: 'x' }})", "options"),
    (f"await $.process.run({SNAPSHOT_ARGV}, {{ cwd: name, shell: true }})", "options"),
    (f"await $.process.run({SNAPSHOT_ARGV}, {{ ...x }})", "options"),
    (f"const options = {{ cwd: name }}\nawait $.process.run({SNAPSHOT_ARGV}, options)", "options"),
    (f"await $.process.run({SNAPSHOT_ARGV}, {{ cwd: name }}, 3)", "options"),
    # Tether's words split over a `+` or a template, and the ways to reach code from a string.
    ("const w = `HAND${'OFF'}`", "HANDOFF"),
    ("const w = 'HAND' + 'OFF.md'", "HANDOFF"),
    ("const a = 'HAND'\nconst w = a + 'OFF'", "HANDOFF"),
    ("const part = 'ear'\nconst w = `/cl${part}`", "/clear"),
    ("function f(e: { transcript_path: string }) { return e.transcript_path }", "transcript_path"),
    ("const F = (() => {}).constructor as unknown as (s: string) => () => void\nF('return 1')()", "constructor"),
    ("const F = (() => {}).constructor", "constructor"),
    ("const F = (() => {})['constructor']", "constructor"),
    ("const F = (() => {})['con' + 'structor']", "constructor"),
    ("const F = Reflect.get(fn, 'constructor')", "constructor"),
    ("const v = 1 as unknown as string", "as unknown as"),
    ("setTimeout('evil()', 0)", "setTimeout"),
    ("setInterval(() => {}, 1)", "setInterval"),
    ("queueMicrotask(() => {})", "queueMicrotask"),
]

#: Code the mod may contain: each must pass the check.
ALLOWED = {
    "the hooks the mod writes": """
import { atom, read, update } from 'claude-code'
async function draw(api: EngineInterface, e: unknown): Promise<unknown> { return api.ui.resolve(e) }
const count = atom({ plugin: 'conclave', key: 'count' } as const, 0)
const PANE = 'conclave'
export const register: Register = on => {
  on('session.start', async ($, e, next) => {
    try {
      await $.command.register({ name: 'conclave', description: 'x' })
      if (e.isInteractive) $.ui.open({ id: PANE, title: 'Conclave' }).catch(() => undefined)
    } catch {}
    return next(e)
  }).catch(($, e, next) => next(e))
  on('ui.render', { component: 'AbovePrompt' }, async ($, e, next) => (await draw($, e)) ?? next(e))
  on('ui.render', { component: 'Pane', requestId: PANE }, async ($, e) => {
    await update($, count, n => n + 1)
    return $.ui.resolve(e)
  })
  on('ui.render', { component: 'Pane', requestId: 'conclave' }, ($, e) => $.ui.resolve(e))
  on('agent.offer', async ($, e, next) => ((await read($, count)) > 1 ? next(e) : { isOffered: false })).catch(
    ($, e, next) => next(e),
  )
  on('agent.offer', async ($, e, next) => (name ? { isOffered: false } : await next(e)))
  on?.('command.run', { command: 'conclave' }, async $ => ({ text: String(await read($, count)) }))
  on('classic.SessionStart', async ($, e, next) => {
    const result = await next(e)
    try {
      await $.ui.status('x')
    } catch {}
    return result
  }).catch(($, e, next) => next(e))
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
    "the snapshot command, as the mod runs it": """
const root = checkoutOf($.plugin.root)
const argv = ['uv', 'run', '-q', '--project', `${root}/python`, 'coney-tools', 'conclave', 'snapshot', '--json']
await $.process.run(argv, {
  cwd: root,
  timeoutMs: 1000,
  ...(name === 'x' ? {} : { env: { UV_PROJECT_ENVIRONMENT: name } }),
})
""",
    "a comment that names what Tether keeps": (
        "// on('classic.Stop', fn) and $.session.usage() and HANDOFF\n/* /clear */\nconst quiet = 1\n"
    ),
    "a property that is called process": "const run = { process: { run: 1 } }\nconst again = run.process.run\n",
}


def all_snippets() -> dict[str, str]:
    """Every snippet above, by the name the check answers to."""
    return {snippet: snippet for snippet, _ in CROSSINGS} | {f"allowed: {name}": text for name, text in ALLOWED.items()}


@functools.cache
def verdicts() -> Verdict:
    """One run of the check over every snippet above and every source of the mod (a program build costs seconds)."""
    return run_check(all_snippets(), mod_sources())


@pytest.fixture
def verdict() -> Verdict:
    """The check's answer, or a skip naming what is missing (Node, TypeScript or the engine's types)."""
    reason = unavailable()
    if reason is not None:
        pytest.skip(reason)
    return verdicts()


def test_a_missing_tool_is_named_where_the_check_cannot_run(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    # Where there is no Node, or no description of the engine at all, the check says which; it never fails for a
    # reason of the mod's.
    unchecked = unavailable.__wrapped__
    monkeypatch.setattr("conclave_boundary.shutil.which", lambda _name: None)
    assert unchecked() == "needs Node.js"
    monkeypatch.setattr("conclave_boundary.shutil.which", lambda name: "node" if name == "node" else None)
    monkeypatch.setattr("conclave_boundary.ENGINE_TYPES", tmp_path / "missing" / "index.d.ts")
    monkeypatch.setattr("conclave_boundary.ENGINE_STUB", tmp_path / "missing" / "stub.d.ts")
    reason = unchecked()
    assert reason is not None and reason.startswith("needs the engine's types")
    # Neither `claude plugin validate` nor `claude plugin test` writes the types, so neither is the way to them.
    assert "plugin validate" not in reason and "--plugin-dir" in reason


def test_the_stub_stands_in_where_the_engines_types_are_not_written(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    stub = tmp_path / "stub.d.ts"
    stub.write_text("declare module 'claude-code' {}\n", encoding="utf-8")
    monkeypatch.setattr("conclave_boundary.ENGINE_TYPES", tmp_path / "missing" / "index.d.ts")
    monkeypatch.setattr("conclave_boundary.ENGINE_STUB", stub)
    assert engine_types() == "stub"
    monkeypatch.setattr("conclave_boundary.ENGINE_TYPES", stub)
    assert engine_types() == "engine"


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
