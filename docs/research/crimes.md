# Crimes and context actions

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`), static analysis only
(Ghidra), with the disc's compiled scripts read through `coney-tools`' Lua walker for string ids. Runtime reads (the
mugging's starting money) are over PINE in PCSX2 2.9.94.

## Purpose

What the player does with **triangle** besides grabbing: the **context actions** (pick a lock, steal a car stereo,
uncuff a partner, mug, tag a wall, take an item), the timed **mini-games** behind them, what each one gives and what
it costs, and how the game turns these into **crimes**, **wanted** time and police. The pieces already documented are
linked rather than repeated: the mugging's stick game and the stereo's stick rotation are on
[Combat: mugging](combat-throws.md#mugging) and [Combat: the stereo theft](combat-targets.md#stereo-theft); the crime
report and the police response on [AI: crimes](ai-gangs.md#crimes) with the types in [Crime
types](../references/crime-types.md); breakable glass and doors on [World objects](objects.md); store flags on [World
flags](flags.md#groups); the inventory and the scores on [Inventory, unlockables and statistics](player-state.md) and [Statistics](../references/statistics.md).

## Original structure

No source file names this code; it sits with the combat code after `Human/cns/cnsplayertag.cpp` ([Source
map](source-map.md)). Names are ours.

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x002811f0` | `Player_TriangleAction` | triangle pressed: a key for a cuffed partner, a tag in progress, then the context action, then a pick-up | confirmed (code) |
| `0x0024d530` | `ContextAction_Use` | starts the action of the human's current context record (+`0x660`) | confirmed (code) |
| `0x00417ca0` / `0x00418150` | `ContextActions_Register` / `ContextActions_Pick` | the registry of context records; the per-update choice | confirmed (code) |
| `0x0022dc40` / `0x0022e400` / `0x0022d628` | `MiniGame_Start` / `MiniGame_End` / `MiniGame_Abort` | dispatch by mini-game mode | confirmed (code) |
| `0x0022ec18` / `0x0022ef58` | `Human_Arrest` / `Human_Unarrest` | `HuSetArrested` 1 / 0: the cuffed state, its style, record and icon ([Arrest](#arrest)) | confirmed (code) |
| `0x0028c5d8` | `Brain_OnArrested` | the brain's reset and event 17 | confirmed (code) |
| `0x002271f0` / `0x00227388` | `Human_ShowOverheadIcon` / `Human_RemoveOverheadIcon` | the icon over a human (`dyn_cuffs`, `dyn_cross`) | confirmed (code) |
| `0x00260ca8` / `0x0022d3f8` | `Uncuff_Start` / `Uncuff_BeginMash` | the uncuff mash's start ([Uncuffing](#uncuffing)) | confirmed (code) |
| `0x00260fd0` | `Uncuff_WithKey` | freeing at once with a key | confirmed (code) |
| `0x002606e8` / `0x00260a70` | `Uncuff_MashSuccess` / `Uncuff_MashFail` | the mash's outcome | confirmed (code) |
| `0x0027bcd8` / `0x0027bc48` | `Mash_IsAlternation` / `Mash_IsQuitCommand` | the mash's input tests | confirmed (code) |
| `0x00278018` | `Tag_StartSprayClips` | the spray clips and the turn to the tag | confirmed (code) |
| `0x00255f08` | `MiniGame_Update` | each update: success, failure or the meter | confirmed (code) |
| `0x0027e6d8` | `Player_UpdateTheft` | each update: the mode's input ([Combat](combat-targets.md#stereo-theft)) | confirmed (code) |
| `0x0022d790` / `0x0022d908` | `LockPick_Start` / `LockPick_End` | the lock-picking set-up and outcome | confirmed (code) |
| `0x002878b8` | `LockPick_JudgePress` | a cross press against the dial | confirmed (code) |
| `0x001b8d38` / `0x001b8530` / `0x001b7a18` | `LockPickDial_Judge` / `_Draw` / `_SetDifficulty` | the HUD dial (HUD `+0xf420` + player × `0x540`) | confirmed (code) |
| `0x0022dd98` / `0x0022e020` | `StereoTheft_Start` / `StereoTheft_End` | the stereo's set-up and outcome | confirmed (code) |
| `0x00225ff0` | `Mug_CanMugVictim` | whether the held human can be mugged | confirmed (code) |
| `0x002856b8` | `Player_UpdateMugging` | the mugging ([Combat](combat-throws.md#mugging)) and its payout | confirmed (code) |
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
| 4, 5 | objects with their own text: kind 4 a dealer's offer ([AI: GoalDealer](ai-scripting.md#dealer)) | the caller's text | 1.75 m, 1.5 m | confirmed (code); kind 5's registrar is not traced |

The reaches are stored squared at `0x00514878 + kind × 4` (`CfgActionDistance`); the executable's defaults are
2, 1.1, 2, 1.5, 1.5, 1.5 m. The prompt texts are `GSTRING.HUD` strings ([Text labels](../references/text-labels.md#gstring)),
fetched by number through `0x0019ee70`.

### The mini-game record {#mini-game-record}

The fields `+0x40`-`+0x4c` of the per-player record that `0x00222b18` returns ([Combat](combat-targets.md#stereo-theft)
gives its address), shared by every timed action; confirmed (code):

| Offset | Meaning |
| --- | --- |
| `+0x40` | s16 meter: the mash meter (modes 1, 4) or the stereo's |
| `+0x42` | s16 lock picking: good presses in a row; −1 asks to abandon |
| `+0x44` | s16 lock picking: perfect presses in a row |
| `+0x46` | u16 **mode**: 0 none, 1 uncuff mash, 2 lock picking, 3 car stereo, 4 a mash on an object, 5 `HuButtonMiniGame` |
| `+0x48`, `+0x4c` | the stereo's angle and stage ([Combat](combat-targets.md#stereo-theft)) |

While a mode runs, human state `0x4000000` is set ([state flags](combat.md#state-flags)). Combat's "theft mode 1"
is the **uncuff** mash and "mode 2" is **lock picking**. No code was found that sets mode 4 (its start
`0x0022df88` and end `0x0022e330`, which marks the object `0x8000` and calls `0x0024c560`, are reached only through
the dispatch).

### Human fields {#human-fields}

| Where | Meaning | Evidence |
| --- | --- | --- |
| state `0x2000000` | **tagging** (`HuIsTagging`); the other state bits are on [Combat](combat.md#state-flags) | confirmed (code) at `0x002238c0` |
| `+0x250` / `+0x254` | the pocket: item id and count (`HuPutItemInPocket`) | confirmed (code) |
| `+0x257` | the carried object's name; anything but `none` counts as something to give ([starting values](#starting-money)) | confirmed (code) at `0x00225ff0` |
| `+0x278` | the carried object's drop chance (`CfgChar` `+0xb4`; 100 after `HuSetCarriedItem`), inferred a percentage | confirmed (code) at `0x00218850` |
| `+0x36c` | the tag object being sprayed | confirmed (code) at `0x00238db0` |
| `+0x370` | money carried (0-999); rolled at creation ([Starting money](#starting-money)) | confirmed (code) at `0x00218a90` |
| `+0x3ba` | 1 once the current tag is finished | confirmed (code) |
| `+0x590`-`+0x59c` | the four interrogation lines; `+0x5a0` the interrogation callback (`HuSetInterrogation`; [Interrogation](#interrogation)) | confirmed (code) |
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

**Triangle pressed** (command `0xa`, [Commands](combat.md#commands)) runs `Player_TriangleAction` **after** a climb
is tried for a pad-controlled human (an AI's triangle runs it alone), and before the jump and the drop
([Triangle with something in hand](characters.md#triangle-held)); an earlier reading here had it before the climb.
Confirmed (code) at `0x0027c120`, `0x002811f0`:

1. Nothing while a move or a state blocks it (state mask `0x7bf9e9f7ff0`, record `+0x08` mask `0x2fefefff`).
2. A human with no player number standing by a cuffed player uses a key (item 6) if either holds one
   (`0x00260fd0`).
3. A tag in progress (a tag spot, a particle object with class bit `0x10`, at `+0x36c`) keeps the press.
4. `ContextAction_Use` (`0x0024d530`) by the record's kind: **0** frees the cuffed human ([Uncuffing](#uncuffing): the
   mash of mode 1; with a key, at once); **2** sets mode 2 and plays 687 `ANIM_LOCKPICK_INTRO`; **3** sets mode 3 and
   plays 683 `STEREO_STEAL_INTRO` (both through `0x002789d0`, which turns the human to the object and starts the
   mini-game); any other kind hands the press to the object's message handler (its `+0x44`, message 0 with the human as
   the subject); a true result ends the press. Confirmed (code), and at runtime for a kind-1 bat.
5. Otherwise the pick-up search (`0x0024d810`, objects within 1.5 m; only while `0x005109a0` is set). It too sends
   message 0 to every object it gathers, before its filters, and a true result ends it; then it takes one
   ([Breakables](combat-targets.md#breakables), [A bat in hand](combat-throws.md#bat)). With something in hand and an
   object to take, it drops what is held (`0x00257f38`) and sets human `+0x5b8`, so the triangle hold takes the new one.
   With nothing to take, only an AI drops here; a pad-controlled human gets 0 and the dispatcher goes on to the jump, then
   the drop (`0x0024d810`, confirmed (code); an earlier reading had every human drop here).

**The prompt** (two text widgets per player at HUD `+0x6b40`, `0x590` each;
[HUD](hud-messages.md#announcements-and-other-messages)) is chosen every frame by `HUD_Update` (`0x001af010`), confirmed
(code): nothing while the player is mugging, mugged, tagging, in a mini-game of mode 2 or 3, in a hold
(`0x18000000000`), uncuffing (action `0x15`) or a few other states, or when command `0xa` is not available
(`0x00147738`). Otherwise, in order: holding a human who can be mugged → `GSTRING.HUD` 1 (mug), or 0 (interrogate) when
the human has an interrogation set; a revivable friend or gang member knocked out within 3 m and in sight, while the
player or (in co-op) the downed player carries a flash → 4 (revive, [HUD: the revive
test](hud-messages.md#action-prompts)); a context record → its text (`+0x10`; the second text `+0x14` goes in the hint
box at priority 0, [HUD](hud-messages.md#action-prompts)); else a nearby human's own talk prompt (`0x001acd60`). The
panel's activity test then matches the prompt's text against the dealer prompts ([HUD](hud.md#the-player-panel)). The
prompt sits 0.04 above its place in the default video mode (0.02 in the others) and rises with a scroll-in message
(`0x0019f430`).

### Arrest {#arrest}

`HuSetArrested(h, on)` (`Human_SetArrested`) runs `Human_Arrest` (`0x0022ec18`) or `Human_Unarrest` (`0x0022ef58`),
then sets state `0x20000000`. Confirmed (code) at the cited addresses.

**Arrest**, for a human not already cuffed:

1. The human leaves its fight stance and drops a held weapon of kind 4 or 6, or anything held without human flag
   `0x2000`. A stun ends.
2. Its push weight (attribute 7, vtable `+0xe4`) becomes 1e9, so nothing shoves it. State `0x8000` (block) is
   cleared and `0x20000` (cuffed) set.
3. Movement style `0x11` is pushed (`Human_PushMoveStyle`, `0x00253ed0`), which puts 320 `ANIM_ARRESTED_IDLE` in
   anim slots 0 and 11 ([Anim slots](characters.md#anim-slots)). The action (record `+0x14`) becomes 0.
4. `Brain_OnArrested(brain, 1)` (`0x0028c5d8`): the brain's actions are cleared, its target dropped and its goals
   popped down to a goal of type `0x41` if it has one. Then the human gets **event 17** `{+0x00 the other human of a
   grab (record +0xc4), +0x04 1}`, which a script handler receives as `(self, other, 1)`
   (`ScriptHandler_MarshalMessage` case `0x11`, `0x00384ce0`).
5. Brain `+0x288` = 0.
6. **The kind-0 record** (`Human_RegisterContextAction`, human vtable `0x0053f088` slot `+0x124`, args kind 0, no
   text) is registered for one of two humans:
   - an AI human friendly to player 1 (`Human_IsFriendly`, `0x00222a90`). An AI Warrior in the player's crew is one;
   - a player, when two players are in or when he owns upgrade (6, 15) and holds a key.

   That human says 25 `arrested` ([Speech](../references/speech.md#speech-command-25)). Unless it is a player with a
   key, it also gets the **`dyn_cuffs` icon** over its head (`Human_ShowOverheadIcon`, `0x002271f0`, handle at human
   `+0x360`; `dyn_p_one` and `dyn_p_two` names become `dyn_play_one` and `dyn_play_two`, with `_euro` when game state
   `+0x120` is set).
7. For a type-3 brain, the player whose gang it is gets `0x004de250(…, player, 0, 1)` (not traced).

A human with no AI brain component (vtable `+0xf4` null) gets only state `0x20000`, the style and action 0.

**Unarrest** (`Human_Unarrest`), for a cuffed human:

1. Brain `+0x40` gets `0x80000004`, and the kind-0 record is removed (slot `+0x12c`).
2. `0x0024ce40` is called and the icon removed (message `0x15`, `Human_RemoveOverheadIcon`, `0x00227388`). Style
   `0x11` is removed (`0x00253f78`), state `0x20000` is cleared, and the push weight returns to 1.
3. A member of a leaderless gang (gang `+0x40` = 0) with a leader other than itself and a type-3 brain clears its
   actions and goals and follows the leader (`Goal_FollowPlayer`, 2.0, mode 3).
4. **Event 17** goes out as above with `+0x04` = 0: `SetMsgHandler(h, 17, "SnowUnarrested")` gets `(self, other, 0)`,
   and `other` is the null handle unless the human was in a grab.

### Uncuffing {#uncuffing}

Triangle by a cuffed human's kind-0 record (`ContextAction_Use`, [Triangle](#triangle), step 4) needs two things:
the cuffed human is not already being freed (`+0x08` `0x10000`), and nothing stands between the two
(`0x0021c0a8`, a 0.2 m capsule collision test from the freer to him). The record's user (`+0x18`) becomes the
freer. Then the press goes one of two ways. Confirmed (code) at the cited addresses.

**With a key** (item 6 held by either), `Uncuff_WithKey` (`0x00260fd0`):

1. Interface cue 24 plays and one key is spent: the freer's, or else the cuffed human's.
2. The freer turns to face the cuffed human (`Attack_SteerToTarget`, 0.1, over clip 325). He plays 325
   `ANIM_ARREST_RELEASE_INTRO_FRONT` (holding `+0x08` `0x2000`), then 332 `ANIM_ARREST_RELEASE_END`
   (end hook `0x002605f0`), then his idle.
3. The cuffed human gets `Human_Unarrest`, `Human_ClearPartner` and a heal (below). He plays 326 (paired, holding
   `0x10000`), then 333 `ANIM_ARREST_RELEASE_END_REACT` (end hook `0x00260670`), then his idle.

When a player frees himself with his own key, he plays only his half.

**Without a key**, the mash (`Uncuff_Start`, `0x00260ca8`):

1. The freer says 68 `unarrest_reasure` and turns to face the cuffed human over clip 325. His action (record
   `+0x14`) becomes **`0x15`**.
2. `Uncuff_BeginMash` (`0x0022d3f8`):
   - leaves the fight stance; mode 1, **meter 0**;
   - the cuffed human becomes the freer's partner (`+0xc8`);
   - both bodies share a new group (body `+0x3c`, from a counter at `IPhysics+0x10`), so they never touch;
   - both get push weight 1e9;
   - a held weapon of kind 3 or 4 is dropped;
   - state `0x8000` is cleared on both and `0x4000000` set on the freer.

   For the triangle press it also shows the player's **mash meter** (`HUD_MashMeterShow`: HUD `+0xebc0` + player ×
   `0x430`, button id 1 and sprite word `0x171`). Hint 19 is queued when in-game hints are on, the level is not an
   Armies level, hint 19 is unlocked and the hint's shown count (game state bits 24-25) is below 2.
3. The freer plays 325, holding `+0x08` `0x2000000`, which keeps the mash's input closed until it ends. He then loops
   329 `ANIM_ARREST_RELEASE_LOOP`.
4. The cuffed human plays 326 `ANIM_ARREST_RELEASE_INTRO_FRONT_REACT` paired to the freer, then loops 330, holding
   `0x10000` throughout. He gets the freer as his partner too.

The cuffed human's kind-0 record stays registered during the mash; only `Human_Unarrest` removes it.

**Each update** (`Player_UpdateTheft` mode 1, `0x0027e6d8`, while `+0x08` has none of `0x7c7eee0`):

- An **alternation** between commands 6 (L1 held) and 4 (R1 held) adds the gain. `Mash_IsAlternation`
  (`0x0027bcd8`) counts the first of either and then each switch. The gain is half of `CfgButtonMash`'s press
  gain, × 1.5 for a Warrior of class byte `+0x08` = 1 and × 0.7 for 3, rounded. Each gain also restarts the decay.
- A **quit command**, 10 (triangle), 17, 18 (cross) or 30 (circle) (`Mash_IsQuitCommand`, `0x0027bc48`), sets the
  meter to −1.
- Otherwise the meter **decays** by `CfgButtonMash`'s decay each update. The decay stops once a step takes the meter
  to 0 or below, and it does not run before the first alternation: the flag `0x005109a8` starts at 1.

The executable's defaults are a target of 1000, a decay of 10 and a press gain of 100 (`0x005102b8`-`0x005102c0`).
The script values in force are on [Combat](combat-targets.md#stereo-theft).

**The outcome** (`MiniGame_Update` mode 1, `0x00255f08`, each update):

- **Success**: meter ≥ target. `Uncuff_MashSuccess` (`0x002606e8`):
    - the mini-game ends (`MiniGame_Abort`: mode 0, meter hidden, record user cleared, state `0x4000000` cleared,
      action 1, partner cleared);
    - the **cuffed human says 67** `unarrest_thank`; the hint's shown count rises by one;
    - the freer plays 332 (blend 1/6 s, end hook `0x002605f0`), then his idle;
    - the cuffed human gets `Human_Unarrest`, then a heal when he is knocked out (`Human_IsKnockedOut`, state
      `0x40000`) and has human flag `0x4`;
    - the cuffed human plays 333 paired (end hook `0x00260670`), then his idle.

  `0x00280fd8(freer, 3000)` sets the per-player record's `+0x4d` and `+0x50` = now + 3 s (their reader is not
  traced).
- **Failure**: the meter is below 0 (a quit command, or a decay step that ends below 0), or the cuffed human is gone
  (`Object_AsHuman`, `0x00229868`, null). `Uncuff_MashFail` (`0x00260a70`):
    - the mini-game ends;
    - the freer plays 332, then his idle;
    - the cuffed human returns to his idle loop (still 320, since he is still cuffed). His `0x10000` and his partner
      are cleared.
- **A hit on the freer**: `Human_ApplyPendingDamage` sends a human in a mini-game other than lock picking to
  `0x00268c50`. That function ends the mini-game and plays 331 `ANIM_ARREST_RELEASE_HIT_REACT` (holding `0x2000`).
  The cuffed human's clips are not reset there (not traced further).
- A human leaving normal mode (`0x002325e0`, from `Human_SetNormalMode` and scenes) also ends it.

While the mash runs, the meter's fill is meter ÷ target each update (`HUD_MashMeterUpdate`), and the action prompt
is hidden (action `0x15`).

### Lock picking {#lockpick}

A door made pickable by `SetDoorPickable` (message `0x22` value 10; [Doors](objects.md#doors)) registers a kind-2
record. Triangle at it starts the game, confirmed (code) at `0x0022d790`:

1. Mode 2, both counters 0; state `0x4000000`; the human's target is the door; the first time (tutorial hints on),
   hint `0x14`; the **start callback** (`CfgSetLockPickHandler`'s first name) runs with the human's and the door's
   handles; the HUD dial is shown at the difficulty of the Warrior class's byte `+0x0a` less 1 (0-2).
2. **The dial** (`LockPickDial_Draw`): three pins, all starting at 3.14 (not π, [HUD](hud-functions-lua.md#lock-pick-dial-layout)).
   Each frame the current pin turns by 0.1 rad × its speed and wraps in 0-2π; the speeds are 1, 2 and 3, and the
   directions by difficulty are (+, +, +), (+, −, +) and (−, +, −). The step runs once per character update, 30 times
   a second, so a pin turns once in 62.8 steps (2.09 s), 31.4 (1.05 s) and 20.9 (0.70 s). Confirmed (runtime),
   PCSX2 2.9.94: with the dial shown on a copy of quick-save slot 3 (player 0's dial `0x0060fc60`, `+0x478`
   written 1), the angle `+0x454` of pin 0 (speed 1) grew by exactly 0.1 every second 60 Hz tick (`0x005104f4`),
   90 steps in 180 ticks, wrapping 6.24 → 0.0568.
3. **Cross pressed** (`0x12`) judges the current pin's angle θ (`LockPickDial_Judge`): **good** when θ is at least
   the upper bound or at most the lower one, **perfect** inside the narrower bands, otherwise a **miss**:

   | Difficulty | Good | Perfect |
   | --- | --- | --- |
   | 0 | θ ≥ 5.45 or θ ≤ 0.80 (312°-46°) | θ ≥ 5.90 or θ ≤ 0.20 (338°-11°) |
   | 1 | θ ≥ 5.70 or θ ≤ 0.57 (327°-33°) | θ ≥ 5.95 or θ ≤ 0.12 (341°-7°) |
   | 2 | θ ≥ 5.90 or θ ≤ 0.35 (338°-20°) | θ ≥ 6.05 (347°-0°) |

   A good press adds 1 to `+0x42` (a perfect one also to `+0x44`) and moves on to the next pin; a miss resets both
   counters, puts every pin back at 3.14, plays a click and runs the **stage-fail callback** with the human and the door.
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
frees it and it registers a kind-3 record. The game itself is [Combat: the stereo theft](combat-targets.md#stereo-theft);
its set-up and end, confirmed (code) at `0x0022dd98`, `0x0022e020`:

- Start: mode 3, meter at twice `CfgButtonMash`'s gain, stage and angle 0; a camera change on the player's camera
  (`0x001361a8`, inferred); hint `0x12` the first times; the stereo HUD widget (HUD `+0xfea0` + player × `0xb10`).
- Success: the player gets **$15** (item 2) and **one car stereo** (item 11); the stereo is taken as an owned item
  ([below](#owned-items)); `CfgSetSteroTheftHandler`'s callback runs with the human and the car.
- The hint counter (game-state bits 22-23) counts thefts up to 2.

### Mugging {#mugging}

The stick game and its timings are on [Combat: mugging](combat-throws.md#mugging). **Who can be mugged**
(`Mug_CanMugVictim`, `0x00225ff0`), confirmed (code): the player must be holding the human from the front or the rear
with no move playing, and either the human has an **interrogation** set (`+0x5a0`, [below](#interrogation)), or it may
be mugged (`+0x5b0`, `HuSetMug`; `Human_Init` sets it to 1 at `0x002180ec`, so every new human may be) and:

- a human no player controls must not carry the ledger (pocket item 12) and must have something: a carried object
  (`+0x257` not `none`), money (`+0x370`), or a pocket count (`+0x254`);
- a **player** human (the other player) only while `CfgPlayerMugging` is on (game state `+0x5708`) and he holds any
  of items 0-6.

A human made by `HuCreate` with no `HuSetMoney` carries the money `Human_Init` rolled
([below](#starting-money)); for the street civilian (`PoizoCiv`, type 417) that is never less than $5, so he can
always be mugged.

**What happens**, confirmed (code) at `0x002856b8`:

- The mugger says speech command 19 `mug`, or 39 `mugcop` when the victim's brain is a cop's; an interrogation plays
  the victim's set lines instead. Hint 0 (a mugging) or 1 (an interrogation) the first times.
- Half-way to the required time the victim says 28 `resist` (103 `dirty_resist` for ped type 5, inferred a dirty
  dealer). A victim with **nothing** (no money, `+0x257` `none`, pocket item 0, no interrogation) says 31 `no_item`
  at that point and the mugging stops; it still ends through the **success** clips (the result flag, mugger
  `+0x5b4`, is set to 1), so the mug callback runs with true although nothing was taken.
- **Success** is decided in the update that reaches the required time, before any end clip: the victim says 30
  `item` (104 `dirty_item`). With pocket item 0 and `+0x257` `none`, **all** its money (`+0x370`; ped type 5 gives
  1.5 times, at most 999) goes to the mugger's player in one call, `Inventory_AddItem(inv, player, 2, money, 1)`
  (`0x0041e5b0`, notify on), the victim's money is set to 0 and item 2's pick-up sound plays (`0x0010fcd8` with the
  hash at inventory entry `+0x24`). A victim with a pocket item or object hands that over first (`0x002334d0`:
  pocket items 1 and 3 are added to the inventory with notify, item 4 equips the switchblade, a named object goes to
  the mugger); its money follows with the same call once the object handed over no longer resolves (state 5). A
  player victim hands over all of items 0-6 (`0x00285520`). An interrogation instead plays the fourth line and pays
  nothing ([below](#interrogation)).
- Then (state 5) `0x00273110(mugger, result)` plays the end clips, mugger 344 and victim 345 on success, 346 and 347
  on failure, and gives the mugger's clip the end callback `0x00273090` (success) or `0x00273038` (failure). A
  player mugger who succeeds also counts a statistic by the victim's brain kind (1 → 6, 2 → 7, 4 or 5 → 5, others
  none; `0x004ed948` on `0x006fe490`).
- Any command outside the game's own ends it; so does, for a mugger no player controls, its time limit.

Adding the money notifies, synchronously and in this order ([Player state](player-state.md#pickup-callback)): the
`CfgMoneyCallback` function with (player, amount), the `CfgInventoryCallback` one with (2), then the
`CfgHuInventoryCallback` one with (player, 2). The mugging calls nothing for the HUD; the HUD shows the inventory
count. Confirmed (code).

**The mug callback** (`HuSetMugCallback`, human `+0x5a4`). Every end of a mugging goes through `0x0022ceb8(mugger,
victim, success)`: the end clips' callbacks above, **when the mugger's clip ends**, not at the decision; a let-go
(`0x00258a88`); a hit and the other aborts (`0x002325e0`, `0x00267e48`, `0x00268df8`, `0x00268e50`,
`Player_UpdateActions`). It clears the mugging states (`0x100`, `0x200`), puts the pair back in a rear hold (`0x80`
/ `0x20`), withdraws hint 0 or 1, and then:

- a victim with no interrogation that is not a player: the **mugger's** callback (`0x002262f0`) is called with
  **(mugger, success)**, so it also runs, with false, for a failed or broken-off mugging;
- a victim with an interrogation: on success only, the interrogation callback ([below](#interrogation));
- a player victim: no callback.

Confirmed (code). By the time the callback runs the money has already moved.

#### Starting money and the carried object {#starting-money}

`Human_Init` (`0x00218008`, from `HuCreate` through `Human_Create` `0x00233d60`) sets the money (`+0x370`), the
carried object (`+0x257`) and its drop chance (`+0x278`); the pocket (`+0x250`, `+0x254`) starts empty. Afterwards
only `HuSetMoney` (`0x00238100`), `GangSetMoney` (`0x0016bd38`), a mugging, a drop (`0x00233494`) and the shopkeeper
goal (`0x002e74c0`) write a human's money. Confirmed (code).

The inputs are the type's `CfgChar` record (`0x00684620 + type × 0x1ac`): its category byte `+0x11b` (category 4
with byte `+0x14b` = 1 uses record 16 instead), the integer `+0xb4` (`v16`; -1 means "use the class range",
otherwise it is a drop chance and a money cap) and the name `+0x16c` (`str15`: an object, a `grp_` object group, or
`none`); and that category's `CfgCharClassAttribs` record (game state + category × 16): `min` (byte `+0x08`), `max`
(`+0x09`), `noObject` (byte `+0x10`, percent), `bonusChance` (byte `+0x11`, percent) and `bonus` (float `+0x14`).
Every draw uses the game's generator (`0x006eb880`, [Scripting](scripting.md)); `rand(a, b)` (`0x003353f0`) is `a`
to `b` inclusive. Confirmed (code) at `0x002187e4`-`0x00218a90`:

1. `roll = rand(0, 100)`.
2. The object: `none` when `roll <= noObject`. Otherwise `+0x278 = v16`, and a `str15` containing `grp_` gives a
   random member of that object group (drawn from another generator, `0x006eb8a8`; `none` for an unknown group),
   while any other `str15` is the object when `roll <= v16` and `none` otherwise.
3. The money starts at 0. If `v16` is -1 or the object is not `none`: `m = rand(min, max)`. Else, when `roll <=
   noObject`: `m = rand(min, v16)` if `min < v16`, else `m = v16`. Else the money stays 0 and step 4 is skipped.
4. When `rand(0, 100) <= bonusChance`, `m = (int)(m × bonus)`. The money is `m` clamped to 0-999.
5. Categories 4 and 6 then set the brain's `+0x26c` to 1, or 2 when `+0x14b` is 1 or the money is 21 or more;
   category 4 first draws `Random_Int(100)` (`0x003353b8`, 0 to 100) and takes 2 when it is below 10. Categories 5
   and 7 set 3.

The `CfgCharClassAttribs` records, confirmed (runtime) over PINE in level99 (a copy of the user's slot 6; game
state `0x01fd8400`):

| Category | `min`-`max` | `+0x0c` | `noObject` | `bonusChance` | `bonus` |
| --- | --- | --- | --- | --- | --- |
| 1 | 10-20 | 0.25 | 100 | 1 | 5 |
| 2 | 10-15 | 0.2 | 100 | 1 | 5 |
| 3 | 10-20 | 0.2 | 100 | 1 | 5 |
| 4 | 5-20 | 0.1 | 100 | 1 | 5 |
| 5 | 7-20 | 0.1 | 100 | 0 | 1 |
| 6 | 1-4 | 0.1 | 100 | 0 | 1 |
| 7 | 2-6 | 0.1 | 100 | 0 | 1 |
| 8 | 5-10 | 0.1 | 100 | 0 | 1 |
| 9, 10 | 5-10 | 0.1 | 0 | 0 | 1 |
| 11 | 5-10 | 0.1 | 20 | 0 | 1 |
| 12 | 10-20 | 0.1 | 20 | 0 | 1 |
| 15 | 10-20 | (not read) | 100 | 0 | 1 |

Category 15 was read in `level87` (below), the same game state; its `+0x0c` was not read.

**`PoizoCiv` (type 417)**, confirmed (runtime) from the same state: category 4, `+0x14b` 0, `v16` -1, `str15`
`grp_male_ped`. Step 2 always gives `none` (`noObject` is 100), and the money is `rand(5, 20)`, times 5 when the
second roll is 0 or 1 (2 in 101): $5-$20, rarely $25-$100. The $18 seen at runtime ([Combat](combat-throws.md#mugging))
is in range. His pocket is empty (item 0, count 0), so the mugging pays money only. A seeded reimplementation draws, in
this order: the object roll, the money, the bonus roll, then `Random_Int(100)` for `+0x26c`.

**`level87`'s sellers and Destroyer soldiers** (issue #354). Their `CfgChar` records, confirmed (runtime) over PINE
in PCSX2 2.9.94 on 2026-10-13 (a copy of the user's slot 7, `level87` checkpoints 3 and then 2 loaded through mode 8
as on [Scripts](scripting.md#level34-fixes): level index 3 to `0x005e5db0`, the checkpoint to `W_GameState + 0x33a`,
3 to `+0x14c`; game state `0x01fd8400`); categories 11 and 15 are in the table above:

| Type | Humans seen | Category | `v16` | `str15` |
| --- | --- | --- | --- | --- |
| 428 | `drugsSpawner0` (a seller) | 15 | 0 | `none` |
| 431 | `spraySpawner0`-`2` (the paint sellers) | 15 | 0 | `none` |
| 197 | `WS1`, `liqD_01` (Destroyer soldiers) | 11 | 5 | `none` |

Through the steps above (confirmed (code) at `0x002187e4`-`0x00218a90`):

- **The sellers always carry $0.** `noObject` is 100, so step 2 gives `none` and step 3 takes the `roll <= noObject`
  branch with `v16` = 0, which is not above `min`: the money is `v16`, 0, and the bonus (1.0) leaves it 0.
- **A type 197 soldier carries $5 or $0.** When `roll` (0-100) is 20 or less (21 in 101), step 3 gives `v16` = 5
  (`min` 5 is not below it) and the bonus (1.0) keeps 5. Otherwise step 2 writes `+0x278` = 5, the object stays
  `none` (`roll` is above `v16`) and the money stays 0. The class range 5-10 is never used, because `v16` is not -1
  and the object is always `none`. Types 192, 194 and 199, the level's other Destroyers, have the same record
  (category 11, `v16` 5, `none`), so the same $5-or-$0 unless a script sets their money.

At runtime (confirmed (runtime), the same method, the live humans' `+0x370` polled over PINE for about four minutes in
checkpoint 3 and five in checkpoint 2, each human's money logged when first seen; log kept in the track's scratch
folder): every seller held $0 (`spraySpawner0`-`2`, type 431, and `drugsSpawner0`, type 428, in both checkpoints).
The type 197 soldiers held $5 (`WS1`, checkpoint 3) and $0 (`liqD_01`, checkpoint 2). The other category 11
Destroyers gave the same two values: a later `WS1` (192) $5, and `WS0` (194, then 199) and `liqD_02` (194) $0; the
`CfgChar` record of type 199 is the same as 197's. `Boozer` (194) and `Knockout` (192) held $5 in both checkpoints;
most likely the script's `EDConstructor` set it with `HuSetMoney` ([Scripts](scripting.md#level87-fixes), fix 4;
inferred), and the roll's $5 branch is the alternative. `Boozer`, `Knockout` and `WS1` (197) later went to $0 while
fighting the Warriors (inferred: dropped when down, [below](#down-drop)). The civilians (type 395) held $5-$19 and the
bums (types 272-278) $1-$4, which fit category 4 for the civilians and 6 ($1-$4) for the bums (inferred: their
categories were not logged).

`Human_Init` writes `+0x278` only on step 2's object branch (stores at `0x00218850` and `0x00218888`); the only other
writer of that field is `HuSetCarriedItem` (`0x00238230`), which stores 100 (`sw` at `0x00238270`). Confirmed (code).
So a human whose roll skips step 2's object branch keeps an older value. `drugsSpawner0` (type 428, whose `noObject`
of 100 always skips it) read 100 in checkpoint 3 and 5 in checkpoint 2, confirmed (runtime). The 5 can only be left
in the slot by an earlier human. The 100 is either such a leftover or an earlier `HuSetCarriedItem`; against the
script, his logged object (`+0x257`) was `none` while `HuSetCarriedItem` sets it (inferred: a leftover). So `+0x278`
is no sign of which branch ran.

So the $0 sellers and the $0 soldier seen in Coney's `level87` run (issue #354) match the original, and Coney needs no
fix for them ([Coney's implementation](#coneys-implementation), the starting money).

#### Dropped when down {#down-drop}

A human whose health runs out drops what he carries: `Human_OnHealthOut` (`0x002674c0`, [Defeat](combat-reactions.md#defeat))
calls `Human_DropCarried(human, 1, 1)` (`0x00232c60`) when he is wounded or knocked out, unless he has human flag
`0x4` (the players and the crew, [Human flags](combat.md#human-flags)). Every human goes through it, so a spawner's
human drops his money as a scripted one does ([Starting money](#starting-money)). Confirmed (code) at `0x00232c60`; the
order of the steps:

1. No brain: nothing.
2. **The pocket** (`+0x250`, an item): `+0x254` pickups of that item's object, at his feet 0.5 m up. Unless the
   brain's kind (`+0x04`) or its `+0x26c` is 5, nothing more is dropped. (For brain kind 5 the pickups go round him
   instead, 1 m out in his free sectors, [AI: neighbour sectors](ai-moving.md#neighbour-sectors).)
3. **Power cuffs**: a human with cuffs (`Human_GetCuffCount`) drops `dyn_powercuffs` when `Random_Int(100)` is below
   51 and game state `+0x158` is set or unlock (6, 14) is; then nothing else.
4. **The carried object** (`+0x257`, not `none`): an AI human drops it unless `+0x278` is -1, and carries `none`
   from then on. A player keeps it when `rand(0, 100)` is below 51; otherwise he drops it and loses money: the
   object type's `+0x5a` × game state `+0x380`.
5. **Money** (the carried object is `none`; a human of brain kind 1 whose class has `+0xb4` = 0 drops `dyn_key`
   instead once unlock (6, 13) is set):
    - brain kind 4: his money × his category's `+0x0c` factor (0.1-0.25 in the table above), at least 1 when he has
      any;
    - any other AI human: **all** his money (`+0x370`);
    - a player: `rand(min, max)` of his category, at most what his inventory holds, taken from it.

   One dollar or more makes a `dyn_money` pickup holding the amount (`+0x124`) at his feet, 0.5 m up, with the
   respawn timer of item 2, and his money becomes 0.

So `level87` chapter 3's early Destroyers, given $5 each by their constructor, leave $5 each when beaten: the price
of one can of paint ([GoalDealer](ai-scripting.md#dealer)). **Coney** drops nothing yet ([`level87`'s
fixes](scripting.md#level87-fixes), fix 4). Fix: on health out (wounded or knocked out), run steps 1-5 with the game's
random numbers. Tests: an AI human of a brain kind other than 4 with $5 and no object, knocked out, leaves one
`dyn_money` of $5 and has $0; a civilian of brain kind 4 with $15 and factor 0.1 leaves $1; a human whose object is
`none` and money 0 leaves nothing.

#### Interrogation {#interrogation}

`+0x5a0` holds a reference to a Lua function, confirmed (code). Its only writers: `HuSetInterrogation` (`0x00239f20`:
clears the four lines and `+0x5a0`, then stores the lines and the callback; a nil callback leaves 0), `Human_Init`
and `Human_Destroy` (0), and the interrogation's success (`0x00226168`: calls the function with (victim, mugger,
true), the mugger an invalid handle when there is none, then clears `+0x5a0` and the lines, so an interrogation
succeeds once). Non-zero means "can be interrogated": `Mug_CanMugVictim` accepts the human whatever it carries and
whatever `+0x5b0` says, the prompt reads "interrogate", and the mugging plays the set lines and pays nothing.
**`SetInterrogateParam` does not touch it**: it only overrides the stick game's tuning for every mugging
([Combat: mugging](combat-throws.md#mugging)). No level99 script calls `HuSetInterrogation` (levels 2, 11 and 83 do), so
the lesson's mugging of `PoizoCiv` is a plain mugging.

The scores (crime events 4-5 to 4-7) are on [Statistics](../references/statistics.md). The mugging itself reports
no crime; type 8 (`Mugging`) comes from scripts or from the witnesses' goals, which report a type from their own
records ([AI: crimes](ai-gangs.md#crimes)).

### Tagging {#tagging}

A level's tag spots are `part_spray_tag` particle objects (class bit `0x10` marks a particle system; configured by
`CfgTagSettings`) with flags of activity 12
([World flags](flags.md)); `global.lua`'s helper loads a **pattern** (`HuTagPattern`) and calls `HuTag`.
Circle + triangle (`0x24`) also sprays where the player has paint ([Combat](combat.md#dispatch)).

**`HuTag`** (`0x00238db0`), confirmed (code): a player with **no spray paint** (item 3) says 37 `nopaint` and the
**human himself** gets event `0xe` with the tag and 0 (not finished; the record goes through the human's own vtable
`+0x44`, so a script's event-14 handler also runs for a refusal, [below](#tag-callbacks)); otherwise the human's
target (`+0x33c`) becomes the flag, `+0x36c` the tag, its action `0x17`, and a player occupies the flag. `HuTag`
calls no script itself.

**The stick game**, confirmed (code) at `0x002741d8`, `0x002748a8`, `0x00274710`:

1. The pattern's points (`0x00510914` count n, float (x, y) pairs at `0x006cd978`) are sampled along a uniform
   **Catmull-Rom** curve (`0x00273ff0`): segment i runs from point i to i + 1 with weights
   (−t³/2 + t² − t/2, 3t³/2 − 5t²/2 + 1, −3t³/2 + 2t² + t/2, t³/2 − t²/2) on points i − 1, i, i + 1, i + 2, the
   indices clamped to the first and last point (so the last segment stays on the last point). Each segment, i = 0
   to n − 1, takes ⌊300 / n⌋ samples at t = k · n / 300 (k = 0, 1, ...). A sample's x and y are truncated to
   integers and their low byte kept, so pattern values are **grid cells 0-255** directly (no scaling; larger values
   wrap). A sample joins the path when its squared distance from the last one kept is more than 6 (the first is
   compared with (0, 0)); at most 300 samples, so at most 300 path points. The tag's painted fraction (its `+0xc8`,
   [tag spots](#tag-spots)) × the path length, truncated, is the start index s (0 for a fresh tag). Every path point
   is also copied into the painted list, whose count is set to s, so a half-sprayed tag shows its first s points
   already painted. The cursor starts on point s.
2. The **cursor** moves with the left stick when its squared length is at least 0.04 (0.2): by stick × elapsed ms ×
   the speed, the elapsed time being the game clock since the last unpaused update, at most 30 ms. The speed ramps
   linearly from 0 over 2 s from the **ramp start** (`+0x51c`: the first update, and again at each new charge, see
   4; a slip does not restart it). The cursor stays inside 0-255 on both axes. The stick is the per-player record's
   `+0x00` / `+0x04` floats (the pad's raw stick, not turned by the camera). The cell under the cursor (both floats
   truncated) is painted when its squared distance from every painted cell is at least 4 (`+0x04`); a 301st cell
   ends the game (5).
3. **Tracking** (`+0x500`, `TagGame_IsOnTrack`), with p the progress index and the cursor's truncated cell; "near" is
   a squared distance below 36 (within 6 cells):
    - near any of the 4 points **behind** (p − 4 to p − 1): on track, p unchanged (progress never goes back);
    - else the **first** of p, p + 1, p + 2, p + 3 (not beyond the last point) that is near: on track, and p becomes
      that index. Point p itself is tried first, so p stays put while the cursor is within 6 cells of it and moves
      on only once the cursor has left that circle (not to the nearest point: to the first one in order);
    - else off track: the **off-track count** (`+0x510`) goes up by one if the stick moved this update.

   The count is **not** reset by coming back on track: it is cleared only by a slip or a new charge. At the start
   of a moving update, after painting and before tracking, a count above 4 is a **slip**: the cursor snaps to point
   p, the count is cleared, the game pauses for the pause time (`+0x514` = clock + pause), the pad rumbles (per-player
   `+0x1d` = 1, `+0x1c` = 255) and a crew member may comment (speech 80 `tagcheer`, when the tagger's `+0x19c` is
   set; [who speaks](sound-events.md#tag-lines)). So five off-track moving updates since the last slip (30 Hz: about
   a sixth of a second in all, not necessarily in a row) make the sixth moving update slip.
4. **Paint** (`TagGame_SpendPaint`, `0x00274710`, run first in **every** update, paused ones included, so the charge
   keeps draining through a pause): the first update stamps the charge start (`+0x518`); when the charge time has
   passed, a new charge starts: the cursor snaps to point p, the off-track count is cleared, the game pauses and
   the ramp restarts from the end of the pause; then one can is taken (item 3 − 1) if there is one, else the game is
   over (`+0x4fc` = 1, progress set to the path count). The first charge takes no can when it starts, but
   `Tag_UpdatePlayer` ends the game once the can count is 0, so n cans give n charges.
5. **Finished** (`Tag_IsComplete`, `Tag_GetProgress` `0x00274e38`) when the path count < p + 4, that is
   **p ≥ count − 3**: the third point from the end, which is where the drawn guide ends (the panel leaves the last 3
   points out, [HUD](hud-functions-panels.md#fn-after-subtitle)). The progress is then 1.0; below it is p / count, the
   value sent to the tag each update. Painting a 301st cell or running out of cans also makes it 1.0 but sets `+0x4fc`,
   which `Tag_UpdatePlayer` treats as unfinished.
6. Any command outside the game's own (`PlayerCmd_IsOtherInput`), no cans left, or the action becoming `0x18` ends
   it. A paused update (before `+0x514`) runs only step 4.

The **tuning**, by the Warrior class's byte `+0x09` (1-3, 0 counting as 1), from the table at `0x00510918`, unless
`HuTagDifficulty` sets its own (no script does):

| Difficulty | Charge time | Pause | Speed (cells per ms) |
| --- | --- | --- | --- |
| 1 | 13,000 ms | 500 ms | 0.05 |
| 2 | 11,000 ms | 600 ms | 0.07 |
| 3 | 9,500 ms | 500 ms | 0.073 |
| `HuTagDifficulty` defaults | 15,000 ms | 1,000 ms | 0.06 |

**The end**, confirmed (code): on a finish the player's update (`Tag_UpdatePlayer`) marks the tag sprayed (`+0x3ba`,
messages `0x41` 1.0 and `0x19` 3) and a crew member says 83 `tagdone` ([who speaks](sound-events.md#tag-lines)); then
`Tag_End` (`0x0022e848`): a clean finish scores bonus event 1-3; unfinished with less than 30 %
of the current charge left, one more charge is spent. The tagger's flag is freed, the **tagger** gets event `0xe`
(the tag's handle and whether it was finished, through its own vtable `+0x44`) and the **tag object** (human
`+0x36c`) gets message `0x13`. Hint `0x10` in level `0x57`.
Tagging is crime type 10, which sends no responders ([Crime types](../references/crime-types.md)).

#### What the player sees: body, paint, mist and the wall {#tag-look}

**The body.** `Tag_StartSprayClips` (`0x00278018`, from `Tag_UpdatePlayer` on the first update in action `0x17`)
drops the held object only when its type's anim set (`+0x87`) is 4 (`OVERHEAD_WEAPON_SET`) or 6 (`GHETTO_SET`)
(`Human_HoldsKind4Or6`). A pipe (`dyn_pipe_a`), a bat or a crowbar is set 3 (`CLUB_WEAPON_SET`, disc check of
`CfgObj`'s 20th argument) and **stays in the hand**; what 334/335 do with it is not traced. It queues 334
`ANIM_TAGGING_INTRO` (end hook `Tag_OnIntroEnd`), then the loop 335 (loop hook `Tag_StartSprayEffect`), sets held
flag `0x400` (meaning not traced) and calls `Human_TurnToFacePoint(0.5 × the play time of 334, human, the tag's
point)`: the turn to the tag object's position is spread over the first half of the intro. Confirmed (code). When a
pause begins (a slip or a new charge; `TagGame_IsPaused` `0x002745e8` turns on during the update),
`Tag_RestartSprayClips` (`0x00278210`) plays 337 and then the loop 335 again, with the same hooks.

**The end** (issue #407). Confirmed (code) at the addresses unless marked. `Tag_EndSprayClips` (`0x002783b0`) plays
336 `ANIM_TAGGING_END`, whose event `0x31` at frame 7 removes the can that 334's event `0x30` made at frame 10
([props of a clip](combat-grabs.md#power-move-props), human `+0x348`). Its callers:

- `Tag_UpdatePlayer` (`0x00273c60`, a player's spray, after the stick game's update), at `0x00273f10`: a **finished**
  tag (`Tag_IsComplete`), **any other command** (`PlayerCmd_IsOtherInput`: the player lets go), or **no cans** left;
  after it the action is set to 0. At `0x00273dc0`: the action became **`0x18`** while tagging.
- `MiniGame_Update` (`0x00255fd8`, a human that is not a pad player, `+0x1b0` = −1, so an **AI tagger**): action
  `0x18` while tagging (`0x00256004`), or any other command while tagging (`0x00256034`, then action 0). The AI's
  `Tag` action presses command 10 each update ([AI code](ai-code.md#actions)), so the end comes from `0x18`.
- **Who sets `0x18`**: the human's message `0x13` (case `0x00245978` of `Human_HandleMessage`, jump table
  `0x0055c980` indexed by message − 1): while tagging, action `0x18`; a human that is not a pad player
  (player record `+0x1b` = 0) also gets `+0x3ba` = 1, a finished tag. The [tag spot](#tag-spots) sends the tagger
  `0x13` when its fade reaches 1 or 0 or when it is hidden. So an AI tag ends with 336 when the wall is painted, and
  a player's ends with 336 if his spot is hidden. Message `0x17` with state 4 while tagging also sets `0x18`
  (`0x00245a94`; who sends it is not traced).
- `Tag_OnIntroEnd` (`0x00277f10`) calls it only when the tag object no longer resolves, and then it **does nothing**:
  its first step returns when `+0x36c` does not resolve (`0x002783e8`). No 336 and no `Tag_End` there; what then
  ends that spray is not traced.

What it does, in order:

1. Takes three tasks. Makes a **fade** (type 9) of **0.05 s** (`0x3d4ccccd`), no flags, no owner.
2. **Cuts off** the current top task (vtable `+0x84`; the bottom one when the top is a fade): the loop 335, or 337.
   A clip task clears its held flags as it is cut off ([Task fields](formats/animation.md#animation-tasks)).
3. Push sphere call with 0, as `Tag_StartSprayClips` makes (`Human_SetPushSphere`, `0x0021c058`).
4. Picks the clips by `Human_IsInFightStance` (`0x00228340`, state word `0x00` any of `0x3`):
    - **Not in the stance**: anim state (record `+0x18`) = 0; the idle **loop** (type 1) of record `+0x28` (anim
      slot 0, the idle), no callback; then 336 as a type 3 task with blend time **0** into that loop, holding
      **`0x800000`**, owner the human, end hook `Tag_OnEndClipEnd` (`0x002783a8`, an empty function).
    - **In the stance**: held flag **`0x80000`** set now; the loop of record `+0x54` (anim slot 11, the fight idle,
      358 for Rembrandt); anim state = `0xb`; a type 3 task of the id **record `+0x54` + 1** (359
      `ANIM_FIGHT_TRANSITION_FROM_NORMAL` when the fight idle is 358) with blend 0 into the fight idle, holding
      `0x80000`, no hook; then 336 with blend 0 into that clip, holding `0x800000`, hook `0x002783a8`. So 336, then
      the neutral-to-fight transition, then the fight idle.
5. Inserts 336 at the bottom of the task stack and pushes the fade on top: the usual change of animation
   ([Task stack](formats/animation.md#task-stack)), a 0.05 s blend from the spray pose into 336.
6. Sets held flag `0x800000` (cleared by 336's task when 336 ends or is cut off).
7. Calls **`Tag_End` at once** (`0x002785ec`), in the same update, not when 336 ends: action 0, state `0x2000000`
   cleared, the stick game freed, the bonus or the extra charge, the flag freed, the **tagger's event 14** (the tag,
   finished `+0x3ba`), `+0x36c` emptied, message `0x13` to the tag object, the voice stopped
   ([The end](#tagging)). So event 14 comes on 336's first update, about 7 frames before the can goes.

It sets no turn and no velocity, and it does not clear `0x400` (that flag is held by 334's task and goes when 334
ends). 336 is not muted (no task flag `0x10`), so its events fire. While `0x800000` is held, `MiniGame_Update`
skips the tag (any held flag) and `Human_ApplyPendingDamage` gives a hit no reaction (`0x1c16a40` includes
`0x800000`, `0x0026668c`; the props are released before that test).

**Ends without 336.** `Tag_End` alone, with no clip: the update with the action no longer `0x17` / `0x18`, not
tagging, while the tag still exists (`Tag_UpdatePlayer` `0x00273f6c`, `MiniGame_Update` `0x00255fa8`: the cut-short
intro of issue #330); a **hit** (`Human_ApplyPendingDamage`, `0x002667ac`, while tagging or while the tag exists),
a death (`Human_PlayLethalReaction`, `0x00268068`), a grab or mount (`Grab_Link` `0x0022c8dc`, `Pair_LinkMount`
`0x0022c178`, `Grab_BreakOnHit` `0x0022ba5c`), a broken pair (`Human_BreakPair` `0x00258af8`) and a mode reset
(`Human_ResetForMode` `0x00232740`). `Tag_End` itself never touches the can or the clips. On those paths the can goes
another way: `Human_ReleaseCarriedProps` (`0x0024ce40`) runs, unless the human is cuffed, from every damage dealt
(`Human_DealDamage` `0x0021d81c`), every attack warning (`Human_WarnOfAttack` `0x0021d658`), the pending damage of a
non-lethal hit (`0x002664a8`, before the reaction), a death (`0x00267ee4`) and a mode reset such as a scene start
(`0x00232954`); and for an AI human each update that asks for a gait above 0 or is in a scene state
(`Human_UpdateControl`, `0x002439f4`, `0x00243a0c`). So a hit removes the can on the hit's update. Whether a grab
always passes a warning or damage first, and so drops the can, is not traced. Runtime: not run (the prop at `+0x348`
around 336's frame 7 has not been watched in PCSX2).

**The paint colour** is the tagger's human `+0x640`, four bytes. `Human_Init` (`0x00218770`) stores bytes
`50 07 05 20` (in memory order) for everyone; a human whose class has brain kind 4 (`+0x11a`, after the `+0x19d`
remap) and `+0x3b8` = 1 gets `ff ef ef ef` instead (`0x002187b0`). `HuTagColor(h, {t1, t2, t3, t4})` writes
t4, t3, t2, t1 in that byte order (`0x00239080`: `f12` = t4 to byte 0 in `Colour_PackFloat` `0x0017aca8`). The
two readers take the bytes in **opposite orders**:

- the **HUD** (`TagHud_Render` `0x001cb798`) keeps bytes 0-2 and puts its own alpha in byte 3 (sprite colours are
  r, g, b, a in memory: the path's grey is `CONCAT13(0xff, c c c)`): painted cells (rgb, 205), the charge bar
  (rgb, 255). So the HUD shows (t4, t3, t2); the default is **(80, 7, 5)**, a dark maroon;
- the **particles** read the word as `0xRRGGBBAA` with alpha in the low byte (`SprayMist_Init` `0x003aad68` keeps
  `colour & 0xffffff00` as the previous colour, fading in from alpha 0): the mist is (t1, t2, t3) at alpha t4, the
  scripts' `{r, g, b, a}` read as written; the default is **(32, 5, 7) at alpha 80**.

Confirmed (code) at the addresses; the default **confirmed (runtime)** (PCSX2 2.9.94, PINE, a copy of quick-save slot
6, `level99`): every human in use, the player included, had bytes `50 07 05 20` at `+0x640`. The scripts' values
(disc check, counts only): level 34's Moon tagger `{0, 0, 40, 200}`, level 93's Destroyer tagger
`{121, 155, 213, 75}`, a level 2 dancer `{75, 175, 0, 25}`; level 87 colours only `sguy` (`DEST_SPRAY`), never the
player, so **the player's paint is the default** in mission 3.

**The spray mist**, confirmed (code):

- `Tag_StartSpray` (`0x0022e610`) and `Tag_StartSprayEffect` (`0x00277f28`, each time the loop 335 starts) create a
  **`sub_spr`** (`0x003aa900`) and play `vags/character/spray_loopx28` on the tagger. The `sub_spr` is attached to
  the tagger at **attach point `0x13`** (the arguments, popped in reverse: the mode 1, the rotation, the position,
  the attach point `0x13`, the human, the colour `+0x640`). Every 2 ticks for 3 updates (`0x003aaa40`) it makes
  two **`spray_mist`** at the attach point, each with a random velocity of −0.025 to 0.025 on each axis, in the
  colour above.
- `spray_mist` (`0x003aad68`, update `0x003aaf28`): `part_page1` (batch 4) rectangle 42-44 at random, size 0.05,
  random spin ±90°, an update every 2 ticks; it grows 0.015-0.03 a step, fades and slows at step 5, is done at 6.
- On the wall: each update's progress message `0x41` (below) makes the tag spawn mist **at the paint's leading
  edge** while the progress is between 0.03 and 0.95 (`SprayTag_SpawnMist`, `0x003fbe98`): with (w, h) the tag's
  extent (particle `+0xa0`, `+0xa4`; where it is set is not traced), x = (progress + a random 0-0.03) × w − w/2
  along the tag's own axis, a column of max(1, ⌊h / r⌋ − 1) puffs from the bottom (−h/2) up in steps of r = a random
  0.6-0.7, each a `spray_mist` in the **tagger's** colour, flagged to start at step 4 at size 0.45 (a quick big
  puff). It stops when the particle pool is full or the point fails `Effects_CanSpawnAt(25 m, 20 m)`.

**The paint on the wall** is not drawn stroke by stroke: the tag object is one **pre-made image** whose opacity is
the progress. Confirmed (code) unless marked:

- `SprayTag_Init` (`0x003fc600`): the spot is placed 0.5 m along its own +y from the spawn point, then a ray of
  10 m back along −y (`0x003a36e0`) finds the wall; on a hit it moves to the hit point + 0.05 × its +y and its
  rotation is built from the wall's normal (inferred: the global vectors 1 and 2 read after the ray are the hit
  point and normal). Its sprite batch (`SprayTag_SetSprite` `0x003fc218`) is `PTank_New(depth 10, sprite, position,
  1, flags, 255)` with the sprite word `0x50000` until `CfgTagSettings` sets one, and the task's flags `0x00508001`;
  bit 0 of the flags makes the batch format 2 where the camera-facing fog uses 0 (`0x003a4e88`; inferred: an
  **oriented quad lying on the wall**). `CfgTagSettings`' third argument (`+0xc0`, 0.5 by default; 0.8-3.5 in the
  scripts) is the task's size field ([Particles](particles.md#task-fields)), so it is the image's size (inferred).
- The drawn opacity is `+0xc8`, set from the painted fraction `+0x10` (`Task_SetFieldC8`). During the stick game
  `Tag_UpdatePlayer` (`0x00273c60`) sends the tag message `0x41` with `Tag_GetProgress` **every update**, so the
  image fades in evenly as the progress goes (0 to 1 over the path; 1.0 at the finish, then `0x19` state 3 keeps it).
  A spot not finished keeps its fraction, and the next spray starts at that point of the path (step 1).
- The image's own colours are drawn with the task colour (white by default, `0x005fd268` = `0xffffffff` at runtime),
  so the wall tag does not take the paint colour: a Warriors' tag is its texture. Level 87's club wall is three
  layered tags (`CfgTagSettings` sheet records `0x1fe`, `0x200`, `0x202`, size 3.5; prompts "base", "fill",
  "detail"); its earlier spots use the default sprite.

#### Coney status and fixes {#tag-coney-fixes}

The checklist items (2026-10-10): the paint is black, no mist, no paint on the wall, a short near-black smear on the
HUD, a dark charge bar, the Warrior not turned to the wall and still holding a pipe, and the tag ending just before
the W is complete. Causes and fixes, for the implementer.

**Done** (2026-10-11): 1-5, and 8 but the turn with no intro clip, which Coney makes at once (**Coney's reading**: a
missing clip plays for 0 s); 9 for player 1 (2026-10-12, `HumanAnimator::playTagEnd`; an AI tagger's end still plays
no 336). **Not yet**: 6 (the mist) and 7 (the wall tag).

1. **Ends early** (`TagGame::update`, `repo:src/warriors/tag_game.cpp`): Coney finishes at
   `progress + 4 >= size − 1` (p ≥ count − 5), two points early, and its tracking picks the **nearest** point in
   ±4, so the progress can jump up to four points ahead at once. Use step 5's p ≥ count − 3 and step 3's tracking
   (back 4 without moving p, forward first match from p, squared distance < 36 on the truncated cell).
2. **Off-track count**: cumulative until a slip or a new charge (step 3), not reset when back on track; the slip is
   tested at the start of the next moving update after painting.
3. **Charge during a pause**: the charge time keeps running through slip and charge pauses (step 4); the ramp
   restarts at each new charge, measured on the game clock from the end of its pause.
4. **Paint colour**: give every human the `Human_Init` default (bytes `50 07 05 20`; `ff ef ef ef` for the brain-kind
   4 case) instead of 0, and keep `+0x640` as the four bytes `HuTagColor` writes (t4, t3, t2, t1). The HUD's painted
   cells and charge bar use bytes 0-2 (default (80, 7, 5)); the mist uses `0xRRGGBBAA` with alpha in byte 0
   (default (32, 5, 7, 80)). Coney's `HuTagColor` now packs t1 into the red byte of both, which is right for the mist
   only (`repo:src/scripting/story_bindings.cpp`, `repo:src/gamemodes/gameplay_tag.cpp`).
5. **Cursor colour** (`TagHud::cursorColour`, `repo:src/hud/tag_hud.cpp`): plain white while **on track**
   (`+0x500`), flashing white and black while off track; the pause does not choose it (Coney passes
   `pauseLeftMs > 0`). During a slip pause the flag keeps its last value (off track), so the cursor flashes.
6. **Mist**: the `sub_spr` burst at attach point `0x13` when the spray starts and each time the loop 335 starts, and
   the wall's leading-edge column on every progress message, as above; `spray_mist`'s numbers are in
   [Script types](script-types.md#emitters).
7. **The wall tag**: draw each `part_spray_tag` as its sprite (the `CfgTagSettings` sheet and rectangle, `0x50000`
   by default) on a quad on the wall after the ray snap, sized by `+0xc0`, at opacity = its painted fraction; feed it
   the progress each update (Coney's `TagSession` already sends `0x41`).
8. **Turn and weapon**: the turn to the tag over half of 334 should be checked with a test (the play-tester saw
   none); when 334 is missing Coney skips the clips and so the turn too (`GameplayMode::startTag`), where the
   original still turns. A held pipe staying in the hand is **faithful**; drop only anim sets 4 and 6. Restart the
   clips with 337 when a pause begins.
9. **The end clip** (issue #407): Coney never plays 336, so the can 334 made stays in the hand. End every spray
   that `Tag_EndSprayClips` ends as [The end](#tag-look) says: 336 after a 0.05 s fade, then the idle (or the
   transition and the fight idle), `Tag_End` on the same update; the can goes at 336's event `0x31`. A hit, death,
   grab or reset ends the spray without 336, and the hit or reset releases the can itself.

**Tests** (synthetic, no game data): a straight 20-point path finishes when p reaches 17 and not at 15 or 16; a
cursor within 6 cells of point p and of p + 1 leaves p unchanged; a cursor near p − 2 keeps p; five off-track moving
updates split by on-track ones make the next moving update slip; a charge that ends inside a slip pause starts the
next charge on time; the ramp restarts after a new charge but not after a slip; `HuTagColor(h, {1, 2, 3, 4})` gives
HUD rgb (4, 3, 2) and mist `0x01020304`; a human never given a colour draws HUD cells (80, 7, 5, 205); the cursor is
white on track and flashes off track and during a slip pause; the wall tag's opacity equals the progress after each
update and stays 1.0 after a finish; a human holding a set-3 object keeps it at the spray's start, one holding a
set-4 object drops it.

#### The script callbacks {#tag-callbacks}

The engine calls a script at two points of a tag, and nowhere else (no progress, layer or cancel callback: the only
reader of the start callback's name `0x006b6870` is `0x00238f50`, and `Tag_UpdatePlayer`, `TagGame_*` and `Tag_End`
call no script). Confirmed (code) at the addresses cited.

**The start callback** ([`CfgTagStartCallback`](../references/bindings/config.md#cfgtagstartcallback)), called by
`0x00238f50`:

1. **When.** Not at `HuTag`. Once the human is in action `0x17` and not yet tagging, the update
   (`Tag_UpdatePlayer` for a player, which first makes the stick game; also `MiniGame_Update`, `0x00255fd8`) calls
   `Tag_StartSprayClips` (`0x00278018`), which queues 334 `ANIM_TAGGING_INTRO` (with the end hook `0x00277ed8`) and
   then the loop 335. Nothing walks the human to the flag or places him: he stays where he pressed triangle (within
   the flag's kind-1 reach, 1.1 m) and turns to face the tag object (its vtable `+0xac` point) over half of 334's
   length (`Human_TurnToFacePoint`, `0x00221c20`). A held weapon of kind 4 or 6 is dropped first. When that
   hook runs and the tag still exists, `0x0022e610` starts the spray: it sets the tagging state `0x2000000`, sends
   the tag message `0` (the tagger, [Tag spots](#tag-spots)), and last calls `0x00238f50`. So the callback runs once
   per spray, at the moment the stick game goes live; `0x00238f50` does not check for a player.
2. **Arguments: three object handles, in this order** (each pushed with the script system's handle slot `+0x5c`,
   `0x00356fc8`, as the same number a script gets from any binding returning a handle):
   1. the **tagger** (the human's own handle, its vtable `+0x2c`);
   2. the **tag object** being sprayed (human `+0x36c`, the `tag` given to `HuTag`);
   3. the **flag** sprayed from (the human's target `+0x33c`, resolved by `0x00227060`: the `flag` given to
      `HuTag`).

   Snippet (`0x00238fdc`-`0x00239058`): `ori a1,sp,0x4` / `lw v0,0x36c(s2)` ... `jal 0x00227060` ...
   `li a1,0x3` / `move a2,zero` / `jalr` slot `+0x8c`.
3. **Result**: none asked: slot `+0x8c` (`0x00357188`) is `lua_call` with 3 arguments and 0 results, so a return
   value is dropped. The call is immediate (not scheduled). Nothing happens when no name is set (`global.lua` sets
   `nil`) or the human is gone.

**The end event**: `Tag_End` (and `HuTag`'s refusal) give the **tagger** event `0xe` (record `+0x00` the tag object,
`+0x04` 1 finished or 0, `+0x24` the human). The message marshaller (`0x00384ce0`, case `0xe`) calls the human's
message handler as `(self, tag, finished)`: three handles/numbers, `finished` the number 1 or 0 (unsigned, not a
boolean), and it **asks one result**; a non-zero number means the event was consumed
([Message handlers](scripting.md#message-handlers)). That a human's vtable `+0x44` reaches its handler component is
inferred from the event-14 handlers the scripts set ([Script events](../references/script-events.md#event-14)).

**What the scripts do with the start callback** (our summary): every one takes three parameters. Level 87's and level
9's look the **third** (the flag) up in their own list of tag spots to find which spray it is, lock the **first**
(the tagger's) pad movement and turn on that spot's tag camera; level 3's first checks that the **first** is a
player, then has a crew member comment and switches cameras. A callback called with no arguments therefore indexes a
table with `nil`. None returns a value.

### Tag spots {#tag-spots}

A `part_spray_tag` (init `0x003fc600`, handler `0x003fc8d8`, update `0x003fca68` every second frame) keeps a small
record: its sprite batch, a **fade mode** (`+0x04`: 0 still, 1 fading out, 2 fading in), the sprite word, a **spray
mode** (`+0x0c`: 7 the next spray paints the tag in, 5 it wipes the tag out; 7 at start), the **painted fraction**
(`+0x10`, copied to object `+0xc8`, the drawn opacity and where the stick game starts; 0 at start), the fade step
(`+0x14`, 0.005), the depth (`+0x18`) and the **tagger** (`+0x1c`, a human). Confirmed (code) at the addresses.

| Message | Effect |
| --- | --- |
| `0x00` (tagger) | stores the tagger, then as `0x12` |
| `0x12` (shown) | sets path flag 8 on the path polygon at the tag ([AI](ai-moving.md#path-planning)); if the spray mode is 7 with fraction < 1, or 5 with fraction > 0, and there is a tagger: the tagger gets message `0x17` (state 1, the tag's position) and the tag sends itself `0x19` with the spray mode (7 starts the fade in, 5 the fade out); otherwise the tagger, if any, gets `0x13` |
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
(`0x00395d98`) and `Item_TakeOwned(taker, object)` (`0x00412dc8`) runs, confirmed (code):

1. **Message 7 goes to volume boxes only**, as message 6 does ([Scripts: triggers](scripting.md#triggers)): every
   box (handles `0x26c`-`0x2eb`) that is enabled (`+0x68`), of kind 0 (slot `+0x64`), has a Lua handler for 7 and
   whose inside test (`0x00412a18`) holds for **the object's position** (not the taker's). The Lua handler gets
   **(taker, object)** (marshaller case 7, `0x00384ce0`). No human is sent message 7.
2. Humans within **30 m** of the object are alerted (`AI_AlertNearby`, `0x00293768`) with the message and the spot.
3. A **player** taker scores statistic [4-8](../references/statistics.md#stat-4-8) (`Stats_AddCrime(..., 8, 1)`);
   an AI taker of brain type 3 (a Warrior) whose gang leader is a player scores it for every player
   (`Stats_AddCrimeAllPlayers`).
4. An **AI taker** then says line 128 `stealitem` (30 %) or 143 himself (not a witness), only when he may gesture
   (`Ambient_MayGesture`) and the shared 8 s timer allows (`AI_Every8s`).

When it runs: the pick-up search (`Human_PickUpSearch`, `0x0024d810`) calls it at the press, for the player and an AI
alike; for any object but a `TYPE_SPECIAL` it then clears the owned byte (`0x00395d98`). A `TYPE_SPECIAL` keeps the
byte, and `Human_PickUpObject` (`0x0023bf00`) calls `Item_TakeOwned` again only on its path for a human with none of
record flags `0x5c7eae0` (not during a pick-up clip, which holds `0x4000`), followed by a turn to the object and
overlay clip 667. Confirmed (code).

**What a take of a `TYPE_SPECIAL` gives** (`Human_PickUpObject`, the store items' case: model hash not one of the
named mission items, during the pick-up clip, type class name `pickup_item`), confirmed (code) at `0x0023bf00`:
nothing for brain types 2 and 4 (gangs, civilians); for a player, item 10 ×1 (notify) and the money (the type's
value × game state `+0x380`) to his own panel; for a **Warrior** (type 3), item 10 to his own panel, which an AI
crew member does not have (`+0x380` = −1, which `Inventory_AddItem` ignores), and **the money to his gang leader's
panel** (the player's, without notify), with item 10's pick-up sound played in 2D. So a crew member's theft pays the
player and adds no loot item.

### Stores {#stores}

A store is a set of flags ([World flags: groups](flags.md#groups)) with a store front (activity 14), browsers and
buyers. A **break-in** (crime type 1) comes from an alarmed pane ([panes](objects.md#pane)), a lock pick that was not
perfect, or a third abandoned pick. The report ([AI: crimes](ai-gangs.md#crimes)), confirmed (code) at `0x0041b8b0`: within
10 m the store front is marked robbed (bit 16 of its group word, the offender's gang in bits 18-22), the store's
buyers and browsers are switched off, the nearest `strobe` within 6 m gets message `0x12`, and responders are queued
with a delay of `CfgBreakAndEnterDelay` (15, 10 or 7 by difficulty). The first gang to rob a store scores crime event
4-1 for its leader when he is a player. `ResetStore` (`0x0041ddd8`) clears the robbed bit, sets the group word's bits
8-15 to `0xff` and the gang bits to 31, and restocks the store; its 7 m test (squared distance under 49, message
`0x13`) is a strobe's distance from the store's flag, not the player's (confirmed (code)). The two strobe distances
differ in the code: a break-in reaches a strobe within 6 m of the store's flag, a reset one within 7 m. `EnterStore` /
`ExitStore` only change the screen colour.

#### Which break-ins call the police {#break-in-police}

`GameState_ReportCrime` (`0x0041b8b0`) handles type 1 differently from every other type (its `case 1`), confirmed
(code) at `0x0041b8b0`, `0x0016df68` and `0x0016dc70`:

1. It looks for the nearest flag of activity `0xe` (a store front) to the report's position
   (`Flag_FindNearestByActivity`) and goes on only when one exists and lies within **10 m** (squared distance under
   100). **No store front within 10 m: no police, no strobe, nothing robbed.** The wanted timer, hostility and
   crime callback run before this, but only with an offender (step 2 of [AI: crimes](ai-gangs.md#crimes)).
2. Only when that store is **not yet robbed** (bit 16 of the flag's word `+0xd8` clear) and the call asked for
   responders (`mode` 1): `Responders_Queue(1, kind 1, count = CfgCrimeResponders(1) (game state +0x295), delay =
   CfgBreakAndEnterDelay (0x005148a4) seconds, the crime scene, no target, no offender)`. Then the strobe's message
   `0x12` and the store's buyers switched off ([above](#stores)). A store already robbed only fills in the robbing
   gang (when the bits hold 31) and scores its leader's crime event; **a second break-in at the same store brings no
   police** until `ResetStore`.
3. `Responders_Queue` (`0x0016df68`) does nothing for a count of 0. Otherwise it takes, over all 32 gangs' four
   spawners, the one in state **4** (police dispatch) or **6** (dispatching) nearest the crime scene whose queue is not
   full (`+0xb0` < 4), and `Spawner_QueueCall` (`0x0016dc70`) appends the entry: due at now + delay × 1,000 ms, the
   type, kind and count. **No spawner in state 4 or 6 anywhere: no police.** The spawner then makes a `Responder<n>`
   gang of type 1 when the entry is due, while the police count is under its limit, and places the squad out of sight
   ([AI: spawners](ai-gangs.md#spawners), steps 4-6).

**Who reports a break-in** and with what, confirmed (code):

| Reporter | Position | Offender | Mode |
| --- | --- | --- | --- |
| A lock pick that was not all perfect, or a third abandoned pick (`LockPick_End` `0x0022d908`, both calls) | the picker's position | **none** (`0x006ebd30`), so no wanted time, no police hostility, no callback | 1 |
| An alarmed pane ([panes](objects.md#pane)) and the others in [AI: crimes](ai-gangs.md#crimes) | their own | their own | their own |

So a lock-picked store door brings `CfgCrimeResponders(1)` police after `CfgBreakAndEnterDelay` seconds (15, 10 or 7
by difficulty), from the nearest police dispatch spawner, only when the door is within 10 m of a store front that
was not yet robbed; the picker himself is not made wanted by the pick. The counts the scripts set for type 1:
`config_easy` 2, `level14` 2, `level87` 1, `level95_robinhood` 1 ([Crime types](../references/crime-types.md)).
Whether the responders then attack the player's gang when the pick reported no offender (no police hostility was
set) is not traced: the dispatch goal's target is the crime scene (inferred from the queued entry, which carries no
target).

**User item 13 (no police on a break-in)**: Coney's report (`repo:src/warriors/crime_reports.cpp`, checked
2026-10-11) calls `CrimeServices::queueResponders`, whose only implementation is the interface's empty default, so
nothing is ever queued; Coney also has no dispatch queue on its spawners (`GameplayMode`'s scout hook logs its call
only). **Coney fixes**:

1. Build the spawner dispatch queue ([AI: spawners](ai-gangs.md#spawners) steps 1, 4-6, [Spawner
   states](../references/spawner-states.md) 4 and 6): four entries per spawner (time due, type, kind, count, target,
   point, offender gang), served when due; state 4 makes a `Responder<n>` gang of type 1 while the game's police
   count is under its limit and goes to 6, which places the squad out of sight and returns to 4 when it is complete.
2. Implement `queueResponders` in the level's crime services as `Responders_Queue`: nearest spawner in state 4 or 6
   to the crime scene with a free queue slot, delay in seconds.
3. Gate type 1 as the original does: responders, the strobe and the buyers only when a store front (activity `0xe`)
   lies within 10 m of the report and is not yet robbed; today Coney asks for responders before and regardless of
   the store test.
4. Report a lock pick's break-in with **no offender** (Coney passes the picker), so the pick sets no wanted time,
   no hostility and no callback.
5. Test (pad-driven, a level with a police dispatch spawner and a pickable store door, e.g. `level87`'s stores):
   pick the door with a non-perfect press, assert a `Responder` gang appears after `CfgBreakAndEnterDelay` seconds
   with `CfgCrimeResponders(1)` members; a second pick at the same store queues nothing; a pick away from any store
   front queues nothing.

### Wanted and the crime level {#wanted}

**Wanted** is per gang, confirmed (code): every crime report with an offender sets the offender's gang wanted for
**10 s** (gang `+0x5e8` = now + 10,000 ms, `0x00169878`), after making the police hostile to it. `Gang_UpdateWanted`
(`0x001698f0`) holds it at 10 s from now while `ForceCrimeLevel` is on (game state `+0x28c`); otherwise, once it
passes, `GangClearWanted`'s work runs (`0x00169a20`): the police are neutral again and, for the player's gang, the HUD
is told (`0x001b2520` message `0xb`) and the last crime type becomes 14 (`NoCrime`). While wanted, the HUD shows
`CfgCrimeMessage` of the last crime type (a plain assault: 0) and the first report of each wanted spell sounds the
`hudalarm` cue ([HUD: the crime message](hud-functions-parts.md#fn-radar-frame)). The HUD shows the time left as a
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

## Code index {#code-index}

Every crime function of the human code (`0x002176b8`-`0x00288000`) that the sections above do not walk through, in
address order, with what it does. Names are ours, as in the local Ghidra project, where each function also carries a
plate comment. The rest of the human code is indexed on [Characters](characters.md#code-index).

### Mini-games, mugging and tags {#code-crimes}

The start and end of each mode of the [mini-game record](#mini-game-record), the mug and the tag spray.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00228588` | `Human_IsInButtonMiniGame` | A player whose mini-game record mode (`+0x46`) is 5, `HuButtonMiniGame`. | confirmed (code) |
| `0x002285c0` | `Human_IsStealingStereo` | A player whose mini-game record mode is 3, the car stereo. | confirmed (code) |
| `0x002285f8` | `Human_IsPickingLock` | A player whose mini-game record mode is 2, lock picking. | confirmed (code) |
| `0x0022ceb8` | `Mug_End` | Turns a mugging back into a rear grab, withdraws the hint and calls the mug callback. | confirmed (code) |
| `0x0022df88` | `ObjectMash_Start` | Starts mini-game mode 4 (a mash on an object): meter 2 x 0x005102c0, immovable, blocking cleared. | confirmed (code) |
| `0x0022e330` | `ObjectMash_End` | Ends mode 4: clears the meter, hides it and marks the object 0x8000 (0x0024c560). | confirmed (code) |
| `0x0022e4c0` | `ButtonMiniGame_Start` | Starts mini-game mode 5 (`HuButtonMiniGame`). | confirmed (code) |
| `0x0022e558` | `ButtonMiniGame_End` | Ends mode 5 and releases the object target. | confirmed (code) |
| `0x0022e610` | `Tag_StartSpray` | Starts spraying a tag: state 0x2000000, action, voice, particle and the script's start callback. | confirmed (code) |
| `0x00236508` | `Cfg_SetStereoTheftHandler` | `CfgSetSteroTheftHandler`: interns the stereo theft's Lua handler (0x0051027c). | confirmed (code) |
| `0x00236548` | `StereoTheft_CallHandler` | Calls the stereo theft handler with two values; StereoTheft_End uses it. | confirmed (code) |
| `0x002365f0` | `Cfg_SetLockPickHandlers` | `CfgSetLockPickHandler`: interns the lock picking's start handler (0x00510288) and its two end handlers (0x00510284, 0x00510280). | confirmed (code) |
| `0x00236698` | `Cfg_SetLockPickStageFailHandler` | `CfgSetLockPickStageFailHandler`: interns the handler 0x0051028c. | confirmed (code) |
| `0x002366d8` | `LockPick_CallStageFail` | Calls the stage-fail handler; LockPick_JudgePress uses it. | confirmed (code) |
| `0x00236780` | `LockPick_CallStart` | Calls the start handler; LockPick_Start uses it. | confirmed (code) |
| `0x00236828` | `LockPick_CallEndA` | Calls the first end handler (0x00510284); LockPick_End uses it. | confirmed (code) |
| `0x002368d0` | `LockPick_CallEndB` | Calls the second end handler (0x00510280); LockPick_End uses it. | confirmed (code) |
| `0x00272878` | `Mug_OnStartEnd` | End hook of the mug start: starts the mug proper, or with no victim left resets a player's mug camera. | confirmed (code) |
| `0x002729a8` | `Mug_Start` | Starts a mugging: the paired clips, the mug camera (CamMug_Start) and the mug meter (HUD_MugMeterStart). | confirmed (code) |
| `0x00273668` | `Mug_PlayVictimLoop343` | Player_UpdateMugging: the victim loops 343 after a 1/6 s fade. | confirmed (code) |
| `0x00273820` | `Mug_PlayVictimLoop341` | Player_UpdateMugging: the victim loops 341 after a 1/6 s fade. | confirmed (code) |
| `0x002739d8` | `TagGame_Difficulty` | The stick game's difficulty record from the Warrior's class byte +9. | confirmed (code) |
| `0x00273a68` | `Tag_SayNearbyLine` | Has a random nearby gang member say a line while a player tags. | confirmed (code) |
| `0x00273fa0` | `Vec2_Scale` | A 2D vector times a scalar. | confirmed (code) |
| `0x00273fc0` | `Vec2_Add` | The sum of two 2D vectors. | confirmed (code) |
| `0x00273ff0` | `Tag_CatmullRomPoint` | A point on the uniform Catmull-Rom curve through the pattern points ([Tagging](#tagging)). | confirmed (code) |
| `0x00274188` | `Tag_SetPatternPoints` | Copies the pattern's count and 64 (x, y) points to 0x00510914 / 0x006cd978. | confirmed (code) |
| `0x002741d8` | `TagGame_Init` | Builds the stick game: the path from the pattern, the painted list, the start index from the tag's painted fraction, window 4, paint spacing 4 ([Tagging](#tagging)). | confirmed (code) |
| `0x002745e8` | `TagGame_IsPaused` | Whether the stick game is paused (+0x514, the clock until which it waits after a slip or a new charge, is set). | confirmed (code) |
| `0x002745f8` | `Tag_IsComplete` | Whether the tag's progress has reached 1.0. | confirmed (code) |
| `0x00274638` | `Tag_GetChargeLeft` | The charge left: 1 - elapsed / the difficulty's time, from +0x518. | confirmed (code) |
| `0x00274710` | `TagGame_SpendPaint` | Every update: the share of the charge left; at its end a new charge (snap, pause, ramp restart) and one can, or the end with none left. | confirmed (code) |
| `0x002748a0` | `TagGame_IsOnTrack` | The stick game's on-track flag (+0x500); the HUD cursor is plain white while it is set. | confirmed (code) |
| `0x002748a8` | `TagGame_Update` | One update of the stick game: charge, pause, cursor, painting, slip, tracking ([Tagging](#tagging)). | confirmed (code) |
| `0x00274e38` | `Tag_GetProgress` | The painted fraction +0x508 / +0x268 (1.0 once +0x508 + +0x264 exceeds +0x268). | confirmed (code) |
| `0x00277ed8` | `Tag_OnIntroEnd` | End hook of 334 ANIM_TAGGING_INTRO: starts the spray when the tag still exists, else ends the spray clips. | confirmed (code) |
| `0x00277f28` | `Tag_StartSprayEffect` | Loop hook of 335: while tagging, a `sub_spr` mist burst at attach point 0x13 in the tagger's colour and the spray voice ([What the player sees](#tag-look)). | confirmed (code) |
| `0x00278210` | `Tag_RestartSprayClips` | Requeues the spray clips (the spray effect hook) when the tag still exists, dropping a kind 4 or 6 weapon. | confirmed (code) |
| `0x002783a8` | `Tag_OnEndClipEnd` | End hook of 336 ANIM_TAGGING_END: an empty function. | confirmed (code) |
| `0x002783b0` | `Tag_EndSprayClips` | Unless the tag is gone: a 0.05 s fade into 336 (held `0x800000`), then the idle loop (anim state 0) or, in the fight stance, the fight idle + 1 transition (held `0x80000`) and the fight idle (state `0xb`); then `Tag_End` at once ([The end](#tag-look)). | confirmed (code) |

### Game-state functions {#warriors-functions}

Functions of the game-state module (`0x00417af0`-`0x00424e50`: inventory, statistics, flags, configuration
workers) that belong to this page, by address.

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x00417af0` | `Cfg_SetActionDistance` | `CfgActionDistance(d, kind)`: stores d² at `0x00514878 + kind × 4` ([Context records](#context-records)) | confirmed (code) |
| `0x00417b88` / `0x00417be8` | `ContextActions_Construct` / `ContextActions_Reset` | the registry's constructor zeroes its 256 records of 0x1c bytes (from `+0x18`) and resets: the six kind lists at `+0x00`-`+0x14` emptied, every record chained into the free list (head `+0x1c18`) | confirmed (code) |
| `0x00417c48` / `0x00417c58` | `ContextActions_TakeFree` / `ContextActions_Release` | pops the free list's head (from `ContextActions_Register`) / unlinks a record from its kind list, clears its object and user and pushes it back on the free list | confirmed (code) |
| `0x0041ad90` | `GameState_SetCrimeLevel(level, s)` | the crime level `+0x291` and its expiry `+0x2a8` (now + s × 1000 ms, 0 for none); from `Crime_UpdateLevel`, `InitLevel` and `SetCrimeLevel` | confirmed (code) |
| `0x0041add0` | `Script_SetAllClearCallback` | the worker of `SetAllClearCallBack` | confirmed (code) |
| `0x0041adf8` / `0x0041ae30` | `GameState_SetAllClearCallback` / `GameState_SetCrimeCallback` | store a Lua function name (31 characters) at `+0x2bc` / `+0x2dc`, or clear it; from the game state's set-up and the `SetAllClearCallBack` / `CfgSetCrimeCallback` workers | confirmed (code) |
| `0x0041b6a8` / `0x0041b6c8` / `0x0041b6b8` | `Script_EnableCrimeReports` / `Script_SetCrimeSceneTime` / `Script_SetCopGuardArrestedRange` | the workers of `ReportCrime` (`+0x288`), `SetCrimeSceneTimeLength` (`+0x2b0`) and `SetCopGuardArrestedRange` (`+0x320`) | confirmed (code) |
| `0x0041b888` | `GameState_SetCrimeLevelTimes` | the bytes `+0x2a3`-`+0x2a6` (10, 15, 20, 25 at set-up): the seconds each crime level lasts | confirmed (code) |
| `0x0041b8a0` | `GameState_SetCrimeResponders(type, n)` | byte `+0x294 + type`; `CfgCrimeResponders`'s worker | confirmed (code) |
| `0x0041c1f8` | `GameState_SetCrimeTimer(s)` | `+0x2ac` = now + s × 1000 ms, or 0 | confirmed (code) |
| `0x0041c230` / `0x0041c2a0` | `GameState_AddGuardingCop` / `GameState_RemoveGuardingCop` | count `+0x322` up (the cop says command `0x16` when his brain's `+0x28f` is set) / down; from a police goal's start and end (`0x002c0430`, `0x002c0548`) | confirmed (code); role inferred |
| `0x0041cf80` | `GameState_CanReportCrime(human)` | true when no one is reporting a crime now (`+0x280` empty), reports are on (`+0x288` = 1) and the human's role (character type `+0x11b`) is not 6 or 7; from the civilian and cop brains | confirmed (code) |
| `0x0041cfe8` / `0x0041cff0` | `GameState_SetCrimeReporter` / `GameState_CancelCrimeReport` | the reporter's handle `+0x280`; dropping the report pops the reporter's goal when it is type `0x6d` (from `Gang_MakeNeutralWithType`) | confirmed (code) |
| `0x0041d0a8` | `GameState_SetDispatcherVoice` | `+0x328`, −1 giving the default 125 | confirmed (code) |
| `0x0041d0d0` / `0x0041d0e8` | `GameState_SetCrimeTypeEnabled` / `GameState_IsCrimeTypeEnabled` | the bytes `+0x32b + type` for types 0-14; read by the cop and civilian brains | confirmed (code) |
| `0x0041d108` | `GameState_DamageHook` | empty; called by `LockPick_End`, `Strike_Contact`, `Glass_Break` and `WorldObject_ReportDamage` | confirmed (code) |
| `0x0041d8a0` | `Cfg_SetCrimeResponders` | `CfgCrimeResponders(type, n)` | confirmed (code) |
| `0x0041d8d0` | `GameState_SetForceCrimeLevel` | `ForceCrimeLevel`: `+0x28c` | confirmed (code) |
| `0x0041d8e0` | `GameState_EnableDispatcher` | `CfgEnableDispatcher(on, voice)`: byte `+0x32a` and the dispatcher voice | confirmed (code) |
| `0x0041d910` | `Cfg_SetBreakAndEnterDelay` | `CfgBreakAndEnterDelay`: `0x005148a4` | confirmed (code) |
| `0x0041da80` / `0x0041dab0` | `Cfg_EnableCrimeType` / `Script_SetCrimeCallback` | `CfgEnableCrimeType(type, on)` for types 0-14 / `CfgSetCrimeCallback` | confirmed (code) |

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

**Crime reports** (`repo:src/warriors/crime_reports.h`, `CrimeReports` in `GameState::player`), written from [AI:
crimes](ai-gangs.md#crimes) and [Wanted](#wanted) (2026-10-06): `report()` follows steps 1-6 there, reaching gangs, spawners,
stores, statistics, the script callback and the HUD through a `CrimeServices` interface the play mode implements as
those systems arrive; `update()` clears a gang's wanted state 10 s after its last report (held while forced).
`setSecondWanted()` starts a gang's second timer (10 s, not held by `ForceCrimeLevel`; the all-clear callback at its end
is not built), which `GangIsWanted(gang, false)` reads; a second argument passed as `nil` reads as false, as level87's
`CheckIfWanted` relies on. In play the objects' break-ins report through it
(`repo:src/gamemodes/level_crime_services.h`: the crime callback, the `CrimeScene` flag, which humans are players, and
the offender's gang and player 1's through the level's brains, so a report makes the gang wanted, and player 1's HUD
messages, 7 for a report and `0xb` when the wanted time runs out, to [the HUD's crime message](hud-functions-parts.md#fn-radar-frame);
the police's hostility and the spawners are not wired to it yet). A break-in marks the nearest store flag within 10 m robbed
(the gang in bits 18-22) and switches on the alarm strobe (a `part_strobe_red` of the level's objects file) within 6 m
of it (`LevelObjectServices::robStore`): its [strober](script-types.md#strober), a red light pulsing on a 40-tick cycle,
and its `alarmbell_loop` emitter, heard within 30 m ([Particles](particles.md#coneys-implementation)); `ResetStore`
switches the one within 7 m off. **Coney's stand-in**: the store's buyers are not done yet. `ReportCrime` switches
reporting. `EnterStore` / `ExitStore` switch the [screen tint](graphics.md#looks), and `CfgSetSteroTheftHandler` keeps
its callback's name.

**Tagging** (`repo:src/warriors/tag_game.h`, `repo:src/warriors/tag_session.h`, `repo:src/world_objects/tag_spots.h`,
2026-10-06): `tagPath()` samples `HuTagPattern`'s points along the Catmull-Rom curve into grid cells and
`TagGame` is the [stick game](#tagging) on it, tuned by `tagTuning()` from the Warrior class's byte `+0x09`
(`script::tagDifficulty()`), in the steps' order (2026-10-11): the paint first in every update, paused or not, so
the charge drains through pauses; the cursor past a squared stick of 0.04, its ramp restarted from the end of each
new charge's pause (not a slip's); tracking on the truncated cell, near a point behind (the progress kept) or at the
first near one from the progress on; the off-track count kept until a slip or a new charge, a count above 4 slipping
at the next moving update before tracking; the finish at p ≥ count − 3 and the fraction p / count. `TagSpots`
keeps each spot's record and answers its messages ([Tag spots](#tag-spots)): `CfgTagSettings` and `ProcessTag` reach
it for a particle system's handle only, the fade runs every second 60 Hz tick, and `spray()` is a CPU tagger's step
(Rumble's Tag battle). `TagSession` is one player's spray: the paint from inventory item 3 (taking the last can ends
it, so n cans give n charges), the fraction sent to the spot (message `0x41`) as the game goes, and `Tag_End`'s spot
side and wasted charge. In gameplay (`repo:src/gamemodes/gameplay_tag.cpp`) `HuTag` for player 1 with paint locks
his movement where he stands, drops a held object of anim set 4 or 6 (a club stays in the hand, `tagDropsHeld()`)
and plays 334, turning him to the tag over half its length, then the loop 335, and 337 then 335 again when a pause
begins (`Human::startTagSpray()`). 334 holds the busy bit `0x400`, and while any of the three plays the player's
stance, commands, triangle actions and anim-state choice do not run and the stick step is skipped, as the original's
`Human_UpdateActions` (`0x00254e78`) returns after the spray's update (issue #353; a player in the fight stance used
to have the fight idle put back over 334). When the loop starts, the session runs on pad 1's left stick, the spot gets its
tagger (message `0x00`, which starts its own fade in) and `CfgTagStartCallback`'s function is called with (tagger,
tag, flag) and no result asked ([above](#tag-callbacks)); an AI tagger's starts at once. A body something else takes
during the intro ends it unsprayed (**Coney's reading**), and the tagger still gets event 14 with the tag and 0, as
`Tag_End` (`0x0022e848`) always gives it, so the script puts the spot's prompt back ([level87's fix
1](scripting.md#level87-fixes)). Without paint he says 37 `nopaint` and gets event 14 with
the tag and 0.  At the end the pad is freed, a finish has a crew mate say 83 `tagdone` and the tagger gets event 14
(the tag and whether it was finished); a snap-back off the pattern has one say 80 `tagcheer` while the tagger's tag
cheer is on (`HuEnableTagCheer`). The crew mate is picked as [Tag_SayNearbyLine](sound-events.md#tag-lines) does:
only while player 1 (Coney's war chief) last ordered defend, from his gang's members in slot order that are not the
leader or a player, are free to act, have no actions queued and stand within his far melee range, each later one
replacing the pick on a roll of 50 or more; the pick speaks only when his `HuEnableTagDone` switch is on. The story
reaches `HuTag` through the spot's flag: the script gives the flag (activity 12) a message-0 handler and a prompt
with `SetMsgHandlerEx`, a kind-1 [context record](#context-records). Coney locates such a record's object whether
it is a spawn record, a flag or a particle system (`LevelPickups::actionObject()`), shows its text as player 1's
[action prompt](hud-messages.md#action-prompts) and hands it triangle, whose handler calls `HuTag`. **Coney's stand-ins**:
any picked crew mate may gesture (the gesture slots are not kept), and the busy test is the actions-blocked one;
the spray clips stop at the end (back to the idle); an AI human given `HuTag` becomes the spot's tagger at once
(its fade in) without walking to the flag (`GoalTag` walks him there first, [AI](ai.md#coney)). A spot telling
an AI tagger he is done (message `0x13`, at the end of its fade or when hidden) ends his spray as `Tag_End` does:
he gets event 14 with the tag and whether it is fully painted (**Coney's reading**: the human's handling of `0x13`
is not traced). Each human's tag colour (`repo:src/human/tag_colour.h`, 2026-10-11) is the four bytes of `+0x640`:
`Human_Init`'s `50 07 05 20` (a civilian woman's `ff ef ef ef`; **Coney's reading**: the brain kind before the `+0x19d`
remap), which `HuTagColor` overwrites with t4, t3, t2, t1; the HUD draws bytes 0-2 and the mist will read the word with
alpha in byte 0 ([above](#tag-look)). An unfinished end (no cans, a 301st cell) leaves the spot at p / count, not
`Tag_GetProgress`'s 1.0 (**Coney's reading**: whether `0x41` carries that 1.0 before the end is not on the page). A
finished or abandoned spray of player 1 ends through 336, holding `0x800000`, then the idle, or 359 and the fight idle
when he is locked onto a target (**Coney's reading** of the state word's `0x3`); a spray cut short plays no end clip.
Not yet: the slip's rumble, the bonus event on a clean finish, the spray particles and the tag's drawing, hint `0x10`,
and buttons other than the stick ending a session.

**Car stereos** (`repo:src/world_objects/cars.h`, 2026-10-06): `CarSpawnRadio` puts a stereo in a parked car;
breaking window 15 ([Cars](cars.md#windows)), or a type-12 pane within 2 m (`ObjectServices::freeCarStereos`), frees
it, and `Cars::takeStereo` takes it once. **The theft** (`PlayLevelMode`'s context action,
`repo:src/platform/play_level_objects.cpp`): with no kind-1 prompt in reach, triangle within 2 m in plan of a freed
stereo not below the feet starts it (`Human::startStereoTheft`: the player turns to the stereo, 683 then the loop
684, mode 3 with 3 turns a stage, the player's class byte 2); success calls `LevelPickups::stereoStolen` ($15 and
item 11, then the `CfgSetSteroTheftHandler` callback with the human and the car). **Coney's readings**: the turn to
the stereo is at once, not spread over the intro; both gifts notify the inventory callback; the success and failure
clips (685, 686), the hint and the owned byte are not played or set yet. The HUD panel and its cues follow the theft
([HUD: Coney's implementation](hud.md#coneys-implementation)), as does the action prompt for kinds 2-4. A hit with a
reaction ends the theft at once (as `0x00268c50` does) and plays its reaction; anything else that takes the body (a
death, a knock-down, a scene) ends it at the next update (`Human::stereoTheftPlaying()`); either way there is no
outcome, the car is forgotten and the panel goes. **Coney's reading**: the clip `0x00268c50` plays for a theft is not
traced, so the hit's own reaction plays.

**The mugging and the starting money** (2026-10-07): `HuCreate` rolls each new human's carry as
[Starting money](#starting-money) gives (`characters::rollStartingCarry()`, `repo:src/characters/starting_money.h`)
from its recorded `CfgChar` and `CfgCharClassAttribs` calls with the game's random numbers, and sets the money
(`HuSetMoney` later overrides it). A gang spawner makes its human through the same `HuCreate`
(`ScriptSpawnerWorld::spawn`), as `Spawner_MakeHuman` (`0x0016d860`) calls `Human_Init`, so a spawner's human rolls
the same draws in the same order (`repo:tests/ai/spawner_money_test.cpp`). It takes the same branches for a capped
record (`v16` not -1), so `level87`'s sellers get $0 and a type 197 soldier $5 in 21 of 101 rolls and $0 otherwise,
as in the original ([level87's sellers and soldiers](#starting-money); inferred from reading
`repo:src/characters/starting_money.cpp`, not rerun for these types). Player 1's won mugging
(`PlayLevelMode::stepMugging()`) moves all the victim's money in the deciding update (`LevelPickups::mugPaid()`:
item 2 with the money and inventory callbacks, then item 2's pick-up sound); his
`HuSetMugCallback` callback runs with (mugger, 1 or nil) when his end clip (344 or 346) finishes, or at once for a
let-go or a hit (`LevelPickups::mugEnded()`). **Coney's stand-ins**: a `grp_` object group carries nothing and a
rolled object is not given to the human; `HuSetInterrogation` keeps the lines and callback
(`ScriptState::interrogationLines`, `interrogationCallback`) and the prompt reads "interrogate", but not yet its
mugging (the set lines, no pay, the callback once); not yet: the pocket items, the half-way `no_item` stop, ped
type 5's 1.5 times and the statistic. While he mugs, the HUD's mug meter shows
(`PlayLevelMode::stepMugMeter()`, [HUD: the mug meter](hud-functions-panels.md#mug-meter-layout)).

**The arrest and the uncuffing** (2026-10-07): `HuSetArrested` (`ai::ScriptedHumans::setArrested()`) cuffs the human
(`Human::setArrested()`: stopped, its fighting ended, 320 looping), resets its brain as `Brain_OnArrested` does
(actions, target, goals down to a FindEnemy goal), sends it event 17 with 1 and has a friendly AI human say 25
`arrested`; the release sends event 17 with 0 after the cuffs come off. In gameplay
(`repo:src/gamemodes/gameplay_uncuff.cpp`) the kind-0 record is a cuffed AI human friendly to player 1 within
`CfgActionDistance` 0 in plan and 1.5 m of his waist, not being freed: its prompt is `GSTRING.HUD` 2, chosen before the
action object and hidden while a mash runs, and triangle tries it before the level's own records. The start has the
freer say 68, turn to the cuffed human over 325 (holding `0x2000000`, so the mash's input waits) then loop 329, and
runs the mash ([above](#uncuffing), `combat::ButtonMash`) with his class's byte `+0x08` (`script::warriorMashByte()`,
`combat::mashFactor()`); the cuffed human plays 326 paired, then 330. Success: he says 67 and is released (and revived
when knocked out and revivable), the freer plays 332 and he 333; failure (a quit command, the decay below 0, the cuffed
human gone or released some other way): 332 and back to 320. A body something else takes before the outcome (a hit)
plays 331. Player 1's [mash meter](hud-functions-parts.md#mash-meter-layout) (`hud::MashMeter`) shows from the press to
the outcome, filled by the meter over its target, and hides the prompt, the hint box and the scroll-in messages
meanwhile. A disc test (`[disc][uncuff]`, `repo:tests/platform/disc_level80_test.cpp`) frees Snow and Cowboy at
`level80` checkpoint 2 and reaches checkpoint 3; a cuffed friendly AI human wears the `dyn_cuffs` icon until his
release, which removes whatever icon he wears. **Coney's stand-ins and readings**: friendly is the gangs' friendship
(`Human_IsFriendly`, `0x00222a90`, is not on the page); event 17's other human is none; the push weight, the partner and
the shared body group are not kept; after a hit the cuffed human goes back to 320; the cuffs icon stays attached over
his feet rather than detached where it appeared (he does not move while cuffed). Not yet: hint 19, the key path, the 0.2
m capsule test, players in cuffs, the leaderless gang's follow after a release and `0x00280fd8`.

Coney's choices: a break-in and a custom crime queue kind-1 responders (the break-in after `CfgBreakAndEnterDelay`);
the assault statistic is scored once per victim through the service.

**Owned items** (`repo:src/world_objects/spawn_records.h`, `repo:src/gamemodes/level_pickups.h`, 2026-10-11): a spawn
record made with `ObjSpawn` flag 2 (record bit `0x80000`, `WorldObject_SetOwnedFromFlags` `0x00392510`) is owned; in
`level80` that is the pharmacy's stock, the 23 objects of zone 24 with flags 2 in its placed-objects list (disc check,
NTSC-U, counts only). Whoever takes one, the player with triangle or a Warrior fetching it, clears it and the theft goes
to every enabled kind-0 volume box round the object's spot as message 7, `(taker, object)`
(`VolumeBoxes::sendTheft`, `repo:src/world_objects/volume_boxes.h`); an AI's take of a `TYPE_SPECIAL` removes it with
nothing given. **Coney's readings**: the page names the inside test (`0x00412a18`) but not which boxes are asked nor
whether the point is the object's or the taker's, so they are the boxes message 6 asks and the object's spot. Not yet:
the witnesses' message 7 and alert, the crime score 4-8 and the lines 128 / 143.

## Open questions

- **GetItem's swing at glass** ([AI: GetItem](ai.md#get-item)): at level80's pharmacy (scenario `steal_store`) two Warriors
  reached the glass branch 23 times each in 60 s and no pane broke. Whether the queued kind 0 played at all (the
  anim-dispatch log of that run was not read), whether its strike reaches the pane's body from the standing point, and
  whether the pharmacy's panes are alarmed or otherwise special are not known. The run was staged (checkpoint 1
  state, the crew teleported inside, the order given through the dispatcher), so the mission's own path (the front
  lock picked, the order given by the script) should be run before an implementer copies "they never break it".

- Which objects register context records of kinds 1, 4 and 5, and with what second value.
- Who sets mini-game mode 4.
- The prompt widgets' base position and text style.
- The lock-picking dial's rate at runtime (one step per drawn frame is inferred).
- Whether the police a lock-picked break-in calls attack the picker's gang (the pick reports no offender; [Which
  break-ins call the police](#break-in-police)). Both a break-in and a custom crime queue kind 1.
- Wiring the report's police hostility, responders and HUD messages to Coney's gangs, spawners and HUD.
- Whether `Tag_UpdatePlayer` sends the tag `0x41` with `Tag_GetProgress`'s 1.0 when the game ends unfinished
  (`+0x4fc`: out of paint, or a 301st cell), which would show the wall tag whole though it is not marked sprayed.
