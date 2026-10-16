# coney-zone

Coney's Claude Code mod for each zone ([how the roles run](../../docs/workflow.md)). It is loaded into every zone's
session by that zone's start script (`--plugin-dir`), from the installed copy of this folder; it is never installed
through a plugin store, so nothing reaches a zone through the shared login.

- **Pane.** "Coney zone" (`/coney` opens it again) shows the zone, its role and account; the walls (the last zones
  check, the breach flag, and any legacy copy it found); the role's inbox by state, with the items waiting for it; on
  the public side, the research pages in `main`; and when the status was last read and the session last woken. It
  draws ids, states and counts only, never page text, and only what the account it runs as may read.
- **Waking.** Every 15 seconds it runs `coney-tools zone status --json` locally. When new items wait for the role,
  or items still wait half an hour after the last wake, it submits the role's line, `Follow <brief>`, with a second
  line saying why (the new items by station, state and id, at most eight named; or why a repeat or idle wake came),
  which starts a turn once the session is idle. The woken turn still runs its inbox: that is the full list. Items
  the role handles never wake it, and it sends nothing while the main turn runs or a wake it sent has not started
  yet, so one turn takes in everything that changed meanwhile (a wake that no turn picks up within two idle minutes
  counts as dropped and is sent again). A role whose status names `idle_wake_minutes` (its own work goes on between
  inbox items) is also woken after that long idle, its turn ended and none of its subagents at work. Nothing else
  wakes a role: no timer loop, no model turn while nothing happens.
- **Failures are loud.** A status that cannot be read is shown in the pane and as a notice, never taken for an empty
  inbox; a breach flag is shown in red and on the status line, and a legacy copy (a copy of an older Coney file
  in the analysis zone outside the research sweep's quarantine) in yellow and on the status line.

It reads only: it never writes a file and never sends anything to another zone. `claude plugin validate
mods/coney-zone` and `claude plugin test mods/coney-zone` check it.
