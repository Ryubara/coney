# Contributing to Coney

Thank you for helping. People and AI agents follow the same rules here, and no AI tooling is required to
contribute. If you only read one other file first, read [LEGAL.md](LEGAL.md).

## Before you start

- Read [LEGAL.md](LEGAL.md). The short version: no game data anywhere in the project, and the clean room is
  never broken.
- The clean room splits the work in two. Analysts may read decompiler output and disassembly of the original game,
  and write what they learn into `docs/research/`. Anyone writing engine code works from `docs/research/` only and
  never copies or paraphrases decompiler output. See [LEGAL.md](LEGAL.md#clean-room).
- Research lives in `docs/research/`. If the page you need does not exist or is wrong, fixing the research is a
  contribution in its own right. Each claim carries an evidence level and, where it applies, an address.

## Setting up

Clone the repository. To build and test the engine, follow [docs/guides/building.md](docs/guides/building.md).

CI checks every pull request's title against the rules under [Commits](#commits), because the squash merge makes it the
title of the one commit on `main`; the titles of the commits on the branch are not checked. To check a title yourself,
with [uv](https://docs.astral.sh/uv/) installed:

```sh
echo "docs: Add a page" | uv run --project python coney-tools repo check-title -
```

CI also refuses a pull request that changes `src/` or `python/src/` without changing `docs/` or `research/`; put a
line `Docs: none` in the description when the change needs no documentation. To check it yourself:

```sh
git diff --name-only main... | uv run --project python coney-tools repo check-docs
```

CI also checks the pull request description against [the template](.github/pull_request_template.md), including one
created with `gh pr create --body-file`, and reruns when you edit it. These rules are the `pull_requests` keys of
[.github/conventions.yml](.github/conventions.yml), and the block is generated from them (`coney-tools repo render`):

<!-- conventions:begin pr-rules -->
- Sections, in this order: Summary, Checklist, Testing, Screenshots, AI use.
- Summary and Testing must hold text of their own: a section left as the template has it (its comments, `...`,
  `<reason>`) counts as empty.
- Docs: a change under `src/`, `python/src/` needs a change under `docs/`, `research/` or the line `Docs: none` in the
  description.
- Closing: a line `Fixes #N` in Summary for each issue the pull request finishes, one per line; merging closes it.
- Checklist: each unchecked item under `Remaining:` cites an issue or pull request as `#N`.
- Screenshots: an uploaded image (a path on your machine is refused) or a line `Screenshots: none (<reason>)` that gives
  a reason.
- AI use: after `Commits written by:` one of `a person`, `an AI`, `an AI, edited by a person`, `a person, with AI help`;
  unless it is `a person`, `Model:` names the model (`n/a` is not enough).
- The text names no path on your machine (`scratch/`, `../../`, `C:\`) outside an image reference.
<!-- conventions:end -->

File an issue for what you leave undone ([Issues](docs/guides/issues.md#leftovers)). To check a description yourself:

```sh
uv run --project python coney-tools repo check-body --body description.md
```

To build the documentation:

```sh
py -m venv .venv
.venv/Scripts/pip install -r requirements-docs.txt
.venv/Scripts/python -m mkdocs build --strict
```

On Linux and macOS use `python3 -m venv .venv` and `.venv/bin/` in place of `.venv/Scripts/`. The build must pass
with `--strict`, which rejects broken links and pages missing from the navigation.

Game files, emulators and tool installs live outside the repository, as siblings of the checkout. The layout is
described in [docs/guides/workspace.md](docs/guides/workspace.md).

## Commits

A commit title is:

```text
area: Verb the rest
```

It is at most 72 characters, has no trailing period, and uses the present tense. The areas and verbs are
`commits.title` in [.github/conventions.yml](.github/conventions.yml)
([Repository conventions](docs/guides/repo-conventions.md)), and this block is generated from them
(`coney-tools repo render`):

<!-- conventions:begin commit-areas -->
| Area | Covers |
| --- | --- |
| `build` | CMake, presets, dependency pins and toolchain setup |
| `ci` | GitHub Actions workflows and pre-commit configuration |
| `core` | engine core: memory, chunk system, task engine, utilities (src/core) |
| `fileio` | disc and WAD access, file systems and streaming (src/fileio) |
| `graphics` | rendering through librw: models, textures, effects (src/graphics) |
| `audio` | sound, music and speech playback (src/audio) |
| `platform` | SDL3 and OS glue: windowing, input, timing (src/platform) |
| `tools` | C++ asset tools and the coney_tools Python package |
| `research` | the research pages under docs/research and the research database |
| `agents` | AGENTS.md, the CLAUDE.md and GEMINI.md pointers and other agent instructions |
| `tests` | test-only changes |
| `legal` | LICENSE, LEGAL.md and other licensing or provenance files |
| `docs` | project-wide documentation: README, guides and the docs site |

Verbs: Add, Fix, Update, Remove, Rebuild, Rework, Improve, Document.
<!-- conventions:end -->

Make one commit per logical change: a test goes in the same commit as the code that makes it pass.

The body has an opening paragraph saying what changes and why, then `Changes:` with one bullet per part when there
are several, then `Verified:` with the commands you ran and what they printed. For example:

```text
docs: Add the contributor documents

README, contributing guide, the Contributor Covenant, the security
policy and the pull request template.

Verified: mkdocs build --strict passes
```

```text
legal: Update the provenance rule for commits written by agents

A commit an agent writes names the model in a Co-Authored-By line,
so the history shows which work an agent did.

Changes:
- LEGAL.md: describe the agent trailer
- AGENTS.md: point at the rule

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
```

## Agents' commits

A commit an AI agent wrote names the agent in a trailer: the model's name and the vendor's noreply address, and no
other email address:

```text
Co-Authored-By: <model name> <its noreply address>
Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
```

The person who submits or merges the work answers for it. The full rule is in [LEGAL.md](LEGAL.md#provenance).

## Pull requests

- Every pull request is squash-merged: its commits become one commit on `main`, and the pull request title becomes
  the commit title, so it follows the same `area: Verb the rest` format. A pull request holds one feature, so that
  the commit it becomes also reverts as one. The commits inside it still follow the title rules (CI checks them).
- CI must pass.
- Fill in the pull request template: a summary, a checklist of what the pull request finishes and what is still to
  do, what you tested and the results, before and after screenshots of anything a player can see (attach them to the
  pull request; never commit them), and who wrote the work in the commits (a person, an AI, or both) and with which
  model. Who opened the pull request does not matter there. CI refuses a description that leaves out a section or an answer.
- Keep a pull request to one subject. Unrelated changes belong in separate pull requests.

### Working from a fork

Without write access to the repository, work from a fork:

1. Fork the repository on GitHub, clone your fork, and add the project's repository as a second remote called
   `upstream`.
2. Create a branch from `main` for each change, never work on your fork's `main`: `git switch -c <branch>`.
3. Run the checks above, commit with titles in the `area: Verb the rest` form, and push the branch to your fork.
4. Open a pull request from your branch into the project's `main`; GitHub offers it as a banner on the project's page.
   A maintainer approves CI for a first-time contributor, then the pull request is reviewed and squash-merged.
5. Push further commits to the same branch to update the pull request. Run `git fetch upstream` and rebase on
   `upstream/main` if it conflicts. Leave "Allow edits by maintainers" on, so a maintainer can fix a small thing
   in place.

## Agents

AI agents read [AGENTS.md](AGENTS.md) first; it is the single instruction file for every agent in this repository.
The repository also ships the agent tool configuration (`.mcp.json` for the Ghidra and PCSX2 debugging servers,
`.claude/agents/` for the roles, `.claude/settings.json` for shared permissions); what each holds and how to set
it up: [Setting up an agent tool](docs/guides/agents.md#setup).
