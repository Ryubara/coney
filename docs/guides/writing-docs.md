# Writing these docs

The docs are plain Markdown in `docs/`, built into a website with
[MkDocs Material](https://squidfunk.github.io/mkdocs-material/). Navigation is set in `mkdocs.yml`.

## Setup (once)

```powershell
py -m venv .venv
.venv\Scripts\pip install -r requirements-docs.txt
```

## Preview / build

```powershell
.venv\Scripts\mkdocs serve      # live preview at http://127.0.0.1:8000
.venv\Scripts\mkdocs build      # static site into site/ (strict: broken links fail the build)
```

## Page kinds

Each kind of document has one place and one set of rules. Every page on this site is a living document, one per
subject, updated in place, so a reader never has to work out which of two pages is current.

| Kind | Where | Rules |
| --- | --- | --- |
| Research | `docs/research/` | Living; one page per subsystem or format; evidence levels; implementable without Ghidra |
| Tables | `docs/research/tables/` | Generated from `research/*.yaml` at the repository root; never edited by hand |
| Guides | `docs/guides/` | Living; how to build, contribute, research and review |

## Adding a page

1. Put it where its kind says (above). Before creating a research page, check that no existing page covers the
   subject; if one does, extend it.
2. Add it to `nav:` in `mkdocs.yml`. The strict build fails on a page that is missing from the navigation
   (`mkdocs.yml` sets `validation: nav: omitted_files: warn`, and `--strict` turns that warning into a failure).
3. Link files outside `docs/` (such as `LEGAL.md`) by their GitHub address,
   `repo:<file>`: the strict build rejects relative links that leave `docs/`.
4. On a research page, give every non-obvious claim an address or file offset and an evidence level: confirmed
   (code), confirmed (runtime), inferred or speculative. The levels are defined in
   [Research workflow](research-workflow.md#evidence-levels).

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
