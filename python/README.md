# coney-tools

Coney's command-line tool. Standard library only (Python 3.11 or later): run it straight from a checkout, with
nothing to install, which is how hooks, CI and the other zones use it.

```sh
python -I python/coney-tools.py <group> <command> ...  # or py -3 -I / python3 -I, whichever you have
```

Exit status: 0 passed, 1 a check failed, 2 the command could not run.

## `cleanroom page-check`

Checks research pages against the page rules: a page carries facts and observed behaviour, never the original's
code or anything shaped by it. The same check runs in the analyst zone, in the sweep's export, at the public-side
import and in CI.

```sh
python -I python/coney-tools.py cleanroom page-check page.md [more.md ...]
python -I python/coney-tools.py cleanroom page-check --json page.md   # per section: id, line, sha256, findings
python -I python/coney-tools.py cleanroom page-check -  < page.md     # one page from standard input
python -I python/coney-tools.py cleanroom page-check --guide how-to.md  # a how-to guide: shell blocks allowed
python -I python/coney-tools.py cleanroom page-check --rules          # the rules
```

Every rule looks at what a reader sees: HTML entities decoded, Markdown escapes, invisible characters (Unicode's
format class and the like), HTML comments, tags and link syntax removed, look-alike letters folded (NFKC plus the
common Cyrillic and Greek confusables), curly quotes straightened; and at whole paragraphs as well as single lines
(table rows are checked one by one). A word that mixes Latin letters with look-alike letters from another script
fails on its own. Every pattern is bounded, so the time a page takes grows in step with its size.

Write facts as tables. A numbered or lettered list, or "first ... then" wording, in a section that names a game
address (`0x00100000`-style RAM addresses, their `0x2...`/`0x8...` mirrors, six-digit `0x1.....` addresses, or
`@orig`) fails as a step list, even when it lists ids or outcomes. A line `0x00123456: 0x3F800000` is read as an
address and its value (a fact); a raw word without `0x` after an address, or three 8-digit hex words in a row, is
read as a dump.

