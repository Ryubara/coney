# Enforcement map

Coney runs on many agents and few people, and a rule that lives only in a guide is a rule someone will miss. This page
lists every rule a contributor or an agent must follow, where it is written, and what stops a break today: a hook, a
check, a CI job, a tool that refuses, or only a reviewer. It is the one place that answers "is this rule enforced, and
where?" When you write a rule into a guide, add its row here in the same commit; when you add a check, change the row.

## How to read it {#levels}

**Status.**

- **enforced**: a program refuses the break before it lands, at the point named.
- **partly**: a program catches some forms of the break, or catches it only after the fact (an audit finding, a
  hygiene comment, a CI failure after the push).
- **written**: only the guide and a reviewer stand in the way.
- **judgement**: no program can decide it; a reviewer checklist owns it.

**Where a check belongs.** The earliest point that is cheap and sees the break, in this order:

| Point | Sees | Runs | Examples |
| --- | --- | --- | --- |
| Claude Code hook (`.claude/hooks/`, one dispatcher) | A command or tool call before it runs | Every agent's call, in milliseconds; spelling only, so never a security boundary ([The hooks](#hooks)) | `build_guard.py`, `issue_guard.py`, `roles.py` |
| pre-commit (`.pre-commit-config.yaml`) | The files of a commit, and (at the `commit-msg` stage) its message | On commit when installed, in `check changed` and in CI's `format` job (all files; not the message check) | clang-format, the 512 KB guard, `no-real-clock`, `commit-message` |
| `coney-tools check changed` | The files a branch changed | Before a unit is handed over ([The local gate](testing.md#local-gate)) | `repo audit --files`, mkdocs, pytest |
| Release gate (`repo check-review`) | The description file, its pictures and the branch tip | Before the coordinator pushes ([The description review](how-we-work.md#pr-review)) | Model line against the trailers |
| CI required check | The pushed branch and the pull request's text | After the push; a failure costs a CI round | `title`, `body`, `docs`, `build-ok` |
| Audit and hygiene (`repo audit`, `hygiene.yml`, Tether) | GitHub and the workspace as they are | After the fact, as findings | `issue.labels`, `ws.scratch` |
| Review (`reviewer`, `research-reviewer`, `orig-reviewer`, `pr-reviewer`) | Meaning: whether a test proves the fix, a claim is backed | Per change, by an agent | [Reviewing](review/index.md) |

A check that refuses at an earlier point is worth more than the same check later: a hook costs nothing, a CI failure
costs a CI round and a fresh agent ([When a pull request fails checks](agents.md#ci-failures)).

## The Claude Code hooks {#hooks}

Every Claude Code hook runs through one dispatcher, `.claude/hooks/dispatch.py` (one Python start per call; what it
runs: [The hooks](agents.md#hooks)). Its settings are `.claude/hooks/hooks.toml`:

| Setting | Committed value | What it does | To change it |
| --- | --- | --- | --- |
| `require_role` | `false` | A session with no role (not started through [`coney-agent`](agents.md#session-launcher)) may only read | One line to `true`, in its own pull request made by a tooling agent, once the maintainer's sessions start through `coney-agent`. On at merge, it would lock out the coordinator session that exists today |
| `[pr_guard] refuse_pr_create` | `false` | Refuse every `gh pr create` and `gh api` call that opens a pull request | One line to `true` when `coney-tools pr open` lands |
| `[identity] enabled`, `ttl_seconds` | `true`, `600` | Write the caller's identity file (named by a hash of the token) and put its token in front of `coney-tools` and `git commit` commands; how long the file lives | |
| `[identity] powershell_modes` | `["bypassPermissions"]` | The permission modes in which a PowerShell command gets a token too | The tool refuses the assignment in the others |
| (built into `protect.py`) | not a setting | What no session may touch: `~/.coney`, private keys, stamp logs, signing, other processes' command lines. A settings file that cannot be read does not take it away | Edit `protect.py` |
| `[roles.*]` | see the file | What each role may not do ([Which role to start](agents.md#roles)) | Edit the table; `test_agent_definitions.py` holds it to the files in `.claude/agents/` |

**The permission rule for the identity token.** The token goes in front of the command as `export
CONEY_AGENT_TOKEN=<token>; <the command>` ([Who runs a command](agents.md#identity)), and `.claude/settings.json` allows
`Bash(export CONEY_AGENT_TOKEN=*)` for it. That rule does not widen what a command may do, verified on 2026-10-22 with
`claude -p --permission-mode default --allowedTools "Bash(export CONEY_AGENT_*)"` (the rule before it was narrowed to the
token) on Claude Code 2.1.309: `export CONEY_AGENT_ID=x; python ...`, the same with `&&` and with a newline each still
needed approval for the part after the export, and `export CONEY_AGENT_ID=$(...)` was refused as runtime-determined.
Claude Code checks each part of a compound command against the rules on its own. Re-run this check when Claude Code is
upgraded.

**What stops a forged identity.** Not a refusal by spelling: shell can spell a variable assignment around any pattern
(`export "CONEY_AGENT_TYPE=x"`, `export $name=x`, `source file.sh`). The tools take who ran them from the file a
random, hook-written token names, so a typed variable is worth nothing and a typed token finds no file; the folder is
one the hooks forbid every session to name (all of `~/.coney`, after folding `./`, `..`, case, slashes and
`~`/`$HOME`), and a file is named by a hash of its token, so a listing gives no token away. What stays exposed is the
token in the command line of a running shell, in a process listing. Refusing a command that sets `CONEY_AGENT_*` stays
as a courtesy.

## No game data {#legal}

| Rule | Written in | Enforced by | Status |
| --- | --- | --- | --- |
| No executable, BIOS, asset, extracted file, dump or save in the repository | [LEGAL.md](repo:LEGAL.md#no-game-data), [AGENTS.md](repo:AGENTS.md) | pre-commit `no-game-files` by file type (disc images, executables, console data, PCSX2 states and recordings, memory cards, traces, dumps, Ghidra projects) and `check-added-large-files` at 512 KB, both over every file in CI's `format`; `.gitignore`; `wad`, `xbox` and `extract` refuse an output folder inside the checkout | enforced (a game file of an unlisted type under 512 KB still passes) |
| No decompiled game script in the repository: research describes a script, never pastes it | [Research workflow](research-workflow.md#scripts), [LEGAL.md](repo:LEGAL.md#no-game-data) | `.claude/hooks/lua_guard.py`: pre-commit `no-decompiled-lua` on every text file, and a Claude Code hook before every Write, Edit, MultiEdit and NotebookEdit into a checkout, refuse the marker line every `lua decompile` output carries and three consecutive lines fingerprinted by `lua decompile --all`; `lua decompile` refuses an output folder inside the checkout | partly: CI has no fingerprints (only the marker), a shell command's write is seen only at the commit, and a paraphrase passes |
| Never exclude a file from the 512 KB check | [AGENTS.md](repo:AGENTS.md) | `repo audit` `files.large-file-limit` (the limit, `--enforce-all`, no `exclude` or `files`) and `files.game-files` (no `exclude`, `types` or `stages` on either hook, no wider top-level `exclude`), in `check changed` and CI | enforced |
| Local records (`HANDOFF.md`, `STATUS.md`, `PR.md`, `REVIEW.*`, `coney.local.toml`, `.coney-busy`) never committed | [How we work](how-we-work.md#shared-machine) | pre-commit `no-game-files` by file name, at any depth; `.gitignore` | enforced |
| Tests use synthetic fixtures; disc tests print aggregates only | [Testing](testing.md#no-game-data) | Review | judgement ([Code review](review/code.md)) |
| Disc tests carry `[disc]` and skip without a disc | [Testing](testing.md#kinds-of-tests) | `python/tests/test_disc_labels.py` | enforced (CI `python`) |
| Research pages hold short snippets, never a whole function listing | [LEGAL.md](repo:LEGAL.md#research-limits), [Research workflow](research-workflow.md#writing-up-a-finding) | Review | judgement ([Research review](review/research.md)) |
| Reference lists hold names, ids, values and our own words only; files under 512 KB | [Research workflow](research-workflow.md#reference-lists) | `refs render --check`; the 512 KB guard | partly |
| Issue attachments: gameplay pictures and short videos only; logs without user folders or email addresses | [Issues](issues.md#media) | Bug form's required checkbox; `hygiene.yml` scans attached logs (`issue.attachment-data`, `issue.attachment-personal`) | partly (after the fact) |
| Video clips at most 10 s, only through `clips publish`, never from a screen recorder | [Testing](testing.md#clips) | `clips` code refuses capture devices, long clips and paths in a checkout; `.claude/settings.json` refuses `gh` naming a video | enforced |

## Agents and the shared machine {#agents}

| Rule | Written in | Enforced by | Status |
| --- | --- | --- | --- |
| Build and test only through `coney-tools build` | [AGENTS.md](repo:AGENTS.md), [Running agents](agents.md#builds) | `.claude/hooks/build_guard.py` | enforced |
| Every wait has a bound | [Running agents](agents.md#bounded-waits) | `.claude/hooks/bounded_waits.py` | enforced |
| No agent pushes, opens, edits, comments on or merges a pull request; no agent writes to GitHub except `coney-tools issue` when briefed | [AGENTS.md](repo:AGENTS.md), [How we work](how-we-work.md#release-queue), [Issues](issues.md#agents) | `.claude/hooks/push_guard.py` refuses `git push` and every writing `gh` command inside a subagent; `issue_guard.py` refuses raw issue writes for everyone | partly: not a separate `claude -p` session or a command run from a script file |
| An agent works in `worktrees/coney/<branch>/`, never with `isolation: "worktree"` | [Running agents](agents.md#worktrees) | `.claude/hooks/agent_guard.py` | enforced |
| Every agent starts as a Coney role (`engineer`, `tooling`, a reviewer), never general-purpose or without a type | [Running agents](agents.md#roles) | `.claude/hooks/agent_guard.py` reads the roles from `.claude/agents/` | enforced |
| Each role does only its work: the coordinator writes no engine code, tests, Python or research, commits in no unit worktree and uses no study tool; an engineer opens no pull request; a reviewer writes only its verdict | [Running agents](agents.md#roles), [the hooks](#hooks) | `.claude/hooks/roles.py` from `agent_type`, rules in `hooks.toml` | enforced for a session started with a role; commands of later phases (`pr open`, `task check`) are listed ahead |
| No session names `~/.coney` or a private key, signs with `ssh-keygen -Y sign`, reads another process's command line, or writes a stamp log by hand | [Running agents](agents.md#roles), [Stamps](stamps.md) | `.claude/hooks/protect.py` (built in), for every session and role | enforced (spelling: a deliberately built path gets through) |
| A session has a role: started through `coney-agent` | [Running agents](agents.md#session-launcher) | `require_role` in `hooks.toml`: with no role a session may only read; a SessionStart notice either way | **not yet**: committed off until the maintainer's sessions start with a role |
| `coney-tools` and `git commit` know who ran them, and a command cannot say it was someone else | [Running agents](agents.md#identity) | The dispatcher writes the identity to a file named by a random token and puts only the token in front of the command; the tools read the file, and a typed or changed token is "unverified" | enforced in Bash (and PowerShell under `bypassPermissions`); elsewhere a tool sees the identity as unverified |
| Never edit the main checkout | [How we work](how-we-work.md#shared-machine) | None | written; best point a hook on Edit and Write |
| Never `git stash`; never stop a program by name | [How we work](how-we-work.md#shared-machine) | `.claude/settings.json` deny rules | partly: they match spelling (`Get-Process x \| Stop-Process` passes) |
| The commit trailer names the model the agent runs on; only `Co-Authored-By` trailers; no other e-mail address | [Running agents](agents.md#trailer-guard), [AGENTS.md](repo:AGENTS.md) (`commits.trailers`) | The git `commit-msg` hook (pre-commit, `pr check-commit`) before the commit, for everyone; `.claude/hooks/trailer_guard.py` after it inside Claude Code until the git hook is proven; `repo check-review` holds the Model line to the trailers | enforced for the trailers; the model check needs the hook's identity file, so it applies only inside Claude Code and fails open once the file is gone (it lives 10 minutes, [Who runs a command](agents.md#identity)): `trailer_guard.py` covers that |
| Name the model on every Agent call; never Fable for agent work | [Running agents](agents.md#models) | None | written; best point `agent_guard.py` |
| Never message a finished agent; act before a big agent's cache expires | [Running agents](agents.md#cost) | Tether's cold guard and agent guard | enforced (Tether) |
| Create `.coney-busy` on start, delete it and your `build/` when done | [How we work](how-we-work.md#shared-machine) | `workspace clean` honours the marker and deletes idle `build/` folders | partly |
| Keep at least 30 GB free | [How we work](how-we-work.md#shared-machine) | `workspace disk`, `repo audit --local` (`ws.disk`) | partly (on request) |
| Use PCSX2 only inside a claim, started by `pcsx2 launch`, connected to the claim's ports | [Research workflow](research-workflow.md#driving-pcsx2), [Running agents](agents.md#setup) | `pcsx2 claim` and `launch` refuse a busy copy or port | partly: a hand start or a `pcsx2_connect` to another copy's ports passes; best point a hook |
| Nothing takes the keyboard focus | [How we work](how-we-work.md#shared-machine) | Coney's test mode never activates its window; `pcsx2` tools drive by handle | partly |
| Never restart or stop the shared Ghidra server; never undo another engineer's names | [Ghidra](ghidra.md) | None | judgement |
| Keep Python virtual environments out of worktrees | [How we work](how-we-work.md#shared-machine) | None | written; best point `repo audit --local` |
| Repository rules override a brief; read the guides first | [AGENTS.md](repo:AGENTS.md) | `.claude/agents/*.md` say so | judgement |

## Commits and pull requests {#pull-requests}

| Rule | Written in | Enforced by | Status |
| --- | --- | --- | --- |
| Title `area: Verb the rest`, within the length limit | [AGENTS.md](repo:AGENTS.md) (`commits.title`) | `repo check-title`; CI `title` (required); `repo check-review` | enforced (branch commit titles are not checked: squash merge keeps only the pull request's) |
| The description has the template's sections, filled; Checklist all ticked; Screenshots or a reason; AI use; no local paths | [Testing](testing.md#pr-evidence) (`pull_requests`) | `repo check-body`: `pr_guard.py` before `gh pr create`, CI `body` (required), `repo check-review` | enforced |
| Gameplay, graphics and audio titles carry a `Runtime:` line with numbers, and it is true | [Testing](testing.md#runtime-line) | `repo check-body --title` (shape); `pr-reviewer` (truth) | enforced (shape), judgement (truth) |
| A change to `src/` or `python/src/` changes `docs/` or `research/`, or says `Docs: none` | [AGENTS.md](repo:AGENTS.md) | CI `docs` (required, `repo check-docs`) | enforced at CI |
| A pull request opens only through `coney-tools pr open` | [How we work](how-we-work.md#release-queue) | `.claude/hooks/pr_guard.py` refuses `gh pr create` and a pull request opened through `gh api` behind `[pr_guard] refuse_pr_create` in `hooks.toml` | **not yet**: `pr open` does not exist, the switch is off |
| Every description is reviewed by `pr-reviewer` and passes `repo check-review` before it is pushed | [How we work](how-we-work.md#release-queue) | `.claude/hooks/pr_guard.py` runs CI's title and body checks before `gh pr create` and `gh pr edit`; the verdict itself is `repo check-review`, run by hand | partly: the hook does not require a fresh verdict |
| Reimplemented code is ported, not transcribed: no decompiler names, `undefined` types, raw offset reads or `goto` | [Conventions](conventions.md#porting) | `orig copy-check` in `check changed` and CI's `python` job | enforced (the mechanical signs) |
| Every `@orig` function a branch adds or changes matches the original and is our own expression | [Orig review](review/orig.md) | `orig-reviewer` (judgement); `orig check-review` before release (a passing verdict covering each changed function's body) | judgement, gated at release |
| No "Generated with" line in a pull request or issue | [AGENTS.md](repo:AGENTS.md) | None in the repository | written; best point `repo check-body` and `issue check` |
| One feature per pull request; a diff holds only that change | [How we work](how-we-work.md#working-in-parallel) | `pr-reviewer`, the coordinator | judgement |
| Run the local gate before handing a unit over | [Testing](testing.md#local-gate) | CI finds what it missed, a CI round later | written; best point a stamp `check changed` writes, which the release gate reads |
| A stacked pull request says `Depends on #N`, has no auto-merge and is never merged into its base | [How we work](how-we-work.md#release-queue) | `repo audit` `pr.stacked-*`, `pr.merged-into-base` | partly (after the fact) |
| No pull request sits conflicting or failing | [How we work](how-we-work.md#merging) | `hygiene.yml` labels; Tether's `prs` poller blocks the coordinator's turn | enforced (Tether) |
| Only a maintainer or the coordinator merges, by squash, once required checks pass | [AGENTS.md](repo:AGENTS.md) (`merge`) | GitHub ruleset on `main`; `repo audit` `gh.ruleset` | enforced (GitHub) |
| The repository's address appears only in the README | [AGENTS.md](repo:AGENTS.md) (`paths.address_allowed_in`) | `repo audit` `files.address` (`check changed`, CI `python`) | enforced |
| Say "user" and "maintainer", never "owner" | [Writing these docs](writing-docs.md#style) (`repo.wording`) | `repo audit` `issue.person-word`, `pr.person-word` | partly (after the fact; docs pages unchecked) |

## Issues {#issues}

| Rule | Written in | Enforced by | Status |
| --- | --- | --- | --- |
| Issues are drafted, checked and filed only with `coney-tools issue` | [Issues](issues.md#filing) | `.claude/hooks/issue_guard.py`; `issue-check.yml` on every issue event | enforced |
| Form headings, required sections, Screenshots, `From:` line, links that exist on `main`, truthful status | [Issues](issues.md#writing) (`issues`) | `issue check`; `repo audit` `issue.*` | enforced (filing), partly (edits on the website) |
| Labels: one type, an area, a priority after triage, one status; a closed issue keeps no status | [Issues](issues.md#labels) | `repo audit` `issue.labels`, `issue.closed-status`; `issue-status.yml` | partly (after the fact) |
| Labels are edited only in `.github/conventions.yml` | [Issues](issues.md#labels) | `repo audit` `gh.labels`; `repo labels --apply` | enforced (audit) |

## Repository conventions and generated files {#generated}

| Rule | Written in | Enforced by | Status |
| --- | --- | --- | --- |
| A rule a program checks lives in `.github/conventions.yml`; the forms, template and pre-commit agree with it | [Repository conventions](repo-conventions.md) | `repo check`; `repo audit --files` (`files.*`) in `check changed` and CI | enforced |
| Generated blocks, binding pages, reference lists and mission pages are never edited by hand | [Writing these docs](writing-docs.md#style) | `repo render --check`, `natives render --check`, `refs render --check`, `missions render --check` (CI `python`) | enforced |
| Every `@orig` tag has its `[[function]]` in `functions.toml` | [Conventions](conventions.md#the-orig-tag) | `progress check` (CI `python`) | enforced |
| Mission work updates `research/missions.yaml` in the same commit | [AGENTS.md](repo:AGENTS.md) | `missions render --check` checks the file, not that it moved | judgement |
| CLAUDE.md and GEMINI.md hold only the import line | [AGENTS.md](repo:AGENTS.md) | `repo audit` `files.pointer` | enforced |
| Workflows declare the narrowest `permissions`; no title or body text expanded into a script | [Repository conventions](repo-conventions.md#workflows) | `actionlint` (untrusted expressions in `run:`) | partly; best point a `repo audit --files` check for `permissions:` |
| A workflow that writes to GitHub beyond comments needs a maintainer's approval | [Repository conventions](repo-conventions.md#workflows) | Review | judgement |

## Code {#code}

| Rule | Written in | Enforced by | Status |
| --- | --- | --- | --- |
| clang-format decides layout | [Conventions](conventions.md#formatting) | pre-commit; CI `format` (required) | enforced |
| Warning-free on MSVC, GCC, Clang and Apple Clang | [Conventions](conventions.md#standard-and-compilers) | `/WX` and `-Werror` in every CI build | enforced at CI |
| clang-tidy's checks (`bugprone-*`, `performance-*`, unchecked optional access) | [Conventions](conventions.md#portability-traps) | `check changed` and the nightly CI pass | partly (nightly, after the merge) |
| Only the frame pacer reads the real clock or sleeps | [Conventions](conventions.md#update-and-render) | pre-commit `no-real-clock` | enforced |
| Every code file starts with the SPDX line | [Conventions](conventions.md#licence-header), [LEGAL.md](repo:LEGAL.md#licences) | None | written: four files lack it; best point pre-commit |
| The engine throws no exceptions | [AGENTS.md](repo:AGENTS.md), [Conventions](conventions.md#errors) | None (no `throw`, `try` or `catch` in `src/` today) | written; best point the engine targets' compile flags |
| OS and SDL3 code only in `src/platform/` | [AGENTS.md](repo:AGENTS.md), [Conventions](conventions.md#platform-code) | Review | written (holds today); best point pre-commit by include |
| Naming: `snake_case` files, `PascalCase` types, `camelCase` functions, `m_` members, `k` constants | [Conventions](conventions.md#naming) | Review | written; best point clang-tidy `readability-identifier-naming` |
| Include order and include what you use | [Conventions](conventions.md#include-order) | Review | written; best point clang-format `IncludeBlocks` and `misc-include-cleaner` |
| A doc comment on every public type and function, a comment above every other function, no commented-out code | [Conventions](conventions.md#comments-and-orig) | Review | judgement |
| Reimplemented code carries `@orig` and the research link | [Conventions](conventions.md#the-orig-tag) | `progress check` (format of tags it finds) | partly (an untagged function passes) |
| A Coney-only capability is its own function and has a native-candidates row | [Conventions](conventions.md#coney-only) | Review | judgement |
| A source file past about 500 lines is split | [Conventions](conventions.md#file-size) | Review | judgement (best point a `check changed` warning) |
| Python: one `coney-tools` entry point, `pathlib`, values from the conventions file; ruff, mypy, pytest | [Conventions](conventions.md#python) | ruff and mypy (pre-commit, CI `python`) | partly (style is review) |

## Tests, research and docs {#work}

| Rule | Written in | Enforced by | Status |
| --- | --- | --- | --- |
| Tests first; a bug fix's test fails before the fix | [Testing](testing.md#done) | Review | judgement |
| Disc tests that play a level start through the player's path | [Testing](testing.md#test-through-the-players-path) | `disc_game_session_test.cpp` for the session itself | partly |
| No test opens a dialog | [Testing](testing.md#no-dialogs) | `coney.error_dialogs_off` test | enforced |
| Stand-ins are marked in code and listed on the research page | [Testing](testing.md#stand-ins) | Review | judgement |
| Every research claim has an address and an evidence level | [Research workflow](research-workflow.md#evidence-levels) | `docs check` fails an address with no level in its block, row or lead line, and a level in a refused form; `research-reviewer` reads the claims that cite no address and whether a lead line is true | partly |
| A research page has the front matter and the sections of its kind, one owner per address, addresses that resolve in the executable, no copied paragraph, no date outside In Coney, and is within its length limit; a page that cannot yet is listed in `research/legacy.toml`, which only shrinks | [Writing these docs](writing-docs.md#research-pages) | `docs check` on the changed pages (a legacy page that is touched is held to the mechanical rules page-wide), which also runs the whole-file check of `docs legacy --check` when the change touches a research page, research data, the conventions or the checks (pre-commit gate `check changed`; CI `mkdocs`, required) | enforced on touched pages |
| Docs build with `mkdocs build --strict`, lines within 120 columns | [Writing these docs](writing-docs.md#preview-and-build) | `check changed`; CI `mkdocs`, `markdownlint` (required); pre-commit markdownlint | enforced |
| One living page per subject; no machine paths; say what is true now | [Writing these docs](writing-docs.md#style) | Review | written |
| A function counts as understood with a Ghidra name, plate comment and a cited page row | [Research workflow](research-workflow.md#understood) | `progress check`, `progress ghidra --check` | enforced |

## Checks being added {#in-progress}

Tracks adding checks now; their rows change when they merge:

- **Docs standard** (`coney-tools docs check`): the research page skeleton, `Verified against`, an evidence level near
  each address, no machine paths or "not yet" wording, on the pages a change touches.
- **New untagged gameplay code** (`coney-tools orig check`): a new function in a gameplay folder carries `@orig` or a
  Coney-only marker.
- **clang-tidy scheduling**: when and where clang-tidy runs on a pull request.
- **Networking checklist**: review items for network code.

## What stays judgement {#judgement}

No program can tell whether a test proves the fix, a picture shows what its caption says, a claim's evidence level is
right or a pull request holds one feature. Those rules live in the reviewer checklists ([Reviewing](review/index.md))
and the `pr-reviewer` agent's instructions ([The description review](how-we-work.md#pr-review)); a program checks the
shape around them (a `Runtime:` line exists and has numbers; a verdict is fresh) so the reviewer's time goes to the
meaning.
