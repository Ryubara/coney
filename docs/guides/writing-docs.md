# Writing these docs

The docs are plain Markdown in `docs/`, built into a website with
[MkDocs Material](https://squidfunk.github.io/mkdocs-material/). Navigation is set in `mkdocs.yml`.

## Setup (once)

In a virtual environment at the repository root:

```powershell
py -m venv .venv
.venv/Scripts/pip install -r requirements-docs.txt
```

On Linux and macOS use `python3 -m venv .venv` and `.venv/bin/` in place of `.venv/Scripts/`.

## Preview and build

```powershell
.venv/Scripts/python -m mkdocs serve --dev-addr 127.0.0.1:8000   # live preview, reloads as you edit
.venv/Scripts/python -m mkdocs build --strict                    # static site into site/, as CI builds it
```

`--strict` turns broken links and pages missing from the navigation into errors, so run it before every commit that
touches `docs/` or `mkdocs.yml` ([The local gate](testing.md#local-gate)). The Changelog page is generated from
`git log` at build time, so it needs the full history (no shallow clone) and is never committed. This section is the
one home of these commands; the README, [Building and testing](building.md#documentation) and CONTRIBUTING link here.

## Publishing

The site is published on GitHub Pages; its address is in the README (the only file that names it). The `docs`
workflow builds it on every push and pull
request, and a push to `main` also deploys that build to GitHub Pages (the `deploy` job), so the published docs are
always those of `main`. There is nothing to run by hand; a page appears online a minute or two after its commit
reaches `main`. Pull requests and other branches build the site as a check but never publish it.

## Page kinds

Each kind of document has one place and one set of rules. Every page on this site is a living document, one per
subject, updated in place, so a reader never has to work out which of two pages is current.

| Kind | Where | Rules |
| --- | --- | --- |
| Research | `docs/research/` | Living; one page per subsystem or format; evidence levels; implementable without Ghidra |
| Progress | `docs/progress/` | The tables at the `progress:generated` marker are produced by the docs build, never committed; edit the data files beside the page instead |
| Game references: lists | `docs/references/` | Generated from `research/references/` by `coney-tools refs render`; edit the YAML instead. `entities.md` is written by hand |
| Game references: script bindings | `docs/references/bindings/` | Generated from `research/bindings/` by `coney-tools natives render`; edit the YAML instead |
| Guides | `docs/guides/` | Living; how to build, contribute, research and review |
| Designs | `docs/guides/` | A plan for work that is not allowed or not started yet, with the questions only a maintainer can answer ([enhancements](enhancements.md), [realtime lighting](realtime-lighting.md), [asset and script architecture](asset-and-script-architecture.md)). Not a how-to: say at the top that it is a design, and move what is built into a guide or research page |
| Missions | `docs/missions/` | Generated from `research/missions.yaml` by `coney-tools missions render`; edit the YAML instead |

## Adding a page

1. Put it where its kind says (above). Before creating a research page, check that no existing page covers the
   subject; if one does, extend it.
2. Add it to `nav:` in `mkdocs.yml`. The strict build fails on a page that is missing from the navigation
   (`mkdocs.yml` sets `validation: nav: omitted_files: warn`, and `--strict` turns that warning into a failure).
3. Link files outside `docs/` (such as `LEGAL.md`) as `repo:<file>`, e.g. `[LEGAL.md](repo:LEGAL.md#clean-room)`:
   the strict build rejects relative links that leave `docs/`, and `mkdocs_hooks.py` turns these into links to the
   file on GitHub. Never write the repository's or the site's address into a file, code comment or commit message;
   the README is the one exception. Name roles, not people or accounts: "a maintainer decision", not who made it.
4. On a research page, give every non-obvious claim an address or file offset and an evidence level: confirmed
   (code), confirmed (runtime), inferred or speculative. The levels are defined in
   [Research workflow](research-workflow.md#evidence-levels).

## Style

- **One subject per page, said once.** Before writing a rule or a figure, search the docs for it; if a page already
  owns it, link to that page (a heading anchor) and do not restate it. Two copies drift apart.
- **Pages say what and how; code comments say why.** A guide says how to do the thing and links the reasons; do not
  paste a command or a list that another page owns.
- **Generated text is never edited.** A block between `<!-- conventions:begin ... -->` markers, and the pages under
  `docs/references/`, `docs/missions/` and the progress tables, are rebuilt from their source; change the source
  ([Repository conventions](repo-conventions.md#generated-blocks)).
- **Links.** A file outside `docs/` is `repo:<path>` (step 3 above). Link a heading, not just a page, when the
  point is in one section.
- **Name roles, not people.** Write "the user" for the person who tests, reports or directs and "a maintainer" for the
  one who decides; never "the owner" (the `repo.wording` key of `.github/conventions.yml`). Do not give dates or
  names to say who decided something.
- **No machine paths.** Write `<workspace>`, `scratch/` or a relative path, not a drive letter or a home folder.
- **Say what is true now.** No "still small", "once it exists" or "not yet" for something that already exists; a
  thing that is planned says "planned" in the one place that owns it.
- **Review checklist:** [Docs review](review/docs.md) lists what a reviewer looks for.

## Subsystem page

A research page for an engine subsystem covers six things, in this order, so that someone can write their own
implementation from it without Ghidra. Copy the skeleton below and fill it in; a section with nothing known yet
says so in one line rather than disappearing, so the gap stays visible. File-format pages follow the same order,
with the data section doing most of the work.

````markdown
# <Subsystem name>

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`); runtime claims with
PCSX2 <version>.

## Purpose

What this part of the game does, in a few sentences a player would recognise, and what other subsystems
depend on it.

## Original structure

The original classes and source files (`c:/Warriors/Source/<Subsystem>/<File>.cpp`), and how they relate.
A table of the important functions: address, name, one-line role, evidence level.

## Data

Every struct the subsystem owns, with field offsets, types and meanings, and the constants it uses.
Little- or big-endian, alignment, and how the data is found (a WAD file name, a global's address).

## Behaviour

What the code does, in prose, step by step where order matters. Short pseudocode where it explains
better than prose; never a whole function. Every non-obvious claim carries an evidence level.

## Coney's implementation

Where the reimplementation lives (`src/<subsystem>/`), what it does differently from the original
and why (a PC platform, a librw call in place of a PS2 one, a simpler structure).

## Open questions

What is not known yet, what depends on it, and how it could be found out.
````
