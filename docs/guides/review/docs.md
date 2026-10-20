# Docs review checklist

For a reviewer of Markdown outside `docs/research/`: the guides in `docs/guides/`, the designs, the README,
CONTRIBUTING, AGENTS.md and the reviews themselves. Research pages go to the [research checklist](research.md).
Docs are simple changes, so they may be reviewed on a smaller model ([index](index.md#which-model-reviews-what)).
Report findings in the [format on the index page](index.md#finding-format) and never edit. The rules are in
[Writing these docs](../writing-docs.md); this page says what to check.

## Checklist

- [ ] **One subject per page, and the subject has one home.** For every rule, command, list or figure the diff adds,
  search the docs (`grep -rn` for a distinctive phrase). If another page already says it, the new copy is a finding:
  it should be a link to that page's heading. A copy that already differs from the original is Important.
- [ ] **Link, do not restate.** Where the diff mentions something another page owns (the checks to run, a legal
  rule, a merge rule), it gives one line and a link, not a second version.
- [ ] **Generated text is untouched.** No edit between `<!-- conventions:begin ... -->` and `<!-- conventions:end -->`
  markers, and none under `docs/references/`, `docs/missions/` or the progress tables, unless the diff also changes
  their source and the render is rerun (`uv run --project python coney-tools repo render --check` passes).
  A hand edit of a generated block is Important.
- [ ] **Links to files outside `docs/` use `repo:<path>`,** and no relative link leaves `docs/`.
- [ ] **No repository or site address** in any file, comment or commit message but the README
  (`paths.address_allowed_in` in `.github/conventions.yml`).
- [ ] **Wording.** Roles, not people: "the user" and "a maintainer", never "the owner"; no person's or account's name
  as the one who decided. No machine path (a drive letter, a home folder); `<workspace>` or a relative path.
- [ ] **True now.** No "still small", "once it exists" or "not yet" for something that exists; a planned thing says
  "planned" in the one place that owns it. Commands and options match `coney --help` and `coney-tools --help`.
- [ ] **The page is where its kind says** ([page kinds](../writing-docs.md#page-kinds)), a new page is in the `nav:`
  of `mkdocs.yml`, and a moved or renamed heading has its inbound links fixed (`grep -rn "<page>.md#<anchor>"`).
- [ ] **A living doc moves with its change.** A change to behaviour, a command or what we know updates its page in
  the same commit; a pull request that changes `src/` or `python/src/` has a change under `docs/` or a `Docs: none`
  line.
- [ ] **No game data.** No file bytes, game text or script source, and no decompiler output
  ([LEGAL.md](repo:LEGAL.md#no-game-data)).
- [ ] **`mkdocs build --strict` passes.** Run it (`.venv/Scripts/python -m mkdocs build --strict`) and
  `uvx pre-commit run --files <changed files>`.

## Severity examples

- **Critical:** game data or pasted decompiler output in a page; a build that fails.
- **Important:** a second copy of a rule that differs from the first; a hand edit of a generated block; a new page
  missing from the nav; an inbound link broken by a rename; a command that does not work as written.
- **Minor:** a restated sentence that still agrees with its source; wording ("owner", a machine path); a page that
  could link a heading instead of the page.
