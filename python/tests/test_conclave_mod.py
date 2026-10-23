# SPDX-License-Identifier: GPL-3.0-or-later
"""The Conclave mod keeps to its boundary (docs/guides/conclave.md#boundary): it draws and hides agent types, and leaves
the session's health (context window, prompt cache, /clear, HANDOFF, the idle wake) to Tether. A program checks that,
not a reader, so the check below is itself tested: every way across the line that a reader would catch is a case that
must fail here, and the real sources must pass it."""

import json
import re
from pathlib import Path

import pytest
from conventions_support import REPO

MOD = REPO / "tools" / "claude-mod"
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
API = {
    "clock.every",
    "clock.now",
    "command.register",
    "plugin.root",
    "process.run",
    "store.get",
    "store.set",
    "store.delete",
    "ui.open",
    "ui.panes",
    "ui.resolve",
    "ui.status",
}
#: What the mod never names in code: session figures, the prompt, the transcript, a stop and Tether's messages.
FORBIDDEN = [
    "usage(",
    "context_tokens",
    "seconds_since_last_response",
    "rateLimits",
    "session.append",
    "$.agent.spawn",
    "$.agent.register",
    "$.http",
    "/clear",
    "HANDOFF",
    "Continue from",
]
QUOTED = r"""(['"`])((?:(?!\1)[^\\\n]|\\.)*)\1"""


def mod_sources(root: Path = MOD) -> list[Path]:
    """Every runtime code file of the mod, in any subfolder."""
    found = {path for pattern in CODE for path in root.rglob(pattern) if not path.name.endswith(".d.ts")}
    return sorted(path for path in found if not NOT_RUNTIME & set(path.relative_to(root).parts[:-1]))


