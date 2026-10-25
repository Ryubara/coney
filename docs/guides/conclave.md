# Conclave, the Claude Code mod

Conclave is Coney's mod for Claude Code, a folder in the repository (`tools/claude-mod/`) that a session loads to get
three things the shell cannot give it: a band above the prompt, a status line and a pane that stay on screen and update
by themselves, and a shorter list of agent types for the model to choose from. It **draws and hides; it never decides
and never writes.** Every fact it shows comes from one command, `coney-tools conclave snapshot`, so the mod holds no
second copy of the formats that command reads (the task file, the stamp log, the claim cards, the hook settings), and
every rule it displays is still held by the hooks, `coney-tools` and CI without it. A session started without the mod
loses the display and the shorter list, and nothing else.

This version is read-only: it has no wakes, no stop guard, no tools for the model and no toasts. What comes next is
under [Planned](#planned).

## What it shows {#shows}

**The band** sits above the prompt, one line on a wide terminal and two on a narrow one (under `band_narrow_columns`,
100). Each item is a button that opens the pane, and an item whose count is zero is left out:

| Item | Says | Colour |
| --- | --- | --- |
| `2 units`, `✖1 failed` | Units with a task file that are not merged: their count, and how many failed a step, have a pull request whose checks failed or have a task file that cannot be read | red when any failed |
| `PRs 4 (✖1 #812)` | Open pull requests, and the first one with a failed check or a cause from [`repo pr-health`](coney-tools-repo.md#repo-pr-health) | red when any failed |
| `claims 3` | Held claims of every contributor | amber from the warning hours, red in the last `claim_red_hours` of any claim, once expiry is enforced |
| `builds 2 (+3 queued)` | Build slots in use and builds waiting, over every lane | none |
| `⚠ P-critical #790` | The first open issue labelled `P-critical`, and how many more | amber |
| `disk 61 GB` | Free space on the workspace drive | amber under `workspace.disk_min_free_gb` + `workspace.disk_margin_gb`, red under `workspace.disk_min_free_gb` |

A snapshot older than `stale_after_reads` (3) local intervals dims the band and adds `stale 4m`. A failed first read
shows the first line of the error; a snapshot of a newer shape than the mod knows shows "update the checkout" and
nothing else.

**The status line** reads `role · issue · stage` for a session in a unit's worktree (`engineer · #741 · compared
failed`; the task name when the unit names no issue), `coordinator · 2 units · ✖1` for the coordinator, and `no role:
start with coney-agent` for a session that started with no `--agent`. The role is the session's agent type, which the
mod reads from the start of the session. The issue and the stage are written as the pane's row writes them.

**The pane** (`/conclave`, or a press on the band) has two sections and a layout for every width, chosen by three
`conclave` keys of [.github/conventions.yml](repo:.github/conventions.yml) that the snapshot carries:

| Width | Layout |
| --- | --- |
| From `pane_wide_columns` (110) | **Wide**: a row is one line. |
| From `pane_compact_columns` (60) | **Compact**: the pane opens with the band's own items in the band's colours, and a row has the key fields on its first line (the mark, the issue, the stage and the pull request) and the title, who works on it and the build on a dim second line. |
| From `pane_min_columns` (30) | **Narrow**: as compact, but a row's first line keeps only the mark, the issue (the task when it names none) and the stage; the pull request, who, the build and last the title go on the second line. A claim reads `#741 2d 14h (warned) contrib-a`: the bare countdown, `~2d 14h` while expiry is not enforced (it would expire then), or `waits: S-blocked`, ahead of the claimant. |
| Under `pane_min_columns` | **One line**: the band's items joined by `·`, clipped, in the colour of the worst of them. |

In the compact and narrow layouts every line is clipped to the width with an ellipsis, so the most decision-relevant
text (the issue, the stage, the countdown) comes first and the title is what a clip cuts. The details of an open row
wrap under their label, so nothing an open row says is lost to the width. The pane is never empty and never refuses a
width. The words (stage, build, pull request, counts) are the band's and the status line's in every layout. The pane
opens by itself at the start of a session where the terminal is wide enough for the engine to place an unasked pane, and
waits for `/conclave` where it is not; the engine seats a pane the person opens at any width.

- **Units and issues** lists every unit (a scratch folder with a task file, with a worktree or changed in the last
  `unit_days`) and the issues the signed-in account holds with no unit here. A wide row is one line: a mark, the issue
  numbers, the title, who works on it, the stage, the build it runs or waits for, the pull request and its age. A press
  opens the row, one at a time, and shows the claimant and agent, the status and time in stage, the claim's expiry,
  the scope and the units or claims it overlaps, every stamp step (`✔` done, `✖` failed, `–` not needed, `·` missing),
  the reason a failed row is red, the last detached build and the links. Failed rows come first, then active, not
  started and merged ones (merged units stay for `merged_visible_minutes`).
- **Claims** lists every held claim with its countdown. The countdown runs on the mod's own clock, once a second while
  the pane is shown, from the deadline in the snapshot, so the display never waits for a read. A claim whose release
  clock waits (an `S-blocked`, `S-needs-info` or `S-on-hold` issue) says why instead of a time, and while
  `claims.release` is off the line reads "would expire in ..." because nothing releases it yet.

Marks are single-width coloured symbols (`⚠ ● ✔ ✖`) on the terminal, which depends on the font, and emoji on the
desktop, VS Code and mobile surfaces.

## The boundary: what Conclave leaves to Tether {#boundary}

Session health belongs to [Tether](coney-tools-setup.md#coord-wakes): the context window, the prompt cache and its
expiry, the automatic `/clear` and the "Continue from HANDOFF.md" message, subagents past their context limit, the
checkpoint cycle and the cache guard. **Conclave shows no context or cache figure, sends no wake about them and adds no
hook that overlaps them.** The idle wake stays in Tether too, now and in later versions.
`python/tests/test_conclave_mod.py` holds the line, so a change that crosses it fails the Python tests. The check does
not read spellings: it builds the mod with the TypeScript compiler (`python/tests/conclave_boundary.cjs`) and judges the
tree and the types, so a renamed `$`, an alias, a spread or a cast cannot hide a call. It refuses:

- **Hooks other than the five.** The mod hooks exactly `session.start`, `command.run`, `agent.offer`, `ui.render` and
  `classic.SessionStart` (which only reads the session's agent type, passes the event on first and returns the answer
  whole). Every value of the engine's type `On` (the `on` of `register`, whatever it is called) may only be *called*,
  with a plain string literal that is one of those five: a hook on another event, a template or a variable as the
  event, `on` renamed, passed on, put in an object, cast or reached through `on.call` are refused, and so is any string
  that names an event Tether hooks (`classic.UserPromptSubmit`, `classic.Stop`, `session.end`, `turn.complete` and
  the rest).
- **A hook that does more than its job.** What the five hooks do is held too, not only their names. `command.run` must
  carry the filter `{ command: 'conclave' }` and `ui.render` the filter `{ component: 'AbovePrompt' }` or `{ component:
  'Pane', requestId: 'conclave' }`, so neither answers one of Tether's commands or draws over one of its panes (a
  literal, or a const that holds `'conclave'`). `session.start` and `agent.offer` must end every path in `next(e)`
  (`agent.offer` may also return `{ isOffered: false }`, the answer that hides a type), and so must the `.catch` of any
  hook; a hook that answers for itself would drop what Tether adds beneath it. `classic.SessionStart` begins `const r =
  await next(e)` and only ever returns `r` from its own body (no function inside it returns or touches `r`), so Tether's
  HANDOFF context comes through whole. In these hooks `next` is only that one call, so the hooks beneath run once (no
  second `next(e)`, no `next.to`, no copy), and `e` is passed on as it came: it is never replaced, changed or deleted
  from, `e` itself goes to no function but `next`, and only a part of it (`e.cwd`) may go to a function of the mod's
  own. Before it passes the event on, `session.start` waits only for the engine's quick calls (any but `$.process.run`,
  directly or in a function of the mod's own), never for the snapshot command or a promise of its own, so a session
  start never waits for Conclave. `$.command.register` takes only the name `'conclave'` and a description, and
  `$.ui.open` only the id `'conclave'` and a title. Each hook (and each `.catch`) is an arrow function written in place
  with at most the three parameters `$`, `e` and `next`, each a plain name (no default, rest or destructuring), which
  the check can read and which has no `arguments`, `this` or extra parameter to reach `e` and `next` another way, and
  the registration `on(...)` returns is used only as a statement or with that `.catch`. No code of the mod waits in a
  `for await` loop or declares with `using`.
- **Engine calls beyond the eleven the mod needs.** Every value of type `EngineInterface` (the `$` of a hook, or the
  parameter of any helper, under any name) may only be the object of `$.noun.method`, called, for the clock, the command
  registration, the one process, the store and the UI, or `$.plugin.root`. So no `$.session.` or `$.model.` call, tool,
  file, prompt or agent API appears. It may be passed on only to a function of the mod's own that declares the
  parameter as `EngineInterface`, or to the `read` and `update` of `claude-code` (the mod's own state). Copying it into
  another name, spreading, destructuring, a computed name (`$['session']`), returning or storing it, putting it in an
  array or object, and any `as` cast of it are refused, in parentheses or not.
- **Anything that hides a type from the check.** `any` anywhere in the mod, a `@ts-ignore`, `@ts-expect-error` or
  `@ts-nocheck` comment, and any type error in a runtime source (the check compiles it against the engine's types).
  Sources are TypeScript only, so there is no plain script the check cannot see into.
- **Code loaded or built at run time**, an `import` of anything but `claude-code` and the mod's own checked sources (an
  import into `tests/` would hide code from the check), and the globals a hooks module has no business with (`fetch`,
  `globalThis`, `process`, `require`, `eval`, `Function`, `Reflect`, `arguments`, the timers and the like). The ways to
  the `Function` constructor are closed too: no `constructor` name or string (`.constructor`, `['constructor']`,
  `'con' + 'structor'`), no prototype reflection (`getPrototypeOf`, `getOwnPropertyDescriptor`, `__proto__`), no
  property read or named by a name computed at run time (`x[k]`, `const { [k]: v } = x`, `{ [k]: v }`, whatever the
  type of `x` says: the mod maps a tone to a colour with a `switch`, not a table) and no `as unknown as` cast, which
  hides a value's type from the check.
- **Other commands.** `$.process.run` takes only the snapshot command, an array that begins `uv run -q --project <dir>`
  and ends `coney-tools conclave snapshot --json`, and that may grow by `--github` and nothing else. The `<dir>` is
  exactly `${checkoutOf($.plugin.root)}/python` with the mod's own `checkoutOf` (so never a flag or another folder),
  and the options are an object with only `cwd`, which is that checkout, `timeoutMs`, and an `env` that holds only
  `UV_PROJECT_ENVIRONMENT`, which is the checkout's own `venvOf(...)`: no other folder, variable or Python, no
  `stdin`, no options passed in a variable.
- **Tether's words.** No string or name in the mod holds the session's usage figures (`context_tokens`,
  `seconds_since_last_response`, `rateLimits`), the transcript's path (`transcript_path`), `/clear`, `HANDOFF` or
  `Continue from`, whether written whole or split over a `+` or a template of constants (`'HAND' + 'OFF'`), and the
  mod never calls `usage(...)`. Comments may name them. Splitting a word by a call (`String.fromCharCode`) is not
  caught, and does not need to be: reading the prompt, a file or the session takes an engine call the list above
  refuses.

Three things are not checked, by design. A word split by a call (above). What a function of the mod's own does with a
part of `e` it is given (today's parts are strings, which cannot be changed). And whether the band's `ui.render` hook
passes the event on: the band answers in place of the hooks beneath it when it has something to draw, which is what a
band is, and Tether does not hook `AbovePrompt`.

Every code file in every subfolder of the mod is checked (only `tests/`, `types/` and the engine's own typings are
not). The check is itself tested: each way across the line that a reviewer found, or that an earlier version of the
check missed, is a case that must fail (`CROSSINGS` in the test), the code the mod really writes is a case that must
pass, and the real sources must pass. A session start never waits for Conclave ([Starting a session](#start)).

**Where it runs.** The check needs Node.js, TypeScript 5.6.3 (fetched once with `npx`) and a description of the engine's
`claude-code` module. The engine's own types are written beside the mod only by a session that loads it (`claude
--plugin-dir tools/claude-mod`); neither `claude plugin validate` nor `claude plugin test` writes them, and CI has no
logged-in Claude Code to load it. So where they are absent the check reads a stub of the module in Coney's own words,
`python/tests/conclave_engine_stub.d.ts`: only what the check looks at (`EngineInterface` and the eleven calls, `On`,
`Register` and the names the mod imports), with no type errors reported. The stub is in the repository (the maintainers
allowed it on 2026-10-23: it holds no key, token, path, name or address, only the shapes the check reasons with, in
Coney's own words), so CI runs the whole check. Where the check cannot run (no Node.js, no TypeScript, or the stub is
gone) its tests skip on a machine and **fail under CI** (the `CI` variable GitHub sets), naming what is missing, because
a skip cannot be told apart from a pass among a pull request's required checks and this check is the only enforcement of
the boundary. Keep the stub free of anything but those shapes. `python/tests/test_conclave_engine_stub.py` holds the
stub against the engine's types whenever both are present (the names and kinds the mod imports, the nouns and methods it
calls, and the verdict on every case of the boundary tests), so the stub cannot drift unseen; the rest of its tests run
on the stub alone.

What Conclave may take over later, because the maintainers allowed it, is Coney's own pollers (pull requests, the
release queue, builds, triage), the stop guard on unowned pull request problems and the pull request ownership store;
each arrives with its own change to `.claude/tether.json` so nothing is told twice ([Planned](#planned)).

## Loading it {#loading}

```sh
claude --plugin-dir <checkout>/tools/claude-mod
```

The flag loads the mod for that session, in every mode and trusted folder or not, and an interactive session reloads
it when a file is saved. The session launcher `coney-agent` is planned to pass the flag itself when the checkout has the
folder, so an agent never types it. A session started without the mod keeps every rule and gets no display.

The mod runs `uv run -q --project <checkout>/python coney-tools conclave snapshot --json` from the checkout its own
folder belongs to, so a worktree's session shows that worktree's units, and it needs `uv` on `PATH`. It sets
`UV_PROJECT_ENVIRONMENT` to `<workspace>/scratch/<branch>/conclave-venv` for that call (`scratch/main/conclave-venv`
for the main checkout), so `uv` never creates `python/.venv` inside a worktree ([Keep Python out of the
tree](how-we-work.md#shared-machine)). The environment lives in the branch's own scratch folder, where the guide
already puts a private environment, and goes when that folder does: `workspace clean` leaves `scratch/` alone, so prune
it with the rest of the branch's scratch ([Scratch](workspace.md#scratch)). The first read in a checkout builds the
environment, and a checkout outside the workspace layout (`repos/coney` or `worktrees/coney/<branch>`) is left to the
caller's own variable.

## Starting a session {#start}

Conclave never makes a session start wait, Tether's included. `session.start` registers `/conclave`, applies the agent
list this checkout saved, starts the first read in the background and returns; the band draws when the read lands. A
read that hangs is killed after 30 seconds (60 with `--github`), and a failed one is tried again. `classic.SessionStart`
passes its event on first and returns the answer of the plugins beneath whole, and `agent.offer` passes its event on
unless the type is hidden; both keep a `.catch` that passes the event on, so a failure in Conclave leaves that answer
untouched.

## The snapshot {#snapshot}

[`coney-tools conclave snapshot`](coney-tools-setup.md#conclave) prints one JSON object, read-only. The mod reads it on
two rhythms, and every number is a `conclave` key of [.github/conventions.yml](repo:.github/conventions.yml) that the
snapshot carries in `config`, except the three the mod needs before any snapshot exists and holds itself in
`src/snapshot.ts`: the first retry (15 s) and the two read time limits (30 s, 60 s with `--github`):

| Read | Command | Every | Cost measured |
| --- | --- | --- | --- |
| Local | `snapshot --json` | `local_seconds` (15) | about 1 s warm |
| GitHub | `snapshot --json --github` | `github_seconds` (60) while the pane is shown, `github_idle_seconds` (300) while it is not | about 6.6 s, a handful of `gh` calls |

A read never starts while the last one runs. A failed read keeps the last good one, and the band marks it stale; a local
read keeps the pull requests and claim overlaps of the last GitHub read.

| Section | Holds | Read from |
| --- | --- | --- |
| `config` | The roles and helpers of the hook settings, the intervals and limits | `.claude/hooks/hooks.toml`, the `conclave` keys |
| `units` | Each unit: task file, stage from the stamp log, build, worktree, overlaps, pull request | `scratch/<task>/TASK.md`, the stamp log, `coord status` |
| `builds`, `machine` | Build slots and queues; free disk with its two limits | `build status`, the workspace drive |
| `claims`, `prs`, `issues` | Held claims with deadlines and titles; open and merged pull requests; the issues the pane names (`--github` only) | the claim cards, `repo pr-health`, `gh` |

A section that cannot be read holds only its `error`, and the others stand. The object carries `schema`; the mod refuses
a higher number than it was built for. A unit's **stage** is the step after the highest one done in the stamp log (a
step whose latest stamp is `pass` or `none`), and it is `failed` when the latest stamp of that step failed; the step
names are `stamps.steps`, never listed in the mod.

## Hiding agent types {#agent-types}

`agent.offer` hides every agent type that is neither a role (with its aliases) nor a helper in
`.claude/hooks/hooks.toml`, so `general-purpose` and the rest are not offered to the model. Hiding is a convenience: a
model can still name a hidden type, and the rule that refuses it is a hook, never the mod. The three role files stay in
`.claude/agents/`, so `claude --agent coordinator` works with or without the mod. A checkout without `hooks.toml` (the
hook dispatcher brings it) reports the reason in `config.error` and hides nothing.

The list is frozen: a listing that changed mid-session would rewrite the prompt cache, and a session (`claude -p`
included) never waits for a read to learn it. So a session hides by the list its checkout saved last, from its first
turn. Each checkout saves its own, under a key that names it (`offer:<checkout>`), because each has its own
`hooks.toml`: a worktree with other roles never changes what the main checkout hides. The first session in a checkout,
with no list saved, hides nothing; its first read saves the list. A change to `hooks.toml` takes effect where a new
prompt cache starts: the next session, or a `/clear` (Tether's automatic one included), which takes up the list saved
since. A compaction and a resumed conversation keep the cache of the listing they have, so they keep the list.

One read ends the freeze early: a good read that says the settings cannot be read (no file, a bad one, no role) drops
the checkout's saved list and the frozen one, so a list from an earlier session never hides anything the settings no
longer ask for. The listing changes once in that case, which is the price of not hiding by a stale list. A failed read,
one that printed nothing usable, changes nothing.

## Versions and checks {#checks}

`plugin.json` carries the mod's version, and `conclave.claude_code` the Claude Code version it is tested on. The snapshot
carries that version, but the mod does not compare it yet (a note in the band for an older engine is planned). The mod's
types are the contract in `tools/claude-mod/types/index.d.ts`. Run these from the checkout:

```sh
uv run --project python pytest python/tests/test_conclave_snapshot.py python/tests/test_conclave_mod.py python/tests/test_conclave_fixture.py
claude plugin validate tools/claude-mod
npx -p typescript@5.6.3 tsc -p tools/claude-mod
claude plugin test tools/claude-mod
```

`tsc` reads the typings the engine lays in `tools/claude-mod/.claude-plugin/types/` (it ignores them in git) the first
time it loads the mod, so start `claude --plugin-dir tools/claude-mod` once before it. `claude plugin test` draws the band
and the pane on several surfaces from `tests/fixtures/snapshot.ts`, which `python/tests/test_conclave_fixture.py`
writes from the same synthetic units the snapshot tests use (run it with `CONEY_UPDATE_FIXTURES=1` after a change to the
snapshot), so the mod and the command share one shape. `claude plugin validate` finds a state reference outside
`src/state.ts` and `$` passed to a function that is not a declaration, which the engine would refuse.

## Planned {#planned}

The later steps each change `.claude/tether.json` in the same pull request so nothing is told twice:

1. **Watchers**: event ids in the snapshot, wakes for the coordinator about Coney's own events (a failed check, a
   merge, a unit ready for `pr open`, a question), toasts, the stop guard on pull request problems nobody owns, and
   `coney-tools pr hold`. Never an idle wake.
2. **Briefs and tools**: a fixed prompt section per role, a unit brief for each engineer or reviewer started, and the
   `conclave_ask` and `conclave_unit` tools for the model.
3. **More panes**: pull requests, questions and the machine (PCSX2 copies), and the overlap check from the files each
   worktree changed.
4. **The dispatcher**: its cost per call measured, and a resident dispatcher only if the number says so.
