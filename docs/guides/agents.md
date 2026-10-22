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
| `.mcp.json` | The debugging servers: `ghidra-mcp` (the shared Ghidra project) and `pcsx2` (the emulator, over the PCSX2-MCP fork's debug server and PINE) | Claude Code; other tools take the same two commands in their own MCP config |
| `.claude/agents/` | The roles: `engineer`, `reviewer`, `research-reviewer`, `orig-reviewer`, `pr-reviewer` | Claude Code |
| `.claude/skills/` | The coordinator's skills: `repo-hygiene` ([the skill](repo-conventions.md#hygiene-skill)) | Claude Code |
| `.claude/settings.json` | Shared permissions: the project's checks, generators, tests and `coney-tools build` allowed; `git stash` and stopping programs by name refused ([Sharing one machine](how-we-work.md#shared-machine)), and `gh` commands naming a video file, which only `coney-tools clips publish` uploads ([Clips](testing.md#clips)); hooks that refuse a raw `cmake --build`, `ctest` or `ninja` ([Builds and tests](#builds)), a wait loop with no bound ([Bounded waits](#bounded-waits)), a raw `gh` command that writes an issue (`.claude/hooks/issue_guard.py`, [Filing an issue](issues.md#filing)), an Agent call with `isolation: "worktree"` (`.claude/hooks/agent_guard.py`, [Where an agent works](#worktrees)), a call of the shared `pcsx2` MCP server on a copy the caller has not claimed (`.claude/hooks/pcsx2_guard.py`, [The PCSX2 MCP guard](#pcsx2-guard)), a `gh pr create` or `gh pr edit` whose title or description CI would refuse (`.claude/hooks/release_guard.py`, [The release queue](how-we-work.md#release-queue)), and, inside a subagent, `git push` or a `gh` command that writes to GitHub (`.claude/hooks/push_guard.py`, [The release queue](how-we-work.md#release-queue)) and, after a `git commit`, a `Co-Authored-By` trailer that names another model than the agent runs on (`.claude/hooks/trailer_guard.py`, [Commit trailers](#trailer-guard)) | Claude Code |
| `.claude/hooks/` | The hook scripts `settings.json` runs before or after a shell command or an Agent call, and their `ruff.toml` (the package's rules; ruff lints the hooks like `python/`, [Conventions](conventions.md#python)) | Claude Code |

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

**The PCSX2 server and your copy.** The `pcsx2` server connects to no PCSX2 when it starts, and several agents run
their own copies at once, so connect it to the copy you claimed and to no other. `coney-tools pcsx2 claim --agent
<id>` prints the copy's `port` (PINE) and `debug-port`; start PCSX2 with `pcsx2 launch --agent <id>`, then call
`pcsx2_connect` with `debug_port` and `pine_port` set to those, `mode: "debug"` and `agent: "<your id>"` (the id you
claimed with). **Pass `agent: "<your id>"` on every `pcsx2_*` call**, not only the connect: it names your connection
in the server, and the hook refuses a connect without it. Debug mode is the default to use:
the debug server serves several clients, but PCSX2's PINE serves one, and an MCP server holding it blocks
`pcsx2 record` and every other PINE client of that copy. Use `mode: "auto"` or `"pine"` only when you need PINE's
own calls and nothing else uses the copy, and never leave the ports out: the defaults (21512, 28011) are copy
`pcsx2`'s, someone else's game. An agent tool that starts one server per agent can run
`coney-tools mcp serve pcsx2 --copy <name>`, which hands the copy's ports to the server as `pcsx2_connect`'s defaults
([`mcp`](coney-tools.md#mcp)); the hook still wants both ports in the call, so those defaults serve a server run
outside it. **A hook enforces this** ([below](#pcsx2-guard)). What the tools do: [Driving PCSX2](research-workflow.md#driving-pcsx2).

**The roles.** Each file in `.claude/agents/` names a role from [How we work](how-we-work.md) and points at the
guides that role reads; it holds no rules of its own. The engineer and the research reviewer are set to Opus, which
AGENTS.md requires for reverse engineering (a coordinator may name Sonnet for an engineer whose task needs none, per
[Which model](#models)); the others take whatever model the coordinator names. The `pr-reviewer` is set to Sonnet:
it is the release gate on a pull request description ([The description review](how-we-work.md#pr-review)), reads
the description, the diff, the logs and every picture, and writes only its verdict (`REVIEW.json`) and report
(`REVIEW.md`) next to `PR.md`. Its tools are Read, Glob, Grep, Bash and Write; its instructions keep Bash to
read-only commands and Write to those two files. The coordinator runs it headlessly with `coney-tools repo review`
([repo review](coney-tools.md#repo-review)), or starts it as an agent on a `PR.md`, a worktree and a base branch.
The engineer uses the Ghidra and PCSX2 MCP tools and writes `src/`; the code and research reviewers report and never
edit (`disallowedTools` withholds Edit, Write and NotebookEdit). Effort, turn and token limits are each user's own
settings and stay out of the repository.

**The permissions.** The allowed commands are the project's own read-mostly ones: `coney-tools config show`, the
`repo` checks, the generators, pytest, ruff, mypy, pre-commit and `coney-tools build`. Commands that start or drive
PCSX2 (`coney-tools pcsx2 ...`), and git beyond Claude Code's own read-only detection, still ask. The refusals match
on spelling (`taskkill /IM` is refused, `taskkill.exe /Im` or a piped `Stop-Process` is not), so they catch honest
mistakes; they are not a security boundary.

**The agents' display.** On a machine with a display set aside for agents, every window an agent opens goes there,
never on the display a person is working on. It is set once, in the agent tool's own environment, so no brief or
memory has to carry it: `CONEY_WINDOW_DISPLAY` (`portrait`, a display number or part of its name;
[Which display](running.md#window-display)) in the environment the tool gives its agents, for Claude Code the `env`
block of your user settings (`~/.claude/settings.json`, `"env": {"CONEY_WINDOW_DISPLAY": "portrait"}`). Coney opens
its window centred on that display without taking the keyboard focus, and `coney-tools pcsx2 launch` and
`pcsx2 record` move each PCSX2 window there as it appears ([Driving PCSX2](research-workflow.md#driving-pcsx2)). It
is each machine's own setting and stays out of the repository; a machine with one display leaves it unset. An agent
that runs anything else with a window puts it on the same display.

Another agent tool needs only its own MCP entries for the two servers (the same
`uv run --isolated --frozen --project python coney-tools mcp serve <name>` command, run from the checkout) and reads
the rules from `AGENTS.md`.

## The orig reviewer {#orig-reviewer}

The `orig-reviewer` agent (`.claude/agents/orig-reviewer.md`, Opus, since it reads the original) compares Coney
functions that carry `@orig` with the original functions in Ghidra, on fidelity and own expression. It follows one
rubric, [Orig review](review/orig.md). Its tools are Read, Glob, Grep, Bash (read-only commands and the
`coney-tools orig` commands), Write (its own report files only) and the ghidra-mcp read tools (`decompile_function`,
`disassemble_function` and the like). It has no Edit tool and no Ghidra tool that writes.

Run it in one of two modes:

- **pr mode, on every branch whose diff adds or changes an `@orig` function,** after the code review and before
  pr-reviewer. A fix to the code would otherwise make the description's review stale. Give it the branch (or its
  worktree), the base and the scratch folder that holds `PR.md`. It runs `coney-tools orig review-stamp --branch`,
  reviews each listed function, and writes `ORIG-REVIEW.json` and `ORIG-REVIEW.md` there. When the stamp lists no
  function, no run is needed, and the release check passes without one. Any later change to a reviewed function
  makes the verdict stale; a rebase that leaves the functions as they were does not.
- **sweep mode, on existing code,** for the [fidelity audit](how-we-work.md#fidelity-audit). Give it a `src/` folder
  or a list of addresses and a group folder under `scratch/orig-sweep/`. It reviews what
  `orig review-stamp --folder` lists as unreviewed or stale, writes `FINDINGS.tsv` and `PROGRESS.md`, records the
  findings with `coney-tools orig record`, and returns a summary that the launcher saves as `SUMMARY.md`. Run one
  agent per folder, and leave out the folders that another audit or open branch owns. The coordinator turns each
  finding that is not `clean` or `diff-deliberate-ok` into a fix unit.

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

Measured on this project (2026-10-08): an agent that ran three Windows builds of 6-10 minutes with a five-minute
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
| **Claude Opus** | Reverse engineering of any kind, and implementation that needs judgement: reimplementing a system faithfully from a research page (numeric fidelity, timing, AI, physics, rendering, combat), work across several subsystems, diagnosing a failure whose cause is unknown | Engineers in Ghidra or the emulator; combat, physics and renderer work; fixing a red CI with unknown causes |
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

- the role (engineer or support), the model, the worktree and the scratch folder;
- the task and the scope it owns, the files it must not touch, and what "done" means for it
  ([Testing](testing.md#done)), and that it reports it in the pull request's Testing and Screenshots sections, with
  its captures saved in `scratch/<branch>/shots/` and referenced in `PR.md` as `![alt](./shots/file.png)`
  ([Capturing and attaching](testing.md#pr-media)); video clips are recorded only with `coney --record` or
  `coney-tools pcsx2 clip` into `scratch/<branch>/clips/` and uploaded only with `coney-tools clips publish`, which
  deletes them ([Clips](testing.md#clips));
- for gameplay work (anything a player sees: the title areas `gameplay`, `graphics` and `audio`), the brief names the
  [runtime comparison](testing.md#runtime-comparison) with the original as part of "done", and the `Runtime:` line of
  the pull request's Testing section that reports it ([the Runtime line](testing.md#runtime-line)); it points at
  `coney-tools pcsx2 recipe` for the PCSX2 recipes that capture and read the original (a save state's values and
  picture, a trace; being added, with its own guide page), and the agent works in a PCSX2 claim
  ([Driving PCSX2](research-workflow.md#driving-pcsx2)). An agent that is not told compares nothing with the running
  original, and the pull request is then refused;
- "Read `AGENTS.md`, [How we work](how-we-work.md), [Testing](testing.md) and this page first; they override the
  brief";
- how it runs ([Running without losing the cache](#cache)): it works local-first, never pushes or waits on CI, and
  hands each finished unit to the [release queue](how-we-work.md#release-queue);
- disk: it creates `.coney-busy` in its worktree as it starts and deletes it when it finishes, and before reporting a
  unit finished it deletes its own `build/` folder; a build is about 6 GB per preset, and with the shared
  [compiler cache](building.md#compiler-cache) a fresh one builds in about 2 minutes, not 11
  ([Cleanup](workspace.md#cleanup));
- builds: "build and test only with `coney-tools build --agent <id> ...`, in the background" ([Builds and
  tests](#builds)); the brief does not paste the flags;
- the local gate command, named in full, as part of "done": `uv run --project python coney-tools check changed`
  ([The local gate](testing.md#local-gate)), run in the agent's worktree before it reports a unit finished. An agent
  that is not told runs only the checks it knows of, and CI finds the rest;
- only the facts the agent cannot find in the repository (a log path, a question to answer, the state file).

Engineers who drive the original use the [PCSX2 recipes](pcsx2-recipes.md) for the basic things (finding a human,
teleporting, spawning, calling the game, loading a checkpoint) instead of working the addresses out again, and add a
recipe, confirmed in PCSX2, when they find a new technique, and start from the [state library](pcsx2-states.md)
(`coney-tools pcsx2 states list`) instead of driving to the same place again; a brief need not repeat either.

A brief may be just "issue #N": the issue holds the steps, the screenshots and the acceptance test, and the agent
reads it as [Issues](issues.md#agents) describes. Naming the number is cheaper than pasting the issue, and the
issue stays the one place to read it.

## Where an agent works {#worktrees}

Every agent works in a worktree at `../../worktrees/coney/<branch>/` on a branch its brief names, and creates it
itself (`git worktree add ../../worktrees/coney/<branch> -b <branch> origin/main`). **Never start an agent with the
Agent tool's `isolation: "worktree"` option.** It creates the worktree for you under the main checkout's
`.claude/worktrees/` with an automatic branch name, which is outside the layout [AGENTS.md](repo:AGENTS.md) fixes:
the virus scanner exclusion a machine sets for the worktrees folder (without it a build starves the CPU) does not
cover it, the `.coney-busy` marker and [the workspace cleanup](workspace.md#cleanup) do not look there, and the
branch name is not the one the release queue and `STATUS.md` use. A hook (`.claude/hooks/agent_guard.py`, registered
for the Agent tool in `.claude/settings.json`) refuses the call and points here; start the agent again without
`isolation`.

## Commit trailers {#trailer-guard}

An agent's commits end with `Co-Authored-By: <the model it runs on> <noreply@anthropic.com>`
([AGENTS.md](repo:AGENTS.md)). The coordinator's session gets a system reminder naming the coordinator's own model for
that line, and subagents see it too and copy it: on 2026-10-13 a Sonnet agent twice credited Opus, the pull request's
Model line followed the wrong trailer, and `coney-tools repo check-body` passed because body and trailers agreed with
each other. A hook (`.claude/hooks/trailer_guard.py`, registered in `.claude/settings.json` for `PostToolUse` on Bash
and PowerShell commands that start `git commit` or `git -C ... commit`, and `git -c ... commit` on Bash) checks
every commit right after it is made. Claude Code gives a hook no model, but it does give the main session's
`transcript_path` and, inside a subagent, an `agent_id` and an `agent_type` (its role, such as `engineer`; the
hook does not use it, because the role does not name the model). The agent's own transcript
(`<session>/subagents/agent-<id>.jsonl`, [Checking a run](#check)) names the model on every assistant entry
(`claude-sonnet-5-5`), and the hook derives the trailer's name from it (`Claude Sonnet 5.5`) instead of keeping a
table. A trailer with the vendor's address that names a different model exits 2, and the agent sees the reason and
amends the commit with the one trailer it should have. A commit that printed nothing (`git commit -q`) is judged from
the HEAD of the working directory and of the directories the command names, if it was made in the last two minutes.
Other command shapes are not checked: `git --no-pager commit`, `git --git-dir=... commit`, `git.exe commit`
and a full path to git. The hook fails open when it cannot tell (no transcript, an id that is not a Claude model, no
trailer, another vendor, a commit it cannot find), and a person committing outside Claude Code never runs it.

## Running without losing the cache {#cache}

- **Never sit inside a long command.** Builds, test sweeps, emulator runs and CI waits that take more than a few
  minutes run in the background with their output going to a file; the agent checks the file every few minutes
  (each check keeps the cache warm) or once when the run should be done. Even an hour-long cache does not survive a
  run that takes longer. A build or test run is `coney-tools build ...` started with `run_in_background`
  ([Builds and tests](#builds)); it prints its log path first, and gives up waiting for a slot after 40 minutes,
  or sooner when the wait plus the build would pass its 45-minute budget; a longer wait runs `--detach`
  ([Build waits](#build-waits)).
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

## Builds and tests {#builds}

Six agents compiling at once leave every one of them waiting, so builds share the machine through claims, the way
PCSX2 copies do ([`build`](coney-tools.md#build)). An agent never runs `cmake --build`, `cmake --preset`, `ctest` or
`ninja` itself (a hook refuses them); it runs, from its worktree:

```sh
uv run --project python coney-tools build --agent <id> --preset dev --test [-R <regex>] [-L <label>]
```

- **Run it in the background** (`run_in_background`, or output to a file) and read the log path it prints first, then
  the short summary when it ends. Do not sit in it: a wait for a slot can take many minutes.
- **A small job is quick.** An incremental rebuild of a few steps (the tool counts what is out of date by itself, so
  one edited source is one step however much sits behind it), or a test run filtered with `-R` or `-L` to a few
  tests, takes the fast lane and does not queue behind full builds. Rerun a failed test with `-R <its name>`, not the
  whole suite; the first build in a fresh worktree and a full test run are the big jobs. `-L disc` runs only the tests
  that need the game's disc. `-R` is a CMake regular expression: escape `.` `+` `(` `)` with a backslash and quote it
  with single quotes ([Choosing tests](coney-tools.md#build)).
- **A play build is `--target coney`.** It builds the executable and what it links, not the tests; it cannot be
  combined with `--test`. Every build goes through the shared [compiler cache](building.md#compiler-cache), so the
  first build of a fresh worktree takes most of its objects from builds other worktrees already ran, and its summary
  prints the hits.
- **One run per build folder.** A second `build` in the same checkout and preset waits for the first (it prints who
  holds the folder), so start a build and its tests as one `--test` run rather than two commands, and never run two at
  once in one worktree. Each run writes a log of its own, named for the preset, the agent and the time; read the path
  it prints first.
- **A second configuration** (the Clang build clang-tidy reads on Linux and macOS) is `--build-dir build/tidy --env
  CC=clang-19 --env CXX=clang++-19` with `--configure-only`; never a raw `cmake`.
- **Exit code 75 means "still queued".** The command left the queue: its wait reached `--max-wait`, or the wait the
  queue still expects plus the step would pass the time budget (`--budget`), in which case it leaves at its first
  look. It said its position and the numbers. Do other work and run the same command again later, or detach it and
  stop ([Build waits](#build-waits)); do not loop on it in one call.
- **`--priority`** goes ahead of the queue. The coordinator uses it for a build somebody is waiting on; an agent does
  not set it on its own.
- **`build status`** shows who holds the slots and who waits. A crashed run is taken over by the next claimer, which
  also stops the `ctest` or `cmake` it left running; `build release --agent <id>` cleans up by hand.
- **A build never outlives its agent.** Its children end with the command, and the command stops when the shell that
  started it is gone (a restart of the agent tool, a killed background shell), so a run never needs killing by hand
  ([Process lifetimes](coney-tools.md#build)). Never stop `ctest` or a test executable yourself: stop the
  `coney-tools build` command, and everything under it goes with it.
- The coordinator sets the number of slots for the machine with `build_slots` in `coney.local.toml`
  ([Local workspace](workspace.md#local-config)) and does not stagger builds by hand any more.

## Build waits {#build-waits}

**No agent waits on a build past its prompt cache.** The cache lasts about an hour; an agent that sits idle longer
wakes up and writes its whole context again, which on 2026-10-12 cost about 1.1 M tokens in one day. So
`coney-tools build` enforces it:

- A call you wait on may wait at most 40 minutes for a slot (`--max-wait`, the default) and take at most 45 minutes
  in all, waits and steps together (`--budget`, the default). Larger values, and `--budget 0`, are refused (exit 2).
- The run estimates its wait from the queue (who holds the slots, how long their steps usually take) and each step
  from the median of its recent runs. When the two cannot fit the budget it leaves at once with exit 75, instead of
  sitting through most of the hour.
- When your wait would be long (exit 75, or `build status` shows a long queue), either do other work and run the
  command again later, or **detach it and stop**: `coney-tools build --agent <id> ... --detach` starts the same run
  in the background, apart from your shell, and returns at once. It may wait as long as it needs (default 240 minutes,
  no budget). Write `STATUS.md` naming the worktree and the command, and end your turn. Whoever continues runs
  `coney-tools build result --dir <worktree>`: exit 75 while it runs, then the run's summary, log path and exit code
  ([`build result`](coney-tools.md#build)).
- The local gate's clang-tidy waits in the same way, for a slot of the tidy lane: at most 40 minutes in a call you
  wait on, then exit 75. `coney-tools check changed --detach` runs the whole gate in the background; whoever continues
  runs `coney-tools check result` in the worktree ([`check`](coney-tools.md#check)).

## The PCSX2 MCP guard {#pcsx2-guard}

The `pcsx2` MCP server is one process for a whole agent session, so every agent in it (the coordinator and its
subagents) shares **one connection**. On 2026-10-21 that cost two runs: one agent's connection moved from copy `pcsx2`
to `pcsx2-b` in the middle of its work and a memory write landed in the other agent's game, and another agent's
`pcsx2_pause` held a neighbour's emulator for 10 to 20 seconds. The ports of every copy are readable by anyone, so a
hook checks the one call that picks the copy (`.claude/hooks/pcsx2_guard.py`, the rules in
`repo:python/src/coney_tools/pcsx2_guard.py`):

- **Connect only to your claim.** `pcsx2_connect` is refused unless **both** `debug_port` and `pine_port` are given
  (in every mode, so the arguments are always consistent; `mode: "debug"` still keeps PINE free), they are the ports of
  one copy, and that copy's live claim is held by an id you used in a claiming `coney-tools pcsx2 claim | launch |
  record | clip | mission | recipe ... --agent <id>` command. The hook reads that id from your shell commands, so run
  `coney-tools pcsx2 claim --agent <your id>` in a shell first (it hands your existing claim back); that is also how it
  knows who you are after a resume. The ports of a copy nobody claimed, of a stale claim and of a copy another agent
  holds are refused, naming the holder.
- **Your own connection, by name.** The PCSX2-MCP fork build v2.9.129.9 or newer gives every `pcsx2_*` tool an
  optional `agent` argument and keeps one DebugServer and Pine connection per name, so your `pcsx2_connect` replaces
  only yours (both halves, whatever the mode), a port another name is connected to is refused (`share: true` to use
  one copy on purpose) and `pcsx2_status` shows your name, the ports you are connected to, the game and the other
  names. `coney-tools mcp serve pcsx2` starts the server with `PCSX2_REQUIRE_AGENT=1`, so **pass `agent: "<your
  id>"` on every `pcsx2_*` call**, the id you claimed with. The hook **requires it on `pcsx2_connect`, whatever server
  runs** (a local-scope `pcsx2` entry or an older checkout may not require it itself, and two agents that leave it out
  share the server's `default` connection), and checks it wherever it is given: it must be an id of your claiming
  commands and, on `pcsx2_connect`, the id the copy is claimed as, so nobody acts in another agent's name by mistake.
  It does not check on a later call that the claim is still live (after a `release` or a takeover the old id still
  passes; the server frees an idle name within 30 minutes). Your tool list shows whether the installed server has
  the `agent` argument (it is among `pcsx2_connect`'s arguments). A command that gets no reply in time closes its
  connection (a late reply could answer your next command), and closing one fails the command waiting on it at once
  with `Connection closed`: reconnect with `pcsx2_connect`.
- **Release it when you finish.** `pcsx2_disconnect` (with your `agent`) closes your connection and frees your name
  and ports for the next agent on the copy; one idle for 30 minutes (`PCSX2_IDLE_MINUTES` in the server's
  environment) is released by a connect that wants its port, and the answer says so. Without that, the next agent to
  be handed your copy would be refused, and `share: true` switches the check off.
- **Nothing is locked.** There is no lease: every other agent of the session keeps using its own connection, and a
  lock on a shared one would let one engineer's task shut the others out of the emulator for hours.
- It does nothing on a machine with no PCSX2 copies set up or in a payload without a session id, and when it fails
  itself (an unreadable payload, an error inside it) it lets the call through and writes one line to `guard.log` in
  the `mcp/` folder beside the claims; when the package cannot even load, to `coney-pcsx2-guard.log` in the system
  temp folder. A hook killed by its 10 second timeout writes nothing. A binding (which ids a caller claimed with)
  expires 3 days after the caller last used it or claimed.
- **It stops mistakes, not impersonation.** The ids come from the text of your shell commands, so a caller that runs
  `claim --agent <another agent's id>` goes by that id (`claim` hands that agent's claim back to anyone who asks).

**Against an older server** (one connection for the whole process, no `agent` argument) the hook still stops a connect
to a copy you did not claim, but another agent's `pcsx2_connect` to its *own* claim moves yours (your next write then
lands in its game), and `mode: "debug"` leaves the PINE half of the previous connection. There, **call `pcsx2_status`
before the first write of a task and after any pause** to see which copy you are connected to, and prefer what needs no
shared server: `coney-tools pcsx2 ...` (`record`, `recipe`, `clip`) and, from a script, `DebugClient.claimed("<your
id>")` (`coney_tools.debug_server`), which connects to the debug server of the claim you hold and refuses any other.
`coney-tools pcsx2 check-ports --agent <id> --debug-port N --pine-port M` makes the claim check on a pair of ports.

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
