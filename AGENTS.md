# Agent instructions

How agents work in this repository. People follow the same rules; [CONTRIBUTING.md](CONTRIBUTING.md) is the human
entry point. Coney reimplements *The Warriors* under a clean-room process, and everything below serves it.

## The clean room

Coney's code is written only from research pages that passed the page gate, never from the original.

- **Never read, quote or ask for the original's code** in any form: its executable, disassembly, bytecode, decompiler
  output, memory dumps, or a procedure derived from them. The game's files and the disc are off-limits too.
- **Facts and behaviour cross; procedures do not.** A page may give addresses, sizes, layouts, constants, ids, formats
  and observed behaviour as outcomes, for example "a knocked-down character gets up after 2.5 s unless hit again".
  It may not give code, pseudocode, step lists in the original's order, branch structure, helper boundaries, loop
  shapes or decompiler names. `coney-tools cleanroom page-check --rules` lists every rule.
- **Any doubt, it's out.** If something you are given or find looks like original code, a disassembly, a decompiler
  window or a description of the original's control flow, stop, don't use it, and tell the maintainers at once. Do
  not summarise or repeat it, not even to report it: say where it is, not what it says.
- **Never edit a failing research section.** A section that fails a check or a review is dropped, and its topic is
  researched again from scratch by someone who has not seen the old text.
- **Write in your own structure.** Engine code takes its behaviour from the pages and its structure, names and control
  flow from Coney's own design.

Each role works in its own zone, and only checked material crosses between zones: gated pages, schema-checked status
and verdict files, and behaviour-only test reports. An agent never reaches into another zone, never runs tools that
read the original from the public side, and never sends free-text messages to an agent in another zone.

## Repository layout

| Path | What it holds |
| --- | --- |
| `python/` | `coney-tools`, the command-line tool: clean-room checks, gates, zones, implementers, CI |
| `research/` | gated research pages (`pages/`) and their signed verdicts (`verdicts/`): the engine's only source |
| `engine/` | the engine, C++23 with CMake and Catch2 (once its first pull request lands) |
| `containers/` | the implementer image: its Dockerfile, session script, system prompt and firewall |
| `mods/` | the zone mod: each zone's pane and its waking |
| `docs/` | documentation, starting with the [clean-room page](docs/clean-room.md); `overrides/` styles the site |
| `.github/` | CI workflows, the repository's conventions, the pull request template and issue forms ([CI](docs/ci.md)) |
| `allowed_signers` | the only keys whose signatures count: implementers' commit keys, reviewers' and PR analysts' verdict keys |
| `AGENTS.md` | these instructions (`CLAUDE.md` imports them) |

## Commands

`coney-tools` uses the Python standard library only (3.11 or later) and runs straight from a checkout. Always run
Python with `-I`, so nothing from the current folder or the environment is imported.

```sh
python -I python/coney-tools.py cleanroom page-check --json page.md   # check a research page
python -I python/coney-tools.py cleanroom page-check --rules          # list the page rules
python -I python/coney-tools.py cleanroom orig-check                  # check the engine's @orig tags
python -I python/coney-tools.py cleanroom commit-sigs main..HEAD      # check engine commits are signed, name a model
python -I python/coney-tools.py cleanroom code-verdict main..HEAD     # check engine changes carry a code verdict
python -I python/coney-tools.py impl status                           # the implementer containers and their tasks
python -I python/coney-tools.py zones check                           # check this account's zone walls
python -I python/coney-tools.py zones ports --list                    # show the port wall's filters
cd python && python -I -B -m unittest discover -s tests               # the tool's tests
python -I python/coney-tools.py ci                                    # every CI job, as CI runs it
```

Exit status of every command: 0 passed, 1 a check failed, 2 the command could not run.

## Rules for changes

- **Enforced, not trusted.** A new rule comes with the check that enforces it: a `coney-tools` command, a hook or a CI
  job. A note alone is not a fix.
- **One source of truth.** Every path, list, threshold and rule lives in one place. No hard-coded paths: read them
  from configuration or derive them from the checkout.
- **Tests use synthetic data only.** Never a fixture taken from the game, from old research or from a real page that
  failed a check.
- **Python:** standard library only in `coney-tools`; `python/pyproject.toml` holds its settings.
- **Markdown:** lines up to 120 characters; `.markdownlint.yaml` holds the rules.
- **No game data** anywhere in the repository, issues, pull requests, logs or test fixtures.

## Commits and pull requests

- Write commit messages as a summary line `area: Verb the rest`, a blank line, then what changed and why. The areas
  are in `.github/conventions.yml` (`engine`, `tools`, `research`, `ci`, `docs`, `agents`); the commit-msg hook and
  the pull request's `title` check enforce the form. The verbs are Add, Fix, Update, Remove, Rebuild, Rework, Improve
  and Document.
- A commit an agent writes names it in one `Co-Authored-By:` trailer with the model's name and the vendor's noreply
  address. No other trailers, no personal email addresses, no session or site links. For engine commits
  `cleanroom commit-sigs` enforces it, and an engine pull request's AI use section names the model it lists.
- Implementers run at the model and effort the machine's workspace pins for them (`containers.model`,
  `containers.effort`); each session's record in `containers\live\` names the models the API served.
- An engine pull request merges only with a code verdict a PR analyst signed (`cleanroom code-verdict`).
- Agents commit locally and run every local check; only a coordinator pushes to GitHub. An implementer container
  pushes its branch to its own outbox, which is how its work reaches the coordinator. Pull requests are
  squash-merged once their checks pass; a branch is brought up to date by merging `main` into it, never by
  rewriting it.
- In issues, pull requests and documentation, "user" is whoever tested, was affected or filed something, and
  "maintainers" are whoever decides.
- Public text describes the process in neutral words: what Coney does, never incidents, dates of problems or what
  was removed.
