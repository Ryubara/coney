# Conclave, the Claude Code mod

Conclave is Coney's mod for Claude Code, a folder in the repository (`tools/claude-mod/`) that a session loads to get
three things the shell cannot give it: a band above the prompt, a status line and a pane that stay on screen and update
by themselves, and a shorter list of agent types for the model to choose from. It **draws and hides, and tells the
coordinator what Coney itself raised; it never decides, and it writes nothing of the repository or of the
tools' state.** What it does write is its own: the agent list this checkout saved and the events it has told, in the
engine's store, and a wake, a toast or a stop-guard reason for the coordinator ([The watchers](#watchers)). Every fact
it shows comes from one command,
`coney-tools conclave snapshot`, so the mod holds no second copy of the formats that command reads (the task file, the
stamp log, the claim cards, the hook settings), and every rule it displays is still held by the hooks, `coney-tools` and
CI without it. A session started without the mod loses the display and the shorter list, and nothing else.

It has no tools for the model yet. In a coordinator's session it also watches Coney's own events
([The watchers](#watchers)): a toast and one wake for each event the first time it is seen, and a stop guard on pull
request problems nobody holds. What comes next is under [Planned](#planned).

## What it shows {#shows}

**The band** sits above the prompt, one line on a wide terminal and two on a narrow one (under `band_narrow_columns`,
100). Each item is a button that opens the pane, and an item whose count is zero is left out:

| Item | Says | Colour |
| --- | --- | --- |
| `2 units`, `✖1 failed` | Units with a task file that are not merged: their count, and how many failed a step, have a pull request whose checks failed or have a task file that cannot be read | red when any failed |
| `PRs 4 (✖1 #812)` | Open pull requests, and the first one with a failed check or a cause from [`pr list-stuck`](coney-tools-pr.md#pr-list-stuck) | red when any failed |
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
| From `pane_min_columns` (30; the conventions refuse less than 15, where the narrow layout's fixed parts stop fitting) | **Narrow**: as compact, but a row's first line keeps only the mark, the issue (the task when it names none) and the stage; the pull request, who, the build and last the title go on the second line. A claim reads `#741 2d 14h (warned) contrib-a`: the bare countdown, `~2d 14h` while expiry is not enforced (it would expire then), or `waits: S-blocked`, ahead of the claimant. |
| Under `pane_min_columns` | **One line**: the band's items joined by `·`, clipped, in the colour of the worst of them. |

In the compact and narrow layouts every line is clipped to the width with an ellipsis, counted in terminal columns (an
emoji or an East Asian wide character takes two), so the most decision-relevant text (the issue, the stage, the
countdown) comes first; in the narrow layout the title ends the second line, so it is what a clip cuts first. The
details of an open row wrap under their label, so nothing an open row says is lost to the width. The pane is never
empty and never refuses a width. The words (stage, build, pull request, counts) are the band's and the status line's
in every layout. The pane opens by itself at the start of a session where the terminal is wide enough for the engine to
place an unasked pane, and waits for `/conclave` where it is not; the engine seats a pane the person opens at any width.

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

## The watchers {#watchers}

Between turns a session sees nothing, so what Coney itself raises would wait until someone typed. In a coordinator's
session Conclave tells it. The events come from the `events` section of the snapshot ([The snapshot](#snapshot)), each
with an `id`, a `kind`, a `state` and the line to say; the mod adds a toast, a wake and a stop guard, and decides
nothing: what changes an event's state is a command of the coordinator's own (`pr hold`, `pr open`, a written answer).
The sessions that watch are the `conclave.watch.roles` of [.github/conventions.yml](repo:.github/conventions.yml)
(`coordinator`); every other session, and one started with no role, is told nothing. **Never an idle wake**: a session
with nothing new is left alone, and the idle wake stays Tether's ([boundary](#boundary)).

| Kind | Raised when | Id (one occurrence) | Handled when | The wake says |
| --- | --- | --- | --- | --- |
| `check-failed` | An open pull request has a failed required check ([`pr list-stuck`](coney-tools-pr.md#pr-list-stuck), cause `checks-failed`) | `pr:812:checks-failed:<head>:<checks>`: the first seven characters of the head commit and a short hash of the failing checks, so a new push or another failing check is a new event | A running hold covers the cause on that head ([`pr hold`](pr.md#hold)); a hold that runs out, or a new push, opens it again | Fix it or start an agent, then `pr hold <n> --who <agent>` |
| `pr-stuck` | Any other `pr list-stuck` cause: a conflict, a CI run that never ends, auto-merge off | `pr:812:conflict:<head>` | The same | The same |
| `merged` | A pull request merged within `merged_minutes` (30) | `merged:<n>` | Never: it leaves the list when the window ends | Bring the issues it affected up to date ([After a merge](issues.md#after-a-merge)) |
| `unit-ready` | A release-queue file (`PR.md`, `PR-<n>.md`) in the scratch folder of a unit with a worktree, its branch has no pull request since the file changed, no `HOLD.md` lies beside it and [`pr open` would pass its proof](#ready) | `unit:<branch>:<file>:<change>`: the first twelve digits of the [change](stamps.md#change) the stamps cover, so editing the file or rebasing the branch is the same event and a new commit is a new one | The branch gets a pull request: it leaves the list. The proof stops holding, or a hold is made: it leaves the list too, and is told again if it holds again | `coney-tools pr open <task>` |
| `question` | `QUESTION.md` or `QUESTION-<n>.md` in the scratch folder of a task unit or of any branch with a worktree ([Questions](#questions)) | `question:<task>:<file>:<time the file changed>` | An `ANSWER.md` (`ANSWER-<n>.md`) written at or after the question's last change | Read it and answer |

An event that can come back once handled (a pull request problem, when a hold runs out) stays in the list as
`handled` with who holds it and until when, so the mod can tell the change. The others leave the list when they are
handled. A kind whose source could not be read is named in `unread`: what is missing from it says nothing.

**Told once, until its state changes.** The mod remembers the events it has seen across sessions (`$.store`, the key
`watch`): each one's id, when it was first seen and when it was woken for. After each read:

- an open event not seen before is *new*: it gets a toast and is gathered for the next wake;
- an event already seen is told no more, however often the snapshot repeats it;
- an event that is `handled`, or no longer listed (unless its kind is in `unread`), is forgotten, so the same event
  opening again (a hold that ran out) is new again, and a handled or vanished one is never told;
- a failed `events` section, or a list whose last GitHub read is older than three `watch.github_seconds`, is not acted
  on at all.

**The toast** goes the moment an event is first seen: one for each of up to three new events, or one count when
there are more. It never waits for a draft in the prompt box.

**The wake** is one `$.prompt.submit` of the `watch.preamble` and a line for each event not yet woken, `- <what
happened> -> <what to do>`; the engine starts a turn for it once the session is idle, never during one. It waits for
`wake_gap_seconds` since the last wake (60), for a draft in the prompt box for up to `defer_typing_minutes` (10, from
when the oldest of the events was first seen, then it goes anyway) and, after the engine refused one (a person
interrupted a turn), for `resend_minutes` (2). The events are recorded as woken before the wake is sent, so a session
that ends meanwhile never tells them twice; a refused wake puts them back. A watching session reads GitHub every
`watch.github_seconds` (120) with the pane closed (60 while it is open), so an event reaches the coordinator in about two
minutes.

**The stop guard** (`classic.Stop`) keeps the coordinator from ending a turn while an open pull request has a problem
nobody holds: it blocks with the `watch.stop_preamble` and a line for each problem. Before it blocks it reads GitHub
again (about 7 seconds, only when the last read had such a problem), so a hold recorded a moment ago, or a problem that
fixed itself, counts. It blocks at most `stop_max_blocks` (3) turn ends in a row for one reason, counts again from one
after `stop_reset_minutes` (30) without a block, never blocks on a list it could not make current, and lets the turn
end when it fails itself. Tether's own Stop guard for stopped agents is untouched: Conclave's block rides on the
answer beneath it, and a block already set there is kept whole, with Conclave neither reading GitHub nor counting a
block for that turn end ([boundary](#boundary)).

### When a unit is ready {#ready}

A `PR.md` in a unit's scratch folder says the engineer wrote the description, not that the unit can be released: the
first live wake (2026-10-26) listed twenty units for one that `pr open` would have opened, the rest still checking, with
a failed or missing review, with a review that no longer covered the change after a rebase, or held on purpose. So a
file in the release queue is only a candidate, and the unit is **ready** when the proof `pr open` makes before it pushes
([step 4](pr.md#open): `pr verify`'s stages and `pr text`'s checks) finds nothing to refuse. It is one function,
`pr_ready.readiness`, which `pr open` itself runs (`pr_ready.prove`), so the wake and the command cannot disagree about
the stamps. Not covered are the checks of `pr open` that depend on GitHub or on the moment of release: the claim, a
merged or closed pull request and the rebase, which `pr open` makes and reports as it always did.

The proof is asked from the cheapest reading to the dearest and stops at the first refusal, so a read of twenty
candidates costs about what one ready unit does: the stamp log alone (no passing `checked` or `reviewed` stamp: a check
still running, a failed or missing review), then the change as git sees it (a Check or Review made for another change,
which is what a rebase or a new commit does to a stamp, or uncommitted work in the worktree), and only a unit whose
stamps cover its change has its `@orig` functions read and its description checked in full. A change that touches no C++
file under `src/` has none to read, so that unit's whole proof takes about half a second; a change under `src/` takes
seconds, and is asked again at every GitHub read until the unit gets its pull request. A folder with no task file is not
a task unit (`pr open` refuses it), and a proof that could not be read at all (an unknown default branch, unreadable
rules) names `unit-ready` in `unread` rather than saying "not ready". The read writes nothing: git runs with no
optional locks, so it never makes an engineer's `git commit` fail.

### Holds {#holds}

[`coney-tools pr hold`](pr.md#hold) is how a pull request's problems get an owner. `pr hold 812 --who engineer-2` says
that engineer-2 is fixing #812 for `hold_default_hours` (4; at most `hold_max_hours`, 24); without `--who` it is a
waiver, a known wait, and `--note` says why. A hold covers every cause of the pull request or the one `--cause` names,
and the head commit it was made on: a new push is a new problem, so the hold does not follow it and the problem is
open and told again. A hold is one file of the scratch directory (`conclave.watch.holds_dir`, one file per pull
request, written whole), and ends by itself: when it runs out the problem is open again and is told again, so a fix
that stalls cannot hide. Only the coordinator holds: `hooks.toml` refuses `pr hold` to the other roles, though the
hold is only a file, so a role that writes it by hand is not stopped by that rule (the file is the coordinator's by
convention, [Enforcement](enforcement.md)). This replaces Tether's `pr_owner` tool and owner record, and the
`.coney-busy` marker that Tether's `prs` poller took as an owner: only a hold names an owner now.

**Holding a unit.** A unit that is ready but must wait on purpose (a stale unit waiting for capacity, a stacked branch
waiting for its base) is held by a file: `HOLD.md` (`conclave.watch.unit_hold_file`) in its scratch folder, with a line
that says why. While the file is there the unit raises no `unit-ready` event, and when the coordinator deletes it the
unit is ready again and is told once more. Unlike a hold on a pull request it has no time and no command: it is a
decision of the coordinator's, to lift by hand, like an `ANSWER.md`, and the engineer sees why in its own folder. It
holds back only the `unit-ready` event of that folder; a question the unit asks is still told.

### Questions {#questions}

An agent that needs the coordinator's answer while it works, and cannot go on without it, writes `QUESTION.md` (or
`QUESTION-<n>.md` for several) in its scratch folder, first line the question; the coordinator answers in `ANSWER.md`
(`ANSWER-<n>.md`) beside it, or by message, and writes the file when it does. The scratch folders read are those of the
task units and of every branch that has a worktree (most tracks are plain branches, as for `unit-ready`); a folder with
no worktree is history. The names are `conclave.watch.question_prefix` and `answer_prefix`. The engineer's rules say
when to ask ([Agents](agents.md#roles)). The `conclave_ask` tool of a later step ([Planned](#planned)) will write these
same files.

### Tether's pollers {#pollers}

The watchers replace two pollers of the coordinator's Tether watch settings, `release` (the release queue:
`unit-ready`) and `prs` (stuck pull requests, Tether's PR guard and its owner records: `check-failed`, `pr-stuck` and
the stop guard), so nothing is told twice. They are gone from the block in [Waking the
coordinator](coney-tools-setup.md#coord-wakes), and the coordinator deletes them from its own file when it loads the
mod with the watchers (until then both tell the same event).
`triage`, `builds` and the subagent pollers stay Tether's.

## The boundary: what Conclave leaves to Tether {#boundary}

Session health belongs to [Tether](coney-tools-setup.md#coord-wakes): the context window, the prompt cache and its
expiry, the automatic `/clear` and the "Continue from HANDOFF.md" message, subagents past their context limit, the
checkpoint cycle and the cache guard. **Conclave shows no context or cache figure, sends no wake about them and adds no
hook that overlaps them.** The idle wake stays in Tether too, now and in later versions.
`python/tests/test_conclave_mod.py` holds the line, so a change that crosses it fails the Python tests. The check does
not read spellings: it builds the mod with the TypeScript compiler (`python/tests/conclave_boundary.cjs`) and judges the
tree and the types, so a renamed `$`, an alias, a spread or a cast cannot hide a call. It refuses:

- **Hooks other than the six.** The mod hooks exactly `session.start`, `command.run`, `agent.offer`, `ui.render`,
  `classic.SessionStart` (which only reads the session's agent type, passes the event on first and returns the answer
  whole) and `classic.Stop` (the stop guard on pull request problems nobody holds, which keeps the answer beneath
  whole). Every value of the engine's type `On` (the `on` of `register`, whatever it is called) may only be *called*,
  with a plain string literal that is one of those six: a hook on another event, a template or a variable as the event,
  `on` renamed, passed on, put in an object, cast or reached through `on.call` are refused, and so is any string that
  names an event Tether hooks (`classic.UserPromptSubmit`, `classic.SubagentStop`, `session.end`, `turn.complete` and
  the rest).
- **A hook that does more than its job.** What the six hooks do is held too, not only their names. `command.run` must
  carry the filter `{ command: 'conclave' }` and `ui.render` the filter `{ component: 'AbovePrompt' }` or `{ component:
  'Pane', requestId: 'conclave' }`, so neither answers one of Tether's commands or draws over one of its panes (a
  literal, or a const that holds `'conclave'`). `classic.Stop` has the one form `const r = await next(e)`, `const reason
  = r.block === undefined ? await guard($) : null` (a function of the mod's own, asked with `$` alone and only when no
  block is set beneath) and `return reason === null ? r : { ...r, block: reason }`, so it adds the guard's block and
  nothing else to the answer beneath, a block already there (Tether's) is never replaced, and `e` goes nowhere but
  `next`.
  `session.start` and `agent.offer` must end every path in `next(e)` (`agent.offer` may also return `{ isOffered: false
  }`, the answer that hides a type), and so must the `.catch` of any hook; a hook that answers for itself would drop
  what Tether adds beneath it. `classic.SessionStart` begins `const r = await next(e)` and only ever returns `r` from
  its own body (no function inside it returns or touches `r`), so Tether's HANDOFF context comes through whole. In these
  hooks `next` is only that one call, so the hooks beneath run once (no second `next(e)`, no `next.to`, no copy), and
  `e` is passed on as it came: it is never replaced, changed or deleted from, `e` itself goes to no function but `next`,
  and only a part of it (`e.cwd`) may go to a function of the mod's own. Before it passes the event on, `session.start`
  waits only for the engine's quick calls (any but `$.process.run`, directly or in a function of the mod's own), never
  for the snapshot command or a promise of its own, so a session start never waits for Conclave. `$.command.register`
  takes only the name `'conclave'` and a description, and `$.ui.open` only the id `'conclave'` and a title. Each hook
  (and each `.catch`) is an arrow function written in place with at most the three parameters `$`, `e` and `next`, each
  a plain name (no default, rest or destructuring), which the check can read and which has no `arguments`, `this` or
  extra parameter to reach `e` and `next` another way, and the registration `on(...)` returns is used only as a
  statement or with that `.catch`. No code of the mod waits in a `for await` loop or declares with `using`.
- **Engine calls beyond the fourteen the mod needs.** Every value of type `EngineInterface` (the `$` of a hook, or the
  parameter of any helper, under any name) may only be the object of `$.noun.method`, called, for the clock, the command
  registration, the one process, the prompt box, the store and the UI, or `$.plugin.root`. The prompt box is read bare
  (`$.prompt.read()`, for a draft) and written only by the wake, `$.prompt.submit({ text })`: the text alone, no other
  option; a toast is `$.ui.toast(text)`. So no `$.session.` or `$.model.` call, tool, file or agent API appears, nor any
  other prompt call. **A wake is sent from one place only, the event-wake path.** `$.prompt.submit({ text })` is called
  with the parameter of a function declaration of the mod's own (the wake function), which must be called exactly once,
  and only called; that call sends `wakeText(<preamble>, due)` where `due` is a const given by `pending(...)`, in the
  `then` of `if (due.length > 0)`, directly in a function declaration (the watcher), not in a closure or a timer inside
  it; and the watcher may be referenced only by calls inside the callback given to `startPolling`, which runs after a
  snapshot read. So a timer, a session start, a fixed text or text made up on the spot cannot send a wake: only a
  snapshot's events due can. The cases that must fail are in `CROSSINGS`, among them a `$.clock.every` that submits a
  fixed text and the watcher called from a timer. The engine may be passed on only to a function of the mod's own that
  declares the parameter as `EngineInterface`, or to the `read` and `update` of `claude-code` (the mod's own state).
  Copying it into another name, spreading, destructuring, a computed name (`$['session']`), returning or storing it,
  putting it in an array or object, and any `as` cast of it are refused, in parentheses or not.
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

Four things are not checked, by design. A word split by a call (above). What a function of the mod's own does with a
part of `e` it is given (today's parts are strings, which cannot be changed). Whether the band's `ui.render` hook
passes the event on: the band answers in place of the hooks beneath it when it has something to draw, which is what a
band is, and Tether does not hook `AbovePrompt`. And that `startPolling` calls its callback only after a read, and that
`pending` returns only events not yet woken: the wake path is confined to them, and mod tests (an idle snapshot never
submits, a wake is sent once per event) hold what they do.

Every code file in every subfolder of the mod is checked (only `tests/`, `types/` and the engine's own typings are
not). The check is itself tested: each way across the line that a reviewer found, or that an earlier version of the
check missed, is a case that must fail (`CROSSINGS` in the test), the code the mod really writes is a case that must
pass, and the real sources must pass. A session start never waits for Conclave ([Starting a session](#start)).

**Where it runs.** The check needs Node.js, TypeScript 5.6.3 (fetched once with `npx`) and a description of the engine's
`claude-code` module. The engine's own types are written beside the mod only by a session that loads it (`claude
--plugin-dir tools/claude-mod`); neither `claude plugin validate` nor `claude plugin test` writes them, and CI has no
logged-in Claude Code to load it. So where they are absent the check reads a stub of the module in Coney's own words,
`python/tests/conclave_engine_stub.d.ts`: only what the check looks at (`EngineInterface` and the fourteen calls, `On`,
`Register` and the names the mod imports), with no type errors reported. The stub is in the repository (the maintainers
allowed it on 2026-10-23: it holds no key, token, path, name or address, only the shapes the check reasons with, in
Coney's own words), so CI runs the whole check. Where the check cannot run (no Node.js, no TypeScript, or the stub is
gone) its tests skip on a machine and **fail under CI** (the `CI` variable GitHub sets), naming what is missing, because
a skip cannot be told apart from a pass among a pull request's required checks and this check is the only enforcement of
the boundary. Keep the stub free of anything but those shapes. `python/tests/test_conclave_engine_stub.py` holds the
stub against the engine's types whenever both are present (the names and kinds the mod imports, the nouns and methods it
calls, and the verdict on every case of the boundary tests), so the stub cannot drift unseen; the rest of its tests run
on the stub alone.

What Conclave took over, because the maintainers allowed it, is Coney's own pull request and release queue pollers,
the stop guard on pull request problems nobody holds and the pull request ownership store ([The
watchers](#watchers)). It may take over the builds and triage pollers later; each arrives with its own change to the
coordinator's watch settings so nothing is told twice ([Planned](#planned)).

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
| GitHub | `snapshot --json --github` | `github_seconds` (60) while the pane is shown, `github_idle_seconds` (300) while it is not; a session that watches reads on `watch.github_seconds` (120) instead of the idle interval | about 6.6 s, a handful of `gh` calls |

A read never starts while the last one runs. A failed read keeps the last good one, and the band marks it stale; a local
read keeps the pull requests, claim overlaps and events of the last GitHub read.

| Section | Holds | Read from |
| --- | --- | --- |
| `config` | The roles and helpers of the hook settings, the intervals and limits | `.claude/hooks/hooks.toml`, the `conclave` keys |
| `units` | Each unit: task file, stage from the stamp log, build, worktree, overlaps, pull request | `scratch/<task>/TASK.md`, the stamp log, `coord status` |
| `builds`, `machine` | Build slots and queues; free disk with its two limits | `build status`, the workspace drive |
| `claims`, `prs`, `issues` | Held claims with deadlines and titles; open and merged pull requests; the issues the pane names (`--github` only) | the claim cards, `pr list-stuck`, `gh` |
| `events` | The [watchers' events](#watchers), each with an id and a state, the kinds that could not be read (`unread`) and any hold file that could not (`holds_problems`) (`--github` only) | `pr list-stuck`, the release queue with the proof of [each unit in it](#ready), the holds (`pr hold`, `HOLD.md`), the question files of the unit folders |

A section that cannot be read holds only its `error`, and the others stand. The object carries `schema` (2 since the
events); the mod refuses a higher number than it was built for. A unit's **stage** is the step after the highest one
done in the stamp log (a
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

The later steps each change the coordinator's watch settings (the block in [Waking the
coordinator](coney-tools-setup.md#coord-wakes)) in the same pull request so nothing is told twice. The first step, the
[watchers](#watchers), is done.

1. **Briefs and tools**: a fixed prompt section per role, a unit brief for each engineer or reviewer started, and the
   `conclave_ask` and `conclave_unit` tools for the model.
2. **More panes**: pull requests, questions and the machine (PCSX2 copies), and the overlap check from the files each
   worktree changed.
3. **The dispatcher**: its cost per call measured, and a resident dispatcher only if the number says so.
