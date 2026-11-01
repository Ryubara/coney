# Keeping the places full

The engineer places ([Engineer tracks](agents.md#slots): the machine's build slots times `per_build_slot`, shared by the
reimplementation, fix, networking, renderer and Coney Studio tracks) are a ceiling and a floor. This page is the floor:
what makes a free place a failure, the reasons that excuse one, the waivers, and the `Stop` hook that holds the
coordinator to it. The commands are in [`task`](coney-tools-repo.md#task); the hook is listed with the others in [The
hooks](agents.md#hooks) and the [Enforcement map](enforcement.md#hooks).

A maintainer's rule is that **the places are always full unless there is a good reason**, and that the rule is
enforced, not instructed: a coordinator once ended its turn with one engineer at work, two places free and issues
ready. `.claude/hooks/slot_fill_guard.py`, a `Stop` hook of the coordinator, refuses to let a turn end in that
state. It asks `coney-tools task slots --fill-check`, which decides; run it by hand to see where the places stand.

**Which places are free.** The tool counts the units in work the way `task start` and the Agent guard do
(`task_slots.scan`, with the labels from the claim cards) and takes the machine's limit from `build_slots` in
`coney.local.toml`. A free place is one of five kinds:

| Free place | What fills it |
| --- | --- |
| **Kept for game work** (the reimplementation track runs no `feature` unit: its `min` is kept, and a tooling or docs unit does not count) | A claimed task of a kind in `min_kinds` (`feature`) with no agent, or a ready issue outside Coney Studio whose default task kind is one (`task create` suggests `tooling` for `A-tools` and `docs` for `docs`, so those wait for an open place) |
| **Kept for a fix** (the fix track runs no unit: its `min` is kept) | A queued `kind: fix` task ([which fixes are queued](#fix-tasks)), then each open, non-draft pull request of the account with a failed required check that no fix task (by the issues the pull request closes) and no running [`pr hold`](pr.md#hold) covers |
| **Kept for networking** (the networking track runs no unit: its `min` is kept) | A claimed networking task with no agent, then a ready issue labelled `networking` (`claims.networking_label`). The track holds at most one unit (`max`), so networking work never takes an open place; while a unit runs, the check says the track is full |
| **Kept for the renderer** (the renderer track runs no unit: its `min` is kept) | A claimed renderer task with no agent, then a ready issue labelled `renderer` (`claims.renderer_label`). The track holds at most one unit (`max`), so renderer work never takes an open place, and the check says the track is full while a unit runs |
| **Open** (the rest of the machine's free places) | Coney Studio work, or game work beyond the kept one: a claimed task with no agent (a task file with no unit record and no live reservation, whose claim card is still held and not paused), then the ready issues, most urgent first |

The places kept for game work, for a fix, for networking and for the renderer are never filled with Studio work, as the
Agent guard keeps them from Studio; the one kept for networking takes only networking work, the one kept for the
renderer only renderer work, the one for game work only game work. Most
urgent first is `tasks.start.fill.priority_order`: `P-critical` first, and while an unclaimed `P-critical` issue is
ready only it is startable, as the claim Action demands ([Claiming](issues.md#claiming)).

**The start gate** is a key the tool reads (`tasks.start.fill.gate` in
[.github/conventions.yml](repo:.github/conventions.yml)): with `enabled`, of the ready issues only those with a gate
label, the listed issue numbers and `P-critical` issues are eligible. It carried the "workflow first" rule of
2026-10-27 and is off since the engineer tracks, which keep a place for game work and give Coney Studio the rest; it
stays as the way to narrow what starts, by a one-line pull request the coordinator merges, on the maintainer's
behalf, like the other conventions.

A ready issue or claimed task labelled `workflow` is not work for any place while `tasks.start.slots.exempt_workflow`
is on: its unit holds no place, so starting it would leave the place free and the next check would ask again. The
check says so, and lists the game or Studio work behind it instead.

**Reasons the tool works out itself**, each of which lets the check pass with the reason in its output:

- **No eligible issue** under the gate (or no ready issue at all, or every eligible one is blocked by an unfinished
  issue), and no work of the other kinds either.
- **The claim limit is reached.** The account holds `claims.limits.maintainer` open claims. They are counted from the
  held claim cards of the issues assigned to it, with the claim Action's own rule (`open_task_keys`), so the tool and
  the Action cannot disagree: issues of one task count once, a claim paused by `S-on-hold`, `S-blocked` or
  `S-needs-info` still counts, and a claim outside the limit (a `workflow`, Studio, networking or renderer issue, or one
  whose unit's place is free) does not ([Claiming](issues.md#claiming)). Only an issue outside the limit can still be
  started then.
- **The place kept for game work cannot take Coney Studio, networking, renderer, tooling or docs work**, and no game
  (`feature`) issue can start; likewise the place kept for networking cannot take anything but a networking issue, and
  the one kept for the renderer anything but a renderer issue.
- **The networking or renderer track is full**: a unit of it runs, and the track holds one.
- **Free disk is below the floor** (`workspace.disk_min_free_gb`) on any [watched drive](workspace.md#watched-drives):
  a new unit builds, and a build folder is large. The drives are the workspace's, the build root's (it fills first
  where the builds are on another drive) and the folders of `watch_paths` in `coney.local.toml`, each with the limits
  it is given; the `ok:` line names every drive under its floor, with how much it has and what lives there. A drive
  that cannot be read is a `warning:` and no reason.

Anything else needs a waiver.

**Waivers.** `coney-tools task slots --waive N --reason <code> --hours H --note "<why>" [--issues A,B]` says that `N`
free places may stay free for `H` hours. It is logged (`slot-waivers.jsonl` in the scratch folder, never
edited), expires by itself, and `task slots` lists the active ones. The codes are `tasks.start.fill.waivers.codes`; each
has a meaning, the most hours it allows (`max_hours`, under the cap of `waivers.max_hours`) and whether `--issues` must
name the blocked issues. The tool refuses a waiver that breaks them and prints the codes it knows: a maintainer's
pause, a decision a maintainer owes, and a loaded machine (the last allows the fewest hours).

The note is at least `waivers.note_min_characters` long. The check passes while the active waivers cover at least as
many places as could start work now; waivers add up. Only the coordinator waives: `hooks.toml` refuses the command to
every other role.

**What the check prints.** `task slots --fill-check` exits 1 with a message that starts `Blocked:`: the free places by
kind, the work to start (each place kind's list, the issues with their priority label and title, the claimed tasks
without an agent, each failed pull request and its checks) and the waiver command. It exits 0 with a line `ok: <why>`
otherwise. A GitHub that cannot be read exits 0 with a `warning:` line: it cannot judge what it cannot read.

**The Stop hook** runs the check at the end of each turn of the main session's coordinator (`agent_type`;
`dispatch.py` runs it for the `Stop` event, [The hooks](agents.md#hooks)) and blocks the turn with the message as the
reason, so the coordinator starts the work or waives. A start ends it at once: the next check finds the place taken.

- **It never traps the coordinator.** It blocks only on exit code 1 with a message that starts `Blocked:`; a crash
  also exits with 1 but prints nothing there. A tool that cannot run (no `uv`, a crash, a timeout, a bad option) passes
  the turn with a note shown to the user and a line in `coney-hooks.log`. And the tool lets a session's turn end after
  `tasks.start.fill.block.max_in_a_row` blocks in a row, counting again after `reset_minutes` without a
  block; a pass clears the count, and a counter that cannot be written never blocks.
- **It is quick.** Every turn end runs it, so the reads are few and the answers cached. A normal run makes five `gh`
  calls, however many issues there are: the ready issues (`gh issue list`), the claim cards of every issue assigned to
  the account (one GraphQL search with each issue's first 100 comments; an issue with more is read on its own), the
  states of every blocker the ready issues name (one GraphQL query, 100 numbers at most; a number GitHub does not know
  warns only for the issues that name it), and the pull requests with the branch's required checks. A test
  (`test_a_realistic_run_makes_a_handful_of_gh_calls`) pins the count for 20 assigned and 30 ready issues. Each `gh`
  call is killed after `github_seconds`, all the reads together stop at `budget_seconds`, and the answers are kept
  `cache_seconds` in the scratch folder, dropped as soon as a task file or unit record is newer than them (so a claim
  made a moment ago shows). The budget is for a slow GitHub, never for the number of issues: when the reads together
  do pass it, the warning says "the reads together took longer than the <budget_seconds> s allowed for all of them (N
  gh calls made)", names every issue whose blockers were not read in one line, and the check passes. Do not raise the
  budget to cure a warning; count the calls. The hook waits longer than the tool's longest run (`TIMEOUT`, held there
  by a test).
- **It does not copy the rules.** Every number is the tool's, read from the conventions; the hook knows only the exit
  code, the word `Blocked:` and the role.

**What it cannot do.** It is a hook that judges what the coordinator is told, not a security boundary: a coordinator can
waive without a good reason (the waiver is logged with its note and its maker, and expires); a session a person starts
is not covered; units of the old workflow with no unit record are invisible to the place count; the labels (`workflow`,
`studio`, `networking`, `renderer`) and the claim count come from the claim cards, which follow a label change only when
the claim Action passes over the issue (a sweep behind leaves a card stale for up to an hour); the kind of a ready
issue's task is the default of its labels (`task create` lets the coordinator choose another, which the tool cannot
know); only the account's own pull requests count for the fix place, and "oldest first" is by pull request number.

## Fix tasks without a unit record {#fix-tasks}

A fix task made by `task create --fix-of` and started by `task start` has a `unit.json`, so it is counted like any unit
([Engineer tracks](agents.md#slots)). A fix folder made by hand, or before the records, has a task file (`kind: fix`, a
`parent`) and perhaps a `slot.json`, and `task slots` and this check read it so (`task_slots.scan`):

- **A fix with a `slot.json` is in work in the fix track,** however old the slot is: any other task's reservation lapses
  after `reserve_minutes`, but a fix with no record made nothing else to show its agent. It ends when the fix's log or
  its parent's holds `finished`, or a passing `reviewed` written after the slot; a log that cannot be trusted keeps the
  place. Its issues cover the pull request it fixes, so the check does not ask for another fix unit. The `task slots`
  line of such a fix names the `slot.json` to delete to free the place of an agent that never started. It is always a
  fix-track holder: the `workflow` exemption of a unit comes from its claim cards, and a fix with no record gets none.
- **A fix with no `slot.json` is queued,** oldest first, work for the place kept for a fix.
- **A fix whose pull requests are over is neither:** the issues of the fix have pull requests that close them, and every
  one of those is merged or closed. A fix of several issues stays while the pull request of any of them is open. Both
  commands read this from GitHub (`task_slots.pulls_over`, within the check's budget) and keep the fix when GitHub
  cannot be read or no pull request closes its issues (`finishes: false`). A fix whose pull request was closed unmerged
  is hidden too, though `pr open --reopen` could still release it: start such a fix by hand. The agent guard, `task
  start` and Conclave read no GitHub for this, so they still count a slot-held fix of a merged pull request, and
  Conclave still lists it as queued, until `workspace clean` runs. [`workspace clean --apply`](workspace.md#cleanup)
  moves the folder of a fix whose parent's pull request merged to `scratch/archive/<task>`, which nothing reads, unless
  the fix may still be worked: a `slot.json` younger than `reserve_minutes`, a unit record whose worktree exists, or a
  parent worktree with commits the merged pull request lacks.

The stage commands (`task check`, `task review`, `orig`) stamp such a fix in its parent's log, and `pr open <fix>` goes
out through the parent's unit record, both by the `parent` of its task file (as [A fix task](issues.md#starting) does by
its record), so one log holds the proof `pr open` reads.
