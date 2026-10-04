# Commit review checklist

For a reviewer of a branch's commits before it is pushed, or of a pull request. The title rules are in the
"Commits and GitHub" section of [AGENTS.md](repo:AGENTS.md); the areas,
with what each covers, and the verbs are in
[`.github/commit-conventions.json`](repo:.github/commit-conventions.json),
which `coney-tools repo check-title` and CI read. Report findings in the
[format on the index page](index.md#finding-format), naming the commit title where a file and line do not apply,
and never edit.

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
- [ ] **Agent-written work** ends with `Co-Authored-By: <model name> <its noreply address>` (Claude:
  `Claude Opus 5.5 <noreply@anthropic.com>`). There is **no other email address in the message**, and no personal
  data or machine path.
- [ ] **No game data and no files that should not be tracked** (scratch output, worktrees, local config,
  `HANDOFF.md`) in the diff: list the files with `git show --stat`.
- [ ] **Every commit builds and passes the checks** (`mkdocs build --strict`, the engine build and tests, the
  Python tests). A commit that does not is Critical.

## Checklist, across the branch

- [ ] **The branch is squashed into feature-sized commits:** no work-in-progress commits and no fix of a fix; a fix
  to a feature on the same branch is folded into it.
- [ ] **The order is buildable:** no commit depends on a later one.

## Severity examples

- **Critical:** a commit that does not build or fails the checks; game data in a commit.
- **Important:** a title outside the areas, or over 72 characters; an email address other than the agent's noreply
  one; a fix-of-a-fix left unsquashed.
- **Minor:** a body wrapped past 72 columns; a body that is vague about why.
