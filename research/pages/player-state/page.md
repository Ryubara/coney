# Player state

## Scope and sources

This page records the state each player has during play: health, the rage meter, flashes and revives, arrest and
handcuff keys, carried and banked money, and what of it survives a checkpoint, a level change and the memory card
save. Item counts, limits and pickups are on the inventory page. Character statistics other than the rage maximum are
on the character reference page; the profile record on the card is on the save data page.

Sources, all from the PS2 disc image "Warriors, The (USA) (En,Fr,De,Es,It).iso":

| Source | Size (bytes) | Used for |
| --- | ---: | --- |
| `SLUS_212.15` | 4,903,976 | Health, flash, revive, rage and arrest outcomes (entry points below) |
| `config_easy.lua`, `config_normal.lua`, `config_hard.lua`, `config_fury.lua` | 17,316 / 17,181 / 17,253 / 16,884 | Difficulty values 0 to 3 and the warrior class lines |
| `config_rumble.lua` | 17,049 | Difficulty value 4 and its warrior class lines |
| WAD entry 4105, `global.lua` | 169,601 | Rage timing values, starting items, the player 2 set-up |
| WAD entry 3771, `config_preload2.lua` | 182,649 | The item table (flash, money, keys) |

All files except `SLUS_212.15` are compiled Lua 4.0 scripts inside `WARRIORS.WAD`.

## Players

| Fact | Value |
| --- | --- |
| Players at once | 1 or 2 |
| Player numbers in the level scripts | 1 and 2; a script that names no player means player 1 |
| State kept per player | Health, rage meter, arrest state and every item count, money included |
| State shared by both players | Banked money, difficulty |

- A second player who joins at a level start (checkpoint 0 or 1) gets the same starting items as player 1 (see the
  inventory page).
- Both players' state is reset and restored together at level starts and checkpoints.

## Health

| Fact | Value |
| --- | --- |
| Current and maximum health | Whole numbers |
| Maximum health | From the character's data; 100 for a character that has none |
| Health set by a script | Never above the maximum; a larger value gives the maximum |
| Maximum health set by a script | Current health becomes the new maximum as well |
| Full heal | Current health equals the maximum |
| Half heal | Current health rises by half the maximum (rounded down), never past the maximum |

A knocked-out character who is healed gets up in a fighting stance.

## Flashes and revives

Flashes are item 1 of each player's inventory, 0 to 3 per player (4 with unlockable 6/7).

### Using a flash

What a player holding at least one flash gets from the flash button:

| Player's health | Unlockable 6/8 | Flashes after | Outcome |
| --- | --- | --- | --- |
| Below full | Locked | One fewer | Health rises by the difficulty's amount (next table); `vags/misc/flash` plays |
| Below full | Unlocked | One fewer | Health rises by the difficulty's amount (next table); `vags/misc/flash` plays |
| Full | Locked | Unchanged | Nothing happens |
| Full | Unlocked | One fewer | The rage meter is filled to its maximum and rage starts, where a full meter could start rage |

`vags/misc/flash` is interface cue 23 of `config_preload2.lua`, heard without a position in the world.

| Player's situation | Flash button |
| --- | --- |
| Holds no flash | Does nothing |
| Knocked out | Does nothing |
| Cuffed | Does nothing |

Health after a flash, by difficulty:

