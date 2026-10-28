---
kind: survey
---
# Xbox port: which build came first and what it changed

Verified against: `SLUS_212.15` (NTSC-U), the NTSC-U Xbox `default.xbe` (SHA1 `fe246a74…`) and both discs.

Addresses written **XBE `0x...`** are virtual addresses in `default.xbe` (base `0x00010000`); plain addresses are in
`SLUS_212.15`. The discs and the archive are described on [Xbox assets](xbox-assets.md), the executable as a
reverse-engineering aid on [Xbox executable](xbox-executable.md).

## Summary

The question: was *The Warriors* made for the PS2 and then ported to the Xbox, and what did the Xbox version change?
The answer has two halves.

- **Not a later port.** Both versions come from one codebase in one source-control depot at Rockstar Toronto. Their
  final builds were compiled on the same day from changelists five apart (the Xbox's first), and both shipped on the
  same day. Each side carries the other's names: the PS2 build runs its save screens from a file named
  `Gm_XboxSaveSystem.cpp`.
- **But the PS2 is the platform the engine and its data were built around.** The Xbox build still names its files
  under `ee_files\` (the EE is the PS2's processor), its sounds under `vags/` (VAG is the PS2's sound format), and
  reads the PS2's own chunk formats for animations, levels, collision and paths. What the port replaced is the
  platform layer: renderer (Direct3D 8 instead of RenderWare), file system, world streaming, sound and saves.

Whether work on the Xbox version started at the same time as the PS2's or later is **unknown**: no source found says.
The differences found so far are in [Differences](#differences); the Xbox's lighting and shadows are researched
separately ([the lighting and shadows research](issue:881)) and not repeated here.

**For Coney.** The gameplay code above the platform layer is one code base on both, so the PS2 version is a sound
main source for behaviour, and the one our research is built on. The Xbox version's gains are in presentation
(textures, movies, lighting) and can be taken as faithful; its audio and loading are no better, and in places worse.
The short text the README quotes is in [For the README](#readme).

## Method

- **The executables.** Strings, source paths, build banners and code references were read from `SLUS_212.15` in the
  shared Ghidra project and from `default.xbe` in a separate, read-only Xbox project, with a byte scan of both files
  for strings and for the code that references them.
- **The discs.** File lists, sizes and time stamps were read from the PS2 image's ISO 9660 directory and from the
  Xbox image's XDVDFS volume and `default.xbe` header (only counts, sizes, times and hashes were kept).
- **Public sources.** Rockstar's release statement, the Wikipedia article and its references, and press reviews,
  each cited by link in [How this was checked](#how-this-was-checked) with the date it was read.

## Findings

### Which build came first {#which-first}

| Claim | Evidence |
| --- | --- |
| Both versions were developed by Rockstar Toronto, published by Rockstar Games and shipped to North American shops on the same day; Europe followed four days later. The PSP version is a later port by Rockstar Leeds | inferred from Rockstar's release statement and the Wikipedia article ([sources](#how-this-was-checked)) |
| `main` (`0x001446d0`) formats a build banner from `Release`, the compile date `Sep 25 2005` (`0x00549bf8`) and time `20:21:44` (`0x00549c08`), and a second one from changelist `104242` and a build account's name | confirmed (code) at `0x001446d0` |
| The Xbox's start-up function (XBE `0x00068b40`) formats the same two banners from `Release`, `Sep 25 2005`, `17:33:16`, changelist `104237` and a developer's account name | confirmed (code) at XBE `0x00068b40` |
| So both final builds come from one depot (one changelist numbering), five changelists apart, the Xbox's compiled about three hours before the PS2's | inferred from the two banners |
| The banner times are local time in Toronto (UTC−4 in September): the XBE header's link time is `21:34:01` UTC, 45 s after the banner's `17:33:16` | inferred from the XBE header time stamp against the banner |
| The PS2 disc's files are dated from early to late September 2005, the newest `SLUS_212.15`, minutes after its banner time; the PS2 volume was made the same evening. The Xbox volume was made two days later and its certificate signed the day after that ([dates](#disc-dates)) | inferred from the disc time stamps |
| The two builds share 135 source files, the same object order and 955 Lua binding names; the platform files differ ([Xbox assets](xbox-assets.md#the-executable), [Xbox executable](xbox-executable.md#cross-reference)) | inferred from the source paths and the matched functions |
| **PS2 names in the Xbox build.** The Xbox prefixes every archive name with `ee_files\` before hashing it (XBE `0x0006c4f0`); its sound and music names start with `vags/`, its music under `vags/xboxmusic/` (a `vags/` name is used at XBE `0x0001dff0`); its chunk-type names keep a `Renderware Texture Dic` entry although the build has no RenderWare | confirmed (code) at XBE `0x0006c4f0` and XBE `0x0001dff0`; the chunk name inferred from the string alone |
| **PS2 data in the Xbox archive.** Animations, level resources, collision and paths are the PS2's chunk formats, matched by the PS2's resource hashes ([Xbox assets](xbox-assets.md#asset-kinds)) | inferred from the data |
| **An Xbox name in the PS2 build.** Game mode 7, the save-system screens, is the PS2 unit `Gm_XboxSaveSystem.cpp` (path at `0x0054fce8`); its `Enter`, `Gm_XboxSaveSystem_Enter` (`0x0015f830`), writes through the PS2 save system (`W_PS2SaveSystem.cpp`, path at `0x0058b5c0`). The Xbox build has the same unit (its path used at XBE `0x0007b3b0`) | confirmed (code) at `0x0015f830` and XBE `0x0007b3b0` |
| So the save-screen game mode was written once, under an Xbox name, while both versions were in development, and the PS2 runs it over its own memory-card layer | inferred from the shared unit |
| The Xbox textures are twice the PS2's width and height, so both discs were built from larger source art, not the Xbox's from the PS2's ([Textures](xbox-assets.md#textures)) | inferred from the matched textures |
| When work on the Xbox version began, and whether the PS2 was called the lead platform inside the studio | unknown ([q-start](#q-start)) |

### Disc dates {#disc-dates}

Disc check: only times and sizes were read; the files themselves stay on the discs.

| Disc | Time stamp | Value |
| --- | --- | --- |
| PS2 | primary volume descriptor, created | `2005-09-25 20:50:11` (local time; its zone field is not trusted) |
| PS2 | `SLUS_212.15` | `2005-09-25 20:33:38`; 4,903,976 bytes |
| PS2 | `WARRIORS.WAD`, `WARRIORS.DIR` | `2005-09-25 05:26:55` |
| PS2 | `IOP/BFW.SND`, `IOP/MUSIC.SND` | `2005-09-25 02:10:47`, `02:16:58` |
| PS2 | `PSS/*.BIK` (16 movies) | `2005-09-11` to `2005-09-24` |
| PS2 | `MODULES/` (Sony's modules, `IOP.IRX`) | `2005-05-16`, `IOP.IRX` `2005-07-19` |
| PS2 | `SYSTEM.CNF` | disc version `1.03` |
| Xbox | `default.xbe` header (link) | `2005-09-25 21:34:01` UTC |
| Xbox | XDVDFS volume | `2005-09-27 06:33:32` UTC (XDVDFS keeps no per-file times) |
| Xbox | certificate | `2005-09-28 16:18:30` UTC |

### Differences {#differences}

What the Xbox build does differently, found so far. **Gain** is something Coney can take as faithful; **problem** is
something it should not copy; **neutral** is a difference with no better side. Lighting and shadows are left to
[the lighting and shadows research](issue:881).

| Area | Difference | Kind | Evidence |
| --- | --- | --- | --- |
| Presentation | 23,210 of the PS2's 26,308 textures have a larger Xbox version, nearly all twice the width and height ([Textures](xbox-assets.md#textures)) | gain | inferred from the matched textures |
| Presentation | Each movie has three versions; the player picks `_hd` when the display reports an HD mode, else `_w` for 16:9, else the 4:3 one (and `_pal` forms, absent from this disc), at XBE `0x0024a040`. The `_hd` files are 1280 × 720 ([Movies](xbox-assets.md#movies)) | gain | confirmed (code) at XBE `0x0024a040` |
| Presentation | The renderer is the port's own Direct3D 8 device, not RenderWare; world geometry is Xbox vertex and index buffers, models the same meshes as float triangle lists ([Models](xbox-assets.md#models)) | neutral | inferred from the strings and the data |
| Presentation | An HD video output mode (720p) for the game itself, as one review says | unknown ([q-hd](#q-hd)) | speculative ([q-hd](#q-hd)) |
| Audio | Music is stereo at 26,500 Hz, against the PS2's 30,000 to 44,100 Hz; effects and speech are at the PS2's rates ([Sound](xbox-assets.md#sound)) | problem | inferred from the wave banks |
| Audio | 23,147 of the PS2's 25,395 sound names are on the Xbox; 376 Xbox names are not on the PS2 | neutral | inferred from the shared name hashes |
| Audio | The executable links Microsoft's WMA decoder and Dolby sections: custom soundtracks and Dolby Digital output ([The executable](xbox-assets.md#the-executable)) | gain | speculative ([q-audio](#q-audio)) |
| Content | One movie, `l51_in`, is 8 s longer on the Xbox: a different cut; `plogo` has a sound track, silent on the PS2 | neutral | inferred from the Bink headers |
| Content | 324 events in the shared clips differ, 174 of them in values the Xbox does not leave zero; one clip's keys differ; one `level14` clip is replaced ([Animations on both discs](xbox-assets.md#either-animations)) | unknown ([q-anim](#q-anim)) | inferred from a byte comparison |
| Content | The Xbox build has four Lua bindings the PS2 lacks, two for vertex shaders ([Numbers](xbox-executable.md#numbers)) | neutral | inferred from the binding registration |
| Behaviour | Both run the shared save-screen mode over their own save layer: the PS2's `W_PS2SaveSystem.cpp` over the memory card, the Xbox's own (no PS2 save unit in the XBE) | neutral | inferred from the source paths (see [Which build came first](#which-first)) |
| Behaviour | The Xbox build is five changelists older than the PS2's; what they changed is not known | unknown ([q-changelists](#q-changelists)) | inferred from the two banners |
| Behaviour | Struct layouts and vtables differ (MSVC against GCC 2.x), so offsets do not transfer ([Not easier](xbox-executable.md#findings)) | neutral | confirmed (code) at XBE `0x000e1640` |
| Performance | One Xbox review finds the loading long ("a good amount of time grinding away") and the graphics only passable; no PS2 comparison is made | problem | inferred from the review ([sources](#how-this-was-checked)) |
| Performance | Frame rate against the PS2 | unknown ([q-perf](#q-perf)) | speculative ([q-perf](#q-perf)) |
| Bugs | No Xbox-only bug list was found in public sources | unknown ([q-bugs](#q-bugs)) | speculative ([q-bugs](#q-bugs)) |
| Reception | Metacritic gives the Xbox version 85 and the PS2 84 | neutral | inferred from the Wikipedia article ([sources](#how-this-was-checked)) |

### For the README {#readme}

*The Warriors* was made by Rockstar Toronto for the PlayStation 2 and the Xbox at once, from one codebase: both
versions' final builds were compiled on the same day from neighbouring changelists, and both shipped on 17 October
2005. The code above each console's platform layer is the same, and the engine and its data were built around the
PS2: the Xbox version still reads the PS2's data formats and names its files after the PS2's processor and sound
format. So Coney takes the PS2 version as its main source for how the game plays, and treats what the Xbox port does
better, chiefly its larger textures, HD movies and its own renderer's lighting, as faithful too.

## Open questions

### When the Xbox work started {#q-start}

No public source or file says whether the Xbox version was begun with the PS2's or added later. A developer interview
or credits that date the Xbox team would settle it.

### HD output {#q-hd}

A review says the Xbox version runs at 720p, but the review could not be opened. Read the XBE's display set-up (the
flags the movie player tests at XBE `0x0024a040` and where they are set) and run the game in xemu.

### Custom soundtracks and Dolby {#q-audio}

The WMA and Dolby sections are linked, but no code that uses them was read. Find their callers in the XBE.

### Animation events {#q-anim}

Whether the 324 differing events change timing or effects in play: which bytes `Anim_FireFrameEvents` (`0x00101dd8`)
reads for each event type, against the differences sorted under [Animations on both
discs](xbox-assets.md#either-animations). It also decides whether an Xbox-only install could play the same game
([Proposal: either disc](xbox-assets.md#either-disc-proposal)).

### The five changelists {#q-changelists}

What the PS2 build has that the Xbox's lacks. Comparing matched functions between the builds may show a fix present
on one side only.

### Frame rate {#q-perf}

Measure both in their emulators on the same scenes.

### Xbox-only bugs {#q-bugs}

Search player reports and xemu's compatibility notes, and confirm each in xemu.

## How this was checked

Read on 2026-10-25 for [#884](issue:884):

- The banners: `main` decompiled in the shared project; XBE `0x00068b40` decompiled in the Xbox project (port 8091),
  read-only. Both banners print with the same two format strings.
- Code references in the XBE: the string's virtual address searched as a 32-bit word in `.text` (as on
  [Xbox executable](xbox-executable.md#method)), then the function holding the hit read in Ghidra.
- Disc times: the PS2 image's primary volume descriptor and directory records; the Xbox image's XDVDFS volume
  descriptor (a FILETIME at `+0x1c`) and the XBE header's time stamp (`+0x114`) and certificate time.
- Public sources, read 2026-10-25:
    - Rockstar Games' release statement as published by Gaming Nexus, 17 October 2005,
      <https://www.gamingnexus.com/News/904/The-Warriors-ships-for-the-PS2-and-Xbox>: shipped for PlayStation 2
      and Xbox, developed by Rockstar Toronto.
    - Wikipedia, *The Warriors (video game)*, <https://en.wikipedia.org/wiki/The_Warriors_(video_game)>: release dates
      per platform and region, Rockstar Toronto for both console versions, Rockstar Leeds for the PSP, the review
      aggregates; it names no lead platform.
    - Worthplaying, Xbox review, 20 November 2005,
      <https://worthplaying.com/article/2005/11/20/reviews/29161-xbox-review-the-warriors/>: the loading and graphics
      remarks.
    - Sound & Vision, game review, <https://www.soundandvision.com/node/101095>: named by a search as saying the Xbox
      version runs at 720p; the page refused the fetch, so the claim is unverified ([q-hd](#q-hd)).
