# coney

The coordinator's Claude Code mod: a pane of what waits for the coordinator and what the machine is doing, a band of
counts above the prompt, a status line, and the session's waking. How to start and use it:
[The coordinator's mod](../../../docs/guides/coordinator-mod.md). It is loaded with `--plugin-dir` by
`tools/coordinator.ps1`, never installed from a plugin store.

- **Reading.** Every 15 seconds it runs `coney-tools coord status --json` from the session's folder (with
  `uv run --isolated --frozen --quiet --project <checkout>\python`, as `.mcp.json` does), and every eighth read (two
  minutes) with `--github`. A local read never touches the network; the issues, pull requests, CI counts and the
  release queue's state come from the last `--github` read, and so do their ids.
- **Waking.** The report's `action` ids are what waits. When new ids wait, or ids still wait after the last wake (half
  an hour, then twice as long after each repeat, at most four hours; new work starts it over), it submits
  `Coordinator: new work.` with a second line naming the new ids (at most eight; the rest counted). It sends nothing
  while the main turn runs or a wake it sent has not started, so one turn takes in everything that changed meanwhile;
  a wake no turn picks up within two idle minutes is sent again. A wake the engine refuses (after the person
  interrupts a turn) shows `wake refused` in the status line and a toast once, and is tried again two minutes later or
  after the person's next turn. With `CONEY_IDLE_WAKE_MINUTES` set (the launcher's `-IdleWakeMinutes`), the session
  is also woken after that long idle with no subagent at work. Pull-request problems (`pr:` ids) are Tether's prs
  poller's to wake for: they show here and wake no one.
- **Failures are loud.** A read that fails is shown in the pane, the band and a toast, never taken for "nothing waits";
  the ids waiting stay as they were until a read succeeds.

It reads only: it never writes a file and runs nothing but `coney-tools coord status` and `git status`. It draws ids,
labels, states, counts and times, never an issue's or pull request's title or body.

## The pane

- **Work**: *Waiting*, every action id (a press puts it in the prompt; Tether's are marked); *Issues*, the open
  issues waiting for triage or without a status (every open one on request) with status, priority, area, age and
  labels; *Pull requests*, checks, mergeability, auto-merge, age, branch and pr-health's causes; *Release*, each
  `PR*.md` of a branch with a worktree: queued, released (its pull request) or not known until the next GitHub read.
- **Machine**: *PCSX2*, each copy free or held, by whom and since when, and whether PCSX2 runs; *Builds*, the slots
  and queues of both lanes and each worktree's last detached build; *Tracks*, each `STATUS.md` newest first, whether
  its worktree is there and busy (a press puts its path in the prompt).
- **Files**: the checkout as a tree, git's changes, and a preview of a picked file.

The band holds the counts (`Coney coordinator │ 3 waiting │ 2 agents │ CI 1 queued, 4 running │ builds 1/3 │ PCSX2
1/5 │ read 12:00:15`) and drops the least needed parts first at a narrow width. The status line is
`Coney: N waiting · K agents`, with `· wake refused` while a refusal stands.

## Tests

```sh
claude plugin validate tools/claude-mod/coney
claude plugin test tools/claude-mod/coney
```

The tests drive the hooks with a mocked clock, `coney-tools` and engine (no network, no real session). Every function
that touches `$` lives in `hooks/register.tsx`, beside the hooks: the tabs and the band are drawn from plain data.