A page is split into sections at its `#` headings (up to three leading spaces; `#` lines inside fenced blocks don't
count; setext headings don't split). Text before the first heading is the section `top`. Each finding names the
rule and the line, never the matched text, and a **failing section's heading and hash are withheld**: its id
becomes `withheld-<n>`, and its heading and `sha256` are left out of `--json`, because a heading is page text too
and the hash of a short section would confirm a guessed text. The check is therefore
safe to run where a failing page must not be shown. Any finding fails the page. A failing section is never edited
until it passes: it is dropped, and its topic is researched again from scratch.

`--guide` is for how-to guides (building, running, conventions) and is refused for any page that is not under
`docs/guides/` of the git checkout it sits in (and for standard input). In guide mode, real top-level fenced blocks
(as written: indented at most three spaces, not inside a quote or a list) tagged `sh`, `bash`, `shell`, `console`,
`zsh`, `powershell`, `pwsh`, `ps1`, `cmd`, `bat` or `batch` are allowed; their
content is still checked for assembly, dumps, decompiler names, C-like code, shapes and banned
wording. A fence-like line that is not a real fence (indented, escaped or written with entities) fails, and the
lines after it are checked as ordinary text.

Pages over 2 MB, empty pages and pages that are not UTF-8 text fail. Exit status 2 means the check could not run
(a file could not be read, or an internal error); it never prints a traceback.

### Entry-point tables

The code gate takes the original addresses a Coney function may name (`@orig`) only from a page's entry-point
table, so `entry-points` checks that table's form. A page has at most one section headed `## Entry points`; it holds
one table with exactly the columns `Address | Job | Outcomes`; each row gives the address as `0x` and 8 lower-case
hex digits in inline code (each address once), the job in plain words, and the heading of another section of the same
page that states the job's outcomes. The job is page text like any other: every text rule applies to it, so it can
never be a name from the original or a decompiler. It is written in Coney's own words, and a Coney function that
reimplements the address repeats it in its `@orig` tag (see `orig-check` below):

```markdown
## Entry points

| Address | Job | Outcomes |
| --- | --- | --- |
| `0x00aa11bb` | open the store | Opening |
```

Tables outside an `## Entry points` section are ordinary text and are not checked this way.

### Section hashes

`--json` gives each section's SHA-256 over its normalised text: the section's lines from its heading line to the
line before the next heading, as written in the file (before any of the cleaning above), with each line's trailing
spaces and tabs removed, runs of blank lines collapsed to one, leading and trailing blank lines dropped, joined with
LF and ending in one LF, encoded as UTF-8. Two pages that differ only in trailing spaces (including Markdown's
two-space line break) hash the same. Verdict files key on these hashes, never on section ids, which can change when
the rules change.

`checker` is the SHA-256 of the rules module with LF line endings, so a verdict records which version of the rules
passed it, the same on Windows and Linux checkouts.

### Images

An image must be a local file with a capture record beside it, named `<image>.capture.toml`:

```toml
source = "tester capture"      # where it came from
captured = "2026-10-14"        # when
by = "coney-tester"            # which account or tool
sha256 = "..."                 # optional; when present it must match the image
```

Images from the web, and images in a page read from standard input, always fail.

## `cleanroom paste-check`

```sh
python -I python/coney-tools.py cleanroom paste-check < text.txt
```

Checks text pasted into a session that must never see the original (the public and tester sessions; a machine hook
runs it on every prompt). Only the page rules that the original's own material sets off apply: `mips`,
`instruction-words` and `decompiler-names`. Scripts, configuration, logs and general code pass, since they are
pasted all the time and are not the original. Each finding prints its line and rule, never the text. Text that is
not UTF-8, or larger than a page may be, does not pass. Exit status: 0 passed, 1 the text must not be pasted, 2 the
command could not run.

## `cleanroom disclosure-check`

Public text describes the process in neutral words: what Coney does, never incidents, dates of problems or what
was removed. Every pull request body and commit message is checked before it is pushed.

```sh
python -I python/coney-tools.py cleanroom disclosure-check body.md [more.txt ...]
gh pr view 12 --json body --jq .body | python -I python/coney-tools.py cleanroom disclosure-check -
python -I python/coney-tools.py cleanroom disclosure-check --commits origin/main..HEAD  # every message in a range
python -I python/coney-tools.py cleanroom disclosure-check --json body.md   # per text: rule and line of each finding
python -I python/coney-tools.py cleanroom disclosure-check --rules          # the rules
```

`--rules` lists the rules (`incident`, `slip`, `removal`, `old-repo`, `problem-date`) and what each refuses; they
live in one table in `coney_tools/cleanroom/disclosure.py`. They aim at wording about something that happened, not
at the process: "the breach watch", "the contamination response", "an exposed session is reset" and "after the
breach is contained" pass, and so do engineering words ("remove an unused option", "fix a memory leak"). Each
paragraph is read as one text, as a reader sees it (entities, escapes, invisible characters, markup and look-alike
letters handled as in page-check), so wording split across lines is caught. Each finding prints its source (file,
`-` or commit id), line and rule, never the text. Text that is not UTF-8, or larger than 1 MB, does not pass.

`pre-commit install` also installs the commit-msg hook, which runs the check with `--commit-msg` on every new
commit message (git's comment lines and the diff below the scissors line are left out). Exit status: 0 passed, 1
the text must be reworded, 2 the command could not run.

## `cleanroom scan`

Looks through folders for material that must not be there: original-game material, exact copies of old files and
near copies of old text. It runs before every major milestone (the starting proof) on both sides.

```sh
python -I python/coney-tools.py cleanroom scan ROOT [ROOT ...] --data DIR [--out report.txt] [--json]
python -I python/coney-tools.py cleanroom scan ROOT --markers m.json --hashes h.txt --shingles s.txt [--allow a.json]
    [--exclude FOLDER ...] [--except PATH ...] [--accept-gap PATH ...] [--list-images] [--jobs N]
    [--file-timeout SECONDS]
python -I python/coney-tools.py cleanroom allow-renew SHA256 [...] --data DIR   # after a human re-review
```

**The scanner holds no patterns.** What it looks for lives in private data files that never enter the repository,
given one by one or as a `--data` folder holding them under their standard names:

| File | Holds |
| --- | --- |
| `markers.json` | `{"markers": [{"name", "kind", "regex", "flags", "min_lines"}]}`. Kind `pattern` counts matches anywhere; kind `listing` counts lines that match (a line format, such as a disassembly or decompilation listing; CRLF and LF lines alike) and needs `min_lines` of them. `flags` takes `IGNORECASE`, `MULTILINE`, `DOTALL`, `VERBOSE`, `ASCII` |
| `old-file-hashes.txt` | SHA-256 of old files, one per line (64 hex digits each; an empty or malformed list stops the scan): an exact copy (200 bytes or more) is a hit |
| `old-shingles.txt` | short hashes of old text's 5-line windows (`scan.shingles`; 16 hex digits each; an empty or malformed list stops the scan): 3 or more shared windows are a near copy |
| `allow.json` (optional) | `{"entries": [{"sha256", "path", "reason", "markers_sha256"}]}`: files reviewed and kept on purpose |

A marker is refused, and the scan stops, unless its regex is ASCII, every repeat has an upper bound of at most
10,000 (no `+`, `*` or `{n,}`, also inside lookarounds and atomic groups), nested repeats multiply to at most 10,000,
it has no backreference or conditional, and no match can be wider than 4,096 bytes. These limits bound the work at
each position of a file by a constant that depends on the marker, so the time a file takes grows in step with its
size; they do not keep that constant small. So each marker is also timed when it is loaded, in a separate process,
over adversarial 64 KB inputs: each of the characters it names (and common ones) repeated, pairs of them alternating,
and its characters in order. A marker that takes over 0.5 s of CPU time on any one input, or that has to be stopped,
is refused by name (exit 2). That is a measurement on those inputs only, not a bound for every input. Large files
are read in chunks that overlap by the widest match, and each match counts once.

An allowed file passes while `markers_sha256` is the current markers file's hash. Once the markers change (or when
it is empty), the file is checked again against the markers, and any hit is reported as "allowed under older
markers: re-check": a hit. Nothing renews an entry on its own: after a re-review, `allow-renew` stamps the current
markers hash on the named entries only. An entry without a path or reason, or a hash listed twice, stops the scan.
The data files themselves are skipped when a root holds them, and listed.

**What is checked.** Every file under the roots, whatever its name: tool folders (`.git`, `.venv`, `node_modules`,
`__pycache__`) are scanned like any other, and so are NTFS alternate data streams and the names of the root folders,
files, folders, alternate data streams and archive members. A file's type comes from its content (signature bytes),
not its extension. Each file is checked as it is and as the text it really holds: UTF-8 (with or without a BOM),
UTF-16 and UTF-32 (LE or BE, with or without a BOM) are decoded; the strings inside JSON and JSONL are checked (keys
too, and JSON kept inside a string, 4 levels deep), and JSON escapes are decoded in text that is not valid JSON.
Binary files, and any file with a zero byte anywhere in it, are also read for UTF-16 text at both byte alignments
(see *Binary data* below). Pattern
markers see each text both as written and with CRLF and CR line ends as LF, so anchors match CRLF lines. The scan
opens what a file holds, down to 4 levels: zip (also after other data, such as an installer or an image), tar, gzip,
bzip2, xz and zlib streams (one after another too), git objects found by their content (loose objects anywhere, also
inside archives), PDF streams (FlateDecode, ASCIIHexDecode, ASCII85Decode and RunLengthDecode, chained in order;
after `stream` and CRLF, LF, CR or a space), compressed PNG text, data after an image's end, zip, gzip, zlib, bzip2
and xz data found by its signature anywhere in a file that is not opened whole (images, video, SQLite, compound,
ar, cpio and cabinet files, unknown data and text), and base64 (24 characters or more) in JSON strings, plain text
and the raw bytes of binary files (standard or URL-safe, letters only too; on one line or wrapped over lines; runs of
hex digits only are not taken as base64). Packed git objects are read with `git cat-file`; standalone pack and bundle
files, any file in `objects/pack` that git itself ignores (no `.idx` beside it, `tmp_pack_*`, `*.pack.bak`), and a
pack or an `objects` folder given as a root are read through a temporary repository. Every `GIT_` variable is
removed, replace refs are ignored and lazy fetching is off. git is run by its absolute path, found on `PATH` only:
never in the current folder, an empty or relative `PATH` entry, or a folder under a scan root. Images and video or
audio get the raw markers; disc images get the hash and the raw markers, and are gaps. SQLite, compound (`.doc`),
ar, cpio and uncompressed cabinet files are not opened as containers: they get the raw bytes and UTF-16 views, and
compressed data found inside them is opened. Files over 256 MB are read in chunks: the hash, the raw markers and the
text views (UTF-16 and UTF-32 text decoded, binary data's UTF-16 text at both alignments); JSON, base64, near copies
and what such a file holds are not checked, so it is a gap unless it is an image or video.

**Binary data** (data with no text encoding found, images, video and audio, disc images, and what chance signatures
inside them give) is not read byte by byte: every check that reads its raw bytes (markers, near copies, JSON, JSON
escapes, base64) reads the text runs it holds instead. A text run is a stretch of printable ASCII,
tab, line ends, vertical tab, form feed and well-formed UTF-8 characters holding at least *N* ASCII text characters in
a row, where *N* is the fewest bytes any marker's match can span, and at most 16 (the shortest stretch base64 is
taken from). The runs are joined as they stood: where the data held bytes that are not text, the view holds one
zero byte, or a zero byte, LF and a zero byte where those bytes (or a shorter run left out) held LF or CR. So a run's
neighbours are bytes that are not text, as in the data, and lines end only where the data's lines did. Each marker
sees only the runs at least as long as its own shortest match; near copies are looked for in runs of 24 bytes or
more (5 lines of 4 characters), whose lines end wherever the data had any line break. Its UTF-16 text is the
stretches of 16-bit little-endian code units of printable ASCII, tab, line ends and Latin-1 letters, at even and at
odd offsets (big-endian text is little-endian one byte on), joined the same way, each alignment a view of its own as
before. Text a file over 256 MB holds is followed
across its chunks. The time binary data takes therefore depends on the shortest marker: on random bytes, about
60 MB/s in memory and 100 MB/s in chunks per worker when every marker spans 6 bytes or more, 8 to 12 MB/s when one
can match 3 bytes (`tests/bench_scan.py` measures it). Text (any encoding found) is read whole,
as it always was.

**Coverage gaps.** Whatever the scan could not check is a gap, and a gap fails the scan like a hit: a missing root, a
root with no file in it, a root that `--exclude` excludes, an unreadable file or folder, a file that raised an error,
an archive that is damaged, encrypted or of a compressed format the standard library cannot open (7z, RAR, zstd, LZ4,
compressed cabinets, compress `.Z`, lzip, lzop, RPM, squashfs, ARJ; also 7z, RAR or zstd data, or zip data without
its end record, found inside a file), a zip end record whose zip does not open, compressed data damaged or cut short
part way (what came out before is always checked; for data found by its signature inside another file it is a gap
only when it used 512 bytes of input or more and its output is not random, since chance bytes fail sooner, or when
it is bzip2 or xz data that used 512 bytes or more without giving any output yet), output over the member limit, a
PDF that is encrypted, a PDF stream with an indirect filter (`/Filter 5 0 R`), a predictor (except on
cross-reference and image streams, where predictors are routine; ordinary PDFs can still give predictor gaps, to
be accepted per path), an LZW or
unknown filter, Flate data that ends early, a dictionary not found within 4 KB before it, or one that does not
decode, a disc image, a file over 256 MB that is not an image or video, the depth or size limits (members over 100
MB, more than 1 GB or 100,000 members from one file, where base64 blobs count bytes only; JSON over 128 MB, JSON
nested too deep), git objects that could not be read, git older than 2.44 (it cannot turn lazy fetching off), a git
call that ran over 30 minutes, a file or git object that took longer than `--file-timeout`, and git history outside
the roots (a worktree's git folder, a common folder or alternates elsewhere; roots and targets are compared as real
paths, so short 8.3 names and long names match).
`--accept-gap PATH` lists the gaps on that file or under that folder as accepted gaps instead (for missing roots
kept as existence checks, or a disc image kept on purpose, say). It is path-wide: every gap on that path is
accepted, whatever its reason, including gaps found later on a file that changes. Images, video, links and notes are
listed but are not gaps.

**Folders.** Folder links (symlinks, junctions) are not followed; each is listed. `--exclude` takes a folder. A value
that is absolute, `.` or `..`, or starts with `./`, `.\`, `../` or `..\`, is a path and skips exactly that folder;
any other value (`.venv`, `game`, `tester\game`) skips every folder whose path ends with those whole components
(`game` never skips `gamex`). Each excluded folder is listed. `--except PATH` reports hits on that file or under that
folder as exceptions, to be classified by hand; they do not count as hits. Relative `--except` and `--accept-gap`
paths are taken from the current folder.

**The report** records the Python version, the scanner's hash (LF line endings), each data file's hash and size,
the roots, the options, hits, exceptions, coverage gaps, accepted gaps, missing roots, excluded and skipped folders,
links not followed, notes (partly checked files), images, disc images and the files the allow list passed. A finding
names the file (with `!member` for what is inside it, `:stream` for an alternate data stream) and the marker's
name. A file's content is never printed. A name hit (`[name]`) prints the path, so the matched name is in the
report as part of that path. Control characters in names are escaped. `--out` writes the report whole or not at all
and prints the summary. Exit status: 0 no hits and no gaps, 1 hits or gaps, 2 the scan could not run (no
traceback).

**Jobs and progress.** The scan uses one worker process per CPU; `--jobs N` sets how many. The main process walks
the folders and applies every rule above (excludes, links, missing roots, unreadable folders); the workers scan the
files, and the packed git objects that `git cat-file` reads, in batches (a file or object of 8 MB or more in a
batch of its own, so no small file waits behind it). Each file or git object is scanned whole
by one worker, so the size and member limits apply per file exactly as with one job, and the findings are put back
in the order the walk reached them: the report is the same whatever the number of jobs, and does not record it. A
file whose worker stops or fails is scanned again on its own in a fresh worker; if that fails too, it is a gap
(`could not be checked`), as an error is with one job. Every 30 seconds a line on standard error gives the files
and bytes scanned, the hits so far and the root being scanned, and, once a file has run over a minute, how many
minutes the slowest file now running has taken: counts and root names only. Ctrl+C stops the workers and the
scan, with no report (exit 2).

**Time limit.** `--file-timeout SECONDS` (default 600) is how long one top-level file, with everything opened from
it, or one packed git object may take. A file that takes longer has its worker stopped, and is a gap
(`could not be checked: took longer than 600 s`); the scan goes on. Stopping one worker stops the others' work in
hand too, so those files are scanned again in fresh workers and none is lost: a run where nothing is stopped finds
the same with any limit. The limit holds with `--jobs 1` too, where the one worker is a separate process;
`--file-timeout 0` sets no limit, and with `--jobs 1` the files are then scanned in the main process. The report
records the limit (`file timeout`), so two reports show whether they were made under the same one.

Each worker holds its own copy of the markers, the hash list, the shingle list and the allow list, sent once when
it starts. With lists of 132,000 hashes and 475,000 shingles that is about 100 MB per worker; a lower `--jobs`
lowers the total.

**Known limits.** The starting proof guards against material left behind by accident, not against deliberate hiding.
These are not checked:

- Raw deflate and lzma-alone data have no signature, so they are not found inside other data.
- Base64 runs under 24 characters are not decoded. Wrapped runs are joined across line breaks, indentation, quotes
  and a comma or `+` between lines, but not across other separators.
- Markers are written and reviewed by the maintainers. The load-time timing is a safety net, not a proof: a
  backtracking marker that is fast on the timing inputs can still pass it.
- Images and video of 256 MB or more get the raw markers only (no UTF-16 views, nothing found inside them).
- UTF-16 text inside images and video is not decoded. A zlib stream right after a PNG `IDAT` chunk header inside
  another file (an icon in a program, say) is image data and is not opened.
- Signatures found inside another file count only with a well-formed header after them (the 7z and RAR header
  CRCs, a zstd frame header, a zip local header with a known method and a printable name), so a program that holds
  them as constants is not a gap.
- A file is classed by its leading signature bytes: data after other data is found by its signature only where
  described above.
- A damaged stream found inside another file that used under 512 bytes of input is checked as far as it goes but is
  not a gap. A zstd frame under 18 bytes and a RAR4 archive with encrypted headers are not recognised inside
  another file.
- Base64 inside images and video is not decoded, and a marker split across two wrapped base64 lines that are not
  joined (see above) can be missed.
- In binary data (see above) a marker match that would exist only across bytes that are not text (a control byte,
  a stray high byte, a zero byte between ASCII letters) is not found, nor one that needs a particular such byte
  beside a run (a class of high bytes, say): the view holds a zero byte there. A match that only text and line ends
  make is found as before, and no match is found that the raw bytes did not give, unless a marker names the zero
  byte itself.
- In binary data, UTF-8 text with fewer than *N* ASCII characters in a row (CJK text, say) is not read, nor
  UTF-16 text outside ASCII and Latin-1. A near copy's 5-line windows must lie in text runs of 24 bytes or more; a
  window may still bridge a line of bytes that are not text between two such runs.
- In a binary file over 256 MB, a text run is followed across chunks, but a stretch of over 1 MB with no byte that is
  never text (only high bytes and short ASCII pieces, say) can lose a run that crosses a chunk end there.

## The page gate

A research page crosses from the analysis zone to the public repository only with a verdict file signed by two
different page reviewers (the [clean-room page](../docs/clean-room.md), section 6). Four commands carry it across, and
all of them verify a page the same way:

- page-check passes with the current rules;
- the page passes the repository's markdownlint ([below](#markdownlint));
- the verdict file is a valid `coney-verdict/1` file in canonical form, and every hash in it matches the page, its
  images and their capture records as they stand now;
- every section and image is `keep` (a page with any `reject` never leaves the analysis zone);
- exactly two signatures verify, from two different `page-reviewer-*` principals with two different keys, against
  the `allowed_signers` file read from `main`.

The verdict records which version of page-check passed the page (`checker_sha256`); a different version is noted,
not failed, but the current rules must pass the page too. Results and messages name page ids, paths and hashes,
never page text: a section is named by its number on the page.

### markdownlint

A signed page can never be reformatted (its hashes would change), so a page must pass the repository's markdownlint
before the first reviewer signs it, and `research/` is linted in the repository like every other Markdown file. The
gate lints the page itself: `research verdict` (both reviewers), `research publish`, `research import` and `cleanroom
verify` all run it.

- **Version:** the `rev:` of the `https://github.com/DavidAnson/markdownlint-cli2` hook in `.pre-commit-config.yaml`
  (a leading `v` dropped). **Rules:** `.markdownlint.yaml`. They are the files the commit hook and CI use.
- **Where they are read:** when coney-tools runs from a checkout, from `main` of that checkout (`--ref` for `cleanroom
  verify`) with `git cat-file`, never from its working tree. An installed copy has no checkout (a reviewer's account
  cannot read the repository): it reads the snapshot the installer copies from `main` into `lint-config/` beside the
  package (`<tools>/lint-config/.markdownlint.yaml` and `.pre-commit-config.yaml`). A missing or unreadable source
  stops the command with exit 2.
- **How it runs:** `npx --yes markdownlint-cli2@<version> --config .markdownlint.yaml <page-id>.md` on a copy of the
  page (LF line ends) in a private temp folder, with that folder as the current folder. The version and rules are
  read once per command. The first run in an account downloads the pinned markdownlint-cli2 from npm (allow a few
  minutes; a run is stopped after 300 s).
- **npx:** found on `PATH` only (`npx.cmd` on Windows, never in the current folder); `--npx` names another. A
  missing npx stops the command with exit 2.
- **Output:** markdownlint's output can carry page text, so each finding is reduced to `line <n>: <MDxxx>/<rule>` and
  nothing else of it is shown. A run that fails without a finding that can be read is exit 2 (could not run), not a
  failed check.

### Verdict files

`verdict.json` sits in the page's folder, next to `page.md`. It is published, so it holds no free text: only ids,
hashes and enums.

```json
{
  "checker_sha256": "<page-check's checker hash when the verdict was made>",
  "images": [
    {"capture_sha256": "...", "path": "img/shot.png", "sha256": "...", "verdict": "keep"}
  ],
  "page": "<page id>",
  "page_sha256": "<SHA-256 of page.md with CRLF and CR turned into LF>",
  "schema": "coney-verdict/1",
  "sections": [
    {"id": "<section id>", "kind": "fact", "sha256": "<section hash>", "verdict": "keep"}
  ]
}
```

A page id is lower-case letters, digits and hyphens, up to 64 characters, starting with a letter or digit. Sections
are listed in page order with page-check's ids and [section hashes](#section-hashes); `kind` is `fact`, `behaviour`
or `reference`. Images are every image the page references, by POSIX path relative to the page's folder, in path
order, with the hash of the image and of its capture record (CRLF read as LF). Every section and image appears once,
and nothing else. The file is written as `json.dumps(doc, indent=2, sort_keys=True, ensure_ascii=False)` plus one LF,
so the same verdict gives the same bytes everywhere; a verdict file in any other form fails. A checkout that turns
LF into CRLF does not break it: CRLF is read as LF before checking.

### Signatures and `allowed_signers`

Signatures are SSH signatures (`ssh-keygen -Y`) in the namespace `coney-verdict`; every sign and verify names it.
A reviewer's signature is `verdict.json.<principal>.sig` beside the verdict. The keys that count are listed in
`allowed_signers` at the repository's root, one per line, limited to one purpose:

```text
page-reviewer-1 namespaces="coney-verdict" ssh-ed25519 AAAA...
pr-analyst-1 namespaces="coney-verdict" ssh-ed25519 AAAA...
impl-1 namespaces="git" ssh-ed25519 AAAA...
```

The file is always read from a git ref (`main` unless `--ref` says otherwise) with `git cat-file`, never from a
working tree, so a change can never add its own key and sign with it. A key listed only for `git` never verifies a
verdict, and only `page-reviewer-*` principals count toward a page: a PR analyst's verdict key does not.

**A page reviewer's key.** Each page reviewer makes one Ed25519 key pair in its own account:

```cmd
ssh-keygen -t ed25519 -f %USERPROFILE%\.ssh\coney-verdict
```

The private key never leaves that account. A pull request adds the public key (`coney-verdict.pub`) to
`allowed_signers` as `page-reviewer-<n> namespaces="coney-verdict" ssh-ed25519 AAAA...`; a maintainer reviews it, and
the key counts once it is on `main`.

ssh-keygen and git are found on `PATH` only (never in the current folder); `--ssh-keygen` names another ssh-keygen.
Either one missing stops the command with exit 2.

### `research verdict`

A page reviewer's verdict on a page in the gate folder (the page's folder is named by its id and holds `page.md`).

