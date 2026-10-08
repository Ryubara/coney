# How we work

This page is the map of Coney's reverse-engineering process: what happens between the original game and a line of
Coney code, which tools sit at each step, and how many people (or agents) work at once without stepping on each
other. It stays high level and links to the guide that holds the detail. Read it first; read the linked guide when
you are about to do that step.

## The pipeline

Every feature goes through the same steps:

1. **Read the code.** An analyst studies the original executable `SLUS_212.15` in Ghidra: names the functions,
   writes plate comments and works out what each one does.
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

Analysts and implementers are never the same person or agent
([Clean room](research-workflow.md#clean-room), [LEGAL.md](repo:LEGAL.md)):

| | Analyst | Implementer |
| --- | --- | --- |
| Reads | Ghidra, disassembly, PCSX2 | research pages, Coney's code |
| Writes | research pages, Ghidra names and comments | engine code, tests |
| Never | writes engine code | opens Ghidra or PCSX2 |

An implementer who needs more asks a question; a coordinator routes it to the analyst who owns that area, and the
answer lands on the page, not in a private message. Neither role ever puts game data in the repository.

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
  the front or types into it, so the machine stays usable; the owner's quick-save slots are read-only, and
  analysts save states to files.
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
- **Small shared files are regenerated, not hand-merged**: binding pages, progress bars and similar files come from
  generators that the merge reruns.
- **A coordinator merges, one branch at a time.** Each merge rebases the branch on `main` and runs every check
  (tests, generators, docs build); only one merge runs at a time. Work-in-progress commits are never merged, and a
  branch is squashed into feature-sized commits before it is pushed.
- **State lives on disk, not in anyone's head.** Each track keeps a short state file (role, branch tip, done, next,
  open questions) in the scratch folder, so another person or agent can take it over cold.

## Sharing one machine {#shared-machine}

Many agents and the owner use one computer at the same time. These rules each exist because a past session broke
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
- **Keep the owner's machine usable.** Nothing you start may take the keyboard focus or send keystrokes to another
  window; drive programs through their own interfaces (for the emulator, see
  [Driving PCSX2](research-workflow.md#driving-pcsx2)). Keep build parallelism moderate, and do not run
  virtual-machine builds locally when the project's CI covers them.
- **Keep Python out of the tree.** Use a private virtual environment in your scratch folder
  (`UV_PROJECT_ENVIRONMENT`) rather than a `.venv` inside a worktree, which every tool and linter would then scan.
- **Scratch is yours per branch.** Put output in `scratch/<branch>/`; game-derived files (captures, states, dumps,
  screenshots) stay there and are deleted when done.

## Merging safely {#merging}

- **Merge only finished commits.** A "WIP" commit made when a track paused is never merged: create a clean commit
  (or cherry-pick the finished part) instead.
- **Check a branch's diff before merging it.** A branch can be contaminated with unrelated reverted files (a bad
  rebase or a stray copy); a diff against the merge base that touches files outside the track's scope means the
  branch is rebuilt, not merged.
- **One merge at a time, with a lock.** Merges run from a script that takes a lock directory; remove the lock only
  after confirming no merge process runs (list processes by command line). A loop started before a session reset
  can survive it and run alongside the new one in the same checkout, which loses merges.
- **Do not hide a failed merge in a pipe.** `git merge ... | tail -1 && next` reports the status of `tail`, not of
  the merge. Run the merge on its own and test its exit code.
- **Regenerate, then resolve by hand only what a generator cannot.** On a conflict in a generated file, take either
  side and rerun the generator; resolve prose pages by hand, keeping both sides' claims.
- **Run the full checks on the merged result,** including the platform builds CI will run, before pushing; fix
  failures forward rather than leaving `main` red. Before editing a page many tracks share, rebase on `main` first.
- **Do not wait.** When a commit is finished and you are sure of it, report its sha in one line and go on to the next
  item in your scope. Never block on a merge, a rebase, a manual test or an answer: use a clearly marked stand-in or
  take the next unblocked item. Test findings come back later as fixes.

## Agents {#agent-conduct}

- **Brief every agent completely.** A brief names the role (analyst or implementer), the worktree, the scope it owns,
  the files it must not touch, and pastes the standing rules that matter for the work (clean room, input driven like
  a gamepad, never focus the emulator, faithful before extras). An agent knows only what its brief and the repository
  tell it.
- **Put the right model on the work.** Reverse-engineering work, and implementation that depends on its results, goes
  to the strongest available model; light, non-RE work (docs, generators) can use a smaller one.
- **Long-running tracks keep going.** They commit in groups, tell the coordinator each group's sha, and continue with
  the next item in scope until it is empty, instead of stopping after one finding.
- **Report what a reader needs:** commits, what works now and the command that shows it, stand-ins, open items and
  the results of the checks. Short.
- **Commits carry the agent's own model name** in a `Co-Authored-By` trailer (the format is in
  [Conventions](conventions.md)); nobody pushes or merges except the coordinator.

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
`coney-tools progress` turns that into the [progress page](../progress/index.md) and a per-subsystem backlog.

## Where next

- Doing research: [Research workflow](research-workflow.md), then [Ghidra + ghidra-mcp](ghidra.md).
- Writing code: [Conventions](conventions.md), then [Building and testing](building.md).
- Writing pages: [Writing these docs](writing-docs.md).
