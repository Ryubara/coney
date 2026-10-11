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
- **Task** (`.github/ISSUE_TEMPLATE/task.yml`) is for maintainers and agents. It asks for a summary, the area, the
  research page that covers it, what "done" means (the acceptance test [Testing](testing.md#done) defines for this
  kind of work), a fix list or steps, and the issues it is blocked by (`#12`).

Attach screenshots and short videos by dragging them into the form's "Attachments" box, and attach Coney's log, the
text it prints in the terminal you started it from, as a `.txt` file.

### What may be attached {#media}

Gameplay screenshots and short videos of Coney or of the original may be attached to issues. That is the only
exception to [No game data](repo:LEGAL.md#no-game-data) for issues: never attach asset files, save files, memory
dumps, executables, BIOS images, disc images, extracted files or the game's text or script source. The bug form has a
required checkbox that says so. A maintainer deletes an attachment that breaks the rule.

## Labels {#labels}

The labels live in `.github/labels.yml`, the one source of truth: edit that file, never the labels on GitHub. The
coordinator makes GitHub match it with `coney-tools repo labels --apply`; without `--apply` the command prints the
`gh label create ... --force` commands instead of running them.

| Family | Labels | Meaning |
| --- | --- | --- |
| Type | `bug`, `parity`, `task`, `docs`, `enhancement` | What kind of issue it is. `parity` is a difference from the original; `enhancement` is beyond the original and waits until the game is complete ([Faithful first](repo:AGENTS.md)). |
| Area | `A-combat`, `A-ai`, `A-missions`, `A-hud`, `A-graphics`, `A-audio`, `A-physics`, `A-camera`, `A-tools` | The subsystem. The task form's area list is the same list. |
| Priority | `P-high`, `P-medium`, `P-low` | How soon. Set by the coordinator. |
| Status | `S-needs-triage`, `S-needs-info`, `S-needs-research`, `S-in-progress`, `S-pending-review`, `S-blocked` | Where the issue is in its life; one at a time. |
| GitHub's own | `good first issue`, `help wanted`, `duplicate`, `wontfix` | Kept as GitHub names them. |

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
  ([Testing](testing.md#done)). The pull request description starts with `Fixes #N`.
- **Agents never write to GitHub.** No comments, labels, issues or pull requests: the coordinator does all of it,
  from the same release queue as pull requests ([How we work](how-we-work.md)). An agent reports what it would say
  (the finding, the question for the reporter, the status change) in its message to the coordinator.
- **HANDOFF lists only issue numbers in flight.** The coordinator's state file names an issue by number and track,
  for example "#31, bug-doors, in progress"; the title and details stay on the issue, so there is one place to read.
