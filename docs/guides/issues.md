# Issues

Bugs and work to do are tracked as issues. This page is for three readers: a player who found a bug, a maintainer or
agent who writes a task, and the coordinator who triages and moves issues along. Labels, forms and the rules for
agents all come from files in the repository, so this page and those files change together.

## How to report {#report}

Start from a form; blank issues are off.

- **Bug report** (`.github/ISSUE_TEMPLATE/bug.yml`) is for players. It asks for the Coney version or commit, your
  system (operating system, graphics card, controller), how you started the game (the story, `--play-level`, a
  debug-menu jump or Rumble), the mission and checkpoint, numbered steps to reproduce, the position and heading from
  the [debug menu](debug-menu.md) if you know them, what happens, what the original game does (optional, but it
  tells us whether Coney differs from it), and how often it happens. The exact steps matter most: a bug that can be
  repeated is a bug that can get a regression test ([Testing](testing.md#done)). Its *Log* box takes `coney.log`,
  written right after the bug with the debug menus' *Write bug report log* ([The run log](run-log.md#bug-reports)).
- **Task** (`.github/ISSUE_TEMPLATE/task.yml`) is for maintainers and agents. It asks for a summary that opens with the
  problem ([Writing an issue a newcomer can read](#writing)), the area, the research page that covers it, what "done"
  means (the acceptance test [Testing](testing.md#done) defines for this kind of work), a fix list or steps, and the
  issues it is blocked by (`#12`).

### Writing an issue a newcomer can read {#writing}

An issue is read by someone with no other context: a player, a contributor on their first day, an agent briefed with
only "issue #N". Write it so that the first screenful answers *what is wrong or missing, and what is the state now*.

- **Problem first.** The first sentence of the Summary (or the "What happens" of a bug) says in plain words what is
  wrong or missing for the player or the developer. The current state comes second. Never start from the pull
  request that left the work ("Pull request #55 adds ..."): the issue is about the problem, and that pull request is
  provenance. It goes on the `From:` line, which names where the issue came from and nothing more.
- **State the status as it is.** When the text names a pull request or another issue, check it with `gh pr view N`
  or `gh issue view N` and say what it is: merged, open (not merged yet), closed without merging. Write "merged",
  never "(open)" for a pull request that has landed, and tick the checklist item it finished. An issue is usually
  filed while the pull request it came from is still open, so that wording goes stale when it merges: describe what the
  repository does and lacks rather than only what a pull request will do, and the coordinator fixes the statuses of
  the issues that cite a pull request when it merges ([Merging safely](how-we-work.md#merging)).
- **Link every page by a relative link.** A research page or guide is written as a Markdown link that works from an
  issue page, with the path in the repository as its text:
  `[docs/guides/workspace.md](../blob/main/docs/guides/workspace.md)`, and `#anchor` after the path when a section
  is meant (check the heading's `{#id}` exists). Check the page exists on `main`
  (`git cat-file -e origin/main:<path>`); a page or section that exists only on an unreleased branch is
  named in plain text with a note that it is not on `main` yet. Source files can be linked the same way. GitHub's
  file view opens the page at the top for an `{#id}` anchor, so name the section in words too. Never write the
  repository's or the documentation site's full address anywhere ([AGENTS.md](repo:AGENTS.md)).
- **Make each checklist item a step someone can verify.** "Windows 10 extraction route" becomes "Document or automate
  PCSX2's 7z extraction on Windows 10, whose `tar` cannot unpack it". Say what to do, to what, and how you will know
  it is done; tick the item (`- [x]`) when it is done and name the pull request that did it.
- **Write numbers and names, not shorthand.** An issue refers to `#123` for issues and pull requests, to the
  research page with its section, and to the item numbers of the user's play-test by their text; a word like "the
  unit", "the stack" or a branch name alone means nothing to a newcomer.

### Screenshots {#screenshots}

Every issue has a **Screenshots** section, as every pull request does. It shows the problem: Coney at the place and
moment it goes wrong and, when there is a capture of it, the original from the same place. When there is nothing to
show (a sound, a research question, tooling), the section holds one line, `Screenshots: none (<reason>)`, such as
`Screenshots: none (sound only)`. The forms ask for it, and an issue filed from the command line has the same
`### Screenshots` section ([Filing an issue](#filing)).

Drag screenshots and short videos into the form's Screenshots box, and attach Coney's log, the text it prints in the
terminal you started it from, as a `.txt` file in the bug form's Log box (with no user name or email address in it:
[Attached logs](#attachments)). From the command line,
`gh issue create --attach ./shots/f.png` (or `gh issue edit N --attach ...`) uploads the file, and a
`![alt](./shots/f.png)` already in the body is pointed at the upload (gh 2.99 or later). Captures stay in scratch
and are deleted once uploaded.

### Wording {#wording}

Issues, pull requests and guides call whoever tested, was affected by a problem or created the issue or pull request
"the user" ("From: user playtest of the preview build, 2026-10-11", "the user says", "the user's screenshot"), and
whoever decides "the maintainer" or "the maintainers" ("a maintainer decides", "maintainer decisions"). Never "the
owner" for a person: whoever owns the repository on GitHub is one of the maintainers. The word "owner" stays for what
it means in the code and the research (an object's owner) and in GitHub's own fields. The rule is
`repo.wording` in the [conventions file](repo-conventions.md), and `coney-tools repo audit` finds the banned word.

### What may be attached {#media}

Gameplay screenshots and short videos of Coney or of the original may be attached to issues. That is the only
exception to [No game data](repo:LEGAL.md#no-game-data) for issues: never attach asset files, save files, memory
dumps, executables, BIOS images, disc images, extracted files or the game's text or script source. The bug form has a
required checkbox that says so. A maintainer deletes an attachment that breaks the rule.

### Attached logs {#attachments}

A log is public the moment it is attached, and a person can attach more than they meant: a path that names their
user folder, an email address, or a file that is not a log at all. So the `hygiene` workflow
([Workflows](repo-conventions.md#workflows)) downloads each `.txt` and `.log` attached to an issue's body whenever the
issue is opened or edited (and every open issue's in the weekly sweep) and scans it. Two checks of the audit report
what it finds, in the workflow's one comment on the issue:

- `issue.attachment-personal`: a line shows a user folder (`C:\Users\<name>\`, `/home/<name>/`, `/Users/<name>/`) or
  an email address. Replace the name with `~` (`~\Downloads\disc.iso`), attach the new copy and delete the old link.
- `issue.attachment-data`: the file may be game data: it is not text, it is larger than a log should be, or enough of
  its lines look like the game's script source or data files. Delete it if it holds anything
  [What may be attached](#media) rules out; a long ordinary log can stay.

The comment names the file and the line numbers, never the text, so it does not repeat what it warns about. The
patterns and limits (which links, how much is downloaded, the size and line thresholds) are `hygiene.attachments` in
[.github/conventions.yml](repo:.github/conventions.yml). The download is bounded (a size cap and a timeout), sent
without credentials and only ever held in memory: nothing attached is stored or run. The scan is a heuristic, so a
finding is a prompt to look, and a clean scan is not a promise. `coney-tools repo audit --github --attachments` runs
the same scan over every open issue. Removing the link from the body does not delete the file on GitHub; a person who
attached something private by mistake can ask a maintainer, who can ask GitHub to remove it.

## Filing an issue {#filing}

An issue filed from the command line skips the web form, so a guard runs before it is written. Nobody files, edits or
links an issue with a raw `gh` command: `coney-tools issue` is the only way, and it reads every rule from
`.github/conventions.yml` (`issues`) and the form files in `.github/ISSUE_TEMPLATE/`, so a change to a form or a rule
changes the command with it ([Repository conventions](repo-conventions.md)). The commands are in
[coney-tools](coney-tools-issues.md#issue).

1. **Draft.** `coney-tools issue draft task` (or `bug`) prints a blank body with the form's `###` headings in its
   order. A comment before each section carries the form's own description, whether the section is required, and
   the values it allows (the Area is one of the `A-` labels; a bug's dropdowns list theirs). The `From:` line and the
   `Screenshots: none (<reason>)` convention are there too. Fill it in; the comments may stay, they are not shown and
   the issue is filed without them.
2. **Check.** `coney-tools issue check --title "..." --body FILE [--leftover] [--online]` says what is wrong, one line
   per problem, and exits 1: headings at the wrong level (`## Screenshots`), with other names, extra, missing or out of
   order; a required section empty or `x`; an Area that is not an option; a docs or research page written as plain text
   instead of `[docs/x.md](../blob/main/docs/x.md)`, or linked to a file or `{#anchor}` that is not on `main` (read from
   `origin/main` in your checkout, no network; `git fetch origin` first); a Screenshots section with neither an uploaded
   image nor the `none (<reason>)` line; a leftover without its `From:` line; "owner" for the user; the repository's
   address; a title that is not `area: Verb the rest` ([Titles](#titles)). The form is the one the body's headings
   belong to (the title says nothing about it); `--form` overrides it. The bug form's "How often does it happen?" also
   allows `Unknown`. A page that exists only on a branch stays plain text with a note such as "not on `main` yet". With
   `--online`, the words that call `#N` open, merged or closed are compared with `gh` ([State the status as it
   is](#writing)).
3. **File.** `coney-tools issue create --title "..." --body FILE [--label L ...] [--parent N] [--leftover] [--attach F]`
   runs the same check and then `gh issue create` with the form's labels and the Area's `A-` label; every label is
   validated against the labels of the conventions, and a `--label` of the type or status family replaces the form's
   (`--label docs` for a documentation task). `--parent N` then links the new issue as a sub-issue of issue N, which
   is where a leftover goes (the issue its source pull request fixes). `--dry-run` checks and prints the commands
   and writes nothing.
4. **Change.** `coney-tools issue edit N --body FILE [--title T]` checks the new body the same way before
   `gh issue edit`, and `coney-tools issue link N --parent P` makes an existing issue a sub-issue.

The coordinator, and an agent whose brief says to file, use these commands; any other agent hands the checked body
file to the coordinator.

### Titles {#titles}

An issue title is a pull request title: `area: Verb the rest`, with the areas, the verbs, the length limit and the
`(BREAKING)` ending of [AGENTS.md](repo:AGENTS.md), and no `Bug:` or `Task:` prefix (the forms add none). One
definition (`commits.title` in `.github/conventions.yml`) serves both, so `issue check`, the `issue-check` workflow and
the audit refuse the same titles. `coney-tools issue titles` lists the open issues whose title breaks the rule, with a
proposed title for each and the `gh issue edit` command that applies it; `--apply` runs those commands (the coordinator
does that once, when the rule starts). The proposal takes the area from the issue's labels
(`issues.title_area_by_label`), keeps a verb the title already starts with, otherwise picks `Fix` for a `bug` or `parity`
and `Add` for the rest, and marks any title it had to guess or cut as `review`: a person reads those before applying
them.

**How the rules are enforced.** Four layers, each for a way an issue could skip the form:

- `coney-tools issue draft`, `check` and `create` (above) put the check before the write.
- A Claude Code hook in the repository, `.claude/hooks/issue_guard.py` (registered in `.claude/settings.json`, so every
  contributor and agent session has it), refuses a raw `gh issue create`, a `gh issue edit` with `--body`,
  `--body-file`, `-b` or `-F`, and a `gh api` call that creates an issue, changes its body or title, or links a
  sub-issue. It refuses only where `gh` starts a command, so a command that merely mentions these (an `echo`, a
  commit message, the body of a here-document) passes; labels, titles, comments and closing stay with `gh`, and so
  does a command that names only another repository (`-R owner/name`, `GH_REPO=`, a `repos/owner/name` path), which
  the hook tells from this one by the working directory's `origin` remote. The
  refusal names `coney-tools issue draft`, `create` and `edit`. `coney-tools` itself runs `gh` as a subprocess,
  which the hook never sees.
- The `issue-check` workflow (`.github/workflows/issue-check.yml`) runs `coney-tools issue check --online` on every
  issue opened or edited, so an issue filed on the website or through the API is covered too. When the check
  fails it posts one comment listing the problems, updates that comment on later edits, and deletes it once the
  issue passes. It only comments: it sets no labels and edits nothing.
- `coney-tools repo audit --github` runs the same shape check over every open issue (`issue.form` in
  [Audit checks](repo-conventions.md#audit-checks)), so an issue that was filed before these existed, or
  that slipped through, is flagged afterwards.

## Labels {#labels}

The label scheme follows rust-lang/rust's prefixes (`A-` for the area, `P-` for the priority, `S-` for the status),
so contributors who know that project recognise it. The labels live under `labels` in `.github/conventions.yml`
([Repository conventions](repo-conventions.md)), the one source of truth: edit that file, never the labels on
GitHub. The coordinator makes GitHub match it with `coney-tools repo labels --apply`; without `--apply` the command
prints the `gh label create ... --force` commands instead of running them.

The table is generated from `labels.list` (`coney-tools repo render`). `parity` is a difference from the original and
`enhancement` is beyond it, so it waits until the game is complete ([Faithful first](repo:AGENTS.md)). The task form's
area list is the `A-` labels. The coordinator sets the priority, and an open issue carries one status label at a
time. A closed issue carries none ([What a closed issue keeps](#closed)).

<!-- conventions:begin labels -->
| Family | Label | Meaning |
| --- | --- | --- |
| Type | `bug` | Something in Coney is broken |
| | `parity` | Coney differs from the original game |
| | `task` | A piece of work for a maintainer or an agent |
| | `docs` | Documentation or research pages |
| | `enhancement` | Beyond the original; waits until the game is complete |
| Area | `A-combat` | Area: fighting, grabs, weapons, damage |
| | `A-ai` | Area: AI humans, pathing, goals, cars driven by AI |
| | `A-missions` | Area: mission scripts, checkpoints, objectives |
| | `A-hud` | Area: HUD, menus, text, prompts |
| | `A-graphics` | Area: rendering, models, textures, animation, effects, lighting |
| | `A-audio` | Area: sound, music, speech |
| | `A-physics` | Area: collision, movement, cars, doors, world objects |
| | `A-camera` | Area: cameras and cutscenes |
| | `A-platform` | Area: window, input, gamepad, rumble, saves, loading, disc files |
| | `A-core` | Area: engine core: memory, chunks, tasks, scripting runtime |
| | `A-tools` | Area: coney-tools, build, CI, the sandbox |
| Priority | `P-critical` | Priority: main or a nightly run is broken; before everything else |
| | `P-high` | Priority: blocks play or other work |
| | `P-medium` | Priority: next in line |
| | `P-low` | Priority: when there is room |
| Status | `S-needs-triage` | Status: not yet looked at |
| | `S-ready-to-start` | Status: triaged and accepted; waiting for someone to pick it up |
| | `S-needs-info` | Status: waiting for the reporter |
| | `S-needs-research` | Status: the original must be studied first |
| | `S-in-progress` | Status: an agent or person is working on it |
| | `S-pending-review` | Status: a pull request is open |
| | `S-blocked` | Status: waiting for another issue or a decision |
| | `S-on-hold` | Status: paused by decision; a branch may exist; not in progress |
| | `S-conflict` | Status: a pull request conflicts with main (set by CI) |
| | `S-failing` | Status: a pull request fails a required check (set by CI) |
| Other | `good first issue` | Good for newcomers |
| | `help wanted` | Extra attention is needed |
| | `duplicate` | This issue already exists |
| | `wontfix` | This will not be worked on |
| | `workflow` | Builds the new workflow; its claim is outside the open-claims limit |
| | `claim-trial` | A throwaway issue for testing the claim Action; never real work |
<!-- conventions:end -->

`S-on-hold` is for work that was started and then paused on purpose, such as a held branch that becomes a task
later. It differs from `S-blocked` (waiting for another issue or a decision, with the work still wanted next) and from
`S-in-progress` (someone is working on it now): the idle check (`issue.progress-idle`) looks only at `S-in-progress`.
A claimed issue that is put on hold keeps its claim like one that is blocked: `S-on-hold` is one of the
`claims.paused_labels`, so the claim Action leaves it in place and the release clock waits ([Claiming](#claiming)).
It is also a hold label (`claims.hold_labels`): the claim Action takes `S-in-progress` (or `S-pending-review`) off,
so a held issue has no status label of the claim's own, and puts it back when the hold comes off while the claim still
stands. It takes off only the claim's label. A held issue that also carries `S-blocked` or `S-needs-info` keeps both
labels (a maintainer put each there): the claim stays, its clock waits, and the claim's label comes back only when the
last of them is gone. Such an issue has two status labels, so the audit reports it (`issue.labels`: one is expected)
until one of them is taken off.
By convention (nothing checks it) the issue body names the branch and what remains.

`S-conflict` and `S-failing` are for pull requests, not issues, and nobody sets them by hand: the `hygiene` workflow
puts them on an open pull request that conflicts with `main` or fails a required check, and takes them off when that
is fixed ([Workflows](repo-conventions.md#workflows)).

## Triage {#triage}

The coordinator reads each new issue and:

1. sets the type (`bug` or `parity`, or `task`, `docs`, `enhancement`), the area and the priority;
2. moves an accepted issue from `S-needs-triage` to `S-ready-to-start`: it is understood, has a priority and can be
   picked up as written. Otherwise it asks for what is missing with a comment and sets `S-needs-info`, sets
   `S-needs-research` when the original must be studied first ([Research workflow](research-workflow.md)), or
   sets `S-blocked` when it waits for another issue or a decision;
3. closes a repeat as `duplicate` (pointing at the first), and a request outside the project as `wontfix`;
4. turns a vague report into a task that an agent can take, with a research page link and an acceptance test.

Whoever picks up an `S-ready-to-start` issue claims it ([Claiming an issue](#claiming)), which moves it to
`S-in-progress`. Only an `S-ready-to-start` issue can be claimed, so the coordinator triages first. The audit counts
every status except `S-needs-triage` as triaged, so an `S-ready-to-start` issue already needs its priority label
([Repository conventions](repo-conventions.md)).

## The life of an issue {#life}

Every open issue has exactly one status label, and the coordinator moves it:

| Status | Set when |
| --- | --- |
| `S-needs-triage` | The form sets it. |
| `S-ready-to-start` | Triage accepted the issue: type, area and priority are set and nothing blocks it. |
| `S-needs-info` | Triage found the report incomplete and asked the reporter. Back to triage when they answer. |
| `S-needs-research` | No research page describes the behaviour of the original. |
| `S-in-progress` | An agent or person claims an `S-ready-to-start` issue ([Claiming](#claiming)); a release or `/unclaim` sets `S-ready-to-start` again. |
| `S-pending-review` | The pull request is open, with `Fixes #N` in its body (the claim Action sets it on a claimed issue). |
| `S-blocked` | The issue waits for another issue (named in the task's "Blocked by") or a decision. |
| `S-on-hold` | The maintainer paused the work by decision (a held unit: a branch may exist, nobody is working on it). It is not in progress and not claimable; on an issue that is already claimed it keeps the claim and pauses the release clock like `S-blocked`. On a claimed issue it replaces `S-in-progress` until it comes off, and then the claim's label returns (with `S-blocked` or `S-needs-info` also on, both stay and it returns when the last of them is off). To pick the work up as a new task, the coordinator first gives the claim up with `/unclaim` (the issue keeps `S-on-hold`), then swaps `S-on-hold` for `S-ready-to-start`; to resume the same claim, it just takes `S-on-hold` off. |

An issue is closed automatically when its pull request is squash-merged into `main`, because the body says
`Fixes #N` ([Pull requests](repo:CONTRIBUTING.md)). If the problem is still there after the merge, reopen the issue;
it gets `S-needs-triage` again ([below](#closed)). Close with a reason: completed, not planned (`wontfix`) or
duplicate.

### What a closed issue keeps {#closed}

A status describes work that is still open, so a closed issue never carries an `S-` label: the close reason
(completed, not planned, duplicate) is its final state. It keeps its type, area and priority labels, and
`wontfix` or `duplicate` when they apply, as history. A leftover status such as `S-pending-review` on a closed issue
only misleads whoever filters by it.

The `issue-status` workflow (`.github/workflows/issue-status.yml`) keeps this true without anyone remembering it:

- **On close** it removes every label that starts with `S-`, whoever closed the issue and however (a merged
  `Fixes #N` pull request, the button, `gh issue close`).
- **On reopen** it adds `S-needs-triage`, so a reopened issue is looked at again, unless the issue already has an `S-`
  label (someone set the status as they reopened it).

It is the one workflow that writes to GitHub, and its token is limited to `issues: write`
([Workflows](repo-conventions.md#workflows)). If it ever fails, `coney-tools repo audit --github` reports the closed
issue under `issue.closed-status` and the coordinator removes the label by hand.

## Claiming an issue {#claiming}

To work on an issue, take it: one comment, answered by a GitHub Action, so that two people never work on the same
issue and nobody has to remember to say so. It is the same for everyone, the maintainers and the coordinator included
(the coordinator's `coney-tools task create` posts the comment, [Task files](#task-files)). The rules are `claims` of
`.github/conventions.yml`; the first list below is generated from it. Claiming is available now but nothing makes it
mandatory until the new workflow starts ([How we work](how-we-work.md)); two switches, `claims.adopt` and
`claims.release`, are off until then and say below what they hold back.

<!-- conventions:begin claim-rules -->
- An issue can be claimed only when it carries `S-ready-to-start`, whoever claims it. A claim sets `S-in-progress`
  (`S-pending-review` while its pull request is open); a release or `/unclaim` sets `S-ready-to-start`.
- A maintainer (someone with `admin` or `write` permission on the repository) holds 3 open claims at most, a contributor
  2. Issues whose cards share a `task:` id count as one claim, and a task holds 3 issues at most.
- The claim of an issue labelled `workflow` (a unit that builds the new workflow) neither counts toward that limit nor
  is refused by it; the cap of a task still holds. Only someone who may label issues (triage access or more) can set the
  label.
- A claim stops counting toward the limit once its card reaches `reviewed`, `pr-opened`, `fix`, `finished` or its pull
  request is open, as the engineer's slot frees when the unit passes review; it stays held until it ends.
- While an open `P-critical` issue that can be claimed has no claim, only a `P-critical` issue can be claimed.
- A claimed issue that carries `S-blocked`, `S-needs-info`, `S-on-hold` keeps that label; the claim stays and its
  release clock waits, as it does while the claimant's pull request is open.
- `S-on-hold` also takes the place of the claim's own label: while it is on, the claimed issue loses `S-in-progress` (or
  `S-pending-review`), and when it comes off while the claim stands, that label comes back.
- Release is off until the cutover: a claim is released after 72 hours without activity (`/progress`, a pull request
  opened, reopened, made ready or pushed to by the claimant, `/claim renew`); the claimant is mentioned at 48 hours, and
  a claim is never released sooner than 24 hours after that warning. The stages `/progress` accepts: `assigned`,
  `claimed`, `studied`, `fails-first`, `compared`, `checked`, `reviewed`, `pr-opened`, `fix`, `finished`; a passing
  stage stamp posts it, the same stage again after 12 hours.
- An issue labelled `claim-trial` (a throwaway test issue) has adoption and release on whatever the switches say, with a
  warning at 1 and a release at 2 hours.
- The `claim` check of a pull request wants every issue it closes (`Fixes #N`) held by a claim of its author (a
  maintainer's: by any claim); a problem only warns until the cutover.
- Adoption is off until the cutover: the sweep gives a card (and the deadline) to an issue assigned by hand. The text
  after `scope:` is at most 400 characters, and at most 10 `Fixes #N` lines of one pull request event are acted on.
- The sweep runs on the cron `7 * * * *` (UTC): it repairs cards, answers unhandled commands, releases claims over the
  limit and reports issues in progress with nobody assigned.
- Claim jobs run one at a time for the whole repository; each also handles the commands other issues received in the
  last 30 minutes, so the command of a job GitHub cancelled while it waited is usually answered by the next one, and
  otherwise by the next sweep that runs.
<!-- conventions:end -->

**The commands.** A comment is a command only when its whole body is exactly one of these (so a sentence that
contains `/claim` is never one); the Action deletes each real command after acting on it, so the issue stays readable.

| Comment | What it does | Who |
| --- | --- | --- |
| `/claim` | Takes the issue after the checks below | anyone |
| `/claim scope: src/ai/, 0x00322b78` | The same, saying which folders, files and original addresses the work touches | anyone |
| `/claim scope: ... task: <task id>` | The same for one issue of a task of several issues | anyone |
| `/claim scope: ...` on your own claim | Replaces the scope of your claim (the task cannot change) | the claimant |
| `/claim renew` | Starts the idle clock again | the claimant |
| `/unclaim` | Gives the claim up: unassigns, back to `S-ready-to-start` | the claimant, or a maintainer |
| `/progress <stage>` | Records a stage (`assigned`, `studied`, ...); the tools post it at each stage | the claimant |

The coordinator writes none of these by hand: `task create` posts `/claim`, the stage tools post `/progress`, and
`coney-tools claim unclaim N` and `claim renew N` post the other two and wait for the Action's answer
([coney-tools](coney-tools-issues.md#claim)).

A comment that starts with `/claim`, `/unclaim` or `/progress` but is not exactly one of them gets a reply from the bot
(`<!-- coney-claim-reply:ID -->`, naming the comment) saying what to write, and **stays**: only a comment that was a
real command is deleted, a refused one included. A reply is deleted by the sweep a day after its command comment is
gone. A reply mentions only the person who wrote the command; anyone else it names is written without an `@`. The card
mentions the claimant (its title), and so does the 48-hour warning.

**The checks of a `/claim`**, in this order; the first that fails is the answer.

1. It is an open issue, not a pull request.
2. Nobody holds it: no assignee, no claim card, no `S-in-progress` or `S-pending-review`.
3. It carries `S-ready-to-start` (not `S-needs-triage`, `S-needs-info`, `S-needs-research`, `S-blocked` or `S-on-hold`).
4. While an open `P-critical` issue that can be claimed (ready, unassigned) has no claim, only a `P-critical` issue can
   be claimed; one that is blocked or already held does not stop anyone.
5. Every issue in its "Blocked by" section is done: closed as completed by a merged pull request (a listed pull
   request must be merged). Work is never stacked on an unmerged task.
6. The claimant holds fewer open claims than the limit (3 for a maintainer, 2 for a contributor), and a `task:` id
   holds at most 3 issues. Issues that share a `task:` id count as one claim, so the id is no way round the limit; a
   fix task reuses its claim. A maintainer is whoever the repository gives write permission or more (asked of GitHub by
   login); only if GitHub will not say does the comment's own association decide, and then only the repository's OWNER
   counts.

**Workflow units are outside the limit.** An issue labelled `workflow` (`claims.exempt_label`) builds the new workflow
for the cutover, and its claim neither counts toward the open-claims limit nor is refused by it, as those units are
not counted in the engineer [slots](#slots) either (a maintainer decision, 2026-10-27). The label is the mark because
only someone who may label issues can set it (triage access or more; nothing checks who set it, so the repository
gives triage only to people the maintainer trusts with it), while a task id chosen by whoever claims could be named
anything. The card says `Limit: not counted`, and its hidden JSON carries `exempt`, which every pass sets again from the
label. The cap of a task (3 issues) holds for an exempt claim too.

A label event for `workflow` starts a claim run, so the card usually follows within a minute or two, and at the latest
by the sweep: the jobs run one at a time and GitHub cancels a pending one when another arrives, so labelling several
issues in a burst leaves some cards stale. After labelling several, run the sweep by hand (`gh workflow run claim.yml
--ref main`) or label one issue at a time. A stale card cannot refuse a start: `task start` reads the label of each of
the account's claimed issues itself, as `task create`'s checks do.

Taking the label off makes the claim count from the next pass, and when that puts its claimant over the limit, the next
sweep releases the claimant's youngest claim by the time of its first claim, which may be another unit's live claim, not
the one that lost the label; give a claim back with `claim unclaim` first if that matters.

After it assigns, the Action recounts the claimant's claims and the `P-critical` issues on fresh data, a backstop for a
run started outside the one-at-a-time queue (below). A claim that turns out to be over the limit (the newest of its
owner's tasks) or behind a new `P-critical` issue is released again with a reply. The sweep recounts the limit only and
releases the late claims it finds, with the reply posted after the release; a `P-critical` issue that appears later
never releases a claim that was already held. A claim that failed half way (assigned, no card, because a label call
failed) is finished by the next pass, with its scope and task, instead of being refused as already taken; it is checked
like any claim, as the issue stood before it (so a closed or untriaged issue is still refused).

**The claim card.** The Action posts one comment per claim and keeps editing it: who claimed, when, the task and the
scope, the status (claimed, the stage, `PR #N open`, merged, released, lost), the claimant's other open claims, and the
Expiration. From the cutover GitHub shows the Expiration as a live countdown ("in 3 days", then hours) from the
`<relative-time datetime="...">` element, with the exact UTC deadline as its text (already now on a `claim-trial` issue,
below); until then the card says it is not enforced yet. A hidden JSON block on the card's last line carries the same
fields for the tools (`task create`'s overlap warning, `coney-tools claim list`, the mod); the visible words are never
parsed. The card keeps only what GitHub cannot tell later (the time of the claim, the scope, the task, the last stage
and activity, the ids of handled commands). The claimant, the status, the pull request and the labels are recomputed
from the assignee, the labels and the linked pull requests on every pass, so a card that drifted is repaired, not
trusted.

**Status labels.** A claimed issue has exactly one `S-` label: `S-in-progress`, or `S-pending-review` while the
claimant's pull request is open. The exception is a maintainer's `S-blocked`, `S-needs-info` or `S-on-hold`
(`claims.paused_labels`): it stays on the claimed issue, the claim keeps, and the release clock waits until the label
is taken off (it then starts again from that moment). `S-blocked` and `S-needs-info` sit beside `S-in-progress`;
`S-on-hold` (`claims.hold_labels`) replaces it until the hold comes off. The clock also waits while the claimant's
own pull request is open and ready for review, however long the review takes; a draft, or a maintainer's pull
request, is activity but does not stop the clock. The card says what it waits for and is written once when the wait
starts, not every hour.

**What moves the card.** A command; a pull request with `Fixes #N` (at most 10 such lines of one event are acted on)
that is opened, reopened, made ready for review or pushed to by the claimant or a maintainer (activity; the issue gets
`S-pending-review` and the status `PR #N open`), or merged; someone else's pull request counts for nothing; the issue
closing; and an hourly sweep that rebuilds every claim from GitHub, answers any command comment nobody handled, and
applies the deadline.

**The deadline (from the cutover).** With `claims.release` on, 72 hours without activity (a `/progress`, a pull request
event or push as above, `/claim renew`) releases the claim, and a warning comment mentions the claimant at 48 hours. The
clock does not run while the claim waits (an open pull request of the claimant, a paused label); when that ends the 72
hours start again. Released means unassigned, `S-ready-to-start`, the card marked released. Before a release the Action
reads the card and the comments again and gives up if either changed since the pass began, and if the card changes while
it releases, it puts the assignee and the label back (only if the claimant still holds an active card on the issue, so a
re-claim made meanwhile counts) and starts the pass over. That undo is best effort against writers outside the queue:
it reads the cards and then assigns, and a hand-run `claim run` or `claim sweep` can still act between the two, which
no check closes without a lock. The sweep (or the audit) catches the result on its next pass. With
`claims.adopt` on, the sweep also gives a card ("assigned by hand") to an issue someone assigned in the web page, and
its clock starts. When an issue has two assignees, the next pass leaves one: the earliest claim keeps the issue, a
hand-added second assignee is removed, and a claim that lost a race is marked `lost` and names the winner.

**Never released unwarned.** A claim is released only after its warning, and never sooner than 24 hours (the
deadline less the warning hours) after the warning was posted: the card's deadline is the later of the two. So a claim
the sweep first sees late (GitHub starts scheduled workflows late under load, and on 2026-10-23 to 2026-10-27 the
hourly sweep ran every 2 to 6 hours) still gets its full notice, and when `claims.release` is switched on at the
cutover a claim that was already idle for days is warned first, not released on the spot.

**Trying it before the cutover.** An issue that carries `claim-trial` (`claims.trial`) is treated as if adoption
and the release were on, with a warning after 1 hour and a release after 2, whatever `claims.adopt` and
`claims.release` say. It is for a throwaway test issue only (the label says so): the [live test](#live-test) uses it to
watch the countdown, the warning, the release and the adoption on GitHub without switching them on for everyone.
Taking the label off puts the issue back under the repository's switches on the next pass.

**Work starts with a claim.** The tools hold an engineer to it (`task start` and `pr open` refuse a task whose claim is
not the signed-in account's), and the `claim` job of the `pr` workflow holds every pull request to it: each issue the
description closes (`Fixes #N`) must have a held claim whose claimant is the pull request's author, or the author is a
maintainer, whose pull request may close anyone's claimed issue, as the Action counts it. A bot's pull request is not
checked. Until the cutover (`claims.require_on_pr` false) a problem is a warning annotation on the pull request and the
check passes; from then it fails. A claim made after the pull request opened is seen when the description is edited
or a commit is pushed (or the job is re-run).

**`/progress` from the stamps.** `task create` posts `/progress assigned`, `task start` `claimed` and `pr open`
`pr-opened`; between them, every passing stage stamp (`studied`, `fails-first`, also with no test, `compared`,
`checked`, `reviewed`) posts `/progress <stage>` on the unit's issues, so a claim whose work goes on keeps its clock. A
new stage is posted at once, the same stage again after 12 hours (`claims.progress_refresh_hours`), remembered in
`claim-progress.json` in the task's scratch folder. A stamp log with no unit record (a task not started with `task
start`) posts nothing, and a post that fails is a warning: the stamp stands.

**An issue labelled `S-in-progress` or `S-pending-review` with nobody assigned** (the old way of picking one up) is
neither adopted nor released. The sweep lists it, the audit reports it as `issue.claim-orphan` in the hygiene report,
and the coordinator either claims it with `task create` or sets it back to `S-ready-to-start`.

**One run at a time.** Every claim job of the repository (comments, issue and pull request events, the sweep) waits in
one queue, so two runs of the Action never act on an issue together. Each run handles every unhandled command of its
issue oldest first, so the earliest `/claim` wins. GitHub keeps one pending job in the queue and cancels the older
pending one when a newer one arrives, so each run also handles the commands other issues received in the last 30 minutes
(`claims.catch_up_minutes`). A job takes about 20 seconds (as the hygiene workflow does), so a command is usually
answered within a minute or two, by its own job or the next one. One that no job reaches within 30 minutes is answered
by the next sweep that runs; a sweep waiting in the queue can be cancelled too, which delays that by an hour. Only
commands are caught up: a cancelled issue or pull request event is read back by the next pass of that issue (its pull
requests, assignees and labels), at the latest by the sweep. A comment that is no command, and a label event for any
label but a paused one, `workflow` or `claim-trial`, starts no job. Something outside the queue can still write (a
person editing a card, `claim run` or `claim sweep` started by hand), so the Action keeps a backstop: before its first
change of each step and before every card write it reads the issue's cards again and starts the pass over when one
changed, and two cards of one claimant are merged into the first (its handled commands, stage, scope and latest
activity). `task create` adds a 60-second settle before it trusts a claim ([Task files](#task-files)).

**If the Action cannot delete a comment** (its token may not), the card and the labels are still right and the command's
id is on the card, so it is not acted on again; a maintainer deletes the comment by hand.

`coney-tools claim list [--json]` prints the held claims; `claim run` and `claim sweep` are what the workflow runs,
`claim check-pr` what the `pr` workflow runs, and `claim is-command` the test the user-level no-comment-as-user hook
asks before it lets a comment through ([coney-tools](coney-tools-issues.md#claim)).

### The live test {#live-test}

The claim Action is proven on GitHub, not only in the tests, on a throwaway issue labelled `claim-trial`: a written
plan of steps, each with what the issue, its labels, its card and the Action's log should show, run by the coordinator
(the maintainer for the steps that need a second account), then the issue is closed as not planned and its comments
deleted. The plan and its results are kept in the unit's scratch folder; a change to the claim Action reruns the steps
it touches.

## Task files {#task-files}

The coordinator turns triaged issues into tasks with `coney-tools task create <issue> [<issue>...] --terms ...`
([coney-tools](coney-tools-repo.md#task)). It claims first and writes after, so a task file exists only for a claim that
held:

1. It reads the issues and runs the claim checks above locally, so a claim that cannot succeed fails in a second, with
   the Action's words. A task holds at most 3 issues.
2. It compares the planned scope with every other held claim's and warns about an overlap (a folder holding another
   claim's folder or file, the same address). A warning only.
3. It posts `/claim scope: ... task: <task id>` on each issue and waits for the card that names the claimant (a refusal
   reply ends it with the reason): at most 5 minutes.
4. It **settles for 60 seconds**, looking every 10 seconds that the card still names the claimant, that the claimant is
   the only assignee and that no earlier `/claim` by someone else is waiting; any change stops it with nothing written
   and prints the `/unclaim` hint for the claims it did make.
5. It writes `<scratch>/<task>/TASK.md` and posts `/progress assigned`. If a card has vanished by then it stops cleanly
   with the same hint.

`--resume` skips posting when a held claim of yours is already there (after a timeout, the claim may still have landed);
the task id is the one on the card, and a `--name` that differs is refused. A **fix task** is `--fix-of <issue>`: it
reuses the claim you hold (no `/claim`, no limit) and posts `/progress fix`; its task file names the task of that claim
as `parent`, because the fix works in that task's unit ([Starting a task](#starting)). The kinds are `feature`, `fix`, `docs`,
`tooling` and `ci-fix` (`tasks.kinds`); `ci-fix` repairs main or a nightly run through its `P-critical` issue and is an
ordinary claim.

The task file starts with a small YAML block the tools read (`task`, `issues`, `kind`, `claimant`, `claimed_at`,
`scope`, `terms`, and `parent` in a fix task) and has these sections, in this order: Issues, Goal, Done, Scope, Where
to look, Searched, not related, Expected tests, Decision points, Related PRs and open questions. Goal and Done start as
the issue's Summary and "What done means", quoted line by line (with a leading `>`) so that no line of issue text can
start a section of the file; Expected tests says "no fails-first test" for `docs` and `tooling`; the coordinator prunes
the rest.

**Where to look** comes from a search of prepared indexes with the terms the coordinator chose (`--terms`): the
headings and cited addresses of the research pages, the reimplemented functions of the function database, the
committed Ghidra function list, the `@orig` tags of `src/`, the merged pull requests (titles and files) and, when it
exists, `research/glossary.yaml`. Names are split into parts (`Goal_ThrowObject` is `goal throw object`), a part found
in few entries ranks above a common one, and the exact identifiers, addresses, paths and references written in the
issue are always included, outside the limits. Every match carries the reason it matched, and the terms that matched
nothing go to "Searched, not related". The limits are `tasks.leads`; `coney-tools task leads` runs the same search
without claiming anything.

## Starting a task {#starting}

The engineer assigned a task runs `coney-tools task start <task id>` once ([coney-tools](coney-tools-repo.md#task)); the
coordinator never does (a hook refuses it). It turns the claimed task into a **unit** (the task's branch, worktree,
scratch folder and proof log) and makes everything the later stages look for, so none of them guesses where the work is.

**It checks first** and makes nothing until all of it holds, so a refused start leaves nothing behind:

1. `<scratch>/<task>/TASK.md` exists, is the task's own and belongs to the account that is signed in.
2. Every issue of the task is open and its claim card is held by that account for this task id: a claim the Action has
   released, or one made for another task, is refused, and the coordinator claims again.
3. The account does not hold more open claims than [the limit](#claiming): the Action enforces it when it assigns,
   and this is the backstop. A fix task reuses its claim and is not counted.
4. The engineer slots ([below](#slots)).
5. The task's stamp log (a fix task's: its parent's) can be written to: it is intact and is this task's own. A damaged
   log is refused here, before anything is made, rather than after the branch exists.
6. The branch and the folder are free, and the base is a commit: `<remote>/<default branch>` after
   `git fetch`, or `--base`. A fetch that fails stops the command (`--no-fetch` starts from the ref as it is).

**It makes**, in this order:

| What | Where |
| --- | --- |
| The branch, named like the task id, and its worktree, from the tip of the default branch | The folder `workspace.worktrees` names, in the workspace ([Worktrees](workspace.md#worktrees)); the branch name is the task id, so `CONEY_TASK` is not needed inside the worktree |
| The **unit record**: task, kind, issues, claimant, branch, worktree, the base ref and commit, the time, and whether `/progress claimed` was posted | `<scratch>/<task>/unit.json` (`tasks.start.unit_file`); `task check`, `task review`, `pr open` and the mod read it |
| The stamps `assigned` and `claimed` ([Stamps](stamps.md#log)): `assigned` covers the task file's YAML block (`task_sha256`) and lists the issues, kind, claimant and claim time; `claimed` covers the base commit and names the branch, base ref and kind. Both carry the agent the hook vouched for, so the log says which engineer took the task | `<scratch>/<task>/stamps/` |
| `/progress claimed` on each issue, which restarts the claim's idle clock | the issue's claim card |

Running it again is safe. A task with a unit record is not made twice: the command finishes what a stopped start left
(a missing stamp, a `/progress` that failed) and says `already started`; if the worktree has been removed meanwhile it
refuses and leaves the record. A `/progress` comment that cannot be posted is a warning, not a failure.
`--dry-run` prints the branch, worktree and stamps it would make (for a started task: what it would finish) and
changes nothing, posts nothing and runs the same checks. If the hook did not vouch for who runs the command, the
command warns: its stamps then say `unverified`, which `pr open` refuses, and a rerun cannot rewrite them.

It does not write the `.coney-busy` marker: one with an id in it protects the worktree from `workspace clean` until
someone deletes it, so the engineer writes it only for a stretch where files do not change
([the busy file](workspace.md#cleanup)).

**A fix task** (`kind: fix`, from `task create --fix-of`) makes no branch or worktree. It opens the unit of the task
named by its `parent`: the parent must have a unit, its worktree must still exist and its log must hold a `pr-opened`
stamp (there is a pull request to fix). The fix gets a record of its own that points at the parent's branch and
worktree, and a second `claimed` stamp with `detail.fix` set to the fix's id is appended to the **parent's** log, which
is the one log all the stage commands write (`UnitRecord.log_task` in `coney_tools/unit_record.py`). The claim check
uses the parent's task id, because the claim is the parent's.

When the fix is done, `coney-tools pr open <issue>-fix` releases it onto the parent's pull request ([pr
open](pr.md#open)): the parent's log, text and branch, a push that updates the open pull request in place (a merged or
missing one is refused), and the stamps the fix did not touch still match their hashes, so only the stale ones are
redone. A release posts `/progress pr-opened` on each issue once, which the claim card shows as `PR #N open`.

### Slots {#slots}

A machine runs at most `tasks.start.slots.task` task units and `tasks.start.slots.fix` fix units at a time, and
`task start` refuses a start over the limit and names the busy units while `tasks.start.slots.enforce` is on. The
rule, who counts, what frees a slot, the unit record it counts and the guard that checks the claim and the slot
before an engineer is started are in [Engineer slots](agents.md#slots); `coney-tools task slots` shows who holds what.
A task whose issue has the `workflow` label holds no slot until the cutover; the label is read from the issue's claim
card each time, not copied into the task file. Work in other repositories (Tether, Conclave's own plugin repository)
has no unit here and is not counted.

The open-claims limit follows the slots: the maintainer's limit of 3 stands for the 3 task slots, so a claim stops
counting toward it when the unit's slot frees. The passing `reviewed` stamp posts `/progress reviewed`, and from that
stage on (`claims.limit_ends_at`: `reviewed`, `pr-opened`, `fix`, `finished`), or once the claimant's pull request is
open, the card says `Limit: no longer counted` and keeps `counted: false` for good; the claim itself stays held, with
its card and its deadline, until it is merged or released. So three units waiting for CI never refuse a fourth claim.

## Working from an issue {#agents}

A brief may be just "issue #N" ([Briefing](agents.md#briefs)); the issue holds the rest.

- **Read it, read only.** `gh issue view N --comments` shows the form's answers and the comments. An agent uses `gh`
  for reading and for nothing else.
- **Download attachments** with `curl` into your scratch folder (never into the repository); the link is in the
  issue text. Extract video frames with `ffmpeg` into scratch, for example `ffmpeg -i clip.mp4 -vf fps=2
  scratch/<name>/frame-%03d.png`, and look at them.
- **Describe media, do not commit it.** A research page or a test describes what the screenshot or video showed
  (the place, the heading, what moved) in words and numbers. The media stays on the issue; screenshots and
  recordings are deleted from scratch when done ([Sharing one machine](how-we-work.md#shared-machine)).
- **Work the usual way:** the research first when the status says so, then a failing test, then the fix
  ([Testing](testing.md#done)). The pull request description has a `Fixes #N` line in its Summary.
- **Agents write to GitHub only through `coney-tools issue`.** No comments, labels or pull requests, and an issue only
  with `issue create`, `edit` or `link` ([Filing an issue](#filing)), and only when the brief says to: the
  coordinator does the rest ([How we work](how-we-work.md#coordinator)). An agent
  reports what it would say (the finding, the question for the reporter, the status change) in its message to the
  coordinator. `.claude/hooks/push_guard.py` refuses a subagent's `gh` command that writes; `coney-tools issue` runs
  `gh` itself, so filing stays possible when the brief allows it.
- **HANDOFF lists only issue numbers in flight.** The coordinator's state file names an issue by number and unit,
  for example "#31, bug-doors, in progress"; the title and details stay on the issue, so there is one place to read.

## Issues a pull request changes {#affected}

A pull request names the issue it finishes (`Fixes #N`), and merging closes that one. It also partly fixes other
issues, changes the behaviour an issue describes, or makes an issue's text untrue (a file it cites is renamed, a pull
request it waits for is replaced). Until 2026-10-23 nobody was told: an audit that day found 175 of 311 open issues out
of date, with bodies calling merged pull requests open and issues left `S-in-progress` with no work. So every pull
request lists them, and the list is checked.

- **The section.** The description has an **Issues affected** section after Summary: one line `- #N: how it is
  affected` for each open issue the change partly fixes, changes the behaviour of or makes out of date, for example
  `- #12: step 2 of its fix list is done`. When there is none, the single line `None (<reason>)` with any short reason,
  for example `None (one-line fix)`. An issue the change finishes is `Fixes #N` in Summary and is not listed here.
  `repo check-body` (CI's `body` job, and the release step) refuses a description without the section, an empty one, a
  `None` with no reason and an item with no line on how, and its message says what to write
  ([What CI checks](testing.md#pr-evidence)). Nothing needs Claude Code or `coney-tools` to meet it: a person who
  edits one line on a fork writes `None (one-line fix)`.
- **Finding the issues.** `coney-tools issue impact` suggests them from the diff: open issues whose bodies cite a file,
  folder, function name, `@orig` address or page section the branch touches, or a pull request or branch it replaces
  ([issue impact](coney-tools-issues.md#issue-impact)). Read each candidate before you list it: a match is a reason to
  look, not a verdict. It is a helper, never required to have been run.
- **A pull request opened before the rule** has no such section. The check applies when its description is next
  edited or its branch is next pushed (the `body` job runs on those events); until then the audit's `pr.body` reports
  it, and the author adds the section when the description is next touched.

### After a merge {#after-a-merge}

The coordinator brings each listed issue up to date as soon as the pull request merges, in the same round as the issues
that cite it ([Merging safely](how-we-work.md#merging)):

1. Read the pull request's Issues affected section (`gh pr view N --json body`).
2. For each issue: change what the pull request made untrue (a pull request called open is merged, a renamed file), tick
   the fix-list items it finished, set the status label (`S-in-progress` when work remains, `S-ready-to-start` when
   nobody is on it), and close it if it is now done. Edit the body with `coney-tools issue edit N --body FILE`; a
   label or a comment is `gh`. An issue the line does not change after all gets a one-line note in its body saying
   what the pull request did and why it is unaffected; a comment alone does not count for the audit.
3. Look for what the author missed: `coney-tools issue impact --base <merge base> --head <the pull request's tip>`
   run on the merged branch's commits lists the other issues that cite what changed.
4. The audit's `pr.affected-unedited` reports a pull request merged since the rule began whose listed open issues have
   not been updated since the merge; `/repo-hygiene` fixes those first
   ([The /repo-hygiene skill](repo-conventions.md#hygiene-skill)). The check counts only a body edit or a label
   change by a person after the merge time as the edit; a comment does not count, and neither does a label a bot or
   the workflow token changed ([step 2](#after-a-merge) says what to write in an issue that needs no change).

## Work found along the way becomes its own issue {#follow-up-work}

A pull request merges only when every task in it is finished. Its Checklist lists what it finishes, each item ticked,
and CI's `body` check refuses a description with an unticked `- [ ]` item, the template's `...` placeholder included
(`coney-tools repo check-body`, rules in `pull_requests.checklist` of `.github/conventions.yml`). There is no
"Remaining" list: work the pull request does not finish is not part of it and is not named in it, and a pull request
that finishes an issue says `Fixes #N` in its Summary. Pull requests merged before the rule's own pull request did
(2026-10-12 at 10:39 UTC) keep the Remaining lists they were written with; their items are open issues and go
through the normal queue, and the audit still holds those lists to the older rule (each unchecked item names an
issue as `#N`).

- **The author** finishes the task it was given. Work it finds on the way that is not part of the task (a bug, a gap, an
  open question) is written up as the issue it would be: title, area, research page and "done" test, in the shape of
  the [task form](#report), in the message it sends the coordinator. It follows
  [Writing an issue a newcomer can read](#writing): the problem first, not "the pull request leaves ..."; pages as
  relative links; each checklist item a verifiable step. It starts from `coney-tools issue draft`, saves the body to a
  file in its scratch folder and runs `coney-tools issue check --leftover` on it until it passes
  ([Filing an issue](#filing)); it does not write to GitHub ([Working from an issue](#agents)), and the pull request
  neither lists nor links that work.
- **Work a player can see comes with a picture.** For a visible bug, gap or stand-in, the author saves a capture of
  Coney in its scratch folder's `shots/` (and of the original from PCSX2 when it has one at the same place), names the
  files in the issue text it sends, and the coordinator files the issue with `--attach`. Pure research questions and
  tooling work need none.
- **The coordinator** files the issue from that checked body with `coney-tools issue create --leftover` (and its
  captures, `--attach`), naming where it came from on the `From:` line, and labels it at triage. It rewrites a draft
  that reads like a copy of the pull request, and when a pull request merges it updates the statuses in every issue
  that cites it.
- **A task a pull request cannot finish is split before the pull request is written.** The part it can finish is the
  pull request; the rest is an issue (or a pull request of its own, opened when it can be finished). A task nobody
  will do is dropped, not left unticked.
- **The state file keeps no work list.** HANDOFF holds the live state (what runs where, what waits for CI) and issue
  numbers, never the work itself; work that exists only in HANDOFF is lost when the file is cleared.