| Difficulty | Choice | Health after the flash | Maximum 100, health 10 | Maximum 100, health 70 | Maximum 125, health 10 |
| ---: | --- | --- | ---: | ---: | ---: |
| 0 | SUCKER | The maximum | 100 | 100 | 125 |
| 1 | BOPPER | The maximum | 100 | 100 | 125 |
| 2 | HARDCORE SOLDIER | The maximum | 100 | 100 | 125 |
| 3 | UNLEASH THE FURY | A half heal (see [Health](#health)) | 60 | 100 | 72 |
| 4 | Rumble mode | A half heal (see [Health](#health)) | 60 | 100 | 72 |

### Reviving a partner

A player can revive a partner on their own side who is knocked out, not cuffed, made revivable by the level script
and in sight within 3.0 world units. The revive needs a flash, held by either of the two:

| Reviver's flashes | Knocked-out partner's flashes | Revive | Flash spent when the revive completes |
| ---: | ---: | --- | --- |
| 0 | 0 | Does not start | None |
| 0 | 1 or more | Starts | One of the partner's |
| 1 or more | 0 | Starts | One of the reviver's |
| 1 or more | 1 or more | Starts | One of the partner's |

| Moment | Heard | Partner |
| --- | --- | --- |
| The revive starts | The reviver says speech command 76 | Still knocked out |
| The revive completes | `vags/misc/flash`, and the partner says speech command 75 | At full health on every difficulty, up in a fighting stance |

A revived partner always gets up at full health, also on UNLEASH THE FURY, where a flash used on oneself gives only a
half heal.

Worked examples, two players on UNLEASH THE FURY, maximum health 100:

| Before | Action | After |
| --- | --- | --- |
| Player 1: 2 flashes, health 30 | Player 1 uses a flash | Player 1: 1 flash, health 80 |
| Player 1: 1 flash, health 100 | Player 1 uses a flash (unlockable 6/8 locked) | Player 1: 1 flash, health 100 |
| Player 1: 2 flashes; player 2 knocked out with 1 flash | Player 1 revives player 2 | Player 1: 2 flashes; player 2: 0 flashes, health 100 |
| Player 1: 3 flashes; player 2 knocked out with 0 flashes | Player 1 revives player 2 | Player 1: 2 flashes; player 2: 0 flashes, health 100 |
| Player 1: 0 flashes; player 2 knocked out with 0 flashes | Player 1 tries to revive | No revive; player 2 stays knocked out |

## Rage

| Fact | Value |
| --- | --- |
| Meter range | 0 to the rage maximum of the character's warrior class |
| Meter full | Rage can be started; the level scripts are told the meter is full |
| Script control | Fill the meter, set it to a fraction of its maximum, hold it where it is, or block rage |
| Rage timing values in `global.lua` | 20,000 ms and 5,000 ms |

The rage maximum is the fourth value of each warrior class line in the five difficulty scripts. It is the same in all
five:

| Warrior class | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Rage maximum | 150 | 120 | 120 | 120 | 150 | 100 | 78 | 120 | 96 |

How fast the meter rises and falls belongs to combat and is not covered here.

## Arrest and handcuffs

| Situation | Outcome |
| --- | --- |
| A character is arrested | It stands cuffed in place until freed |
| Overhead icon of an arrested character | The handcuffs icon (object `dyn_cuffs`), in the cases listed under open questions |
| A cuffed character is freed | It is no longer cuffed and the icon goes; a gang member follows its leader again |
| A cuffed player holds a key, unlockable 6/15 unlocked | The player can free themselves; one key is spent and a sound plays |
| A cuffed player | Cannot use a flash |

Handcuffs (item 5) and keys (item 6) are counted per player, 0 to 9 each.

## Money

| Kind | Range | Kept |
| --- | --- | --- |
| Carried money, per player | 0 to 999 dollars | Until the level ends; item 2 of the inventory |
| Banked money, one total | 0 or more dollars | In the profile record at offset `0x4AC` (see the save data page) |

- When a mission ends and play leaves the level, the money both players carry is added to the banked money and the
  game autosaves.
- Starting a level from its beginning empties both players' carried money; level 87 then gives $15.

## What carries over

| State | Checkpoint restart | Next level | Memory card |
| --- | --- | --- | --- |
| Item counts, per player | The counts recorded at the checkpoint | Not kept; each level starts from 0 plus its starting items | Not saved |
| Carried money | As item counts | Banked at the end of the mission | Saved only as part of the banked total |
| Banked money | Kept | Kept | Saved, record offset `0x4AC` |
| Difficulty | Kept | Kept | Saved, record offset `0x4EC` |

## Difficulty

| Value | Set by | Choice |
| ---: | --- | --- |
| 0 | `config_easy.lua` | SUCKER |
| 1 | `config_normal.lua` | BOPPER |
| 2 | `config_hard.lua` | HARDCORE SOLDIER |
| 3 | `config_fury.lua` | UNLEASH THE FURY |
| 4 | `config_rumble.lua` | Rumble mode (not a menu choice) |

Values 0 to 3 match the difficulty field of the profile record. Value 4 only exists during Rumble.

## Differences from the Xbox version

Not checked; the Xbox executable was not examined for this page.

## Entry points

| Address | Job | Outcomes |
| --- | --- | --- |
| `0x00237848` | Set a character's health | Health |
| `0x00284280` | Use a flash | Flashes and revives |
| `0x00274ee0` | Complete a partner revive | Flashes and revives |
| `0x00236a68` | Fill a character's rage meter | Rage |
| `0x0022ec18` | Arrest a character | Arrest and handcuffs |
| `0x0022ef58` | Free an arrested character | Arrest and handcuffs |

## Acceptance checks

- On BOPPER, a player below full health who uses a flash returns to full health and has one flash fewer.
- On UNLEASH THE FURY, a player at 10% health who uses a flash ends at 60% of maximum health.
- A player at full health who uses a flash keeps the flash (without unlockable 6/8).
- A player with no flashes cannot revive a knocked-out partner who also has none.
- Reviving a partner who holds a flash leaves the reviver's flash count unchanged.
- A revived partner gets up at full health.
- Each of the five difficulty scripts gives the nine warrior classes the rage maxima 150, 120, 120, 120, 150, 100, 78,
  120 and 96.
- `config_rumble.lua` sets difficulty 4 and `config_fury.lua` sets difficulty 3.
- `global.lua` gives the rage timing values 20,000 and 5,000.
- After a mission ends, the profile record's money field at `0x4AC` has grown by the money both players carried.
- Handcuff keys held at the end of a level are gone at the start of the next.

## Open questions

- What the rage timing values 20,000 ms and 5,000 ms each control.
- Which Warrior uses which warrior class.
- Starting health at a level start, and whether health carries across a checkpoint restart.
- The button that uses a flash and the one a cuffed player uses to free themselves.
- The exact conditions under which an arrested character drops what it holds, shouts for help or shows the
  handcuffs icon.
- The flash-to-rage outcome needs the player's rage to be enabled; when the game enables or disables a player's rage
  was not traced.
- When the mission fails because the players are down or cuffed; this was not traced.
- Whether banked money is ever spent, and its largest value.
- Which state the HUD shows for each player.
- The Xbox version's player state.
