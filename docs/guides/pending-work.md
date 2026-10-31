# Pending work that overlaps a unit

A unit starts from `main`, so work that is finished but not merged is invisible to it. An engineer could repeat research
another unit did, contradict a research page that is about to change or write code that conflicts with what lands next
(#925 and #921 changed the same review tools, and #921 had to be rebased). So `task start` reads the **pending work**
and lists what meets the unit. The maintainer decided (2026-10-26) that agents take pending work into account, and that
`main` stays the truth. The commands are in [`task`](coney-tools-repo.md#task); the start is described in [Starting a
task](issues.md#starting).

## What is pending {#pending}

Every open pull request, and every other unit in progress on this machine: a started task that is not a fix, has no
`finished` stamp, has not opened its pull request and still has its worktree. A unit that has a pull request is listed
by the pull request, and only while it is open, so a merged unit never appears. The unit's own pull request is left out.

## What counts as an overlap {#overlap}

A pending change touches a path the unit wants (the same file, a file inside a folder it wants, or one a glob of its
scope fits) or an `@orig` address it wants.

* The unit wants the `scope` of its task file and the paths and addresses the issue writes in the task file's Issues,
  Goal, Done and Scope sections, read as the leads search reads them (`identifiers_in`, [Task
  files](issues.md#task-files)). A section anchor in a path (`docs/guides/agents.md#slots`) matches the whole page.
* Once the unit has a branch it also wants what the branch changes and what is changed but not committed: the paths
  and the addresses in the changed lines of its diff since it left its base. What it merged from `main` is not its
  own, and a file `main` now holds exactly as the branch has it (a squash merge) is no longer pending. The lines are
  read as [`issue impact`](coney-tools-issues.md#issue-impact) reads them: a test file's invented values and generated
  files do not count. An `@orig` function whose body changed without its tag line changing is met by its file's path,
  not by its address: `issue impact` also reads those functions, but that takes 10 to 20 seconds a unit, and every
  start, check and review reads every unit in progress.
* A pull request's paths come from `gh pr list`; its addresses from its diff, read the same way, only when the unit
  wants an address and for at most `tasks.overlap.diff_prs` pull requests. A diff `gh` refuses (too large) is a note
  and the next is read; when `gh` does not run or times out (`tasks.overlap.gh_seconds`) the remaining diffs are not
  read, so a GitHub that hangs costs one timeout, not thirty.

The most shared addresses and paths come first, then pull requests before units. How much is read and listed is
`tasks.overlap` in [.github/conventions.yml](repo:.github/conventions.yml).

## Where it is shown {#shown}

* `task start` writes the list into the task file's "Pending work that overlaps" section (when it was read, then one
  line per pull request or unit with its title, state and the paths and addresses shared) and prints it in its report.
  A dry run only prints it.
* `task check` and `task review` read the pending work again and print the list when it differs from the section: work
  that appeared since, work that left, or the unit's own changes now meeting more. They record it, so a unit that runs
  for hours sees what appeared meanwhile, and say nothing when it is the same.
* `coney-tools task overlap [TASK] [--write]` shows it at any time, including for a task that is not started yet: the
  coordinator reads it to name the overlaps in a brief ([Briefing a fresh agent](agents.md#briefs)). `--write` records
  it in the task file.

A read that fails (GitHub down, git failing in another worktree) is a note, and it never refuses a start or fails a
check. When the pull requests cannot be read, a stage prints that the list was not checked and records nothing.

## What the engineer does with it {#engineer}

1. Reads the overlapping changes before researching or changing that area, so it does not repeat research or
   contradict a page that is about to change.
2. Never builds on them: `main` is the truth. The one exception is a unit stacked on the other branch, which says so
   with a `Depends on #N` line.
3. When an overlap matters, notes it in `STATUS.md` and tells the coordinator in its report or in `QUESTION.md`
   ([Questions](conclave.md#questions)). The coordinator orders the two units (the one that lands first, or a stack).

The coordinator runs `task overlap <task>` before it briefs an engineer and names the overlaps that matter in the brief;
the whole list is in the task file either way ([How we work](how-we-work.md#working-in-parallel)).
