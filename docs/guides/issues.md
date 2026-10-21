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
  repeated is a bug that can get a regression test ([Testing](testing.md#done)).
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
[coney-tools](coney-tools.md#issue).

1. **Draft.** `coney-tools issue draft task` (or `bug`) prints a blank body with the form's `###` headings in its
   order. A comment before each section carries the form's own description, whether the section is required, and
   the values it allows (the Area is one of the `A-` labels; a bug's dropdowns list theirs). The `From:` line and the
   `Screenshots: none (<reason>)` convention are there too. Fill it in; the comments may stay, they are not shown and
   the issue is filed without them.
2. **Check.** `coney-tools issue check --title "..." --body FILE [--leftover] [--online]` says what is wrong, one line
   per problem, and exits 1: headings at the wrong level (`## Screenshots`), with other names, extra, missing or out
   of order; a required section empty or `x`; an Area that is not an option; a docs or research page written as plain
   text instead of `[docs/x.md](../blob/main/docs/x.md)`, or linked to a file or `{#anchor}` that is not on `main`
   (read from `origin/main` in your checkout, no network; `git fetch origin` first); a Screenshots section with
   neither an uploaded image nor the `none (<reason>)` line; a leftover without its `From:` line; "owner" for the
   user; the repository's address. The form is the one the body's headings belong to (a "Bug:" title on a task-form
   body is checked as a task); `--form` overrides it. The bug form's "How often does it happen?" also allows
   `Unknown`. A page that exists only on a branch stays plain text with a note such as
   "not on `main` yet". With `--online`, the words that call `#N` open, merged or closed are compared with
   `gh` ([State the status as it is](#writing)).
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
| Priority | `P-high` | Priority: blocks play or other work |
| | `P-medium` | Priority: next in line |
| | `P-low` | Priority: when there is room |
| Status | `S-needs-triage` | Status: not yet looked at |
| | `S-ready-to-start` | Status: triaged and accepted; waiting for someone to pick it up |
| | `S-needs-info` | Status: waiting for the reporter |
| | `S-needs-research` | Status: the original must be studied first |
| | `S-in-progress` | Status: an agent or person is working on it |
| | `S-pending-review` | Status: a pull request is open |
| | `S-blocked` | Status: waiting for another issue or a decision |
| | `S-conflict` | Status: a pull request conflicts with main (set by CI) |
| | `S-failing` | Status: a pull request fails a required check (set by CI) |
| GitHub's own | `good first issue` | Good for newcomers |
| | `help wanted` | Extra attention is needed |
| | `duplicate` | This issue already exists |
| | `wontfix` | This will not be worked on |
<!-- conventions:end -->

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

Whoever picks up an `S-ready-to-start` issue moves it to `S-in-progress`. The audit counts every status except
`S-needs-triage` as triaged, so an `S-ready-to-start` issue already needs its priority label
([Repository conventions](repo-conventions.md)).

## The life of an issue {#life}

Every open issue has exactly one status label, and the coordinator moves it:

| Status | Set when |
| --- | --- |
| `S-needs-triage` | The form sets it. |
| `S-ready-to-start` | Triage accepted the issue: type, area and priority are set and nothing blocks it. |
| `S-needs-info` | Triage found the report incomplete and asked the reporter. Back to triage when they answer. |
| `S-needs-research` | The behaviour of the original is not yet written on a research page. |
| `S-in-progress` | An agent or person picks up an `S-ready-to-start` issue and starts work. |
| `S-pending-review` | The coordinator opens the pull request, with `Fixes #N` in its body. |
| `S-blocked` | The issue waits for another issue (named in the task's "Blocked by") or a decision. |

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
  coordinator does the rest, from the same release queue as pull requests ([How we work](how-we-work.md)). An agent
  reports what it would say (the finding, the question for the reporter, the status change) in its message to the
  coordinator. `.claude/hooks/push_guard.py` refuses a subagent's `gh` command that writes; `coney-tools issue` runs
  `gh` itself, so filing stays possible when the brief allows it.
- **HANDOFF lists only issue numbers in flight.** The coordinator's state file names an issue by number and track,
  for example "#31, bug-doors, in progress"; the title and details stay on the issue, so there is one place to read.

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
