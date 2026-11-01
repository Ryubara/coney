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
| `.mcp.json` | The debugging servers: `ghidra-mcp` (the shared Ghidra project), `pcsx2` (the emulator, over the PCSX2-MCP fork's debug server and PINE) and `xemu` (the Xbox build in xemu, Coney's own server, [The xemu server](coney-tools-xemu.md#mcp)) | Claude Code; other tools take the same three commands in their own MCP config |
| `.claude/agents/` | The roles: `coordinator` (the main session), `engineer`, `tooling`, `reviewer` | Claude Code |
| `.claude/skills/` | The coordinator's skills: `repo-hygiene` ([the skill](repo-conventions.md#hygiene-skill)) | Claude Code |
| `.claude/settings.json` | Shared permissions: the project's checks, generators, tests and `coney-tools build` allowed; `git stash` and stopping programs by name refused ([Sharing one machine](how-we-work.md#shared-machine)), and `gh` commands naming a video file, which only `coney-tools clips publish` uploads ([Clips](testing.md#clips)); `export CONEY_AGENT_TOKEN=*`, the [identity](#identity) prefix the dispatcher adds; hooks, run by one dispatcher (`.claude/hooks/dispatch.py`, [The hooks](#hooks)), that refuse a raw `cmake --build`, `ctest` or `ninja` ([Builds and tests](#builds)), a wait loop with no bound ([Bounded waits](#bounded-waits)), a raw `gh` command that writes an issue (`.claude/hooks/issue_guard.py`, [Filing an issue](issues.md#filing)), an Agent call with `isolation: "worktree"` or with no Coney role as its type (`.claude/hooks/agent_guard.py`, [Where an agent works](#worktrees), [Which role to start](#roles)), a tool call the role of the session does not allow (`.claude/hooks/roles.py`), a call of the shared `pcsx2` MCP server on a copy the caller has not claimed (`.claude/hooks/pcsx2_guard.py`, [The PCSX2 MCP guard](#pcsx2-guard)), an `xemu` MCP call whose `agent` the caller never claimed with (`.claude/hooks/xemu_guard.py`, [The xemu MCP guard](coney-tools-xemu.md#xemu-guard)), the hand-typed ways it recognises of opening, updating, auto-merging or pushing a pull request, since `pull_requests.open_only` is on (`.claude/hooks/pr_guard.py`, [The PR guard](enforcement.md#pr-guard); a spelling check, not a boundary), and, inside a subagent, `git push` or a `gh` command that writes to GitHub (`.claude/hooks/push_guard.py`, [Opening the pull request](how-we-work.md#release-queue)) and, after a `git commit`, a `Co-Authored-By` trailer that names another model than the agent runs on (`.claude/hooks/trailer_guard.py`, [Commit trailers](#trailer-guard)) | Claude Code |
| `tools/claude-mod/` | Conclave, the Claude Code mod: a band, a status line, a pane, the shorter list of agent types and, for the coordinator, the watchers that tell it Coney's own events ([Conclave](conclave.md)) | Claude Code, with `--plugin-dir` |
| `.claude/hooks/` | The dispatcher `dispatch.py` that `settings.json` runs for every event ([The hooks](#hooks)), the guard scripts it runs, the role rules and identity injection (`roles.py`, `identity.py`), their settings `hooks.toml`, and `ruff.toml` (the package's rules; ruff lints the hooks like `python/`, [Conventions](conventions.md#python)) | Claude Code |

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
in the server, and the hook refuses a connect without it. Debug mode is the default to use: the debug server serves
several clients, but PCSX2's PINE serves one, and an MCP server holding it blocks `trace original` and every other PINE
client of that copy. Use `mode: "auto"` or `"pine"` only when you need PINE's own calls and nothing else uses the
copy, and never leave the ports out: the defaults (21512, 28011) are copy `pcsx2`'s, someone else's game. An agent
tool that starts one server per agent can run `coney-tools mcp serve pcsx2 --copy <name>`, which hands the copy's
ports to the server as `pcsx2_connect`'s defaults ([`mcp`](coney-tools-setup.md#mcp)); the hook still wants both ports in
the call, so those defaults serve a server run outside it. **A hook enforces this** ([below](#pcsx2-guard)). What the
tools do: [Driving PCSX2](research-tools.md#driving-pcsx2).

**The roles.** Each file in `.claude/agents/` names a role from [How we work](how-we-work.md) and points at the
guides that role reads; it holds no rules of its own. The engineer and the reviewer are set to Opus, which
AGENTS.md requires for reverse engineering (a coordinator may name Sonnet for an engineer whose task needs none, or for
a reviewer of a change with no research page or `@orig` function, per [Which model](#models)); the others take whatever
model the coordinator names. The engineer uses the Ghidra and PCSX2 MCP tools and writes `src/`. The `tooling` engineer
is set to Sonnet: it builds the project's own tools and process (`python/`, `.github/`, `.claude/`, the Claude Code mod,
build configuration and their guides) and does no reverse engineering. The `reviewer` reports and never edits ([The
reviewer](#reviewer)). Effort, turn and token limits are each user's own settings and stay out of the repository.

**The permissions.** The allowed commands are the project's own read-mostly ones: `coney-tools config show`, the
`repo` checks, the generators, pytest, ruff, mypy, pre-commit and `coney-tools build`. Commands that start or drive
PCSX2 (`coney-tools pcsx2 ...`), and git beyond Claude Code's own read-only detection, still ask. The refusals match
on spelling (`taskkill /IM` is refused, `taskkill.exe /Im` or a piped `Stop-Process` is not), so they catch honest
mistakes; they are not a security boundary.

**The agents' display.** On a machine with a display set aside for agents, every window an agent opens goes there,
never on the display a person is working on. It is set once, in the agent tool's own environment, so no brief or
memory has to carry it: `CONEY_WINDOW_DISPLAY` (`portrait`, a display number or part of its name;
[Which display](running-options.md#window-display)) in the environment the tool gives its agents, for Claude Code the `env`
block of your user settings (`~/.claude/settings.json`, `"env": {"CONEY_WINDOW_DISPLAY": "portrait"}`). Coney opens
its window centred on that display without taking the keyboard focus, and `coney-tools pcsx2 launch` and
`trace original` move each PCSX2 window there as it appears ([Driving PCSX2](research-tools.md#driving-pcsx2)). It
is each machine's own setting and stays out of the repository; a machine with one display leaves it unset. An agent
that runs anything else with a window puts it on the same display.

Another agent tool (Codex, Gemini CLI, Antigravity, Cursor, Copilot, Grok Build) is set up as
[Agent tools other than Claude Code](agents-other-tools.md) says: its own hook file, MCP entries and checks.

## The reviewer {#reviewer}

The `reviewer` agent (`.claude/agents/reviewer.md`, Opus) is the one review of a change, by someone other than its
author. It covers code, tests, tools, docs and commits, research pages, `@orig` functions against the original in
Ghidra (fidelity and own expression, the rubric of [Orig review](review/orig.md)), and the pull request description with
every picture it shows ([Description](review/text.md)). The checklists are the pages of [Reviews](review/index.md); the
agent file only says which to apply. Its tools are Read, Glob, Grep, Bash (read-only commands and the `coney-tools`
checks), Write (its verdict and report files only, which the role rules enforce), the ghidra-mcp read tools
(`decompile_function`, `disassemble_function` and the like) and the PCSX2 MCP tools that only read. It has no Edit
tool, no Agent tool, no Ghidra tool that writes and no PCSX2 tool that changes state. It starts no agents: the role
rules refuse the Agent call and, in the shell, `claude`, `coney-agent`, `task review --run` and `legacy pr-review`.

Start it for a task with the model the change needs: Opus for research pages and `@orig` functions, which `task review
--record` insists on, a smaller one for docs and tooling. Its brief names the task; it runs `coney-tools task review
<task>` from any checkout (the tool part and the packet, of the unit's own worktree), judges, writes `REVIEW.json` and
`REVIEW.md` and records the verdict with `task review <task> --record` ([Task review](review/task.md)). A person runs
`task review <task> --run`, which starts a headless reviewer. Any later commit, or an edit to the description, makes a
recorded review stale. A fix to the code after an `@orig` review therefore comes before the review of the description.

A branch started before the cutover, outside a task, is released with the [legacy release
checks](how-we-work.md#pr-review), which run the same agent and are retired at the cutover:

- **The description**, with `coney-tools legacy pr-review` ([legacy pr-review](coney-tools-repo.md#legacy-pr-review));
  the verdict is `REVIEW.json` next to `PR.md` ([Description](review/text.md#legacy-verdict)).
- **An `@orig` branch, in pr mode** (`orig check-review`): give it the branch (or its worktree), the base and the
  scratch folder that holds `PR.md`; it writes `ORIG-REVIEW.json` and `ORIG-REVIEW.md` there. When `orig review-stamp
  --branch` lists no function, no run is needed. A rebase that leaves the functions as they were keeps the verdict.

**Sweep mode** reviews existing code for [legacy fidelity](how-we-work.md#fidelity-audit), until the sweep is filed
(it ends at the [cutover](how-we-work.md#cutover)). Give it a `src/` folder or
a list of addresses and a group folder under `scratch/orig-sweep/`. It reviews what `orig review-stamp --folder` lists
as unreviewed or stale, writes `FINDINGS.tsv` and `PROGRESS.md`, records the findings with `coney-tools orig
record-findings`, and returns a summary that the launcher saves as `SUMMARY.md`. Run one agent per folder, and leave out
the folders that a held claim's scope covers. The coordinator files each finding that is not `clean` or
`diff-deliberate-ok` as an issue, which becomes an ordinary task.

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

## Which role to start {#roles}

Every session and every Agent call runs as one of Coney's roles (the files in `.claude/agents/`), never as the generic
"general-purpose" or "claude" agent, which carries none of the repository's rules for the kind of work. The hooks know
the role from the hook input's `agent_type` (a subagent's type; for the main session, the agent it was started as with
`claude --agent coordinator`, which [the session launcher](#session-launcher) does), and hold each role to what it may do:

| Role | Is | May not (a hook refuses it) |
| --- | --- | --- |
| `coordinator` | The main session: triage, briefs, answers, pushes and opens pull requests, merges, the maintainer's contact | Write under `src/`, `tests/`, `python/`, `docs/research/`, `research/`; `git commit` in a unit's worktree; the Ghidra and PCSX2 tools; the study, compare, fails-first, check and review commands of the [pipeline](how-we-work.md#the-pipeline), except `orig fails-first --accept-change`, which only the coordinator runs ([a changed test](stamps.md#changed-test)); the hook matches that option in any letter case and any prefix argparse takes (`--ac` to `--accept-change`) and refuses it in a command that also has `--none` or `--keeps` |
| `engineer` | A subagent: one task, any method | `git push` and every GitHub write, `coney-tools pr open`, `pr hold`, `claim unclaim` and `claim renew` |
| `tooling` | A subagent for the project's own tools and process | The same as the engineer; it may edit `src/` (warning fixes) |
| `reviewer` | A subagent that reports | `coney-tools pr open`, `pr hold`, `claim unclaim` and `claim renew`; writing any file but its verdict and notes (`REVIEW.json`, `REVIEW.md`, `ORIG-REVIEW.*`, `FINDINGS.tsv`, `PROGRESS.md`, `HANDOVER.md`, `SUMMARY.md`, and `CODE-REVIEW*.md` for the report of a [review outside `task review`](review/task.md#report)) or any of those inside a checkout or with a colon in its name, a git command that changes a ref, the index, the tree or the worktrees (`commit`, `add`, `stage`, `rm`, `mv`, `reset`, `restore`, `checkout`, `checkout-index`, `switch`, `merge`, `mergetool`, `rebase`, `cherry-pick`, `revert`, `apply`, `am`, `clean`, `pull`, `fetch`, `bisect`, `stash`, `read-tree`, `update-ref`, `update-index`, `sparse-checkout`, `replace`, `pack-refs`, `gc`, `prune`, `repack`, `maintenance run`, `filter-branch`, `symbolic-ref` with two names or `-d`, `worktree add`/`move`/`remove`/`prune`, `notes add` and the other note writes, `reflog expire`/`delete`, `remote add`/`remove`/`rename`/`set-url`/`set-head`/`set-branches`/`update`/`prune`, `submodule update` and the other submodule writes, and `branch` or `tag` with anything but listing options, also after global options such as `--no-pager` or `-C dir`; `merge-base` and `merge-tree` pass, and so do `stash list`, `worktree list`, `remote -v`, `remote show`, `symbolic-ref HEAD`, `branch -a`, `tag -l`), GitHub writes (but a passing `task review --record` posts the claim command `/progress reviewed` itself, as every passing stage stamp does: [Stamps](stamps.md#api)), any Agent call (`allow_agent_types = []`: a helper has Bash and could write for it), starting a session from the shell (`claude`, `coney-agent`, `task review --run`, `legacy pr-review`) |
| `contributor` | A session of an agent tool other than Claude Code whose `CONEY_AGENT_ROLE` names no other role (`[harnesses] default_role`; no file in `.claude/agents/`, [Agent tools](enforcement.md#harnesses)) | `git push` and every GitHub write (its person pushes and opens the pull request), `coney-tools pr open`, `pr hold`, `claim unclaim` and `claim renew`, Edit or Write of the hook files (`.claude/hooks/`, `.claude/settings.json`, `.claude/settings.local.json`, `.codex/`, `.gemini/`, `.agents/`, `.cursor/`, `.github/hooks/`, `.github/copilot/`, `.grok/`) |
| every role, and a session with none | | Naming `~/.coney` ([Who runs a command](#identity)); naming a file in `~/.ssh/` other than a `.pub`, `config` or `known_hosts`; reading another process's command line or environment; deleting keys from the SSH agent (`ssh-add -d`, `-D`, also bundled with other flags such as `-qD`; listing with `-l` is fine); Edit or Write under a `stamps/` or `build-runs/` folder and a shell write into one (a stamp log or build record is written by the stage commands only, [Stamps](stamps.md)) |

The role rules are the `[roles.*]` tables of `.claude/hooks/hooks.toml`, read on every call; the rules for every
session are built into `.claude/hooks/protect.py`, so a settings file that cannot be read does not take them away and
no setting weakens them. They read the fields that name a path (a Read's file, a Glob's pattern, a shell command's
words, every string of an MCP tool's input), after folding `./`, `..`, case, both slashes and `~`, `$HOME` and
`%USERPROFILE%` together, and the aliases Windows gives a folder (trailing dots and spaces, `SSH~1` for `.ssh`, the
real path of a short name); not a search pattern, a commit message or the text a file is given, so `grep
".coney/identity" docs`, `grep "Win32_Process" docs`, `git log -S`, `pytest -k Win32_Process` and
`git commit -m "Refuse Win32_Process"` are fine (text the shell would run, `$(...)` or a backtick, is still read).
Some name commands that are planned (`pr open`) so that they are held from the day they land. A hook
judges spelling: a command can be hidden from it, so these rules make a break visible and deliberate, not impossible.
A setting of the wrong shape is ignored and an invalid pattern skipped, so one bad entry never disables the rest.

**Asking the coordinator.** An agent that cannot go on without an answer, and whose report would come too late, writes
`QUESTION.md` in its scratch folder (`QUESTION-<n>.md` for several), the first line the question and the lines after
it what it needs to decide. [Conclave](conclave.md#questions) tells the coordinator, who answers by message or in
`ANSWER.md` (`ANSWER-<n>.md`) beside the question; read that file, or look for the message, before going on. A
question that can wait goes in the report. The coordinator reads these files for every branch with a worktree.

Pick the role of an Agent call by what the agent does:

| Start | For |
| --- | --- |
| `engineer` | Anything that writes code, tests, tools, research pages or docs, or opens Ghidra or PCSX2; also upkeep and measurement runs. When unsure, this one |
| `tooling` | The project's own tools and process: `coney-tools`, workflows, hooks, build configuration and the guides that describe them |
| `reviewer` | A review of a change by someone other than the author: code, tests, tools, docs, commits, research pages, `@orig` functions and the pull request description ([The reviewer](#reviewer)) |
| `Explore`, `Plan`, `claude-code-guide` | A read-only search of many files, a plan that writes nothing, a question about Claude Code |
| `fork` | A short job that needs the coordinator's conversation ([Who does the work](#who)) |

The model is separate: name it on every call ([Which model](#models)). `agent_guard.py` refuses a call with no
`subagent_type` (which Claude Code runs as general-purpose), an empty one, `general-purpose`, `claude`, `coordinator`
(a main-session role) or any type that is neither a role nor one of the helpers above, so Claude Code's other built-in
types (`statusline-setup`, for one, which edits settings) are refused too. It reads the roles from the files in
`.claude/agents/` each time, so a new role file works at once, and names the allowed types in its refusal; start the
agent again with the right one.

**A session with no role.** `require_role` in `hooks.toml` says what a session started without a role (a plain
`claude`) may do. On, it may only read (Read, Grep, Glob) and every other call is refused with a message to start with
`coney-agent`. It is **on in the committed file**: the maintainer's sessions start through `coney-agent`, and every
agent they start carries its own role. Off, such a session would get no role rules at all. Whatever its value, a
session start with no role shows a notice (a SessionStart hook). To keep the conversation of a session that has no
role, quit it and start again with `coney-agent -- --continue` (the latest conversation in the folder) or
`coney-agent -- --resume` (pick one): everything after `--` goes to `claude` unchanged.

## Which model {#models}

Match the model to the judgement the task needs, not to its size. A cheaper model that has to redo work, or that
sends a branch through CI three times, costs more than a stronger one that gets it right once.

| Model | Use it for | Examples |
| --- | --- | --- |
| **Claude Opus** | Reverse engineering of any kind, and implementation that needs judgement: reimplementing a system faithfully from a research page (numeric fidelity, timing, AI, physics, rendering, combat), work across several subsystems, diagnosing a failure whose cause is unknown | Engineers in Ghidra or the emulator; combat, physics and renderer work; fixing a red CI with unknown causes; the reviewer of research pages or `@orig` functions |
| **Claude Sonnet** | Well-specified implementation and upkeep, where the research page or an existing pattern says exactly what to write | Wiring a documented script binding, a HUD panel built to its spec, a new test following an existing one, generators, doc upkeep; the reviewer of a docs or tooling change |
| **Claude Haiku** | Mechanical work with a clear check and nothing to decide | Running a command and reporting the result, measurements, renames, regenerating pages, simple searches |

Claude Fable is not used for agent work. Always name the model when starting an agent; never rely on the default.

**Coney Studio units have their own rule** (maintainer decision 2026-10-28): research runs on Opus at high effort,
development on Opus at medium effort when the task needs reasoning or on Sonnet at high effort for simple
implementation, as the coordinator decides per task. It names both `model` and `effort` in the Agent call, and the
guard refuses any pair outside `tasks.start.slots.tracks.studio.models` ([Engineer tracks](#slots)).

**Escalate on signals, not on hope.** Move a task up a model when the agent fails the same check twice, has to make
a call the page does not settle, or touches a subsystem it was not briefed on. Record which model did which kind of
task and how often its work came back (CI failures, review findings, play-test bugs) in the coordinator's state file,
so the table above follows evidence.

## Briefing a fresh agent {#briefs}

A brief is part of the context the agent carries on every request, so it is short and points at the repository
instead of repeating it:

- the role (engineer or support), the model, the worktree and the scratch folder;
- for an `engineer` or `tooling` agent, the unit, as a line of its own that starts with the words, `Task id: <id>`
  (the id `task create` printed once its claim held; the engineer runs `task start <id>`): the coordinator's start
  of the agent is refused without one, or when the claim is no longer held ([Engineer tracks](#slots));
- the task, its scope, the files not to touch, the [pending work that overlaps it](pending-work.md) and what
  "done" means ([Testing](testing.md#done)), and that it reports it in the Testing and Screenshots sections, with
  its captures saved in `scratch/<branch>/shots/` and referenced in `PR.md` as `![alt](./shots/file.png)`
  ([Capturing and attaching](testing.md#pr-media)); video clips are recorded only with `coney --record` or
  `coney-tools clips record` into `scratch/<branch>/clips/` and uploaded only with `coney-tools clips publish`, which
  deletes them ([Clips](testing.md#clips));
- for gameplay work (anything a player sees: the title areas `gameplay`, `graphics` and `audio`), the brief names the
  [runtime comparison](testing.md#runtime-comparison) with the original as part of "done", and the `Runtime:` line of
  the pull request's Testing section that reports it ([the Runtime line](testing.md#runtime-line)); it points at
  `coney-tools pcsx2 recipes` for the PCSX2 recipes that capture and read the original (a save state's values and
  picture, a trace; being added, with its own guide page), and the agent works in a PCSX2 claim
  ([Driving PCSX2](research-tools.md#driving-pcsx2)). An agent that is not told compares nothing with the running
  original, and the pull request is then refused;
- "Read `AGENTS.md`, [How we work](how-we-work.md), [Testing](testing.md) and this page first; they override the
  brief";
- how it runs ([Running without losing the cache](#cache)): it works local-first, never pushes or waits on CI, and
  is done when its task passes Review; the coordinator then opens the pull request
  ([Opening the pull request](how-we-work.md#release-queue));
- disk: it writes its id into `.coney-busy` in its worktree as it starts (`echo <id> > .coney-busy`; an empty marker
  older than a day is ignored by the cleanup) and deletes it when it finishes, and before reporting a
  unit finished it deletes its own `build/` folder; a build is about 6 GB per preset, and with the shared
  [compiler cache](building.md#compiler-cache) a fresh one builds in about 2 minutes, not 11
  ([Cleanup](workspace.md#cleanup));
- builds: "build and test only with `coney-tools build --agent <id> ...`, in the background" ([Builds and
  tests](#builds)); the brief does not paste the flags;
- the stages, as part of "done": `coney-tools task check` on the committed tip, then `task review` by a reviewer
  ([The pipeline](how-we-work.md#the-pipeline), [The local gate](testing.md#local-gate)); `coney-tools ci run`
  is an aid while working, not the gate;
- only the facts the agent cannot find in the repository (a log path, a question to answer, the state file).

Engineers who drive the original use the [PCSX2 recipes](pcsx2-recipes.md) for the basic things (finding a human,
teleporting, spawning, calling the game, loading a checkpoint) instead of working the addresses out again, and add a
recipe, confirmed in PCSX2, when they find a new technique, and start from the [state library](pcsx2-states.md)
(`coney-tools pcsx2 states list`) instead of driving to the same place again; a brief need not repeat either.

A brief may be just "issue #N": the issue holds the steps, the screenshots and the acceptance test, and the agent
reads it as [Issues](issues.md#agents) describes. Naming the number is cheaper than pasting the issue, and the
issue stays the one place to read it.

## Engineer tracks {#slots}

Engineer work runs in five **tracks**, and **no engineer or tooling agent starts without a held claim**. The rules are
`tasks.start.slots` in [.github/conventions.yml](repo:.github/conventions.yml); there are no teams, and the coordinator
keeps the places full ([Keeping the places full](slot-fill.md)) from the open issues, `P-critical` first.

| Is | What | Its rule (each of the first five needs a held claim, a fix its parent's, and a place) |
| --- | --- | --- |
| **Reimplementation** | An `engineer` or `tooling` agent on a task whose issues are labelled none of `studio`, `networking` and `renderer`: game issues and pull requests outside Coney Studio, networking and the renderer | At least one game (`feature`) unit always (`min: 1`, `min_kinds`): while none runs, one place is kept for it, which Studio and tooling or docs units may not take. No cap but the machine's |
| **Fix** | An agent on a `kind: fix` task, made by `task create --fix-of` after a CI check failed on the pull request of a task that passed Review | One at a time (`max: 1`), oldest failure first; one place is kept for it (`min: 1`), so a full machine still takes a fix, except one with a single build slot |
| **Studio** | A task whose issue carries the `studio` label (`claims.studio_label`): Coney Studio research and development, the sub-issues of #1054 and the Studio build issues | As many as the machine holds once the kept places are set aside, on one of the models of [Which model](#models) |
| **Networking** | A task whose issue carries the `networking` label (`claims.networking_label`): the co-op plan in [multiplayer](multiplayer.md#plan), network ids, snapshots, interpolation, the level-load barrier | One at a time (`max: 1`) and always one (`min: 1`): while none runs, one place is kept for it, so a full machine still takes it. Any model, as the reimplementation track; its claim is outside the open-claims limit ([Claiming an issue](issues.md#claiming)) |
| **Renderer** | A task whose issue carries the `renderer` label (`claims.renderer_label`): the SDL3 GPU migration (design #883), Coney's own renderer on SDL3's GPU API with librw kept only for reading data | As networking: one at a time (`max: 1`) and always one (`min: 1`), with its place kept, any model, and its claim outside the open-claims limit. An issue with `networking` and `renderer` is a networking unit; with `renderer` and `studio`, a renderer one |
| **Workflow unit** | A task whose issue carries the `workflow` label (`claims.exempt_label`) now: a unit that builds the new workflow | A held claim, no place, while `tasks.start.slots.exempt_workflow` is on; **the cutover turns it off** (a test fails if `claims.adopt` or `claims.release` is on while it is still on) and such units count like the rest |
| **Reviewer**, **helper** | A `reviewer` ([Task review](review/task.md)); an `Explore`, `Plan` or `claude-code-guide` agent, a `fork`, and any agent an engineer starts for its own task (no cap) | No claim, no place, no `Task id:` line |

The labels are read from the issue's held claim card (`workflow`, `studio`, `networking` and `renderer`, recomputed by
the claim Action every pass), an issue with `workflow` and another label is a workflow unit until the cutover, and the
claims of all four are outside the open-claims limit ([Claiming an issue](issues.md#claiming)). There is no `Slot:
none`: an `engineer` or `tooling` agent has a unit, or it is not started. Other repositories' work is not counted.

**The machine limit.** All tracks together, a machine holds at most `build_slots` (its build slots in
`coney.local.toml`, the ones [`coney-tools build`](#builds) keeps) times `per_build_slot` (2) units in work: 10 on a
machine with 5 build slots, 4 on one with the default 2. Of those, the places a track keeps (`min`) while it runs fewer
are set aside for it: with 10 places and none of the other tracks running, Studio may take 6. The number 2 was measured
on 2026-10-28 from the build records (`<scratch>/<task>/build-runs/`) and the unit records: from `task start` to Review,
a `feature` unit held a build slot for 10% of its life (5 units, 52 hours, 5.3 of them building) and a `tooling` unit
for 2% (19 units); over about 600 build runs since 2026-10-24, five or more builds ran at once for 1.7 of the 52.7 hours
any ran. Two units per build slot put the builds' demand at about a fifth of the slots; past that, the limit is the
unslotted checks (8 at once made the Python tests of `task check` take 80 minutes, not 48). The tracks keep 4 places
together, so on the default 2 build slots (4 places) they are all kept, and one game unit fits beside the fix,
networking and renderer places. A machine with one build slot (2 places) sets aside at most all but the starting unit's
place, so an empty one starts a unit of any track, Studio's too. `per_build_slot` changes only after measuring again the
same way.

**The place belongs to the unit, not to the agent's life.** A unit takes its place at `task start` (an agent the
coordinator has just started holds it from the Agent call, for at most `reserve_minutes`, so that several calls made
together are counted one after the other) and frees it at the first of: a passing `reviewed` stamp for it (a fix: its
own), `finished`, or its worktree gone. **An engineer's job ends when its unit passes Review**: it reports, and the
coordinator starts the next task in the place while CI runs; the engineer is not kept for CI. A unit whose stamp log
or `unit.json` cannot be read keeps its place. A unit that was abandoned (its engineer died before Review) holds its
place until its worktree is removed (`git worktree remove`, after anything worth keeping is committed or saved) or a
`finished` stamp is written for it; `task slots` shows who holds what. A reservation of an agent whose call was denied
or never started lapses after `reserve_minutes`, except that of a fix task with no `unit.json`: it holds its place until
its `slot.json` is deleted or its pull request is over ([why](slot-fill.md#fix-tasks)). To start sooner, delete the
`slot.json` (the refusal and `task slots` name it).

**A CI failure is a fix task.** For a required check that fails on a pull request the coordinator makes a `kind: fix`
task (`task create --fix-of <issue>`) and starts an agent on it when the fix track is free: one at a time, **the oldest
failure first** (`task slots` lists them so; the guard refuses no younger one). If the engineer of the original task
still runs, the fix goes to it. The fix is held to the failing check; only the stamps it made stale are redone ([When a
pull request fails checks](#ci-failures)). A fix of a Studio task is in the fix track.

**What the guard checks.** `.claude/hooks/slot_guard.py` runs for every Agent call of the coordinator (last, only when
no other guard refused it) and asks `coney-tools task slots --admit`, with the brief on standard input and the call's
`model` and `effort`. For an `engineer` or `tooling` agent (the types in `tasks.start.slots.agent_types`, in any letter
case) the brief must carry a line `Task id: <id>` that starts the line, with no list mark, quote or bold in front of it
(so a brief that quotes the rule does not satisfy it; the first such line counts, and Windows line ends are read like
any other), and the call is refused unless all of these hold:

1. **The task record exists**: `<scratch>/<id>/TASK.md`, which `task create` writes only after its claim held.
2. **The claim is held now**: each issue of the task has a claim card on GitHub, held by the task's claimant for this
   task (a fix's: its parent's). One comments read per issue, bounded (`GH_CHECK_SECONDS`, 20). A claim that was
   released since `task create` (idle too long, `/unclaim`, lost) refuses the start.
3. **A Studio unit names an allowed model and effort**: the Agent call's `model` and `effort` are one of the pairs of
   `tasks.start.slots.tracks.studio.models` ([Which model](#models)); a call that names neither is refused too.
4. **Its track has room**: its cap (`max`) is not reached, and the machine has a place once the places other tracks
   keep are set aside. The kind comes from `TASK.md`, the liveness from `unit.json`, its worktree and the stamp log
   (`task start` counts the same way), the labels from the claim cards and the build slots from `coney.local.toml`, so
   the hook copies none of it. A refusal names the reason, the busy units and what frees a place. A second start for a
   unit whose agent has not run `task start` yet is refused too (a workflow unit keeps no reservation, so not for it).

A start that is let through keeps the place for the agent (`slot.json`, which names its track) until its `task
start` makes the unit. Every GitHub read happens before the slot lock is taken, so a slow GitHub never holds the lock.

**When GitHub cannot be read** (down, rate-limited, `gh` logged out, or hanging), only the claim and the labels go
unchecked. A read is killed after 20 seconds (`GH_READ_SECONDS`), the first read that fails ends the reading (every
other issue is then taken as unreadable at once), and no read starts once 20 seconds (`GH_BUDGET_SECONDS`) have passed
since the first, so the reads take at most 40 seconds however many units are live; with the 40-second lock wait that is
`ADMIT_MAX_SECONDS` in `task_slots.py`, and the hook waits 110 seconds (`TIMEOUT` in `slot_guard.py`, held above it by a
test). A unit whose card was not read counts as unlabelled (a fix as a fix, any other as reimplementation, none as a
workflow unit) and no model is checked; the place is still counted under the lock and kept, so a full machine still
refuses. Such a start leaves a line `let through with a check skipped` in `coney-hooks.log`.

**What the guard cannot do.** It is a hook that judges what the coordinator writes, not a security boundary:

- **A tool that cannot judge passes the call.** If `coney-tools` crashes, `uv` is missing, the check times out (110
  seconds) or the slot lock cannot be had (held for 40 seconds, or a scratch folder that cannot be written), the call
  goes through uncounted and a line is written to `coney-hooks.log` in the temp folder (only the tool's own refusal,
  exit code 3, blocks). A GitHub that cannot be read is narrower: the claim alone is not checked (above). Read that log
  when agents start that should not have.
- **Sessions a person starts are not Agent calls.** An engineer session started from the terminal or the launcher
  (`claude --agent engineer`, `coney-agent`) is not seen by the guard; its `task start` is the only check.
- **The first `Task id:` line wins.** A brief with two different ones is judged by the first; the agent may read the
  other.
- **The brief is the coordinator's word.** A `Task id:` of a live unit passes whatever the agent is then told to do (a
  restart after a `/clear` is the same call), so the engineer's own `task start`, which refuses a start over the limit
  and checks the claim, is the real backstop; the guard moves the refusal to where the coordinator sees it. Whether a
  Studio unit is research or development is the coordinator's call too: the guard checks only that its model and
  effort are one of the allowed pairs.
- **Only units with a `unit.json` are counted** (and fixes with a slot). `unit.json` is written by `task start` of the
  new workflow, so the count binds nothing that predates it: the worktrees of the old workflow hold no place, and the
  coordinator counts them by hand until they end. `task slots` lists the worktrees no unit record names ("without a unit
  record") so the gap is visible.
- **The agent type is the caller's choice**, read from `subagent_type` (any case, blanks stripped; `agent_guard.py`
  refuses an unknown or missing type, so a spelling variant of `engineer` does not get past either). A call that picks
  a type that needs no claim for work that writes (`Explore` and `Plan` have Bash; a `fork` is the coordinator's own
  conversation, and `reviewer` writes only its verdict files) is not caught: the role rules in `hooks.toml` limit
  what those types may do, not what the coordinator says they are for.
- **Only the coordinator is held to it.** An engineer's own helpers are not checked.
- **`task start` counts without the lock.** Two engineers started by hand without a reservation can both pass `task
  start` at the limit; with the guard each is counted as it is reserved.
- **A lock left past its stale age can let two calls count at once.** The slot lock is broken when it is older than
  30 seconds. A holder that is stuck rather than crashed then overlaps with the caller that broke its lock. And if
  one waiter (A) renames the stale lock aside just after another caller (B) broke it and took a fresh one, and a
  third caller (C) takes the free lock before A can put B's back, B and C are both inside the count until one of
  them releases (`_break_stale` in `task_slots.py`, and the test that reproduces it). That needs a stuck holder and
  three callers, or a crashed one and four (the one that left the lock, A, B and C), within one rename; the effect
  is that two calls may both take the last free place: one start over the limit.

`coney-tools task slots` shows the machine's places and each track's units at any time
([task](coney-tools-repo.md#task)); [Conclave](conclave.md) and Tether keep their own state (the band's units, the live
agents) and are not asked to count places.

## Where an agent works {#worktrees}

Every agent works in a worktree at `../../worktrees/coney/<branch>/` on a branch its brief names, and creates it itself
(`git worktree add ../../worktrees/coney/<branch> -b <branch> origin/main`); an engineer given a task that `task create`
made runs `coney-tools task start <task id>` instead, which makes the same worktree and the rest of the unit ([Starting
a task](issues.md#starting)). **Never start an agent with the Agent tool's `isolation: "worktree"` option.** It creates
the worktree for you under the main checkout's `.claude/worktrees/` with an automatic branch name, which is outside the
layout [AGENTS.md](repo:AGENTS.md) fixes: the virus scanner exclusion a machine sets for the worktrees folder (without
it a build starves the CPU) does not cover it, the `.coney-busy` marker and [the workspace
cleanup](workspace.md#cleanup) do not look there, and the branch name is not the task id that the unit record, the
stamps and `pr open` use. A hook (`.claude/hooks/agent_guard.py`, run by the dispatcher for the Agent tool) refuses the
call and points here; start the agent again without `isolation`. The same hook refuses an agent that is not one of the
roles ([Which role to start](#roles)).

## The hooks {#hooks}

`.claude/settings.json` registers **one** hook script, `.claude/hooks/dispatch.py`, for six events: before every tool
call (`PreToolUse`), after a shell command (`PostToolUse`), when a shell command fails (`PostToolUseFailure`) or is
denied (`PermissionDenied`), when a session starts (`SessionStart`) and when the coordinator ends a turn (`Stop`).
A process per hook cost a Python start-up on every shell command; now there is one, and the checks run inside it:

| Event | What runs |
| --- | --- |
| `PreToolUse` | What no session may touch (`protect.py`, first); the role rules (`roles.py`); a refusal of a command that sets `CONEY_AGENT_*`; the guards `build_guard.py`, `issue_guard.py`, `comment_guard.py` (no comment under the maintainer's account, [the comment guard](enforcement.md#comment-guard)), `bounded_waits.py`, `pr_guard.py` and `push_guard.py` for the shell tools, `pcsx2_guard.py` for the shell commands that name PCSX2 and the PCSX2 MCP tools, `xemu_guard.py` the same for xemu ([The xemu MCP guard](coney-tools-xemu.md#xemu-guard)), `agent_guard.py` and `slot_guard.py` (the coordinator's [engineer tracks](#slots)) for the Agent tool, `lua_guard.py` (no decompiled game Lua, [`lua`](coney-tools-fidelity.md#lua)) for Write, Edit, MultiEdit and NotebookEdit; then the identity token |
| `PostToolUse` | `trailer_guard.py` after a `git commit`; the command's identity file is removed |
| `PostToolUseFailure`, `PermissionDenied` | The command's identity file is removed (a failing build or a denied command would leave it for its ten minutes) |
| `SessionStart` | The notice about a session with no role, or about settings that could not be read |
| `Stop` | `slot_fill_guard.py`: a coordinator turn cannot end while an engineer place is free with work to start and no reason ([Keeping the places full](slot-fill.md)) |

Of the thirteen guard scripts, `comment_guard.py`, `slot_guard.py`, `slot_fill_guard.py`, `lua_guard.py` and
`xemu_guard.py` were written for the dispatcher (`lua_guard.py`'s file mode is the pre-commit hook `no-decompiled-lua`;
the slot guards ask `coney-tools`, so they copy nothing from the conventions), and five run exactly as they did as
separate hooks (`build_guard.py`, `issue_guard.py`, `bounded_waits.py`, `push_guard.py`, `pcsx2_guard.py`);
`trailer_guard.py` is unchanged but the dispatcher starts it only for a real `git commit`; `pr_guard.py` and
`agent_guard.py` run as before and have a rule more each (the `pull_requests.open_only` switch, the role check). The
dispatcher gives a guard's `main()` the hook input and takes its exit code and stderr (the `Stop` guard answers
`judge(payload)` instead), so a guard stays a script of its own with its own tests. The refusals of all guards come back
together, as Claude Code's JSON answer (`permissionDecision: deny` with the reasons before a tool runs, `decision:
block` after it) with exit code 0, shown to the agent; never exit code 2, which Claude Code also gets from a launcher
that fails ([Nothing turns the guards off](#hooks)). The settings are in `.claude/hooks/hooks.toml` (hand-written TOML
that needs no packages, so it cannot load `.github/conventions.yml`). There are two switches, both on. `require_role` in
`hooks.toml` ([Which role to start](#roles)). And `pull_requests.open_only` in `.github/conventions.yml` (on since
2026-10-27): the PR guard then refuses, in any session, the hand-typed ways it recognises of opening, updating,
auto-merging or pushing a pull request, so `pr open` is the release route ([The PR guard](enforcement.md#pr-guard) lists
them, [After the switch](pr.md#after-the-switch) gives the commands). The second is a conventions key, not a line of
`hooks.toml`, so the rule has one source: `pr_guard.py` reads that one line itself (`read_open_only`; a file it cannot
read leaves the switch off), and a test holds its reader to the conventions loader. The hooks run from the checkout the
session was started in, so a change to them reaches a session when that checkout has it.

**Nothing turns the guards off, and a guard that cannot start blocks nothing.** The guards are what
keeps agents from breaking the shared machine, so only a guard's own failure may stop that guard. A settings file that
cannot be read (not UTF-8, invalid TOML, a table of the wrong shape) gives the defaults (no role rule is on); a rule
module that does not import (`hooklib.py`, `roles.py`, `identity.py`, `protect.py`) is skipped alone, `protect.py`
apart from the others, since its rules are built in; a rule that crashes is skipped and the others still run; a guard
that crashes passes the call alone. Each case leaves a line in `coney-hooks.log` in the temp folder, the next session
start says that the settings could not be read, and the hook command pins the interpreter (`uv run --python
">=3.12"`) so an old default Python cannot fail an import.

The other failure is before the script runs: uv finds no Python 3.12 or newer (offline, downloads off), or the
checkout the session was started in no longer has `dispatch.py` (it moved to an older commit; Claude Code keeps the
hooks it read at start). uv and Python then exit with code 2, which for a `PreToolUse` hook with the matcher `*`
blocks every tool call, `Read` included, and the agent cannot recover. So the command in `settings.json` ends in
`|| exit 0`, and the dispatcher refuses only through its JSON answer. A session in that state runs with no guard, and
`coney-hooks.log` has no line for it; if the guards seem to be missing, run the command by hand.

### Who runs a command {#identity}

The tools that stamp work or check a commit need to know which agent ran them, and an agent's word cannot be trusted:
spelling cannot stop a shell from setting a variable (`export "CONEY_AGENT_TYPE=x"`, `export $name=x`, `source
file.sh` all get round a pattern). So the tools do not read the id, type or model from the environment at all. Before a
shell command that mentions `coney-tools`, or runs `git commit`, the dispatcher writes the caller's identity to
`~/.coney/identity/<hash>.json`, from the hook input and the agent's own transcript, and puts **only the token**, 128
random bits, in front of the command; the file is named by the SHA-256 of the token, so listing the folder gives no
token away:

```text
export CONEY_AGENT_TOKEN=<32 hex digits>; <the command>
```

`coney_tools.identity` reads the file the token names, as the first thing a `coney-tools` command does. A token that is
typed, changed or expired finds no file, so the tool records the identity as **unverified** (every field `unknown`),
never as another role; `stamps.who_from_env` takes it from there. The file holds the id (the
subagent's, or `main`), type (or `none`), model (the id of the agent's latest turn in its transcript, or `unknown`),
the session, agent and tool call it was written for, and when it expires (`[identity] ttl_seconds`, 10 minutes). It is
read, not consumed, because one command can run several tools (`coney-tools ... && git commit`); the dispatcher removes
it when the command ends, fails (`PostToolUseFailure`) or is denied (`PermissionDenied`), and a call that issues a
token removes the expired files and leftover temporary ones. A background command (`run_in_background`) keeps its file
for the sweep, because the end of the call comes as soon as the process is started, before uv has started the tool. A
tool that runs for long has read the file at its start and keeps the answer; a tool that starts after the file's life,
such as a later `coney-tools` in a chain that runs past 10 minutes, finds none and is unverified. The folder is 0700
and a file 0600.

What an agent can still do: read its own token (it proves only its own identity); write an identity file itself, which
needs the folder, and the hooks refuse every session to name any of `~/.coney` (and `~/.ssh/`, the stamp logs
and other processes' command lines, [Which role to start](#roles)), but that is spelling, so a deliberately built path
gets through; or read another command's token from the command line of its running shell, in a process listing (`ps`,
Task Manager) for as long as that command runs, which no hook can hide (the readers of `/proc/<pid>/cmdline` and
`environ`, `wmic process` and `Win32_Process` are refused, `ps` is not). The protection is that a forgery is visible and
deliberate, that a token lives minutes, and that a typed variable is worth nothing; it is not that a forgery is
impossible. A command that sets `CONEY_AGENT_*` is still refused, as a courtesy, with that text.

Why a prefix: Claude Code lets a `PreToolUse` hook change a call only by returning a new input (`updatedInput`); it
cannot set the environment of a Bash call. Measured on 2026-10-22 with `claude -p` 2.1.309:

- The rewritten command is checked against the permission rules like any other. `export ...; <command>` keeps the
  `Bash(...)` allow rules working because Claude Code checks each part on its own, so `.claude/settings.json` allows
  `Bash(export CONEY_AGENT_TOKEN=*)`. That rule allows only the export part ([the measurement](enforcement.md#hooks)).
  A prefix of the form `NAME=x <command>` would stop `Bash(python *)` and the like from matching, so it is not used.
- The PowerShell tool refuses `$env:NAME = ...` (also `Set-Item Env:` and `[Environment]::SetEnvironmentVariable`) in
  every permission mode except `bypassPermissions`. So a PowerShell command gets a token only in the modes listed in
  `[identity] powershell_modes` (`bypassPermissions`); in the others a tool sees the identity as unverified. Use Bash
  for `coney-tools` where the identity matters.

## The session launcher {#session-launcher}

`coney-agent` starts Claude Code as a role, from the Coney checkout that holds the current folder: `claude --agent
coordinator`, plus `--plugin-dir` for the Coney mod when the checkout has it (the mod loads in every mode, trusted
folder or not). Run it as `uv run --project python coney-agent`, or put `tools/` on `PATH` for `coney-agent` (the
`.ps1` wrapper in PowerShell, the `.cmd` one for `cmd.exe`, an `sh` one elsewhere). It is not the player-facing
launcher of the game. Every parameter, and what Windows PowerShell 5.1 needs first:
[`coney-agent`](coney-tools-setup.md#coney-agent).

## Commit trailers {#trailer-guard}

An agent's commits end with `Co-Authored-By: <the model it runs on> <noreply@anthropic.com>`
([AGENTS.md](repo:AGENTS.md)). The coordinator's session gets a system reminder naming the coordinator's own model for
that line, and subagents see it too and copy it: on 2026-10-13 a Sonnet agent twice credited Opus, the pull request's
Model line followed the wrong trailer, and `coney-tools pr check-body` passed because body and trailers agreed with
each other. **Two checks stand against it.** A git `commit-msg` hook, installed by `uvx pre-commit install` (the
`commit-message` hook of `.pre-commit-config.yaml`, which runs [`coney-tools pr
check-commit`](coney-tools-pr.md#pr-check-commit)), refuses the message before the commit exists, for everyone, with or
without Claude Code: only `Co-Authored-By` trailers, no other e-mail address, and, when the commit runs in a Claude
Code session (its [identity](#identity) file names the model), a trailer naming that model. The hook below stays
beside it until the git hook has run for real: it reads the model from the transcript, so it still catches the
commits the identity does not reach (a PowerShell command outside the modes that get a token, and a commit made after
the identity file's ten minutes). The git hook also checks a commit that a script makes by running `git commit`, but
without the model check (no token). A Claude Code hook (`.claude/hooks/trailer_guard.py`, run by the
[dispatcher](#hooks) after a shell command that runs `git commit`) checks every commit right after it is made. Claude
Code gives a hook no model, but it does give the main session's `transcript_path` and, inside a subagent, an `agent_id`
and an `agent_type` (its role, such as `engineer`; the hook does not use it, because the role does not name the model).
The agent's own transcript (`<session>/subagents/agent-<id>.jsonl`, [Checking a run](#check)) names the model on every
assistant entry (`claude-sonnet-5-5`), and the hook derives the trailer's name from it (`Claude Sonnet 5.5`) instead of
keeping a table. A trailer with the vendor's address that names a different model exits 2, and the agent sees the reason
and amends the commit with the one trailer it should have. A commit that printed nothing (`git commit -q`) is judged
from the HEAD of the working directory and of the directories the command names, if it was made in the last two minutes.
A commit made by a program the command only starts (a script) prints nothing the hook can read, so only the git hook
checks it. The hook fails open when it cannot tell (no transcript, an id that is not a Claude model, no trailer, another
vendor, a commit it cannot find), and a person committing outside Claude Code never runs it.

## Running without losing the cache {#cache}

- **Never sit inside a long command.** Builds, test sweeps, emulator runs and CI waits that take more than a few
  minutes run in the background with their output going to a file; the agent checks the file every few minutes
  (each check keeps the cache warm) or once when the run should be done. Even an hour-long cache does not survive a
  run that takes longer. A build or test run is `coney-tools build ...` started with `run_in_background`
  ([Builds and tests](#builds)); it prints its log path first, and gives up waiting for a slot after 40 minutes,
  or sooner when the wait plus the build would pass its 45-minute budget; a longer wait runs `--detach`
  ([Build waits](#build-waits)). A full suite waits for a **check slot** and logs each stage, so a quiet log means a
  hung run ([Check slots](coney-tools-build.md#check-slots)).
- **Every wait has a bound** ([Bounded waits](#bounded-waits)): never `until ...; do sleep ...; done` or `while true`
  in a shell, which a hook refuses.
- **Keep tool output small.** Print counts, the failing lines and the end of a log, never a whole log: every line an
  agent reads stays in its context for the rest of the task.
- **Do not message a finished agent.** Start a new one from its report and the task's [STATUS.md](how-we-work.md#status).
- **No agent waits on CI.** An engineer commits locally, runs Check, and is done when its task passes Review; it
  never pushes. The coordinator opens the pull request with `pr open` ([Opening the pull
  request](how-we-work.md#release-queue)), watches CI and sends a failure to the fix track
  ([When a pull request fails checks](#ci-failures)). An agent that sat idle past its cache lifetime would re-write
  its whole context, often hundreds of thousands of tokens, when it resumed.
- **Keep the coordinator small.** The coordinator re-reads its whole conversation on every turn and is the context a
  fork would inherit. It saves the state, clears and continues from it at a natural boundary, and never forks once
  its context is large.

## Builds and tests {#builds}

Six agents compiling at once leave every one of them waiting, so builds share the machine through claims, the way
PCSX2 copies do ([`build`](coney-tools-build.md#build)). An agent never runs `cmake --build`, `cmake --preset`,
`ctest` or `ninja` itself (a hook refuses them); it runs, from its worktree:

```sh
uv run --project python coney-tools build --agent <id> --preset dev --test -R <regex> [-L <label>]
```

- **Run it in the background** (`run_in_background`, or output to a file) and read the log path it prints first, then
  the short summary when it ends. Do not sit in it: a wait for a slot can take many minutes.
- **A small job is quick.** An incremental rebuild of a few steps (the tool counts what is out of date by itself, so
  one edited source is one step however much sits behind it), or a test run filtered with `-R` or `-L` to a few
  tests, takes the fast lane and does not queue behind full builds. Rerun a failed test with `-R <its name>`, not the
  whole suite: the full run (no `-R` or `-L`) is `task check`'s, and an agent's is refused without `--full`
  ([the rule](coney-tools-build.md#build)). `-L '^disc$'` runs only the tests that need the game's disc. `-R` is a CMake
  regular expression: escape `.` `+` `(` `)` with a backslash and quote it with single quotes ([Choosing tests](coney-tools-build.md#build)).
- **A play build is `--target coney`.** It builds the executable and what it links, not the tests; it cannot be
  combined with `--test`. Every build goes through the shared [compiler cache](building.md#compiler-cache), so the
  first build of a fresh worktree takes most of its objects from builds other worktrees already ran, and its summary
  prints the hits.
- **One run per build folder.** A second `build` in the same checkout and preset waits for the first (it prints who
  holds the folder), so start a build and its tests as one `--test` run rather than two commands, and never run two at
  once in one worktree. Each run writes a log of its own, named for the preset, the agent and the time; read the path
  it prints first.
- **A second configuration** (a Clang build beside the default compiler's) is `--build-dir build/clang --env
  CC=clang-19 --env CXX=clang++-19` with `--configure-only`; never a raw `cmake`.
- **Exit code 75 means "still queued".** The command left the queue: its wait reached `--max-wait`, or the wait the
  queue still expects plus the step would pass the time budget (`--budget`), in which case it leaves at its first
  look. It said its position and the numbers. Do other work and run the same command again later, or detach it and
  stop ([Build waits](#build-waits)); do not loop on it in one call. Run again within the hour, it keeps the place it
  had rather than joining at the back ([The queue](coney-tools-build.md#build)).
- **A test run holds no build slot.** Once the compile ends its slot goes back to the queue; a test run too big for the
  fast lane (the whole suite, or many or long tests) waits for a check slot instead ([Check
  slots](coney-tools-build.md#check-slots)).
- **`--priority`** goes ahead of the queue. The coordinator uses it for a build somebody is waiting on; an agent does
  not set it on its own.
- **`build status`** shows who holds the slots and who waits. A crashed run is taken over by the next claimer, which
  also stops the `ctest` or `cmake` it left running; `build unclaim --agent <id>` cleans up by hand.
- **A build never outlives its agent.** Its children end with the command, and the command stops when the shell that
  started it is gone (a restart of the agent tool, a killed background shell), so a run never needs killing by hand
  ([Process lifetimes](coney-tools-build.md#build)). Never stop `ctest` or a test executable yourself: stop the
  `coney-tools build` command, and everything under it goes with it.
- The coordinator sets the number of slots for the machine with `build_slots` in `coney.local.toml`
  ([Local workspace](workspace.md#local-config)) and does not stagger builds by hand any more.
- **A new build folder may be refused** when too many worktrees already hold one ([The cap on build
  folders](#build-cap)).

### The cap on build folders {#build-cap}

On 2026-10-21 the maintainer capped the agents that build the engine at about 7 at a time (research, tools and docs
agents, which build nothing, stay unlimited). The reason is the drive, not the cores (the slots above already share
those): a `build/` folder is about 7 GB, the drive must keep 30 GB free, and every worktree that has built once keeps
its folder until somebody removes it. A rule written in a brief is not enough, so the tool enforces it:

- `coney-tools build` refuses to configure a **new** build folder while the worktrees that already hold a `build/`
  folder (any preset) are at `build_worktree_cap` (default 7; `0` turns it off; `coney.local.toml`, [Local
  workspace](workspace.md#local-config)). It exits `2` at once and lists those worktrees: size, hours idle, and
  whether the branch's pull request merged or the worktree is busy. Builds that already have a folder keep building,
  the main checkout's `build/release` (the coordinator's play build) does not count, and a second folder in a
  worktree that already holds one (a second configuration) adds none.
- **On a refusal**, run `uv run --project python coney-tools workspace clean` (a dry run: it lists what it would do),
  then with `--apply`. It removes worktrees whose pull request merged (when nothing in them is uncommitted) and
  deletes the `build/` folders idle over a day, and never touches a worktree that holds or waits for a build slot,
  or one with a `.coney-busy` marker ([Cleanup](workspace.md#cleanup)). Then run the build again.
- If it frees nothing, every holder is busy or recent: do not delete another agent's build by hand. Delete your own
  `build/` when you hand over a unit (the [brief](#briefs) says so), do other work, and ask the coordinator (a
  question that cannot wait for your report goes in `QUESTION.md` in your scratch folder, which
  [Conclave](conclave.md#questions) tells the coordinator about).
- **The busy marker** is how a worktree says "leave me alone". Write your agent id into it
  (`echo <id> > .coney-busy`): a marker with something in it always protects, while an empty one older than a day
  is taken for a dead agent's and ignored, so a crashed agent no longer protects its worktree for ever.

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
  ([`build result`](coney-tools-build.md#build)).
- `coney-tools ci run --detach` runs the CI-style checks of the changed files in the background; whoever
  continues runs `coney-tools ci result` in the worktree ([`ci result`](coney-tools-ci.md#ci-run)).

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
  It does not check on a later call that the claim is still live (after a `pcsx2 unclaim` or a takeover the old id still
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
shared server: `coney-tools pcsx2 ...`, `trace original`, `clips record` and, from a script, `DebugClient.claimed("<your
id>")` (`coney_tools.debug_server`), which connects to the debug server of the claim you hold and refuses any other.
`coney-tools pcsx2 check-claim --agent <id> --debug-port N --pine-port M` makes the claim check on a pair of ports.

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

A failing check on a pull request is a small, well-defined task, so it becomes a **fix task** in the fix track
([Tracks](how-we-work.md#slots), [Engineer tracks](#slots)), not a hand fix by the coordinator. The coordinator watches
CI; the engineer that wrote the pull request has moved on. The one exception is a failure that is only the pull
request's title or description (a long title, a missing `Docs: none`), a few tool calls the coordinator makes itself:
nothing in the branch changes, so it takes no place.

1. The coordinator reads only the failure: `gh pr checks <n>` for which checks failed, then the end of the failing
   job's log (`gh run view <id> --log-failed`, last lines). It does not read the whole log.
2. It makes the fix task with `coney-tools task create --fix-of <issue>`, which reuses the claim, and writes the
   failing check and the log lines that matter into the task file ([Task files](issues.md#task-files)).
3. If the engineer that wrote the task is still running, the fix goes to it, with its context warm. Otherwise a fresh
   engineer takes the fix track, runs `task start` on the fix (it opens the parent's unit:
   [Starting a task](issues.md#starting)) and works on the pull request's branch. Fixes wait their turn, oldest
   failure first; an engineer that finished one may take the next while its context is warm. Messaging a finished
   agent instead re-writes its whole context (rule 2 in [What a run costs](#cost)), often hundreds of thousands of
   tokens, for a fix of a few lines. Several failures with the same cause go to one engineer.
4. The fix stays inside the failing check and redoes only the stamps it made stale: the pairs if an `@orig` function
   changed, Check always, Review only if reviewed code changed. Then the coordinator runs `pr open <issue>-fix`, which
   updates the same pull request ([pr open](pr.md#open)).
5. The model follows [Which model](#models): Sonnet for a lint, format, title or docs failure with a clear message;
   Opus when the cause is unknown or the failure is a test or build in code that needs judgement. If the same check
   fails a second time, escalate a model instead of repeating the same fix.

## CI capacity {#ci-capacity}

GitHub Actions on this repository runs about 20 jobs at once (5 of them on macOS), shared by every pull request, and a
full code pull request queues 4 builds plus sanitizers (about 20 minutes). CI never holds an agent back:

1. **Agents work local-first.** The engineer's stages end at Review, on this machine, and the full suite with the disc
   runs in `task check`, which CI cannot run. CI finds only what the local platform cannot see.
2. **Only the coordinator pushes**, through `pr open`, as soon as Check and Review hold; it does not wait for room in
   the queue.
3. **Each push starts a run** and cancels the pull request's previous one, so a branch is pushed once it is ready,
   not after every commit. A merge to `main` likewise cancels `main`'s older run, so a merge round costs one build
   ([What CI runs](building.md#what-ci-runs)).
4. **A pull request runs only the jobs its change can break** ([What CI runs](building.md#what-ci-runs)), so
   documentation-only pull requests are cheap.

## Checking a run {#check}

Claude Code writes each agent's transcript to
`~/.claude/projects/<project>/<session>/subagents/agent-<id>.jsonl`. Every assistant line has a `usage` object:
`cache_read_input_tokens`, `cache_creation_input_tokens`, and under `cache_creation` the split between
`ephemeral_5m_input_tokens` and `ephemeral_1h_input_tokens`. A large `cache_creation_input_tokens` repeating after
long gaps is a cache being lost and re-written; writes in the five-minute bucket mean the hour-long setting is not
in effect.