```sh
python -I python/coney-tools.py research verdict GATE\page-id --reviewer page-reviewer-1 --key KEY --decisions d.json
python -I python/coney-tools.py research verdict GATE\page-id --countersign --reviewer page-reviewer-2 --key KEY
```

The **first reviewer** gives a decision for every section and image, and nothing else:

```json
{"sections": {"<section id>": {"verdict": "keep", "kind": "fact"}}, "images": {"img/shot.png": "keep"}}
```

A missing or unknown id, or a value outside the enums, is refused. A page that fails page-check or markdownlint
gets no verdict at all. The command signs the verdict (on standard input, so no stray file appears), then writes
`verdict.json` and its signature. The gate folder is add-only: an existing `verdict.json` is never replaced.

The **second reviewer** (`--countersign`) recomputes every hash from the page and its images and signs the existing
verdict only if it matches them exactly, and lints the page again (the rules may have changed since the first
signature). It cannot change a decision: a reviewer who disagrees does not sign. It refuses a verdict it has signed
already, or one that does not have exactly one signature.

Exit status: 0 signed, 1 the page or the verdict failed a check, 2 the command could not run or refused (bad
input, a missing tool or key, a file that exists already).

### `research publish`

Analysis side: sends a page in the gate to the public side.

```sh
python -I python/coney-tools.py research publish PAGE-ID [--workspace W] [--ssh-keygen PATH] [--npx PATH]
```

