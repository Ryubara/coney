# Evidence levels and run records

How a research claim says how we know it: the four evidence levels, the spellings `coney-tools docs check` accepts and
the run record that a `confirmed (runtime)` claim cites. The rest of the research workflow is listed in
[its overview](research-workflow.md#pages).

## Evidence levels {#evidence-levels}

Every non-obvious claim on a research page says how we know it. A reader deciding whether to build on a claim needs
to know whether it was read in the code, seen happening, or worked out from clues. There are four levels.

| Level | Meaning | YAML value |
| --- | --- | --- |
| **confirmed (code)** | Read in the original's code, at a cited address | `confirmed-code` |
| **confirmed (runtime)** | Observed in the original game running under PCSX2; the claim cites the [run record](#run-records) of the run (`run <id>`), which holds the PCSX2 version and the method. A legacy claim states them in its own text | `confirmed-runtime` |
| **inferred** | Follows from evidence that points one way, without being read or observed directly | `inferred` |
| **speculative** | A plausible guess, written down so it can be tested | `speculative` |

### How a level is written {#level-forms}

`coney-tools docs check` accepts only these spellings, so a claim graded one way reads the same on every page:

| Level | In prose | In a table cell | Must carry |
| --- | --- | --- | --- |
| confirmed (code) | `confirmed (code)` | `confirmed (code)` | An address (or script locator) in the same block |
| confirmed (runtime) | `confirmed (runtime), run <id>` | `confirmed (runtime), run <id>` | The id of a run record, `research/runs/<id>.toml` |
| inferred | `inferred from <basis>` | `inferred` (the basis in the role text) | What it follows from |
| speculative | `speculative ([q-...](#q-...))` | `speculative` and a question link in the row | A linked open question that says how to test it |

Mixed grades are written out: `confirmed (code); role inferred`. A claim that is not worth a level because nobody needs
it is `not needed: <reason>` (the [Understood measure](research-workflow.md#understood) reads it). Refused:
`confirmed (disc)`, `confirmed (name hash)`, `confirmed (runtime, coney)`, `Confidence:` and "probably" or "likely" in
an Evidence cell. A check against the game's files is **corroboration**, not a level: write `Disc check:` beside the
claim it supports.

A run record, `research/runs/<id>.toml`, holds what a run observed in PCSX2: who ran it, the build, the state it started
from, the recipe or the method and the values read (numbers and addresses only, no dumps). It is written by the tool
that drives PCSX2, so the method cannot be paraphrased away, and the page does not repeat it. The commands that write
one are in [Run records](#run-records). While `research/runs/` does not exist no run id can name a record, so a
page that is not legacy cannot make a runtime claim until the first record is committed (the check names the
command; a claim nobody observed is `speculative`, and a PCSX2 observation is never written as `inferred`), and a
runtime claim of the old form (PCSX2 named, with the method) stands on a page that is listed as legacy. Its method
goes to the page's *How this was checked* section when the page is next restructured.

Where the level sits on the page is the page standard's rule, not a style: in the claim's block, in the Evidence cell of
its table row, or in the lead line under a `###` heading ([Evidence levels on a page](writing-docs.md#evidence-levels)).
The examples:

- **Confirmed (code).** [WARRIORS.DIR](../research/formats/wad-dir.md): the index's constructor at `0x00149160`
  reads the entry count, seeks to `0x10`, allocates `count * 12` bytes and reads the entry table. The claim names
  the address where anyone with the executable can check it.
- **Confirmed (runtime).** [Memory](../research/memory.md#sizes-at-runtime): the `Sector Pool` is 17,217,536 bytes,
  read in PCSX2 over PINE from the pool object the page documents (`0x00760f80`, block size at `+0x0c`) on a retail
  boot. The page is a legacy one and names the emulator version and the moments of the reads; a new claim cites the run
  that made them.
- **Inferred.** [Compiler](../research/compiler.md): the vtables use 8-byte `{delta, fn}` slots, the GCC 2.x
  layout, which points to GCC 2.95.x. The layout is confirmed; the compiler version is a conclusion drawn from it,
  and the page says the exact version is not confirmed.
- **Speculative.** [Recon](../research/overview.md#disc-layout): the custom `IOP.IRX` module is "probably" the
  audio driver. Nothing has been read or observed yet; the guess is recorded so that someone can test it.

A check against the game's files makes a claim more trustworthy but grades nothing by itself.
[WARRIORS.DIR](../research/formats/wad-dir.md) shows it: its layout is confirmed (code), and the line beside it notes
that the layout predicts `16 + 10701 × 12 = 128,428` bytes, the size of `WARRIORS.DIR` on the disc.

A claim that moves up a level (a guess confirmed in the code, say) is updated in place, and the old level goes away.
Some older pages say "Confidence: confirmed from code"; that means confirmed (code), and those pages are brought to the
new wording when they are next edited.

A page also says what it was checked against: the executable (`SLUS_212.15`, see
[Writing up a finding](research-workflow.md#writing-up-a-finding)) and, for runtime claims, the run record that holds
the PCSX2 version.

### Xbox addresses {#xbox-addresses}

Presentation may follow Rockstar's Xbox port where it differs ([Faithful first](repo:AGENTS.md)), so a page can cite
the Xbox executable too. An address there is written with the word Xbox in front, in any letter case: `Xbox
0x000bd3c0`, or ``Xbox `0x000bd3c0` `` with the address in a code span. It is a virtual address in the NTSC-U
`default.xbe` (base `0x00010000`), eight hex digits ([Xbox executable](../research/xbox-executable.md)). A plain
address is always `SLUS_212.15`'s. One word covers a range or a list that follows it (`Xbox 0x000bd3c0-0x000bd400`,
`Xbox 0x000bd3c0, 0x000bd400 and 0x000bd500`): put it before the first address, and write a PS2 address of the same
sentence before the word or in another sentence.

- The same levels apply, written the same way: `confirmed (code)` needs the Xbox address in the block, and
  `docs check` reports an Xbox address with no level as it does a PS2 one.
- `docs check` never looks an Xbox address up in the PS2 listing or sections, and the progress pages never count it
  as a PS2 function.
- `research claims` lists it as a target (`xbox 0x000bd3c0`) whose backing is `Xbox by hand`: `original study`
  reads `SLUS_212.15` only, so the reviewer reads an Xbox claim in a Ghidra project of the XBE.
- A page that cites Xbox addresses names `default.xbe` and its SHA1 on its Verified line, and a page that a
  feature follows says which build it follows: the PS2 for every behaviour, the Xbox port where its presentation
  is taken instead.

### Run records {#run-records}

A `confirmed (runtime)` claim cites the run that observed it: `confirmed (runtime), run r-20261023-grab-reach`. The run
is a file, `research/runs/<id>.toml`, written by the tool that drove PCSX2 and by nothing else, so the method cannot be
paraphrased away on the page and a reviewer can read the numbers the claim rests on. A record holds:

| Field | What it says |
| --- | --- |
| `id`, `date`, `kind`, `tool` | `r-<date>-<name>`; the day (UTC); `recipe`, `trace` or `method`; the command that wrote it |
| `who` | The agent id, type and model, and whether the hook named them or they are `unverified` ([Who](stamps.md#who)) |
| `pcsx2` | The version PCSX2 reported over PINE, and the copy |
| `start` | The state library id the run started from (`unknown` when the engineer did not say), and the patches it needed, with the state's from the library |
| `run` | The recipe or scenario and its arguments, or the method in words |
| `values`, `addresses`, `hashes` | What was read: numbers by name, the addresses of the original the run names, and the SHA-256 of a file the run made (a trace's CSV, which stays in scratch) |

Three commands write one ([`pcsx2` in coney-tools](coney-tools-pcsx2.md#pcsx2)):

- `coney-tools pcsx2 recipes run NAME --record --state ID ...` after a good [recipe](pcsx2-recipes.md) run: the numbers
  the recipe showed and its arguments.
- `coney-tools trace original SCENARIO --run-record --library-state ID ...` with a
  [trace](research-traces.md#recording-a-trace): the scenario, patches, the counts of updates and missed updates, and
  the CSV's hash.
- `coney-tools pcsx2 record-run --method "..." --value NAME=NUMBER ...` for an observation made by hand through the
  `pcsx2` MCP server (a watch, a read, a frame count): the method in words, and the numbers and addresses read.

`recipe run --record` and `record-run` refuse a copy you do not hold a claim on or whose PCSX2 is not running;
`trace original` takes its claim and starts PCSX2 itself. Each fills in the date, who, version and copy itself, checks
every rule of the record it will write before the run (only what the run measures is filled in afterwards), and writes
a record only for a run that went well. A record holds names, numbers and addresses as printable ASCII, within the
limits of `runs` in [.github/conventions.yml](repo:.github/conventions.yml), and a key the schema does not know is
refused. Those limits cap how much a record holds; they do not keep game data out, since the game's text is English
and numbers can carry bytes. So the reviewer reads the method, the arguments and the value names, and refuses anything
that is not our own words or a measurement ([LEGAL.md](repo:LEGAL.md#no-game-data)). A record is never overwritten: a
second run of the same name on the same day takes another name. It carries a digest of its fields, so a hand edit of
a value or the who shows when it is read; that catches an edit, not someone who writes a whole record with its digest,
which is why the reviewer reads the values against the claim. A run is not a stamp: the record in the pull request is
the proof, with no `observed` step in [the stamp log](stamps.md) (maintainer decision, 2026-10-23).

Commit the record with the page that cites it. `coney-tools research claims`
([research](coney-tools-fidelity.md#research)) lists every graded block a branch changed and fails on a runtime claim
whose record is missing or breaks a rule; the reviewer checks that the record's values support the claim's number and
scenario. A runtime claim written before run records keeps its level when a change only touches it, with a warning; a
new runtime claim needs a record.
