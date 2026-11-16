---
kind: system
system: game-stats
owns:
  ranges: ["0x00214d08-0x00217a70"]
  functions: ["0x00159520", "0x00159538", "0x001596d0", "0x0041f298", "0x004241d8", "0x00424298", "0x004245d8"]
---
# The statistics screen

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`).

## Summary

The statistics screen is the full-screen page a script opens with `ShowGameStatsInterface` (the hub's menu does,
`menu.DoOptionsMenu` in `level95.lua`). Over a background that slowly cycles through red, green and blue it shows a
summary at the top left (a title, the completion percentage, the missions done, the collectables found, the unlockables
opened and the total play time) and, below, a list of the missions the player has finished. Each list entry is a header,
the mission's name after its number in a circle of the area's colour, and under the focused header a block opens with
the mission's picture and up to five lines: the best score against the goal, three counts of what the mission unlocks,
and the time spent. Up and down move through the list; back fades the screen out and returns to the script.

How it works: the binding pushes game mode `0x13`, whose widget is the screen object built by `GUI/GameStats.cpp`. The
screen gathers its numbers once, when it is made: the counts from the unlockables manager, the list from the level
table and the bests from the statistics record. The list is a `ScrollingMenu` whose items are `CircledText` headers
and whose item descriptions are the per-mission blocks (`GameStatsSubItem`). The widgets themselves are on their own
pages: the circled header on [Circled texts and headers](circled-text.md), the menu on
[GUI: ScrollingMenu](gui.md#fn-scrollingmenu); the unlockable records the counts walk on
[Inventory, unlockables and statistics](player-state.md#unlockables); the game-mode stack on [Boot](boot.md).

## Map

`GUI/GameStats.cpp` names itself (path string at `0x00559f70`) and holds the screen; `GUI/GameStatsSubItem.cpp` holds
the per-mission block ([Source map](source-map.md#gui); confirmed (code)). Mode `0x13`'s functions sit with the other
game modes. The names are ours. Every function, with its role and level, is on [the front-end function
list](frontend/functions.md); these are the entry points, each read in Ghidra at its address.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x00214d08`-`0x00216eb0` | `GUI/GameStats.cpp` | the screen; the five batch makers at its start belong to it by their callers | confirmed (code); the file's start inferred from the address order |
| `0x00216eb0`-`0x00217a70` | `GUI/GameStatsSubItem.cpp` | the block; the source map ends the file at `0x002176b8`, but the four functions after it are the block's own methods (its vtable `0x0053ef58`) | confirmed (code); the file's end inferred from the vtable |
| `0x00159538` / `0x001596d0` | `GameStatsMode_Enter` / `GameStats_Update` | mode `0x13`'s enter and update ([The mode](#mode)) | confirmed (code) |
| `0x00215e78` | `GameStats_Init` | builds every part ([The parts](#parts)) | confirmed (code) |
| `0x00215150` | `GameStats_BuildSummary` | the summary's text ([The summary](#summary-text)) | confirmed (code) |
| `0x00215da0` | `GameStats_CollectLevels` | which levels are listed, in which order ([The list](#list)) | confirmed (code) |
| `0x002155a8` | `GameStats_BuildMissionList` | the menu of headers and blocks ([The list](#list)) | confirmed (code) |
| `0x00216eb0` | `GameStatsSubItem_BuildText` | a block's text ([A mission's block](#block)) | confirmed (code) |
| `0x00217788` | `GameStatsSubItem_Layout` | a block's picture and text ([A mission's block](#block)) | confirmed (code) |
| `0x00216a60` | `GameStats_Update` | the background's colour, then every part ([The background](#background)) | confirmed (code) |
| `0x004245d8` / `0x00424298` | `Unlocks_CountUnlocked` / `Unlockables_GetLevelTargets` | the summary's and a block's counts ([The counts](#counts)) | confirmed (code) |
| `0x0041f298` | `LevelRecord_GetAreaColour` | a header's circle colour ([The list](#list)) | confirmed (code) |

## Data

### Layout values {#values}

Confirmed (code) at `0x00215e78` unless a line says otherwise. `GameStats_Init` writes these globals before it builds
the screen, picking the column by the device's flags (`*0x0050cdb4`; [Graphics](graphics.md#video-mode)): flag `0x02`
first, with or without `0x04` (16:9); then `0x20` (progressive), whatever its shape; then `0x04` alone; else the
default. Positions are GUI coordinates ([HUD: coordinates](hud.md#coordinates)).

| Value | Where | Default | 16:9 (`0x04`) | Progressive (`0x20`) | `0x02` | `0x02` and `0x04` | Evidence |
| --- | --- | --- | --- | --- | --- | --- | --- |
| the summary's and the usage line's x | `0x00510114` (copied to `0x005100f0`) | 0.01 | -0.07 | -0.05 | 0.02 | -0.15 | confirmed (code) |
| the summary's y | `0x005100f4` | 0.06 | 0.04 | 0.08 | 0.16 | 0.14 | confirmed (code) |
| the usage line's y | `0x00510118` | 0.94 | 0.94 | 0.9 | 0.82 | 0.86 | confirmed (code) |
| the background's size | `0x005100e8` / `0x005100ec` | 1.15 × 1.1 | 1.45 × 1.2 | 0.74 × 0.74 | 1.1 × 0.8 | 1.55 × 0.95 | confirmed (code) |
| a header's size | `0x005100e4` / `0x005100e0` | 0.35 × 0.09 | 0.43 × 0.09 | 0.3 × 0.09 | 0.3 × 0.07 | 0.43 × 0.078 | confirmed (code) |
| a circle's size | `0x005100f8` / `0x005100fc` | 0.065 × 0.11 | 0.081 × 0.11 | not written: 0.065 × 0.11 | 0.05 × 0.077 | 0.051 × 0.077 | confirmed (code) |
| the list's place: the first header's centre ([Placement](#placement)) | `0x00510100` / `0x00510104` | (0.185, 0.45) | (0.145, 0.45) | (0.1, 0.45) | (0.17, 0.445) | (0.07, 0.46) | confirmed (code) |
| the clip height: the window's stacked heights are cut there | `0x00510110` | 0.5 | 0.5 | 0.5 | 0.38 | 0.4 | confirmed (code) |

The progressive column leaves the circles' size at its static value (`.data`, the default's), since the video mode is
chosen at boot (inferred from no other writer of `0x005100f8`). The circles' step divisor follows the mode too: 3, or 2
with flag `0x02` (`CircledText_Setup`, [Circled texts](circled-text.md#ct-setup)). In every mode:

| Value | Where | All modes | Evidence |
| --- | --- | --- | --- |
| window | `0x00510108` / `0x0051010c` | at most 5 entries | confirmed (code) |
| reveal step | `0x005100d4` | 0.25 an update | confirmed (code) |
| colour step | `0x0051011c` | 5,000 ms | confirmed (code) at `0x00216a60` |
| background colours | `0x00640c60` | (150, 50, 50), (50, 150, 50), (50, 50, 150), written by `GameStats_StaticInit` (`0x00216e00`) | confirmed (code) |
| usage line | | string `0x19` at the column's x and y | confirmed (code) |

### The screen object {#screen-object}

Confirmed (code) at `0x00215e78` and `0x002166d0`. The object lives at mode `+0x20`; its widget vtable is `0x0053eec0`
and its command listener `0x0053eea0`.

| Offset | Part | Evidence |
| --- | --- | --- |
| `+0x60` | the listed level indices (a vector), sorted by `GameStats_CollectLevels` (`0x00215da0`) | confirmed (code) |
| `+0x64` | batch over sheet-table record 12, 1 sprite, depth 8,000: the background | confirmed (code) |
| `+0x68` | batch over record 24, 40 sprites, depth 8,700: the headers' backs and circles | confirmed (code) |
| `+0x6c` / `+0x70` | batches over records 22 and 23, 12 sprites each: the mission pictures | confirmed (code) |
| `+0x74` | batch over record 24, 3 sprites: the three special entries' pictures | confirmed (code) |
| `+0x88` | the `ScrollingMenu` | confirmed (code) |
| `+0x90` | the summary's text | confirmed (code) |
| `+0x94` / `+0x9c` | the missions-done count and the time sum `GameStats_BuildMissionList` adds up ([The summary](#summary-text)) | confirmed (code) at `0x002155a8` |
| `+0xa8` / `+0xac` | the background step's start time and its index | confirmed (code) at `0x00215150` |

A block (`GameStatsSubItem`, vtable `0x0053ef58`, `0x90` bytes) holds its picture at `+0x68`, a `ScrollingTextWidget` at
`+0x6c`, its text at `+0x70`, its level at `+0x74` and its picture column's width at `+0x60`. Confirmed (code) at
`0x00217350`.

### The level records it reads {#level-records}

Confirmed (code) at `0x00215ca0` and `0x002155a8`. The level table is game state `+0x14d4`, `0x84` bytes a record, the
count at `+0x56d4`. The screen reads the record's mission byte `+0x0c` (listed when set, and the sort key), its number
`+0x24` (the text up to a `:`), its name `+0x49`, its area byte `+0x7c` and its picture's sprite word `+0x80`. A
level's best score and time come from `Stats_GetLevelBest` (`0x00423050`), the 12-byte record at
`0x006fe490` + `0x300` + 12 × the level's mission slot.

## Behaviour

### The mode {#mode}

Confirmed (code) at the cited addresses. `ShowGameStatsInterface` pushes game mode `0x13`
([HUD bindings](../references/bindings/hud.md#showgamestatsinterface)).

- **Enter** (`GameStatsMode_Enter`, `0x00159538`): puts the task manager in phase 0, clears the auto-close flag
  (`0x0050c6e4`), sets the mode's own two flags (not the screen object's): `+0xd0`, the fade out queued, to 0 and
  `+0xd4`, the fade in still to queue, to 1; enters the store tint (`ScreenFx_EnterStore`, `0x0018e6b8`) with a clear
  colour, loads the sound bank named at `0x0054ee50` (`menu`) unless it is the current bank, and runs `GameStats_Init`
  (`0x00215e78`) on the screen object.
- **Update** (`GameStats_Update`, `0x001596d0`): one world frame, then the screen's update and render, and the overlay
  only (`ResourceMgr_RenderOverlay`, `0x00185d20`). The world frame is a gameplay frame without its drawing: the tick,
  the task manager's phase 0 (`TaskManager_Tick`), the cameras, the managers while `0x005e536c` is 1, streaming,
  `WorldManager_Update`, the instances, the audio listener at the player camera (`AudioManager_Update`) and the pad
  bindings; nothing of the world or the HUD is drawn and no script runs. So the level goes on under the screen: the
  AI's update runs while it is open (confirmed (runtime), run r-20261116-stats-world-frame), and its characters move
  and their sounds play (inferred from the frame's calls), unseen under the opaque background. On the first frame the
  screen is ready (`GameStats_IsReady`) while `+0xd4` is set it clears `+0xd4` and queues a fade in over 1 s
  (`ScreenFx_Queue`, `0x0018b168`). Once the auto-close flag is set it queues a fade out over 1 s, once (`+0xd0` set),
  and pops the mode on the first frame that fade is over (the fade's busy flag at `0x005fdeb8` + `0x1d4`).
- **Input** (`GameStats_OnCommand`, `0x002169a8`): back (command 5) sets the auto-close flag (`GameStats_SetAutoClose`,
  `0x00159520`); accept (4) is taken and does nothing; the list's own input moves the focus. The mode's exit runs the
  script's `onClose` callback.

### The parts {#parts}

Confirmed (code) at `0x00215e78`. `GameStats_Init` makes the five batches, the summary (a `ScrollingTextWidget` set up
with size 0 and given its text with wrap width 0, so its lines are never wrapped), the list
([The list](#list)), the background sprite and the usage line, at the values of [Layout values](#values).
`GameStats_Render` (`0x00216d48`) draws the background, the summary, the list and the usage line, in that order.

### The background {#background}

Confirmed (code) at the cited addresses unless a line says otherwise.

Set up in `GameStats_Init` and coloured in `GameStats_Update` (`0x00216a60`): one sprite, word `0xc0005` (record 12,
rectangle 5), centred at (0.5, 0.5), depth 8,000. Each update sizes it to 1.15 × 1.1 through `BaseWidget_SetSize`
(`0x001a2120`) with the device's conversion on, so the 1.15 is a GUI width that `RwDevice_GuiWidthToOverlay`
(`0x00195330`) stretches by 640 / 448, and the widget keeps that size (`BaseWidget_UpdateSize`, `0x001a21e8`, mode 2).

Its colour walks red, green, blue and back to red. Building the summary starts step 0 at the current time. Each update,
once more than 5,000 ms have passed since the step began, the next step begins on that frame and shows its first
colour; otherwise the colour is the blend of the step's two colours at the elapsed share, each channel
a × (1 − t) + b × t cut to a whole number (`Colour_LerpPacked`, `0x0017a258`).

### The summary {#summary-text}

Confirmed (code) at the cited addresses unless a line says otherwise.

`GameStats_BuildSummary` (`0x00215150`) writes it from the format at `0x00559f98` (one `snprintf`, at most 511
characters) and its arguments at `0x002153b4`-`0x00215458`.

- The title (string `0x60`) in the big font at size 1.6, grey 178 (`B2B2B2FF`), then a line break; every line after it
  is size 1.2.
- Five lines, each a label in grey 80 (`505050FF`), the value's colour tag, a space and the value:
    1. string `0x61` and the **completion percentage**: (unlocked / total) × 100 over the counted records
       ([The counts](#counts)) in single precision, dividing first (`0x00215290`), truncated (`Float_ToUInt`,
       `0x0042c718`), with a `%` after it; so 53 of 100 shows 52;
    2. string `0x63` and the **missions**, done / listed: `GameStats_BuildMissionList` adds one to `+0x94` for each
       entry it adds that is not one of the specials 95, 60 and 101, so done is the complete listed levels; listed is
       every listed level less 3;
    3. string `0x64` and the **group-1 items**, unlocked / total;
    4. string `0x62` and the counted records, unlocked / total (the numbers behind the percentage);
    5. string `0x66` and the **total time**: slot 0's time plus the times of the entries the list shows (`+0x9c`,
       added only for the levels the list adds), formatted by [the time format](#time-format) without days. This line
       has its space before the colour tag and no line break after it.
- The value colours: red (134, 26, 26), `861A1AFF`, when complete (100 %, or unlocked equal to total); otherwise gold
  (128, 100, 0), `806400FF`, or purple (106, 65, 131), `6A4183FF`, for the group-1 line. The time is gold.

### The time format {#time-format}

Confirmed (code) at `0x00214f58`. `GameStats_FormatTime(seconds, withDays, out)` splits the seconds into days
(÷ 86,400), hours (the rest ÷ 3,600) and minutes (the rest of the hour ÷ 60), dropping the seconds. Without days, which
both callers ask for, the days are added to the hours, 24 each. It writes minutes alone below an hour, hours then
minutes from an hour, and days, hours and minutes when days are shown, each count followed by a space and its word,
the parts joined by a comma and a space. Each word is the singular for a count of 1 and the plural otherwise (minutes
`0x67` / `0x68`, hours `0x69` / `0x6a`, days `0x6b` / `0x6c`), so no time reads `0` and the plural, and a whole hour
still shows its `0` minutes.

### The list {#list}

Confirmed (code) at the cited addresses.

**Which levels, in which order.** `GameStats_CollectLevels` (`0x00215da0`) takes every level record whose mission byte
is set, except levels 61-65 and 102-137 (`GameStats_IsLevelListed`, `0x00215ca0`), and sorts their indices by the
mission byte, ascending (`StatLevelSort_IntrosortLoop` `0x004ed068` and `StatLevelSort_FinalInsertion` `0x004ed388`,
comparing record `+0x0c`). Disc check: on the NTSC-U disc the 26 listed records carry the bytes 1-26, each once, so the
order is total, from level 99 through to 95, 60 and 101 last. `GameStats_BuildMissionList` (`0x002155a8`) then adds an
entry for each listed level that is complete (`Unlocks_IsLevelComplete`, [The counts](#counts)), and for 95 and 101
whether or not they are.

**One entry per level.** The header, a `CircledText` built in a `0x600`-byte block allocated with the tag
`CircledTextHeader` ([Circled texts and headers](circled-text.md#circled-text)), is the menu's item
(`ScrollingMenu_AddItem`, `0x001e1830`); the level's block is that item's description
(`ScrollingMenu_SetItemDescription`, `0x001e1a38`, slot `+0xd0`), which the menu opens under the header 550 ms after
the item takes the focus and closes when it loses it ([GUI: ScrollingMenu](gui.md#fn-scrollingmenu)). After the first
entry is added the menu takes the focus (slot `+0x90`, which focuses its first item), and that entry's block is given
reveal 1.0 and the focus at once, so the first block starts open.

**The menu's settings**, confirmed (code) at `0x002155a8`: `+0xc0` = 0 (slot `+0xd8`), the reveal step `+0xc8` = 0.25
(slot `+0xf8`), `+0x100` = 1 (slot `+0x1a0`), the window `+0xac` and `+0xb0` = min(5, the entry count) (slots `+0x128`
and `+0x138`), the clip height `+0xb8` = 0.5 (slot `+0x130`). It is placed each frame by `GameStats_UpdateMenu`
(`0x002169e0`).

**A header.** `CircledText_Setup` with circles 0.065 × 0.11, size 0.35 × 0.09, circle offset 0, text metrics
`Font_Size(1.7)`, and the sprite word `0x180002` (record 24, rectangle 2) in the header batch for both its back and its
circles. The back's colour is then set to (0, 0, 0, 0), so the back is invisible; the circled characters are black
(colour-table entry `0x005fd260`). Its text is the record's name; its circled characters are the record's number up
to a `:`, three at most. Confirmed (code) at `0x002155a8`.

**The area colour.** `LevelRecord_GetAreaColour` (`0x0041f298`) turns the record's area byte into the circles'
colour, alpha 255: 1 (132, 78, 37), 2 (62, 98, 41), 3 (42, 71, 141), 4 (74, 49, 29). For any other byte it writes
only the alpha and leaves the colour as the caller's stack held it. When the number reads as 0 (`atoi`, `0x00433a68`)
the screen uses purple (106, 65, 131) instead. Confirmed (code) at `0x0041f298` and `0x002155a8`.

**The specials.** Levels 95, 60 and 101 get only a text, strings `0x73`, `0x74` and `0x75`: no circled characters and
no colour, so their headers show no number even where the record has one. Confirmed (code) at `0x002155a8`.

### Placement {#placement}

Confirmed (code) at the cited addresses unless a line says otherwise.

The list's place is the **centre of the first header**:
`ScrollingMenu_Update` (`0x001e21d8`) puts each item at the menu's (x, y) plus the heights of the items above it, and
a `CircledText` draws itself about that point, its left edge at x − width / 2 (`CircledText_Update`, `0x001d1250`).
So the first header spans x 0.01-0.36 and is centred on y 0.45.

An item's height is its header's plus, while its description shows, the description's shown height less 2 / the
screen's height (`ScrollingMenuItem_GetRect`, `0x001e0c90`). The description goes at the item's x and at y + the
header's height / 2 − 2 / the screen's height (`ScrollingMenuItem_Update`, `0x001e0f98`, with `+0x78` = 0): from the
header's bottom, two screen lines up. The next header follows one header and the shown block, less that overlap, below.

### Scrolling {#scroll}

Confirmed (code) at the cited addresses and confirmed (runtime), run r-20261116-stats-scroll, which read the menu every
frame while the pad scrolled a list of 25 entries (all four states below seen, each over four updates).

The menu keeps a window of entries, first `+0xa4`, count `+0xac`, at most `+0xb0` (5), and a scroll state `+0x80`. An
entry (`ScrollingMenuItem`) has a reveal `+0x50`, 0 to 1, which it gives its header (`ScrollingMenuItem_SetReveal`,
`0x001e0b58`; with `+0x68` = 0 the header's reveal is the entry's) and which scales its height
(`ScrollingMenuItem_GetRect`, `0x001e0c90`: the header's height, plus the shown block less 2 / 448 while the block
shows, times the reveal).

- **A move** (`ScrollingMenu_MoveDown` `0x001e1d70`, `ScrollingMenu_MoveUp` `0x001e1cb8`, after
  `ScrollingMenu_Select` moved the cursor): with `+0x100` set (the statistics screen's) the middle row is
  count / 2 − 1, so row 1 of five. A move down to a row past it, while entries are left below the window, adds one to
  the count and starts state 2 (down). A move up to an entry above the window, or to a row above the middle while
  entries are above it, starts state 1 (up) with the window one entry higher and one longer.
- **Each update** (`ScrollingMenu_Update`, `0x001e21d8`) reads the pad only while the state is 0, 100 ms after the last
  command, then steps the state by `+0xc8` (0.25): state 2 lowers the first entry's reveal and gives the window's last
  entry 1 − that reveal; at 0 the window starts one lower. State 1 raises the first entry's reveal and gives the last
  entry 1 − it; at 1 the scroll is done. State 3 raises the first entry's reveal without touching the last. State 4
  lowers it (at 0 the window starts one lower) and, while the window's last entry is not the focused one, gives that
  entry 1 − the first's reveal too.
- **The window** is then `+0xb0` entries, one more while a state runs, cut to the list's end; each is placed at the
  stacked heights of those above it ([Placement](#placement)) and updated, which steps its block
  ([GUI: ScrollingMenuItem_Update](gui.md#fn-scrollingmenu)): the block opens only while its header shows whole.
- **The clip** (`+0xb8`, 0.5): stacking the window's heights again (with every reveal set back to 1 while the state is
  0), the first entry that takes the sum past the clip ends the drawn entries (`+0xb4`), itself drawn only when it is
  focused. `ScrollingMenu_Render` (`0x001e2e40`) draws those, one more in state 3. So a header that would cross the
  clip is not drawn at all, and the focused entry is drawn whole past it.
- **The clip's scrolls**, while the state is 0: the entry above the window is measured at reveal 1 and put back at 0;
  when the drawn entries and it fit under the clip and fewer than five are drawn, state 3 starts with the window one
  entry higher. Otherwise, when the last drawn entry is not the first, the drawn entries run past the clip and that
  entry is focused, state 4 starts. At the list's end, where a move cannot scroll, the last block opening hides the
  headers above it one by one (25 entries: the window ends at entries 23 and 24), and moving up reveals them again.

Each header of the window is centred one entry height below the one above it, so while a header's reveal falls the
headers under it move up with it (inferred from [Placement](#placement): with the first entry at 0.75 the second sits
at 0.45 + 0.09 × 0.75 = 0.5175, which the per-frame trace of run r-20261116-stats-scroll showed; the run record keeps
the first header's place, not this sample). The original's input (`ScrollingMenu_OnCommand`) also waits for an
entry's description to drop its focus only when the menu's `+0xe4` is set, which the statistics screen leaves at 0.

### A mission's block {#block}

Confirmed (code) at the cited addresses.

**Set-up** (`GameStatsSubItem_Setup(0.35, item, level, word, batch)`, `0x00217350`): the picture is the record's
sprite word `+0x80` (`0x180001`, `0x180000` and `0x180003` for 95, 60 and 101) in the batch
`GameStats_PickThumbBatch` (`0x00215490`) chooses: `+0x74` for the specials, `+0x70` for levels 2, 3, 5, 9, 11, 20,
31, 34, 51, 52, 54 and 55, `+0x6c` for the rest. The text widget is set up with size 0, and the column is 0.35 wide.

**Layout** (`GameStatsSubItem_Layout`, `0x00217788`, slot `+0x30`): the block is placed by its x centre and its top
(the layout first moves y down by half the shown height). Its width is the text widget's (0) + the column; its height
the text's height × the reveal (`GameStatsSubItem_GetScaledRect`, `0x00217730`). The picture, white, depth 11,000, is
centred on the block's position, the column's width wide and the shown height high, its texture rectangle cut by the
reveal. The text starts at x − width / 2 + 0.35 + 0.02, its first line half a `Font_Size(1.0)` line below the block's
top, with the same reveal.

**The text's height and its wipe.** The text widget's height `+0x38` is its measure's bottom less its top
(`ScrollingTextWidget_SetText`, `0x001e3070`), and the measure's bottom is one line, h + the line gap, past the last
line's centre line (`MessageHUD_Layout`, `0x001b9600`), so a block of five size-1.2 lines is 5 × (h + gap) high: 0.2664
in the default mode (run r-20261116-stats-scroll), the gap being −h / 7. `ScrollingTextWidget_Update` (`0x001e32a0`)
sets the text's depth `+0x1f4` to that height less a line gap of the widget's metrics, and `MessageHUD_Layout` draws
each line with the reveal r below 1 as a share of each glyph, (depth × r + the first line's y − the line's y) / h,
clamped to 0-1, which `Font_Draw` (`0x00179c30`) turns into a glyph cut from the bottom (its height and its texture
rectangle's v1 × the share, its top kept). So the text is wiped from the top, line by line, as the picture is cut.
Confirmed (code) at those addresses.

**Text** (`GameStatsSubItem_BuildText`, `0x00216eb0`), from the formats at `0x0055a268`-`0x0055a3e8`, confirmed (code):

- the score: size 1.2, the label `0x6d` in grey 178, then the score in gold followed, in grey 80 brackets, by the goal's
  label `0x6e` and the goal in red, while the score is below the goal (`0x0055a268`); at or above it, the score alone
  in red (`0x0055a2d8`). Levels 95 and 101 have no score line, only the size tag;
- three counts from `Unlockables_GetLevelTargets` ([The counts](#counts)), each a grey 178 label, its colour, a space
  and found / total (`0x0055a310`): `0x64` the level's group-1 items (purple until complete), `0x6f` its character-type
  records (type 4) and `0x70` its arena records (type 2), these two gold until complete; red when complete. A count
  whose total is 0 is left out;
- the time: the label `0x66` in grey 178, a space, then the time in gold, formatted without days, with no line break
  (`0x0055a340`);
- the block joins the five in the order score, group-1, type 4, type 2, time (`0x0055a3a0`) and pads with one line
  break for each line left out, to five (`0x0055a3b0`-`0x0055a3e8`; the time counts as a line, the score as one except
  for 95 and 101).

So a block's labels are grey 178 where the summary's are grey 80.

### The counts {#counts}

Confirmed (code) at the cited addresses unless a line says otherwise.

Each walks every unlockable record in index order; unlocked means the
record's bit is clear (`Unlockables_IsUnlocked`, `0x00424098`). The records' layout and types are on
[Unlockables](player-state.md#unlockables).

- **`Unlocks_IsLevelComplete(level)`** (`0x004241d8`): lists the level's group-0 records (`Unlockables_ListByLevelGroup`,
  `0x00423de0`, at most 32) and answers with the first whose item is 0: a level is complete when its (level, 0, 0)
  record is unlocked.
- **`Unlocks_CountUnlocked(&groupUnlocked, &groupTotal, &unlocked, &total)`** (`0x004245d8`): `total` and `unlocked`
  over the records whose type is **not** 0, 5, 7, 8, 9, 10 or 11 (the test names the types it leaves out, which on the
  disc's types 0-11 leaves 1, 2, 3, 4 and 6) and whose level byte is neither 1 nor 4; `groupTotal` and `groupUnlocked`
  over the distinct (level, item) pairs of the group-1 records of types 1, 2, 3, 4 and 6 (named one by one here), any
  level, marked in a table at level × 100 + item, testing only the first record of each pair.
- **`Unlockables_GetLevelTargets(level, ...)`** (`0x00424298`): over the level's records (60 stands for 60-64, 101 for
  101-137), groups 0 and 1 only: the score goal, the highest item among the group-0 records × 1,000; the type-4 and
  type-2 records, total and unlocked; the distinct items of the group-1 records of types 1, 2, 3, 4 and 6, total and
  unlocked.

Disc check: over the NTSC-U disc's 610 records the summary counts 469 records and 40 group-1 pairs; the per-level
numbers are on [Unlockables: the counts](player-state.md#unlockable-counts).

## In Coney

The screen is `GameStatsScreen` (`repo:src/gui/game_stats.h`) and mode `0x13` is `GameStatsMode`
(`repo:src/gamemodes/game_stats_mode.h`); the counts are `UnlockRecords::countUnlocked`, `levelTargets` and
`isLevelComplete` (`repo:src/warriors/unlock_records.h`), the time is `formatPlayTime`, the summary
`gameStatsSummary`, a block's text `gameStatsSubItemText` and the area colour `areaColour`. The level table gives the
mission byte, area byte and picture word as `LevelRecord::kMissionValue`, `kSubwayValue` and `kPictureValue`; that
they are those bytes is corroborated by the picture words naming records 22 and 23 exactly as
`GameStats_PickThumbBatch` splits the levels. The headers are Coney's `CircledText`
([Circled texts and headers](circled-text.md#in-coney)). Tests: `repo:tests/gui/game_stats_test.cpp` (the formats,
the colours, the list's places and the blocks' reveal), `repo:tests/warriors/unlock_counts_test.cpp` and
`repo:tests/platform/disc_game_stats_test.cpp`, which calls the binding in `level95` and moves through and closes the
screen with the pad. On the NTSC-U disc 26 levels are listed, so the missions line counts 23.

The layout of each video mode is `GameStatsLayout::forFlags`, which the mode picks from the device's flag `0x02`
(Coney's European setting; its screen is never 16:9 or progressive). The window's scrolling and clip are
`ScrollingWindow` (`repo:src/gui/scrolling_window.h`), tested by `repo:tests/gui/scrolling_window_test.cpp`; the reveal's
cuts are `BaseWidgetSetup::rectShare` and `TextWidget::setReveal` (with a reveal depth for the block's line wipe).

Stand-ins and deliberate differences:

- An area byte other than 1-4 gives black, where the original leaves the colour unwritten; a tie in the mission byte is
  broken by the level number; fewer than three listed records count as none ([q-edge-cases](#q-edge-cases)).
- `isLevelComplete` searches every record, without the original's bound of 32 ([q-edge-cases](#q-edge-cases)).
- The mode runs no world frame, where the original's runs the level on under the screen ([The mode](#mode)): Coney
  has no world frame a menu mode can run yet, so the level stands still while the screen is open. The store tint is
  not set either; it shows nothing.
- Size-1.2 lines are drawn about 7 % shorter than the original's, from the font metrics' aspect
  ([q-line-height](#q-line-height)).

## Open questions

### Edge cases nobody has seen {#q-edge-cases}

An area byte outside 1-4, a tie in the mission byte (the original's
sort is not stable), fewer than three listed records, a level with more than 32 group-0 records. None happens on
the NTSC-U disc; a modded table would show what the original does.

### Line height {#q-line-height}

`Font_Size` (`0x00179808`) multiplies a line's height by the live view aspect
`0x0050b208`, which the device sets to 1.45 in interlaced 4:3 (static 1.3333); Coney's font metrics use 1.3333, so
its size-1.2 lines are about 7 % short. The same cause moves the pause header
([Circled texts and headers](circled-text.md#q-line-height)). A run record of the screen opened with
`ShowGameStatsInterface` in `level99` should measure the line height and the list's placement; a capture of it
matched the code's placement by eye but has no record.
