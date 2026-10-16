# Controller input

## Sources

Facts on this page come from the NTSC-U PS2 disc (`SLUS_212.15`, `SYSTEM.CNF` version 1.03) and were read from:

- the executable `SLUS_212.15` (4,903,976 bytes): the stick table, the default camera buttons, the cheat sequences
  and the controller handling;
- the IOP modules in `MODULES\` on the disc;
- the compiled Lua 4.0 scripts in `WARRIORS.WAD`, found through `WARRIORS.DIR` by name: `global.lua` (169,601 bytes)
  holds the command bindings, `config_strings_en.lua` (38,257 bytes) the English prompts and controller messages,
  `config_preload2.lua` (182,649 bytes) the run-button setting, `level99.lua` (108,385 bytes) the tutorial hints.

The scripts carry values for more than one platform. The PS2 executable reports platform 1, so every value below is
the platform 1 value unless a row says otherwise.

## Ports and controllers

| Fact | Value |
| --- | --- |
| Controller ports used | 2: port 1 and port 2, slot 0 of each |
| Multitap | not supported; the disc carries no multitap module |
| Pad modules on the disc | `PADMAN.IRX` (44,869 bytes) and `SIO2MAN.IRX` (6,641 bytes) in `MODULES\` |
| Pad index | port 1 is pad 0, port 2 is pad 4 (port x 4 + slot); scripts name the ports by these numbers |
| Pad records kept | 8 (pad 0 to 7); only pads 0 and 4 are ever opened |
| Button history | the last 8 button words of each pad, one per game update |
| DualShock and DualShock 2 | accepted; the vibration motors are set up on connection |
| Pad that reports digital mode but has an analogue mode | switched to analogue mode with the mode locked |
| Pad with no analogue mode, or any other controller type | treated as not connected |
| Pressure-sensitive buttons | turned on for every pad that offers them (DualShock 2) |

## Button word

Each update a pad gives a 16-bit word with one bit per button, set while the button is down. The text markup of the
disc's strings draws each button with a tag; the glyph codes are the characters of the button font.

| Bit | Button | Text tag | Glyph |
| --- | --- | --- | --- |
| `0x0001` | L2 | `<L2>` | `0x95` |
| `0x0002` | R2 | `<R2>` | `0x94` |
| `0x0004` | L1 | `<L1>` | `0xa0` |
| `0x0008` | R1 | `<R1>` | `0x9c` |
| `0x0010` | triangle | `<T>` | `0x96` |
| `0x0020` | circle | `<O>` | `0x9d` |
| `0x0040` | cross | `<X>` | `0x9e` |
| `0x0080` | square | `<S>` | `0x9f` |
| `0x0100` | SELECT | `<SELECT>` | `0x93` |
| `0x0200` | L3 | `<L3>` | `0x91` |
| `0x0400` | R3 | `<R3>` | `0x92` |
| `0x0800` | START | `<START>` | `0x97` |
| `0x1000` | d-pad up | `<DU>` | `0x9b` |
| `0x2000` | d-pad right | `<DR>` | `0x98` |
| `0x4000` | d-pad down | `<DD>` | `0x99` |
| `0x8000` | d-pad left | `<DL>` | `0x9a` |

The cheat sequences below confirm this layout independently: their published button orders decode to these bits.

When two or more d-pad directions are down in the same update, only one of them reaches the game; the d-pad never
gives a diagonal.

D-pad auto-repeat (menus and the repeat trigger kind): a held direction fires on the update it goes down, again on
the 15th consecutive held update and every 4th held update after that (updates 1, 15, 19, 23 ...).

## Analogue sticks

The pad reports each stick axis as a byte, 0 to 255, centre 128. The game maps every byte through a fixed
256-entry table in the executable:

| Raw byte | Value |
| --- | --- |
| 0 | -1.0 |
| 1 to 94 | (byte - 95) / 95, from -0.989 to -0.011 |
| 95 to 160 | 0 (dead zone, 66 values) |
| 161 to 254 | (byte - 160) / 95, from 0.011 to 0.989 |
| 255 | 1.0 |

- The dead zone is per axis, so it forms a square around the centre.
- The vertical axes are sign-flipped: pushing a stick up gives a positive value.
- The left stick is turned into camera space: pushing up moves the character away from the camera.
- The game keeps, per player, the left stick's direction and its length, the length capped at 1.0.

## Left stick on foot

| Left stick length | Outcome on foot |
| --- | --- |
| 0.12 or less | no jog |
| above 0.12 | the character jogs |
| above 0.95 | the character runs (PS2 run threshold) |
| any, with L1 held | no run while L1 is held |
| any, carrying a heavy object | jog only above 0.95 |

- Sprinting is L2 held while moving (command 5 below); the tutorial text pairs it with the left stick.
- The run threshold is 0.95 on platform 1; `global.lua` holds 0.85 for platform 2 and 0.75 for any other platform.
- `global.lua` also passes a creep value of 0.4, which the PS2 executable does not keep.
- The executable can instead pick run or sprint from the pressure on L2 (sprint above 100 of 255);
  `config_preload2.lua` switches that mode off, so on the retail disc the stick decides.
- Tutorial hints on the disc name the left stick for: creeping up on a scout by pushing lightly, aiming a throw (the
  arc turns red when locked on), the throw direction with circle, the mugging and interrogation bars (find the
  vibration), tracing a yellow line, and turning it anticlockwise to unscrew a car radio.

## Right stick and camera

| Input | Outcome in play |
| --- | --- |
| Right stick horizontal raw byte 64 or less, or 176 or more | the follow camera turns; the rate grows with the push |
| Right stick vertical raw byte 8 or less, or 232 or more | the follow camera tilts; the rate grows with the push |
| R3 pressed | the follow camera steps to its next zoom distance |
| L3 pressed | the follow camera swings round behind the followed character |
| R1 (default reverse-camera button, `0x0008`) | reverse view, see open questions |

- The right stick has its own, wider dead zone: horizontal bytes 65 to 175 and vertical bytes 9 to 231 do nothing.
- The tutorial says the right stick controls the camera; the Warrior command menu is also chosen with it (R2 held).
- The wheelchair game (`wchair.lua`) sets the reverse-camera button to none.

## Trigger kinds

Each binding in the command table says how its buttons must move. Counts are in game updates.

| Kind | Name | The binding matches on the update when | Rank |
| --- | --- | --- | --- |
| 1 | held | the button is down | 9 |
| 2 | pressed | the button goes down | 7 |
| 3 | released | the button comes up | 1 |
| 4 | repeat | the button goes down, or a held d-pad direction auto-repeats | 6 |
| 5 | tapped | the button comes up after 1 to 6 updates down | 3 |
| 6 | long hold | the button comes up after 1 to 3 updates down, or reaches its 4th update down | 2 |
| 7 | history hold | the button has been down for 5, 6 or 7 updates | 8 |
| 8 | combination held | every button of both groups is down, and was not all down the update before | 5 |
| 9 | combination press | both groups are down, and the second group was not all down the update before | 4 |

- A player carries at most one command per update. When bindings of different kinds match on the same update, the
  player carries the one with the lowest rank number.
- History hold uses a hold length of 5 updates; no script on the disc changes it.
- Kinds 1, 2, 6 and 7 accept a pressure threshold; every binding on the disc sets none.
- Kind 4 is not used by any script on the disc.
- Commands can be switched off and on per player. A player whose command is switched off does not act on it.

## Command table

`global.lua` binds 33 command ids for the PS2 (34 rows, one of them platform-specific), and `level84_chase.lua`
binds id 45. Ids run from 1 to 57; ids not listed are bound by no script on the disc.

| Id | Kind | Buttons (PS2) |
| --- | --- | --- |
| 1 | held | R2 |
| 2 | released | R2 |
| 3 | pressed | R1 |
| 4 | held | R1 |
| 5 | held | L2 |
| 6 | held | L1 |
| 7 | pressed | L1 |
| 8 | released | L1 |
| 9 | combination held | L3 + SELECT |
| 10 | pressed | triangle |
| 11 | history hold | triangle |
| 13 | tapped | circle |
| 14 | history hold | circle |
| 15 | pressed | square |
| 16 | long hold | cross |
| 18 | pressed | cross |
| 21 | held | square |
| 22 | held | cross |
| 30 | pressed | circle |
| 31 | combination held | L1 + R1 (platform 2: L1 + L2) |
| 32 | combination press | L2, then cross |
| 33 | combination press | L2, then square |
| 34 | combination press | cross, then square |
| 35 | combination held | circle + cross |
| 36 | combination press | circle, then triangle |
| 37 | pressed | d-pad down |
| 38 | pressed | d-pad up |
| 39 | pressed | d-pad left |
| 40 | pressed | d-pad right |
| 41 | combination held | d-pad up + SELECT |
| 42 | combination held | d-pad right + SELECT |
| 43 | combination held | d-pad down + SELECT |
| 44 | combination held | d-pad left + SELECT |
| 45 | pressed | one of triangle, circle, square, cross, chosen per chaser (`level84_chase.lua` only) |

In the combination press rows the first group may already be down; the command comes when the second group goes
down while the first is held.

Examples of what a player carries:

| Input | Commands carried |
| --- | --- |
| Square pressed and held | 15 on the update it goes down, 21 on each later update while held |
| Circle tapped for 3 updates | 30 when it goes down, 13 when it comes up |
| Circle held for 10 updates | 30 when it goes down, 14 on held updates 5, 6 and 7 |
| Cross held for 10 updates | 18 when it goes down, 22 on held updates 2 and 3, 16 on held update 4, 22 afterwards |
| L1 tapped | 7 when it goes down, 8 when it comes up |
| L1 and R1 pressed on the same update | 31 |

## Commands in play

What each command does on foot. "Code" rows were seen in the executable; "text" rows come from the disc's tutorial
and hint text (`config_strings_en.lua`, `level99.lua` and level scripts), which name the PS2 buttons.

| Id | Button | Outcome | From |
| --- | --- | --- | --- |
| 1 | R2 held | opens the Warrior command menu; the entry is chosen with the right stick | code, text |
| 2 | R2 released | issues the chosen Warrior command | code |
| 3 | R1 pressed | reverses an enemy's grab; spins a grabbed opponent; uses cuffs on a mounted enemy | code, text |
| 4 | R1 held | blocks; ducking and square gives a counter attack | code, text |
| 5 | L2 held | sprints; lets go of a grabbed or mounted enemy; drops the target; breaks away from a fight | code, text |
| 6 | L1 held | manual target: a yellow mark shows in the target's health bar; no running | code, text |
| 10 | triangle pressed | action: climb a fence or obstacle, pick up a weapon or item, talk, steal a car radio | text |
| 10 | triangle pressed while cuffed | uses a key to remove the cuffs, when one is held | code |
| 11 | triangle held | starts a tag at a rival gang's burner, with spray paint | text |
| 13 | circle tapped | grabs an enemy; throws a grabbed enemy toward the left stick | text |
| 14 | circle held | tackles and mounts the opponent | text |
| 15 | square pressed | light attack; chains with square and cross into combos; weapon attack | text |
| 18 | cross pressed | heavy attack; weapon attack; throws a projectile weapon | text |
| 31 | L1 + R1 | enters Rage when the Rage meter is full | code, text |
| 32 | L2 held, cross | charge (bust through a weak fence while sprinting) | text |
| 33 | L2 held, square | running attack (cross body) while sprinting | text |
| 34 | cross held, square | strong attack or power move; throws a melee weapon | text |
| 35 | circle + cross | strong grapple; catches an enemy off guard from behind | text |
| 36 | circle held, triangle | spray attack | text |
| 37 | d-pad down | shows the bonus objective again | code |
| 38 | d-pad up | shows the current objective again | code |
| 40 | d-pad right | takes Flash and heals, when Flash is held and health is not full | code, text |
| 45 | per chaser button | the chase in `level84_chase.lua` | data |

- Tutorial text on the disc also names circle (pressed on impact or quickly) to break free from, or reverse, a hold.
- The flash hint in `level99.lua` names "right on the D-pad" but draws the `<DL>` tag (see open questions).
- At full health, d-pad right with Flash held and the matching unlock owned fills the Rage meter and enters Rage.
- The PS2 menus show cross as ok and triangle as back.

Scripts switch commands per player. Examples on the disc:

| Script | Commands switched off for the player |
| --- | --- |
| `level60.lua` to `level64.lua` | 1 and 5 (no command menu, no sprint); 32 and 33 are removed |
| `level99.lua` (tutorial) | 1, 37, 38 and 40 until each lesson opens them |
| `level95_flashrage.lua` | 40, on and off around the Flash lesson |

## Buttons by context

Button uses that the scripts set for a context, by port (pad 0 is port 1, pad 4 is port 2):

| Context | Script | Ports | Buttons and outcome |
| --- | --- | --- | --- |
| Front end menus | `level100main.lua` | 1 | cross selects, triangle backs out, d-pad left and right cycle, SELECT toggles the scene |
| Pop-up dialogs | `dlgpopup.lua`, `textdlgpopup.lua` | 1 | every script button use on port 1 is cleared |
| Point-of-interest views (POIZO) | `global.lua`, `level86_chapter4.lua` | 1 and 2 | cross or START skips the view |
| Wheelchair game | `wchair.lua` | 1 and 2 | L2 and R2 presses are counted on port 1; L3 rings the bell on either port |
| Party scene | `level55_party.lua` | 1 | triangle leaves the dance |

Prompts the PS2 shows (HUD strings in `config_strings_en.lua`; platform 2 shows circle instead of triangle for back):

| HUD string | PS2 prompt |
| --- | --- |
| 23 | d-pad right and left select, cross ok, triangle back |
| 24 | d-pad up and down select, cross ok, triangle back |
| 25 | d-pad up and down select, triangle back |
| 26 | all four d-pad directions select, cross ok, triangle back |
| 30 | triangle back |
| 212 | press the START button to join |

Other button outcomes outside the command table:

| Context | Input | Outcome |
| --- | --- | --- |
| Play | START pressed on a player's own controller | the pause menu opens for that player |
| Play, two-player levels | START held on a connected controller with no player | a second player joins |
| Lead-in to the mission-failed or mission-complete screen | cross pressed by a player | the wait is cut to at most 10 updates |
| Skippable movie | any button on any controller | the movie ends; a START held through it does not open the pause menu |

## Cheat sequences

The executable stores 27 cheat sequences of six buttons each. Entering one on player 1's controller during play,
one button at a time (a repeated button needs a release in between), shows a "Cheat Activated!" banner for 2.25 s
(text in `global.lua`). Only the last six buttons entered count.

| No. | Sequence |
| --- | --- |
| 0 | R1, R2, L1, cross, down, L1 |
| 1 | up, triangle, L3, SELECT, cross, L2 |
| 2 | down, square, left, cross, L1, SELECT |
| 3 | up, SELECT, cross, triangle, triangle, circle |
| 4 | SELECT, square, left, square, R3, R2 |
| 5 | square, circle, triangle, SELECT, cross, left |
| 6 | down, down, SELECT, up, up, L3 |
| 7 | square, R2, down, down, L1, L1 |
| 8 | R2, circle, SELECT, up, L1, right |
| 9 | L1, cross, R1, R1, SELECT, R2 |
| 10 | right, SELECT, circle, left, circle, square |
| 11 | L3, L3, circle, up, circle, SELECT |
| 12 | down, square, cross, SELECT, R1, left |
| 13 | L1, SELECT, square, down, L2, right |
| 14 | circle, circle, circle, L1, SELECT, triangle |
| 15 | R3, R2, R1, L3, L2, L1 |
| 16 | left, cross, cross, R2, L1, down |
| 17 | up, cross, up, SELECT, L3, L1 |
| 18 | triangle, triangle, triangle, SELECT, triangle, R1 |
| 19 | L2, cross, R2, L1, L1, circle |
| 20 | cross, L1, down, square, up, cross |
| 21 | L2, SELECT, SELECT, SELECT, SELECT, triangle |
| 22 | down, left, up, up, square, right |
| 23 | circle, circle, down, R2, L2, circle |
| 24 | R1, R1, L1, R1, L1, up |
| 25 | right, R1, circle, cross, SELECT, square |
| 26 | up, up, down, down, left, right |

## Pulled controller

| Situation | What the player sees |
| --- | --- |
| A controller bound to a player is pulled out | play stops, sound pauses and a message names the port |
| The message (HUD strings 373, 374, 375) | a controller has been disconnected from controller port N; reconnect a controller to port N and press the START button to continue |
| During play | the frozen game view stays behind the message, dimmed |
| A controller is reconnected to port N and START pressed and released on it | play resumes with that controller driving the same player |
| No controller connected at all (HUD string 376) | no controllers are connected; connect one and press the START button |
| START pressed and released on any connected controller after that | play resumes with that controller as player 1 |
| A controller with no player (for example port 2 in one-player play) is pulled | nothing happens |
| A controller is put back | the game sets it up again (analogue mode, vibration, pressure) before it is read |

The port number N is 1 or 2.

## Vibration and pressure

- The game drives both vibration motors: the small motor on or off, the large motor at a strength from 0 to 255.
- A controller vibrates only while it is bound to a player and the Vibration option (HUD string 287) is on.
- Pressure is read for 12 buttons: the four d-pad directions, triangle, circle, cross, square, L1, R1, L2 and R2.
  No binding and no retail setting on the disc uses it.

## Platform differences

The scripts on the PS2 disc carry values for platform 2 beside the PS2 ones; the Xbox disc was not checked here.

| What | PS2 (platform 1) | Platform 2 |
| --- | --- | --- |
| Run threshold | 0.95 | 0.85 |
| Rage combination (id 31) | L1 + R1 | L1 + L2 |
| Back button in prompts | triangle | circle |
| Hint wording | "press" or "hold down" for shoulder buttons | "pull" or "pull and hold" |
| No-controller message | "press the START button" | "press START", "any controller port" |

## Entry points

| Address | Job | Outcomes |
| --- | --- | --- |
| `0x001454a8` | Read both controllers each update | Analogue sticks |
| `0x00146078` | Turn a player's pad into stick values and a command | Trigger kinds |
| `0x00147430` | Bind a command id to buttons | Command table |
| `0x0022a980` | Free the player of a pulled controller | Pulled controller |
| `0x00129050` | Steer the follow camera from the right stick | Right stick and camera |
| `0x00163c68` | Match cheat button sequences | Cheat sequences |

## Acceptance checks

| Check |
| --- |
| `MODULES\` on the PS2 disc holds `PADMAN.IRX` and `SIO2MAN.IRX` and no multitap module |
| With only a controller in port 2, pressing START on it clears the no-controller message |
| With the left stick pushed less than a quarter of its travel, the character does not move |
| With the left stick pushed fully, the character runs; at about two thirds, it jogs |
| Holding L1 while pushing the stick fully, the character does not run |
| Pressing square on foot gives a light attack; pressing cross gives a heavy attack |
| Tapping circle next to an enemy grabs the enemy; holding circle tackles the enemy |
| Holding R2 opens the Warrior command menu and releasing R2 issues the chosen entry |
| With a full Rage meter, pressing L1 and R1 together enters Rage |
| With Flash in the inventory and health below full, pressing d-pad right heals the character |
| Pressing d-pad up in a mission shows the current objective again |
| Pressing START in play opens the pause menu |
| Pushing the right stick fully left or right turns the camera; small pushes inside the wider dead zone do nothing |
| Pressing R3 in play changes the camera distance |
| The front-end prompts on the PS2 show cross for ok and triangle for back |
| Entering up, up, down, down, left, right one button at a time in play shows the cheat banner |
| Pulling the controller from port 1 during play stops the game and shows the disconnected message naming port 1 |
| Reconnecting it to port 1 and pressing START resumes play with the same character |
| Pulling a controller from an unused port 2 in one-player play shows no message |
| A held d-pad diagonal moves a menu cursor along one axis only |

## Open questions

- The game update rate is not confirmed, so update counts are not given in seconds here.
- The executable can re-read a pad only after a minimum time between reads in some modes; when it applies is unclear.
- Which d-pad direction survives when two are held: the choice follows how long each has been held, but the exact
  pairing could not be confirmed.
- The reverse-camera button (R1 by default) shows a reverse view only in some camera states; R1 is also block.
- L3 recentring is limited to some camera states (not in combat framing, not in a mini-game); the full set is
  unconfirmed.
- Commands 9, 39 and 41 to 44 (L3 + SELECT, d-pad left, SELECT + d-pad) have no confirmed outcome on foot.
- Commands 7, 8, 16, 21, 22 and 30 have no outcome of their own confirmed beyond the rows above.
- A command switched off for a player may still be carried a short time later; the timing is unconfirmed.
- The flash hint draws `<DL>` while its words say right; whether the button font draws a right arrow for that tag
  is unconfirmed.
- The effects of the 27 cheat sequences are set by `global.lua` and are not listed here.
- Platform 2 is taken to be the Xbox from the hint wording; the Xbox executable was not checked.
- Whether a DualShock switched to digital mode by the player is set back to analogue is unconfirmed.
- The second-player join conditions (which levels, which camera states) are not fully listed.
