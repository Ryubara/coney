---
kind: topic
system: ai
owns:
  functions: ["0x0016cdf0", "0x00166308", "0x00165678", "0x00165738", "0x0016c3a8", "0x0016acf0", "0x0016ad80"]
---
# Gang records: making a gang, joining it and its leader

Verified against: `SLUS_212.15` (NTSC-U), static reading in Ghidra and PCSX2 (run r-20261127-gang-records).

## Summary

A level's scripts make each gang with `GangCreate`, put humans in it with `GangAddMember` and set who is enemy or
friend of whom. The gang's leader is the human its tactics move the group around and take sides through. This page is
how a gang record is made, how a member joins it, how the leader is kept or picked again, and how two gangs become
enemies or friends. The gang record itself, the update and the tactics are on [AI gangs](ai-gangs.md#gang-record);
what a member leaving and the follow command do is on [The default command: follow](ai-gangs.md#warrior-follow).

## Map

| Behaviour | Function | Level |
| --- | --- | --- |
| [Making a gang](#create) | `0x0016cdf0` `Gang_Create` | confirmed (code) |
| [Making the wanted gangs hostile to a kind](#create) | `0x0016c3a8` `Gang_SetHostileToKind` | confirmed (code) |
| [Joining a gang](#join) | `0x00166308` `Gang_AddMember` | confirmed (code) |
| [The leader kept](#leader) | `0x00165678` `Gang_GetLeader` | confirmed (code) |
| [The leader picked](#leader) | `0x00165738` `Gang_PickLeader` | confirmed (code) |
| [Enemies and friends](#relations) | `0x0016acf0` `Gang_MakeEnemies`, `0x0016ad80` `Gang_MakeFriends` | confirmed (code) |

The script bindings `GangCreate`, `GangAddMember`, `GangMakeEnemies` and `GangMakeFriends` call these
([the gang bindings](../references/bindings/gang.md)).

## Data

The fields this page reads and writes are in the gang record's table on [AI gangs](ai-gangs.md#gang-record): the
leader handle `+0x44`, the 16 member handles `+0x48`, the masks `+0x38` and `+0x3c`, the clip groups `+0x1b0`; and
the wanted timer `+0x5e8` (an end time, 0 when not wanted, [Crimes](ai-gangs.md#crimes)). Confirmed (code) at
`0x0016cdf0` and `0x00166308`.

The clip tables `Gang_Create` and `Gang_AddMember` install, each a list of animation names:

| Who | Group | Clips (table) | Level |
| --- | --- | --- | --- |
| Kind 1 (police) | `0x29c` | `cop_lost_target_search`, `cop_spotting_trouble`, `cop_fidget_idle`, `cop_radio` (`0x0050cbb0`) | confirmed (code) |
| Kind `0x17` | `0x29c` | `gen_cower_plead`, `cop_lost_target_search`, `gen_cower_point`, `hurry_up` (`0x0050cb80`) | confirmed (code) |
| Kind `0x17` | `0x25c` | `fidget_crossarms`, `fidget_head_scratch`, `fidget_nose_wipe`, `fidget_tie_shoe` (`0x0050cb90`) | confirmed (code) |
| Kind `0x17` | `0x253` | `taunt1_adv` to `taunt4_adv` (`0x0050cba0`) | confirmed (code) |
| Kind `0x18` (dealers) | `0x25b` | the eight dealer fidgets of [GoalDealer](ai-scripting.md#dealer-gestures) (`0x0050cbc0`) | confirmed (code) |
| Kind `0x18` (dealers) | `0x29c` | `dlr_becken_1`, `dlr_becken_2`, `money_take` (`0x0050cbe0`) | confirmed (code) |
| A member of type `0xed` | `0x29c` | `l55_gun_aim`, `l55_gun_fire`, `l55_gun_warn`, `l55_gun_wave` (`0x0050cb30`) | confirmed (code) |

## Behaviour

### Making a gang {#create}

Confirmed (code) at the cited addresses.

`Gang_Create` (`0x0016cdf0`, `GangCreate(kind, name)`), confirmed (code):

1. A gang in use with the name already: −1. Otherwise the first record not in use (`Gangs_FindFreeSlot`).
2. The record is reset (`Gang_Reset`, `0x00164808`): not in use or suspended; members, turf boxes, handlers, masks,
   tactic and clip groups cleared; the spawners reset; the wanted timers 0; the respond percent `+0xd7` 30, the
   team-ups `+0xd9` 0, may-flee `+0xdf` 1, `+0xe0` 0xff; the leader and the shared target the null handle.
3. It is put in use and calm (`Gang_SetAlertState(0)`), given the kind, name and id, the spot-line flag `+0xd2`, the
   kind's three music track names (`CfgGangMusic`), and no spawner active.
4. Kinds 1, `0x17` and `0x18` get their clip groups (the table under [Data](#data)).
5. For a police gang (kind 1), every **other** gang in use whose wanted timer runs is passed to
   `Gang_SetHostileToKind(gang, 1, mutual)` (`0x0016c3a8`), `mutual` being whether game state `+0x28c`
   (`ForceCrimeLevel`) is 0. That function makes every gang in use of the kind, so the new police gang too, treat the
   gang as an enemy (`Gang_SetEnemyBit`: the friend bit cleared, the enemy bit set), and with `mutual` the gang treat
   each of them so too.

   Seen running: with record 2 made wanted (`Gang_SetWantedTimer`, `0x00169878`) and the crime level not forced, two
   police gangs made one after the other (records 6 and 7) each held record 2's enemy bit, and record 2's enemy mask
   held both of theirs (`0xc0`); confirmed (runtime), run r-20261127-gang-records.

### Joining a gang {#join}

Confirmed (code) at the cited addresses.

`Gang_AddMember` (`0x00166308`) has no early return for a human already in the gang, confirmed (code):

1. A human in any gang leaves it first (his actions, target, counted enemies and brain `+0x164` list cleared, then
   `Gang_RemoveMember`), as [follow](ai-gangs.md#warrior-follow) describes.
2. The gang's member count is tested against 16 for kinds 1 and `0x17`, else 10. Past it the function only calls
   `Gangs_ResolveAllMembers` (`0x0016d0e0`), which resolves every handle and keeps no result: **a full gang never drops
   a member**.
3. Brain `+0x20c` names the gang, the attack weights are loaded from the class again (`Brain_LoadAttackWeights`,
   `0x0028aa18`), and god mode (human flag `0x10`) is set under an invincible gang.
4. His handle goes into the first free of the 16 member slots (`HandleArray16_PutInFree`). With none free nothing is
   stored: he names the gang but no slot holds him.
5. A player who is war chief (human `+0x3ac` = 1) becomes the leader; a human of character type `0xed` gives the gang
   his own cower clips (the table under [Data](#data)).
6. Event `0x16` goes to the gang with the count before plus one and `+8` = 1.

### The leader {#leader}

Confirmed (code) at the cited addresses.

`Gang_GetLeader` (`0x00165678`) keeps the human the handle at `+0x44` names unless he is gone, is dead (state
`0x100000000`, `0x00227eb0`), or is **not a player** (human `+0x1b0` = −1) and is cuffed (`0x00223b70`) or down
(`Human_IsDownOrDead`, `0x00227e60`). A player who leads stays the leader while arrested or knocked down; only his
death passes the lead on. The handle is resolved globally, so a leader named outside the gang stays while he passes.
Confirmed (code).

Otherwise `Gang_PickLeader` (`0x00165738`) clears `+0x44` and stores the first member slot that passes, in three
passes, confirmed (code):

1. A player who is player 1 (game state `+0x228`), or any player while player 1 is dead; or a member whose class role
   (`CharClass` `+0x11b`) is 12 or 13 (the police; an unsigned compare) and is not cuffed.
2. A member neither down nor cuffed.
3. The first member at all (`HandleArray16_First`); the null handle for an empty gang.

Seen running, in level99's Warriors gang (the player and Ash): the player arrested (state flag `0x20000`) and still
the leader; Ash made the leader (`GangSetLeader`), then arrested, and the pick gave the lead back to the player, player
1 in pass 1. Confirmed (runtime), run r-20261127-gang-records.

### Enemies and friends {#relations}

Confirmed (code) at the cited addresses.

`Gang_MakeEnemies(a, b)` (`0x0016acf0`) sets each gang's enemy bit for the other and clears its friend bit
(`Gang_SetEnemyBit`, `0x001690c8`), both ways; `Gang_MakeFriends` (`0x0016ad80`) does the reverse
(`Gang_SetFriendBit`, `0x00169060`). Each ignores id −1 and checks nothing else, so a record not in use is written
too. Confirmed (code).

The bit written is not the id passed but the other record's **stored id** `+0x30`, which only `Gang_Create` sets
(`0x0016cdf0`, to the record's index). `Gang_Construct` (`0x00164630`), `Gang_Reset` (`0x00164808`), `Gang_Destroy`
(`0x00164738`) and `Gangs_ResetAll` (`0x0016c298`) leave it, so it keeps its index from the first `GangCreate` there
until the game ends, across levels. Confirmed (code). So for a record not in use:

- **made before and deleted since**: the gang named holds its real bit, and a gang made there later is already its
  enemy or friend (the new gang's own masks are cleared in the reset). Confirmed (code); confirmed (runtime), run
  r-20261127-gang-records: `GangMakeEnemies(1, 5)` on deleted record 5 set bit 5 in gang 1's enemy mask, and the next
  `GangCreate` took record 5 and was gang 1's enemy.
- **never made since boot**: its `+0x30` is 0, the gang named gets bit 0 (gang 0's) instead of the record's, and a
  gang made there later inherits nothing. The 0 is inferred from the table lying in `.bss` (`0x005e6e30`), which the
  runtime start clears; confirmed (runtime), run r-20261127-gang-records: the 25 records never made read 0 at `+0x30`,
  and `GangMakeEnemies(1, 9)` on never-made record 9 set bit 0 in gang 1's enemy mask (and cleared its friend bit 0),
  not bit 9.

## In Coney

The port is `repo:src/ai/gangs.h`: `Gangs::create`, `Gangs::addMember`, `Gang::leader` (both leader functions),
`Gangs::setHostileToKind`, `Gangs::makeEnemies` and `Gangs::makeFriends`. A level wires the wanted test and the forced
crime level to the crime reports (`GangLookups::wanted`, `GangLookups::crimeLevelForced`, in
`repo:src/gamemodes/gameplay_mode.cpp`). The member list keeps the 16 slots' order (`Gang::members`).

Deliberate differences and stand-ins:

- The music track names, the spot-line flag `+0xd2` and the bytes `+0xd0`, `+0xd1` and `+0xda` are not on Coney's gang
  record ([the open question](#q-unkept-fields)); the spawners' reset is `repo:src/ai/spawners.h`'s work (#645).
- With every record in use, `Gangs::create` makes no gang and returns −1; the original goes on with record −1.
- Any player joining becomes the leader, where the original makes only a war chief one (Coney keeps no war chief
  flag).
- Leaving a gang empties the brain's enemy list; the hostile counts the original lowers with it
  (`Brain_UncountAllEnemies`, `0x0028ef48`) are read from the lists in Coney.
- The attack weights are not loaded again on joining ([the open question](#q-attack-weights)).
- Dead is out of health and cuffed the script's arrested state ([the open question](#q-state-flags)).
- `Gangs::makeEnemies` and `Gangs::makeFriends` write the stored id as the original does (`Gang::m_storedId`, set by
  `Gangs::create`, 0 until then). Coney's gangs last a level, so a record made in an earlier level and not in this one
  reads 0 here and its index in the original; a marked stand-in at the code ([the open question](#q-stored-id)).
- An id outside 0 to 31 does nothing in `Gangs::makeEnemies` and `Gangs::makeFriends`; the original reads past its
  records.

## Open questions

### Dead and cuffed in Coney's leader pick {#q-state-flags}

Coney keeps no human state flags, so its leader pick reads dead as out of health (as `Gang::standing()` does) and
cuffed as the script's arrested state (as the gang's bounds do). Open: whether a human can be out of health and not
in the dead state for an update or more during the death chain, which would make Coney's pick run an update early.
How to answer: in PCSX2, kill a gang's leader that is not a player (`HuKill`, or a fight) and read his health
(`+0x144`) and his state flags (`*(human+0xd4)`, flag `0x100000000`) every frame until the flag is set; any frame with
0 health and no flag is an update where the two differ.

### The attack weights on joining {#q-attack-weights}

`Brain_LoadAttackWeights` (`0x0028aa18`) copies the 45 attack weights of the human's class (`+0x11e`) to brain
`+0x298`, or class 5's when the brain type is not the class's and human `+0x19d` is set. Coney's join keeps the
weights the brain was made with, or a script set since (`BrSetAttackWeight`). Open: whether a script sets weights
before a `GangAddMember` that the original then overwrites, and what human `+0x19d` is.

### The war chief on joining {#q-war-chief}

`Gang_AddMember` makes a joining player the leader only when he is war chief (human `+0x3ac` = 1, set by
`HuSetWarChief`). Coney keeps no war chief flag and reads player 1 as the war chief, so any player joining leads.
Open: whether a story joins a player who is not war chief to a gang (a second player in co-op), where the two differ.
How to answer: search the decompiled scripts for `GangAddMember` calls whose human is a player (`player`, `player2`,
`HuGetPlayer`) and check whether `HuSetWarChief` is called on that human first; in PCSX2, read human `+0x3ac` of player
2 after `F.Player2Join` (level99) joins him.

### Fields Coney does not keep {#q-unkept-fields}

`Gang_Create` stores the kind's three music track names and sets the spot-line flag `+0xd2`, and clears the record
bytes `+0xd0`, `+0xd1` and `+0xda`; `Gang_MakeNeutralWithType` clears the members' brain byte `+0x290` when the kind
is 0; `Gang_AddMember` sends event `0x16` with the member count before plus one in its second word. Coney keeps none of
them. Open: who reads each; a reader found means porting the field. How to answer: list the references to each offset
in Ghidra (a search of the instructions for the offset on a gang or brain pointer) and to the music names' reader
(`CfgGangMusic`'s table), and set a PCSX2 read watch on each field of a gang in use.

### Ids and records out of range {#q-out-of-range}

`Gang_MakeEnemies` and `Gang_MakeFriends` refuse only id −1 and index any other id, and `Gang_Create` goes on with
record −1 when every record is in use; past the 32 records both write outside the table. Coney does nothing there
instead. Open: whether any script passes such an id or makes a 33rd gang (no level seen so far does).

### Stored ids from an earlier level {#q-stored-id}

A record's stored id `+0x30` lasts from its first `GangCreate` until the game ends, so in a later level a record made
in an earlier one holds its index even before this level makes it; Coney's gangs last a level, so such a record reads
0 until made. `GangMakeEnemies` or `GangMakeFriends` naming a record the level has not made is the only place they
differ. Open: whether any script names a gang id before that level's `GangCreate` of it. How to answer: search the
decompiled scripts for `GangMakeEnemies`, `GangMakeFriends` and `GangSetEnemy` whose ids are numbers or variables not
set by a `GangCreate` earlier in the same level's set-up.
