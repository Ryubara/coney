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

The site opens in the dark palette (`slate`) whatever the reader's system setting; the toggle in the header switches
to light, and Material keeps that choice for the next visit. The order of `theme.palette` in `mkdocs.yml` decides it.

## Diagrams {#diagrams}

A diagram is a Mermaid fence, drawn in the reader's browser with the ELK layout engine. Start it with the front matter
below, so that ELK lays it out, and give it a one-sentence caption under the fence. A diagram illustrates a rule that
the text states; it never replaces the text. Keep one to about fifteen nodes, with short labels (`<br/>` breaks a
line), and put a long label in the text instead.

````markdown
```mermaid
---
config:
  layout: elk
---
flowchart LR
  a["First"] --> b["Second"]
```
````

- **Why a script.** Material for MkDocs draws a diagram with its own copy of Mermaid, which has no ELK.
  `docs/javascripts/mermaid-elk.js` (listed under `extra_javascript` in `mkdocs.yml`) puts a small stand-in where
  Material looks for Mermaid. The stand-in loads a pinned Mermaid and `@mermaid-js/layout-elk` from jsDelivr when
  the first diagram of a page is drawn, so a page without a diagram loads nothing extra. To upgrade, change the two
  pinned versions in that file to releases at least two weeks old and look at every diagram again.
- **Light and dark.** Material colours the diagram from the palette (its `--md-mermaid-*` variables), so the palette
  toggle changes it at once. Do not set colours in the diagram (`classDef`, `style`): they would stay the same in both.
- **Layout.** Nodes in one chain with `flowchart LR` wrap into rows by themselves. Avoid a `subgraph` that arrows enter
  and leave from several sides, and long arrows back to an early node: ELK then reverses the flow or draws long
  detours. Preview with `mkdocs serve` in both palettes before committing.
- **Needs a network.** The diagrams are drawn from the CDN, so a reader who is offline sees the fence's text instead.

## The home page {#home-page}

`docs/index.md` is the site's landing page: a full-height hero over a painting, then the page's own content. The hero's
bottom blurs and fades into the page background, and as you scroll the art drifts at a slower pace than the page, the
fade grows upwards and the text lifts and fades out, while the header takes its colour. The design follows the home page
of Material for MkDocs; the files are Coney's own:

