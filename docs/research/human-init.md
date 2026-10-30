---
kind: feature
owns:
  functions: ["0x00219638", "0x00222c98"]
---
# A new human's set-up after its model

Verified against: `SLUS_212.15` (NTSC-U), read in the shared Ghidra project.

## Summary

`Human_Init` (`0x00218008`) makes every human, the player included: it remaps the type to its class, picks the model,
snaps the human to the ground and rolls its money ([Characters: Creation](characters.md#creation),
[Starting money](crimes.md#starting-money)). This page is the rest of what it gives a new human, in its order: the
voice and the idle lines, the brain's ped type, the task byte and the radar blip, the power class's switches in the
human flags, the class's weapon in the hand, a byte from the character data's name, and the size. "The class" is the
record after the [remap](characters.md#type-to-model), "the type" the one `HuCreate` was given.

## Rules

- A human of category 4 or 6 is a ped type 1, or 2 when he is rich (21 dollars or more), a woman, or (category 4
  only) on a roll below 10 of 0-100; one of category 5 or 7 is a ped type 3. Confirmed (code) at `0x00218008`.
- The switches of the human's power class set or clear eight bits of his flags; a player's are those of his own power
  class until `Human_MakePlayer` (`0x00229c40`) applies his Warrior's. Confirmed (code) at `0x00222c98` and
  `0x00222b78`.
- A class whose weapon is not `none` (the police's `dyn_baton`) holds it from creation. Confirmed (code) at
  `0x00218008`.
- Thirteen classes have a size of their own, five of them another one in co-op; the others keep the size they had.
  Confirmed (code) at `0x00219638`.

## Map

| Behaviour | Function | Section |
| --- | --- | --- |
| The voice and the idle lines | `0x00218008` `Human_Init`, `0x00229110` `Anim_LevelVariant` | [The voice](#voice) |
| The brain's ped type | `0x00218008` `Human_Init` | [The ped type](#ped-type) |
| The task byte and the radar blip | `0x00218008` `Human_Init`, `0x001b2ee8` `HUD_RadarAddPlainHuman` | [The radar blip](#radar) |
| The power class's switches | `0x00222c98` `Human_ApplyClassFlags` | [The class's switches](#class-switches) |
| The weapon in the hand | `0x00218008` `Human_Init`, `0x0024c280` `Human_PlaceItemInHand` | [The weapon](#weapon) |
| The character data's name byte | `0x00218008` `Human_Init`, `0x0025aeb8` `Human_TryIdleFidget` | [The name byte](#name-byte) |
| The class's own size | `0x00219638` `Human_ApplyTypeScale` | [The size](#size) |

## Behaviour

### The voice and the idle lines {#voice}

The human's voice `+0x3b0` is the class's `CfgChar` value `+0x118`; type 259 instead draws one of 120-125 from the
random stream `0x006eb8b0`. `+0x3b4` is `Anim_LevelVariant(class)` (`0x00229110`), or the voice when that gives 0.
Then for n = 1 to 3 the name `l<level>_c<checkpoint>_idle_<nn>` in the voice's folder (the format at `0x0055a548`) is
looked up in the sound bank (`Sound_FindByName`): each one found adds 1 to the count `+0x197` and sets `+0x196`. The
class's women's byte `+0x14b` goes to `+0x3b8`. Confirmed (code) at `0x00218008`.

### The ped type {#ped-type}

After the money, the brain's ped type `+0x26c` follows the class's category `+0x11b`. Categories 4 and 6 give 1;
category 4 then draws `Random_Int(100)` (stream `0x006eb880`, after the money's draws; confirmed (code)) and gives 2 on
a roll below 10; either becomes 2 when the human's money (`+0x370`) is 21 or more or `+0x3b8` is 1 (a woman). Categories
5 and 7 give 3. Any other category leaves the brain's own 0. Confirmed (code) at `0x00218008`. What the values make a
civilian do is on [AI: brain types](ai-brain-types.md#think-civilian).

### The task byte and the radar blip {#radar}

The human's task byte `+0x6c` is set to 1 and `+0x19e` to 5. Then, by brain type ([AI: Types](ai/index.md#types)): a
cop (1) gets brain `+0x28d` = 1, the task byte 2 and two blips of type 8 (`HUD_RadarAddGangHuman`, `0x001b3258`,
layers 0 and 1); the player or a Warrior (0 or 3) one blip from `HUD_RadarAddPlainHuman` (`0x001b2ee8`), type 9 in grey
`0x787878ff` with icon `0x16a` for a brain of kind 0, else type 7 in white with icon `0x16d`; a civilian or a bum (4)
gets brain `+0x28d` = 1, the task byte 2 and a blip of type 5, which `HUD_RadarAddBlip` (`0x001b2990`) does not add, so
civilians have none; any other the task byte 2. Confirmed (code) at `0x00218008` and in the three HUD functions
([GUI: The radar](gui.md#radar-icons)).

### The class's switches {#class-switches}

After the flags by brain type, `Human_ApplyClassFlags` (`0x00222c98`, confirmed (code)) copies bytes `+0x3a`-`+0x3f`
of the human's power class ([Characters: power class fields](characters.md#power-class-bytes)) into the flag word
`+0xe0`:

| Byte | `CfgPowerClass` argument | At 1 | At -1 | Otherwise |
| --- | --- | --- | --- | --- |
| `+0x3a` | 23 | sets `0x80` | clears `0x80` | clears `0x80` |
| `+0x3b` | 24 | sets `0x1000000000` | clears it | clears it |
| `+0x3c` | 25 | sets `0x2000000000` | clears it | clears it |
| `+0x3d` | 26 | sets `0x800000000` | clears it | clears it |
| `+0x3e` | 28 | sets `0x200000`, clears `0x400000` | sets `0x400000`, clears `0x200000` | clears both |
| `+0x3f` | 27 | sets `0x400`, clears `0x200` | sets `0x200`, clears `0x400` | clears both |

The bytes are the script's numbers stored unclamped (`Cfg_SetPowerClass`, `0x002288c0`; arguments 27 and 28 are
crossed). The record is the one `Human_GetClassRecord` (`0x00222b78`) picks: `+0x1b8`, the type's own power class,
since the player slot `+0x1b0` is still -1 at this point; `Human_MakePlayer` (`0x00229c40`) applies the switches again
with `+0x1b9`. Confirmed (code) at each address.

### The weapon {#weapon}

When the class's weapon (`CfgChar` string `+0x18c`, its 17th argument) is not `none`, `Human_Init` calls
`Human_PlaceItemInHand(h, name, 1, 1)` (`0x0024c280`): the object is made by name and goes into the hand. The police
types carry `dyn_baton`. Confirmed (code) at `0x00218008`; what the object is afterwards is on
[Characters: Character classes](characters.md#classes).

### The name byte {#name-byte}

`+0x3bc` is set when the character data's name hash (its `+0x04`) equals `Crc32_Hash("civl_bm_header")`, the name at
`0x0055a580`. Its one reader, `Human_TryIdleFidget` (`0x0025aeb8`), lets a human with it and nothing held play the
fidget 601 as an overlay on a 30 % roll. Confirmed (code) at both addresses.

### The size {#size}

Last, the random body size ([Characters: Body scale](characters.md#body-scale)), then `Human_ApplyTypeScale`
(`0x00219638`) for the class (`+0xcc`): 5 → 1.02; 18, 30 → 0.97; 21, 23, 24 → 0.98; 33 → 1.03; 89 → 1.16; 119, 120, 135
→ 1.2; 128 → 1.175; 161 → 1.11. With game state `+0x158` set (co-op) 89, 119, 120, 128 and 161 take 1.1 and 135 takes
1.03. Any other class keeps the size before it: the random one, or 1.0 (`+0x65c`, set at the start of `Human_Init`) for
the humans the random size skips, every Warrior among them. Confirmed (code) at `0x00219638` and `0x00218008`. Rembrandt
(class 30) is the 0.97 read at runtime ([Characters: Walls](characters.md#walls)), and Vermin (class 26) the 1.0,
confirmed (runtime), run r-20261029-class-size-vermin.

## In Coney

`HuCreate`'s binding takes the steps after the money (`humanInitSteps` in `repo:src/scripting/script_bindings.cpp`),
so every human the scripts or the spawners make gets them:

- the ped type, rolled with the money in the same draws and from the same record, the class's
  (`characters::rollStartingCarry`);
- the switches of the type's own power class (`human::classFlagChanges`), for a human who is not a player;
- the class's weapon in the hand, the next of the scripts' handles;
- the class's size (`characters::typeScaleOf`), co-op being off, given through `HuSetScale`'s path: the original
  stores it and a script's `HuSetScale` alike (`Human_StoreScale`, `0x00219608`, confirmed (code)). Given before the
  human's first update, it is also the size the human is made at, which Coney's Entity editor scales from and resets
  to; a script's later `HuSetScale` is an edit of it.

Making the humans of an unlisted class at 1.0 rather than Coney's former 0.97 (Vermin among them: class 26 is not listed
and a Warrior skips the random size, confirmed (code) at `0x00218008`; his 1.0 and Rembrandt's 0.97 read in level99's
checkpoint 2, confirmed (runtime), run r-20261029-class-size-vermin) moves level99's fence lesson by one update against
the fence's 60-tick phase, so the disc test of the fence charge
(`repo:tests/platform/disc_level99_fence_dealer_test.cpp`) delays its charges 17 and 20 updates, not 18 and 21, to meet
the same phases ([Scripting: item 44](scripting.md#level99-p2-vermin-fence)). At an 18-update delay the fence is removed
one update after the hit, before the charge reaches its body, and Coney's charge is not stopped; whether the original's
is in that case is open ([q-early-removal](#q-early-removal)).

Stand-ins: the voice is read from the type when a line plays, so type 259's draw and `+0x3b4` are not kept, and the idle
lines are not counted ([q-voice](#q-voice)); no radar blip, task byte or `+0x19e` ([q-radar](#q-radar); types 6 and 8
also wait for the scanner's marks, [#1018](issue:1018)); no `+0x3bc` ([q-name-byte](#q-name-byte)). A player's switches
wait for `Human_MakePlayer`'s port ([#1011](issue:1011)), and a class the type scale does not list is made at 1.0 where
the original rolls the random size ([#689](issue:689)).

[Characters: Coney's implementation](characters.md#stats-coney) still lists the weapon as not traced and every human at
the player's 0.97; this page is what Coney does until that legacy page's restructure ([#1164](issue:1164)) brings its
table up to date.

## Open questions

### The voice and the idle lines {#q-voice}

Whether the stream `0x006eb8b0` is apart from `0x006eb880` (Coney keeps one), so that type 259 can draw its voice
without moving the other draws; and what reads `+0x3b4` and the idle line count once the idle dialogue
(`HuSetIdleDialogMaxIter`) is built. Reading the stream objects at both addresses in Ghidra answers the first.

### The radar blips {#q-radar}

How the original draws a type 9 blip, a sprite widget where every other type is a particle dot (Coney's radar draws
every blip as a dot); whether player 1's own blip shows (the player is made through `Human_Init` too); and what reads
the task byte and `+0x19e` = 5 (the brain's blip guard reads 4 and 5, [AI: brain types](ai-brain-types.md)). Reading
the radar's draw for blip type 9 in Ghidra answers the first; a PCSX2 capture of the radar at level99's start the
second; the cross-references to `+0x19e` and to `Task_SetByte6c`'s byte the third.

### The name byte {#q-name-byte}

How often the fidget is tried. Its one reference is data at `0x0025b8c0` in `Human_BuildMoveTasks` (`0x0025b200`):
the stand-still branch passes it as the callback of the idle gait blend when `+0x3bc` is set and the brain's `+0x04` is
not 2 (confirmed (code) at `0x0025b8c0`). Reading `GaitBlend_Construct` and the blend's update in Ghidra for the call
of its callback answers it; Coney has no such fidget, so it keeps no `+0x3bc`.

### The position written back {#q-written-back}

`lua_HuCreate` (`0x00358428`) writes the snapped position back into the script's table after `Human_Create`
(`0x00233d60`); Coney neither snaps nor writes back while the script runs, as no collision is loaded then, and snaps
when the play mode makes the human ([Characters: Creation](characters.md#creation)). Whether a script reads the
table again after `HuCreate` is open; searching the decompiled scripts for a reuse of the table answers it.

### A fence removed at once {#q-early-removal}

Whether the original's charge stops at a wooden fence that is removed one update after the hit, before the charge
reaches its body (speculative: the original's two runs of item 44 had the removal 21.5 and 27.5 updates after the
hit, and the charge stopped 2 updates after it). A PCSX2 run of the `fence_dealer` scenario with the charge started to
meet that phase answers it.
