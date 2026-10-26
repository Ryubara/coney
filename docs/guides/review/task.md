# Task review

The review stage of a task is one command, `coney-tools task review`, and one agent, the `reviewer`
([Running agents](../agents.md#reviewer)). The command prepares what the reviewer is to judge, records what it judged
as a `reviewed` stamp in the task's [stamp log](../stamps.md#log), and refuses to start the reviewer on a change that a
program can already see is not ready. The checklists the reviewer applies are the other pages of [Reviews](index.md);
this page is the command, the file the reviewer writes and the stamp.

## The three steps {#steps}

1. **Prepare**, `task review <task>`: the [tool part](#tool-part) runs. Every problem it finds is listed, no reviewer
   is started and nothing is written. When it passes, it writes the [packet](#packet): the tip, the base, the hash of
   the change, which [checklists](#kinds) apply, the `@orig` functions, the research pages and the description's
   hashes.
2. **Judge**: the reviewer reads the packet and applies the checklists, then writes the [verdict](#verdict)
   (`REVIEW.json`) and its report (`REVIEW.md`) in the review folder, copying the packet's hashes into the verdict.
3. **Record**, `task review <task> --record`: the packet is built again from the repository as it is now, the verdict
   must answer it, the caller must be [a reviewer who may record it](#who), and one `reviewed` stamp is appended,
   `pass` or `fail`. A verdict with a problem writes no stamp.

The reviewer runs steps 1 and 3 itself, so the stamp's `who` is its own identity ([Who runs a
command](../agents.md#identity)). A coordinator starts the reviewer as a subagent. A person without the hooks, or an
engineer who wants an early look, runs `task review <task> --run`, which does all three with a headless reviewer
(`claude -p`).

Review may run while Check runs: both are bound to the same change. A review is of what is committed,
so the worktree must be clean. Any new commit that changes the change's [hash](#stamp), and any edit to the
description's title, summary paragraph or pictures, makes the verdict stale, and the review is done again; `--text`
redoes the description alone.

## The command {#command}

```sh
uv run --project python coney-tools task review [TASK] [--worktree DIR] [--base REF] [--pr-file FILE] [--text] \
    [--record | --run] [--claude CMD] [--model M] [--effort E] [--timeout-minutes N]
```

| Argument or option | Default | What it does |
| --- | --- | --- |
| `TASK` | `CONEY_TASK`, else the branch | The task: its scratch folder holds the stamp log, the description and the review folder |
| `--worktree DIR` | the current checkout | The unit's worktree: the branch under review, which must be clean |
| `--base REF` | `origin/<default branch>` | What the change is measured from (the merge base of `REF` and the tip) |
| `--pr-file FILE` | `<scratch>/<task>/PR.md` | The engineer's description file: the title on line 1, a `## Summary`, the pictures |
| `--text` | off | Review the description alone. Needs a passing full review of the same change and pairs; the packet's `kinds` is `text` |
| `--record` | off | Step 3: record the verdict in the review folder as a `reviewed` stamp |
| `--run` | off | All three steps with a headless reviewer; not together with `--record` |
| `--claude CMD` | `claude` | The Claude Code executable, for `--run` |
| `--model M` | the agent file's (`opus`) | The model, for `--run` |
| `--effort E` | the agent's | `low`, `medium`, `high`, `xhigh` or `max`, for `--run` |
| `--timeout-minutes N` | 60 | Stop a `--run` review after this long (it then records nothing) |

The review folder is `<scratch>/<task>/review/` (`tasks.review` in [.github/conventions.yml](repo:.github/conventions.yml)
names the folder, the three files and the agent). It holds `PACKET.json` (the tool's), `REVIEW.json` and `REVIEW.md` (the
reviewer's) and `recorded-<seq>.json`, a copy of each verdict that was recorded. The newest is also
`<scratch>/<task>/reviewed.json`, which the stamp's `review_sha256` covers (`pr verify` still reads a `review.json`
recorded before the name changed: on Windows that name is the same file as a description review's `REVIEW.json` beside
`PR.md`). Preparing removes an earlier verdict and report: a verdict made for another packet is never mistaken for this
one's.

| Exit | Meaning |
| --- | --- |
| 0 | Prepared and ready for the reviewer; or recorded as a `pass` (also a rerun that changes nothing) |
| 1 | Refused (every problem is listed), or recorded as a `fail` |
| 2 | It cannot run: a bad argument, no checkout, unreadable conventions |

## The tool part {#tool-part}

Everything below is checked before a reviewer is started, and again at `--record`, since the branch may have moved. A
problem is printed with its prefix and what to redo; all problems are listed, not the first.

| Prefix | What is checked | Redo |
| --- | --- | --- |
| `nothing to review`, `uncommitted changes` | The branch has a commit beyond the base; the worktree is clean | The engineer commits |
| `stamp log:` | The log is intact ([Verifying a log](../stamps.md#verify)) | Report to the coordinator: the unit is unusable |
| `compared:` | Each changed `@orig` function has a pairs file and a current `compared` stamp for its body and for that file | `original compare <addr>` |
| `copy-check:` | [`orig copy-check`](../coney-tools-fidelity.md#orig) finds no fingerprint on the lines added | The engineer, then `original compare` |
| `fails-first:` | Each added C++ `TEST_CASE` has a passing `fails-first` stamp (Python tests are not stamped) | `original fails-first` |
| `docs check:` | `docs check` passes on the changed pages, research pages included | The engineer |
| `title:`, `docs:`, a missing picture | The description's title follows the rules, the [docs rule](../coney-tools-repo.md#repo) holds (`Docs: none` is read from the description), every local picture exists | The engineer edits the description |
| `text:` | For `--text`: a passing full review still covers the change and the pairs | `task review`, without `--text` |

What a program cannot decide is the reviewer's: whether the pairs say the same as the original, whether a research
claim is graded right, whether a picture shows what the text says. Checks that are planned join this table when they
exist ([test citations](../testing.md#done), the standards of touched files).

## The packet {#packet}

`PACKET.json` is one JSON object. The reviewer copies its values and never computes a hash.

| Key | Holds |
| --- | --- |
| `v`, `task`, `mode` | Version 1; the task; `full` or `text` |
| `tip`, `base` | The commit reviewed and where the branch left its base |
| `diff_sha256` | The hash of the change (what the branch itself changes), which a clean rebase keeps |
| `kinds` | The [checklists](#kinds) to apply |
| `files` | The paths the change touches |
| `functions` | Each changed `@orig` function: `key`, `where`, `coney`, `addresses`, `names`, `body_sha256` (`orig review-stamp --branch` lists the same) |
| `pairs_set_sha256` | The hash of the pairs files of those functions, which a `compared` stamp covers one by one |
| `pages` | Each touched research page and the addresses it writes |
| `tests` | Each added C++ test and the result of its `fails-first` stamp |
| `text` | The description: `title`, `paragraph`, the pictures (`path`, `sha256`) and the three hashes of them |
| `rubric_version` | The [orig rubric](orig.md)'s version |

### Which checklists apply {#kinds}

| Kind | When | Checklist |
| --- | --- | --- |
| `code` | A changed file that is neither documentation nor research | [Code](code.md) |
| `docs` | Changed Markdown outside the research paths | [Docs](docs.md) |
| `research` | A change under the research paths (`tasks.review.research_paths`) | [Research](research.md) |
| `orig` | A changed function carries `@orig` | [Orig review](orig.md) |
| `text` | Always | [Description](text.md) |
| `commits` | Always, except for `--text` | [Commits](commits.md) |

A change of several kinds applies each checklist to the files it covers. Review of `research` and `orig` is
reverse-engineering work and needs a reviewer on Opus ([Who may record](#who)).

## The verdict {#verdict}

`REVIEW.json` has exactly these keys; `task review --record` reports every key that is missing, unknown or of the wrong
type.

| Key | Holds |
| --- | --- |
| `v`, `task`, `mode` | As in the packet |
| `tip`, `base`, `diff_sha256`, `pairs_set_sha256`, `title_sha256`, `paragraph_sha256`, `images_sha256`, `rubric_version` | Copied from the packet. Any that is no longer the repository's is `stale` and the review is done again |
| `verdict` | `pass` or `fail` |
| `applied` | The kinds applied; every kind of the packet must be there |
| `functions` | One `{key, body_sha256, addresses, class, evidence, note}` per `@orig` function of the packet. `class` is one of the [orig classes](orig.md#classes), `evidence` is `confirmed (code)` or `inferred`, `addresses` are the ones compared and also in `reviewed` |
| `reviewed` | Every `@orig` address compared |
| `pages` | One `{path, checked, unchecked}` per touched research page: the addresses the reviewer checked in Ghidra (`tasks.review.pages_min_addresses` of them, or all of a shorter page, each on the page), or `unchecked` with the reason, which needs a critical or important finding at the page |
| `images` | One `{path, sha256, shows}` per picture of the packet, the hash it has now and what the reviewer saw |
| `findings` | `{severity, where, text}` with `severity` `critical`, `important` or `minor` ([Severity](index.md#severity)) and `where` a `file:line` (or a section of the description) |
| `sampled`, `not_reviewed` | A function the reviewer skipped in a large change is listed by key in `not_reviewed` and `sampled` says why ([the limits](orig.md#limits)) |

Its entries are checked against the packet first, and a problem is listed, never a traceback: an entry that is not an
object, a function, page or picture the packet does not have (or has already listed), a key the entry may not have, a
value of the wrong type, and a list in a `text` verdict that must be empty. Then the verdict must not contradict
itself: `pass` holds no failing class (`diff-uncommented`, `wrong-address`, `transcription`, `diff-bug`) and no
critical or important finding; `fail` has one of them. A `text` review has no functions or pages (its verdict lists
none, in `functions`, `pages`, `reviewed` or `not_reviewed`). `REVIEW.md` is the same verdict for a person, in the
[finding format](index.md#finding-format).

## The stamp {#stamp}

A `reviewed` stamp is bound to the change, not to a commit, so a clean rebase keeps it ([the change
hash](../stamps.md#change)). Its `commit` is the tip reviewed.

| Part | Holds |
| --- | --- |
| `covers` | `diff_sha256`, `pairs_set_sha256`, `title_sha256`, `paragraph_sha256`, `images_sha256` and `review_sha256`, the hash of the recorded verdict |
| `detail` | `verdict`, `scope` (`full` or `text`), `kinds`, `functions` (each compared address to one letter: `c` clean, `d` diff-deliberate-ok, `u` unclear, `a` diff-uncommented, `w` wrong-address, `t` transcription, `b` diff-bug), `pages`, `blockers`, `notes`, `not_reviewed` and `reviewer` (id, type, model). `reviewer` also holds the caller's `session` when the hooks know it. A stamp's `detail` may hold 2048 bytes; when the rest of it is over 1648 bytes (leaving room for `reviewer` and the pointer) the function list is replaced by `function_count`, and the recorded verdict has the list |
| `result` | `pass` or `fail`; a fail stamp is a verdict, so it is written, and a later pass replaces it |

A text review keeps the `functions`, `pages` and `full_seq` of the full review it rests on. Recording the same verdict
again for the same change writes no second stamp.

## Who may record {#who}

The hooks vouch for who runs a command ([Who](../stamps.md#who)). When they do, `--record` refuses a caller that is not
a `reviewer`, a reviewer who is also the author of any stamp of the task but a `reviewed` one (a `compared` or
`fails-first` stamp written by a reviewer counts too: a review is by someone who did not do the work), and a review of
`research` or `orig` kinds from a model whose name has none of `tasks.review.reverse_engineering_models`.

Who is the same person is decided by the id and the session the hooks recorded (`who.agent.id` and
`who.agent.session`). A subagent's id is unique, so equal ids are one agent. Every main session has the id `main`, so
two stamps are by the same main session only when both name the same session; a stamp with no session (made before the
hooks recorded it) or an unknown id is never taken for the caller. When the hooks do not vouch for the caller (a
person at a terminal, no valid identity file, or a `main` caller whose session the hook did not record), the check is
skipped, nothing is refused, and the stamp is written with `source: unverified`; the release step may refuse such a
stamp.

The reviewer starts no agents, and the hooks enforce it twice in `[roles.reviewer]` of `.claude/hooks/hooks.toml`.
`allow_agent_types = []` refuses every Agent call (the agent file gives it no Agent tool either). Its `deny_commands`
refuse a session started from the shell: `claude` or `coney-agent` where a command starts, in any letter case (also
after `&&`, `;`, `|`, `$(`, `then`, launchers such as `uv run`, `uvx`, `npx -y`, `cmd /c` or `Start-Process`, or a
folder such as `.venv/Scripts/`), the session launcher by module or file, `task review --run` and `repo review` (which
always starts one). Even a read-only helper has Bash and no role rules, and a session started from the shell is a
main session the hooks vouch for under a session of its own, so either could write a `compared` stamp for the
reviewer, and the reviewer would grade its own comparison.

This rule judges how a command is spelled; it is not a security boundary. Quoting (`bash -c "claude ..."`), a
launcher it does not list, a name built up at run time and other indirection get past it. It makes the common
spellings fail and a deliberate dodge visible in the transcript. The `--record` check above is the last line of
defence, and it cannot see a stamp written by a session the reviewer started.

## A review outside `task review` {#report}

A change is sometimes reviewed with no task of its own: a fix round, a conflict resolution, a stacked branch or a branch
of the launcher stack. There is then no review folder, packet or stamp, and the `reviewer` has nowhere to write its
report. It writes the report as `CODE-REVIEW.md`, or `CODE-REVIEW-<n>.md` for the n-th round, in the folder the brief
names (usually the scratch folder of the branch), in the [finding format](index.md#finding-format) with the verdict on
the first line; the coordinator reads it there instead of copying it out of the reply. The hooks allow exactly the names
`CODE-REVIEW*.md` besides the verdict files (`write_only` in `[roles.reviewer]` of `.claude/hooks/hooks.toml`; an entry
may be a pattern for the file name, matched in any letter case; a name with a colon never matches), and only outside
every checkout (`write_outside_checkouts`): a report inside a worktree or the main repository is a file an engineer
could commit by mistake. Such a report is not a verdict: it writes no `reviewed` stamp, and a task's own review still
ends in `REVIEW.json` and `--record`.

To find where a stacked or rebased branch starts, the reviewer may run `git merge-base` and `git merge-tree
--write-tree`: the first only reads, and the second adds objects to the object store and changes no index, tree or ref.
`git merge` and the other `merge-*` verbs that write (`merge-file`, `merge-index`, `merge-one-file`) stay refused with
the rest of the verbs that change a ref, the index, the tree or a worktree (the list is in
[Which role to start](../agents.md#roles)). `branch`, `tag`, `stash`, `worktree`, `notes` and `reflog` pass only in
their listing forms (`git branch -a`, `git tag -l`, `git stash list`, `git worktree list`).

## Release checks that predate it {#legacy}

`repo review` (the description) and `orig check-review` (a branch's `@orig` functions) still gate a release until
`pr open` replaces them. They run the same `reviewer` agent and keep their own verdict files; the formats are in
[Description review](text.md#legacy-verdict) and [Orig review](orig.md#verdict).
