# Reviews

Every change gets a review, and a review is a list of findings written by someone who did not do the work.
This page says how a review runs; the three checklists say what to look at:

| Reviewing | Checklist |
| --- | --- |
| Code: C++, Python, CMake, scripts, tests | [Code](code.md) |
| Research pages and the research database | [Research](research.md) |
| A branch's commits or a pull request | [Commits](commits.md) |

A review of a change that mixes kinds (a research page and the code that uses it) applies each checklist to the files
it covers.

## How a review works

- **One reviewer per change**, who did not write it, plus **a commit review before a branch is pushed or merged**,
  which reads the branch's squashed commits (see [Commits](commits.md)).
- **The reviewer reports and never edits.** It does not fix a finding, not even a typo; the author does, in a
  follow-up commit. A reviewer that fixes things hides what was wrong from the next reader and from the history.
- **The reviewer reads the diff and the files it touches**, not the implementer's summary of them. Run the checks
  the change calls for (`mkdocs build --strict`, tests once they exist) rather than trusting a reported result.
- **Every checklist item is answered**: it passes, or it produces a finding. A review that lists only problems
  cannot be told apart from one that skipped half the list, so the report says which checklists it applied and
  which items did not apply.

## Severity

Every reviewer uses these levels; each checklist ends with examples for its kind of work.

- **Critical:** breaks something or hides a break (a build or test failure, a crash, corrupted data, a gate that
  passes when it should fail, a documented decision contradicted), or breaks the legal rules. Blocks the change.
- **Important:** works, but is wrong in a way a user or the next change will hit, or breaks a standing project rule.
  Blocks the change.
- **Minor:** correct, but improvable. Written down with the review and dealt with later, before the work is
  released.
- **Tie-breaker:** when unsure between two levels, take the higher and say why. A Minor that would cause harm if
  left until later is Important.

## Finding format

One finding per entry, in this form:

```text
**<Severity>** — <file>:<line> — <what is wrong> — <why it matters>
```

For example:

```text
**Important** — docs/research/formats/wad-dir.md:42 — the claim that entries are sorted by offset has no evidence level — a reader cannot tell whether to rely on it
```

- `<file>:<line>` points at the exact place. For a finding about a whole file, use line 1; for a missing file, name
  the path that should exist and line 0; for a commit, name the commit title instead of a file.
- *What* is one sentence a person can check. *Why it matters* names the consequence, which is also what decides the
  severity.
- Order findings by severity, Critical first.
- The report ends with a verdict: no Critical or Important findings (pass), or the count of each (fixes needed).

## Which model reviews what

Review of research, and of any other reverse-engineering work (Ghidra, disassembly, PCSX2 runtime analysis, claims
drawn from the binary), runs on Opus 5.5, as AGENTS.md says. Simple changes (docs, tooling) may be reviewed on a
smaller model.

A research reviewer may consult the original binary to check an address or a claim. A code reviewer never needs to:
code is checked against the research page it cites, and a code reviewer who opens the original code can no longer
review or implement that function under the [clean-room rule](../research-workflow.md#clean-room).
