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
uv run --project python coney-tools repo pr-health
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
| `gh.labels` | `coney-tools repo labels --apply` |
| `ws.disk`, `ws.worktrees` | `coney-tools workspace clean --apply` (it never touches busy or unmerged work) |

### `skill`: you fix it with judgement

Keep the author's words: change only what the finding names. Write the new text to a file in scratch and apply it with
`--body-file`; never paste a body into a shell line. Never delete an issue, a pull request or a comment. When the
right answer is not yours to give (a missing summary, a missing reproduction), ask the reporter with `gh issue comment`
and set `S-needs-info`; do not invent it.

| Check | What to do |
| --- | --- |
| `issue.sections`, `issue.form`, `issue.placeholder`, `issue.screenshots` | Rewrite the body to the form's shape (`coney-tools issue check` names what differs) (`gh issue view N --json body`, then `gh issue edit N --body-file`), moving the author's text under the right headings. Add `Screenshots: none (reason)` only when the reason is plain. If the content is missing, ask. |
| `issue.labels` | Set exactly one type, area, priority (after triage) and status label as [Triage](../../../docs/guides/issues.md#triage) says: `gh issue edit N --add-label ... --remove-label ...`. Replace an unknown label with the right one. |
| `issue.closed-status` | `gh issue edit N --remove-label <S- label>`; the `issue-status` workflow normally does it. |
| `issue.person-word`, `pr.person-word` | Reword "owner" to "user" for a person who plays or reports, "maintainer" for a person who decides. |
| `issue.closed-unchecked` | File the follow-up issue for the unchecked item (or tick it if it was done) and write its number into the item. |
| `issue.stale-status` | `gh pr view N` or `gh issue view N`, then change "open" to "merged" (or the true state) and tick what it finished. |
| `issue.links` | Make the link relative and point it at a file and anchor that exist on `main`. |
| `issue.provenance` | Add the `From:` line naming where the issue came from. |
| `issue.attachment-personal` | Never quote the log. Ask the author in a comment to replace their name with `~` in the log, attach the new copy and delete the old link; a maintainer may remove the link from the body meanwhile ([Attached logs](../../../docs/guides/issues.md#attachments)). |
| `issue.attachment-data` | Open the log only to judge it (never copy it anywhere). If it holds game files or game text, a maintainer removes the link from the body and asks the author to attach a log without it ([LEGAL.md](../../../LEGAL.md)); if it is a long ordinary Coney log, leave it. |
| `issue.triage-age` | Triage it now ([Triage](../../../docs/guides/issues.md#triage)). |
| `issue.info-age` | Ask once more in a comment, or close as not planned with the reason. |
| `issue.progress-idle` | Ask the track for its state; return the issue to `S-ready-to-start` when nobody works on it. |
| `issue.review-without-pr` | Find the pull request (`gh pr list --search "#N"`) and link it, or set the status back to `S-in-progress`. |
| `pr.title`, `pr.body` | The author fixes the text in the pull request's description file in their scratch folder; if you fix it yourself, `gh pr edit N --title ...` or `--body-file`, and rerun `repo check-body`. |
| `pr.unfinished-items` | The author finishes each unticked item in the pull request, or takes it out: work the pull request does not finish lives in its own issue. Then they update the description file and rerun `repo check-body`. |
| `pr.remaining-uncited` | File an issue for each unchecked Remaining item and write its number into the merged pull request's description. |
| `pr.idle` | Ask the author; close the pull request with a comment when the work is abandoned. |
| `pr.stacked-depends` | Add the `Depends on #N` line to the description. |
| `pr.stacked-automerge` | `gh pr merge N --disable-auto`; auto-merge waits until the base merges. |
| `pr.base-merged` | The author rebases onto `main` (`git rebase --onto origin/main <old base> <branch>`); then `gh pr edit N --base main`. |
| `pr.merged-into-base` | Close by hand the issues the pull request fixes, with a comment naming it, and move the work onto `main`. |
| `ws.scratch` | Delete the finished work's captures from its scratch folder (generated media may be deleted), or ask the track. |
| `files.pointer`, `files.exist`, `files.address`, `files.forms`, `files.template`, `files.ci-scope`, `files.large-file-limit`, `files.markdown-hook` | A change to the repository: make it on a branch in a worktree as one unit, with the check's rule from the conventions, and queue it ([the release queue](../../../docs/guides/how-we-work.md#release-queue)). |

### `maintainer`: stop

A finding with fix `maintainer` (`gh.ruleset`: the ruleset or the repository settings differ from the `merge` section) is
a maintainer decision. Do not change the ruleset, the settings, the conventions file's `merge` section or the repository's
visibility to make it pass. Report it to the maintainer with the finding's message and which side you think is right.

## 3. After a merge round: the issues that cite the pull requests

For each pull request that merged, search the open issues that name it
(`gh issue list --state open --search "#N"`), change "open" to "merged" where the text names the pull request, tick the
checklist items it finished, and close or relabel an issue it fixed
([Writing an issue a newcomer can read](../../../docs/guides/issues.md#writing)). `issue.stale-status` finds the ones
you miss.

## 4. Check and report

Run the audit again. Report the counts per class before and after, what you changed (item numbers), what you left and
why, every `maintainer` finding, and every notice. If a finding came back, a workflow or a rule is not doing its job: say
so in the report so that it is fixed at the source (the conventions or the check), not by hand again.

## Limits

* Change only what a finding names. No bulk relabelling, no mass closing, no rewriting an issue that has no finding.
* Never put game data, a local path or the repository's address in a body or a comment.
* A fix that needs a repository change is a unit on a branch, like any other, and goes through the release queue.
* This skill and the `hygiene` workflow share one source of rules, `.github/conventions.yml`; when a rule needs
  changing, change it there and rerun `coney-tools repo render`, not in this file.
