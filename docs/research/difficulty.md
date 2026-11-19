---
kind: feature
owns:
  functions: ["0x0041d800", "0x0041d810"]
---
# The difficulty

Verified against: `SLUS_212.15` (NTSC-U); runtime claims cite run records.

## Summary

The player picks a difficulty when making a profile (SUCKER, SOLDIER, HARDCORE SOLDIER and, once unlocked, UNLEASH THE
FURY). The profile keeps it, and every level's start turns it into the **difficulty in force**: a byte of the game
state that the game's code and the level scripts read. The profile's own choice, its screen and its save are on
[Profiles, options and saving](save.md) and [the PM_Difficulty screen](frontend.md#fn-pm-difficulty); this page is the
difficulty in force, who sets it and every reader of it. Most of what a difficulty changes is not code at all but the
difficulty script's configuration (the Warrior classes, the characters' stats), on
[Characters: stats](characters.md#stats).

## Rules

- The difficulty in force is 0 easy, 1 normal, 2 hard, 3 fury or 4 Rumble. A fresh game plays on normal.
- Each level's start runs the difficulty script of the profile's difficulty, which sets the difficulty in force. A
  Rumble arena (level number above 100) always runs Rumble's, and the levels numbered 60 to 64 always run normal's.
- A flash fills a player's health up to hard; on fury and in the Rumble it adds half his maximum health, capped.
- The pause menu's Stats screen measures the bars against the hard row for fury and Rumble.
- The level scripts read it for their own choices (dealers more likely to deal dirty on hard and fury, a spare flash
  in one level on hard and fury only, another boss on fury).

## Map

| Behaviour | Function | Section |
| --- | --- | --- |
| The byte and its start value | `0x00418588` `GameState_Construct` | [The setting](#setting) |
| `SetDifficulty(n)` | `0x0036c440` `lua_SetDifficulty`, `0x0041d810` `GameState_SetDifficulty` | [The setting](#setting) |
| `GetDifficulty()` | `0x0036c3d8` `lua_GetDifficulty`, `0x0041d800` `GameState_GetDifficulty` | [The setting](#setting) |
| Which difficulty script a level runs | `global.lua:CfgGlobalMulti` | [The difficulty scripts](#scripts) |
| The flash's heal | `0x00284280` `Flash_Use`, `0x0022eb40` `Human_Heal` | [The flash](#flash) |
| The Stats screen's row | `0x001e5618` `PauseStats_Setup` | [The Stats screen](#stats) |
| The level scripts' choices | the scripts below | [The scripts that read it](#script-readers) |

## Behaviour

### The setting {#setting}

The difficulty in force is the byte at `W_GameState + 0x154` (the game state is the object `0x0051489c` points to).
`GameState_Construct` (`0x00418588`) stores 1 there at `0x0041879c`, confirmed (code). `GameState_SetDifficulty`
(`0x0041d810`) stores its argument's low byte and `GameState_GetDifficulty` (`0x0041d800`) returns the byte, confirmed
(code).

The bindings: `lua_SetDifficulty` (`0x0036c440`) reads argument 1 with tolua's number reader (`0x004091c8`, 0 when
the script passes nothing), converts it to an unsigned integer with the soft-float conversion `0x0042c420` (a
negative number is 0, the rest truncated toward zero) and stores that through `GameState_SetDifficulty`, so only its
low byte is kept; it returns no result. `lua_GetDifficulty` (`0x0036c3d8`) pushes the byte as an unsigned number,
0 to 255. Confirmed (code). In the running game `SetDifficulty(260)` left 4 in the byte, `SetDifficulty(-1)` 0 and
`SetDifficulty(2.9)` 2: confirmed (runtime), run r-20261119-difficulty-set-byte.

The byte is not saved: the profile record keeps the profile's difficulty (`W_GameState + 0x43c`, record `+0x4ec`,
[the record](save.md#record)) and nothing else writes `+0x154`. The search behind "nothing else" read every byte and
halfword load and store with offset `0x154` in the executable, delay-slot forms included: the only ones on the game
state are the five functions of this page's Map (`GameState_Construct`, the two accessors, `Flash_Use` at
`0x002842d8` and `PauseStats_Setup` at `0x001e5e3c`); inferred from that search, which a word access or an address
taken with `addiu` would escape.

### The difficulty scripts {#scripts}

Confirmed (code) in the scripts named. `global.lua` ends by running `CfgGlobalMulti`, so every level's script step
(`global.lua`, then the level's own) runs it. It takes the profile's difficulty (`GetProfileDifficulty`), replaces it
with 4 when the current level's number (`GetLevelId(GetCurrentLevelIndex())`) is above 100 and with 1 when it is 60
to 64, and runs `config_easy`, `config_normal`, `config_hard`, `config_fury` or `config_rumble` with `doFile` for 0 to
4. Each of those scripts defines `CfgDifficulty`, which first calls `SetDifficulty` with its own number (0 to 4) and
then sets every Warrior class (`CfgWarriorClass`) and the characters' stats again; the script calls it once and
clears it. The boot's `config_preload2.lua` holds a `CfgDifficulty` of normal's too, with `SetDifficulty(1)`.

### The flash {#flash}

Confirmed (code) at the cited addresses. `Flash_Use` (`0x00284280`) asks `Human_Heal` (`0x0022eb40`) for the full
heal unless the human is a player (`+0x1b0` is not -1) and the difficulty in force is above 2 (the unsigned byte, so
fury, Rumble and anything higher). Without the full heal `Human_Heal` adds half the maximum health (shifted right,
so rounded down) to the health and caps the sum at the maximum. The rest of the flash is on
[the flash](combat-rage.md#flash).

At runtime, a player of maximum health 900 at 200 health was healed to 650 on fury and to 900 on hard, and from 600 to
900 (the cap) in the Rumble: confirmed (runtime), run r-20261119-difficulty-flash-heals.

### The Stats screen {#stats}

`PauseStats_Setup` (`0x001e5618`) reads the byte at `0x001e5e3c` and uses it, capped at 2, as the row of the table at
`0x0050f230` that the bars are measured against, confirmed (code). So fury and Rumble use the hard row. The screen is
on [the pause menu](pause.md).

### The scripts that read it {#script-readers}

Confirmed (code) in the scripts named; what each choice means for play is on the level's own page where one exists.

| Script and function | What it does with the difficulty |
| --- | --- |
| `StartFactory` in the story levels' scripts (`level2.lua`, `level3.lua`, `level5.lua`, `level11.lua`, `level14.lua`, `level20.lua`, `level31.lua`, `level34.lua`, `level51.lua`, `level52.lua`, `level82.lua`, `level83.lua`, `level87.lua`, `level92.lua`, `level93.lua`, `level95_coney.lua`) | On hard and above, a dealer spawner of kind 0 or 1 gets a 40 % chance to deal dirty (`chance2BDirty`) in place of its own or 10 |
| `level54.lua:loader` | Below hard, the spare flash (`Objects.ExtraRevive`) is destroyed |
| `level54_boss.lua:ch5.MakeHealthBar` | On fury the boss bar names Cleon at 0.2 in place of Cobb at 0.19 |
| `level54_popsicle.lua:Setup` | Keeps the difficulty; above normal, a later step treats the seventh flash holder differently |
| `level95.lua` and `level95_clubhouse.lua`: `SetupClubhouseEnvironment`, `events.WorkoutChat`, `worksetup.Setup`, `worksetup.FlagRadiusNormal` | On fury the hangout's Warriors keep their weapons (and the heavy-bag boxer gets a bat); otherwise each one's weapon is taken away when he moves between flags |
| `level99_combat.lua:P1.StartTraining` | On fury the two players' held weapons are taken away before the training |

## In Coney

- The difficulty in force is `GameState::difficulty` (`src/warriors/game_state.h`), a byte owned by the game state of
  the session, beside the profile's own difficulty (`GameState::profileDifficulty`); no network code reads or sends it
  yet. The levels' constants are in `src/warriors/difficulty.h`.
- `SetDifficulty` and `GetDifficulty` are real bindings (`src/scripting/script_bindings.cpp`), so `global.lua`'s
  difficulty script sets it at each level's start and the level scripts above read it as the original's do.
- The flash: `difficulty::flashHealth()` (`src/warriors/difficulty.h`), which `PlayLevelSimulation::useFlash()`
  applies; it closes the full-heal stand-in [#1482](issue:1482) tracked.
- The Stats screen: `PauseStats::barValues()` (`src/gui/pause_menu/pause_stats.cpp`) already took the difficulty;
  the pause mode now hands it the one the scripts set.
- Not built here: whether Coney applies each difficulty script's class and character configuration as the original
  does ([q-difficulty-configuration](#q-difficulty-configuration)).

## Open questions

### The difficulty scripts' configuration in Coney {#q-difficulty-configuration}

Coney records the configuration calls of every script it runs, the difficulty script's included, and its readers take
one record per class. Whether every reader takes the difficulty script's record over the boot's, as the original's
later call overwrites the earlier, is not checked. A test on the disc with the profile on easy and on fury, reading a
Warrior class's values after the level's start, would say.

## How this was checked

The byte accesses were found with ghidra-mcp's instruction search for the mnemonics `lb`, `lbu`, `lh`, `lhu`, `sb`
and their delay-slot forms (`_lbu`, `_sb`) with the operand `0x154(`, then filtered to the ones whose base register
holds the game state.