The page folder is the workspace's `gate` path plus the page id. The page is verified (allowed_signers from `main`
of the workspace's `repo`), then exactly the verified bytes are copied (`page.md`, its images and capture records,
`verdict.json` and the two signatures) to `<from_analyst>/pages/<id>-<first 12 hex of the verdict's SHA-256>/`.
The copy is written to a temporary folder beside it and renamed into place; an existing target is refused. Exit
status: 0 published, 1 the page failed a check, 2 could not run or refused.

### `research import`

Public side: commits a published page.

```sh
python -I python/coney-tools.py research import BUNDLE-FOLDER [--repo R] [--ssh-keygen PATH] [--npx PATH]
```

The bundle is verified again (allowed_signers from `main` of the repository; by default the checkout coney-tools
runs from), and its folder's name must be `<id>-<first 12 hex of the verdict's SHA-256>`. The page is then committed
to a new branch `page/<id>` made from `main`, with git plumbing only (a temporary index): the working tree, the index
and the current branch are never touched. The commit holds `main`'s tree with the page's files replaced:

| Path | Holds |
| --- | --- |
| `research/pages/<id>/page.md` | the page (LF line ends) |
| `research/pages/<id>/<image>` | its images and their `.capture.toml` records, at their paths in the bundle |
| `research/verdicts/<id>.json` | the verdict |
| `research/verdicts/<id>.<principal>.sig` | the two signatures |

Its message is `research: Add the <id> page` (or `research: Update the <id> page` when the page is on `main`
already), a blank line and `Verdict: <verdict SHA-256>`, so the branch's commit is already in the form pull request
titles take. An existing `page/<id>` branch is refused. Exit status: 0 committed, 1 the
bundle failed a check, 2 could not run or refused.

```sh
python -I python/coney-tools.py research import --all [--workspace W] [--repo R] [--ssh-keygen PATH] [--npx PATH]
```

`--all` imports every bundle that [`research status --role public`](#research-status) lists as `new`, from the
workspace's outbox (`<from_analyst>/pages/`), one by one as above, and goes on after a failure. It prints each
result, then `imported <n> of <m> new bundles`. Exit status: 0 every new bundle was imported (or none was new), 1 one
failed a check, 2 the command could not run, or one bundle could not be imported for another reason (a `page/<id>`
branch that exists already, say); the worst result counts.

### `research status`

Each role of the page gate runs as its own session in its own account, and sessions never message each other: each
one polls its own inbox on a timer (`/loop`) and acts on what waits there. This command is that inbox.

```sh
python -I python/coney-tools.py research status --role ROLE [--json] [--workspace W] [--repo R]
```

It is cheap: it looks at which files are there and reads only `verdict.json` (up to 1 MiB), never running page-check,
markdownlint or a signature check, so a state is a hint and the gate commands still decide. It prints ids, states,
counts and verdict hash prefixes, never page text. Page folders named `gate-drill-*` are drills: no inbox lists them.

| Role | Reads | Lists |
| --- | --- | --- |
| `research-coordinator` | the gate, and in the outbox each page's own bundle by name (never listed) | every page in the gate |
| `page-reviewer-1` | the gate only | pages with no verdict |
| `page-reviewer-2` | the gate only | pages waiting for the second signature |
| `public` | the outbox, and the repository's git objects | every bundle in the outbox |

The research coordinator's states, for each gate folder holding `page.md` and each folder whose name is not a page
id (the first that applies):

| State | When | Needs the coordinator |
| --- | --- | --- |
| `invalid-name` | the folder's name is not a page id (whether or not it holds a page) | no |
| `awaiting-reviewer-1` | no `verdict.json` | no |
| `invalid-verdict` | the verdict is not a valid `coney-verdict/1` file in canonical form for this page | yes |
| `rejected` | the verdict marks a section or image `reject` | yes |
| `declined` | a decline note `review.<principal>.txt` is there | yes |
| `awaiting-reviewer-2` | exactly one signature file | no |
| `invalid-signatures` | no signature file, or more than two | yes |
| `ready-to-publish` | two signature files, and the outbox has no `<id>-<first 12 hex of the verdict's SHA-256>` | yes |
| `published` | that bundle is in the outbox | no |