def code(text: str) -> str:
    """Source without its comments (a comment may name what the mod leaves to Tether)."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"(?m)^\s*//.*$|(?<=\s)//\s.*$", "", text)


def hooked_events(text: str) -> set[str]:
    """The event names in `on(...)` calls, written with any quote."""
    return set(plain_events(text))


def plain_events(text: str) -> list[str]:
    """The event name of each `on(...)` call that gives one as a plain literal (a template with `${` is not plain)."""
    names = [match.group(2) for match in re.finditer(r"\bon\(\s*" + QUOTED, text)]
    return [name for name in names if "${" not in name]


def crossings(source: str) -> list[str]:
    """What in `source` crosses the line between Conclave and Tether, or hides a crossing from this check."""
    text = code(source)
    found: list[str] = []
    # Hooks: every `on(` names its event as a plain literal, and `on` is not passed around or renamed.
    if len(re.findall(r"\bon\(", text)) != len(plain_events(text)):
        found.append("an on(...) call whose event is not a plain string literal")
    for event in sorted(hooked_events(text) - EVENTS):
        found.append(f"hooks the event {event}, which is not one of the five the mod lists")
    bare = re.sub(QUOTED, '""', text)
    bare = re.sub(r"Register\s*=\s*(?:\(\s*on\b[^)]*\)|on)\s*=>", "", bare)
    if re.search(r"\bon\b(?!\s*\()", bare):
        found.append("`on` is used other than as on(...): it is renamed, passed on or called indirectly")
    # Tether's events, in any quoting and anywhere (a variable that holds one is as bad as a call).
    quoted = {match.group(2) for match in re.finditer(QUOTED, text)}
    found += [f"names the event {name}, which is Tether's" for name in sorted(quoted & TETHER_EVENTS)]
    found += [f"names {word!r}: Tether's, not Conclave's" for word in FORBIDDEN if word in text]
    # The engine API: an allowlist of `$.noun.method`, no aliasing of `$` and no computed access.
    for noun, method in re.findall(r"\$\.(\w+)(?:\.(\w+))?", text):
        call = f"{noun}.{method}" if method else noun
        if call not in API:
            found.append(f"calls $.{call}, which is not in the mod's list (no $.session. or $.model. at all)")
    if re.search(r"\$\s*\[|\$\s*\?\.\[", text):
        found.append("reads `$` by a computed name")
    if re.search(r"=\s*\$\s*(?:[;,)]|$)", text, flags=re.MULTILINE):
        found.append("copies or destructures `$`, which hides what is called on it")
    if re.search(r"\b(?:require|eval)\s*\(|\bimport\s*\(|new\s+Function\b", text):
        found.append("loads or builds code at run time")
    for _, module in re.findall(r"\bfrom\s+(['\"])([^'\"]+)\1", text):
        if module != "claude-code" and not module.startswith("."):
            found.append(f"imports {module!r}: the mod uses only claude-code and its own files")
    return found


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


@pytest.mark.parametrize("path", mod_sources(), ids=lambda path: path.relative_to(MOD).as_posix())
def test_no_source_crosses_into_what_tether_keeps(path: Path) -> None:
    assert crossings(path.read_text(encoding="utf-8")) == []


def test_the_mod_hooks_only_the_events_it_lists() -> None:
    hooked = {event for path in mod_sources() for event in hooked_events(code(path.read_text(encoding="utf-8")))}
    assert hooked == EVENTS
    assert not EVENTS & TETHER_EVENTS


#: Each snippet is a way across the line that this check has to see, with a word its message must carry.
CROSSINGS = [
    ('on("classic.UserPromptSubmit", async ($, e, next) => next(e))', "classic.UserPromptSubmit"),
    ("on('classic.UserPromptSubmit', async ($, e, next) => next(e))", "classic.UserPromptSubmit"),
    ("on(`classic.Stop`, async ($, e, next) => next(e))", "classic.Stop"),
    ("on(  'turn.complete' , h)", "turn.complete"),
    ("on('session.end', h)", "session.end"),
    ("on(EVENT, h)", "plain string literal"),
    ("on(`tool.${name}`, h)", "plain string literal"),
    ("const listen = on\nlisten('classic.Stop', h)", "`on` is used other than"),
    ("setup(on, h)", "`on` is used other than"),
    ("export const register: Register = (on, options) => { helper(on) }", "`on` is used other than"),
    ("const EVENT = 'classic.UserPromptSubmit'", "Tether's"),
    ("const e = ['turn.step']", "Tether's"),
    ("await $.session.usage()", "$.session.usage"),
    ("await $.session.send({ text: 'x' })", "$.session.send"),
    ("await $.model.complete(ask)", "$.model.complete"),
    ("const s = $.session", "$.session"),
    ("await $['session'].usage()", "computed name"),
    ("const { session } = $", "destructures"),
    ("const ui = $\nui.session", "destructures"),
    ("await $.fs.read('HANDOFF.md')", "$.fs.read"),
    ("await $.tool.register({ name: 'x' })", "$.tool.register"),
    ("await $.prompt.read()", "$.prompt.read"),
    ("await $.agent.list()", "$.agent.list"),
    ("await $.process.spawn(['x'])", "$.process.spawn"),
    ("const m = await import('./other')", "run time"),
    ("import fs from 'node:fs'", "node:fs"),
    ("const x = '/clear'", "/clear"),
    ("const x = 'HANDOFF.md'", "HANDOFF"),
    ("const n = usage(", "usage("),
]


@pytest.mark.parametrize(("snippet", "word"), CROSSINGS)
def test_a_crossing_is_found(snippet: str, word: str) -> None:
    found = crossings(snippet)
    assert found, f"not seen: {snippet}"
    assert any(word in line for line in found), f"{word!r} not in {found}"


def test_a_crossing_in_a_comment_is_not_one() -> None:
    assert crossings("// on('classic.Stop', h) and $.session.usage()\n/* HANDOFF */ const x = 1\n") == []


def test_what_the_mod_does_is_not_a_crossing() -> None:
    allowed = """
import { atom } from 'claude-code'
import type { Register } from 'claude-code'
import { helper } from '../src/helper'
export const register: Register = on => {
  on('session.start', async ($, e, next) => { await $.command.register({ name: 'x' }); return next(e) })
  on("ui.render", { component: 'AbovePrompt' }, ($, e) => $.ui.resolve(e))
  $.clock.every(1000, () => {})
}
"""
    assert crossings(allowed) == []


def test_the_only_command_the_mod_runs_is_the_read_only_snapshot() -> None:
    runs = [path.name for path in mod_sources() if "$.process." in code(path.read_text(encoding="utf-8"))]
    assert runs == ["snapshot.ts"]
    text = code((MOD / "src" / "snapshot.ts").read_text(encoding="utf-8"))
    assert len(re.findall(r"\$\.process\.\w+", text)) == 1 and "$.process.run(argv" in text
    assert "'conclave', 'snapshot', '--json'" in text


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
