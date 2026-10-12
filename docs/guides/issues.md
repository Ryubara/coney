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
  never "(open)" for a pull request that has landed, and tick the checklist item it finished. A leftover is usually
  filed while its pull request is still open, so that wording goes stale when it merges: describe what the
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
  research page with its section, and to the item numbers of the owner's play-test by their text; a word like "the
  unit", "the stack" or a branch name alone means nothing to a newcomer.

### Screenshots {#screenshots}

Every issue has a **Screenshots** section, as every pull request does. It shows the problem: Coney at the place and
moment it goes wrong and, when there is a capture of it, the original from the same place. When there is nothing to
show (a sound, a research question, tooling), the section holds one line, `Screenshots: none (<reason>)`, such as
`Screenshots: none (sound only)`. The forms ask for it; an issue filed from the command line ends with a
`## Screenshots` section in the same shape.

Drag screenshots and short videos into the form's Screenshots box, and attach Coney's log, the text it prints in the
terminal you started it from, as a `.txt` file in the bug form's Log box. From the command line,
`gh issue create --attach ./shots/f.png` (or `gh issue edit N --attach ...`) uploads the file, and a
`![alt](./shots/f.png)` already in the body is pointed at the upload (gh 2.99 or later). Captures stay in scratch
and are deleted once uploaded.

### Wording {#wording}

Issues and pull requests call the person who play-tests and decides "the user", never "the owner": "From: user
playtest of the preview build, 2026-10-11", "the user says", "the user's screenshot". The word "owner" stays for what
it means in the code and the research (an object's owner) and in GitHub's own fields.

### What may be attached {#media}

Gameplay screenshots and short videos of Coney or of the original may be attached to issues. That is the only
exception to [No game data](repo:LEGAL.md#no-game-data) for issues: never attach asset files, save files, memory
dumps, executables, BIOS images, disc images, extracted files or the game's text or script source. The bug form has a
required checkbox that says so. A maintainer deletes an attachment that breaks the rule.

## Labels {#labels}

The label scheme follows rust-lang/rust's prefixes (`A-` for the area, `P-` for the priority, `S-` for the status),
so contributors who know that project recognise it. The labels live under `labels` in `.github/conventions.yml`
([Repository conventions](repo-conventions.md)), the one source of truth: edit that file, never the labels on
GitHub. The coordinator makes GitHub match it with `coney-tools repo labels --apply`; without `--apply` the command
prints the `gh label create ... --force` commands instead of running them.

The table is generated from `labels.list` (`coney-tools repo render`). `parity` is a difference from the original and
`enhancement` is beyond it, so it waits until the game is complete ([Faithful first](repo:AGENTS.md)). The task form's
area list is the `A-` labels. The coordinator sets the priority, and an issue carries one status label at a time.

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
| | `S-needs-info` | Status: waiting for the reporter |
| | `S-needs-research` | Status: an analyst must study the original first |
| | `S-in-progress` | Status: an agent or person is working on it |
| | `S-pending-review` | Status: a pull request is open |
| | `S-blocked` | Status: waiting for another issue or a decision |
| GitHub's own | `good first issue` | Good for newcomers |
| | `help wanted` | Extra attention is needed |
| | `duplicate` | This issue already exists |
| | `wontfix` | This will not be worked on |
<!-- conventions:end -->

## Triage {#triage}

The coordinator reads each new issue and:

1. sets the type (`bug` or `parity`, or `task`, `docs`, `enhancement`), the area and the priority;
2. asks for what is missing with a comment and sets `S-needs-info`, or sets `S-needs-research` when an analyst must
   study the original first ([Research workflow](research-workflow.md));
3. closes a repeat as `duplicate` (pointing at the first), and a request outside the project as `wontfix`;
4. turns a vague report into a task that an agent can take, with a research page link and an acceptance test.

## The life of an issue {#life}

Every issue has exactly one status label, and the coordinator moves it:

| Status | Set when |
| --- | --- |
| `S-needs-triage` | The form sets it. |
| `S-needs-info` | Triage found the report incomplete and asked the reporter. Back to triage when they answer. |
| `S-needs-research` | The behaviour of the original is not yet written on a research page. |
| `S-in-progress` | An agent or person starts work. |
| `S-pending-review` | The coordinator opens the pull request, with `Fixes #N` in its body. |
| `S-blocked` | The issue waits for another issue (named in the task's "Blocked by") or a decision. |

An issue is closed automatically when its pull request is squash-merged into `main`, because the body says
`Fixes #N` ([Pull requests](repo:CONTRIBUTING.md)). If the problem is still there after the merge, reopen the issue
and set `S-needs-triage`. Close with a reason: completed, not planned (`wontfix`) or duplicate.

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
- **Agents never write to GitHub.** No comments, labels, issues or pull requests: the coordinator does all of it,
  from the same release queue as pull requests ([How we work](how-we-work.md)). An agent reports what it would say
  (the finding, the question for the reporter, the status change) in its message to the coordinator.
- **HANDOFF lists only issue numbers in flight.** The coordinator's state file names an issue by number and track,
  for example "#31, bug-doors, in progress"; the title and details stay on the issue, so there is one place to read.

## Leftovers become issues {#leftovers}

A pull request that leaves work undone does not leave it in its description, which nobody reads after the merge. Every
unchecked item under **Remaining** in the description carries the number of the issue (or the follow-up pull request)
that holds it, such as `- [ ] The heavy bag's combo game (#109)`. CI's `body` check refuses a description with an
unchecked item that has no `#123` in it (`coney-tools repo check-body`, rules in `pull_requests` of
`.github/conventions.yml`). Wrapped lines belong to the item above them; checked items, prose (`None.`), the
template's `...` placeholder and everything under Done are not looked at.

- **The author** finishes what it can, and for each leftover that is real work or an open question writes its title,
  area, research page and "done" test, in the shape of the [task form](#report), into the message it sends the
  coordinator with the pull request text. It follows [Writing an issue a newcomer can read](#writing): the problem
  first, not "the pull request leaves ..."; pages as relative links; each checklist item a verifiable step. It does
  not write to GitHub ([Working from an issue](#agents)).
- **A leftover a player can see comes with a picture.** For a visible bug, gap or stand-in, the author saves a
  capture of Coney in its scratch folder's `shots/` (and of the original from PCSX2 when it has one at the same
  place), names the files in the issue text it sends, and the coordinator files the issue with `--attach`. Pure
  research questions and tooling work need none.
- **The coordinator** files the issue from that text with its captures, labels it at triage, and puts the number into
  the description before the pull request is opened. It rewrites a draft that reads like a copy of the pull request,
  and when the pull request merges it updates the statuses in every issue that cites it. An item that is already
  tracked cites its existing issue; one that is finished is ticked and moved to Done; one nobody will do is dropped,
  not left unchecked.
- **A follow-up pull request** may be cited in place of an issue when it is already open, but the work it carries
  still finishes the issue that describes it, with a `Fixes #N` line in its Summary.
- **The state file keeps no work list.** HANDOFF holds the live state (what runs where, what waits for CI) and issue
  numbers, never the leftovers themselves; a leftover that exists only in HANDOFF is lost when the file is cleared.