`page-reviewer-1` lists `awaiting-reviewer-1` pages. `page-reviewer-2` lists `awaiting-reviewer-2` pages whose
verdict is valid, rejects nothing and has exactly one signature, not its own, and that have no
`review.page-reviewer-2.txt`. The reviewers never read the outbox or the repository (their accounts cannot), and
take no `--repo`.

`public` gives each bundle `<id>-<12 hex>` in the outbox that holds a `verdict.json` one state, by the SHA-256 of
`research/verdicts/<id>.json` read with `git cat-file` (never the working tree) in the repository (`--repo`, by
default the checkout coney-tools runs from): `merged` when `main` holds a verdict whose hash starts with the 12 hex,
else `imported` when branch `page/<id>` does, else `new`. `new` and `imported` need the public side (import, then
merge). Folders that are not `<id>-<12 hex>`, and publish's temporary `.`-folders, are skipped.

The outbox is `<from_analyst>/pages/`; before the first publish it may not exist yet, which is an empty outbox. A
missing gate or `from_analyst` folder stops the command (exit 2).

The output is one line per page, `<state> <id>` and the verdict's hash prefix where there is one, then the counts:

```text
awaiting-reviewer-1 getting-up
ready-to-publish stamina 0123456789ab
2 pages: 1 awaiting-reviewer-1, 1 ready-to-publish; 1 need research-coordinator
```

`--json` gives `{"role", "pages": [{"id", "state", "verdict"}], "counts": {state: n}, "action": [ids]}`: `verdict` is
the 12 hex or `null`, `counts` names every state of the role, and `action` lists the pages that need this role. Exit
status: 0 nothing needs this role, 1 something does, 2 the command could not run. A polling session runs it on each
tick, acts on the `action` pages when it exits 1, does nothing when it exits 0, and reports an exit 2 to the
maintainers.

### `cleanroom verify`

CI and the pre-push check: every research page in a checkout.

```sh
python -I python/coney-tools.py cleanroom verify [--repo R] [--ref main] [--ssh-keygen PATH] [--npx PATH]
```

