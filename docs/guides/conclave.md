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
| `PRs 4 (✖1 #812)` | Open pull requests, and the first one with a failed check or a cause from [`repo pr-health`](coney-tools.md#repo-pr-health) | red when any failed |
| `claims 3` | Held claims of every contributor | amber from the warning hours, red in the last `claim_red_hours` of any claim, once expiry is enforced |
| `builds 2 (+3 queued)` | Build slots in use and builds waiting, over every lane | none |
| `⚠ P-critical #790` | The first open issue labelled `P-critical`, and how many more | amber |
| `disk 61 GB` | Free space on the workspace drive | amber under `workspace.disk_min_free_gb` + `workspace.disk_margin_gb`, red under `workspace.disk_min_free_gb` |

A snapshot older than `stale_after_reads` (3) local intervals dims the band and adds `stale 4m`. A failed first read
shows the first line of the error; a snapshot of a newer shape than the mod knows shows "update the checkout" and
nothing else.

**The status line** reads `role · unit · stage` for a session in a unit's worktree (`engineer · fix-frame-812 ·
compared`), `coordinator · 2 units · ✖1` for the coordinator, and `no role: start with coney-agent` for a session that
started with no `--agent`. The role is the session's agent type, which the mod reads from the start of the session.

**The pane** (`/conclave`, or a press on the band) has two sections. It needs about 110 columns (`pane_min_columns`);
below that it says so and the band stays. It opens by itself at the start of a session where the terminal is wide enough
to place an unasked pane, and waits for `/conclave` where it is not.

- **Units and issues** lists every unit (a scratch folder with a task file, with a worktree or changed in the last
  `unit_days`) and the issues the signed-in account holds with no unit here. A row is one line: a mark, the issue
  numbers, the title, who works on it, the stage, the build it runs or waits for, the pull request and its age. A press
  opens the row, one at a time, and shows the claimant and agent, the status and time in stage, the claim's expiry,
  the scope and the units or claims it overlaps, every stamp step (`✔` done, `✖` failed, `–` not needed, `·` missing),
  the reason a failed row is red, the last detached build and the links. Failed rows come first, then active, not
  started and merged ones (merged units stay for `merged_visible_minutes`).
- **Claims** lists every held claim with its countdown. The countdown runs on the mod's own clock, once a second while
  the pane is shown, from the deadline in the snapshot, so the display never waits for a read. A claim whose release
  clock waits (a `S-blocked` or `S-needs-info` issue) says why instead of a time, and while `claims.release` is off the
  line reads "would expire in ..." because nothing releases it yet.

Marks are single-width coloured symbols (`⚠ ● ✔ ✖`) on the terminal, which depends on the font, and emoji on the
desktop, VS Code and mobile surfaces.

## The boundary: what Conclave leaves to Tether {#boundary}

Session health belongs to [Tether](coney-tools.md#coord-wakes): the context window, the prompt cache and its expiry, the
automatic `/clear` and the "Continue from HANDOFF.md" message, subagents past their context limit, the checkpoint cycle
and the cache guard. **Conclave shows no context or cache figure, sends no wake about them and adds no hook that
overlaps them.** The idle wake stays in Tether too, now and in later versions. `python/tests/test_conclave_mod.py` holds
the line, so a change that crosses it fails the Python tests:

- the mod hooks exactly five events: `session.start`, `command.run`, `agent.offer`, `ui.render` and `classic.SessionStart`
  (which only reads the session's agent type and passes the event on unchanged and first). The check reads every
  `on(...)` call whatever quote it is written in, refuses an event that is not a plain string, a renamed or passed-on
  `on`, and any quoted name of an event Tether hooks (`classic.UserPromptSubmit`, `classic.Stop`, `session.end`,
  `turn.complete` and the rest);
- the mod calls only the twelve `$.noun.method` it needs (the clock, the command registration, the plugin's folder, the
  one process, the store and the UI), so no `$.session.` or `$.model.` call, tool, file, prompt or agent API appears,
  and `$` is never copied, destructured or read by a computed name;
- no source names the session's usage figures, the prompt, a stop, a compaction, the transcript, `/clear` or the
  HANDOFF message;
- every code file in every subfolder of the mod is scanned (only `tests/`, `types/` and the engine's own typings are
  not), and the check is itself tested with a case for each way across the line, so a rule that stops catching fails;
- the only command the mod runs is the read-only snapshot, in one place (`src/snapshot.ts`);
- a session start never waits for Conclave ([Starting a session](#start)).

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
`UV_PROJECT_ENVIRONMENT` to `<workspace>/scratch/conclave/<checkout folder>/venv` for that call, so `uv` never creates
`python/.venv` inside a worktree ([Keep Python out of the tree](how-we-work.md#shared-machine)); the first read in
a checkout builds that environment, and a checkout outside the workspace layout (`repos/coney` or
`worktrees/coney/<branch>`) is left to the caller's own variable.

## Starting a session {#start}

Conclave never makes a session start wait, Tether's included. `session.start` registers `/conclave`, applies the agent
list the last session stored, starts the first read in the background and returns; the band draws when the read lands.
A read that hangs is killed after 30 seconds (60 with `--github`), and a failed one is tried again. The two classic
hooks (`classic.SessionStart` and `agent.offer`) pass their event on first and keep a `.catch` that does the same, so a
failure in Conclave leaves the answer of the plugins beneath whole.

## The snapshot {#snapshot}

[`coney-tools conclave snapshot`](coney-tools.md#conclave) prints one JSON object, read-only. The mod reads it on two
rhythms, and every number is a `conclave` key of [.github/conventions.yml](repo:.github/conventions.yml) that the
snapshot carries in `config`, except the three the mod needs before any snapshot exists and holds itself in
`src/snapshot.ts`: the first retry (15 s) and the two read time limits (30 s, 60 s with `--github`):

| Read | Command | Every | Cost measured |
| --- | --- | --- | --- |
| Local | `snapshot --json` | `local_seconds` (15) | about 1 s warm |
| GitHub | `snapshot --json --github` | `github_seconds` (60) while the pane is shown, `github_idle_seconds` (300) while it is not | 8 to 14 s, a handful of `gh` calls |

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

The list is frozen for the session: a listing that changed mid-session would rewrite the prompt cache, and a session
(`claude -p` included) never waits for a read to learn it. So a session hides by the list the previous session stored,
from its first turn. The first session in a checkout, with no list stored, hides nothing; its first read stores the list
for the next one. A change to `hooks.toml` takes effect in the next session as well.

One read ends the freeze early: a good read that says the settings cannot be read (no file, a bad one, no role) drops
the stored list and the frozen one, so a list from an earlier session never hides anything the settings no longer ask
for. The listing changes once in that case, which is the price of not hiding by a stale list. A failed read, one that
printed nothing usable, changes nothing.

## Versions and checks {#checks}

`plugin.json` carries the mod's version, and `conclave.claude_code` the Claude Code version it is tested on. The mod's
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
