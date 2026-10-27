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
| Stage commands (`task check`, `task review`) and `pr open` | The committed change, its stamps, the description and its pictures | At Check, Review and before the push ([The pipeline](how-we-work.md#the-pipeline)) | the whole gate for the change's kind, the reviewer's verdict, the stamps `pr open` proves |
| CI required check | The pushed branch and the pull request's text | After the push; a failure costs a CI round | `title`, `body`, `docs`, `build-ok` |
| Audit and hygiene (`repo audit`, `hygiene.yml`, Tether) | GitHub and the workspace as they are | After the fact, as findings | `issue.labels`, `ws.scratch` |
| Review (the `reviewer`) | Meaning: whether a test proves the fix, a claim is backed | Per change, by an agent | [Reviewing](review/index.md) |

A check that refuses at an earlier point is worth more than the same check later: a hook costs nothing, a CI failure
costs a CI round and a fresh agent ([When a pull request fails checks](agents.md#ci-failures)).

## The Claude Code hooks {#hooks}

Every Claude Code hook runs through one dispatcher, `.claude/hooks/dispatch.py` (one Python start per call; what it
runs: [The hooks](agents.md#hooks)). Its settings are `.claude/hooks/hooks.toml`:

| Setting | Committed value | What it does | To change it |
| --- | --- | --- | --- |
| `require_role` | `true` | A session with no role (not started through [`coney-agent`](agents.md#session-launcher)) may only read | Leave it on: off, a session with no role gets no role rules |
| `pull_requests.open_only` (in `.github/conventions.yml`, not here) | `false` | Only `coney-tools pr open` opens a pull request ([The PR guard](#pr-guard) lists what is refused) | One line to `true`, merged by a maintainer, after one pull request went through `pr open` |
| `[identity] enabled`, `ttl_seconds` | `true`, `600` | Write the caller's identity file (named by a hash of the token) and put its token in front of `coney-tools` and `git commit` commands; how long the file lives | |
| `[identity] powershell_modes` | `["bypassPermissions"]` | The permission modes in which a PowerShell command gets a token too | The tool refuses the assignment in the others |
| (built into `protect.py`) | not a setting | What no session may touch: `~/.coney`, private keys, stamp logs and build records, other processes' command lines. A settings file that cannot be read does not take it away | Edit `protect.py` |
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

### The PR guard {#pr-guard}

`.claude/hooks/pr_guard.py` (with `pr_guard_shell.py`, which reads the command, `pr_guard_routes.py`, which reads its
`git push` and `gh api` calls, and `pr_guard_repo.py`, which reads the switch and tells repositories apart) holds every
pull request to the rules below. It is the one place that lists what is refused; the other guides link here.

**Switch off (today)**, the hook validates what a person or agent types:

- `gh pr create` (and its alias `gh pr new`) needs `--title` and `--body-file <file>`, never `--body`, `--fill`, `--web`
  or a body on stdin, and both must pass `repo check-title` and `repo check-body --title` (with `--local-images` when the
  command has `--attach`): the checks CI's required `title` and `body` jobs run.
- `gh pr edit` with `--title` or `--body-file` runs the same check on what it changes, and refuses `--body`.
- A command for another repository (`-R` or `--repo`, as `OWNER/REPO`, `HOST/OWNER/REPO` or a URL), `--dry-run` and
  `--help` pass. A relative `--body-file` is found from the folder a leading `cd` moved to. A title or file that a shell
  expansion fills in is not checked, and the hook lets a command through when it cannot run the checks or cannot
  split its arguments: in practice bash's `$'...'` quoting or a quote with no end. A quoted argument over several
  lines is read, so a multi-line `--body` or `--body "$(cat <<'EOF' ...)"` is refused like any `--body`.

**Switch on** (`pull_requests.open_only: true` in `.github/conventions.yml`; a maintainer's pull request turns it on after
one pull request went through `coney-tools pr open`), in every session, the coordinator's included:

- Refused: `gh pr create` and `gh pr new`; `gh pr edit` with `--title`, `--body`, `--body-file` or `--attach` (also as
  `-t`, `-b`, `-F`, attached values and `=` forms); `gh pr merge --auto` (also `--auto=true`); `git push`; and `gh api`
  calls that open a pull request (a POST to `repos/OWNER/REPO/pulls`), rewrite one (a PATCH of `pulls/N`), run a GraphQL
  mutation `createPullRequest`, `updatePullRequest` or `enablePullRequestAutoMerge`, or read a GraphQL query from a
  file, a variable or `--input`, which the hook cannot read.
- Allowed: reading (`gh pr view`, `list`, `checks`, `diff`), comments, labels and reviewers, `gh pr edit` of those,
  `gh pr merge` without `--auto`, `gh run rerun`, `coney-tools pr open` and `pr verify`, `--dry-run`, `-n` and `--help`.
  These are read by their value as gh and git read them: `--dry-run=false` and `--help=false` are no dry run or help,
  the last of `-n`, `--dry-run` and `--no-dry-run` wins, `-on` is the push option `n`, and a unique prefix (`--dry`)
  counts as the option.
- A command for another repository passes: `-R` or `--repo` that names it, a `gh pr` command run in a folder whose
  remote is another repository, a `git push` whose remote (a name, an address) is another one; a remote on the command
  line beats `--repo`, as in git. What the hook cannot show to be another repository counts as this one, so the switch
  holds, and what it cannot tell is refused: a folder it cannot follow (`cd "$DIR"`, `git -C "$DIR"`,
  `Set-Location $env:X`, `popd` past the first folder, `cd -`, a PowerShell `Set-Location` in parentheses), `--git-dir`,
  `GIT_DIR`, `--config-env`, `GIT_CONFIG_*` or `-c remote.*`, `url.*`, `branch.*`, `include.*` or `includeIf.*` on a
  push, a `git push` option it does not know, and arguments it cannot split (`$'...'` quoting). A `cd` inside a
  subshell (`( ... )`, `$( ... )`, `bash -c "..."`) moves only the commands inside it, and `popd` goes back.
- `pr open` itself runs `gh` and `git` as subprocesses, which are no tool call, so the hook never sees them.

The message names `coney-tools pr open <task>` and the [pr open section](pr.md#open) of the release guide. The
hook reads the switch itself from the conventions (the hooks run without packages), a file it cannot read leaves it off,
and `test_pr_guard.py` holds that reader to the conventions loader. A command counts only where a program starts, also
inside `bash -c "..."` and a command substitution in double quotes or backticks; text that merely mentions a command
passes.

**Not covered** (spelling checks, not a security boundary): a program named by a quoted word (`"git" push`,
`& 'gh' pr create`), by an alias or function, through `eval`, `iex`, `cmd /c`, `Start-Process`, `xargs` or a script
file; `env -u NAME git push` (the option's value is not taken); `git send-pack`; `GH_REPO` in the environment; a `gh`
alias of the user; `gh api` that PATCHes an issue number to rewrite a pull request's text, or builds its endpoint or
mutation from a variable or splits a mutation's name with quotes (`createPull''Request`); `gh pr create --dry-run`,
which the hook lets through although gh's help says it may still push git changes; a `git push` with no remote, which
the hook judges by `origin` although git pushes to `branch.<name>.pushRemote`, `remote.pushDefault` or the branch's
remote first; a `)` that is no shell syntax (in a comment, an escaped `\)`, a `case` pattern) between a `cd` and the
command, which the hook reads as the end of the `cd`'s subshell; a quoted argument made only of `;`, `&`, `|`, `(` or
`)` (`--body "("`), which the hook reads as shell punctuation; git configuration the command does not show (the
environment a shell profile sets, `include.path` in the checkout's own config); a git alias (`git -c alias.p=push p`);
a renamed owner or repository, which GitHub redirects but the hook calls another repository; a pull request opened or
edited on the GitHub website; and a person at their own terminal, which runs no hook.

## No game data {#legal}

| Rule | Written in | Enforced by | Status |
| --- | --- | --- | --- |
| No executable, BIOS, asset, extracted file, dump or save in the repository | [LEGAL.md](repo:LEGAL.md#no-game-data), [AGENTS.md](repo:AGENTS.md) | pre-commit `no-game-files` by file type (disc images, executables, console data, PCSX2 states and recordings, memory cards, traces, dumps, Ghidra projects) and `check-added-large-files` at 512 KB, both over every file in CI's `format`; `.gitignore`; `wad`, `xbox` and `extract` refuse an output folder inside the checkout | enforced (a game file of an unlisted type under 512 KB still passes) |
| No decompiled game script in the repository: research describes a script, never pastes it | [Research workflow](research-workflow.md#scripts), [LEGAL.md](repo:LEGAL.md#no-game-data) | `.claude/hooks/lua_guard.py`: pre-commit `no-decompiled-lua` on every text file, and a Claude Code hook before every Write, Edit, MultiEdit and NotebookEdit into a checkout, refuse the marker line every `lua decompile` output carries and three of any five lines fingerprinted by `lua decompile --all` (compared without quote, list, table and comment markup); `lua decompile` refuses an output folder inside the checkout | partly: CI has no fingerprints (only the marker), a shell command's write is seen only at the commit, an issue or pull request body is never checked, and a paraphrase passes, as does a block spread thinly among lines of our own (two or more of ours after every script line keep any window of five below three) |
| Never exclude a file from the 512 KB check | [AGENTS.md](repo:AGENTS.md) | `repo audit` `files.large-file-limit` (the limit, `--enforce-all`, no `exclude` or `files`) and `files.game-files` (no `exclude`, `types` or `stages` on either hook, no wider top-level `exclude`), in `check changed` and CI | enforced |
| Local records (`HANDOFF.md`, `STATUS.md`, `PR.md`, `REVIEW.*`, `coney.local.toml`, `.coney-busy`) never committed | [How we work](how-we-work.md#shared-machine) | pre-commit `no-game-files` by file name, at any depth; `.gitignore` | enforced |
| Tests use synthetic fixtures; disc tests print aggregates only | [Testing](testing.md#no-game-data) | Review | judgement ([Code review](review/code.md)) |
| Disc tests carry `[disc]` and skip without a disc | [Testing](testing.md#kinds-of-tests) | `python/tests/test_disc_labels.py` | enforced (CI `python`) |
| Research pages hold short snippets, never a whole function listing | [LEGAL.md](repo:LEGAL.md#research-limits), [Research workflow](research-workflow.md#writing-up-a-finding) | Review | judgement ([Research review](review/research.md)) |
| Reference lists hold names, ids, values and our own words only; files under 512 KB | [The research data](research-data.md#reference-lists) | `refs render --check`; the 512 KB guard | partly |
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
| At most 3 task units and 1 fix unit of engineer work at a time, and no engineer or tooling agent starts without a held claim: its brief carries `Task id: <id>`, its task file exists, its claim is held now and the slot of its kind is free; a unit frees its slot when it passes Review; units of `workflow` issues hold no slot until the cutover | [Engineer slots](agents.md#slots) | `.claude/hooks/slot_guard.py` refuses the coordinator's start of an `engineer` or `tooling` agent that fails any of the three (`coney-tools task slots --admit`, numbers in `tasks.start.slots`, claim cards and the `workflow` label read from GitHub); `task start` repeats the slot count and checks the claim | enforced for the coordinator's Agent calls; the holes: a tool that cannot judge (a crash, no `uv`, a slot lock it cannot have) passes the call and logs it; GitHub down skips only the claim check (the slot is still counted) and logs it; a `Task id:` of a live unit used for other work; work of the old workflow that has no unit record; a session a person starts; an agent of a type that needs no claim |
| Each role does only its work: the coordinator writes no engine code, tests, Python or research, commits in no unit worktree and uses no study tool; an engineer opens no pull request; a reviewer writes only its verdict | [Running agents](agents.md#roles), [the hooks](#hooks) | `.claude/hooks/roles.py` from `agent_type`, rules in `hooks.toml` | enforced for a session started with a role; commands of later phases (`pr open`, `task check`) are listed ahead |
| No session names `~/.coney` or a private key, deletes keys from the SSH agent, reads another process's command line, or writes a stamp log or build record by hand | [Running agents](agents.md#roles), [Stamps](stamps.md) | `.claude/hooks/protect.py` (built in), for every session and role | enforced (spelling: a deliberately built path gets through) |
| A session has a role: started through `coney-agent` | [Running agents](agents.md#session-launcher) | `require_role` in `hooks.toml`: with no role a session may only read; a SessionStart notice either way | enforced (on since the maintainer's sessions start through `coney-agent`) |
| `coney-tools` and `git commit` know who ran them, and a command cannot say it was someone else | [Running agents](agents.md#identity) | The dispatcher writes the identity to a file named by a random token and puts only the token in front of the command; the tools read the file, and a typed or changed token is "unverified" | enforced in Bash (and PowerShell under `bypassPermissions`); elsewhere a tool sees the identity as unverified |
| Never edit the main checkout | [How we work](how-we-work.md#shared-machine) | None | written; best point a hook on Edit and Write |
| Never `git stash`; never stop a program by name | [How we work](how-we-work.md#shared-machine) | `.claude/settings.json` deny rules | partly: they match spelling (`Get-Process x \| Stop-Process` passes) |
| The commit trailer names the model the agent runs on; only `Co-Authored-By` trailers; no other e-mail address | [Running agents](agents.md#trailer-guard), [AGENTS.md](repo:AGENTS.md) (`commits.trailers`) | The git `commit-msg` hook (pre-commit, `pr check-commit`) before the commit, for everyone; `.claude/hooks/trailer_guard.py` after it inside Claude Code until the git hook is proven; `repo check-review` holds the Model line to the trailers | enforced for the trailers; the model check needs the hook's identity file, so it applies only inside Claude Code and fails open once the file is gone (it lives 10 minutes, [Who runs a command](agents.md#identity)): `trailer_guard.py` covers that |
| Name the model on every Agent call; never Fable for agent work | [Running agents](agents.md#models) | None | written; best point `agent_guard.py` |
| Never message a finished agent; act before a big agent's cache expires | [Running agents](agents.md#cost) | Tether's cold guard and agent guard | enforced (Tether) |
| Only the coordinator holds a pull request's problems (`pr hold`); a held problem is not told or guarded for the hours of the hold, and a hold covers the head it was made on | [`pr hold`](pr.md#hold), [Conclave](conclave.md#holds) | `deny_coney_tools` in `.claude/hooks/hooks.toml` refuses `pr hold` to the engineer, tooling and reviewer roles | partly: the hold is a file in the scratch folder, so a role that writes it by hand is not stopped |
| Create `.coney-busy` on start, delete it and your `build/` when done | [How we work](how-we-work.md#shared-machine) | `workspace clean` honours the marker and deletes idle `build/` folders | partly |
| Keep at least 30 GB free | [How we work](how-we-work.md#shared-machine) | `workspace disk`, `repo audit --local` (`ws.disk`) | partly (on request) |
| Use PCSX2 only inside a claim, started by `pcsx2 launch`, connected to the claim's ports | [Driving PCSX2](research-tools.md#driving-pcsx2), [Running agents](agents.md#setup) | `pcsx2 claim` and `launch` refuse a busy copy or port | partly: a hand start or a `pcsx2_connect` to another copy's ports passes; best point a hook |
| Nothing takes the keyboard focus | [How we work](how-we-work.md#shared-machine) | Coney's test mode never activates its window; `pcsx2` tools drive by handle | partly |
| Never restart or stop the shared Ghidra server; never undo another engineer's names | [Ghidra](ghidra.md) | None | judgement |
| Keep Python virtual environments out of worktrees | [How we work](how-we-work.md#shared-machine) | None | written; best point `repo audit --local` |
| Repository rules override a brief; read the guides first | [AGENTS.md](repo:AGENTS.md) | `.claude/agents/*.md` say so | judgement |
| The Conclave mod leaves session health (context, prompt cache, `/clear`, HANDOFF, the idle wake) to Tether, and keeps to the watchers' few calls | [Conclave](conclave.md#boundary) | `python/tests/test_conclave_mod.py` runs the TypeScript compiler over the mod: the events it hooks, what the six hooks do (their filters, and that they pass the event on; the stop guard's one form), the fourteen `$.noun.method` calls it may make on any value of the engine's type (no `$.session.` or `$.model.`, no copy, spread, cast or `any`), the one command it runs with its folder and options, the names its sources may not use, each tested with a case that crosses the line | enforced in the local checks and in CI, which has no Claude Code types and reads the stub of them committed in `python/tests` ([Conclave](conclave.md#boundary)); where the check cannot run at all (no Node.js or TypeScript) its tests skip on a machine and fail under `CI` |

## Commits and pull requests {#pull-requests}

| Rule | Written in | Enforced by | Status |
| --- | --- | --- | --- |
| Title `area: Verb the rest`, within the length limit | [AGENTS.md](repo:AGENTS.md) (`commits.title`) | `repo check-title`; CI `title` (required); `repo check-review` | enforced (branch commit titles are not checked: squash merge keeps only the pull request's) |
| The description has the template's sections, filled; Issues affected lists each open issue the change affects or says `None (<reason>)`; Checklist all ticked; Screenshots or a reason; AI use; no local paths | [Testing](testing.md#pr-evidence) (`pull_requests`) | `repo check-body`: `pr_guard.py` before `gh pr create`, CI `body` (required), `repo check-review` | enforced |
| A merged pull request's listed Issues affected are edited after the merge | [Issues](issues.md#after-a-merge) | `repo audit` `pr.affected-unedited` (warn); `/repo-hygiene` fixes it; `reviewer` judges the list's completeness before the push | enforced (listed issues, after the merge), judgement (completeness) |
| Gameplay, graphics and audio titles carry a `Runtime:` line with numbers, and it is true | [Testing](testing.md#runtime-line) | `repo check-body --title` (shape); `reviewer` (truth) | enforced (shape), judgement (truth) |
| A change to `src/` or `python/src/` changes `docs/` or `research/`, or says `Docs: none` | [AGENTS.md](repo:AGENTS.md) | CI `docs` (required, `repo check-docs`) | enforced at CI |
| A pull request opens only through `coney-tools pr open` | [How we work](how-we-work.md#release-queue) | [The PR guard](#pr-guard) refuses the hand-typed routes behind `pull_requests.open_only` in `.github/conventions.yml` | partly: `pr open` exists and gates the release; the switch is off until one real pull request has gone through it, so a hand-typed `gh pr create` is only checked |
| A task's review is recorded only by a reviewer who did not work on it, with every checklist, `@orig` function, research page and picture answered, bound to the change | [Task review](review/task.md) | `task review --record` (`review_verdict.problems`; the identity rules of `tasks.review`) writes the `reviewed` stamp; nothing else does | enforced when the hooks vouch for the caller; `source: unverified` otherwise |
| Every description is reviewed by the `reviewer` before it is pushed | [Task review](review/task.md) | `task review` judges the description with the change; `pr open` requires the `reviewed` stamp over this text; a branch outside a task uses the [legacy release checks](how-we-work.md#pr-review) (`repo check-review`, run by hand) | enforced through `pr open`; partly for legacy branches |
| A unit is ready to become a pull request: its stamp log is intact, Check, the compared functions and Review hold for the change, and the generated title and description pass the checks CI runs | [Releasing a pull request](pr.md) | `pr verify` and `pr text`, run by hand; [`pr open`](pr.md#open) runs them before it pushes | enforced for a release that goes through `pr open` (see the switch above) |
| Reimplemented code is ported, not transcribed: no decompiler names, `undefined` types, raw offset reads or `goto` | [Conventions](conventions.md#porting) | `orig copy-check` in `check changed` and CI's `python` job | enforced (the mechanical signs) |
| Every `@orig` function a branch adds or changes matches the original and is our own expression | [Orig review](review/orig.md) | `reviewer` (judgement) in `task review`; `pr verify` requires a `compared` stamp and a `clean` or `diff-deliberate-ok` class for each changed function's body (`orig check-review` for a legacy branch) | judgement, gated at release |
| No "Generated with" line in a pull request or issue | [AGENTS.md](repo:AGENTS.md) | None in the repository | written; best point `repo check-body` and `issue check` |
| One feature per pull request; a diff holds only that change | [How we work](how-we-work.md#working-in-parallel) | `reviewer`, the coordinator | judgement |
| Run the local gate before Review | [Testing](testing.md#local-gate) | `task check` writes the `checked` stamp; `pr open` refuses without a current one (`task review` does not need it) | enforced |
| A stacked pull request says `Depends on #N`, has no auto-merge and is never merged into its base | [Audit checks](repo-conventions.md#audit-checks) | `repo audit` `pr.stacked-*`, `pr.merged-into-base` | partly (after the fact) |
| A pull request does not merge while a pull request it `Depends on` is unmerged | [Repository conventions](repo-conventions.md#workflows) | CI `dependencies` (required, `ci dependencies`); it reruns when a dependency merges (`ci rerun-dependencies`) | enforced at CI |
| A failed run on `main` (nightly, push or manual) is an issue at once | [Repository conventions](repo-conventions.md#workflows) | CI `ci-failure`: `ci report-failure` opens a `P-critical` issue or comments on the open one | enforced at CI |
| No pull request sits conflicting or failing | [How we work](how-we-work.md#merging) | `hygiene.yml` labels; Conclave's stop guard blocks the coordinator's turn end ([Conclave](conclave.md#watchers)) until a problem is held (`pr hold`) | enforced (Conclave) |
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
| A claim passes its checks in order (open issue, nobody holds it, `S-ready-to-start`, `P-critical` first, Blocked by done, the open-claims limit); a `workflow` issue is outside the limit | [Issues](issues.md#claiming) (`claims`) | `claim.yml` (`coney-tools claim run`); `task create` runs the same checks before it posts | enforced |
| Claim commands are posted by the tools, never written by hand: `/claim` by `task create`, `/progress` by the stage tools, `/unclaim` and `/claim renew` by `claim unclaim` and `claim renew` | [Issues](issues.md#claiming) | the user-level no-comment-as-user hook (refuses any other comment, and asks `claim is-command` for the shape); `.claude/hooks/hooks.toml` keeps `claim unclaim` and `claim renew` to the coordinator | enforced |
| Work starts with a claim: a task is started, and a pull request opened, only on issues the account holds | [Issues](issues.md#claiming) | `task start` and `pr open` (refuse); the `claim` job of `pr.yml` (`claim check-pr`) for every pull request | enforced (tools); warns on GitHub until the cutover (`claims.require_on_pr`) |
| One claimant per issue: the earliest claim wins, a second assignee is removed, a losing card is marked `lost` | [Issues](issues.md#claiming) | `claim.yml` (one job at a time, every pass), the hourly sweep; `task create`'s 60 s settle | enforced |
| A held claim shows life: `/progress` at each stage (posted by the tools and each passing stage stamp), a push, `/claim renew`; warned at 48 h, released at 72 h, never unwarned | [Issues](issues.md#claiming) | `claim.yml` and the sweep (`claims.release`); the stamp log's observer posts `/progress` | built; release off until the cutover (try it on a `claim-trial` issue) |
| An issue assigned by hand gets a card and the deadline | [Issues](issues.md#claiming) | the sweep (`claims.adopt`) | built; off until the cutover |

## Repository conventions and generated files {#generated}

| Rule | Written in | Enforced by | Status |
| --- | --- | --- | --- |
| A rule a program checks lives in `.github/conventions.yml`; the forms, template and pre-commit agree with it | [Repository conventions](repo-conventions.md) | `repo check`; `repo audit --files` (`files.*`) in `check changed` and CI | enforced |
| Generated blocks, binding pages, reference lists, mission pages and function lists are never edited by hand | [Writing these docs](writing-docs.md#style) | `repo render --check`, `natives render --check`, `refs render --check`, `missions render --check`, `research render --check` (CI `python`) | enforced |
| The source files data is well formed and agrees with `totals.toml`, and the source map has its marker | [The source files](research-source-files.md) | `research render --check` (CI `python`; the docs build runs the same checks) | enforced |
| A page's `owns` names source files that have an address range, and a glossary word names an existing page section and functions Ghidra lists | [Finding where a behaviour lives](research-where.md) | `docs check` (rule `owns`), `research render --check` (CI `python`; the docs build runs the same checks) | enforced |
| A function entry names a function Ghidra lists, by Ghidra's name, and a page section that exists, on the page that owns its address | [The function database](research-function-database.md) | `research render --check` (CI `python`; the docs build runs the same checks) | enforced |
| Every `@orig` tag has its function database entry with `coney`, and every such entry its tag | [Conventions](conventions.md#the-orig-tag) | `progress check` (CI `python`) | enforced |
| A new `@orig` tag spells the function's name as the database (Ghidra) does | [Recording a port](research-function-database.md#ported) | `progress check --base HEAD^1 --require-base` (CI `python`): fails on the tags the branch adds, warns about the earlier ones, and fails when the base cannot be read | enforced for new tags |
| Mission work updates `research/missions.yaml` in the same commit | [AGENTS.md](repo:AGENTS.md) | `missions render --check` checks the file, not that it moved | judgement |
| CLAUDE.md and GEMINI.md hold only the import line | [AGENTS.md](repo:AGENTS.md) | `repo audit` `files.pointer` | enforced |
| Workflows declare the narrowest `permissions`; no title or body text expanded into a script | [Repository conventions](repo-conventions.md#workflows) | `actionlint` (untrusted expressions in `run:`) | partly; best point a `repo audit --files` check for `permissions:` |
| A workflow that writes to GitHub beyond comments needs a maintainer's approval | [Repository conventions](repo-conventions.md#workflows) | Review | judgement |

## Code {#code}

| Rule | Written in | Enforced by | Status |
| --- | --- | --- | --- |
| clang-format decides layout | [Conventions](conventions.md#formatting) | pre-commit; CI `format` (required) | enforced |
| Warning-free on MSVC, GCC, Clang and Apple Clang | [Conventions](conventions.md#standard-and-compilers) | `/WX` and `-Werror` in every CI build | enforced at CI |
| clang-tidy's checks (`bugprone-*`, `performance-*`, unchecked optional access) | [Conventions](conventions.md#portability-traps) | CI `clang-tidy` (required) on the lines a pull request changes; the nightly pass on every file | enforced at CI (a finding in a line the pull request did not change waits for the nightly pass) |
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
| The Lua decompiler's output compiles back to the same code | [coney-tools](coney-tools-fidelity.md#lua) | `python/tests/lua4` round-trips every fixture and checks the committed chunks against their source when a `luac` is found; the committed expected outputs everywhere | partly: CI has no `luac`, so it runs neither, and a wrong change committed with its regenerated outputs passes there; the round trip is a local check |
| Stand-ins are marked in code and listed on the research page | [Testing](testing.md#stand-ins) | Review | judgement |
| Every research claim has an address and an evidence level | [Evidence levels](research-evidence.md#evidence-levels) | `docs check` fails an address with no level in its block, row or lead line, and a level in a refused form; `reviewer` reads the claims that cite no address and whether a lead line is true | partly |
| A `confirmed (runtime)` claim cites a run record written by the tool that ran it | [Run records](research-evidence.md#run-records) | `research claims` (the record exists and meets `runs` in the conventions file; its digest matches); the records' own test in CI `python`; the record's who is marked `hook` or `unverified` | partly: `check changed` and CI do not run `research claims` yet; a hand-written record with a computed digest passes, so the reviewer reads the values |
| A research page has the front matter and the sections of its kind, one owner per address, addresses that resolve in the executable, no copied paragraph, no date outside In Coney, and is within its length limit; a page that cannot yet is listed in `research/legacy.toml`, which only shrinks | [Writing these docs](writing-docs.md#research-pages) | `docs check` on the changed pages (a legacy page that is touched is held to the mechanical rules page-wide), which also runs the whole-file check of `docs legacy --check` when the change touches a research page, research data, the conventions or the checks (pre-commit gate `check changed`; CI `mkdocs`, required) | enforced on touched pages |
| A source file has a doc comment on every public function and a comment above every other one whose name does not say everything (not a one-statement accessor or forwarder, nor an `override`), a doc comment on every public type a header declares and `@orig` or `@coney-only` on every gameplay function; a test case cites where its expected values come from; a port is recorded `compared` (only the ones already marked `legacy` stay so); what is below the standard is listed and the lists only shrink | [The standards](coney-tools-standards.md#standards) | `standards check`, run inside `docs check` whenever a change touches a source or test file, the function database or the legacy file (CI `mkdocs`, required; `check changed`) | enforced from the cutover (2026-10-25, `standards.enforce`): a touched file below the standard, a touched port not `compared`, a mark out of step and a new legacy mark are errors |
| Docs build with `mkdocs build --strict`, lines within 120 columns | [Writing these docs](writing-docs.md#preview-and-build) | `check changed`; CI `mkdocs`, `markdownlint` (required); pre-commit markdownlint | enforced |
| One living page per subject; no machine paths; say what is true now | [Writing these docs](writing-docs.md#style) | Review | written |
| A function counts as understood with a Ghidra name, plate comment and a cited page row | [Research workflow](research-workflow.md#understood) | `progress check`, `progress ghidra --check` | enforced |

## Checks being added {#in-progress}

Checks being built; their rows change when they merge:

- **Networking checklist**: review items for network code.
- **The cutover switches** ([The cutover](how-we-work.md#cutover)): `pull_requests.open_only`, `claims.adopt`,
  `claims.release`, and `tasks.start.slots.exempt_workflow` turning off.

## What stays judgement {#judgement}

No program can tell whether a test proves the fix, a picture shows what its caption says, a claim's evidence level is
right or a pull request holds one feature. Those rules live in the reviewer checklists ([Reviewing](review/index.md))
and the `reviewer` agent's instructions ([Task review](review/task.md)); a program checks the
shape around them (a `Runtime:` line exists and has numbers; a verdict is fresh) so the reviewer's time goes to the
meaning.
