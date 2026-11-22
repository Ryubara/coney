---
kind: feature
---
# Story progress

Verified against: `SLUS_212.15` (NTSC-U) and the retail disc's compiled Lua chunks, read in their decompiles.

## Summary

How the original decides which story level comes next, and every place that reads the story's order. There is no
story table in the executable: the order lives in `global.lua` (a table from the last story mission complete to the
next level, and a list to find that mission), in the hub's `fRunMission` (the next mission per hub chapter), in fixed
comparisons with the hub's chapter number, and in the level records' mission byte and label, which order the menus.
Progress itself is only the profile's completion records, one per level; no checkpoint is kept. This page is the map
that [Coney Studio: mission mods](../guides/studio/mission-mods.md#story-graph) builds the story graph from. The
scripts' place in the engine is [Scripts](scripting.md); the levels are listed in [Levels](../references/levels.md).

## Rules {#rules}

### What counts as progress {#progress}

Confirmed (code) at the cited script functions and addresses unless a line says otherwise.

- **A level is complete** when its completion record `(level, 0, 0)` is unlocked: `UM_IsLevelComplete(level)`
  (`Script_UMIsLevelComplete` `0x004238a8`, which returns false while no records exist) asks
  `Unlocks_IsLevelComplete` (`0x004241d8`). The records live in the profile's 640 locked bits
  ([Profiles: the record](save.md#record)).
- **The latest story mission complete** decides everything else. Nothing records the level last played or a
  checkpoint inside a mission, so a mission that was quit, or a session that ended mid-mission, starts again from its
  checkpoint 1 (inferred from what the profile holds: [Profiles: the record](save.md#record)).
- **The story's order** is 18 missions and a hub (`level95`) entered at checkpoints 1-12 between them
  ([The order](#order)). The flashbacks and the Armies of the Night stages hang off the hub and never move the story
  on ([The hub's other doors](#hub-doors)).

### The order {#order}

Confirmed (code) at `global.lua:f61` and the hub's `fRunMission` (`level95.lua:f42` reads it). Disc check: the mission
numbers equal the level records' mission byte `+0x0c` (1-18 in [Levels](../references/levels.md)).

| Mission | Level | Loaded by |
| --- | --- | --- |
| 1 | `level99` | a new game: `runNextMission` with nothing complete |
| 2 | `level80` | `runNextMission` after 99 |
| 3 | `level87` | `runNextMission` after 80 |
| hub | `level95` checkpoint 1 | `runNextMission` after 87 |
| 4 | `level34` | the hub, checkpoint 1 |
| hub | `level95` checkpoint 2 | `runNextMission` after 34 |
| 5 | `level2` | the hub, checkpoint 2 |
| hub | `level95` checkpoint 3 | `runNextMission` after 2 |
| 6 | `level3` | the hub, checkpoint 3 |
| hub | `level95` checkpoint 4 | `runNextMission` after 3 |
| 7 | `level5` | the hub, checkpoint 4 |
| hub | `level95` checkpoint 5 | `runNextMission` after 5 |
| 8 | `level81` | the hub, checkpoint 5 |
| hub | `level95` checkpoint 6 | `runNextMission` after 81 |
| 9 | `level86` | the hub, checkpoint 6 |
| 10 | `level93` | `runNextMission` after 86 |
| hub | `level95` checkpoint 7 | `runNextMission` after 93 |
| 11 | `level31` | the hub, checkpoint 7 |
| hub | `level95` checkpoint 8 | `runNextMission` after 31 |
| 12 | `level14` | the hub, checkpoint 8 |
| hub | `level95` checkpoint 9 | `runNextMission` after 14 |
| 13 | `level9` | the hub, checkpoint 9 |
| hub | `level95` checkpoint 10 | `runNextMission` after 9 |
| 14 | `level51` | the hub, checkpoint 10 |
| 15 | `level52` | `runNextMission` after 51 |
| 16 | `level54` | `runNextMission` after 52 |
| 17 | `level55` | `runNextMission` after 54 |
| 18 | `level84` | `runNextMission` after 55 |
| hub | `level95` checkpoint 11 | `runNextMission` after 84; the hub offers no next mission |
| hub | `level95` checkpoint 12 | `runNextMission` after 64, the last Armies stage |

Every level is entered at checkpoint 1 except the hub, whose checkpoint is its chapter. Each pair of story missions
is joined either directly (`runNextMission` loads the next mission: 99 → 80 → 87, 86 → 93, 51 → 52 → 54 → 55 → 84) or
through a hub chapter (`runNextMission` loads the hub at a checkpoint, and that chapter's last action loads the next
mission).

## Map {#map}

| Behaviour | Function | Section |
| --- | --- | --- |
| Choose and load the next level | `runNextMission` (`global.lua:f61`) | [The four functions](#global-functions) |
| Find the latest story mission complete | `findLastMission` (`global.lua:f62`) | [The four functions](#global-functions) |
| Unlock a finished level's records | `MissionCompleteUnlocks` (`global.lua:f60`) | [The four functions](#global-functions) |
| End a mission: unlocks, bank, next level | `UnlockAndLoad` (`global.lua:f59`); called by mode 0xb's `Enter` (`0x0015cf70`) | [Who calls them](#callers) |
| A hub chapter and its next mission | `Main`, `story.RunMissionAction`, `story.LoadLevel` (`level95.lua:f229`, `f42`, `f55`) | [The hub](#hub) |
| A chapter's configuration: actions, cast, scenes, extras | `clubhouse.Setup` (`level95.lua:f220`), `story.SetupSubmission` (`level95.lua:f54`) | [The hub](#hub) |
| Flashback trophies | `flashback.Setup`, `flashback.PlayMission` (`level95.lua:f73`, `f76`) | [The hub's other doors](#hub-doors) |
| The mission menu's list | `MissionSelect_BuildAreas` (`0x001bcc48`), `MissionSelect_IsMissionListed` (`0x001bc100`) | [The mission menu](#replay) |
| Every reader of the order | | [Every reader](#readers) |

## Behaviour

### The four functions {#global-functions}

Confirmed (code) at the cited script functions, read in the decompiled `global.lua`, unless a line says otherwise.

1. **`runNextMission(arg)`** (`global.lua:f61`) builds a table from a level to the next `{checkpoint, level}` with 20
   rows: 0 → 99; 99 → 80; 80 → 87; 87 → 95 (checkpoint 1); 34 → 95 (2); 2 → 95 (3); 3 → 95 (4); 5 → 95 (5);
   81 → 95 (6); 86 → 93; 93 → 95 (7); 31 → 95 (8); 14 → 95 (9); 9 → 95 (10); 51 → 52; 52 → 54; 54 → 55; 55 → 84;
   84 → 95 (11); 64 → 95 (12); the rows without a hub checkpoint give checkpoint 1. When the global `LastLevel` is 0
   or nil it sets it from `findLastMission()`. With a row for `LastLevel` it calls `SetCheckPoint`,
   `SoundStopMusicTrack()` and `MenuLoadLevel("level" .. level)`, and when `arg` is 1 also
   `HUDLaunchMissionComplete(4)`. With no row it calls a name no binding has (`HUDLaunchMisssionComplete`, three `s`),
   which fails silently, and nothing loads; that the name is a slip is inferred from its spelling.
2. **`findLastMission()`** (`global.lua:f62`) returns the first of 64, 84, 55, 54, 52, 51, 9, 14, 31, 93, 86, 81, 5,
   3, 2, 34, 87, 80, 99 that `UM_IsLevelComplete` says is complete, else 0: the table's keys but 0, latest first.
3. **`MissionCompleteUnlocks()`** (`global.lua:f60`) unlocks `(Level, 0, 0)`. When `player` is a player it adds the
   scores of `player` and `player2` (each only while alive and a player), divides by `PointsPerRank` (1000, from the
   main chunk), truncates, and unlocks `(Level, 0, k)` for each `k` from that rank down to 1. It ends with
   `LastLevel = findLastMission()`.
4. **`UnlockAndLoad()`** (`global.lua:f59`) calls `MissionCompleteUnlocks()`, banks each live player's assets below
   level 100 (`LiquidizeAssets(p, true, true, true, false)` for `player` and `player2`), then calls
   `runNextMission(1)`.

`global.lua`'s main chunk sets `LastLevel = 0` and runs at every level load, so `runNextMission` starts from
`findLastMission()` unless `MissionCompleteUnlocks` ran in the same level. A new game has nothing complete,
`findLastMission()` gives 0, and the row for 0 loads `level99` at checkpoint 1.

Because every completion ends with `LastLevel = findLastMission()`, the row used after any completion is the latest
story mission's, not the level just played: a finished flashback, a story mission replayed from the mission menu and a
Rumble arena left for the clubhouse all return to the hub at the story's current chapter. Once `level64` is complete it
is the first `findLastMission` finds, so every later completion leads to hub checkpoint 12 (read in the code, not
observed).

### Who calls them {#callers}

Confirmed (code) at the cited script functions and addresses.

| Caller | Call |
| --- | --- |
| The front end's story start: `Menu.startGame` (`level100.lua:f7`); `DlgGreet.startGame`, `DlgCreate.loadLevel`, `DlgContinue2.continue` after `SSMC_loadGame`, and `DlgLight.onSelect` (`level100main.lua:f57`, `f119`, `f159`, `f183`) | `runNextMission(1)` |
| Every Rumble arena's `ExitToClubhouse` (`level101.lua:f16`, the same function in each arena's script) | `runNextMission(1)` |
| The mission-complete mode's `Enter` (`0x0015cf70`, named `Gm_InGame_Enter` in Ghidra; mode 0xb, pushed by `MissionComplete_Launch` `0x0015d420`) calls the Lua name at `0x0054f528`, loaded at `0x0015d09c` | `UnlockAndLoad()` |
| The quit choices of the pause menu (`PauseMenu_Toggle` `0x00154f28`), the mission-failed menu (`MissionFailed_Toggle` `0x00155408`), the Rumble result (`RumbleResult_Toggle` `0x00155648`) and the Armies game over (`ANGameOver_Toggle` `0x001557f8`) call the Lua name at `0x0054e798` with one number | `runNextMission(0)` |

No caller passes a level: the next level always comes from the completion records.

### The hub {#hub}

Confirmed (code) at the cited script functions unless a line says otherwise.

- **The chapter.** `Main` (`level95.lua:f229`) reads `GetCheckPoint()` into `checkpoint` and loads
  `level95_chapter<checkpoint>` (12 scripts) with the callback `RunLevel` (`level95.lua:f230`). The chapter sets the
  crew, the Warchief, the radio and its list of mission actions, the last of which is `MA_LOADLEVEL`.
- **The next mission.** `story.RunMissionAction` (`level95.lua:f42`) handles `MA_LOADLEVEL` by loading
  `fRunMission[checkpoint].level` when the row has one, at once or after 1,000 ms. `fRunMission`, a table of the
  `level95.lua` main chunk, has one row per chapter: the flag where the mission's giver stands (Vermin's, the radio's,
  Cleon's or Ajax's) and the level. `story.LoadLevel` (`level95.lua:f55`) stops the music, banks a live player's
  assets, sets checkpoint 1, starts a save (`SSMC_StartSaveSequence()`) and calls `MenuLoadLevel`.

  | Hub checkpoint | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 |
  | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
  | Next mission | 34 | 2 | 3 | 5 | 81 | 86 | 31 | 14 | 9 | 51 | none | none |

- **The chapter's configuration.** The chapter file sets its crew, radio, chatter and idle animations as globals
  when it runs, and leaves the rest to functions and tables that the hub reads later, from `RunLevel`:
  `clubhouse.Setup` (`level95.lua:f220`) makes scene-extra humans from the chapter's `SceneExtrasTable` when it is
  set (`AddSceneExtras`), then calls `CfgMissionActions` (the chapter's actions), `CfgMissionHumans` (its scenes'
  cast) and `CfgSceneTables` (its scenes), each only when the chapter defines it. `CfgSceneTables` is called at once
  in a release build and scheduled 100 ms later otherwise. `story.SetupSubmission` (`level95.lua:f54`) calls
  `CfgMissionTable`, when it is set, and only chapter 3 defines one: its side mission. `SceneExtrasTable` is set by
  chapters 2, 5, 6 and 9.
- **Chapter 11's actions** (`level95_chapter11.lua:f0`) are only two: a red circle at `fRunMission[checkpoint].flag`
  and the mission load, which loads nothing there since chapter 11's row has no level. Coney's default chapter for
  mods copies that shape ([Hub chapters](../guides/studio/hub-chapters.md)).
- **The "new" camera.** `dirty.Setup` (`level95.lua:f225`), when the profile has new type-0 unlocks and the checkpoint
  is below 11, points a camera at the giver's flag from the same row.
- **The chapter number as story time.** `RunLevel` and `StartLevel` (`level95.lua:f230`, `f231`), the crew's chatter
  (`level95.lua:f195`, `f196`, `f198`) and the Coney streets (`level95_coney.lua:f48`, `f101`) compare `checkpoint`
  with fixed numbers (`> 2`, `> 3`, `< 10`, `== 1`, `> 6`, `> 10`, `< 12`, odd or even) to choose the clubhouse's burn
  state, the chatter, the collectables that can be taken, the street spawners, the cops and the radio. A chapter is
  therefore a point in the story's time, not only an index into `fRunMission`.
- `level95_clubhouse.lua` defines the menu and flashback functions of `level95.lua` a second time, with the same logic.

### The hub's other doors {#hub-doors}

Confirmed (code) at the cited script functions unless a line says otherwise.

- **Flashbacks.** `FBMission` (the `level95.lua` main chunk) lists six trophies: `level82`, `level92`, `level83`,
  `level20`, `level11`, and `level60`, which needs the first five complete. `flashback.Setup` (`level95.lua:f73`)
  offers a trophy whose level has an unlocked record of type 7 or 9: with its requirement met and the level not yet
  complete it gets a marker and a handler; `level60`'s keeps its handler once complete and shows a refusal while its
  requirement is unmet. `flashback.PlayMission` (`level95.lua:f76`) runs an `MA_LOADFBLEVEL` action, which loads the
  level through `story.LoadLevel` after 1,000 ms. `flashback.SetupTrophies` (`level95.lua:f74`) opens zone 11 once
  `level84` is complete and removes the trophy objects whose type-5 records are still locked.
- **What opens each trophy.** The records `flashback.Setup` asks for are `global.lua`'s `Unlockables` rows of type 7
  and 9 (the main chunk's `CfgUnlockables` loads them): a story mission's completion opens a flashback's type-7 record
  (34 → 82, 2 → 92, 5 → 83, 81 → 20, 31 → 11), and `level84`'s opens `level60`'s type-9 record (then 60 → 61 up to
  63 → 64). So the sixth entry, whose flag is the arcade cabinet `fMenu[4]` rather than a trophy, appears only once the
  story is finished: with its five flashbacks complete it gets the `bonus` marker and the play handler, with one
  missing the marker and `flashback.NoAction`. In the original, a save with the 18 story missions complete at hub
  checkpoint 11 gave `UM_IsDataUnlocked(9, 60)` = 1; with the five flashbacks complete, triangle at the cabinet loaded
  `level60` at checkpoint 1, and with `level11` incomplete it loaded nothing (confirmed (runtime), run
  r-20261122-level95-armies-trophy).
- **Armies of the Night.** Each stage's last checkpoint script loads the next, `level60` to `level64`
  ([Armies of the Night](armies.md#stages)); `level64` complete makes the hub's chapter 12 the story's place from then
  on ([The four functions](#global-functions)).
- **The Rumble menu** (`menu.DoRumbleMenu`, `level95.lua:f64`) starts an arena, whose `ExitToClubhouse` returns through
  `runNextMission(1)` ([Who calls them](#callers)).

### The mission menu {#replay}

Confirmed (code) at the cited script functions and addresses.

`menu.MenuAction` (`level95.lua:f60`) at the menu flag `fMenu[1]` schedules `menu.DoSubwayMenu` (`level95.lua:f61`),
which plays the menu music and opens the native mission select with `HUDShowMissionSelect`, naming
`menu.EndMenuActionMission` for a cancel and `menu.BeginChosenLevel` for a choice ([Front end: mission
select](frontend.md#fn-missionselecthud)). The screen builds its list from the level records, not from any story
table:

- **Which missions.** `MissionSelect_BuildAreas` (`0x001bcc48`) walks the level records in index order and keeps those
  `MissionSelect_IsMissionListed` (`0x001bc100`) passes: levels 2-99 except 95 and 60-65, complete
  (`Unlocks_IsLevelComplete` `0x004241d8`). Each goes into its area, named by the text after `:` in the record's
  second name.
- **In which order.** The areas are sorted by the vertical map position (the second float) of their first mission,
  the larger value first (comparator `0x004e58f8`, through `LevelRecord_GetMapPos` `0x0041f540`, which reads the
  mission byte `+0x0c`), and each area's missions by their label, the text before `:`, read as a base-10 number, the
  smaller first (comparator `0x004e6410`, through `LevelRecord_GetLabel` `0x0041f428` and `0x00433a68`).
  Ghidra's names for the two sorts are the other way round from these roles: `MissionSort_*` sorts the areas,
  `AreaSort_*` the missions.
- **What it starts.** `menu.BeginChosenLevel(level, checkpoint)` (`level95.lua:f62`) banks both live players' assets,
  clears the script save data (`ClearSaveData()`), sets the chosen checkpoint, starts a save and calls
  `MenuLoadLevel`. The replay's completion goes through `UnlockAndLoad` like any other.

### Every reader of the order {#readers}

Confirmed (code) at the cited script functions and addresses.

Found from the decompiled scripts' index (every caller of the four functions, every `UM_IsLevelComplete`, every read
of the hub's `checkpoint`) and from the callers of the level records' mission byte and label in Ghidra:

| Reader | What it reads | What it decides |
| --- | --- | --- |
| `runNextMission` (`global.lua:f61`) | its own 20-row table | the next level and checkpoint |
| `findLastMission` (`global.lua:f62`) | its own list of 19 levels | the latest story mission complete |
| `story.RunMissionAction`, `dirty.Setup` (`level95.lua:f42`, `f225`) | `fRunMission`, by hub checkpoint | the chapter's next mission; whose spot the "new" camera shows |
| `Main` (`level95.lua:f229`) | the hub checkpoint | which chapter script runs |
| The fixed comparisons (`level95.lua:f195`, `f196`, `f198`, `f230`, `f231`; `level95_coney.lua:f48`, `f101`) | the hub checkpoint as a number | the clubhouse's state, chatter, collectables, spawners, cops, radio |
| `flashback.Setup` (`level95.lua:f73`) | completion of 82, 92, 83, 20, 11 | when `level60`'s trophy opens |
| `UM_IsLevelComplete(84)` in `global.lua`'s main chunk and `flashback.SetupTrophies` (`level95.lua:f74`) | the last story mission's completion | the menu music; trophy zone 11 |
| The mission byte `+0x0c` of the level records that `CfgLevels` (`config_preload3.lua`) sets through `CfgLevelName` | 1-26 | the statistics list's order (`GameStats_CollectLevels` `0x00215da0`, [Statistics](game-stats.md#list)); which of the 30 best records a level uses (`Stats_GetLevelBest` `0x00423050`, [Profiles](save.md#record)); a mission's map point (`LevelRecord_GetMapPos` `0x0041f540`) |
| The label of the same records (`LevelRecord_GetLabel` `0x0041f428`) | the mission's number as text | the mission menu's order within an area; the circled number on the mission menu, the statistics list (`GameStats_BuildMissionList` `0x002155a8`) and the Rumble soldier screen (`RMBuySoldiers_Show` `0x001e9f70`) |
| The range checks on the level's number: `GetLevel` (`global.lua:f64`, run by the main chunk), `UnlockAndLoad` (`global.lua:f59`), `SetupLevelInventory` (`global.lua:f150`), `DbgEnterCheat` and `DoCheat` (`global.lua:f46`, `f47`); natively `GameState_IsArmiesLevel` (`0x0041d110`), `MissionSelect_IsMissionListed` (`0x001bc100`) and `GameStats_IsLevelListed` (`0x00215ca0`) | the current level's number | `GetLevel` turns 0 and 300 or more into 1; below 100 the players' assets are banked at the end and the second player's inventory is set; the cheats act differently at 95, at 60-64 and at 100 or more; 60-69 is an Armies stage; 2-99 but 95 and 60-65 can be listed on the mission menu; 61-65 and 102-137 are left off the statistics |

A mission added to the story therefore has to appear in each of these, and a number given to it has to pass the range
checks of the last row as a story mission's does.

## In Coney {#coney}

Coney runs `global.lua` and the hub's scripts as the original does, with two of `global.lua`'s functions and the
hub's `fRunMission` rows answered from Coney's story graph (below). The callers are the original's:
`MissionCompleteMode` calls `UnlockAndLoad` (`repo:src/gamemodes/mission_complete_mode.h`), the pause, mission-failed,
Rumble result and Armies game-over menus call `runNextMission(0)` (`repo:src/gamemodes/pause_mode.h`,
`repo:src/gamemodes/rumble_result_mode.h`, `repo:src/gamemodes/armies_game_over_mode.h`), and the level start's
`UnlockAndLoad` path is `repo:src/gamemodes/level_start.h`. The mission menu's screen is missing: `HUDShowMissionSelect`
opens nothing yet ([#1071](issue:1071)). Coney's own story graph, which replaces the two Lua tables, is designed in
[Coney Studio: mission mods](../guides/studio/mission-mods.md#story-graph).

The graph's data is built: `story::StoryGraph` (`repo:src/core/story_graph.h`) holds the 31 steps of
[The order](#order) as level and chapter numbers, and gives `runNextMission`'s 20 rows, `findLastMission`'s 19 levels
and the 12 offers of `fRunMission` as views of that chain. The story session (`StartUpFlow`) holds the original's
chain; its hash, for a co-op session to compare, is the SHA-256 of a fixed text form (a version line, then each step's
id, level, checkpoint and side-step mark). `repo:tests/core/story_graph_test.cpp` checks each view against the rows
above and walks the story from an empty profile; `repo:tests/platform/disc_story_graph_test.cpp` runs the disc's own
`runNextMission` and `findLastMission` against the graph for each prefix of the story complete, and for `level64`
alone, and prints only how many of the 21 cases agree.

The scripts read the graph through Coney's natives ([#1300](issue:1300), `repo:src/scripting/story_natives.h`), as
[The natives that read it](../guides/studio/mission-mods.md#graph-natives) designs them. At every level entry, after
`global.lua`'s main chunk and before the level's own script, the session (`script::StoryHooks`, held by
`StartUpFlow`) sets a `findLastMission` and a `runNextMission` native over the Lua functions of those names, mods or
none; `UnlockAndLoad`, `MissionCompleteUnlocks` and every caller in [Who calls them](#callers) are unchanged and reach
them by name. `findLastMission` walks the chain latest first and asks the scripts' own `UM_IsLevelComplete` for an
original mission, the calls the Lua makes in the same order; `runNextMission` keeps the Lua's `LastLevel` rule and
calls `SetCheckPoint`, `SoundStopMusicTrack`, `MenuLoadLevel` and `HUDLaunchMissionComplete(4)` by name, and loads
nothing (with a line in the run log) where the Lua's table has no row. After `level95.lua`'s main chunk the hub load
plans the chapter with `planHubChapter` from `latestMission` and applies it with `applyHubChapter`, which writes the
chapter's offer into `fRunMission[checkpoint].level`: the row already there for the original chapters.
`repo:tests/scripting/story_natives_test.cpp` checks the natives against the 20 rows of
[The four functions](#global-functions) for every prefix of the story, the missing row and the hub's offer, on
synthetic scripts; the disc test above runs the disc's own two functions and the natives side by side for the same 21
cases and prints how many agree (21 of 21).

The merge of the story mods' missions into that chain is built too: `story::mergeStory`
(`repo:src/core/story_merge.h`) places the missions of each mod's `[story]` section
(`repo:src/core/story_manifest.h`) by their anchors and refuses the rest with messages, as
[Merging and refusals](../guides/studio/mission-mods.md#graph-merge) describes; without a story mod its graph is the
original's, with the same hash. Nothing calls it yet, since Coney does not start mods yet.

Mod missions' progress is kept by mission id: `ModStoryProgress` (`repo:src/warriors/mod_story_progress.h`, held in
the game state) routes `UM_Unlock` and `UM_IsLevelComplete` on a mod level's session number to the save's story
section ([Profiles: story and mod sections](save.md#save-mods)) instead of the unlock records. The calls themselves
are the original's (`Script_UMUnlock` `0x004237e8` and `Script_UMIsLevelComplete` `0x004238a8`, confirmed (code)):
they keep a level's progress only as records `(level, group, item)`, so for a mod level the route keeps what those
records would mean, `(level, 0, 0)` as the completion and `(level, 0, k)` for k 1-32 as rank record k, and nothing for
any other call, since a pool number has no records for the original's search (`Unlockables_Unlock` `0x00423f70`) to
match (inferred from the code: the pool's numbers are the ones no level record uses). A mod mission's best record
keeps `MissionBest`'s four described fields. The save system puts a loaded profile's entries in and takes them back
at each save, entries of mods the session lacks included, and a new profile starts with none; no checkpoint is kept.
`latestMission` walks the chain latest first, an original mission by its record and a mod's by its entry, so a save
loaded without one of its story mods goes on from the latest mission present, and `warnMissingStoryMods` names each
missing mod in the run log, at the first level the story enters after the front end.

Mod missions get their numbers once per session, at the first level entry after the preloads have filled the level
table (`StoryHooks::numberMissions`): the numbers 2-59 that no record uses (`story::levelNumberPool`,
`repo:src/core/story_numbers.h`), in chain order, each with a level record `level<n>` at a free index of the table and
its id given to `ModStoryProgress::assign`; the numbers are never saved. A mission past the pool is refused with a
message and leaves the chain, with the new hub chapter it opened (a replacement gives the original mission its place
back). **Coney's stand-in:** a mission needs a free record as well as a free number, so the pool is cut to the
table's free records, 17 of 128 on the NTSC-U disc's table, where the design counts the 45 numbers only
([Open questions](#open-questions)). The disc test runs each of the 45 numbers through the disc's own `global.lua`:
`GetLevel` keeps it, `SetupLevelInventory` takes it for a level below 100, and `UnlockAndLoad` completes the mission by
its id and the natives load the step after it (it prints how many pass: 45 of 45). Coney starts no mods yet, so in
the game the session's merge is the original's chain and nothing is numbered.

A mission after hub chapter *n* opens a new chapter at *n*'s checkpoint, which the hub tells from chapter *n* by the
story's progress, not the checkpoint:
[Hub chapters that mod missions open](../guides/studio/hub-chapters.md) gives the rule and what is built, resting on
the chapter's file and its configuration as [The hub](#hub) describes them (`Main` and `clubhouse.Setup`,
`level95.lua:f229` and `f220`; chapter 11's actions).
The graph answers by step for it, with the latest mission complete from `latestMission` above (the hub load passes
its id to `planHubChapter`), and `fRunMission`'s view keeps the original chapters' 12 rows.

## Open questions {#open-questions}

- **Hub chapters 11 and 12 at runtime.** That every completion after `level64` leads to chapter 12 is read in the code;
  a run with `level64` complete would confirm it.
- **More mod missions than free level records.** Coney's level table has the original's 128 records, and the disc's
  `CfgLevels` fills 111, so at most 17 mod missions get a record and a number, not the 45 free numbers the design
  counts. Whether Coney's table grows past the original's for mods, or the design's count is cut to the free records,
  is open; until it is decided, Coney cuts the pool to the free records (`StoryHooks::numberMissions`).

## How this was checked

The scripts were read in the decompiles that `coney-tools lua decompile --all` writes to the shared scratch folder, and
the callers found with its `index.md`; the script locators are function prototype numbers from that index. The native
side was read in Ghidra (ghidra-mcp) on `SLUS_212.15`: the mission select's build, filter and sort comparators, the
completion check, and the instructions that load the names `runNextMission` and `UnlockAndLoad` (searched by their
low halves, `-0x1868` and `-0xad8`). The arcade cabinet's offer and start were run in PCSX2 from the state library's
`street-end-run`, the story save made with `UM_Unlock` through the `lua` recipe (run r-20261122-level95-armies-trophy).
