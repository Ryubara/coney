# HUD messages: objectives, hints, prompts and scripted bars

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`); runtime claims with
PCSX2 2.9.94 as on [The in-game HUD](hud.md), which this page was split from because that page had grown too
large. The overview, the data tables and the rest of the behaviour stay on [The in-game HUD](hud.md).
Every claim keeps the evidence level it had there.

This page covers the objective, hint, announcement and scripted-bar messages, the action prompts and the instruction arrow.

## Objectives (`HUDSetObjective`)

`HUD_SetObjective(slot, text, mode, silent, ms)` (`0x001dad88`), confirmed (code):

- The objective lines are kept in the **pause menu's checklists** (the menu at `0x0062e790`, `ChecklistMessageHUD.cpp`,
  lists of `0x360` bytes at `+0x20d0` and `+0x2430`, `0x001dd0b8`); mode 0 clears slot `slot` (`0x001dd178`) and sets
  the text in it (`0x001a4d20`), mode 1 clears it, mode 2 marks it (`0x001a5070(item, text, 1)`, inferred: ticks it
  off), and mode 3 sets and marks it with no message. Slot 2 goes to a third list at `+0x2790`. The lists are the
  pause menu's Objectives screen: current, bonus and overview ([Pause menu](pause.md#the-items-screens)).
- **Mode 0, not silent:** **one scroll-in message** is queued (`0x001db000`), its text a single string joined from
  four parts with nothing between them, confirmed (code) at `0x001dae54`-`0x001daf8c`:
  1. the **icon template**: slot 0 `<SIZE 0.8><MONEYFONT><YOBJ></MONEYFONT></SIZE>` followed by one space (47 bytes at
     `0x00556260`), slot 1 `<MONEYFONT><BOBJ></MONEYFONT>` followed by one space (30 bytes at `0x005562a0`). The icon
     tags draw as the money font's rectangles (28 and 22, [MONEYFONT](gui.md#moneyfont)) because the template opens
     `<MONEYFONT>` around the tag alone; slot 1's icon has no `<SIZE>`. **The gap before the heading is that trailing
     space**, drawn at the message's own size (after `</SIZE>`), not the icon's width;
  2. the **colour tag** of HUD colour slot 4 (slot 0) or 6 (slot 1) (`HudColourTag_Get`, `0x0019eee0`, the table
     `0x00600660` that `CfgHUDColor` fills);
  3. the **heading**, HUD string **`0xe5`** (slot 0) or **`0xe6`** (slot 1) (`GlobalString_Get`, `0x0019ee70`);
  4. the objective's **text**, unchanged.

  A text that starts with `<` but not with `<AUTOINDENT` takes its icon template from its leading tag instead
  (`Objective_GetIconPrefix`, `0x001dab30`, the strings at `0x00556210`-`0x00556408`): `<ROBJ_N>`, `<YOBJ_N>` and
  `<BOBJ_N>` give the red, yellow and blue icon as above (the red one at `<SIZE 0.8>` like the yellow) followed by the
  colour tag of slot 3, 4 or 6; `<ARREST_N>`, `<DEAD_N>`, `<FLASH_N>`, `<SPRAY_N>`, `<PEDRAT_N>` and `<CALLGANG_N>` give
  `<MONEYFONT><RARREST></MONEYFONT>` and so on, one space and no colour tag; `<NICON>` gives no icon. The heading is
  still chosen by the slot. Confirmed (code) at `0x001dab30`. The message is queued with the header part empty
  (`0x00556410`), header kind −1, at **(0.025, 0.88)** (x `0x0050ede4` = 0.025, y `0x0050ede0` = 0.9 less 0.02), size
  **0.05**, for **`ms`** milliseconds (default 8,000), with the interface sound cue **`0x11`**.
  **Runtime** (PCSX2 2.9.94, a copy of quick-save slot 1, `level99` checkpoint 3, 2026-10-11; the scenario
  `repo:research/traces/scenarios/objective_messages.toml` calls `HUD_SetObjective` through the call hook): the
  strings read from the running game are `0xe5` = `<SIZE 1.0>New Objective</COLOR><CR3 0.01>`, `0xe6` =
  `<COLOR 6A4183FF><SIZE 1.0>New Bonus Objective</SIZE></COLOR><CR3 0.01>`, colour slot 4 = `<COLOR 806400FF>` and
  slot 6 = `<COLOR 6A4183FF>`. So **the heading string itself ends the line** (`<CR3 0.01>`, a new line 0.01 lower),
  and closes the colour the slot's tag opened: the objective's text follows on the next line in the message's own
  colour. The screenshots show the yellow circled icon, a space, "New Objective" in yellow and the text below it in
  grey, and the blue diamond, "New Bonus Objective" in purple and the text below it.
- **Mode 0, slot 1,** while the game's tutorial hints are on (`W_GameState + 0x56e2`) and not in an Armies of the
  Night level: the first time only (game-state flag `0x40000`), hint `0x15` is queued in the hint box.
- **Mode 2 (completion) queues no message.** It builds the same string as mode 0 for that slot and text, then
  **flushes** the scroll-in queue twice (`HUD_FlushScrollIn`, `0x001b3940`, at `0x001db16c` and `0x001db178`): with
  that string, which ends the objective's own "new objective" message if it is showing and drops it if it is still
  waiting, and with the bare text. Then it ticks the item (`0x001dd1a0`). It reads neither `silent` nor `ms`, and plays
  no cue itself. Confirmed (code) at `0x001db02c`-`0x001db1f4`.
    - **Slot 0:** nothing else. The game's "Objective complete" (built-in announcement 0 below) is not shown by
      `HUD_SetObjective`: its only path is the script binding `HUDSetAnnounceMsg` (`0x001b5b08`, the one caller of
      `HUD_ShowAnnouncement` besides this function), so a level script that wants it shows it itself. Confirmed (code)
      for the callers; that the mission scripts do so is inferred.
    - **Slot 1:** for each player slot whose handle resolves (`W_GameState + 0x224` players, handles from `+0x228`),
      `HUD_ShowAnnouncement(hud, 2)` (`0x001b3cb0`): the built-in announcement 2, with interface cue **`0x14`**. Every
      call targets the one HUD (`0x00600840`), so in one-player play it shows once. Confirmed (code); the per-player
      repeat in split screen is not traced.
    - **Runtime** (same run): after mode 0 for slot 0 at update 5, the message showed from update 7; mode 2 at update 95
      ended it at update 96 and the queue stayed empty (the pool's five entries free), so the level's own hint box came
      back. For slot 1, mode 2 at update 220 ended the "New Bonus Objective" message at update 221 and the announcement
      widget (`+0xe344`) showed "Bonus complete" until update 282: 2,035 ms of game time, its `<DISPLAYTIME 2000>`.
    - The built-in announcements read at runtime (`0x00622e20`; `<LEFT>`, `<DISPLAYTIME 2000>` and a white
      `<MONEYFONT>` icon at 0.8, then one space and the words at `<SIZE 1.0>`): 0 the yellow icon and "Objective
      complete" in `806400FF`; 1 the red icon (`<ROBJ>`) and "Objective complete" in `861A1AFF`; 2 the blue icon and
      "Bonus complete" in `6A4183FF`; 3 "Checkpoint" in `861A1AFF` with no icon; 4 "Objective/Checkpoint complete" in
      `6A4183FF` with no icon.

**Scroll-in messages** (`ScrollInHUD.cpp`, HUD `+0x8dd0`): a queue with no priorities in which **the newest message
cuts the showing one short**. `ScrollIn_Queue` (`0x001c8b08`) appends the message at the tail (nothing is queued when
its pool `+0x548` has no free entry, and a message equal to the one showing, header and text compared as strings, is
dropped) and then ends the showing message (`MessageHUD_Shutdown`, `0x001b9290`, which clears its text `+0x68`).
`ScrollIn_Update` (`0x001c9028`) starts the front waiting message when none shows, taking it out of the list, at its
position with its wrap width (`0x0050ea50` one player, `0x0050ea4c` split), its y centred on the given y less
`0x0050ea58`, and ends it once game time (`GameTimer +0x48`, so not while frozen) is past its start plus its `ms` plus
500 ms; the next one starts on the following update. So messages queued in one update play one after another, but a
message queued later ends the one showing at once. Confirmed (code) at `0x001c8b08`, `0x001c9028`. **Runtime** (the
run above): an objective set at update 330 showed from update 332; a second one set at update 340 replaced it at
update 342 and showed until update 597, 8,509 ms of game time (8,000 + 500 and the update it ends in); completing the
first at update 400 changed nothing, as it no longer showed. `ScrollIn_Flush` (`0x001c94c8`) ends the showing message
when its text equals the given one and drops every waiting message whose text equals it.

## Hints (`HUDSetTutorialText`) {#hints-hudsettutorialtext}

The hint box (`TutorialHUD.cpp`, HUD `+0x8a10`, constructor `0x001cd8e0`, set-up `0x001cd988`) is a markup text widget
([GUI](gui.md#widget-classes)) over a box sprite (`part_page0` rectangle 78, depth 11,000). It shows **one hint at a
time** from a queue ([HUDSetTutorialText](../references/bindings/hud.md#hudsettutorialtext)). Confirmed (code) at the
functions named:

- **Queue.** A pool of **20** entries `{text, priority}` (box `+0x1f0`, free list `+0x294`); the queue (`+0x290`) is
  kept **sorted by priority, lowest number first, first-in first-out within a priority** (`HintBox_InsertByPriority`
  inserts before the first entry with a higher number). When the pool is empty a new hint is dropped. The text is
  kept by pointer, not copied.
- **Queueing** (`HintBox_Queue(box, text, priority)`, `0x001ce3c0`): a nil text clears the showing hint unless the game
  is paused (`MessageHUD_Shutdown`, `0x001b9290`); it does **not** take the next hint, which waits for the next
  `HintBox_Update`. Otherwise the hint is inserted, with **no check for a duplicate** (the same text twice is two
  entries). If a hint is showing and the new priority is **lower** (more urgent), the showing hint is put back in the
  queue with its own priority and cleared, and it **starts again from the beginning** later. Equal or higher numbers
  wait. Last, when no hint is showing (the box was free, or the urgent one just cleared it), `HintBox_Queue` calls
  `HintBox_Update` itself (the box's vtable `+0x34`), so the front of the queue **shows in the same call**, with its
  cue: a script's `HUDSetTutorialText(nil)` then `HUDSetTutorialText(text)` shows `text` at once, in the update the
  script ran. Confirmed (code) at `0x001ce484`-`0x001ce52c`, and confirmed (runtime): every text set on a free box in
  level99's lessons 1-8 was shown in the same tick ([The queue at runtime](#hint-queue-runtime)).
- **Taking the next** (`HintBox_Update`, `0x001cdc80`, each HUD update and also during a freeze, from `GameTimer_Update`
  `0x00145a10`, and from `HintBox_Queue` and `HintBox_Withdraw` above): when no hint shows, the front of the queue
  becomes the text (`MarkupText_SetText`), its priority goes to `+0x3ac`, interface cue `0x15` plays, and the box is
  laid out ([layout](hud.md#hint-box-layout-0x0050eb50)). When a hint shows, it checks the markup's time instead and clears
  an expired hint; the next one is **not** taken in that call, so it shows one update later.
  The queue entry is in `s1` there (`+0x0` the text, `+0x4` the priority byte); `0x001cdf7c`, just after
  `MarkupText_SetText`, is where the `event-hint` hook logs each hint the moment it shows. Confirmed (runtime).
- **How long a hint stays** comes **only from its markup** (`MarkupText_IsExpired`, `0x001bb2f0`; when it returns true
  the text is cleared, vtable `+0x6c` = `0x001b9290`, and the next update takes the next hint):
    - `<DISPLAYTIME ms>`: gone `ms` after it was first laid out, timed on the clock at `0x0050b8b8` (inferred: the
      real clock, as it also times the caption fades during a freeze); in its last 1,000 ms its alpha falls as
      remaining × 0.255 ([GUI markup](gui.md#markup)), and the box's alpha follows (`0x001ce8b8`: 125 × the text's
      alpha / 255);
    - `<FREEZE ms>`: the game is paused for at least 2,000 ms and until `ms` has passed, or cross is released after the
      2,000 ms ([Boot](boot.md#timers)); the hint goes when the game runs again. A last line containing `press` and
      `to continue` is not drawn for the first 2,000 ms and then fades in over 334 ms (`0x001b9600`; matched in
      English only, inferred);
    - **neither tag: it stays** until it is flushed, withdrawn or interrupted by a more urgent hint.

  Disc check (counts only): of the hint strings, `config_strings_en.lua` has 7 `<DISPLAYTIME>` tags and `level99.lua` 8
  (2,500-8,000 ms); neither uses `<FREEZE>`, so most hints stay until the script removes them.
- **Removing:** `HUDFlushTutorialText(p)` (`HintBox_FlushPriority`, `0x001ce748`) removes every queued hint of priority
  `p`, and the showing one when its priority is `p`; 4 removes all. The next hint then waits for the next
  `HintBox_Update`. `HintBox_Withdraw(box, text)` (`0x001ce5c0`) removes one hint by its text pointer: the showing one
  through `HintBox_Queue(box, nil)` (so only when the game is not paused) followed by a `HintBox_Update` call, so the
  next queued hint **shows at once**; a queued one, the first entry with that pointer only. `HUDCheckTutorialText`
  (`HintBox_Contains`, `0x001ce550`) is true while the text is showing or queued. Texts are compared by pointer;
  Lua 4 interns its strings, so a script's equal strings are one pointer (inferred). Confirmed (code).
- **Style:** text colour `0x005fd310` (178, 178, 178, 255) ([GUI](gui.md#colour-table)), markup lines `<CR2>` and
  `<CRM>` enabled; the widget's `+0x1d4` is 0.74 with one player and 0.52 split, or the number after a leading
  `<AUTOINDENT` tag (`0x001bb390`). Read at runtime (PCSX2 2.9.94, copies of quick-save slots 3-8, `level99`): a
  priority-3 hint showing with no end time (`+0x170` = -1), `+0x1d4` 0.74, box height (`+0x3b0`) 0.124.
- **The game's own hints.** `CfgTutorialMessage(id, text)` (`0x001cd888`) fills a table at `0x00622fd0`;
  `HintBox_QueueGameHint(box, id)` (`0x001ce1e8`) queues entry `id` at **priority 2**, and `HintBox_WithdrawGameHint`
  removes it. 19 functions queue them (human, brain, radar and objective code, for example hint `0x15` from
  `HUDSetObjective` above and hint 6 from `Humans_Update`); the two read show each hint once, behind a game-state flag
  and the tutorial switch `W_GameState + 0x56e2` (`HUDEnableGameTutorialText`). Which event raises which id is not
  catalogued.
- **Action-object hints** go in at **priority 0**, the most urgent ([Action prompts](#action-prompts)).

The box is hidden while a scroll-in message, an announcement or a mini-game panel shows ([What hides
what](hud.md#the-huds-frame)); it is drawn by `0x001ce8b8` and hidden by `0x001bb2b0`.

### The queue at runtime {#hint-queue-runtime}

Two plays of `level99` checkpoint 1 (2026-10-11, PCSX2 2.9.94, the state copy `l99-cp1-lesson1` driven by the
`level99-tutorial` course of [Differential playthroughs](../guides/research-workflow.md#differential-playthroughs),
3,600 updates each, through lesson 9's set-up), with the hooks `hint-queue` (`HintBox_Queue`), `hint-clear`
(`MessageHUD_Shutdown` on the box `0x00609250`), `hint-withdraw`, `hint-flush`, `event-hint` (`0x001cdf7c`),
`tutorial-callback`, `anim-dispatch` and `anim-callback-match` of `repo:research/traces/patches/`. Times are taken
from the tick count each hook logs (`0x005104f4`, two ticks an update). Confirmed (runtime):

- **A text set on a free box shows in the same tick.** All `HintBox_Queue` calls (87 and 89 in the two runs) came
  from `HUDSetTutorialText` (`Tutorial_QueueText`, return address `0x001b499c`). Of those with a text (40 and 41),
  all but one found the box empty (a nil just before it, or nothing showing) and were shown within the same call:
  `MarkupText_SetText`'s own clear (`0x001bb26c`), then the `0x001cdf7c` show, at the same tick. The one is `TT_26b`
  below; apart from it the queue never held an entry past the call that queued it.
- **A held hint makes the next wait for its end, plus one update.** `TT_26a` (`<DISPLAYTIME 3000>`) and `TT_26b` were
  set in one call; `TT_26b` stayed queued while `TT_26a` showed. `HintBox_Update` cleared `TT_26a` 91 updates (182
  ticks) after it showed, and `TT_26b` showed on the next update, 92 updates after `TT_26a`, the same in both runs.
- **Duplicates are not dropped.** `P1.StartSnaps` sets the snap text twice in one call (nil, text, nil, text, one
  tick): the box showed it, cleared it and showed it again, with the cue twice. The two entries never coexist in the
  queue, because the first is shown before the second is queued.
- **Nothing else queued.** No `HintBox_QueueGameHint`, no `HUDFlushTutorialText`; `HintBox_Withdraw` ran about
  3,000 times (from `HUD_WithdrawActionHint`, return address `0x001b3e64`, [Action prompts](#action-prompts)), always
  with no text (no action hint was queued), so it did nothing.

So in the original a hint queue backs up only behind a hint with a time (`<DISPLAYTIME>`, `<FREEZE>`) or a more urgent
one, never because two scripts set texts in one update: the last text set in an update is the one on screen at its
end. The order and timing of lessons 5-8 are in [Scripts](scripting.md#level99-hint-timing).

**Coney's implementation** (`repo:src/hud/hint_box.cpp`) follows this:

1. `HintBox::queue` shows the front of the queue at once, with its cue and its `hint` event, when nothing is showing
   after the insert (or after an urgent hint cleared the showing one), so two nil + text pairs in one update leave
   nothing queued.
2. `HintBox::withdraw` of the showing text takes the next hint at once; `HUDSetTutorialText(nil)` and `flush` do not.
3. `HintBox::update` clears an expired hint and takes the next one only on its next call, and it is called once a
   frame, by gameplay after the step's callbacks and before the scheduled calls (where the original's `HUD_Update`
   stands); `Hud::update` leaves the box alone.
4. No duplicate check in `queue`; nil does not take the next hint.
5. Not the box's: a grab's hold ids fire their animation callbacks at the connect
   ([Animation callbacks](characters.md#anim-callbacks)), so `PlayerState`'s grab hint and `P1.Power`'s `TT_24d` come
   in different updates, as in the original ([lessons 5-8](scripting.md#level99-hint-timing)).

The disc test that checks it is `repo:tests/platform/disc_level99_course_test.cpp`: lessons 1-8 with no text left
queued except behind a `<DISPLAYTIME>` hint, lesson 5's grab hint 34 frames before `TT_24d` (the original's 34
updates) and `P1.StartSnaps`'s text shown twice in one frame.

## The tutorial callback (`HUDSetTutorialCallback`) {#tutorial-callback}

Despite its place in the hint box (`+0x3a0`, `0x006095f0`), the callback has nothing to do with the hint text: it is
the combat tutorial's **"player 1 hit someone"** hook. Confirmed (code):

- `HUDSetTutorialCallback(name)` (`0x001b5e90`) stores the name pointer as given; nil stores 0.
- The only reader is `Tutorial_CallCallback(box, animId)` (`0x001ce9a8`): when the name is set and the Lua function
  is found (script slot `+0x4c`, [Scripting](scripting.md)), it is called at once with **one argument, the attacker's
  current anim id** (pushed as an unsigned number, slot `+0x74`; call `+0x8c` with 1).
- Its one caller is the attack-scoring step `0x002653d8(attacker, victim, landed)`, which `Human_ApplyPendingDamage`
  (`0x00265f70`) runs on the victim's pending hit when the attacker is pad-controlled (player record `+0x1b`): with
  `landed` 1 for a hit that lands and 0 for one a block stops (both call the callback). `0x002653d8` returns first when
  the attacker's brain `+0x4` is not 0 or the two are allies (`0x00290230`), then calls the callback only when the
  attacker's player index (`+0x1b0`) is **0**, before it awards the hit's rage ([Combat](combat-rage.md#rage)). The anim
  id is the attacker's record `+0x20` (11 `X1`, 12 `S1`, 13 `XX2`, ...; [Combat](combat/index.md#attacks)).
- The id is read when the **victim** applies the pending damage, not when the hit was dealt. For a hit dealt as a
  clip ends, the attacker has already moved on: the strong grapple's damage is added at the end of 657 and applied as
  the hold 82 starts, so the callback gets **82** (84 from the rear). Confirmed (runtime)
  ([Strong grapple](combat-grabs.md#strong-grapple)).
- So it fires **once per hit, every hit**, for as long as it is set; nothing in the engine clears it after a call. It is
  cleared by `HUDSetTutorialCallback(nil)` and when the HUD is released at `UnloadLevel` (`0x001607b8` →
  `0x001ae980` → `0x001cdc20`, which zeroes `+0x3a0`).

## Announcements and other messages

- **`HUDSetAnnounceMsg(kind, text, flag)`** (`0x001b5b08`): kind **5** sets `text` in the **centred** widget (HUD
  `+0xe150`) at **(0.5, 0.25)** (`0x0050d470`; 0.35 in one other mode); any other kind shows the built-in message
  `kind` of the `CfgAnnounceMessage` table (`0x00622e20`) in the widget at HUD `+0xe340` (`HUD_ShowAnnouncement`,
  `0x001b3cb0`, which also looks up interface cue `0x14` into `+0xe4d4`), at **(0.02, 0.90)** with one player, (0.22,
  0.90) with two (`0x0050d4a0`, `0x0050d4a4`, `0x0050d4a8`). Each call **replaces** the text at once: there is no
  announcement queue. An announcement lasts as its markup says (`<DISPLAYTIME>`): `HUD_Render` clears `+0xe344` when
  `MarkupText_IsExpired` says so; without the tag it stays until replaced. While the `+0xe340` one shows it hides the
  bottom-left texts ([What hides what](hud.md#the-huds-frame)); the centred one hides nothing. `flag` goes to
  `0x00617fe8`. Confirmed (code) at `0x001b5b08`, `0x001b3cb0`, `0x001af010`, `0x001b1688`.
- **Counter panels** (`HUDGetNewPH`, five at HUD `+0xacb0`): laid out each frame by `0x001c3608` from its own
  floats (`0x0050e97c`-`0x0050e994`), filled from the top with the visible panels; the height they take is kept at
  HUD `+0xe044`, and the [scripted bars](#scripted-bars) go below it. Confirmed (code) at `0x001c3608`, `0x001af010`.

## Scripted bars (`HUDEnableBar`) {#scripted-bars}

`HUDEnableBar(kind, on, labels, count, flag, texA, texB)` (`HUD_EnableBar`, `0x001b53b0`) makes one of four kinds
of bar; `HUDSetBarPercentage` (`0x001b5450`) fills it and `HUDSetBarProperty` (`0x001b54f0`) recolours it. The
binding's arguments are in [HUDEnableBar](../references/bindings/hud.md#hudenablebar). Kinds 0, 1 and 3 use the
**generic bars**: four slots of `0x3f0` bytes at HUD `+0x9970` (`0x0060a1b0`), each a `HudBar` meter (the class of
the [rage meter](hud.md#the-rage-meter)) plus a markup-text label; kind 2 is a separate **chase gauge** at HUD `+0xa940`
(`0x0060b180`). Confirmed (code) at the addresses given. The positions below are GUI coordinates
([Coordinates](hud.md#coordinates)) in the default video mode (device flag `0x01` only); sizes of the generic bars are
GUI units, sizes of the gauge's sprites overlay units ([GUI](gui.md#widget-classes), `BaseWidget`).

**Kind 1: the labelled bar** (`HUD_EnableLabelledBar(on, labels[0], slot 0, rightColumn 1)`, `0x001b4c98`), used for
a boss's or an object's health. On creates generic bar slot 0 once (a second call while it exists does nothing); off
calls its `Shutdown` (`0x001c2228`).

- **Size:** 0.22 × 0.025 times the HUD scale (HUD `+0x19124`, `0x00619964`: 1.0, or 0.78 when the device flags have
  `0x02` without `0x04`; reset to 1.0 at the start of every `HUD_Update`, `0x001af010`), so 126 × 10 pixels on the
  640 × 448 screen.
- **Where:** `HUD_Update` re-lays every visible generic bar each frame in a column at the right: slot `i`'s **bar
  has its right end at x 1.0** (`0x0050d514`) and is centred on y 0.26 + 0.08 × `i` (`0x0050d518`, `0x0050d520`),
  drawn right to left (HudBar `+0x5c` = 1, so the fill grows leftward from the right end); its **label is
  right-aligned at x 1.0, 0.035 above the bar** (anchor x 0.96, `0x0050d510`, plus the label offset (0.04, -0.035)
  at bar `+0x3d0`/`+0x3d8`, `0x0050e960`/`0x0050e964`). The whole column moves down by the height of the visible
  [counter panels](#announcements-and-other-messages) (HUD `+0xe044`, written by `0x001c3608`) and by 0.07 while the
  stopwatch shows (HUD `+0x6858`); each hidden slot below moves the later ones up by 0.07. So slot 0 alone sits at
  pixels x 481-607, y 126, its label's right edge at x 607. In a two-player split (two players and two camera
  views, `0x001b1640`) the second half of the bars goes to a left column (label left-aligned at x -0.055, bar left to
  right from x -0.015). Other video modes use other column values, written into the same globals by `0x001af010`.
  Confirmed (code) at `0x001af010`, `0x001c22e8`, `0x001c1f80`.
- **Look:** the four rectangles of the rage meter: sprite word `0x30036`, batch 3 (`part_page0`) rectangles 54
  (body), 55 (fill), 56 (left cap) and 57 (right cap), drawn as on [The rage meter](hud.md#the-rage-meter) without the
  capacity strip (capacity 0) and without flashing (`HudBar_Draw(0.25, bar, 0, 0)` from `0x001c2470`). Background
  **(64, 64, 64, 255)**. The **fill colour follows the fill**: `HudGenericBar_SetGradient` (`0x001c2410`) sets HudBar
  `+0x60`, and each draw sets the fill colour to red **(255, 16, 16)** × (1 − fill) + green **(115, 183, 11)** × fill
  (`0x0017a258`, per channel), so a full bar is green, a half one (185, 100, 13), an empty one red. It starts
  **full** (fill 1.0).
- **Label:** a markup text widget (bar `+0x1b0`) with the string `labels[0]`, size 1.0 (0.9 in the `0x02`-without-
  `0x04` mode), font slot 3, colour `0x005fd310` (178, 178, 178, 255), right-aligned (`0x001c2290(bar, 1)`).
- **Fill:** `HUDSetBarPercentage(1, fill, fill2, index, value2)` stores `fill` clamped to 0-1 in slot `index`
  (`HudGenericBar_SetFill`, `0x001c2530`) and, like kind 2, also writes the chase gauge (harmless while it is off).
- **Quirk:** in a two-player game each creation adds 0.22 to the static y `0x0050d548` that the set-up uses before
  the first re-layout; nothing resets it. Confirmed (code); it is overwritten on screen by the per-frame layout above.

**Kind 2: the chase gauge** (`HUD_EnableGaugeBar(on, texA, texB)`, `0x001b4f00`; class vtable `0x00539600`): a
marker that slides along a track between two values, green in the middle and red at the ends. On sets it up once
(`ChaseGauge_Setup`, `0x001a3720`); off releases it (`0x001a38d8`).

- **Texture:** sprite words are `sheet << 16 | rectangle` of the [sprite-sheet table](gui.md#sprite-sheet-table-chunk-0x4d-particle-page-header);
  the defaults (`texA` `0x1a0004`, `texB` `0x1a0000`) and the fixed marker `0x1a0007` are sheet record 26,
  **`chasebar`**: one 256 × 64 texture with 8 rectangles: 0 and 1 are 144 × 13 texel bars (two tracks), 2, 3, 5 and
  6 icons of about 23-29 × 31-38 texels, 4 an icon of 40 × 33 and 7 the 26 × 29 marker. Confirmed (runtime), PCSX2
  2.9.94 over PINE: sheet-table record 26 has name hash `0x7b836eda` (the CRC-32 of `chasebar`, a string on the
  disc) and size 18,016; the rectangles from the disc (corroboration).
- **Sprites** (three `BaseWidget`s, untinted, anchored at their centre), with the base position (0.8, 0.25)
  (0.47 in a two-player game, `0x0050d544`) and `size` 0.03 (gauge `+0x360`):
    - the **track** `texB` at the base, height `size` (0.03 overlay units, about 12 pixels; width from the rectangle's
      aspect: about 134 pixels for rectangle 0), depth 8,000, so drawn under the rest;
    - the **end icon** `texA` at (base x + 0.115, base y), height 2 × `size`, depth 11,000;
    - the **marker** (`0x1a0007`) at (base x − 0.115 + f × 0.23, base y + 0.04), height 2 × `size`, depth
      11,000, where the 0.23 is 2 × 3.8333 × `size` and **f = 1 − (value − min) / (max − min)**: `value` =
      `max` puts it at the left end, `value` = `min` at the right end by the icon.
- **Marker colour:** red (255, 16, 16) and green (115, 183, 11) mixed by t = 1 − 2 × |0.5 − f|: green at the
  centre, red at both ends.
- **Values:** set-up gives max 60 (`+0x348`) and value 30 (`+0x340`); min (`+0x344`) is not set there (0 until a
  script sets it, inferred from the zeroed static object). `HUDSetBarPercentage(2, fill, fill2, index, value2)` sets
  value = `fill`, max = `fill2` and min = `value2` (`0x001b4fc0`); nothing is clamped.
- **Drawing:** `ChaseGauge_Update` (`0x001a3990`) runs from `HUD_Update` while the gauge exists (HUD `+0xa94c`) and
  places the sprites once their sheet is loaded (`0x001a3930`); `ChaseGauge_Render` (`0x001a3c00`, from `HUD_Render`)
  draws track, marker, icon.

Kind 0 (a red (200, 30, 30) bar, `0x001b4ba0`) and kind 3 (a stack of up to four labelled bars, `0x001b5008`) are
described with the binding. **`HUDSetBarProperty(index, gradient, rgba, width)`** sets slot `index`'s fill colour
(HudBar `+0x28`), its gradient flag to `gradient` and its width (`0x001c22b0`, × 0.7 with device flag `0x02`): with
`gradient` true the red-to-green colour replaces the given one at the next draw, so only `false` keeps a custom
colour. Confirmed (code) at `0x001b54f0`, `0x001a1138`.

## Action prompts {#action-prompts}

The "what can I do here" text at the bottom of the screen: **one prompt per player** (HUD `+0x6b40` and `+0x70d0`,
`0x590` bytes each), a scroll-in text widget (base `0x001e6e98`, vtable `0x00539270`) plus an optional two-frame
icon sprite at `+0x480`. Confirmed (code) unless marked:

- **Set-up** (`ActionPrompt_Setup`, `0x0019ef80`, from the HUD's init `0x001adb60`): position x **0.5**, base y
  `+0x464` = **0.86** (read at runtime; the code compares it with 0.86, 0.78, 1.0 and 0.8 for the other modes), size
  0.06, colour **(128, 128, 128, 255)**, font slot 3 (`part_page0`), not visible; `+0x468` the player.
- **Place** (`ActionPrompt_Update`, `0x0019f528`): one player: x 0.5, centred; y = base + the raise `+0x46c` when the
  base is 0.86, 0.78, 1.0 or 0.8, else the base alone (the clubhouse's 0.125, `HUDEnableClubActionText`, takes no
  raise); the icon's y the same with the raise `+0x470`. Two players: x 0.33 or 0.66 (`0x0050cfcc` / `0x0050cfd0`,
  0.25 / 0.74 in 16:9).
- **Text, alignment and size.** The widget itself is left-aligned: its alignment byte `+0x17c` is 0 from the constructor
  (`0x001b8f98`), and nothing on the prompt's path sets it (runtime: 0 for both prompts). The centring comes from the
  **text**: every HUD string the code picks for the prompt (strings 0, 1, 4 and 9) opens with `<CENTER>`, which centres
  the line on the pen at x 0.5 by its measured width ([Markup](gui.md#markup)); then `<COLOR B2B2B2FF>` and `<SIZE
  1.0>`. Runtime (PCSX2 2.9.94, `level99`, the string table `0x00600048`): 19 of the 397 HUD strings open with
  `<CENTER>`. Size: `MessageHUD_Setup` (`0x001b9090`) gets the size 0.06 (an overlay height, from the HUD's init) and a
  font scale of 1.0 (`Font_Size(1.0)` into `+0x1c0`); font slot 3 (`part_page0`), since the last argument (0 from the
  HUD's init) is not 6. So the text is drawn at size 0.06 × 1.0 in colour (178, 178, 178, 255), the tag's, not the
  widget's (128, 128, 128, 255). Confirmed (code) for the widget, confirmed (runtime) for the strings' tags; the prompts
  from action objects (`+0x10`) and talkable humans were not checked.
- **No separate icon without the cycle.** With the cycle off (`+0x45c` = 0) `ActionPrompt_Update` only sets the text's
  visibility from `+0x460`; the icon widget `+0x480` is neither placed nor updated, and `ActionPrompt_Render` draws it
  only while `+0x45c` is set. The box under it is style 1 (`BoxedText_SetupBox(…, 1)`, `0x001e6f50`), which sets up **no
  backdrop** (`+0x204` = 0) and **no box icon** (`+0x208` = 0), so `BoxedText_Render` (`0x001e7750`) draws the text
  alone. The button on screen is part of the text: the strings wrap a button tag in `<MONEYFONT>` (`<ST>` in 0, 1 and 9;
  `<SDL>`, the animated stick, in 4), drawn as a glyph of the font in line with the words. A text that parses as a
  number is instead a **sprite word**: `ActionPrompt_SetText` hands it to `ActionPrompt_SetNumberText` (`0x0019f128`) →
  `MessageHUD_SetIcon` (`0x001bb1e0`), which drops the text and shows that sprite in the widget's own box `+0x70`.
  Confirmed (code).
- **The button glyph** ([MONEYFONT](gui.md#moneyfont)): the context prompts start with `GSTRING.ACTIONBUTTON`,
  `<MONEYFONT><ST></MONEYFONT>` and a space, concatenated in front of the prompt's words (`config_strings_*.lua`; disc
  check, counts only). `<ST>` is the character `n`; inside `<MONEYFONT>` the prompt's font slot 3 (`part_page0`)
  draws it from rectangle 11 + (`'n'` − `'0'`) = **73, the triangle button**, so the original shows "(triangle) Give
  Weapon". The revive string 4 instead uses `<BIGFONT><MONEYFONT><SDL>`: `big_font` rectangle 256 + (4 − 1) = 259, the
  animated stick picture. Confirmed (code) at `0x001b9600`, `0x00179958`, `0x00179c30`.
- **The raise** (`ActionPrompt_SetRaise`, `0x0019f430`, each HUD update) keeps the prompt clear of the texts below it:
  while the hint box shows (box `+0x3a8` set and no scroll-in), raise = 0.05 (`0x0050d500`) − the box's height (`+0x3b0`);
  otherwise −0.02 (`0x0050d504`) − the showing scroll-in's height (scroll-in `+0x578`, 0 when none). A prompt of more
  than one line moves up a further 0.025 (`0x0050cfb4`) × its height in lines. **Runtime check** (PCSX2 2.9.94, quick
  save slot copies, `level99`): with a hint box 0.124 high the raise was −0.0738 = 0.05 − 0.124.
- **Its text** is chosen each update by `HUD_Update` (`0x001af010`) for each player human (game state `+0x224` of them,
  handles at `+0x228`) who is free to act, confirmed (code): not knocked out; not cuffed, unless upgrade (6, 15) is
  unlocked and his player holds a key (item 6); not in a scene state, not in state `0x100000000`, with a player record;
  not mugging, tagging, in a button mini-game, being mugged or in the mug meter; not aiming a throw (record `+0x1be` =
  −1 with the move `Human_MoveThrowAim`); his mini-game mode (player state `+0x46`) not 2 or 3 (lock pick, stereo); his
  action not `0x15` (uncuffing); and command `0xa` (triangle) enabled. Each update starts with no text for either
  player, so a player who is not free has his prompt cleared (`HUD_ClearActionPrompt`). Then, in this order, the first
  that applies:

    1. **Mug or interrogate**: `Mug_CanMugVictim` (`0x00225ff0`) holds and the held human (`+0xc4`) resolves: HUD string
       1 (mug) when that human's `+0x5a0` is 0, else string 0 (interrogate).
    2. **Revive**: `Human_FindRevivableNear` (`0x00279078`) finds a human and the **player or the downed human** carries
       a flash: HUD string 4. Details below.
    3. **The action object** in reach (`0x00240888`): its prompt `+0x10`, and its hint `+0x14` is queued in the hint box
       **at priority 0** once (remembered at HUD `+0x177bc` per player, withdrawn by `0x001b3e20` when the object or its
       hint changes or none is in reach).
    4. Else a **talkable** human within 1.5 m (`ActionPrompt_FindNearbyHuman`, `0x001acd60`): see [the talk
       prompt](#talk-prompt).

    The strings are `GSTRING.HUD` entries ([Strings](gui.md#strings)); the action objects and their kinds:
    [Crimes](crimes.md#triangle).
- **The revive test**, confirmed (code) at `0x00279078`, `0x0029cc78`, `0x0029ca28`, `0x00278fa0`:

    - **Who is searched**: `Humans_FindAhead` (`0x002274a8`) lists up to 60 humans, the player excepted, within **3 m**
      in 3D of the player's position (every direction; humans whose slot flag in `0x00715390` has bit 1; inferred:
      present); with more than 16 the 16 nearest are kept.
    - **The filter** `Human_IsRevivableBy(player, him)` (`0x00278fa0`): he is friendly to the player
      (`Human_IsFriendly`, `0x00222a90`) or in the same gang (brain `+0x20c`); knocked out (`0x00227dd8`); not cuffed
      (`0x00223b70`); not in state `0x80000000` (`0x00227e18`) or `0x100000000` (`0x00227eb0`); and **revivable**, human
      flag `+0xe0` bit `0x4` (`Human_Init` sets it on every player and every Warrior, brain types 0 and 3;
      `Human_MakePlayer` on a player too; scripts set or clear it with `HuSetRevivable`; [Combat: human
      flags](combat/index.md#human-flags)). Confirmed (runtime) on `level80`: beside a knocked-out Vermin with a flash,
      string 4 showed ([Combat: revive at runtime](combat-reactions.md#revive-runtime)).
    - **Sight, no facing**: each candidate must also pass `Human_HasLineOfSight(player, him)` (`0x00222288`: a ray at
      1.7 m to his 1.7 m, else to his 1.0 m; it passes fences, railings and glass, [AI: sight](ai-fights.md#sight)). The
      list's field-of-view test is only consulted beyond 3 m (squared distance over 9), which the 3 m search never
      yields, so **facing does not matter**.
    - **Which one**: the candidate nearest the player's position (`HandleArray16_NearestHandle`).
    - **The flash**: the count of item 1 in the inventory at `W_GameState + 0x480` (`Inventory_Count`, `0x0041e420`) for
      the **player's** panel index (human `+0x380`), or, failing that, for the **downed human's** `+0x380`. An inventory
      belongs to a player panel (0 or 1) and the count is 0 for any other index, so the downed human's flash counts only
      when he is the other player (co-op); an AI crew member (`+0x380` = −1) has none.
    - **Order**: after the mug test and before the action object, under the same "free to act" gates; a player who is
      himself knocked out gets no prompt.
    - **No flash, no prompt beside a downed Warrior.** Without a flash the revive step fails and the steps after it
      run, but none of them offers a downed Warrior anything: the talk prompt (step 4) needs his `+0x1b2`, and
      `WarriorBrain_Think` (`0x003052f0`, [the swap prompt](ai-brain-types.md#think-warrior)) sets it only while
      `HumanRecord_AreActionsBlocked` (`0x00228228`, state mask `0x7bf9e9f7ff0`) is false for him and his gait
      (`+0x1a8`) is 0. That mask holds `0x40000`, the knocked-out state (`Human_IsKnockedOut`, `0x00227dd8`, tests
      exactly `0x40000`), so a knocked-out Warrior's swap prompt ("Give Weapon", strings `0xc`-`0xe`) is cleared on
      his next think. So beside a downed Warrior the original shows **the revive prompt with a flash, else nothing**
      (or an action object's prompt in reach). Confirmed (code).
- **Showing:** no text hides the prompt (`0x001b2490`); a new or changed text restarts it (`ActionPrompt_SetText`,
  `0x0019f1b0`; a text that parses as a number goes through `0x0019f128`). A prompt naming `Spray`, `Flash`, `Blades`
  or `Give Mon...` also wakes the player panel ([Activity](hud.md#the-player-panel)).
- **The cycle animation** (`HUDTurnOnActionCycleAnim(framesPerIcon, seconds, blinkFrames, iconA, iconB, player)` →
  `ActionPrompt_StartCycle`, `0x0019f270`), confirmed (code) unless marked:
    - **The sprites**: `iconA`/`iconB` go to `+0x440`/`+0x444` as sprite words of the icon widget `+0x480`, which the
      set-up gives batch 3 (`ActionPrompt_Setup`, `0x0019ef80`), so they are **`part_page0` rectangles**. The scripts
      (disc scan of the compiled scripts) pass **73**, the triangle button, in `level51_chapter8` and `level54`, and
      **79**, the cross button, in `level51_chapter8`, `level51_chapter9`, `level81_chase` and `level95_workout`
      (`level84_chase` computes them); every call passes the **same word twice**, so the swap never shows and only the
      blink does.
    - **Size**: the start sets the icon's size to its first argument in overlay units (0.1 from the binding), square
      for these 34 × 34-texel buttons, and **hides the text** (`BoxedText_SetVisible(prompt, 0)`).
    - **Place**: each `ActionPrompt_Update` (`0x0019f528`) puts the icon's **centre at the prompt's own anchor**: x 0.5
      for one player (two players: `0x0050cfcc`/`0x0050cfd0`, or 0.5 for both while either player human is cuffed
      or knocked out), y = the base 0.86 + the raise `+0x470` (the raise before the multi-line lift that `+0x46c`, the
      text's, adds). So the icon stands where the text's line would be, centred on it. While the cycle is on, the
      update no longer sets the text's visibility from `+0x460`, so the text stays hidden until `ActionPrompt_SetText`
      restarts it with a new owner or text (it would then overlap the icon; not seen, inferred).
    - **Rate and blink** (`ActionPrompt_UpdateCycle`, `0x0019f328`, once per prompt update; units are **HUD updates**,
      one per game frame, inferred): a counter `+0x44c` swaps the word index `+0x450` whenever it reaches a multiple
      of `framesPerIcon` (`+0x448`; the first update already swaps, and 0 traps on the division), so each word lasts
      `framesPerIcon` updates (scripts: 4, or 3 in `level54`). With `blinkFrames` (`+0x458`) non-zero, the icon is on
      (active `+0x488`) for `blinkFrames` updates and off for as many (6 and 6, or 2 and 2 in `level54`); with 0 it
      stays on. The `seconds` argument is ignored ([HUD bindings](../references/bindings/hud.md#hudturnonactioncycleanim)).
    - **Drawn** by `ActionPrompt_Render` (`0x0019f850`) under the text, in a two-player game or for player 0;
      `HUDTurnOffActionCycleAnim` (`0x0019f320`) clears `+0x45c`.
- **Drawn last** in the HUD, and not while an announcement or a mini-game panel shows ([The HUD's
  frame](hud.md#the-huds-frame)).
- **Mini-game widgets:** the lock-picking dial (HUD `+0xf420` + player × `0x540`), the stereo theft's (`+0xfea0` +
  player × `0xb10`) and the mash meter (`+0xebc0` + player × `0x430`), [Crimes](crimes.md).

### The talk prompt {#talk-prompt}

Step 4 of the prompt's text, `ActionPrompt_FindNearbyHuman` (`0x001acd60`). Confirmed (code) at `0x001acd60`,
`0x0021d530`-`0x0021d570`, `0x00303404`, `0x00305bfc`, `0x00384aa0`, `0x00384b30` unless marked:

- **Who is searched**: `Humans_FindAhead` (`0x002274a8`) lists up to 100 humans within **1.5 m** of the player's
  position (the same search as the revive test); the **first** in the list's order that passes is taken.
- **Talkable** means both: the human's byte **`+0x1b2`** is set, and his brain type (brain `+0x04`, [AI](ai/index.md#types))
  is **0** (a player: the other player in co-op) or **3** (a Warrior). The human's interface at `+0x70` (vtable
  `0x0053f020`) also asks "has a message-0 handler or `+0x1b2`" (slot `+0x50`, `0x0021d570`), which `+0x1b2` already
  answers. Gang leaders, bums, dealers and shopkeepers also get `+0x1b2` from their goals (`InfoTactic_Seat`,
  `BumLogicGoal_Setup`, `DealerGoal_Process`, `ShopkeeperGoal_Process`, `AddressTactic_PushApproach`), but their brain
  types (2, 4, 5, 6) fail this test; their prompts come from the kind-4 and kind-5 context records of step 3
  (`InfoTactic_Seat` and `BumLogicGoal_Setup` register kind 5 with HUD strings 8 and 10).
- **Who sets it**: the brains, every think; no Lua binding. `WarriorBrain_Think` (`0x00305bfc`) and `PlayerBrain_Think`
  (`0x00303404`) write `+0x1b2` = 1 exactly when [the swap prompt](ai-brain-types.md#think-warrior)'s conditions hold
  (else 0), and then write the text of `GSTRING.HUD` string **`0xc`** (both hold an object), **`0xd`** (he holds) or
  **`0xe`** (the player holds) straight into the human's message-handler component (human `+0xe8`): `+0x7c` = that text,
  `+0x78` = 0.
- **Its text**, in order: the component's `+0x78` read as a `GSTRING.HUD` id when it is **1-388** (slot `+0x20`,
  `0x0021d530`, message 0 only); else the text at `+0x7c` when not null (slot `+0x28`, `0x0021d550`); else HUD string
  **9**. `+0x78` is written only by `ScriptHandler_SetSlot` (`0x00384aa0`), whose one caller, `SetMsgHandler`, passes 0,
  and by the brains above (0), so the id branch is never taken (inferred: no other writer found). `+0x7c` is the swap
  text, or the `prompt` of [`SetMsgHandlerEx`](../references/bindings/script.md#setmsghandlerex)`(human, 0, callback,
  prompt)` (`ScriptHandler_SetSlotEx`, `0x00384b30`, which also zeroes `+0x78`); a Warrior's brain overwrites that text
  whenever it sets the swap prompt.
- **In practice** the talk prompt is the **swap-weapons prompt** of a Warrior (or the co-op partner) beside the
  player, strings `0xc`-`0xe`; triangle then sends the event-0 prompt (`WarriorBrain_OnPrompt`, `0x00306040`).

## The instruction arrow (`HUDEnableInstArrow`)

A sprite widget at HUD `+0x134a0`: rectangle 10 of `hud_minigames` (batch 11), size 0.06, colour `(191, 191, 191,
255)`, placed at `(x, y)` and turned by `angle` (`0x001b70e8`). It **bobs** along its direction: a step counter runs
from 0 to 10 frames (`+0x100`, `HUDSetInstArrowAnimSpeed`) by 2 per frame and back by 0.5, and the arrow is offset by
step × (sin angle, −cos angle) / 20 / 10 (`0x0050d61c` = 20). Confirmed (code) at `0x001b7330`; the sprite and
size confirmed (runtime). The enable flag is HUD `+0x134a4`.
