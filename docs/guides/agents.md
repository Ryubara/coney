# Running agents

Every agent the coordinator starts costs part of a plan's usage limit, and most of that cost is not the work itself
but the context the agent carries and how often it has to re-write it. This page is the procedure the coordinator
follows each time it hands out work: who does it, which model, how it is briefed, and how it runs so that its
prompt cache survives. The conduct rules for agents are in [How we work](how-we-work.md#agent-conduct).

**Every dispatch decision weighs the cache.** Tokens are a budget: before the coordinator starts, forks, resumes or
re-briefs an agent, or picks its model, it asks what that costs in cache writes and picks the cheapest way that still
gets the work right. The rules below are that question answered for the usual cases.

## Setting up an agent tool {#setup}

The rules every agent follows are in [AGENTS.md](repo:AGENTS.md) and these guides, whatever tool runs the agent.
The repository also ships the configuration that lets an agent tool use the project's debugging servers and roles,
so a fresh clone gets the same setup as the maintainer's machine:

| File | What it holds | Read by |
| --- | --- | --- |
| `AGENTS.md` (and `CLAUDE.md`, `GEMINI.md`, which only import it) | The rules | Most agent tools |
| `.mcp.json` | The debugging servers: `ghidra-mcp` (the shared Ghidra project) and `pcsx2` (the emulator over PINE) | Claude Code; other tools take the same two commands in their own MCP config |
| `.claude/agents/` | The roles: `analyst`, `implementer`, `reviewer`, `research-reviewer` | Claude Code |
| `.claude/skills/` | The coordinator's skills: `repo-hygiene` ([the skill](repo-conventions.md#hygiene-skill)) | Claude Code |
| `.claude/settings.json` | Shared permissions: the project's checks, generators, tests and builds allowed; `git stash` and stopping programs by name refused ([Sharing one machine](how-we-work.md#shared-machine)); a hook that refuses a wait loop with no bound ([Bounded waits](#bounded-waits)), and the hook `.claude/hooks/issue_guard.py` that refuses raw `gh` commands that write an issue ([Filing an issue](issues.md#filing)) | Claude Code |
| `.claude/hooks/` | The hook scripts `settings.json` runs before a shell command | Claude Code |

**The servers.** `.mcp.json` holds no paths. Each entry runs `coney-tools mcp serve <name>`, which finds the server
through `coney.local.toml` (`ghidra_mcp_repo` and `pcsx2_mcp_dir`, see [Local workspace](workspace.md)) and runs it
over stdin and stdout, from the main checkout or any worktree. The entries run `uv run --isolated --frozen`: uv keeps
the launcher's environment in its own cache rather than a `python/.venv` in each worktree, and never rewrites
`uv.lock`. The ghidra-mcp bridge runs with `uv run --frozen` in the ghidra-mcp checkout, which uses that checkout's
own `.venv` and lock file and changes nothing else in it. A server that is not installed stops at once with one line
naming the missing key or program; the agent tool shows it as failed and everything else works. `ghidra-mcp` also
needs its headless server running ([Ghidra + ghidra-mcp](ghidra.md)), and `pcsx2` needs Node.js 18 or newer. Claude
Code asks once whether to use the project's servers. A server of the same name registered in your local scope
(`claude mcp add -s local`) takes precedence over the project's; one in your user scope does not.

**The roles.** Each file in `.claude/agents/` names a role from [How we work](how-we-work.md) and points at the
guides that role reads; it holds no rules of its own. The analyst and the research reviewer are set to Opus, which
AGENTS.md requires for reverse engineering; the others take whatever model the coordinator names
([Which model](#models)). The implementer and the code reviewer have the Ghidra and PCSX2 MCP tools withheld
(`disallowedTools` names each server, which removes all of its tools). That is a guard against reaching for them,
not the clean room itself: they still have a shell, and the rule that keeps them out of the original's code is
AGENTS.md's. Effort, turn and token limits are each user's own settings and stay out of the repository.

**The permissions.** The allowed commands are the project's own read-mostly ones: `coney-tools config show`, the
`repo` checks, the generators, pytest, ruff, mypy, pre-commit and the CMake presets. Commands that start or drive
PCSX2 (`coney-tools pcsx2 ...`), and git beyond Claude Code's own read-only detection, still ask. The refusals match
on spelling (`taskkill /IM` is refused, `taskkill.exe /Im` or a piped `Stop-Process` is not), so they catch honest
mistakes; they are not a security boundary.

Another agent tool needs only its own MCP entries for the two servers (the same
`uv run --isolated --frozen --project python coney-tools mcp serve <name>` command, run from the checkout) and reads
the rules from `AGENTS.md`.

## What a run costs {#cost}

An agent re-sends its whole context (instructions, tools, brief and everything it has read or done) with every
request. The provider caches that context:

- **A cache read** (the context unchanged since the last request) is cheap, and barely counts toward a subscription's
  limits.
- **A cache write** (the context stored for the first time, or again after the cache was lost) is what uses up the
  limits. A write is as big as the whole context: an agent deep into a task re-writes everything it has read and done.

So the questions for every agent are how big its context will grow and how many times it will have to be written.
A cache is lost, and the next request re-writes everything, when:

1. **It expires.** Claude Code keeps the main session's cache for an hour and an agent's for five minutes unless
   `"subagentPromptCacheTtl": "1h"` is set in the user settings (set it). Any wait longer than the lifetime (a build,
   a test sweep, an emulator run, a CI wait) expires it.
2. **A finished agent is messaged again.** Its next request re-writes almost all of its context, even within the
   hour.
3. **The context is compacted or cleared**, by design.
4. **Anything before the conversation changes:** the tool list (an MCP server connecting or dropping), the
   instruction files, the model or its settings.

Measured on this project (2026-10-08): an implementer that ran three Windows builds of 6-10 minutes with a five-minute
cache re-wrote its 266 000-282 000-token context after each one; those three writes were about three quarters of
everything it wrote. With the hour-long setting (2026-10-09), a test agent waited 6.5 minutes inside one command and
then read its 40 600-token context from the cache, writing only 157 new tokens. A fresh agent starts at about 38 000
tokens (instructions, tools and a short brief).

## Who does the work {#who}

Decide in this order. The first answer that fits wins.

| Do it as | When | Why |
| --- | --- | --- |
| **The coordinator itself** | A few tool calls, no long command, and it needs the current conversation (a doc section about a decision just made, a quick check) | No new context at all |
| **A fork of the coordinator** | Needs details of the current conversation that are costly to write down, stays short (roughly 20 calls or fewer), runs no command longer than a few minutes, needs the coordinator's model, and the coordinator's context is small (well under half of its limit) | Its first request reads the coordinator's cache instead of writing a new context |
| **A fresh agent** | Everything else: long work, its own worktree, builds, tests, the emulator, Ghidra sessions, anything another model should do | Starts small (instructions, tools and a short brief) and stays focused |

A fork inherits the coordinator's whole context and its model, and cannot run on another one. That makes it the
wrong choice for any long worker: every re-write costs the coordinator's full context plus the fork's own work, and it
reaches its context limit and compacts sooner. When in doubt, start a fresh agent.

## Which model {#models}

Match the model to the judgement the task needs, not to its size. A cheaper model that has to redo work, or that
sends a branch through CI three times, costs more than a stronger one that gets it right once.

| Model | Use it for | Examples |
| --- | --- | --- |
| **Claude Opus** | Reverse engineering of any kind, and implementation that needs judgement: reimplementing a system faithfully from a research page (numeric fidelity, timing, AI, physics, rendering, combat), work across several subsystems, diagnosing a failure whose cause is unknown | Analysts in Ghidra or the emulator; the combat, physics and renderer implementers; fixing a red CI with unknown causes |
| **Claude Sonnet** | Well-specified implementation and upkeep, where the research page or an existing pattern says exactly what to write | Wiring a documented script binding, a HUD panel built to its spec, a new test following an existing one, generators, doc upkeep |
| **Claude Haiku** | Mechanical work with a clear check and nothing to decide | Running a command and reporting the result, measurements, renames, regenerating pages, simple searches |

Claude Fable is not used for agent work. Always name the model when starting an agent; never rely on the default.

**Escalate on signals, not on hope.** Move a task up a model when the agent fails the same check twice, has to make
a call the page does not settle, or touches a subsystem it was not briefed on. Record which model did which kind of
task and how often its work came back (CI failures, review findings, play-test bugs) in the track's state file, so the
table above follows evidence.

## Briefing a fresh agent {#briefs}

A brief is part of the context the agent carries on every request, so it is short and points at the repository
instead of repeating it:

- the role (analyst, implementer or support), the model, the worktree and the scratch folder;
- the task and the scope it owns, the files it must not touch, and what "done" means for it
  ([Testing](testing.md#done)), and that it reports it in the pull request's Testing and Screenshots sections, with
  its captures saved in `scratch/<branch>/shots/` and referenced in `PR.md` as `![alt](./shots/file.png)`
  ([Capturing and attaching](testing.md#pr-media));
- "Read `AGENTS.md`, [How we work](how-we-work.md), [Testing](testing.md) and this page first; they override the
  brief";
- how it runs ([Running without losing the cache](#cache)): it works local-first, never pushes or waits on CI, and
  hands each finished unit to the [release queue](how-we-work.md#release-queue);
- disk: it creates `.coney-busy` in its worktree as it starts and deletes it when it finishes, and before reporting a
  unit finished it deletes its own `build/` folder; a build is about 4 GB per preset
  ([Cleanup](workspace.md#cleanup));
- the local gate command, named in full, as part of "done": `uv run --project python coney-tools check changed`
  ([The local gate](testing.md#local-gate)), run in the agent's worktree before it reports a unit finished. An agent
  that is not told runs only the checks it knows of, and CI finds the rest;
- only the facts the agent cannot find in the repository (a log path, a question to answer, the state file).

A brief may be just "issue #N": the issue holds the steps, the screenshots and the acceptance test, and the agent
reads it as [Issues](issues.md#agents) describes. Naming the number is cheaper than pasting the issue, and the
issue stays the one place to read it.

## Running without losing the cache {#cache}

- **Never sit inside a long command.** Builds, test sweeps, emulator runs and CI waits that take more than a few
  minutes run in the background with their output going to a file; the agent checks the file every few minutes
  (each check keeps the cache warm) or once when the run should be done. Even an hour-long cache does not survive a
  run that takes longer.
- **Every wait has a bound** ([Bounded waits](#bounded-waits)): never `until ...; do sleep ...; done` or `while true`
  in a shell, which a hook refuses.
- **Keep tool output small.** Print counts, the failing lines and the end of a log, never a whole log: every line an
  agent reads stays in its context for the rest of the task.
- **Do not message a finished agent.** Start a new one from its report and the track's
  [STATUS.md](how-we-work.md#status).
- **No agent waits on CI.** An agent commits locally, runs the local checks, hands each finished unit to the
  coordinator's [release queue](how-we-work.md#release-queue) and goes on; it never pushes. The coordinator pushes,
  watches CI and sends a failure to a fresh agent ([When a pull request fails checks](#ci-failures)). An agent that
  sat idle past its cache lifetime would re-write its whole context, often hundreds of thousands of tokens, when it
  resumed.
- **Keep the coordinator small.** The coordinator re-reads its whole conversation on every turn and is the context a
  fork would inherit. It saves the state, clears and continues from it at a natural boundary, and never forks once
  its context is large.

## Bounded waits {#bounded-waits}

A shell command that waits must end on its own, whatever happens to what it waits for. A background shell running
`until grep -q done build.log; do sleep 20; done` waits for ever when the build dies before writing the line: the
shell never ends, keeps its agent listed as running long after it finished, holds a little memory and CPU, and is
left for somebody to find and kill. On 2026-10-12 twenty such shells, hours old, were found and killed by hand after
an agent tool restart.

So a hook ([`.claude/hooks/bounded_waits.py`](repo:.claude/hooks/bounded_waits.py), run before every Bash and
PowerShell command) refuses a loop that sleeps (`until` or `while` in bash, `while`, `do ... until` or
`do ... while` in PowerShell) with no bound: no number compared in the loop and no `timeout` in front of it. Wait
in one of these ways instead:

- **A counted loop:** `for i in $(seq 1 30); do grep -q done LOG && break; sleep 20; done` (at most 30 looks), or in
  PowerShell `for ($i = 0; $i -lt 30; $i++) { if (...) { break }; Start-Sleep 20 }`. A counter or a deadline compared
  in an `until` or `while` loop (`[ $n -ge 30 ]`, `$SECONDS -lt 600`, `(Get-Date) -lt $deadline`) is a bound too.
- **`timeout N` around it:** `timeout 600 bash -c 'until grep -q done LOG; do sleep 20; done'`.
- **No loop at all:** start the long command itself in the background (`run_in_background`) and read its output file
  when you are told it finished, or once in a while. A build or test run is `coney-tools build`, whose `--max-wait`
  caps its wait for a slot.

A wait that is still left behind (an older shell, a shell from another tool) is found by `coney-tools workspace
clean`, which stops shells older than three hours waiting on files unchanged for an hour, and orphaned test runs
([Cleanup](workspace.md#cleanup)).

## When a pull request fails checks {#ci-failures}

A failing check is a small, well-defined task, so it goes to a **fresh agent with a tight brief**, not back to the
agent that wrote the pull request. That agent has moved on or ended: it handed the unit to the release queue
without waiting for CI, because an agent idle past its cache lifetime re-writes its whole context when it resumes.
Messaging it re-writes that context (rule 2 in [What a run costs](#cost)), often hundreds of thousands of tokens, for
a fix of a few lines. The coordinator is the one that watches CI.

1. The coordinator reads only the failure: `gh pr checks <n>` for which checks failed, then the end of the failing
   job's log (`gh run view <id> --log-failed`, last lines). It does not read the whole log.
2. If the fix is a few tool calls (a long line, a title, a missing `Docs: none`), the coordinator does it itself.
3. Otherwise it starts a fresh agent on the pull request's branch, in the branch's worktree, with a brief of the
   branch, the failing check, the log lines that matter and "done" (the check passes locally and the fix is
   committed; the coordinator pushes it). The model follows [Which model](#models): Haiku or Sonnet for a lint,
   format, title or docs failure with a clear message; Opus when the cause is unknown or the failure is a test or
   build in code that needs judgement.
4. Several failures with the same cause go to one agent; unrelated failures that are each quick go to separate small
   agents that can run side by side.
5. If the same check fails a second time, escalate a model, as above, instead of repeating the same fix.

## CI capacity {#ci-capacity}

GitHub Actions on this repository runs about 20 jobs at once (5 of them on macOS), shared by every pull request, and a
full code pull request queues 4 builds plus sanitizers (about 20 minutes). A queue that backs up blocks every merge,
and an agent waiting on it re-writes its context. So CI never holds an agent back:

1. **Agents work local-first.** They commit, run every local check that applies and hand each pull-request-sized
   unit to the coordinator ([The release queue](how-we-work.md#release-queue)). CI should only find what the local
   platform cannot see.
2. **Only the coordinator pushes.** Before releasing a unit it looks at the queue,
   `gh run list --status queued --limit 50 --json databaseId --jq length` and the same with `--status in_progress`,
   and releases when there is room: engine fixes first, then what unblocks other work.
3. **Each push starts a run** and cancels the pull request's previous one, so a branch is pushed once it is ready,
   not after every commit. A merge to `main` likewise cancels `main`'s older run, so a merge round costs one build
   ([What CI runs](building.md#what-ci-runs)).
4. **A pull request runs only the jobs its change can break** ([What CI runs](building.md#what-ci-runs)), so
   documentation-only units are cheap to release.

## Checking a run {#check}

Claude Code writes each agent's transcript to
`~/.claude/projects/<project>/<session>/subagents/agent-<id>.jsonl`. Every assistant line has a `usage` object:
`cache_read_input_tokens`, `cache_creation_input_tokens`, and under `cache_creation` the split between
`ephemeral_5m_input_tokens` and `ephemeral_1h_input_tokens`. A large `cache_creation_input_tokens` repeating after
long gaps is a cache being lost and re-written; writes in the five-minute bucket mean the hour-long setting is not
in effect.
