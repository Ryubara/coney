---
kind: topic
system: characters
owns:
  functions: [
    "0x002215d0", "0x00221620", "0x00221670", "0x002216c0", "0x00221710", "0x00222ba8", "0x00222c20", "0x00223068",
    "0x00228258", "0x00228800", "0x00228888", "0x00228af8", "0x0022aeb0", "0x00254078", "0x00383f38", "0x003842c0",
  ]
---
# Characters: classes, stats and speeds

Verified against: `SLUS_212.15` (NTSC-U); runtime claims cite run records.

## Summary

What makes one character stronger, tougher or faster than another comes from four configuration tables set by
the configuration scripts (and again by each difficulty's): the **character type** (`CfgChar`), the **power class**
(`CfgPowerClass`), the **Warrior class** (`CfgWarriorClass`) and the **gym upgrades** (`CfgWarriorUpgrade`). This page
holds the power-class and character-class records, the one list of every stat, the speed classes (`CfgSpeedClass`)
and the way `HuCreate`'s type picks a class and a model. The human object they land in is on
[Characters (humans)](characters/index.md#the-human-object); what the speeds do each update is on
[Locomotion](characters-locomotion.md).

## Map {#original-structure}

The functions of the class tables:

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x00228800` | `CharClass_Get(id)` | `0x00684620 + id × 0x1ac` | confirmed (code) |
| `0x00383f38` → `0x00228af8` | `CfgChar` | fills a character class | confirmed (code) |
| `0x003842c0` → `0x00228888` | `CfgSpeedClass` | fills a speed class | confirmed (code) |
| `0x00221710`, `0x002215d0`, `0x00221620`, `0x00221670`, `0x002216c0` | speed getters | base, walk, jog, run, sprint | confirmed (code) |

## Data

### Power classes {#power-classes}

`CfgPowerClass` fills records of 0x44 bytes at `0x006619a0 + class × 0x44`. A human's class is byte `+0x1b9` for a
player and `+0x1b8` otherwise (`0x00222b78`). Confirmed (code) for the reads at the cited addresses. Rembrandt's
values below are class 64's, and a street civilian's (class 2) a 0.35 hurt threshold, stun 750 ms, ground time 2000 ms,
power 200 and byte `+0x36` 4: each as normal's difficulty script sets it (confirmed (code) at `0x002288c0`, the
binding that fills the record; the values: [Power classes](../references/power-classes.md#normal)). The AI's fields
`+0x08` (block chance), `+0x0c` (block chance while hurt), `+0x24` (counter chance) and `+0x37` (the pattern-reading
threshold) are on [AI](ai-fights.md#block); `+0x32` and the bytes after `+0x37` are [below the
table](#power-class-bytes). The sparring Warriors' class 40 is on [AI](ai/index.md#level99):

| Field | Rembrandt (class 64) | Use |
| --- | --- | --- |
| `+0x00` | 1.3 | as the attacker, scales the victim's stun for weapon hits of 20-49 damage (`0x0022f658`); confirmed (code) |
| `+0x04` | 0.3 | the **hurt** threshold: below this fraction of maximum health the human is hurt (`0x00222ff8`); confirmed (code) |
| `+0x10` / `+0x14` | 1.0 / 1.0 | the stun time's / the ground time's factor while hurt ([Combat](combat-reactions.md#reactions)) |
| `+0x1c` / `+0x20` | 3.0 / 3.0 | AI only: the factor on `CfgAttackDelay`, the second when the target is down (`0x00223800`); confirmed (code) |
| `+0x28` | 400 | the **power meter's maximum** ([Combat](combat-rage.md#power-meter)) |
| `+0x2a` | 60 | the power meter's refill per second |
| `+0x2c` | 135 | **stamina maximum** |
| `+0x2e` | 40 | **stamina refill per second** |
| `+0x30` | 200 | a **stun**'s length, ms (`0x0022f658`); confirmed (code) |
| `+0x32` | 300 | how long a hit keeps an AI busy, ms ([below](#power-class-bytes)); confirmed (code) |
| `+0x34` | 2750 | the time a knocked-down human stays **down**, ms (`0x0022f100`); confirmed (code) |
| `+0x36` | 3 | in a grab struggle, the grabbed human's strike costs the grabber 1 / this of its power (`0x0027fd68`); confirmed (code) |

#### The remaining power class fields {#power-class-bytes}

Each confirmed (code) at the reader named (the class is the one
`Human_GetClassRecord` picks, `0x00222b78`):

| Field | Reader | What it does |
| --- | --- | --- |
| s16 `+0x32` | `Human_ApplyPendingDamage` (`0x00265f70`) | after a hit lands on an **AI** human (brain kind not 0), its record `+0x108` = game time + `+0x32` ms; until then `Human_IsBusy` (`0x00228258`) says busy, which about 40 AI routines test (spotting, reporting a crime, joining a fight, grabbing). A player is never marked. The write comes after the block test and the damage: a hit with no pending damage, or one a block takes, sets nothing, nor does a lethal hit; a human in the falling states `0x1c00000000` takes nothing at all, since the function returns at entry for him (`Human_HasState1c00000000` at `0x00266020`-`0x00266028`), so its later falling-state branch (`0x00266470`) is reached only when the state is set during the update; a friend's hit whose damage was zeroed still sets it; confirmed (code). A street civilian (class 2) given 30 pending damage had `+0x108` = the game time of that update + 750, the player none: confirmed (runtime), run r-20261120-busy-after-hit. The hit's own event 1 does **not** see this hit's timer: `Strike_Contact` (`0x0021b290`) calls `Human_DealDamage` (`0x0021d680`), which stores the pending damage and sends event 1 at once through `Human_OnEvent` (`0x0021d480`: script handler, gang, then `Brain_OnEvent`), so the brain's busy test there (`GangBrain_OnEvent`'s hit back, `0x00304fa8`) runs before the victim's next update applies the damage and writes `+0x108`; it sees only an earlier hit's timer, confirmed (code). Coney sends event 1 after the hit is applied (`ai::Brains::reportDamage()`, a stand-in), so it reads the timer as the brains last saw it, before this hit |
| s8 `+0x38` | `Human_GetClassReactFlag` (`0x002233c8`) in `Human_PlayReaction` (`0x0026a6d0`) | the most ground strikes a downed human reacts to: while a reaction clip plays (`Human_IsInReactionClip` `0x00228488`: held `0x8000`, a clip with flag `0x80`, or 199) and his record `+0xbe` is at least this, a new reaction is skipped. `+0xbe` counts the ground-strike reactions 195 played (`Human_PlayReaction`, `Hit_PlayBegReaction` `0x00269b80`) and is cleared when he gets up (`Human_EndDownState` `0x0025e250`); confirmed (code) |
| bytes `+0x3a`-`+0x3f` | `Human_ApplyClassFlags` (`0x00222c98`), from `Human_Init`, `Human_MakePlayer` and `Human_ReleasePlayer` | switches copied into the human flags `+0xe0` ([Combat: human flags](combat/index.md#human-flags)): `+0x3a` = 1 sets `0x80` (reaction strength at most 1), else clears it; `+0x3b` `0x1000000000` (hard to stun, below); `+0x3c` `0x2000000000` (never escapes when the grabber's power runs out); `+0x3d` `0x800000000` (reader not traced; `HuSetBlockFromReact` sets the same bit); `+0x3e` = 1 sets `0x200000` and clears `0x400000`, −1 sets `0x400000` and clears `0x200000`, 0 clears both; `+0x3f` = 1 sets `0x400` and clears `0x200`, −1 sets `0x200` and clears `0x400`, 0 clears both; confirmed (code) |
| byte `+0x41` | `Human_ClassCanFlee` (`0x00224ea0`) in `Brain_Think` (`0x0028f6c0`) | an AI with a fight goal and **nothing in hand** draws a weapon: when free, it clears its actions and plays 477 `ANIM_ONE_HANDED_OBJECT_DRAW` (`GenToIdleAnimAction`). A cop (brain kind 1) does so in or out of a fight, unless his type's weapon (`CfgChar` `+0x18c`) is `none`. The clip's object event named `random` (`Anim_FireFrameEvents` `0x00101dd8`) calls `Brain_GiveHandObject` (`0x0028c780`), which puts the type's weapon in his hand, or `dyn_swhbld` (a switchblade) when it is `none`. The name "can flee" is an old guess: the byte is "draws a weapon"; confirmed (code) |
| byte `+0x42` | `Human_ShouldFleeHurt` (`0x00224ec8`) in `Brain_Think` | an AI with a fight goal whose health is under 30 % plays 665 `ANIM_SPECIAL_FLASH` (blend 0.1 s in and out) when free, after clearing its actions: an AI's flash. The name is likewise a guess the code does not bear out; confirmed (code) |

Human flag `0x1000000000` (power class `+0x3b`, or `HuSetHardToStun`) changes the stun rule of `Human_PlayReaction`:
without it a hit stuns when its Anim Range List flag `0x400` is set or the caller asks; with it neither counts, and
the hit stuns only a human already stunned on his feet (`Human_IsStunnedStanding`). Flag `0x100` (never stunned) is
tested first. Confirmed (code) at `0x0026a6d0`. Who reads `0x400000` and `0x800000000` in the human flags was not
found (the state and held flags of the same values are other words).

`Human_StaminaMax` (`0x00223188`) returns `+0x2c`; for a player, when unlockable `(6, 12)` is unlocked, it returns
`+0x2c × (1 + b × 0.01)` rounded, where `b` is byte 3 of `CfgWarriorUpgrade`'s record (`0x006b6650`). The power
meter's maximum (`0x00223068`) is `+0x28`, likewise raised by byte 2 with `(6, 11)`, then times `+0x18` while the
human is hurt. Confirmed (code); the unlocks: [Unlockables](player-state.md#unlockables).

**Which class a human has** (`Human_Init`, `0x00218008`; confirmed (code)): `+0x1b8` and `+0x1b9` both take the
type's `CfgChar` byte `+0x11d`; for a type of category 14 (the Warriors) `+0x1b9`, the class it plays with as a
player, is then replaced by `0x00222ba8`'s by type: 58 Ajax (types 11-14), 59 Cleon (1-4, 189), 60 Cochise (15-17),
61 Cowboy (18-20, 188), 62 Fox (21-25), 63 Vermin (26-29, 191), 64 Rembrandt and Ash (30-32, 38-40), 65 Snow (33-37,
190), 66 Swan (5-10), 7 for any other. The **Warrior class** (`+0x1ba`, `0x00222c20`) follows the same groups: 0
Ajax, 1 Cleon, 2 Cochise, 3 Cowboy, 4 Fox, 5 Vermin, 6 Rembrandt and Ash, 7 Snow, 8 Swan, 9 anyone else. Each
difficulty script sets every class again and re-runs `CfgChar` for 63 types. Every value:
[Power classes](../references/power-classes.md), [Warrior classes](../references/warrior-classes.md).

### Character classes {#classes}

`CfgChar` (in `config_preload2.lua`) fills a 0x1ac-byte record per type at `0x00684620 + id × 0x1ac`: 45 floats from
`+0x00`, 16-bit values from `+0xb8`, the model index by name at `+0x112` and by `"<name>_a"` at `+0x114` (used in
levels 60-64), the speed class byte at `+0x11c`, and four strings (32 bytes each) from `+0x14c`. Rembrandt's call is
`CfgChar(32, ..., 1800, DamageFox, Att_Warrior, 1, "warr_re_cv", "none", 7, 0, RangeNormal, "none", 0, "none")`; type
40 (Ash) uses `warr_ty_cv`. Confirmed (code) for the layout; the arguments inferred from the disassembly.

`Human_Init` remaps the type: 32 becomes behaviour class 30 (`0x1e`) with a variant flag (confirmed (code) at
`0x00218008`, runtime `+0xcc` = `0x1e`, `+0xd0` = `0x20`).

**The fields with a known reader** besides the tables above, confirmed (code) at the addresses given:

- **The range table** (45 floats at `+0x00`, the script's numbers ÷ 100, so metres): read once per human by
  `Human_AttachInstance` (`0x00217a98`) when its model is attached, beside the damage table. For each attack kind
  *k* (the index of the damage and attack tables) whose value is above 0, `Attack_CallKindHandler` (`0x002545e0`,
  jump table `0x0055d640`, kinds 0-41) writes it as the **far range** (value × 1000, mm; [Anim Range List
  `+0x08`](formats/animation.md#anim-range-list)) of that kind's anim ids with `AnimRange_SetFarRange`
  (`0x002545a8`). A 0 leaves the anims' own far range. The ids per kind: 0 → 11; 1 → 12; 2 → 13; 3 → 15; 4 → 14;
  5 → 16; 6 → 17; 7 → 19; 8 → 18; 9 → 20; 10 → 25-30; 11 → 21; 12 → 193; 13 → 194; 14 → 237-240; 16 → 653, 655;
  17 → 657, 659; 19 and 20 → 0; 21 → 3, 4; 22 → 70, 71; 15, 18 and 23-44 none. The far range is what
  `Human_CanStartAttack` and the steer use ([Combat](combat/index.md#run-attacks)); kind 22's ids 70, 71 are the grab's
  search and kind 21's 3, 4 the tackle's. The list written is the character data's (record `+0x160` points at the
  model's own list, `HumanRecord_SetAttackTable` `0x00254568`), so every human with that model shares the values the
  last one attached wrote (inferred from the pointer; the damage table has the same reach).
- **The voice** (s16 `+0x118`): human `+0x3b0`, the folder `vags/character/voices/<n>/` (format string at
  `0x0055a548`, `l<level>_c<checkpoint>_idle_<nn>`); type 259 instead takes a random 120-125. `Human_Init` counts the
  type's idle lines 1-3 there (`+0x197`, `+0x196` set when any exists). `+0x3b4` is the level's variant
  (`Anim_LevelVariant`) or the voice. So the binding page's "Warrior index" was a misreading: Rembrandt's 7 is his
  voice folder. Confirmed (code) in `Human_Init` (`0x00218008`), the format string read at `0x0055a548`.
- **The weapon** (string `+0x18c`): when it is not `none`, `Human_Init` puts that object type in the new human's hand
  (`Human_PlaceItemInHand(h, name, 1, 1)` `0x0024c280`, confirmed (code): made by name, recorded as a pinned object,
  refused when the hand is already busy with held flag `0x4000`). The police types carry `dyn_baton`. A cop who later
  has nothing in hand draws it again, and an AI of a power class with byte `+0x41` draws it, or a switchblade when it is
  `none` ([Power class fields](#power-class-bytes)).

#### Body scale {#body-scale}

`Human_Init` ends by sizing the human (confirmed (code), `0x00219040`-`0x002191fc`), stored by `Human_StoreScale`
(`0x00219608`, which also rescales the physics body):

1. **A random size**, only for a human whose brain type is not 3 (a Warrior), whose class category (`+0x11b`) is not
   12 or 13, and whose type (`+0xcc`) is none of 122, 221, 222, 303-305, 307, 351, 414-416, 425, 458, 459. With `r` =
   `Random_Int(99)` (0-99, stream `0x006eb880`, confirmed (code) at `0x002190d4`) and "hat" = the type's hat (`+0x14c`)
   is not `none` (a string compare at `0x0021900c`, so an empty name counts as a hat):
   - a **woman** (`flag_14b`, human `+0x3b8`, any value but 0: a byte load and a branch on not zero at `0x002190e0`,
     confirmed (code)): 1 − 0.01 × (`r` / 25), so 1.0, 0.99, 0.98 or 0.97, a quarter each; with a hat at least 0.99;
   - anyone else: `r` 0-30 → 1.0; 31-54 without a hat → 1.02 for a cop (brain type 1) or a gang member (brain type
     2) whose brain `+0x20c` record's `+0x2c` is below 23, else 0.98; otherwise (31-54 with a hat, or 55-89) → 1.01
     when `r` < 90, else 1.0.
2. **The type's fixed size** (`Human_ApplyTypeScale`, `0x00219638`, also run by `Human_AttachInstance`; confirmed
   (code)) replaces it for these types (`+0xcc`): 5 → 1.02; 18, 30 → 0.97 (so Rembrandt's 0.97, as seen at runtime); 21,
   23, 24 → 0.98; 33 → 1.03; 89 → 1.16; 119, 120, 135 → 1.2; 128 → 1.175; 161 → 1.11. With game state `+0x158` set
   (co-op), 89, 119, 120, 128 and 161 take 1.1 and 135 takes 1.03.

`Human_GetScale` (`0x0021d020`) returns it, except 1.0 in a scene for a scale under 1.1. Confirmed (code).

### Stats {#stats}

Everything that makes one character stronger, tougher or faster than another comes from four configuration tables, all
set by the configuration scripts and again by each difficulty's (the boot set equals normal's). The sections above
and the pages they link hold the detail; this is the one list. "Human" is the field the value lands in when a human
is made or becomes a player; the evidence is as the cited page gives it. Every type's values on normal:
[Characters](../references/characters.md) (its *Stats (normal)* column joins the four tables).

**1. The character type** (`CfgChar`, record `0x00684620 + type × 0x1ac`, [Character classes](#classes); confirmed
(code) at `CharClass_Get`, `0x00228800`); `Human_Init` (`0x00218008`) reads it at creation, `+0x11b` and `+0x11d` from
the type's own record and the rest after the [remap](#type-to-model) from the class's:

| Stat | Field | Human | What it does | Evidence |
| --- | --- | --- | --- | --- |
| Behaviour | byte `+0x11a` | brain type | the AI's brain kind ([AI: types](ai/index.md#types)) | confirmed (code) |
| Category | byte `+0x11b` | (read from the record) | 14 the Warriors: a player takes his Warrior's power class; 13 the bosses and big fighters; the money class and the civilian reactions ([Starting money](crimes.md#starting-money)) | confirmed (code) |
| Speed class | byte `+0x11c` | record `+0x164`-`+0x17c` | the [speed class](#speed-classes), a fallback the clips' own speeds replace in play | confirmed (code) |
| Power class | byte `+0x11d` | `+0x1b8`, `+0x1b9` | below; a Warriors player's `+0x1b9` is his Warrior's | confirmed (code) |
| Health | s16 `+0x116` | record `+0x144` / `+0x146` | health and its maximum (1800 for every Warrior, 600 for the street civilian) | confirmed (code); a Warrior's 1800 confirmed (runtime), run r-20261027-warrior-health-scale ([Combat: damage](combat-reactions.md#damage)) |
| Damage table × damage scale | s16 × 45 at `+0xb8` | the human's Anim Range List | written over the list's damages when the model is attached (`0x00217a98`); a player's first times his Warrior class's damage % ([Damage](combat/index.md#damage-table)) | confirmed (code) |
| Attack table | byte × 45 at `+0x11e` | brain `+0x298` | an AI's weight for each attack kind ([AI](ai/index.md)) | confirmed (code) |
| Range table | float × 45 at `+0x00` (÷ 100) | not traced | no reader found | not traced |
| Hat | string `+0x14c` | the hat object | worn from creation ([Hats](characters-appearance.md#hats)) | confirmed (code) |
| Voice | s16 `+0x118` | voice set | the folder of the type's speech ([Characters reference](../references/characters.md#fields); the binding page calls it a Warrior index) | inferred |
| `flag_14b` | byte `+0x14b` | `+0x3b8` | a civilian woman's tag colour ([Tagging](crimes.md#tag-look)), the `_female` animation sounds ([Sound events](sound-events.md#players)), the money record 16 for category 4 ([Starting money](crimes.md#starting-money)); the body scale `+0x65c` = 1 − 0.01 × n is read beside it, `n` not traced | confirmed (code); the scale not traced |
| Drop group, drop chance | string `+0x16c`, int `+0xb4` | `+0x257`, `+0x278`, money `+0x370` | the carried object and its drop chance, and the starting money ([Starting money](crimes.md#starting-money)) | confirmed (code) |
| Weapon | string `+0x18c` | not traced | read by `Human_Init`; what it gives is not traced | not traced |

**2. The power class** (`CfgPowerClass`, record `0x006619a0 + class × 0x44`, [Power classes](#power-classes)), by
`+0x1b9` for a player and `+0x1b8` otherwise (`0x00222b78`). A player of a Warriors type plays with 58-66 by Warrior
(7 without one); everyone else with the type's own class. Confirmed (code) at each reader:

| Stat | Field | What it does |
| --- | --- | --- |
| Power meter | s16 `+0x28`, `+0x2a` | maximum and refill per second (record `+0x148`); × `+0x18` while hurt (`0x00223068`, confirmed (code)); with the gym's power upgrade a player's maximum × (1 + byte 2 / 100) |
| Stamina | s16 `+0x2c`, `+0x2e` | maximum and refill per second (record `+0x14a`, `Human_StaminaMax` `0x00223188`, confirmed (code)); with the gym's stamina upgrade a player's maximum × (1 + byte 3 / 100) |
| Hurt threshold | float `+0x04` | below this share of maximum health the human is hurt (`0x00222ff8`, confirmed (code)) |
| Stun, down | s16 `+0x30`, `+0x34`; floats `+0x10`, `+0x14` | a stun's and a knockdown's length, ms, and their factors while hurt ([Reactions](combat-reactions.md#reactions)) |
| Weapon stun | float `+0x00` | as the attacker, scales the victim's stun for weapon hits of 20-49 damage (`0x0022f658`, confirmed (code)) |
| Grab struggle | byte `+0x36` | the grabbed human's strike costs the grabber 1 / n of its power (`0x0027fd68`, confirmed (code)) |
| AI: block, counter, attack delay | floats `+0x08`, `+0x0c`, `+0x24`, `+0x1c`, `+0x20`; bytes `+0x37`, `+0x39` | the AI's block chances, counter chance, attack delay factors, pattern-reading threshold and throws at walls ([AI](ai-fights.md#block)) |
| Health bar | byte `+0x40` | the pause Stats screen's Health bar (`+0x40` / 100, [Stats screen](pause.md#stats-screen)); the health rings' span ([HUD](hud-radar.md#the-health-rings)) |
| Busy after a hit | s16 `+0x32` | a hit keeps an AI busy for this long, ms ([Power class fields](#power-class-bytes)) |
| See [Power class fields](#power-class-bytes) | `+0x38`, `+0x3a`-`+0x3f`, `+0x41`, `+0x42` | their readers |

**3. The Warrior class** (`CfgWarriorClass`, record `0x006b65c0 + class × 14`, human `+0x1ba` by type: 0-8 the nine
Warriors, 9 everyone else), read through `0x00222b58`. Confirmed (code) at each reader
([Warrior classes](../references/warrior-classes.md)):

| Stat | Field | What it does |
| --- | --- | --- |
| Rage | s16 `+0x00`; bytes `+0x02`-`+0x04` | the rage maximum, the gain percentage, and the idle decay and the drain while raging, percent of the maximum per 20 s ([Rage](combat-rage.md#rage)) |
| Health % | byte `+0x05` | when the human becomes a player (`Human_MakePlayer` `0x00229c40`, confirmed (code)) his health and maximum are times n %, rounded; divided back when he stops being one (`0x0022a2a8`). Rembrandt: 1800 × 50 % = 900 |
| Damage % (Strength) | byte `+0x06` | a player's attack damages times n % ([Damage](combat/index.md#damage-table)); the Stats screen's Strength bar |
| Context games | bytes `+0x07`-`+0x0c` | the cuffing hold, the uncuff mash, tagging, lock picking, the stereo theft and mugging |

**4. The gym upgrades** (`CfgWarriorUpgrade`, bytes `0x006b6650`-`0x006b6653`; 10, 20, 10, 20), each only while its
unlockable is unlocked ([Unlockables](player-state.md#unlockables)): punch damage + 10 % (6, 5), kick damage + 20 % (6,
6), the power maximum + 10 % (6, 11), the stamina maximum + 20 % (6, 12). Confirmed (code)
([`CfgWarriorUpgrade`](../references/bindings/config.md#cfgwarriorupgrade)).

**The pause Stats screen's bars** read these: Strength from the damage %, Stamina from the power and stamina maxima
(upgrades included), Health from the power class's `+0x40`, Rage from the rage maximum / 150. The formulas are on
[Pause: Stats screen](pause.md#stats-screen). "Strength" is not a field of its own: it is the Warrior class's damage %.

### Speed classes {#speed-classes}

`CfgSpeedClass(class, ...)` writes six floats at `0x006b6548 + class × 0x18`. The getters read entry `+0x00` (base),
`+0x08` (walk), `+0x0c` (jog), `+0x10` (run) and `+0x14` (sprint); `+0x04` has no getter found. Values from
`config_preload2.lua` (inferred from the disassembly):

| Class | +0x00 | +0x04 | walk | jog | run | sprint |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | 3 | 0.8 | 1.6 | 4.1 | 6.5 | 8 |
| 1 | 3 | 0.8 | 1.6 | 4.1 | 7.5 | 9 |
| 2 | 3 | 0.8 | 1.6 | 1.6 | 1.6 | 9.75 |
| 3 | 3 | 0.8 | 1.6 | 4.1 | 7.5 | 7.5 |
| 4 | 3 | 0.964 | 0.964 | 0.965 | 2.723 | 2.723 |

**But these are not used in play.** When `0x005101e0` is set (it is 1 at runtime), each getter returns the human's
own value from its 0x180-byte record times `+0x3a4` instead (confirmed (code) at `0x00221710`, `0x002215d0`): `+0x164`
for the base, `+0x170` for walk, `+0x174`, `+0x178`, `+0x17c` for jog, run and sprint.

**The speeds come from the clips.** `0x00254078` (called when the character is attached, `Human_AttachInstance`
`0x00217a98`, and after a movement-style change, `0x00243848`, `0x00253688`) computes each one from the clip in an
[anim slot](characters-animation.md#anim-slots) as the clip's horizontal root displacement over its playing time:
`sqrt(dx² + dy²) / (duration / rate)`, with the displacement from the clip's descriptor (`0x00101950`), the duration
from `0x00101a00` and the rate from `0x00104a38`. Confirmed (code). It writes, in the 0x180 record:

| Record field | Speed | From slot | Rembrandt (runtime) |
| --- | --- | --- | --- |
| `+0x164` | base (combat walk) | 14 | 3.429 (clip 380) |
| `+0x16c` | sneak walk | 3 | 1.585 (407) |
| `+0x170` | walk | 4 | 1.629 (408) |
| `+0x174` | jog | 5 | 4.857 (409) |
| `+0x178` | run | 6 | 7.801 (410) |
| `+0x17c` | sprint | 7 | 10.245 (411) |

The runtime values are confirmed (runtime), run r-20261124-chars-turn-table. Each equals the clip's displacement
divided by its duration as read from the disc (inferred from Coney's disc test, `repo:tests/human/disc_player_test.cpp`,
whose speeds worked out from the clips match them). The speed class table is therefore only a fallback.

**Gait from speed** (`0x0022aeb0`, the gait stored with the velocity): below 0.5 m/s gait 0; otherwise the gait 1-5
whose speed (`+0x16c`, `+0x170`, `+0x174`, `+0x178`, `+0x17c`) is nearest. Confirmed (code); a jump table at
`0x0055bec0`. It scans from gait 5 down and stops at the first gait whose next lower one is strictly farther, so a
tie goes to the lower gait. With Rembrandt's speeds (above, `+0x3a4` = 1.0) the bounds are the midpoints: gait 0
below **0.5**, 1 below **1.607**, 2 below **3.243**, 3 below **6.329**, 4 below **9.023**, 5 above. So the combat
walk at 3.429 m/s is **gait 3** (1.428 from the jog, 1.800 from the walk), the free walk at 1.629 is gait 2 and the
run at 7.801 gait 4. Confirmed (code) at `0x0022aeb0`; the bounds follow from the runtime speeds.

## Behaviour

### From a type to a model {#type-to-model}

How `HuCreate`'s type picks what is drawn and animated, confirmed (code) at `0x00218008` (`Human_Init`),
`0x00228af8` (`Cfg_SetCharacterClass`, `CfgChar`'s writer) and `0x001780e8`:

1. **`CfgChar` resolves the model name once**, when `config_preload2.lua` runs: its model-name argument (`warr_re_cv`
   for type 32) is hashed (CRC-32 of the lower-case name, [Name hashing](name-hash.md)) and looked up in the
   **Character List** (the resource manager's `+0x88`, the chunk `0x44` records of
   [Files](characters-appearance.md#files)); the record's **index** is stored at `+0x112` (`0xffff` when the name is not
   in the list). The same is done for `"<name>_a"` into `+0x114`.
2. **`Human_Init` maps the type to a class** with a fixed switch. Types not in it are their own class. A type marked
   *variant* below sets a flag; the others are plain aliases of their class:

    | Class | Plain aliases | Variants |
    | --- | --- | --- |
    | 1 | 2 | 3, 4 |
    | 5 | 6 | 7, 8, 9, 10 |
    | 11 (`0xb`) | 12 | 13, 14 |
    | 15 (`0xf`) | 16 | |
    | 18 (`0x12`) | 19 | 20 |
    | 21 (`0x15`) | 22 | 25 |
    | 26 (`0x1a`) | 27 | 28, 29 |
    | 30 (`0x1e`) | 31 | 32 |
    | 33 (`0x21`) | 34 | 35, 36, 37 |
    | 38 (`0x26`) | | 39, 40 |
    | 41 (`0x29`) | | 42 |
    | 43 (`0x2b`) | | 44 |
    | 45 (`0x2d`) | | 46, 47, 48 |
    | 221 (`0xdd`) | | 222 |

3. **The model index** (kept at `+0x37c`): the **type's own** record when the human is not a player (player index
   below 1) or the type is a variant; otherwise (a player created as a plain alias) the **class's** record. Of that
   record it reads `+0x114` (the `_a` model) when the current level's number is 60-64 (the Armies of the Night bonus
   levels), else `+0x112`. So a player made as type 2 (`warr_cl_gen`) is drawn as type 1's `warr_cl`, while type 32
   keeps `warr_re_cv`.
4. **The rest of `Human_Init` reads the class record**: `+0xcc` holds the class from here on, and the fields read
   after the remap (`+0x118`, `+0x11a`, `+0x14b`, `+0xb4`, the strings at `+0x14c` and `+0x18c`) come from it; only
   `+0x11b` and `+0x11d` are first read from the type's own record. `+0xd0` keeps the type as given. Whether the
   later systems (combat, health) read the class or the type is not traced here.
5. **The files**: the Character List record at the model index names the three resources (character data with the
   animations, model, texture dictionary, [Files](characters-appearance.md#files)); `Human_Init` makes the instance at
   once when they are resident (`0x001774d0`, `0x00177b80`, then `Human_AttachInstance` `0x00217a98`; confirmed (code)),
   else the resource manager loads them later ([Creation](characters/index.md#creation)). The anim set is that character
   data over the generic defaults (its 722 slots, [Files](characters-appearance.md#files)) and the [anim
   slots](characters-animation.md#anim-slots), the same for every type until a movement style changes them.

For an implementer with Coney's recorded `CfgChar` calls and its `CharacterList`: find the type's call, apply the
alias rule for a player, take the model-name argument (or `<name>_a` in levels 60-64) and look it up by name. The
[character reference](../references/characters.md) lists every type's model name.

## In Coney {#coneys-implementation}

### Stats in Coney {#stats-coney}

Coney reads the four tables from the configuration calls the scripts make (`characters::CharacterTypes`,
`ai::aiConfigFrom()`). Since `SetDifficulty` became real each level's start runs the difficulty script of the profile's
difficulty after the boot set (normal's); whether every reader takes that script's values over the boot's is not checked
yet ([The difficulty: q-difficulty-configuration](difficulty.md#q-difficulty-configuration)). A human gets its type's
stats when it is made (`ai::AiHumans::spawn()`, from the cast of a level's scripts) and a player when he is made or
changes type (`human::playerClassOf()`, `human::playerProfileOf()`):

| Stat | Applied? | Where | Test |
| --- | --- | --- | --- |
| Behaviour | yes, AI | `ai::aiConfigFrom()` → brain type | `tests/ai/disc_ai_config_test.cpp` |
| Category | yes: boss class, a Warriors player's power class, the money class | `ai::FighterClass::category`, `characters::powerClassOf()`, `characters::moneyCategory()` | `tests/characters/character_types_test.cpp`, `tests/characters/starting_money_test.cpp` |
| Speed class | no (a fallback; Coney uses the clips' speeds as the original does in play) | | |
| Power class (AI and player) | yes | `play_level_cast.cpp` (the type's `+0x11d`), `characters::CharacterTypes::playerTraitsOf()` | `tests/characters/character_types_test.cpp`, `tests/platform/disc_change_character_test.cpp` |
| Health (AI) | yes | `ai::FighterClass::health` | `tests/ai/brain_test.cpp` |
| Health (player) × Health % | yes | `characters::PlayerTraits::health`, `human::playerProfileOf()` | `tests/characters/character_types_test.cpp`, `tests/platform/disc_change_character_test.cpp` |
| Damage table × scale; player × Damage % | yes | `characters::parseCfgChar()`, `human::Human` (its own Anim Range List) | `tests/characters/character_types_test.cpp`, `tests/combat/anim_ranges_test.cpp` |
| Attack table | yes, AI | `ai::FightSettings::attackWeights` | `tests/ai/fight_goal_test.cpp` |
| Range table | no: not traced | | |
| Hat | yes | `platform/play_level_hats.cpp` | `tests/world_objects/hats_test.cpp` (the fittings; the creation path has none) |
| Voice | yes | `script::voiceSetOfType()` | `tests/audio/game_sound_test.cpp` |
| `flag_14b` | the tag colour and the female sounds; not the body scale (every human has the player's 0.97) | `play_level_cast.cpp`, the sound events | `tests/audio/human_sound_events_test.cpp` |
| Drop group, drop chance, money | yes | `characters::rollStartingCarry()` at `HuCreate` | `tests/characters/starting_money_test.cpp` |
| Weapon | no: not traced | | |
| Power meter (`+0x28`, `+0x2a`) | yes, every human (was the player's 400 and 60 for all before) | `combat::PlayerCombat` from `human::FighterProfile::powerClass` | `tests/human/human_test.cpp`, `tests/platform/disc_change_character_test.cpp` |
| Hurt power factor (`+0x18`) | yes | `combat::PowerMeter::setHurt()` | `tests/combat/meters_test.cpp` |
| Stamina (`+0x2c`, `+0x2e`) | yes, every human (was the player's 135 and 40 for all) | `human::Stamina`, `human::Human::spawn()` | `tests/human/stamina_test.cpp`, `tests/human/human_test.cpp`, `tests/platform/disc_change_character_test.cpp` |
| Hurt threshold, stun, down times | yes | `human::Victim`, `human::Fighter::hurt()` | `tests/human/being_hit_test.cpp` |
| Stun and down factors while hurt (`+0x10`, `+0x14`), weapon stun (`+0x00`) | no | | |
| Busy after a hit (`+0x32`) | yes, AI: the hit timer `ai::humanBusy()` reads (its callers in Coney, not the original's 41) | `human::Fighter::busyUntilMs()`, `ai::humanBusy()` | `tests/ai/brain_test.cpp`, `tests/characters/character_types_test.cpp` |
| Grab struggle (`+0x36`) | yes | `human::Fighter` | `tests/combat/grabbed_test.cpp` |
| AI block, counter, delays, walls (`+0x08`, `+0x0c`, `+0x1c`, `+0x20`, `+0x24`, `+0x39`) | yes | `combat::PowerClass`, the brains | `tests/ai/fight_reactions_test.cpp`, `tests/ai/brain_test.cpp` |
| AI pattern-reading threshold (`+0x37`) | no | | |
| `+0x40` (health rings) | yes: read from the configuration now (was Rembrandt's 35 for every class) | `characters::parseCfgPowerClass()`, `play_level_world.cpp` | `tests/characters/character_types_test.cpp`, `tests/hud/health_rings_test.cpp` |
| Rage (max, gain, decay, drain) | yes, the player's Warrior class (was class 6's for every player) | `combat::RageClass`, `combat::RageMeter` | `tests/combat/meters_test.cpp`, `tests/human/human_test.cpp`, `tests/platform/disc_change_character_test.cpp` |
| Health % given back on release | no: Coney's hand-over is a stand-in that keeps one player human ([hand-over](characters-players.md#level99-handover)) | | |
| Lock pick, tagging, mash bytes | yes | `scripting/object_bindings.cpp` | `tests/scripting/object_bindings_test.cpp` |
| Cuffing, stereo, mugging bytes | no: those games are not implemented for a player | | |
| Gym upgrades | no | | |
| Stats screen | no: the pause menu's Stats item is not implemented | | |

Coney's readings: the player's health is read from the type's own `+0x116` (the page does not say which record
`Human_Init` takes it from; every Warriors alias has its class's value), rounded to the nearest; a configuration with
no `CfgWarriorClass` leaves the health unscaled and the rage at class 6's.

- **The model** (`src/characters/character_class.h`, from [From a type to a model](#type-to-model)):
  `characterClassOf` is `Human_Init`'s switch, `modelRecordType` the player's plain-alias rule (a player made as a
  plain alias is drawn as the class's own type; a variant keeps its own), and `modelNameFor` takes the model name from
  that type's recorded `CfgChar` call (its tenth argument), with `_a` added in levels 60 to 64; `HuCreate` keeps it
  and the play mode loads it from the Character List. Disc check: `level2` checkpoint 3 is drawn as `warr_cl` (Cleon),
  `level3` checkpoint 4 as `warr_sn` (Snow), `level99` checkpoint 1 as `warr_re_cv`. Coney's choice: a type with no
  `CfgChar` call or a model the Character List lacks is drawn as Rembrandt, with a log line.

### Notes for implementers {#notes-for-implementers}

- **Speeds per human** come from the clips: for each locomotion slot, the clip's horizontal root displacement over
  its duration ([Speed classes](#speed-classes)). For Rembrandt walk 1.63, jog 4.86, run 7.80, sprint 10.25 m/s.

## Open questions

- **The `+0x65c` scale's source**: what the division in `Human_Init` takes.

## How this was checked

Code claims were read in Ghidra in `SLUS_212.15`.
