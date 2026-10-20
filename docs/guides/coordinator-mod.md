# The coordinator's mod

The [coordinator](how-we-work.md#coordinator) runs the work day to day from one Claude Code session in the main
checkout. Between turns that session sees nothing: a new issue, a unit an agent finished, a build that ended or a pull
request that broke would wait until someone typed. The `coney` mod, a Claude Code plugin in
[tools/claude-mod/coney/](repo:tools/claude-mod/coney/README.md), watches for that work and wakes the session when it
arrives, and shows what waits and what the machine is doing in a pane beside the conversation.

## Starting it {#start}

From the main checkout (or any worktree; the script finds the main checkout):

```powershell
pwsh tools/coordinator.ps1                       # continue the coordinator's last conversation, with the mod
pwsh tools/coordinator.ps1 -New                  # a fresh conversation
pwsh tools/coordinator.ps1 -IdleWakeMinutes 45   # also wake after 45 idle minutes with no subagent at work
pwsh tools/coordinator.ps1 -DryRun               # print the folder and the command line only
```

The script runs `claude --continue --plugin-dir <checkout>\tools\claude-mod\coney` in the main checkout, with a first
prompt that points the session at [the coordinator's rules](how-we-work.md#coordinator) and at `HANDOFF.md`, the
local state file, as background. The mod is loaded for that session only, from the checkout: nothing is installed,
and other sessions on the machine (the agents in their worktrees) never load it. `-Model` picks the session's model,
and `-ModDir <folder>` loads the mod from another folder, such as a worktree that changes it. The mod needs `uv` and
`gh` on the `PATH`, as the rest of `coney-tools` does. In the session, `/coney` brings the pane to the front.

## What wakes the session {#wakes}

Every 15 seconds the mod runs [`coney-tools coord status --json`](coney-tools.md#coord) from the session's folder,
and every eighth read (two minutes) with `--github`. The report's `action` list holds what waits for the coordinator,
as stable ids:

| Id | Waits for | Read |
| --- | --- | --- |
| `issue:<n>:S-needs-triage` | the issue to be triaged ([Triage](issues.md#triage)) | with GitHub |
| `pr:<n>:<cause>` | a pull request that `repo pr-health` calls stuck (`checks-failed`, `conflict`, `ci-stuck`, `auto-merge-off`) | with GitHub |
| `unit:<branch>:<file>` | a `PR.md` (or `PR-<n>.md`) in a branch's scratch folder with no pull request yet: the [release queue](how-we-work.md#release-queue) | with GitHub |
| `build:done:<worktree>` | a worktree's detached build (`build --detach`) that ended within the last hour | local |

When ids that were not there at the last wake appear, the mod submits a prompt whose first line is
`Coordinator: new work.` and whose second names the new ids (at most eight, the rest counted). The rules:

- **Only while the session is free.** No wake while the session's own turn runs (subagents' turns do not count), or
  while a wake it sent has not started a turn yet: one turn takes in everything that changed meanwhile.
- **Repeats back off.** Ids still waiting wake the session again 30 minutes after the last wake, then 60, 120 and at
  most every four hours; new ids wake at once and start the repeats over.
- **A lost wake is sent again.** A wake that no turn picked up within two idle minutes counts as dropped. A wake the
  engine refuses (it may, after the person interrupts a turn) shows `wake refused` in the status line and a toast
  once, and is tried again two minutes later or after the person's next turn.
- **An idle wake only on request.** With `-IdleWakeMinutes N` the session is also woken after N idle minutes, but
  never while a subagent is at work.
- **Pull-request problems are Tether's.** The `pr:` ids show in the pane and wake no one here, because the
  [Tether](#tether) plugin's `prs` poller wakes the session for them already, with its owner and waiver records.
- **A failed read is said, never taken for "nothing waits".** The pane, the band and a toast say what failed, and the
  ids waiting stay as they were until a read succeeds.

The mod only reads: it runs `coney-tools coord status` and `git status`, never writes a file, and draws ids, labels,
states, counts and times, never an issue's or pull request's title or body.

## The pane, the band and the status line {#pane}

- **Work**: *Waiting*, every id (a press puts it in the prompt); *Issues*, the open issues waiting for triage or
  without a status label (every open issue on request) with status, priority, area, age and the other labels; *Pull
  requests*, each open one's checks, mergeability, auto-merge and pr-health causes; *Release*, each unit file of a
  branch that still has a worktree, queued or released.
- **Machine**: *PCSX2*, each copy free or held, by whom and since when; *Builds*, the slots and queues of both lanes
  and each worktree's last detached build; *Tracks*, each `STATUS.md` newest first, with whether its worktree exists
  and is busy ([Track status](how-we-work.md#status)).
- **Files**: the checkout as a tree, git's changes and a preview of a picked file.

The band above the prompt holds the counts (what waits, subagents at work, Tether's pull requests, CI's queue, build
slots, PCSX2 copies, the time of the last read) and drops the least needed at a narrow width. The status line reads
`Coney: N waiting · K agents`.

## Tether {#tether}

[Tether](https://github.com/Ryubara/tether) is the plugin the maintainer runs in every session for the agents pane, the
cache guards and its watchers; the coney mod leaves those to it and adds what is Coney's own. Tether's `prs` poller owns
the pull-request wakes. The repository commits no Tether config: Tether reads `.claude/tether.json` in the session's
folder, so a committed one would apply to every agent's worktree too, and the maintainer keeps a local one (listed in
`.git/info/exclude`). To have Tether read pull-request health through Coney's own check, the coordinator's
`.claude/tether.json` holds a poller like this (field by field over the user's Tether options):

```json
{
  "watch": {
    "pollers": [
      {
        "id": "prs",
        "kind": "prs",
        "title": "Open pull requests",
        "command": ["uv", "run", "-q", "--project", "python", "coney-tools", "repo", "pr-health", "--json"],
        "everySeconds": 300,
        "timeoutMs": 240000,
        "worktrees": "<workspace>/worktrees/coney",
        "busyMarker": ".coney-busy",
        "assignHours": 4
      }
    ]
  }
}
```

## Changing the mod {#changing}

The mod's code is in `tools/claude-mod/coney/hooks/`: `register.tsx` holds the hooks, the reads and the wake engine
(every call on `$` is there), and the tabs and the band draw from plain data. Its tests drive the hooks with a mocked
clock, `coney-tools` and engine:

```sh
claude plugin validate tools/claude-mod/coney
claude plugin test tools/claude-mod/coney
```

Try a change in the live session with `pwsh tools/coordinator.ps1 -ModDir <worktree>\tools\claude-mod\coney`, and
change `coord status` and the mod in the same pull request when the report's shape changes.
