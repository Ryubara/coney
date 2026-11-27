---
kind: topic
system: characters
owns:
  functions: [
    "0x00229c40", "0x0022a2a8", "0x0022a770", "0x00239b80", "0x00356d00", "0x00385bb8", "0x00419b10", "0x00419c68",
    "0x0041ce98",
  ]
---
# Characters: players and level starts

Verified against: `SLUS_212.15` (NTSC-U); runtime claims cite run records.

## Summary

The player is the human whose controller is a pad: `Human_MakePlayer` joins the game state's list of at most
two players, binds a pad and a HUD panel and gives the human brain type 0, and `Human_ReleasePlayer` undoes it.
A later `HuCreate` with a player argument, or a script's hand-over, moves the pad to another member of the
gang. Where a level puts player 1 is read from the level's scripts: the start list is in
[Level starts](../references/level-starts.md). The human object and its record are on
[Characters (humans)](characters/index.md); creation is [there](characters/index.md#creation).

## Map {#original-structure}

The functions of the player and of a level's start:

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x00229c40` | `Human_MakePlayer` | joins the player list, takes a pad and a HUD panel, brain type 0 ([Players](#players)) | confirmed (code) |
| `0x0022a2a8` | `Human_ReleasePlayer` | the reverse: frees the pad and panel, leaves the player list, AI again | confirmed (code) |
| `0x00419b10` / `0x00419c68` | `PlayerList_Add` / `PlayerList_Remove` | the game state's list of at most two players | confirmed (code) |
| `0x0022a770` | `Gang_PickNextPlayer` | the gang member a player hand-over chooses | confirmed (code) |

## Data

The player's fields (`+0x1b0` player slot, `+0x1b1` hand-over priority) are in [the human
object](characters/index.md#the-human-object).

## Behaviour

### Players: making one and giving one back {#players}

Confirmed (code) at the cited addresses unless a line says otherwise.

A player is a human in the game state's **player list**, confirmed (code) at `0x00419b10` and `0x00419c68`:
`W_GameState + 0x224` (s16) counts the players, `+0x228` holds their handles (two at most), and each player's slot
number is kept in the human at `+0x1b0`. Adding a player (`0x00419b10`) does nothing when two are already listed;
with no slot asked for it appends, otherwise it shifts the later players up. Removing one (`0x00419c68`) sets that
human's `+0x1b0` to -1, empties the entry and **shifts the later players down**, rewriting their `+0x1b0`, so player 2
becomes player 1 when player 1 goes. What the engine calls "the player" (the rings, Warrior commands, the
`HuChangePlayerGang` test) is slot 0, the handle at `+0x228`.

**`Human_MakePlayer(human, pad, panel, slot)`** (`0x00229c40`; `HuCreate` passes -1 for all three), confirmed (code):

1. **Pick a pad.** A given pad is used when that pad record ([Front end](frontend.md#pad-record)) is connected
   (`+0x4c`) and has no owner (`+0x42` = -1). Otherwise: the pad the front end assigned to the slot this human will
   take (byte `+0x19` of one of the two profiles at `0x00600840` / `0x00600844`, one per player slot), else the first
   of the 8 pad records that is connected and unowned.
2. **No pad while someone is already player 1: give up.** When no pad was found and the list's slot 0 holds a human,
   the function **returns 0 having changed nothing**: the human stays an AI. With an empty list it goes on without a
   pad.
3. Clear every current player's brain enemy list; **add the human to the player list** (`+0x1b0` = its slot).
4. **HUD panel**: attach the first free panel of the two (or the one asked for) through `HUD_AttachPlayer`
   ([HUD](hud.md#the-player-panel)); a panel still attached to another human is not free. The panel index goes to
   `+0x380`; the game state's per-panel record (`+0x168 + panel × 0x5c`) is reset.
5. **Pad ownership**: the pad record's `+0x42` = the player slot; the human's per-player record `+0x19` = the pad.
6. **Brain**: goals and actions cleared, type **0** (player, [AI](ai/index.md#types)).
7. **War chief**: `+0x3ac` = 1 unless another listed player is already chief (with `W_GameState + 0x158` set, only
   one of the same gang kind counts); a chief becomes its gang's leader (gang `+0x44`).
8. **Difficulty scaling**: health (record `+0x144`, `+0x146`) times a percentage, power maxima reloaded, attack damage
   times another percentage (bytes `+5` and `+6` of the record `0x00222b58` returns, confirmed (code);
   `Human_ReleasePlayer` divides back).
9. The war chief's default Warrior command 0 to its gang (`WarriorCommand_Dispatch`, `0x0041c4e0`, confirmed (code),
   [AI](ai-gangs.md#warrior-commands)), the gang's cached player data (`0x00166708`), physics body flag `0x400` and a
   wider body, the human flags of the table above. Returns 1.

`Human_MakePlayer` does **not** touch the camera: the follow camera keeps the human it was made with, and the hand-over
below puts the new player on the camera target list.

**`Human_ReleasePlayer(human, stats, releaseGrab)`** (`0x0022a2a8`), the reverse, confirmed (code): (with
`releaseGrab`) end a grab between it and another player; free its pad (`+0x42` = -1); clear the players' enemy lists;
brain type back to its class's (as `Human_Init` chose it), goals and actions cleared; the per-panel record reset;
(with `stats`) the stats side; links between it and the players cleared (`0x00226e60`, `0x00226f70`, not traced);
**remove it from the player list** (the others shift down); hide and detach its HUD panel, `+0x380` = -1; per-player
record `+0x19` = -1 and `+0x1b` = 0 (AI-controlled); `Gang_AddMember` with its own gang; difficulty scaling undone,
the AI flags back. The human stays in the world as an AI member of its gang.

**`HuChangePlayerGang(gang)`** (`Players_ChangeGang`, `0x00239b80`), confirmed (code):

1. When player 1 already belongs to `gang`, return at once.
2. Every listed player of **player 1's gang** is released (`Human_ReleasePlayer(h, 0, 1)`), its pad remembered, and
   taken off the camera target list ([Camera](camera.md)). Their count is the number of players to hand over (at
   least 1).
3. A listed player already in `gang` is released and made a player again with a remembered pad, and put on the
   camera target list.
4. For each player still owed: `Gang_PickNextPlayer(gang)` (`0x0022a770`, confirmed (code)) takes the gang's member that
   is not a player with the **lowest non-zero `+0x1b1`**, else the first such member (with no member at all, outside
   level 99 and with `+0x158` clear, a non-player human of a kind-0 gang); `Human_MakePlayer(h, -1, -1, -1)` on it; on
   success it joins the camera target list.

`Gang_PickNextPlayer` is what makes `+0x1b1` matter: in the Warriors' gang Rembrandt (1) is chosen before Ash (2), and
members created with 0 come last.

#### The level 99 checkpoint 2 hand-over {#level99-handover}

`P2.SetupLesson1` creates a new Warriors gang, a new Rembrandt (`HuCreate(..., 1, gang)`) and a new Ash
(`HuCreate(..., 2, gang)`), calls `HuChangePlayerGang` on the new gang, then `GangBrFlush` and `GangDelete` on the old
`Warriors2` gang (gang 0) that still holds the old Rembrandt and Ash. The script order is inferred from the
disassembly; each step is confirmed (code) as above:

1. **The new Rembrandt's `HuCreate`**: the old Rembrandt is still player 1 and owns pad 0. With one controller there is
   no other connected unowned pad, so `Human_MakePlayer` **gives up at step 2**: the new Rembrandt is an AI Warrior
   with priority 1, and nothing about the player changes. (With a second controller in port 2 it would briefly become
   player 2 on that pad; the next step makes the outcome the same.)
2. **The new Ash** (player argument 2): never a player; an AI Warrior with priority 2.
3. **`HuChangePlayerGang(new gang)`**: the old Rembrandt (gang 0) is released and taken off the camera targets; the
   list is now empty; `Gang_PickNextPlayer` picks the new Rembrandt (priority 1), which becomes player 1 on pad 0 (the
   front end's pad for slot 0), takes HUD panel 0 (just freed) and is added to the camera targets.
4. **`GangBrFlush(0)`** clears the old pair's goals and actions.
5. **`GangDelete(0)`** destroys the old Rembrandt and the old Ash at once ([Destroying a
   human](characters/index.md#destroy), [AI: deleting a gang](ai-gangs.md#gang-delete)).

So the player moves with `HuChangePlayerGang`, not with `HuCreate`: an implementation that binds the player on any
`HuCreate(..., 1, ...)` while a player exists diverges here, and one that ignores `HuChangePlayerGang` leaves the pad on
a human that is then deleted.

### Where a level puts the player {#level-starts}

**No data file holds a player start.** The `.lev` file's 18 chunks ([Level loading](level-loading.md#the-level-file))
and the level record ([the record](level-loading.md#the-level-record)) carry none, and nothing in `InitLevel` places
a player (confirmed (code) for those readers). The **level script** creates player 1 with `HuCreate` while
`InitLevel` runs it, before the preload ([Level loading](level-loading.md#initlevel)); the values are in the
[Level starts](../references/level-starts.md) reference list, one entry per checkpoint (inferred from the
disassembly of the 29 level scripts that create a player; the list's values are read from the disc).

**The checkpoint** is `W_GameState + 0x33a` (16 bits), confirmed (code):

- the game state's constructor (`0x00418588`) sets it to **1**;
- `SetCheckPoint(n)` (`0x0037b760` → `0x0041abc0` → `0x0041ce98`) stores `n` and tells the inventory, the stats and
  the world objects (`0x0041e0b8`, `0x00422c60`, `0x00397e88`); scripts call it before `MenuLoadLevel` to enter a level
  at a checkpoint, and during a mission to mark progress, so a retry restarts there;
- `0x0041cef0(gameState, 1)` puts it back to 1 without the bookkeeping: on the paths that restart a mission or
  leave it (the failure menu `0x00155408`, `0x00155648`, `0x001557f8`, the mission-complete mode's `Update`
  `0x0015d160`, and `0x00160d38`, which also selects level index 0, the front end);
- `GetCheckPoint` (`0x0037b798` → `0x0041abe8`) reads it. `InitLevel` uses it as the section: the pack
  `<level>_<checkpoint>.pak` and the intro movie only at 1 ([Level loading](level-loading.md#the-level-record)).

So a level loaded without a `SetCheckPoint` starts at checkpoint 1, or at whatever the last one left (inferred).

**The pattern of a story level's script** (inferred from the disassembly; 28 story levels follow it):

1. The main chunk defines the helpers, the flags (`AddFlagsBoxesPaths`), the objects, then runs `Main`.
2. `Main` reads `GetCheckPoint()` into a global (`checkpoint`, `CHAPTER` or `mState`; `level84` turns 0 into 1) and
   indexes a list with it. The list holds either the **creator** functions themselves (`PlayerGang = {AddWarriors1,
   AddWarriors2, ...}`, sometimes a local) or rows `{creator, start function, chapter script}` (`tMission`, as in
   `level99`, [Scripts](scripting.md#level99)).
3. The creator makes the Warriors' gang (`GangCreate(0, "Warriors...", 0, 0)`) and its humans, **player 1** among
   them: `HuCreate(name, type, {x, y, z}, heading, model, 1, gang)`, the position and heading literal numbers. It
   returns the table of humans and the gang; `Main` then sets `player = Warriors.<name>` (the name differs by
   checkpoint: `level3` plays Rembrandt for checkpoints 1-2 and Snow for 3-5).
4. `preLoadFile(chapter script, start function)` loads the checkpoint's own script; when it arrives the game runs it and
   calls the start function by name (`0x00356d00`, confirmed (code)). A start function may move the player again: in
   five checkpoints it, or a function it calls, teleports the player (`Teleport` or `TeleportToFlag`, noted in the
   list).

`Teleport(object, {x, y, z}, heading)` (`0x00385bb8`) sets the transform; a heading of −1 keeps the rotation. Its
`TeleportToFlag(object, flag, heading)` sibling (`0x00385db0`) takes the flag's position and, with −1, the flag's
heading (`0x00416258`), and for a human also calls its vtable slot `+0x14c`. Confirmed (code). Neither places the
object on the ground as `HuCreate` does (above), but a human not in a scene (`+0x280` = −1) is lowered onto whatever a
short ray (0.5) straight down hits (`0x0023d440`; confirmed (code)). A human in a grab, mount or mugging is first let
go, its partner playing a reaction clip ([Combat](combat-grabs.md#pair-break)).

**Interiors are below the street.** Starts with `z` near −195 to −215 (`level5` checkpoint 2, `level11` 1 and 2,
`level20` 1 and 3, the hub ...) are rooms placed about 200 m under the city in the same world (inferred from the values
and the hub's clubhouse at `z` −194.3).

**The hub** (`level95`, [Scripts](scripting.md#the-hub)): `AddWarchief` creates the player at the flag
`fWchiefStart_1` (−188.6, 95, −194.3), heading 222, as the type the chapter script's `WarchiefTable` names (Cleon,
Rembrandt, Ajax, Cochise, Cowboy or Swan by chapter). `StartLevel` then either opens the quick map (when unlockable
`(6, 3)` is unlocked, or `LoadLight` is set) or makes the **door walk**: `TeleportToFlag(player, fWchiefStart_<n>)`
and a walk to `fWchiefEnd_<n>`, where `n` = `WCLoc`, `random(1, 5)` from the main chunk (1 to 5 inclusive, from the
game's table generator), or 5 while unlockable `(6, 4)` (the tutorial) is still locked, in any chapter. The five
start flags: 1 (−188.6, 95, −194.3) 89°; 2 (−188.6, 102.7, −197.5) 89°; 3 (−163.7, 80.7, −197.5) 358°; 4 (−174.4,
80.5, −194.3) 358°; 5 (−185.2, 112.7, −193.7) 182°. Inferred from the disassembly; how flags work, the generator
and the order of `StartLevel`'s steps are on [World flags](flags.md#player-starts).

**A Rumble arena** (`level101`-`level137`): the level script runs `doFile("level" .. Level .. "_" ..
RumbleInfo[Rumble.gameType] .. "_init")`, which adds the mode's flags, among them the list `fP1` (player 1's gang)
and `fP2`. `AddRumbleGang1` creates `P11` with `HuCreate("P11", Rumble.gang1[1], FlagPos(fP1[1]), 270, nil, 1,
gang, true)` and teleports it to `fP1[1]` with heading −1, so it stands on the first flag facing the flag's heading
(inferred from the disassembly). The gang, and so the type, comes from the Rumble menu (`GetRumbleModeData`); the
steps are on [World flags](flags.md#player-starts).

**No player**: `level100` (the front end), `level1` (its script only calls `MenuLoadLevel("menu")`), and the levels
without a `.lev` file.

## In Coney {#coneys-implementation}

- `src/human/player.*` is player 1 (the character at scale 0.97, the human, the follow camera; L2 held sprints,
  triangle pressed acts): it writes the pad into its human's record and steps the characters, and keeps the snapshot
  drawing reads: the previous and current feet, heading, lean, pose (each bone slerped) and camera, interpolated for a
  renderer that draws between steps.
- **Level starts**: the start comes from the level's own script at run time: its `HuCreate` for player 1 at the
  checkpoint (`GetCheckPoint`), kept by Coney's `HuCreate` binding ([Level
  loading](level-loading.md#coneys-implementation)); the story's way in and `--play-level NAME [--checkpoint N]` both
  run it. A level that places player 1 at a flag (the hub, the Rumble arenas) starts him on the flag ([World
  flags](flags.md#coneys-implementation)). The character's lights (ambient 0.45, one directional 0.7) stand in for the
  LightManager, and he is drawn between the level's two worlds.
- **Handing the player over** (`ScriptedBrains::changePlayerGang`, `PlayLevelSimulation::takePlace`, from
  [Players](#players)): a later `HuCreate` with player argument 1 makes an AI human (the argument is kept as its
  priority), and `HuChangePlayerGang` picks the gang's member as `Gang_PickNextPlayer` does. **Coney's stand-in**:
  Coney has one player human, so instead of making the chosen human the player and leaving the old one in the world
  until its gang is deleted, the player's human takes the chosen one's place (spawned where it stands, drawn as its
  model, named by its handle, in its gang) and the chosen human leaves the world; the old handle names no one. The
  scripts' record of player 1 (`CreatedHumans::player(1)`, which the play mode's thefts, pick-ups and muggings, the
  crew's chief and the teleports read) names the chosen human from then on (`CreatedHumans::setPlayer`). The
  end state after level 99's `GangDelete(0)` ([the hand-over](#level99-handover)) is the original's;
  `tests/platform/disc_level99_handover_test.cpp` plays checkpoint 1 into 2 and steals the first stereo as the new
  player 1 (before, the theft named the old handle, `HuIsAPlayer` said no and the lesson stalled).
  `GangDelete` deletes the gang's members at once ([AI](ai/index.md#in-coney)); player 1, with no other human to go to,
  is only taken out of the gang. `tests/platform/disc_level99_street_test.cpp` plays the end of the combat course into
  the street with the disc.

### Notes for implementers {#notes-for-implementers}

- **Make the player first.** For `level99`, checkpoint 1: one human named `Rembrandt`, model `warr_re_cv` (found
  through the Character List), at `(-284.4, 120.4, 0.3)` heading 0°, snapped to the ground with a 2.5 m ray from 1 m
  above; pad 0; the follow camera targeting it ([Camera](camera.md)). Any other level: its entry for the checkpoint
  in [Level starts](../references/level-starts.md) (checkpoint 1 by default, the first mode's flag in a Rumble arena,
  `fWchiefStart_1` in the hub), with that entry's character type.

## Open questions

- **Level starts at runtime**: the list is read from the scripts; a runtime check would confirm a few. For each of
  `level2` checkpoint 1, `level95` chapter 1 and `level102` brawl, read player 1's transform (the table at
  `0x00714b00`, index `+0x92` of the human whose `+0x1b0` is 1) on the first frame of play, and again after the
  checkpoint script's start function has run (the hub's door walk, the arena's teleport).
- **Start functions that move the player later**: the list notes only teleports made directly by a checkpoint's start
  function (or one function down). Scene callbacks and chapter steps teleport the player too (99 `Teleport` /
  `TeleportToFlag` calls on `player` in the story scripts in all); which of them run before the first frame the player
  controls is not traced.
- **Four Rumble arenas without a known mode script**: their `level<N>_<mode>_init.lua` names are not recovered, so
  they have no entry ([Levels](../references/levels.md) lists the modes found per arena).

## How this was checked

Code claims were read in Ghidra in `SLUS_212.15`.
