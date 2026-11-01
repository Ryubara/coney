---
name: repo-hygiene
description: Coordinator only. Run the repository audit and fix each finding by its fix class (auto, skill, maintainer), then fix the issues that cite the pull requests that just merged. Use after every merge round and once a week, or when asked to tidy the issues, pull requests, labels or workspace.
---

# Repo hygiene

You tidy the repository's issues, pull requests, labels and workspace against `.github/conventions.yml`. The audit
finds the problems; you fix them by the class the audit gives each one. Read
[Repository conventions](../../../docs/guides/repo-conventions.md) and [Issues](../../../docs/guides/issues.md) first if
you have not this session. Only the coordinator writes to GitHub
([Working from an issue](../../../docs/guides/issues.md#agents)): an agent that is asked to run this reports what it
would change and does not run the `gh` writes below.

## 1. Look

Run these from a worktree of your own, never by editing the main checkout
([Sharing one machine](../../../docs/guides/how-we-work.md#shared-machine)); write the output to your scratch folder.

```sh
git fetch origin
uv run --project python coney-tools repo audit --files --github --json > <scratch>/audit.json
uv run --project python coney-tools repo audit --local --json > <scratch>/audit-local.json
uv run --project python coney-tools pr list-stuck
```

Read `notices` first: a notice names what the audit could not look at (no `origin/main`, settings not readable), and a
check that did not run found nothing. The weekly `hygiene` workflow writes the same findings as a report comment on the
"Repo hygiene" issue and as a comment on each item; use whichever is fresher, but a fix is confirmed only by a new run.

## 2. Fix by class

Each finding has `check`, `item`, `message`, `severity` and `fix`. Take them in this order: `auto`, `skill`, then
`maintainer`. Fix errors before warnings.

### `auto`: a command fixes it

| Check | Command |
| --- | --- |
| `files.generated-blocks` | `coney-tools repo render`, then commit the result on a branch (a unit like any other) |
| `files.command-reference` | `coney-tools docs render`, then commit the result on a branch (a unit like any other) |
| `gh.labels` | `coney-tools repo sync-labels --apply` |
| `ws.disk`, `ws.worktrees` | `coney-tools workspace clean --apply` (it never touches busy or unmerged work) |

### `skill`: you fix it with judgement

Keep the author's words: change only what the finding names. Write the new text to a file in scratch and apply it to an
issue with `coney-tools issue edit N --body FILE` (it checks the body first; the issue hook refuses `gh issue edit
--body` and `--body-file`). A pull request's text is not edited by hand: the PR guard refuses `gh pr edit` with
`--title`, `--body-file` or `--attach` ([After the switch](../../../docs/guides/pr.md#after-the-switch)), so the
engineer fixes `PR.md` and the coordinator runs `pr open <task>` again, or you say it in a comment (`gh pr comment`).
Never paste a body into a shell line. Never delete an issue, a pull request or a comment. When the right answer is not
yours to give (a missing summary, a missing reproduction), ask the reporter with `gh issue comment` and set
`S-needs-info`; do not invent it.

| Check | What to do |
| --- | --- |
| `issue.sections`, `issue.form`, `issue.placeholder`, `issue.screenshots` | Rewrite the body to the form's shape (`coney-tools issue check` names what differs) (`gh issue view N --json body`, then `coney-tools issue edit N --body FILE`), moving the author's text under the right headings. Add `Screenshots: none (reason)` only when the reason is plain. If the content is missing, ask. |
| `issue.labels` | Set exactly one type, area, priority (after triage) and status label as [Triage](../../../docs/guides/issues.md#triage) says: `gh issue edit N --add-label ... --remove-label ...`. Replace an unknown label with the right one. |
| `issue.closed-status` | `gh issue edit N --remove-label <S- label>`; the `issue-status` workflow normally does it. |
| `issue.person-word`, `pr.person-word` | Reword "owner" to "user" for a person who plays or reports, "maintainer" for a person who decides. |
| `issue.closed-unchecked` | File the follow-up issue for the unchecked item (or tick it if it was done) and write its number into the item (`coney-tools issue edit N --body FILE`). When the message says "closed by #M, whose Checklist is all ticked", read #M: if it did the items, tick them and name #M. |
| `issue.stale-status` | `gh pr view N` or `gh issue view N`, then change "open" to "merged" (or the true state) and tick what it finished (`coney-tools issue edit N --body FILE`). |
| `issue.links` | Make the link relative and point it at a file and anchor that exist on `main`. |
| `issue.provenance` | Add the `From:` line naming where the issue came from. |
| `issue.attachment-personal` | Never quote the log. Ask the author in a comment to replace their name with `~` in the log, attach the new copy and delete the old link; a maintainer may remove the link from the body meanwhile ([Attached logs](../../../docs/guides/issues.md#attachments)). |
| `issue.attachment-data` | Open the log only to judge it (never copy it anywhere). If it holds game files or game text, a maintainer removes the link from the body and asks the author to attach a log without it ([LEGAL.md](../../../LEGAL.md)); if it is a long ordinary Coney log, leave it. |
| `issue.triage-age` | Triage it now ([Triage](../../../docs/guides/issues.md#triage)). |
| `issue.info-age` | Ask once more in a comment, or close as not planned with the reason. |
| `issue.progress-idle` | Ask the engineer for its state; return the issue to `S-ready-to-start` when nobody works on it. |
| `issue.claim-orphan` | The issue says it is being worked on but nobody is assigned: claim it with `task create` if someone is on it, else set it back to `S-ready-to-start`. |
| `issue.review-without-pr` | Find the pull request (`gh pr list --search "#N"`) and link it, or set the status back to `S-in-progress`. |
| `pr.title`, `pr.body` | The author fixes the text in the pull request's description file in their scratch folder (`PR.md`), then the coordinator runs `pr open <task>` again, which regenerates and checks it. A pull request with no unit or from a fork: ask its author in a comment. |
| `pr.unfinished-items` | The author finishes each unticked item in the pull request, or takes it out: work the pull request does not finish lives in its own issue. Then they update the description file and rerun `pr check-body`, and the coordinator runs `pr open <task>` so the pull request carries it. |
| `pr.remaining-uncited` | File an issue for each unchecked Remaining item and name it in a comment on the merged pull request. Writing it into the description is refused by the PR guard, so list the pull request for the maintainer, who edits it on GitHub; the audit reports it until then. |
| `pr.affected-unedited` | Edit each open issue the merged pull request lists under Issues affected, as [After a merge](../../../docs/guides/issues.md#after-a-merge) says: change what the pull request made out of date, tick what it finished, relabel or close the issue, with `coney-tools issue edit N --body FILE`. An issue the line no longer concerns: see step 2 there. |
| `pr.idle` | Ask the author; close the pull request with a comment when the work is abandoned. |
| `pr.stacked-depends` | `pr open` releases only a unit based on the default branch, so a stacked pull request cannot be updated: close it with a comment naming the pull request it waits for, and start the work again as a unit from `origin/main` once that one has merged ([No stacked work](../../../docs/guides/how-we-work.md#working-in-parallel)). |
| `pr.stacked-automerge` | `gh pr merge N --disable-auto`; auto-merge waits until the base merges. |
| `pr.base-merged` | Take the branch into a unit with `task start <task> --adopt` (`--adopt-branch <branch>` when the branch is named otherwise) ([adopting](../../../docs/guides/issues.md#adopting)); the engineer rebases it in the worktree (`git rebase --onto origin/main <old base> <branch>`) and redoes the stages; the coordinator first retargets the pull request (`gh pr edit N --base main`: `pr open` turns on auto-merge, and a pull request still on the dead base could merge into it), then runs `pr open <task> --adopt <head>`. |
| `pr.merged-into-base` | Close by hand the issues the pull request fixes, with a comment naming it, and move the work onto `main`. |
| `ws.scratch` | Look at the paths the finding lists (a generic `.bin` can be a tool file), then delete the finished work's captures from its scratch folder (generated media may be deleted), or ask the engineer. |
| `files.pointer`, `files.exist`, `files.address`, `files.forms`, `files.template`, `files.ci-scope`, `files.ci-places`, `files.large-file-limit`, `files.game-files`, `files.markdown-hook` | A change to the repository: make it on a branch in a worktree as one task, with the check's rule from the conventions, and take it through the pipeline ([the pipeline](../../../docs/guides/how-we-work.md#the-pipeline)). |

### `maintainer`: stop

A finding with fix `maintainer` (`gh.ruleset`: the ruleset or the repository settings differ from the `merge` section) is
a maintainer decision. Do not change the ruleset, the settings, the conventions file's `merge` section or the repository's
visibility to make it pass. Report it to the maintainer with the finding's message and which side you think is right.

## 3. After a merge round: the issues the pull requests affect

For each pull request that merged, take the issues from two places and bring each up to date
([After a merge](../../../docs/guides/issues.md#after-a-merge)):

1. **The issues it lists.** Its Issues affected section names each open issue it partly fixed, changed or made out of
   date, with a line on how (`gh pr view N --json body`). Edit each one (`coney-tools issue edit N --body FILE`):
   tick what the pull request finished, rewrite what it made untrue, set the status label, close an issue that is now
   done. An issue listed with a line you cannot act on: step 2 of
   [After a merge](../../../docs/guides/issues.md#after-a-merge).
   `pr.affected-unedited` finds the ones left unedited.
2. **The issues that cite it.** Search the open issues that name the pull request
   (`gh issue list --state open --search "#N"`), change "open" to "merged" where the text names it, tick the
   checklist items it finished, and close or relabel an issue it fixed
   ([Writing an issue a newcomer can read](../../../docs/guides/issues.md#writing)). `issue.stale-status` finds the
   ones you miss.

If the list looks short for the size of the change, run `coney-tools issue impact --base <merge base> --head <tip>` on
the merged branch's commits for candidates the author did not list, and edit those too.

When a merged pull request changed engine files, also start the whole C++ suite with the disc on `main`, in the
background (`run_in_background`; it takes a whole suite's time and holds a check slot):

```sh
uv run --project python coney-tools ci disc-suite --agent <your id> --apply
```

`task check` runs only the C++ tests a change reaches and CI has no disc, so this is where a disc test no check ran is
run ([ci disc-suite](../../../docs/guides/coney-tools-ci.md#disc-suite)). A failure files a `P-high` issue (or comments
on the open one) by itself; triage it like a CI failure on `main`. One run covers every merge before it: start it once
per round, not once per pull request.

## 4. Check and report

Run the audit again. Report the counts per class before and after, what you changed (item numbers), what you left and
why, every `maintainer` finding, and every notice. If a finding came back, a workflow or a rule is not doing its job: say
so in the report so that it is fixed at the source (the conventions or the check), not by hand again.

## Limits

* Change only what a finding names. No bulk relabelling, no mass closing, no rewriting an issue that has no finding.
* Never put game data, a local path or the repository's address in a body or a comment.
* A fix that needs a repository change is a task on a branch, like any other, and goes through the pipeline.
* This skill and the `hygiene` workflow share one source of rules, `.github/conventions.yml`; when a rule needs
  changing, change it there and rerun `coney-tools repo render`, not in this file.
