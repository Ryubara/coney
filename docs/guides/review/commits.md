# Commit review checklist

For a reviewer of a pull request's title and commits before it is squash-merged. The title rules are
`commits.title` in [`.github/conventions.yml`](repo:.github/conventions.yml), which `coney-tools repo check-title`
and CI read; the areas and verbs are:

<!-- conventions:begin commit-areas -->
| Area | Covers |
| --- | --- |
| `build` | CMake, presets, dependency pins and toolchain setup |
| `ci` | GitHub Actions workflows and pre-commit configuration |
| `core` | engine level only: main loop and timing, memory, chunks, the task engine's plumbing, shared utilities (src/core) |
| `gameplay` | the game's rules a player meets: combat, AI and crew behaviour, movement and physics responses, missions and scripts, objects and pickups, HUD and hint logic, camera behaviour |
| `fileio` | disc and WAD access, file systems and streaming (src/fileio) |
| `graphics` | rendering through librw: models, textures, effects, drawing the HUD (src/graphics) |
| `audio` | sound, music and speech playback; the rules that trigger a sound stay in their gameplay area (src/audio) |
| `platform` | SDL3 and OS glue: windowing, input, timing (src/platform) |
| `tools` | C++ asset tools and the coney_tools Python package |
| `research` | the research pages under docs/research and the research database |
| `agents` | AGENTS.md, the CLAUDE.md and GEMINI.md pointers and other agent instructions |
| `tests` | test-only changes |
| `legal` | LICENSE, LEGAL.md and other licensing or provenance files |
| `docs` | project-wide documentation: README, guides and the docs site |

Verbs: Add, Fix, Update, Remove, Rebuild, Rework, Improve, Document.
<!-- conventions:end -->

Report findings in the [format on the index page](index.md#finding-format), naming the commit title where a file and
line do not apply, and never edit.

Get the messages with `git log --format=%B <base>..<tip>`.

## Checklist, per commit

- [ ] **It is one feature:** it can be titled with one sentence (one verb, no "and"), and without it everything
  else still builds and passes the checks.
- [ ] **The title follows the rules:** `area: Verb the rest`, a known area and verb, 72 characters at most, no
  trailing period, present tense. `git log -1 --format=%s | uv run --project python coney-tools repo check-title -`
  checks it. A title outside the areas is Important.
- [ ] **The title says what changes for someone using or building the project**, not how it was made: no tool or
  agent name.
- [ ] **A breaking change** to a public interface ends the title with `(BREAKING)` and says what breaks in the body.
- [ ] **The body** says what changed for a user or developer and why, wrapped at 72 columns.
- [ ] **The pull request description shows the testing and the result:** the Testing section lists what ran and
  the numbers (for research, the checks and the runtime evidence), and the Screenshots section has before and after
  images for anything visible (attached, never committed) or `Screenshots: none (<reason>)`
  ([What a pull request shows](../testing.md#pr-evidence)).
- [ ] **Agent-written work** ends with `Co-Authored-By: <model name> <its noreply address>` (Claude:
  `Claude Opus 5.5 <noreply@anthropic.com>`). There is **no other email address in the message**, and no personal
  data or machine path.
- [ ] **No game data and no files that should not be tracked** (scratch output, worktrees, local config,
  `HANDOFF.md`) in the diff: list the files with `git show --stat`.
- [ ] **Every commit builds and passes the checks** (`mkdocs build --strict`, the engine build and tests, the
  Python tests). A commit that does not is Critical.

## Checklist, across the branch

- [ ] **The pull request is one feature:** its title is the commit that lands on `main` (the merge is a squash), so
  it follows the title rules, and unrelated changes belong in a separate pull request. Commits inside it may be
  work in progress; they are dropped by the squash.
- [ ] **The pull request title's area is that of the change's main subject:** its tests and docs do not change the
  area. A pull request that really holds two features in two areas is split into two pull requests.

## Severity examples

- **Critical:** a commit that does not build or fails the checks; game data in a commit.
- **Important:** a title outside the areas, or over 72 characters; an email address other than the agent's noreply
  one; a pull request that mixes unrelated features.
- **Minor:** a body wrapped past 72 columns; a body that is vague about why.
