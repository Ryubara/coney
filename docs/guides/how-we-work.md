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

## Measuring progress

A function counts as understood when it has a meaningful name and plate comment in the shared Ghidra project and a
research page cites its address with an evidence level ([The Understood measure](research-workflow.md#understood)).
`coney-tools progress` turns that into the [progress page](../progress/index.md) and a per-subsystem backlog.

## Where next

- Doing research: [Research workflow](research-workflow.md), then [Ghidra + ghidra-mcp](ghidra.md).
- Writing code: [Conventions](conventions.md), then [Building and testing](building.md).
- Writing pages: [Writing these docs](writing-docs.md).