For the working tree of the checkout (by default the one coney-tools runs from): every `research/pages/<id>/page.md`
passes as above (allowed_signers, and markdownlint's version and rules, from `--ref`), its folder holds nothing but
the page, its images and their capture records, every file in `research/verdicts/` belongs to a page, and no page
lacks its verdict. It prints one line per page (`PASS <id>` or `FAIL <id>: <why>`) and one per stray verdict file.
Exit status: 0 every page passed, 1 a check failed, 2 could not run.

## The code gate

The public side of the code gate ([clean-room page](../docs/clean-room.md), section 7): every engine commit is signed
by an implementer, and every reimplemented function names an address on a gated page. Both commands read their
settings, `[tool.coney-tools.code-gate]` in `python/pyproject.toml`, and `allowed_signers` from `--ref` (`main`), never
from the change being checked, so a branch cannot move its own code out of the gate's reach:

| Setting | What it is |
| --- | --- |
| `engine_root` | the engine's folder, relative to the repository's root (`engine`) |
| `source_extensions` | the files under it that are source code (`.cpp`, `.hpp` and the like) |
| `game_behaviour` | folders under `engine_root` whose every function must carry a tag |

### `cleanroom orig-check`

```sh
python -I python/coney-tools.py cleanroom orig-check [--repo R] [--ref main] [--ssh-keygen PATH] [--json] [--rules]
```

Reads every source file under the engine root of the checkout's working tree. A function that reimplements part of
the game carries a tag in the comment just above its definition, the page link on the line after it:

```cpp
// @orig 0x00aa11bb open the store
// @page research/pages/store/page.md
bool open_store(Store& store) {
```

The address must be in the Entry points table of the linked page (see [Entry-point tables](#entry-point-tables)), and
the job must be that page's job for the address (compared with spacing and case aside). The page counts only when it
verifies at `--ref`: page-check, every hash in its verdict, every entry `keep`, and two page reviewers' signatures, as
`cleanroom verify` checks them (markdownlint aside, which CI ran before the page merged). So every word of a tag is
gated text: no name from the original or a decompiler can enter one. Every function defined in a `game_behaviour`
folder needs a tag; elsewhere under the engine root a tag is checked when present. `--rules` lists the rules:
`orig-malformed`, `orig-no-page`, `orig-page`, `orig-address`, `orig-job`, `orig-placement` (a tag not just above a
function's definition), `orig-untagged` and `orig-not-text`.

It prints `path:line: rule: message` for each finding, then a summary; `--json` gives the findings, counts, the gated
pages and the pages at `--ref` that do not verify. Output names paths, lines, rules, addresses and page ids, never a
job or a line of code. Exit status: 0 passed, 1 a check failed, 2 could not run (no settings at `--ref`, a missing
tool).

### `cleanroom commit-sigs`

```sh
python -I python/coney-tools.py cleanroom commit-sigs main..HEAD [--repo R] [--ref main] [--ssh-keygen PATH] [--json]
```

For every commit in the range that touches the engine root (against any parent): it carries an SSH signature
(`gpgsig`), made in git's namespace over the commit as git verifies it, by a key that `allowed_signers` at `--ref`
lists with `namespaces="git"` under an implementer's principal, `impl-<n>`. An unsigned commit, a verdict key, a
commit key under another principal, a key without a namespace and a key only the branch adds all fail. Its message
must also name the model that wrote it in exactly one trailer, `Co-Authored-By: Claude <model> <noreply@anthropic.com>`
(AGENTS.md); a missing, second or other co-author fails. It prints `PASS <commit> <principal>, <model>` or
`FAIL <commit>: <why>` per checked commit and counts the commits it skipped. Exit status: 0 every engine commit is
signed by an implementer and names its model, 1 one does not, 2 could not run.

### `cleanroom code-verdict` and `code-verdict-sign`

```sh
python -I python/coney-tools.py cleanroom code-verdict main..HEAD [--repo R] [--ref main] [--ssh-keygen PATH] [--json]
python -I python/coney-tools.py cleanroom code-verdict-sign <name>.json --key KEY --principal pr-analyst-<n> --out DIR
```

A PR analyst's verdict is `research/code-verdicts/<name>.json` (schema `coney-code-verdict/1`: `commit`, `files` as
path and git blob, `functions` as path, `@orig` address and `independent`, `parallel` or `transcribed`, and
`prefilter` `pass` or `flag`; no other field), in `research.canonical` form with `files` sorted by path and
`functions` by path then address. Its signatures sit beside it as `<name>.<principal>.sig`.

`code-verdict` reads git objects only. The range `A..B` starts at the merge base of A and B, so work merged into the
base meanwhile is not counted. Every engine source file the range adds or modifies (deleted files need nothing) must
be listed with its blob at B by a verdict the range adds, and every `@orig` address in it must be judged
`independent` there. Each added verdict must be well formed, canonical, pass (every function independent, the
pre-filter `pass`) and carry at least one signature that verifies in the verdict namespace by a key `allowed_signers`
at `--ref` lists under `pr-analyst-<n>`. A signature by anyone else, one that does not verify, a signature without
its verdict and any other file in the folder fail. Verdicts merged earlier are not checked again. Exit status: 0
passed, 1 a check failed, 2 could not run.

`code-verdict-sign`, on the analysis side, checks the verdict, writes it in canonical form and the principal's
signature into `--out` (add-only). A malformed verdict, or one that does not pass, is refused with exit 1: only a
passing verdict is ever signed.

## `inbox` and `roles check`

```sh
python -I python/coney-tools.py inbox --role page-reviewer-1 [--json] [--workspace W]
python -I python/coney-tools.py roles check [--workspace W]
```

Each role of the pipeline ([how the roles run](../docs/workflow.md)) that runs continuously is woken by its zone's
mod when its inbox changes, and acts on what it lists. `inbox` gathers the role's
items from every station that serves it: the page gate (`research status` for that role) and the code gate; the
queue, the containers and the tester add theirs as they are built. The code gate's stations are `engine-branches`
for the PR analyst (each `<impl-N>/engine/<name>` branch in the collecting outbox, `to-judge` until a handed-over
verdict names its head commit, then `judged`) and `code-verdicts` for the engine coordinator (each signed verdict in
`<from_analyst>/code-verdicts/`, `new` until a local branch holds it at `research/code-verdicts/`, `copied`, then
`merged` once `main` does). It prints ids, states and counts, never page text. A role whose tools are not built yet
is reported as inactive. Exit status: 0 nothing needs this role, 1 something does, 2 the command could not run.

The roles live in the workspace's `roles` table: each role's zone (or `container` for roles the engine coordinator
starts per task), its loop interval in minutes (`null` for container roles) and whether its tools exist yet
(`active`). A role whose own work goes on between inbox items also names `idle_wake_minutes`: its zone's mod wakes it
after that long idle with no subagent at work. A role another role's session dispatches as a subagent names it in
`dispatched_by`: it shares that role's zone, never loops and keeps no notes, and while it is active its stations are
part of the dispatcher's inbox, so one session per zone account wakes for all of them. Each role's brief is
`<roles path>/<role>.md`, installed where every account can read it. `roles check` fails when a role lacks its
brief, has a zone the workspace does not know, a looping role lacks its interval, `idle_wake_minutes` is not a whole
number of minutes (or sits on a container role), an active role has no inbox, or a dispatched role loops, keeps
notes, or names a dispatcher outside its zone or inactive; the installer runs it, so a role is never installed
half-wired. Exit status: 0 every role wired, 1 something missing, 2 the command could not run.

## `tether`

```sh
python -I python/coney-tools.py tether check [--installed] [--workspace W]
python -I python/coney-tools.py tether write --out FOLDER [--workspace W]
python -I python/coney-tools.py tether show ZONE [--workspace W]
```

Tether runs in every zone and in the implementer containers: one build, its behaviour set only by a locked config
per zone. The workspace's `tether` table is the one source: `base` settings every zone shares, an override per zone
(`zones`) and one for the containers (`container`), the zones that may reach GitHub (`github_zones`) and the Tether
`commit` the installer copies. A zone's config is the base with each section merged field by field with its
override, and `locked` always true. `write` renders one `<zone>.json` per zone and `container.json`; the installer
writes them into the machine folder, where no zone account can change them, and each zone's start script names its
file in `TETHER_CONFIG`. `show` prints one.

`check` fails when a zone (or the containers) has no settings, a config has a key Tether does not know, or, outside
the GitHub zones, `network` is not false, `plainOutput` is not true or a `prs` poller is configured; it also fails
when a zone's `mcp_allow` does not list `tether` (Tether's tools are `mcp__tether__*`). With `--installed` it also
checks that the installed Tether is the table's commit and every installed config matches the table. The installer
runs it. Exit status: 0 in order, 1 a problem, 2 the command could not run.

## `impl`

```sh
python -I python/coney-tools.py impl build | up [impl-N...] | down | status | check
python -I python/coney-tools.py impl run impl-N --task task.md --branch <new-branch>
python -I python/coney-tools.py impl collect [impl-N] | reset impl-N
python -I python/coney-tools.py impl log [impl-N] [--tail N] [--json]
```

Drives the implementer containers ([containers/README.md](../containers/README.md)) in the WSL2 distro the workspace
names (`containers.distro`), with `wsl.exe -d <distro> -- docker ...`. The pool, the caps per container and the
distro folder for outboxes, artifacts and transcripts come from the workspace's `containers` table.

- `build` builds the image, with Tether and the containers' locked config from the machine folder (`tether`); it
  refuses when they are not installed. `up` checks the hardening of every service in `compose.yml` and refuses to
  start anything that falls short; on a container's first start it makes its commit key and prints its `allowed_signers`
  line (`impl-N namespaces="git" ssh-ed25519 ...`). `down` stops containers (volumes and keys stay); `reset` removes
  one with all its volumes and its key (its line must then leave `allowed_signers`).
- `run` streams `main` of the collecting repository in, makes a fresh worktree on a new branch, copies the task file
  in read-only and runs Claude Code on it until it ends, at the model and effort the workspace pins
  (`containers.model`, `containers.effort`; every session the same). The Claude login comes from the file
  `containers.auth_file`
  names (`CLAUDE_CODE_OAUTH_TOKEN=...` from `claude setup-token`, or `ANTHROPIC_API_KEY=...`), passed in on
  standard input, never written into the image, a command line or a log; without it `run` refuses. While the
  session works, `run` writes its live log, `containers\live\impl-N.log`: one line per event (a UTC time, a kind
  such as `text`, a tool's name, `result` or `done`, and a one-line summary), with `impl-N.json` beside it (task,
  branch, model, effort, start, end, exit, and `served`: each model id the API answered with and how often, the
  record of which model wrote the work). When the session ends it prints the session's final message.
- `collect` fetches each outbox's branches into the collecting repository `containers\outbox\coney.git` as
  `impl-N/<branch>` and copies artifacts and transcripts to `containers\artifacts\` and `containers\archive\`. The
  collecting repository is a zone sub-folder with its own permissions, so a maintainer creates it; `collect` and
  `run` refuse when it is missing. It also writes each session transcript as Markdown into
  `containers\archive\impl-N\transcripts\`.
- `log` lists every implementer with a live log and prints the last lines of one (the named one, else the one started
  last). Implementers see only public material, so every zone may read `containers\live\` (the machine's
  `zones.json`) and show their logs; the zone mod's pane always has an Implementers section, which says when no log
  exists yet or the account may not read the folder (`readable` in `--json`), and `/coney-impl impl-N` picks whose
  log it shows.
- `status` shows each container's state, firewall, key fingerprint, caps and task. `check` tests running containers
  from inside: no route to any host but the API, none to the Windows host's analyst ports, no Docker socket, no
  capabilities, a seccomp filter, no new user namespaces, a read-only root filesystem.

Exit status: 0 done, 1 a check failed, 2 the command could not run. The live test (it builds the image and starts a
container) runs only with `CONEY_TEST_DOCKER=1`.

## `zones check`

Checks that the walls between Coney's zones stand. Each zone is a separate account with its own root folders; which
account may open which folder is set in a local zones file, never in the repository.

```sh
python -I python/coney-tools.py zones check                     # public mode unless elevated
python -I python/coney-tools.py zones check --mode full --json   # elevated or SYSTEM only
python -I python/coney-tools.py zones check --write-state        # also record the result and any breach
python -I python/coney-tools.py zones check --cache PATH         # also write the JSON result to PATH
```

**The workspace.** Every machine path comes from one local JSON file: `--workspace PATH`, else the `CONEY_WORKSPACE`
environment variable, else `%ProgramData%\Coney\coney.workspace.json`. Its `zones_file` (relative to it) lists the
root folders, each with a `role`; every other path is written `<role>:<path under that root>`. An unknown role, a
path that leaves its root, a missing key or a file that is not JSON stops the command (exit 2).

**Public mode** (the default when not elevated) checks what the current account can reach. Every zone root and
sub-folder is listed and must open, or be refused, as the zones file grants this account (a folder this account may
only add files to is not checked by listing; a missing folder counts as refused); other accounts' home folders and
the maintainer's `.claude` must be refused. No analyst port may give this account a connection on `127.0.0.1` or
`::1`, except for accounts whose zone folder lies in the analyst root. When a port is free, the account listens on it
itself for a moment and connects to its own listener: only the port wall (below) can stop that connection, so the
test proves the wall stands even while no analysis tool runs. When the port is taken, the connection is tried as it
is. The check's `got` names the case and the outcome for each address, such as `own listener: blocked` or
`port in use: refused`; a connection that goes through is a breach. Installed copies of the local config files that
differ from their source are warnings, not failures.

**Full mode** (elevated or SYSTEM; otherwise exit 2) reads every root's and sub-folder's security descriptor and
compares it with what the zones setup writes: owner Administrators, inheritance cut on roots (inherited on
sub-folders), and the explicit entries as a set of account, allow or deny, access mask and inheritance flags. Missing
and extra entries are reported one by one. A root on a drive that is not there is reported, not failed. A root the
maintainer locks by hand is checked only for inheritance and for entries outside its grants. The port wall's filters
are compared with what `zones ports --apply` writes for the workspace: each filter present, persistent and with the
expected accounts, nothing else under Coney's provider, and Coney's sublayer at the highest weight.

**Legacy copies** (full mode only, since only SYSTEM and administrators read the analysis zone). Research from
before the current process reaches the analysis zone only through the research sweep's quarantine folder, so that
the sections the sweep turns down can be researched again by analysts who have not seen them. Full mode therefore
scans the analysis zone for exact copies of older Coney files anywhere else: the clean-room scan (`cleanroom scan`)
with the old-file hash list only, since the analysis zone holds the original game's material by design and markers
and near copies mean nothing there. It opens archives, git packs and encoded data as that scan does. The
workspace's `legacy_scan` table sets it up:

| Field | What it holds |
| --- | --- |
| `root` | the folder scanned, a workspace path (`analyst:`) |
| `hashes` | the old-file hash list, a workspace path (one SHA-256 per line, as `cleanroom scan --hashes` reads it) |
| `cache` | the cache of verdicts, a workspace path in the machine's state folder |
| `skip` | folders not entered, each `{path, why}`: the sweep's quarantine, and large folders that cannot hold older Coney files (the game, emulators, the Ghidra project, tools) |
| `budget_seconds` | how long one run may spend scanning files the cache does not know |

A folder holding a copy (directly, or inside an archive there) is a failed `legacy copy` check naming that folder,
with the number of copies. It is not a breach: no flag is written and tool calls go on, but the result's `legacy`
list names the folders, and the zone status, the zone pane and its status line show them. One `legacy scan` line
always says how many files were scanned and taken from the cache, how long it took and which folders were skipped,
and why. Files whose insides could not all be opened, files left for the next run, unreadable paths and links not
followed are warnings, by count (and by folder for the first). Each file's verdict is cached by its path, size and
modification time, under hashed keys, so the cache names no file; a new hash list or scanner starts it over. A run
stops starting new files once `budget_seconds` are spent and the next run goes on from there, so the scheduled check
stays short after the first runs. A workspace without `legacy_scan`, or a hash list that cannot be read, fails the
check without being a legacy copy. Paths and counts are reported, never file contents.

A failed check that gives an account more than the zones file grants (a folder that opens, a port that gives a
connection, an extra allow entry, a missing owner-rights entry, a wrong owner, inheritance not cut, a missing or
changed block filter, an extra permit filter, Coney's sublayer below the highest weight) is a **breach**. `--json` gives
`{checks: [{what, path, want, got, ok}], warnings, breach, legacy, mode, account, time}`. `--write-state` writes the result
to the workspace's `check_result` and, on a breach, a flag file to `breach_flag` naming the folders; only a clean
full run, elevated, removes the flag. Paths and verdicts are printed, never file contents. Exit status: 0 every
check passed, 1 a check failed, 2 the check could not run.

## `zones ports`

Folder permissions do not cover network ports, and Windows Firewall rules do not filter loopback traffic, so any local
account could otherwise connect to an analysis tool listening on `127.0.0.1`. The port wall closes that with Windows
Filtering Platform filters under Coney's own provider and sublayer.

```sh
python -I python/coney-tools.py zones ports --apply    # elevated: write the filters for the workspace's ports
python -I python/coney-tools.py zones ports --list     # show Coney's filters (listing may need elevation)
python -I python/coney-tools.py zones ports --remove   # elevated: remove the filters, the sublayer, the provider
```

For each analyst port in the workspace:

- **Connections to the port** (the ALE connect layers, IPv4 and IPv6) are blocked for every account except the
  analyst-side ones, the accounts whose zone folder lies in the analyst root. A permit whose user condition grants
  only those accounts' SIDs sits above a block that matches everyone. The engine tests a user condition with an
  access check against its security descriptor, and the condition holds only when access is granted; any other
  account, or a token the check refuses for any reason, falls through to the block.
- **Connections from outside this machine** (the ALE receive/accept layers) are blocked for everyone: only loopback
  traffic reaches the port, so a container network or another host never does.

Coney's sublayer asks for the highest weight, so no permit in another sublayer is evaluated before its block, and a
block cannot be overridden by a later permit. Another product (a VPN client, for example) may already hold that
weight; the engine then stores Coney's one lower. A sublayer above Coney's matters only if one of its filters can
overrule a block: a hard permit (one that clears the action right) or a callout that decides, at one of the wall's
layers and able to match one of its ports. The full check (`zones check --mode full`) and `--list` read every filter
at those layers in sublayers at or above Coney's and judge each one:

- A filter limited to a network interface other than loopback (its local-interface condition names, say, a VPN
  adapter) never sees traffic to `127.0.0.1` or `::1`. It could only let in traffic sent to that interface's own
  addresses, so it is harmless as long as no analyst port listens on such an address. It is named, and passes.
- Any other hard permit or deciding callout that could match an analyst port fails the check: the wall could leak.

The **listener check** closes the remaining case in both modes: every analyst port must listen on a loopback address
only, or not at all (read from `netstat`, which needs no elevation). A tool listening on all interfaces, or on a VPN
or network address, fails it, whatever other products are installed. Analysis tools must therefore be set to listen
on `127.0.0.1` only.

The **unlisted-listener check** (full mode only, since it reads other accounts' processes) makes sure every port
gets the wall: each TCP listener whose process runs as an analyst-side account must be on a port in `analyst_ports`,
or in the workspace's `listener_exceptions` (each with a `port` and a `why`). Any other listener is open to every
account on this machine, so it is a breach, named by port, account and program. A new tool that listens is added to
`analyst_ports` and installed; tools that talk over stdio, as most MCP servers do, open no port and need nothing.

The provider, sublayer and filters are persistent: they survive a
restart. `--apply` removes every filter of Coney's provider and adds the current set in one transaction, so running
it again changes nothing and a failure changes nothing. `--apply` and `--remove` without elevation stop with exit 2;
so does `--list` when listing is refused. Exit status: 0 done, 2 the command could not run.

## `ci`

CI's jobs, run the way GitHub Actions runs them ([Checks and CI](../docs/ci.md)).

```sh
python -I python/coney-tools.py ci                       # every job, in order, then a summary
python -I python/coney-tools.py ci --job tests --job site  # the named jobs
python -I python/coney-tools.py ci --list                # the jobs and what each checks
python -I python/coney-tools.py ci --versions            # the pinned tool versions, as name=value
python -I python/coney-tools.py ci --job disclosure --range main..HEAD --text body.md
```

The jobs are listed once, in `coney_tools/ci.py`. Each workflow job under `.github/workflows/` runs exactly one of
them as `ci --job <name>` under the same name, and the `workflow` job refuses a workflow that runs a job under
another name, leaves a job out, names one that does not exist, lets `build-ok` miss a job of `ci.yml`, pins an
action by anything but a full commit SHA, has no `permissions:` or grants a write scope there, or grants one to a
job that does more than run `actions/deploy-pages` (`pages.yml`'s deploy job, which gets `pages` and `id-token`
and nothing else; `DEPLOY_ACTIONS` and `DEPLOY_WRITES` in `ci.py`). Tool versions are read from the files that
already pin them (`python/pyproject.toml`, `containers/Dockerfile`, `.pre-commit-config.yaml`, the hash-locked
requirements files), never written into a workflow.

- `--repo R` checks another checkout than the one coney-tools runs from; with `--ref`, that is how CI's `verify`
  job runs `main`'s checker on a pull request (`--repo pr --ref origin/main`).
- `--ref` (default `main`): the ref `verify`, `orig-check` and `commit-sigs` read `allowed_signers`, pages and
  settings from (CI passes the base branch, never the change), and the base of the default range `<ref>..HEAD`.
  `--range` gives `disclosure` and `commit-sigs` other commits; `--text FILE` (repeatable) gives `disclosure` texts.
- `--site-dir DIR`: the `site` job builds the site into `DIR` instead of `build/ci/site`; `pages.yml` deploys what
  it builds there.
- The `markdownlint` job fails when markdownlint-cli2 reports it read no file (`Linting: 0 files`).
- `--install-node DIR` downloads the Node release `containers/Dockerfile` pins, checks it against the Dockerfile's
  SHA-256, unpacks it into `DIR` and prints its `bin` folder, for a Linux x64 runner's `$GITHUB_PATH`.

The `site` and `conventions` jobs install their hash-locked requirements into virtual environments under
`build/ci/`, made again only when the lock or the Python changes; with uv on PATH, uv makes them (CI caches its
downloads), else the standard library's venv and pip. The `mod` job runs Claude Code with an empty
configuration folder under `build/ci/`, so nothing of the account running it is read. A run under another Python
than CI's prints a note. Exit status: 0 every job passed, 1 a job failed, 2 a job could not run (a missing tool, an
unreadable pin, an unknown job).

## Tests

```sh
cd python
python -I -B -m unittest discover -s tests
```

Tests use synthetic pages only. Code-like test strings are assembled from pieces, so no file in the repo matches
the page rules or the starting-proof scan.
