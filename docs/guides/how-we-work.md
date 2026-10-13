# How we work

This page is the map of Coney's reverse-engineering process: what happens between the original game and a line of
Coney code, which tools sit at each step, and how many people (or agents) work at once without stepping on each
other. It stays high level and links to the guide that holds the detail. Read it first; read the linked guide when
you are about to do that step.

## The pipeline

Every feature goes through the same steps:

1. **Read the code.** Before starting, an analyst checks `docs/research/` and the open pull requests for existing or
   in-flight coverage and extends it ([Before you research](research-workflow.md#upstream)). Then the analyst studies
   the original executable `SLUS_212.15` in Ghidra: names the functions, writes plate comments and works out what
   each one does.
2. **Watch it run.** Where the code alone is unclear, the analyst checks it in the original game running in PCSX2:
   memory reads and writes, scripted input, per-update traces and reference frames.
3. **Write it down.** The finding goes on a research page in `docs/research/`, in prose, with the address it comes
   from and an [evidence level](research-workflow.md#evidence-levels) on every claim.
4. **Build it fresh.** An implementer, who has not read the original's code, writes Coney's version from the page
   and cites the original with `@orig` plus the page link ([Conventions](conventions.md)).
5. **Compare.** The same scenario runs on both games, and traces or frames are compared
   ([Comparing with Coney](research-workflow.md#comparing-with-coney)). A difference goes back to step 1 or 4 as a
   question or a fix.
6. **Merge.** Finished work goes to `main` after the full set of checks.

## Two roles: the clean room

An analyst reads the original (Ghidra, disassembly, PCSX2) and writes research pages; an implementer writes engine
code and tests from those pages and Coney's own code, and never opens Ghidra or PCSX2. The split is per function:
whoever has read a function's original code never implements that function
([Clean room](research-workflow.md#clean-room), [LEGAL.md](repo:LEGAL.md#clean-room)).

An implementer who needs more asks a question; a coordinator routes it to the analyst who owns that area, and the
answer lands on the page, not in a private message. Neither role ever puts game data in the repository.
What a page must hold to be coded from, with three real features traced to the merged result, is in
[From research to code](research-to-code.md).

## The tools

- **Ghidra, shared.** One Ghidra project of the PS2 executable (with the Emotion Engine extension), kept open by a
  headless ghidra-mcp server on port 8090. Every analyst works in that one project at once, so a name given by one
  is seen by all; never undo another analyst's names. Setup: [Ghidra + ghidra-mcp](ghidra.md).
- **The Xbox build as a reading aid.** The same function in the Xbox executable can read faster (inline strings,
  named library calls). It is only read; evidence is always cited at PS2 addresses
  ([Xbox executable](../research/xbox-executable.md)).
- **PCSX2, several copies with claims.** Portable PCSX2 copies (`pcsx2`, `pcsx2-b`, ...) each have their own
  settings, states and PINE port, so several analysts can run the game at once. A copy is used only inside a claim:
  `coney-tools pcsx2 claim --agent <id>` hands out a free copy, `pcsx2 release` frees it, `pcsx2 status` shows who
  holds what, and a claim whose process died is taken over ([Driving PCSX2](research-workflow.md#driving-pcsx2)).
- **Input over PINE only.** The game is driven by writing pad state into its memory (the `scripted-pad`,
  `right-stick` and `puppet` patches), with analog sticks at realistic partial deflections. No tool brings PCSX2 to
  the front or types into it, so the machine stays usable; a maintainer's quick-save slots are read-only,
  and analysts save states to files.
- **Traces and frames.** `coney-tools pcsx2 record` and `coney-tools trace` record the same scripted scenario on the
  original and on Coney and diff them; Coney's `--camera`, `--freeze-world` and `--screenshot` flags reproduce a
  reference frame ([coney-tools](coney-tools.md)).
- **Extraction.** `coney-tools extract` turns the player's disc into open formats (PNG, glTF, WAV) in a folder
  outside the repository ([extract](coney-tools.md#extract)).

## The local layout

Everything that is not source code sits next to the repository, never inside it: the disc and extracted files,
PCSX2 copies, Ghidra and its project, worktrees and a scratch folder. The repository cannot leak game data through
a stray `git add`, and every worktree shares the same tools. Details: [Local workspace](workspace.md).

## Working in parallel

Many tracks run at once, each owning one subsystem, mission or question:

- **One branch and one worktree per track**, under `worktrees/coney/<branch>/`. Nobody edits the main checkout.
- **Small shared files are regenerated, not hand-merged**: binding pages and similar files come from generators
  that the merge reruns. Files every pull request would change are not committed at all: the progress tables and the
  README's badges are produced by the docs build ([Measuring progress](#measuring-progress)), so a merge to `main`
  never makes another pull request conflict on them.
- **Pull requests merge one at a time**, by the maintainer or a coordinator the maintainer has allowed to, never by
  the agent that wrote them. Before merging, the coordinator checks more than CI: the title, one feature per pull
  request, the description, docs and tests that go with the change, and a diff that holds only that change. Each
  pull request is squash-merged once every required check passes (tests, generators, docs build), so it
  lands on `main` as one commit; work-in-progress commits on the branch are dropped by the squash and need no
  tidying first.
- **Agents work locally; the coordinator releases to CI** from a queue ([below](#release-queue)).
- **Each track's status is a file on disk**, `STATUS.md` in its scratch folder ([Track status](#status)), not in
  anyone's head.

## Local first: the release queue {#release-queue}

CI is shared and limited (about 20 jobs at once, 5 of them on macOS; a full code pull request takes about 20
minutes), so it must never hold an agent back. Agents do all their work locally and hand finished units to the
coordinator, who pushes them when CI has room.

- **An agent never pushes and never opens a pull request.** It commits on its branch and runs every local check that
  applies before calling a unit finished: `coney-tools build --test` for code, `uvx pre-commit run --all-files`, pytest,
  `mkdocs build --strict` ([The local gate](testing.md#local-gate)). CI should only ever find what the local
  platform cannot see.
- **The unit is one pull request's worth,** not a whole task: one fix or feature with its tests and docs. For each,
  the agent writes the pull request's title and description (from `.github/pull_request_template.md`) to a file in
  its scratch folder (`PR.md`, or `PR-<n>.md` for several), messages the coordinator the branch, the commit range and
  the file, and goes on with the next unit.
- **Dependent units are stacked.** The next unit branches from the previous one and its description names the
  pull request it depends on. After the earlier one is squash-merged, the later branch is moved onto `main` with
  `git rebase --onto origin/main <old base> <branch>` (the old base is the earlier branch's tip), so the squashed
  commits are not replayed.
- **The coordinator keeps the release queue** in its state file and releases units while the agents keep working:
  it checks the queue (`gh run list --status queued` and `--status in_progress`), pushes the branch and opens the
  pull request with `gh pr create --body-file <PR.md>` when there is room, run from the author's scratch folder with
  one `--attach ./shots/<file>.png` per capture the text refers to (gh 2.99+;
  [Capturing and attaching](testing.md#pr-media)). Priority: engine fixes first, then what
  unblocks other work. A pull request runs only the jobs its change can break, so a documentation-only one is cheap
  ([What CI runs](building.md#what-ci-runs)).
- **Every description is reviewed before it is pushed** ([below](#pr-review)). The coordinator runs
  `coney-tools repo review <PR.md> --worktree <worktree>` and pushes only when `repo check-review` passes. Any edit to
  the description or its pictures, and any new commit on the branch, makes the verdict stale: review again.
- **Stale failures restart themselves.** `coney-tools repo pr-health --rerun --apply` (run from the coordinator's
  watch loop) reruns a required check that failed because the runner or GitHub did (cancelled, never started, timed
  out, or a log that shows the runner was lost), at most twice per workflow and commit, and never a real lint, build or
  test failure. For a `title` or `body` check that failed on text fixed since, it reports that the pull request needs a
  new event (edit the title or description) because a rerun would reuse the old text
  ([pr-health](coney-tools.md#repo-pr-health)).
- **A failure CI finds later** (another platform, a check that cannot run locally) goes to the coordinator when it is
  trivial, otherwise to a small fresh agent ([When a pull request fails checks](agents.md#ci-failures)); never back
  to the agent that wrote the unit.
- **A description is public:** the clean-room rules of a research page apply (addresses and short findings, no
  decompiler output).

### The description review {#pr-review}

`coney-tools repo check-body` (and CI) only checks a description's structure. On 2026-10-12 a pull request was
released whose screenshots did not match it: three of its four pictures showed a letterboxed scene instead of
gameplay, and its before and after pair were the same file. So a second reader goes over every description before it
is pushed: the `pr-reviewer` agent ([Running agents](agents.md#setup)). It reads the description, the branch's diff and
the logs the description cites, opens every picture, and writes a verdict: whether each picture shows what the text
says (gameplay, not a scene, loading screen or menu; the right level; before and after that differ), whether each
claim in Summary and Testing is backed by the diff or a log, whether `Fixes #N` fits, that nothing is left for later,
that `Docs: none` is allowed and that AI use is filled.
The gate also holds the `Model:` line to the branch's commits: `repo check-review` (or `repo check-body --worktree`)
refuses a line that does not name every model a `Co-Authored-By` trailer on them credits; CI cannot, having no commits
([the Model line](coney-tools.md#repo-model-line)).

```sh
uv run --project python coney-tools repo review <PR.md> --worktree <worktree>
uv run --project python coney-tools repo check-review <PR.md> --worktree <worktree>
```

`repo review` runs the agent headlessly (`claude -p`, a throwaway run in the scratch folder) and then
`repo check-review`, the gate the release step runs before it pushes ([repo review](coney-tools.md#repo-review)). It
also runs the title and body checks CI runs, so a description CI would refuse never reaches the push. A failing
verdict goes back as a fix to the description or the captures, never as a push. The gate's rules, generated
from `pull_requests.review` in [.github/conventions.yml](repo:.github/conventions.yml):

<!-- conventions:begin pr-review -->
- The `pr-reviewer` agent (`.claude/agents/pr-reviewer.md`) writes `REVIEW.json` and `REVIEW.md` next to the description
  file: `REVIEW.json` holds `tip`, `body_sha256`, `verdict` (`pass` or `fail`), `images` (each local image's `path` and
  `sha256`) and `findings` (each with `severity` `blocker` or `note`, `where` and `text`).
- `repo check-review` passes only when the description's title (line 1) and body (the rest) pass `repo check-title` and
  `repo check-body` (a local image is allowed, as `gh --attach` uploads it), the verdict is `pass` with no `blocker`
  finding, `tip` is the worktree's HEAD, `body_sha256` is the description file's hash now and every local image the
  description shows is listed with its hash now; any new commit, edit or replaced picture makes the verdict stale.
- The description's hash is the SHA-256 of the whole file (title line included) as UTF-8, with a leading BOM dropped and
  CRLF or CR turned into LF.
- Two byte-identical images in one description are refused, whatever the verdict.
<!-- conventions:end -->

People contributing from their own fork push and open pull requests themselves
([CONTRIBUTING.md](repo:CONTRIBUTING.md)); the queue is how a coordinator runs many agents on one machine.

## Track status {#status}

Each track keeps `STATUS.md` in its scratch folder, so a stopped track is resumed from disk, not from a dead agent's
context: what is done (with commits), what is left, which units are queued or released (branch, commit range,
description file, pull request number once open), and the log paths that matter. The agent updates it as each unit
is finished; a fresh agent continues from it and the branch ([Running agents](agents.md#cache)).

## Sharing one machine {#shared-machine}

Many agents and a maintainer use one computer at the same time. These rules each exist because a past session broke
something:

- **Never edit the main checkout.** Work in your own worktree and address it with `git -C "<absolute path>"`: a
  relative `cd` that failed once ran a build in the wrong checkout.
- **Never use `git stash`.** The stash list lives in the shared repository, so every worktree sees one list, and a
  `stash` followed by a `pop` can take another track's work. To set work aside, commit it as a WIP commit on your own
  branch (or a local `wip/<branch>` branch) and reset or cherry-pick it back later. Never pop, apply or drop a stash
  you did not make.
- **Stop only what you started.** Never kill processes by image name (`taskkill /IM`, `Stop-Process -Name`,
  `pkill`): other tracks run the same programs (the game, test runners, the emulator). Stop your own background
  task, or its process tree by id (`taskkill /T /PID <pid>`). Stopping a background task does not always stop the
  children its shell started, so check that nothing of yours is still running.
- **Keep the machine usable.** Nothing you start may take the keyboard focus or send keystrokes to another
  window; drive programs through their own interfaces (for the emulator, see
  [Driving PCSX2](research-workflow.md#driving-pcsx2)). Keep build parallelism moderate, and do not run
  virtual-machine builds locally when the project's CI covers them. On Windows that means no WSL builds and no
  clang-tidy runs: GitHub CI is the Linux and macOS gate, and a failure there is fixed forward on the branch
  ([CI](building.md#what-ci-runs)).
- **Keep the drive from filling up.** A build is about 4 GB and every track has its own worktree, so forgotten
  worktrees fill a drive (once down to 3 GB free, which broke builds). An agent creates a `.coney-busy` file in its
  worktree when it starts and deletes it when it finishes, and deletes its own `build/` folder when it hands over a
  unit. The coordinator runs `coney-tools workspace clean --apply` after every merge round (it removes merged
  worktrees and old build folders, and never touches busy or unmerged work) and `coney-tools workspace disk` before
  starting builds: below 30 GB free it cleans first ([Cleanup](workspace.md#cleanup)).
- **Heavy builds take a slot, from a tool.** A full engine build uses every core, so only two run at once and the
  rest queue: every build and test run goes through `coney-tools build`, which claims a slot like a PCSX2 copy (a
  fast lane for small jobs, a fair queue, priority for builds someone waits on; [`build`](coney-tools.md#build)), and
  a hook refuses raw `cmake --build`, `ctest` and `ninja`. The machine's number of slots is `build_slots` in
  `coney.local.toml` ([Local workspace](workspace.md#local-config)). Tracks that only read, write docs or drive
  the emulator need no slot.
- **Share the long-lived tools, one user per copy.** One Ghidra server serves every analyst: use it as it is, never
  restart or stop it, and start it only if it is down ([Ghidra + ghidra-mcp](ghidra.md)). An emulator copy has one
  user at a time, through a claim that is released when done ([Driving PCSX2](research-workflow.md#driving-pcsx2)).
- **Keep Python out of the tree.** Use a private virtual environment in your scratch folder
  (`UV_PROJECT_ENVIRONMENT`) rather than a `.venv` inside a worktree, which every tool and linter would then scan.
- **Scratch is yours per branch.** Put output in `scratch/<branch>/`; game-derived files (captures, states, dumps,
  screenshots) stay there and are deleted when done.
- **Your own tooling and records stay yours.** State files (`HANDOFF.md`, `STATUS.md`), personal scripts, launchers,
  editor or agent settings and notes live beside the repository, never in it: the repository holds only the project.
- **Generated images, video and audio may be deleted without asking.** Screenshots, frame captures and recordings
  pile up during reviews; any track may remove the ones the project produced. Write the path literally or guard a
  variable (`rm -f "${DIR:?}"/*.png`): a path built from a possibly empty variable can delete far more than meant.

## Issues {#issues}

Bugs and tasks are tracked as issues, opened from a form and labelled by type, area, priority and status. The
coordinator triages them and is the only one who writes to GitHub; an agent reads an issue (`gh issue view N
--comments`) and is often briefed with just its number. A pull request that finishes one says `Fixes #N`, so merging
closes it. The whole process: [Issues](issues.md).

**Issue rules are enforced, not remembered.** An issue is drafted, checked and filed only with `coney-tools issue
draft`, `check` and `create` (or `edit` and `link`); the repository's Claude Code hook (`.claude/hooks/issue_guard.py`)
refuses a raw `gh issue create`, a `gh issue edit` that sets the body and a `gh api` call that writes an issue; and
the `issue-check` workflow checks every issue opened or edited on the website or through the API and keeps one
comment listing the problems. The audit flags what slips past all three ([Filing an issue](issues.md#filing)).

**A pull request merges only when every task in it is finished.** Its Checklist lists what it finishes, all ticked, and
CI refuses an unticked item; there is no "Remaining" list. Work an agent finds that is not part of its task goes in its
message to the coordinator (title, area, research page, what "done" means) and the coordinator files it as an issue of
its own; a pull request that finishes an issue says `Fixes #N`
([Work found along the way](issues.md#follow-up-work)). The issue states its problem first, links pages by relative links
and names the source pull request's status truthfully ([Writing an issue a newcomer can read](issues.md#writing)).
The `hygiene` workflow comments on an issue or pull request that breaks the repository's rules and keeps that one
comment current; fix what it lists, and the comment says so when it is done
([Workflows](repo-conventions.md#workflows)).
The state file (`HANDOFF.md`) keeps only the live state and issue numbers, never a list of work to do: the work list
is the open issues.

## Testing {#testing}

Tracks never wait for a person to play their work, so the automated tests stand in for one. Every track proves its
work the same way: tests that start through the player's path, input driven like a gamepad, an adaptive playthrough
for every mission checkpoint, and diffs against the original. People play release builds at milestones and their
findings come back as fixes. The standard, including what "done" means for each kind of work and the checks to run
before reporting: [Testing](testing.md). A pull request shows the result: what was run with its numbers, and
before and after screenshots of anything visible ([What a pull request shows](testing.md#pr-evidence)).

## The coordinator {#coordinator}

One agent (or person) runs the work day to day: it keeps the project's state file and each track's state current,
starts and briefs the tracks, routes questions between them, merges and ships. It works under these rules:

- **Decide and keep going.** The coordinator approves its own designs and plans, and when one item ends it takes the
  next from the state file instead of asking whether to start. It stops only when a maintainer says so.
- **Escalate only maintainer decisions**, the list in [AGENTS.md](repo:AGENTS.md) (`repo.owner_decisions` in
  [.github/conventions.yml](repo:.github/conventions.yml)); everything else the coordinator decides itself.
- **Tidy after every merge round and once a week.** Run the `/repo-hygiene` skill: it audits the issues, pull requests,
  labels and workspace and fixes each finding by its class ([Merging safely](#merging)).
- **Keep every gap staffed.** When a track finishes, the next open gap gets a track at once: unfinished fixes,
  subsystems that differ from the original, missions still to build (several at a time, one per track). The
  priorities are on the [roadmap](../roadmap.md#priorities).
- **Merge on the automated gate, not on play-tests.** Pull requests merge once the
  [checks](testing.md#local-gate) pass; the coordinator fixes small gate failures (lint, formatting, generated
  pages) itself instead of sending the work back.
- **Ship after every merged feature.** Push, build the release locally, and hand the play-testers one list of what
  changed and how to reach it ([Play-testing](testing.md#play-testing)). Research-only merges need no build.
- **Resume what was interrupted.** A usage limit, an account switch or a cleared session can stop agents mid-task;
  the coordinator restarts each stopped track as a fresh agent from its `STATUS.md` and branch before taking new work.
- **Keep its own context small.** The coordinator re-reads its whole conversation on every turn: it saves the state,
  clears and continues from it at a natural boundary ([Running agents](agents.md#cache)).
- **A lesson becomes a rule in the same commit.** A mistake that cost time (a tool pitfall, a process race) is
  written into the guide where it applies, generally (why and the method), alongside its fix; briefs only point to
  it.

## Merging safely {#merging}

- **After a merge round, run `/repo-hygiene`.** The skill runs the audit and fixes what it finds by the finding's fix
  class, including the issues that cite a pull request that merged (change "open" to "merged", tick what it finished,
  close or relabel an issue it fixed: [Writing an issue a newcomer can read](issues.md#writing)). It stops at the
  maintainer decisions ([The /repo-hygiene skill](repo-conventions.md#hygiene-skill)).
- **Merge only finished work.** A pull request is merged as it stands at its tip, and the squash drops the commits
  inside the branch, so a "WIP" commit in the middle does no harm. A branch whose tip is a paused, half-finished
  state is never merged: finish it, or cut the finished part into a new branch.
- **Check a branch's diff before merging it.** A branch can be contaminated with unrelated reverted files (a bad
  rebase or a stray copy); a diff against the merge base that touches files outside the track's scope means the
  branch is rebuilt, not merged.
- **GitHub merges, one pull request at a time.** Auto-merge is on: the maintainer or the coordinator enables it on a
  pull request that follows the rules, and GitHub squash-merges it as soon as every required check passes, then
  deletes its branch. The settings are the `merge` section of the conventions file, shown in
  [What CI runs](building.md#what-ci-runs). No local script merges anything, so there is no lock to take; the only
  merges done by hand are of `main` into a branch.
- **Do not hide a failed command in a pipe.** `git merge origin/main | tail -1 && next` reports the status of `tail`,
  not of the merge. Run the command on its own and test its exit code.
- **Regenerate, then resolve by hand only what a generator cannot.** On a conflict in a generated file, take either
  side and rerun the generator; resolve prose pages by hand, keeping both sides' claims.
- **Run the local gate again after merging `main` into a branch or rebasing it**
  ([The local gate](testing.md#local-gate)), and fix a red `main` forward rather than leaving it red. Before editing a
  page many tracks share, rebase on `main` first.
- **Rebuild the release build after an engine merge.** When a merged change touches the engine, the coordinator
  builds the `release` preset ([Build and test](building.md#build-and-test)) from the updated `main`, so the
  maintainer can play the latest `main`. A merge that changes only docs or research needs no build.
- **Make a new pull-request check required only once every open branch has it.** Editing a pull request's
  description reruns the `pr` workflow from that pull request's own merge commit, so a branch made before a new job
  was added to `.github/workflows/pr.yml` never runs it, and a required check that never reports blocks the pull
  request for good. Add the job, wait until every open pull request's branch includes it (rebase or merge `main`
  into the old ones), then add it to the required checks of the ruleset on `main`.
- **Later: a merge queue.** The ruleset on `main` has "require branches to be up to date" off to save CI time, so two
  pull requests that each pass can still break `main` together (a semantic conflict). Once there are regular outside
  contributors, turn on GitHub's merge queue for `main`: it tests each pull request on top of the ones queued ahead
  of it before merging.
- **Do not wait.** When a commit is finished and you are sure of it, report its sha in one line and go on to the next
  item in your scope. Never block on a merge, a rebase, a manual test or an answer: use a clearly marked stand-in or
  take the next unblocked item. Test findings come back later as fixes.

## Agents {#agent-conduct}

- **Choose how to run each agent deliberately.** Whether the coordinator does a task itself, forks or starts a
  fresh agent, which model it gets, how it is briefed and how it runs long commands decide most of what the work
  costs: [Running agents](agents.md).
- **An agent knows only what its brief and the repository tell it.** The brief names the role, the model, the
  worktree, the scope it owns, the files it must not touch and what "done" means, and points at the rules rather than
  pasting them ([Briefing](agents.md#briefs)).
- **No agent waits on CI, or pushes.** An agent works local-first and hands each finished unit to the coordinator's
  [release queue](#release-queue); the coordinator pushes, watches CI and sends a failure to a fresh agent
  ([When a pull request fails checks](agents.md#ci-failures)). An agent idle past its cache lifetime re-writes its
  whole context, often hundreds of thousands of tokens, when it resumes.
- **Long-running tracks keep going.** They hand each finished unit to the release queue and continue with the next
  item in scope until it is empty, instead of stopping after one finding.
- **Report what a reader needs:** commits, what works now and the command that shows it, stand-ins, open items and
  the results of the checks. Short. A pull request's description carries the same as a Testing section (what ran,
  the numbers) and a Screenshots section (before and after for anything visible, uploaded with `gh --attach` and never committed):
  [What a pull request shows](testing.md#pr-evidence).
- **Commits carry the agent's own model name** in a `Co-Authored-By` trailer (the format is
  `commits.trailers` in [.github/conventions.yml](repo:.github/conventions.yml), explained in
  [CONTRIBUTING.md](repo:CONTRIBUTING.md#agents-commits)); an agent never pushes, opens or merges a pull request.

## Starting on another game {#another-game}

The process, not the game, is what carries over. Reuse unchanged: the clean-room split, the evidence levels, the
research database and page layout, the trace-and-diff method, the workspace layout, and every rule on this page and
in [Lessons learned](research-workflow.md#lessons-learned). Replace: the executable and its address base, the
emulator and its remote-memory interface, the patches and hook caves (they are found per game from unused memory),
the pad-input patch, the tick counter that numbers updates, and the format tools. Start by finding, in the new game,
four anchors: where input is read, what counts one update, where free memory for hook code is, and a string table
that names the subsystems.

## Measuring progress

A function counts as understood when it has a meaningful name and plate comment in the shared Ghidra project and a
research page cites its address with an evidence level ([The Understood measure](research-workflow.md#understood)).
`coney-tools progress` turns that into the [progress page](../progress/index.md) and a per-subsystem backlog. The
page's tables and the README's badge data are generated by the docs build and published from `main`; a pull request
changes only their inputs (`docs/progress/functions.toml`, the research pages, the roadmap), never a table, and CI
runs `coney-tools progress check` on those. If a branch from before this change conflicts on `README.md` or
`docs/progress/index.md`, take `main`'s version of both.

## Where next

- Doing research: [Research workflow](research-workflow.md), then [Ghidra + ghidra-mcp](ghidra.md).
- Writing code: [Conventions](conventions.md), [Testing](testing.md), then [Building and testing](building.md).
- Running agents: [Running agents](agents.md).
- Bugs and tasks: [Issues](issues.md) (forms, labels, triage, working from an issue).
- Writing pages: [Writing these docs](writing-docs.md).