| File | What it holds |
| --- | --- |
| `docs/index.md` | The page's text, and in its front matter (`template: home.html`, `hero:`) the hero's heading, tagline, buttons, art path, crop and credit |
| `overrides/home.html` | The template (`theme.custom_dir` in `mkdocs.yml`): the hero's markup around the page content |
| `docs/stylesheets/home.css` | The look and the effect, every rule scoped to a page that has a hero |
| `docs/javascripts/home.js` | One scroll listener that writes how far the hero has scrolled (0 to 1) into `--coney-p`; the stylesheet does the rest |
| `assets/art/hero.webp`, `hero.jpg` | The artwork, shared with the launcher ([LEGAL.md](repo:LEGAL.md#site-and-launcher-artwork)) |

- **The art.** Replace `assets/art/hero.webp` and its fallback `assets/art/hero.jpg` with the new picture, same names,
  16:9, about 1920 x 1080 and under 512 KB each (the large-file check, which is never excluded). The art is
  `object-fit: cover`, so a phone shows its middle: set `hero.position` in the front matter (`50% 30%` is x and y) to
  keep the faces in view. A different file name is the `hero.image` front matter line, without the extension.
  `mkdocs_hooks.py` serves the two files from `assets/art/` (the build fails if one is missing), so there is one copy.
- **The buttons.** Each `hero.actions` entry is `page:` (a page's path under `docs/`; a page that is not in the
  navigation fails the build) or `repo: true` (the repository's address, which the build works out, so no file writes
  it). `primary: true` fills the button.
- **The text.** It sits above the fade, in light type, whatever the palette: the art is dark and a scrim darkens the
  top and left of it. The artist's credit in `hero.credit` links to the licence note.
- **Reduced motion.** With `prefers-reduced-motion` nothing moves: the hero is still and keeps the fade at its bottom.
  Only the header's colour still follows the scroll, so that its text stays readable.
- **Not Material's own parallax.** Material's page scrolls inside a 3D-transformed container; this one scrolls the
  page, so the header, search, the back-to-top button, instant navigation and anchors behave as on every other page.

## Page kinds

Each kind of document has one place and one set of rules. Every page on this site is a living document, one per
subject, updated in place, so a reader never has to work out which of two pages is current.

| Kind | Where | Rules |
| --- | --- | --- |
| Research | `docs/research/` | Living; one page per subsystem or format; evidence levels; a map from each described behaviour to the original function that implements it ([the Subsystem page](#subsystem-page)) |
| Progress | `docs/progress/` | The tables at the `progress:generated` marker are produced by the docs build, never committed; edit the data files beside the page instead |
| Game references: lists | `docs/references/` | Generated from `research/references/` by `coney-tools refs render`; edit the YAML instead. `entities.md` is written by hand |
| Game references: script bindings | `docs/references/bindings/` | Generated from `research/bindings/` by `coney-tools natives render`; edit the YAML instead |
| Guides | `docs/guides/` | Living; how to build, contribute, research and review |
| Designs | `docs/guides/` | A plan for work that is not allowed or not started yet, with the questions only a maintainer can answer ([enhancements](enhancements.md), [realtime lighting](realtime-lighting.md), [asset and script architecture](asset-and-script-architecture.md)). Not a how-to: say at the top that it is a design, and move what is built into a guide or research page |
| Missions | `docs/missions/` | Generated from `research/missions.yaml` by `coney-tools missions render`; edit the YAML instead |

## Touch a page, fix the page {#touch-it}

Whoever changes a page brings the **whole page** up to this standard in the same pull request: its kind's rules
([Page kinds](#page-kinds)), the [Style](#style) rules and, for a research page, the [Subsystem page](#subsystem-page)
skeleton and the [evidence levels](research-workflow.md#evidence-levels). There is no cleanup pass and no agent for
one. A page nobody touches is left as it is until someone does, so the pages that matter, the ones people work in,
reach the standard first. A program holds the line ([Checking a page](#checking)): it checks every page a change
touches and fails on any rule the page breaks, however old the line that breaks it.

Fixing a page means the page, not the lines you wrote: add the missing skeleton sections (a section with nothing known
says so in one line), grade the claims, reword what the Style rules refuse. A page that has grown past the length
warning is split along a sub-subject that has its own page, in the same pull request or in one that follows at once.

## Adding a page

1. Put it where its kind says (above). Before creating a research page, check that no existing page covers the
   subject; if one does, extend it.
2. Add it to `nav:` in `mkdocs.yml`. The strict build fails on a page that is missing from the navigation
   (`mkdocs.yml` sets `validation: nav: omitted_files: warn`, and `--strict` turns that warning into a failure).
3. Link files outside `docs/` (such as `LEGAL.md`) as `repo:<file>`, e.g. `[LEGAL.md](repo:LEGAL.md#no-game-data)`:
   the strict build rejects relative links that leave `docs/`, and `mkdocs_hooks.py` turns these into links to the
   file on GitHub. Never write the repository's or the site's address into a file, code comment or commit message;
   the README is the one exception. Name roles, not people or accounts: "a maintainer decision", not who made it.
4. On a research page, give every non-obvious claim an address or file offset and an evidence level: confirmed
   (code), confirmed (runtime), inferred or speculative. The levels are defined in
   [Research workflow](research-workflow.md#evidence-levels). Run [the check](#checking) before you commit.

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

A research page is a **map** from what the game does to the original code that does it. Engineers port from the
original function: they read it in Ghidra, so the page does not replace the code and does not try to be implementable
without it. What the page holds is what the code does not say: which function does what and where its logic lives, the
layouts, constants and units, what was observed running, how sure we are, and what is still open. A research page for
an engine subsystem covers six things, in this order, so that the next engineer finds the original function, checks the
claim and writes the tests without working it all out again. Copy the skeleton below and fill it in; a section with
nothing known says so in one line rather than disappearing, so the gap stays visible. File-format pages follow the same
order, with the data section doing most of the work.

Each behaviour the page describes **names the original function that implements it**: its address and its name (the
name from Ghidra, or the name the page gives it when the original has none) and, where the logic is spread over several
functions, the one where it lives. A behaviour with no function named is a finding for the reviewer.

````markdown
# <Subsystem name>

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`); runtime claims with
PCSX2 <version>.

## Purpose

What this part of the game does, in a few sentences a player would recognise, and what other subsystems
depend on it.

## Original structure

The original classes and source files (`c:/Warriors/Source/<Subsystem>/<File>.cpp`), and how they relate.
A table of the important functions: address, name, one-line role, evidence level. This is the map: a reader who wants
the code for a behaviour starts here.

## Data

Every struct the subsystem owns, with field offsets, types and meanings, and the constants it uses.
Little- or big-endian, alignment, and how the data is found (a WAD file name, a global's address).

## Behaviour

What the code does, in prose, step by step where order matters, each behaviour naming the original function
(address and name) where its logic lives. Short pseudocode where it explains better than prose; never a whole function
listing from Ghidra or a disassembly. Every non-obvious claim carries an evidence level.

## Coney's implementation

Where the reimplementation lives (`src/<subsystem>/`), what it does differently from the original
and why (a PC platform, a librw call in place of a PS2 one, a simpler structure).

## Open questions

What is not known yet, what depends on it, and how it could be found out.
````

## Checking a page {#checking}

```sh
uv run --project python coney-tools docs check                      # the pages changed since origin/main
uv run --project python coney-tools docs check docs/research/camera.md docs/guides   # named pages, or a folder's pages
uv run --project python coney-tools docs check --summary docs       # the whole tree: pages that pass, per kind and rule
```

With no paths it checks the Markdown pages your branch changed against `--base` (default `origin/main`), committed or
not. It prints `path:line: rule: message` for each finding and exits 1 when any page has an error; a warning never
fails. [`coney-tools check changed`](coney-tools.md#check) runs it in its `docs` step, and the `mkdocs` job of the
`docs` workflow runs it on the pages a pull request changes, so a pull request cannot merge a touched page that breaks
a rule. It never looks at a page the change did not touch ([Touch a page, fix the page](#touch-it)). Generated pages
(`docs/references/` except `entities.md`, `docs/missions/`, `docs/progress/`) and files outside `docs/` are not
checked; their source is. What each rule looks at, by the kind of page ([Page kinds](#page-kinds)):

| Rule | Pages | What it refuses |
| --- | --- | --- |
| `skeleton` | research | A missing `# Title`; a missing, empty or out-of-order section of the [Subsystem page](#subsystem-page) skeleton. A level-2 heading counts when it is the section's name or starts with it and a space, colon or dash ("Data layout"); other sections may sit between the six |
| `verified` | research | No `Verified against: ...` line (with something after it) above the first section: the executable and, for runtime claims, the PCSX2 version |
| `evidence` | research | An address cited with no evidence level nearby, see below |
| `machine-path` | all | A user folder (under `Users` or `/home`, with a real name) or a drive-letter path two folders deep. Not refused: a placeholder (`<name>`), a bare drive (`H:\`), `C:\Program Files`, and the original's own build paths (`c:/Warriors/...`, `/home/ack/...`) |
| `stale-wording` | all | "still small", "not yet", "once it exists", "does not exist yet", "yet to be written": wording that says when something is true instead of what is true. On a research page "not yet" is refused only where it speaks of Coney (the implementation and open-question sections, or a line that names Coney), since "a store that is not yet broken" describes the original. A phrase in code spans or in double quotes is mentioned, not used |
| `repo-link` | all | A relative link (or image) that leaves `docs/`: write `repo:<path>` |
| `person-wording` | guides and other pages | "the owner", "the project owner", "repo owner" and the other words of `repo.wording.avoid` in `.github/conventions.yml`: write "the user" or "a maintainer". "An object's owner" is not refused |
| `address` | all | The repository's or the docs site's address (only the README may carry it) |
| `length` | research, guides | A **warning** above 1,500 lines (research) or 800 lines (guides). A page is read whole before it is changed, and the reader's window is 2,000 lines; a research page past 1,500 lines (the longest sixth of today's pages; the median is about 530) leaves no room for the code beside it, and a guide past 800 (the longest tenth; the median is about 260) is several guides. A warning, because where to cut is the author's call |

**The evidence rule.** It looks at every paragraph, list item and table row that cites an address (`0x` and eight hex
digits, the form of [Writing up a finding](research-workflow.md#writing-up-a-finding)) in a research page's sections
other than Purpose, Coney's implementation and Open questions, and passes it when a level word (`confirmed`,
`inferred`, `speculative` or `Evidence:`) appears in:

- the item itself, the item before it or the item after it (for a table row: the paragraph before or after the table);
- the heading it sits under, or a heading above that up to its section; or
- the first line under such a heading, when that line is a statement that cites no address ("Everything in this
  section is confirmed (code) unless a line says otherwise").

A table with a column headed Evidence, Evidence level or Confidence needs the level in every row instead. A page whose opening,
above its first section, says in one sentence that every claim is graded ("Every claim is confirmed (code) at the cited
address unless it says otherwise") is passed as a whole. Those two forms, a lead line per section or one sentence per
page, are the economical ways to meet the rule on a long page; the research reviewer checks that they are true. Field
offsets, masks and flag values (`+0xd0`, `0x200000`) are not addresses, and neither is anything in a code block. The
report names the line of the address. A false positive is fixed by writing the level, not by rewording around the
check.
