# Crimes and context actions

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`), static analysis only
(Ghidra), with the disc's compiled scripts read through `coney-tools`' Lua walker for string ids. No runtime claims.

## Purpose

What the player does with **triangle** besides grabbing: the **context actions** (pick a lock, steal a car stereo,
uncuff a partner, mug, tag a wall, take an item), the timed **mini-games** behind them, what each one gives and what
it costs, and how the game turns these into **crimes**, **wanted** time and police. The pieces already documented are
linked rather than repeated: the mugging's stick game and the stereo's stick rotation are on
[Combat: mugging](combat.md#mugging) and [Combat: the stereo theft](combat.md#stereo-theft); the crime report and the
police response on [AI: crimes](ai.md#crimes) with the types in [Crime types](../references/crime-types.md); breakable
glass and doors on [World objects](objects.md); store flags on [World flags](flags.md#groups); the inventory and the
scores on [Inventory, unlockables and statistics](player-state.md) and [Statistics](../references/statistics.md).

## Original structure

No source file names this code; it sits with the combat code after `Human/cns/cnsplayertag.cpp` ([Source
map](source-map.md)). Names are ours.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x002811f0` | `Player_TriangleAction` | triangle pressed: a key for a cuffed partner, a tag in progress, then the context action, then a pick-up | confirmed (code) |
| `0x0024d530` | `ContextAction_Use` | starts the action of the human's current context record (+`0x660`) | confirmed (code) |
| `0x00417ca0` / `0x00418150` | `ContextActions_Register` / `ContextActions_Pick` | the registry of context records; the per-update choice | confirmed (code) |
| `0x0022dc40` / `0x0022e400` / `0x0022d628` | `MiniGame_Start` / `MiniGame_End` / `MiniGame_Abort` | dispatch by mini-game mode | confirmed (code) |
| `0x00255f08` | `MiniGame_Update` | each update: success, failure or the meter | confirmed (code) |
| `0x0027e6d8` | `Player_UpdateTheft` | each update: the mode's input ([Combat](combat.md#stereo-theft)) | confirmed (code) |
| `0x0022d790` / `0x0022d908` | `LockPick_Start` / `LockPick_End` | the lock-picking set-up and outcome | confirmed (code) |
| `0x002878b8` | `LockPick_JudgePress` | a cross press against the dial | confirmed (code) |
| `0x001b8d38` / `0x001b8530` / `0x001b7a18` | `LockPickDial_Judge` / `_Draw` / `_SetDifficulty` | the HUD dial (HUD `+0xf420` + player × `0x540`) | confirmed (code) |
| `0x0022dd98` / `0x0022e020` | `StereoTheft_Start` / `StereoTheft_End` | the stereo's set-up and outcome | confirmed (code) |
| `0x00225ff0` | `Mug_CanMugVictim` | whether the held human can be mugged | confirmed (code) |
| `0x002856b8` | `Player_UpdateMugging` | the mugging ([Combat](combat.md#mugging)) and its payout | confirmed (code) |
| `0x00238db0` | `Human_Tag` | `HuTag`: starts tagging | confirmed (code) |
| `0x00273c60` / `0x0022e848` | `Tag_UpdatePlayer` / `Tag_End` | the player's tag each update; its end | confirmed (code) |
| `0x002741d8` / `0x002748a8` / `0x00274710` | `TagGame_Init` / `_Update` / `_SpendPaint` | the tag's stick game | confirmed (code) |
| `0x00412dc8` | `Item_TakeOwned` | taking an owned item: witnesses, the shoplifting score | confirmed (code) |
| `0x001698f0` / `0x00169878` | `Gang_UpdateWanted` / `Gang_SetWantedTimer` | the gang's wanted timer | confirmed (code) |
| `0x0041c028` | `Crime_UpdateLevel` | the crime level's decay | confirmed (code) |

## Data

### Context records {#context-records}

The registry (`0x0059719c`, made at `0x00417b10`) keeps six lists, one per **kind**; a record is `{+0x00 object,
+0x04 kind, +0x08 next, +0x0c previous, +0x10 prompt text, +0x14 a second value, +0x18 user}`. Objects, flags and
humans register through a virtual slot (objects keep the record at `+0x130`, flags at `+0xe0`, humans at `+0x664`).
Confirmed (code) at `0x00417ca0`:

| Kind | What | Prompt (`GSTRING.HUD` id) | Reach (`CfgActionDistance`) | Evidence |
| --- | --- | --- | --- | --- |
| 0 | a handcuffed human | 2 (uncuff), or 3 (uncuff yourself) for a player who owns upgrade (6, 15) and holds a key | 2 m in `global.lua` | confirmed (code) |
| 1 | an object or flag with its own text: `SetMsgHandlerEx` with message 0 on a world object ([Scripts](scripting.md#message-handlers)) | the caller's text, and a second value at `+0x14` | 1.1 m | confirmed (code), runtime |
| 2 | a pickable lock (a door) | 15 (pick lock) | 2 m | confirmed (code) |
| 3 | a car stereo | 16 (steal) | 2 m | confirmed (code) |
| 4, 5 | objects with their own text | the caller's text | 1.75 m, 1.5 m | confirmed (code); who registers them is not traced |

The reaches are stored squared at `0x00514878 + kind × 4` (`CfgActionDistance`); the executable's defaults are
2, 1.1, 2, 1.5, 1.5, 1.5 m. The prompt texts are `GSTRING.HUD` strings ([Text labels](../references/text-labels.md#gstring)),
fetched by number through `0x0019ee70`.

### The mini-game record {#mini-game-record}

The fields `+0x40`-`+0x4c` of the per-player record that `0x00222b18` returns ([Combat](combat.md#stereo-theft)
gives its address), shared by every timed action; confirmed (code):

| Offset | Meaning |
| --- | --- |
| `+0x40` | s16 meter: the mash meter (modes 1, 4) or the stereo's |
| `+0x42` | s16 lock picking: good presses in a row; −1 asks to abandon |
| `+0x44` | s16 lock picking: perfect presses in a row |
| `+0x46` | u16 **mode**: 0 none, 1 uncuff mash, 2 lock picking, 3 car stereo, 4 a mash on an object, 5 `HuButtonMiniGame` |
| `+0x48`, `+0x4c` | the stereo's angle and stage ([Combat](combat.md#stereo-theft)) |

While a mode runs, human state `0x4000000` is set ([state flags](combat.md#state-flags)). Combat's "theft mode 1"
is the **uncuff** mash and "mode 2" is **lock picking**. No code was found that sets mode 4 (its start
`0x0022df88` and end `0x0022e330`, which marks the object `0x8000` and calls `0x0024c560`, are reached only through
the dispatch).

### Human fields {#human-fields}

| Where | Meaning | Evidence |
| --- | --- | --- |
| state `0x2000000` | **tagging** (`HuIsTagging`); the other state bits are on [Combat](combat.md#state-flags) | confirmed (code) at `0x002238c0` |
| `+0x250` / `+0x254` | the pocket: item id and count (`HuPutItemInPocket`) | confirmed (code) |
| `+0x257` | an object name; anything but `none` counts as something to give | confirmed (code) at `0x00225ff0` |
| `+0x36c` | the tag object being sprayed | confirmed (code) at `0x00238db0` |
| `+0x370` | money carried (0-999) | confirmed (code) |
| `+0x3ba` | 1 once the current tag is finished | confirmed (code) |
| `+0x590`-`+0x59c` | the four interrogation lines; `+0x5a0` the interrogation callback (`HuSetInterrogation`) | confirmed (code) |
| `+0x5a4` | the mug callback (`HuSetMugCallback`) | confirmed (code) |
| `+0x5b0` | may be mugged (`HuSetMug`) | confirmed (code) |
| `+0x660` | the current context record | confirmed (code) at `0x00240888` |
| `+0x19a` / `+0x19c` | may comment on a tag (`HuEnableTagDone`) / the tagger's crew comments (`HuEnableTagCheer`) | confirmed (code) |

## Behaviour

### Triangle and the prompt {#triangle}

**Each update**, for a player human, the context record nearest by kind is chosen (`ContextActions_Pick`,
`0x00418150`, from `0x0023fea8`): the current record is kept while it still passes, otherwise the lists are tried in
kind order 0-5 and the first record that passes wins. A record passes when its object is within the kind's reach in
the ground plane and its height is within 1.5 m of the human's waist (feet + 1.0), it has no other user, and for a
stereo (kind 3) the stereo is not below the human's feet. Confirmed (code) at `0x00417ed0`, `0x004180b0`.

**Triangle pressed** (command `0xa`, [Commands](combat.md#commands)) runs `Player_TriangleAction` before a climb is
tried, confirmed (code) at `0x0027c120`, `0x002811f0`:

1. Nothing while a move or a state blocks it (state mask `0x7bf9e9f7ff0`, record `+0x08` mask `0x2fefefff`).
2. A human with no player number standing by a cuffed player uses a key (item 6) if either holds one
   (`0x00260fd0`).
3. A tag in progress (a tag spot, a particle object with class bit `0x10`, at `+0x36c`) keeps the press.
4. `ContextAction_Use` (`0x0024d530`) by the record's kind: **0** frees the cuffed human (`0x00260ca8`, the mash of
   mode 1; with a key, at once); **2** sets mode 2 and plays 687 `ANIM_LOCKPICK_INTRO`; **3** sets mode 3 and plays
   683 `STEREO_STEAL_INTRO` (both through `0x002789d0`, which turns the human to the object and starts the
   mini-game); any other kind hands the press to the object's message handler (its `+0x44`, message 0 with the human
   as the subject); a true result ends the press. Confirmed (code), and at runtime for a kind-1 bat.
5. Otherwise the pick-up search (`0x0024d810`, objects within 1.5 m; only while `0x005109a0` is set). It too sends
   message 0 to every object it gathers, before its filters, and a true result ends it; then it takes one
   ([Breakables](combat.md#breakables), [A bat in hand](combat.md#bat)), or, with something in hand and nothing to
   take, drops it (`0x00257f38`).

**The prompt** (two text widgets per player at HUD `+0x6b40`, `0x590` each; [HUD](hud.md#announcements-and-other-messages))
is chosen every frame by `HUD_Update` (`0x001af010`), confirmed (code): nothing while the player is mugging, mugged,
tagging, in a mini-game of mode 2 or 3, in a hold (`0x18000000000`), uncuffing (action `0x15`) or a few other
states, or when command `0xa` is not available (`0x00147738`). Otherwise, in order: holding a human who can be
mugged → `GSTRING.HUD` 1 (mug), or 0 (interrogate) when the human has an interrogation set; a partner who can be
revived while either holds a flash → 4 (revive); a context record → its text (`+0x10`; whether the second text is ever shown is not traced); else a nearby human's own talk prompt
(`0x001acd60`). The panel's activity test then matches the prompt's text against the dealer prompts
([HUD](hud.md#the-player-panel)). The prompt sits 0.04 above its place in the default video mode (0.02 in the others) and
rises with a scroll-in message (`0x0019f430`).

### Lock picking {#lockpick}

A door made pickable by `SetDoorPickable` (message `0x22` value 10; [Doors](objects.md#doors)) registers a kind-2
record. Triangle at it starts the game, confirmed (code) at `0x0022d790`:

1. Mode 2, both counters 0; state `0x4000000`; the human's target is the door; the first time (tutorial hints on),
   hint `0x14`; the **start callback** (`CfgSetLockPickHandler`'s first name) runs with the human's and the door's
   handles; the HUD dial is shown at the difficulty of the Warrior class's byte `+0x0a` less 1 (0-2).
2. **The dial** (`LockPickDial_Draw`): three pins, all starting at π. Each frame the current pin turns by 0.1 rad ×
   its speed and wraps in 0-2π; the speeds are 1, 2 and 3, and the directions by difficulty are (+, +, +), (+, −, +)
   and (−, +, −). At 30 frames a second a pin turns once in about 2.1, 1.05 and 0.7 s (the rate is inferred from the
   frame rate).
3. **Cross pressed** (`0x12`) judges the current pin's angle θ (`LockPickDial_Judge`): **good** when θ is at least
   the upper bound or at most the lower one, **perfect** inside the narrower bands, otherwise a **miss**:

   | Difficulty | Good | Perfect |
   | --- | --- | --- |
   | 0 | θ ≥ 5.45 or θ ≤ 0.80 (312°-46°) | θ ≥ 5.90 or θ ≤ 0.20 (338°-11°) |
   | 1 | θ ≥ 5.70 or θ ≤ 0.57 (327°-33°) | θ ≥ 5.95 or θ ≤ 0.12 (341°-7°) |
   | 2 | θ ≥ 5.90 or θ ≤ 0.35 (338°-20°) | θ ≥ 6.05 (347°-0°) |

   A good press adds 1 to `+0x42` (a perfect one also to `+0x44`) and moves on to the next pin; a miss resets both
   counters, puts every pin back at π, plays a click and runs the **stage-fail callback** with the human and the door.
   Confirmed (code) at `0x002878b8`, `0x001b8d38`; the band values are set by `0x001b7a18`.
4. **Triangle**, or any command outside cross, L1, R2, the d-pad and SELECT's combinations, sets `+0x42` to −1:
   the pick is abandoned.
5. **Three good presses** in a row succeed (`MiniGame_Update`), then 689 `ANIM_LOCKPICK_END`; an abandoned pick ends
   with 690 `ANIM_LOCKPICK_FAIL` (`0x00278628`).

**The outcome** (`LockPick_End`, `0x0022d908`), confirmed (code):

- **Success**: the **success callback** (human, door); the door is unlocked (message `0x22` value 0) and swung open
  (`Door_OpenAnimated`). Three **perfect** presses score bonus event 1-3 and raise no crime; otherwise a
  **break-in** (crime type 1) is reported at the human, with responders.
- **Abandoned**: the **stop callback** (human, door). A `dyn_door_swinging` door counts abandoned attempts (its
  record `+0x29`), and the **third** reports a break-in.
- Either way the dial is hidden; the hint counter (game-state bits 26-27) counts successful picks up to 2.

No inventory item is spent: **there is no lockpick item**; the inventory has none ([Inventory
items](../references/inventory.md)).

### The car stereo {#stereo}

`CarSpawnRadio` puts a `dyn_carstereo` in a car; breaking the car window (glass type 12, [panes](objects.md#pane))
frees it and it registers a kind-3 record. The game itself is [Combat: the stereo theft](combat.md#stereo-theft);
its set-up and end, confirmed (code) at `0x0022dd98`, `0x0022e020`:

- Start: mode 3, meter at twice `CfgButtonMash`'s gain, stage and angle 0; a camera change on the player's camera
  (`0x001361a8`, inferred); hint `0x12` the first times; the stereo HUD widget (HUD `+0xfea0` + player × `0xb10`).
- Success: the player gets **$15** (item 2) and **one car stereo** (item 11); the stereo is taken as an owned item
  ([below](#owned-items)); `CfgSetSteroTheftHandler`'s callback runs with the human and the car.
- The hint counter (game-state bits 22-23) counts thefts up to 2.

### Mugging {#mugging}

The stick game and its timings are on [Combat: mugging](combat.md#mugging). **Who can be mugged**
(`Mug_CanMugVictim`, `0x00225ff0`), confirmed (code): the player must be holding the human from the front or the rear
with no move playing, and either the human has an **interrogation** set (`+0x5a0`), or it may be mugged (`+0x5b0`,
`HuSetMug`) and:

- a human no player controls must not carry the ledger (pocket item 12) and must have something: a named object,
  money, or a pocket count;
- a **player** human (the other player) only while `CfgPlayerMugging` is on (game state `+0x5708`) and he holds any
  of items 0-6.

**What happens**, confirmed (code) at `0x002856b8`:

- The mugger says speech command 19 `mug`, or 39 `mugcop` when the victim's brain is a cop's; an interrogation plays
  the victim's set lines instead. Hint 0 (a mugging) or 1 (an interrogation) the first times.
- Half-way to the required time the victim says 28 `resist` (103 `dirty_resist` for ped type 5, inferred a dirty
  dealer). A victim with **nothing** says 31 `no_item` at that point and the mugging ends with nothing.
- **Success**: the victim says 30 `item` (104 `dirty_item`). Its money goes to the player (item 2; ped type 5 gives
  1.5 times, at most 999) and its money is set to 0; a victim with a pocket item gives that instead
  (`0x002334d0`: a flash or spray can is added to the inventory, a switchblade is equipped). A player victim hands
  over all of items 0-6 (`0x00285520`). An interrogation instead plays the fourth line and calls its callback.
- Any command outside the game's own ends it; so does, for a mugger no player controls, its time limit.

The scores (crime events 4-5 to 4-7) are on [Statistics](../references/statistics.md). The mugging itself reports
no crime; type 8 (`Mugging`) comes from scripts or from the witnesses' goals, which report a type from their own
records ([AI: crimes](ai.md#crimes)).

### Tagging {#tagging}

A level's tag spots are `part_spray_tag` particle objects (class bit `0x10` marks a particle system; configured by
`CfgTagSettings`) with flags of activity 12
([World flags](flags.md)); `global.lua`'s helper loads a **pattern** (`HuTagPattern`) and calls `HuTag`.
Circle + triangle (`0x24`) also sprays where the player has paint ([Combat](combat.md#dispatch)).

**`HuTag`** (`0x00238db0`), confirmed (code): a player with **no spray paint** (item 3) says 37 `nopaint` and the tag
object is told (message `0xe`); otherwise the human's target becomes the flag, `+0x36c` the tag, its action `0x17`,
and a player occupies the flag.

**The stick game**, confirmed (code) at `0x002741d8`, `0x002748a8`, `0x00274710`:

1. The pattern's points (`0x00510914` count n, float (x, y) pairs at `0x006cd978`) are sampled along a uniform
   **Catmull-Rom** curve (`0x00273ff0`): segment i runs from point i to i + 1 with weights
   (−t³/2 + t² − t/2, 3t³/2 − 5t²/2 + 1, −3t³/2 + 2t² + t/2, t³/2 − t²/2) on points i − 1, i, i + 1, i + 2, the
   indices clamped to the first and last point (so the last segment stays on the last point). Each segment, i = 0
   to n − 1, takes ⌊300 / n⌋ samples at t = k · n / 300 (k = 0, 1, ...). A sample's x and y are truncated to
   integers and their low byte kept, so pattern values are **grid cells 0-255** directly (no scaling; larger values
   wrap). A sample joins the path when its squared distance from the last one kept is more than 6 (the first is
   compared with (0, 0)); at most 300 samples, so at most 300 path points. The tag's painted fraction (its `+0xc8`,
   [tag spots](#tag-spots)) × the path length sets where on the path the player starts (0 for a fresh tag).
2. The **cursor** moves with the left stick when it is past 0.2: by stick × elapsed ms (at most 30) × the speed,
   ramping linearly from 0 over the first 2 s; it stays inside the grid. Each cell at least 2 away from every cell
   already painted is painted.
3. The cursor is **on track** within 6 cells of a path point no more than 4 points ahead of or behind the progress;
   progress moves to the nearest such point. Off track for more than 4 updates while the stick moves, the cursor
   snaps back to the path at the progress, the controller rumbles, and the game pauses for the pause time; a crew
   member may comment (speech 80 `tagcheer`, when the tagger's `+0x19c` is set).
4. **Paint**: each charge lasts the charge time; when it runs out the next charge is spent (item 3 − 1), the cursor
   snaps back and the game pauses. With no charge left the tag ends unfinished.
5. **Finished** when the progress is within 4 points of the path's end; painting 300 cells first ends it unfinished.
6. Any command outside the game's own (or no paint) ends it.

The **tuning**, by the Warrior class's byte `+0x09` (1-3, 0 counting as 1), from the table at `0x00510918`, unless
`HuTagDifficulty` sets its own (no script does):

| Difficulty | Charge time | Pause | Speed (cells per ms) |
| --- | --- | --- | --- |
| 1 | 13,000 ms | 500 ms | 0.05 |
| 2 | 11,000 ms | 600 ms | 0.07 |
| 3 | 9,500 ms | 500 ms | 0.073 |
| `HuTagDifficulty` defaults | 15,000 ms | 1,000 ms | 0.06 |

**The end** (`Tag_End`, `0x0022e848`), confirmed (code): finished, the tag is marked sprayed (messages `0x41` 1.0 and
`0x19` 3), a crew member says 83 `tagdone`, and a clean finish scores bonus event 1-3; unfinished with less than 30 %
of the current charge left, one more charge is spent. The tagger's flag is freed, the **tagger** gets event `0xe`
(the tag's handle and whether it was finished, through its own vtable `+0x44`) and the **tag object** (human
`+0x36c`) gets message `0x13`. Hint `0x10` in level `0x57`.
`CfgTagStartCallback` names the script function called when a tag starts. Tagging is crime type 10, which sends no
responders ([Crime types](../references/crime-types.md)).

### Tag spots {#tag-spots}

A `part_spray_tag` (init `0x003fc600`, handler `0x003fc8d8`, update `0x003fca68` every second frame) keeps a small
record: its sprite batch, a **fade mode** (`+0x04`: 0 still, 1 fading out, 2 fading in), the sprite word, a **spray
mode** (`+0x0c`: 7 the next spray paints the tag in, 5 it wipes the tag out; 7 at start), the **painted fraction**
(`+0x10`, copied to object `+0xc8`, the drawn opacity and where the stick game starts; 0 at start), the fade step
(`+0x14`, 0.005), the depth (`+0x18`) and the **tagger** (`+0x1c`, a human). Confirmed (code) at the addresses.

| Message | Effect |
| --- | --- |
| `0x00` (tagger) | stores the tagger, then as `0x12` |
| `0x12` (shown) | sets path flag 8 on the path polygon at the tag ([AI](ai.md#path-planning)); if the spray mode is 7 with fraction < 1, or 5 with fraction > 0, and there is a tagger: the tagger gets message `0x17` (state 1, the tag's position) and the tag sends itself `0x19` with the spray mode (7 starts the fade in, 5 the fade out); otherwise the tagger, if any, gets `0x13` |
| `0x13` (hidden) | clears path flag 8 there, the tagger (if any) gets `0x13`, the fade stops (mode 0) |
| `0x15` | frees the sprite batch and removes the object |
| `0x19` state 3 | fade stops; the tagger (if any) gets `0x13` (sent by the update when a fade reaches 1 or 0) |
| `0x19` state 4 | fraction 0 (blank), spray mode 7 |
| `0x19` state 5 / 7 | fade out / fade in starts |
| `0x19` state 6 | fraction 1 (fully painted) |
| `0x27`, `0x37`, `0x38`, `0x3b` | [`CfgTagSettings`](../references/bindings/config.md#cfgtagsettings): sprite, `+0xc0`, fade step, depth |
| `0x39` / `0x3a` | spray mode 7 / 5; the look is unchanged |
| `0x41` (fraction) | sets the painted fraction; while it is between 0.03 and 0.95 a few spray particles are emitted across the tag (`0x003fbe98`) |
| `0x0e` and any other | ignored by the tag's handler |

**The fade**: each update in mode 2 adds the step to the fraction; reaching 1 it sends itself `0x19` state 3. Mode 1
subtracts it down to 0 the same way. [`ProcessTag`](../references/bindings/level.md#processtag) sends `0x19` 4 or
6, or `0x39` / `0x3a`. Confirmed (code).

### Taking owned items {#owned-items}

An item with its owned byte (`+0x109`) set, from a store or a stolen stereo, is cleared of it when taken
(`0x00395d98`) and `Item_TakeOwned` (`0x00412dc8`) runs, confirmed (code): every human that can see the spot is told
(message 7, `0x00412a18`), humans within 30 m are alerted (`0x00293768`), a player scores crime event 4-8, a Warrior
led by a player scores it for every player (`0x004f39e8`), and nearby witnesses say 128 `stealitem` (30 %) or 143.

### Stores {#stores}

A store is a set of flags ([World flags: groups](flags.md#groups)) with a store front (activity 14), browsers and
buyers. A **break-in** (crime type 1) comes from an alarmed pane ([panes](objects.md#pane)), a lock pick that was not
perfect, or a third abandoned pick. The report ([AI: crimes](ai.md#crimes)), confirmed (code) at `0x0041b8b0`: within
10 m the store front is marked robbed (bit 16 of its group word, the offender's gang in bits 18-22), the store's
buyers and browsers are switched off, the nearest `strobe` within 6 m gets message `0x12`, and responders are queued
with a delay of `CfgBreakAndEnterDelay` (15, 10 or 7 by difficulty). The first gang to rob a store scores crime event
4-1 for its leader when he is a player. `ResetStore` (`0x0041ddd8`) clears the robbed bit, sets the group word's bits
8-15 to `0xff` and the gang bits to 31, and restocks the store; its 7 m test is a strobe's distance from the store's
flag, not the player's (confirmed (code)). `EnterStore` / `ExitStore` only change the screen colour.

### Wanted and the crime level {#wanted}

**Wanted** is per gang, confirmed (code): every crime report with an offender sets the offender's gang wanted for
**10 s** (gang `+0x5e8` = now + 10,000 ms, `0x00169878`), after making the police hostile to it. `Gang_UpdateWanted`
(`0x001698f0`) holds it at 10 s from now while `ForceCrimeLevel` is on (game state `+0x28c`); otherwise, once it
passes, `GangClearWanted`'s work runs (`0x00169a20`): the police are neutral again and, for the player's gang, the HUD
is told (`0x001b2520` message `0xb`) and the last crime type becomes 14 (`NoCrime`). The HUD shows the time left as a
fraction of 10 s (`0x001aa9b0`; full above 0.9). Gang `+0x5f0` is a second timer drawn the same way, and
`GangIsWanted(gang, false)` tests it. `GangRespond` and `GoalCallGang` set it (`Gang_SetSecondWantedTimer`,
`0x001698c8`); when it runs out, `Gang_UpdateWanted` (`0x001698f0`) calls the all-clear callback at game state
`+0x2bc`. Confirmed (code). The first time the player's gang is wanted, hint 6
(`Humans_Update`).

**The crime level** (game state `+0x291`, 0-3) is set only by `InitLevel` and `SetCrimeLevel`, which no script calls.
While set with a time, it steps down one level at each expiry (`Crime_UpdateLevel`, `0x0041c028`): to 2 the HUD gets
message 8 and the next step waits `+0x2a5` seconds; to 1, message 7 and `+0x2a4` seconds; to 0, message `0xb` and the
police reset (`0x0016c5b8`). The police guard goal reads the level to pick a station's settings (`SetCopStation`'s
values, `0x002c0ca8`). So in the shipped game the level stays 0 (inferred) and **wanted time is the heat**.

**Severity** does nothing: `CrimeIsHappening`'s fourth argument and the report's `severity` argument are never read
(confirmed (code) at `0x0037a2f8`, `0x0041b6e0`, `0x0041b8b0`); its radius and flags are not read either.

## Coney's implementation

**Lock picking** (`repo:src/world_objects/lock_pick.h`, 2026-10-06): `LockPickDial` turns the current pin 0.1 rad ×
its speed per step in the difficulty's direction and judges a press against the difficulty's bands (a miss resets
both counters and every pin); `LockPick` runs the start, stage-fail, success and stop callbacks, plays the click on a
miss, unlocks and opens the door on success (a break-in unless all three presses were perfect, which score bonus event
1-3) and counts an abandoned pick at the door (the third reports a break-in). The door side is on
[World objects](objects.md#coneys-implementation). In play (`repo:src/platform/play_level_objects.cpp`), triangle
within 1.5 m of a pickable door (**Coney's stand-in** for the kind-2 record's reach, not traced) starts a pick at the
difficulty of the player's `CfgWarriorClass` byte `+0x0a` less 1; the pick takes the pad until it ends, the dial
turning each step, cross judging and any button but cross, L1, R2, the d-pad and SELECT abandoning. Not yet: the
lock-pick animations (689, 690), the hint and the HUD dial.

**Crime reports** (`repo:src/warriors/crime_reports.h`, `CrimeReports` in `GameState::player`), written
from [AI: crimes](ai.md#crimes) and [Wanted](#wanted) (2026-10-06): `report()` follows steps 1-6 there, reaching gangs,
spawners, stores, statistics, the script callback and the HUD through a `CrimeServices` interface the play mode
implements as those systems arrive; `update()` clears a gang's wanted state 10 s after its last report (held while
forced). In play the objects' break-ins report through it (`repo:src/gamemodes/level_crime_services.h`: the crime
callback, the `CrimeScene` flag and which humans are players; Coney's gangs, spawners, stores and HUD are not wired to
it yet). `ReportCrime` switches reporting. `EnterStore` / `ExitStore` keep the store colour and preset
(`StoreTint`) for a renderer, and `CfgSetSteroTheftHandler` keeps its callback's name.

**Car stereos** (`repo:src/world_objects/cars.h`, 2026-10-06): `CarSpawnRadio` puts a stereo in a parked car, a broken
pane frees it (`ObjectServices::freeCarStereos`, within 2 m of the pane) and `Cars::takeStereo` takes it once; the
theft itself, the kind-3 record and the stick game are not in Coney yet ([Cars](cars.md#coneys-implementation)).

Coney's choices: a break-in and a custom crime queue kind-1 responders (the break-in after `CfgBreakAndEnterDelay`);
the assault statistic is scored once per victim through the service.

## Open questions

- Which objects register context records of kinds 1, 4 and 5, and with what second value.
- Who sets mini-game mode 4.
- The prompt widgets' base position and text style.
- The lock-picking dial's rate at runtime (one step per drawn frame is inferred).
- The responder spawn kind of a break-in (type 1) and of a custom crime (type 4).
- Coney has no colour controllers (`0x005fdeb8`), so `EnterStore`'s tint is kept but not drawn.
- Wiring the report's hostility, responders, robbed stores and HUD messages to Coney's gangs, spawners and HUD.
