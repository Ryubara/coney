# Resuming task check

How `coney-tools task check` ([the command](coney-tools-repo.md#task-check)) goes on from the checks an earlier run
passed for the same change: [when it applies](#resume), [what it compares](#key) and [what it cannot see](#blind).

## Resuming a run {#resume}

A full check takes 30 to 60 minutes here, and a run can end without a verdict for reasons that are not the change's: the
build waited for a slot and gave up (exit 75), a program was missing (exit 2), the process was killed, or a check failed
for a cause outside the tree (a flaky test). `task check` therefore **resumes by default**: a rerun for the same change
goes on from the first check that did not pass and prints `skip <check>: passed in the run of <time>` for each one it
takes. `--no-resume` runs every check (a `--detach` run takes it along). It is the default, not a flag to remember,
because forgetting it costs an hour and a resume applies only to a tree exactly like the one the checks passed on
([the key](#key)): a change that edits anything starts over. What the key cannot see is listed
[below](#blind); `proof.resume.max_age_hours` (24, in
[.github/conventions.yml](repo:.github/conventions.yml)) bounds it, since a pass older than that runs again, and
`--no-resume` is for a run you want from scratch.

A run keeps a **ledger**, `check-resume/ledger.json` in the task's scratch folder (the parent's, for a fix task),
written after every check that passes, so a run that never reached a verdict keeps what it did. A pass is kept only
when the worktree is still the one the run began with once the check ends: a check that changed it is not kept, and the
run ends with exit 2 as it always did. A check that failed is
never in it: it and every check after it run again (`--keep-going` keeps the passes after a failure too). A passing run
removes the ledger, because its stamp is the record from then on. The protect hook keeps agents from writing the folder,
as for stamps and build records ([Stamps](stamps.md#build-record)).

## What a resume compares {#key}

A run uses a ledger only when every field of its **key** is the same as the run that made it:

| Field | Why |
| --- | --- |
| change id (`diff_sha256`) | The change the stamp is bound to |
| tip and base commit | A clean rebase keeps the change id but moves the ground the checks ran on, and a build record is bound to its tip: a rebase, an amended message or an empty commit runs everything |
| profile and its checks | A bigger profile (`--profile`) or a rule that adds a check is another list |
| the rules the checks read | A hash of `proof` and the scope classes of `paths`, as read from the base |
| the disc setting | Where the tests' disc came from (`CONEY_DISC`, `game_dir`) and its path |

When a key differs the run says so (`resume: the earlier run was for another base: every check runs`). The build run
(`msvc-ci-build` and `ctest-full-disc`) resumes only as a whole: both checks must be in the ledger and the **build
record** they name must still read as one clean run of the preset on this tip with the disc ([The build
record](stamps.md#build-record)), else the build runs again, as it does after failed tests.

**The stamp stays honest.** The `checked` stamp, pass or fail, lists every check in `detail.checks` with the same
`checks_sha256` as a full run, and `detail.resumed` names the checks taken from earlier runs, the times of those runs
and the hash of the key: `{"checks": ["docs-check"], "from": ["2026-11-19T09:02:11Z"], "key_sha256": "..."}`. A resumed
build keeps its record, which the stamp covers by `build_record_sha256` as always, and the facts of a pull request
description say `resumed from an earlier run: ...`. `pr verify` reads the stamp as before and prints a note naming the
resumed checks, as `pr open` does ([pr verify](pr.md#verify)).

## What the key cannot see {#blind}

This is what `--no-resume` and the age limit are for: files git
ignores (a stale `build/` or a cache a check reads), the contents of the disc (its path is keyed, the ISO is not hashed),
which checkout's `coney-tools` started the run and the versions of the tools the checks call (ruff, mypy, pytest,
pre-commit, the compiler). An entry dated after the run began (a clock set back, a ledger made by hand) is not used.
