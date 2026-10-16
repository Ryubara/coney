# Pause menu

## Sources

Facts on this page come from the NTSC-U PS2 disc. The menu text is the HUD string table in `config_strings_en.lua`
(`WARRIORS.WAD` entry 3,775, 38,257 bytes); the items, their layout and the choices they lead to were read from the
executable `SLUS_212.15`.

Text ids on this page are the numbers of that HUD string table. Buttons and the button tags (`<T>`, `<O>`, `<DU>` ...)
are on the published page `pad-input`; the front-end screens the menu can return to are on the page `frontend`.

## Menu layout

The pause menu is a grid of items in two rows. Every item is drawn at text size 1.15.

| Game | Row 1 | Row 2 |
| --- | --- | --- |
| Story level | Objectives (250), Stats (248), Options (247) | Controls (249), Restart (254), Resume (252), Quit (253) |
| Story level 95 | Objectives (250), Stats (248), Options (247) | Controls (249), Resume (252), Quit (253) |
| Rumble | Rules (251), Options (247), Controls (249) | Replay (255), Resume (252), Quit (253) |

- Story level 95 is the one story level whose menu has no Restart item.
- Rules takes the place of Objectives, and Replay the place of Restart, in a rumble.
- The confirmation box answers are Yes (59) and No (60).

## Items

| Item | Outcome |
| --- | --- |
| Objectives | the objectives panels open, with usage line text 25 (d-pad up and down select, back) |
| Rules | the rumble's rules open in the same panels, with usage line text 25 |
| Stats | the stats screen opens, with usage line text 25 |
| Options | the options screen opens under the heading OPTIONS (278) |
| Controls | the controls screen opens under the heading CONTROLS (371) |
| Restart | the restart choices open (see Restart choices) |
| Replay | the confirmation box opens with text 261 |
| Resume | the menu closes and play goes on |
| Quit | the quit choices open, or the confirmation box with text 258 when no choice applies (see Quit choices) |

## Objectives panels

| Text | Heading | Shown as |
| ---: | --- | --- |
| 231 | Current Objectives | yellow objective icon, heading in colour `txt` (`806400FF`) |
| 232 | Current Objectives | red objective icon, heading in colour `warr` (`861A1AFF`) |
| 233 | Bonus Objectives | bonus objective icon, heading in colour `bon` (`6A4183FF`) |
| 262 | Overview | plain heading |
| 256, 257 | Current Objectives, Bonus Objectives | the same headings in grey (`gry`, `505050FF`) |

The colour names and values are the colour table in `global.lua` (`WARRIORS.WAD` entry 4,105), the same values the
page `frontend` lists for the menu text.

## Restart choices

| Choice | Heading text | Description text |
| --- | --- | --- |
| Restart Level | 274 | 275: "Start level over from the beginning" |
| Restart Last Check Point | 276 | 277: "Go back to last completed check point" |

Text 259 is the string table's confirmation for a restart from the last checkpoint: "Are you sure you want to
start from the last checkpoint? All progress made since the last checkpoint will be lost."

## Quit choices

| Game | First choice | Second choice |
| --- | --- | --- |
| Story, two players | Player 2 (266): "Continue game in 1 player mode" (267) | All (268): "Exit game and go back to title screen" (269) |
| Story, return to the hangout offered | To Hangout (273): "Exit game and go back to hangout" (270) | Main Menu (271): "Exit game and go back to title screen" (269) |
| Story, one player, no hangout offered | none; the confirmation box opens at once | |
| Rumble started from the front end | To Rumble Mode (272) | To Main Menu (222) |
| Rumble started from the story | To Rumble Mode (272) | To Hangout (273) |

## Confirmation box

| Text | Wording | Opened by |
| ---: | --- | --- |
| 258 | "Are you sure you want to quit? All progress made on this level will be lost." | Quit |
| 261 | "Are you sure you want to replay? All progress made on this level will be lost." | Replay |
| 259 | "Are you sure you want to start from the last checkpoint? ..." | in the string table |
| 260 | "Are you sure you want to quit 2nd player?" | in the string table |

| Answer | Outcome |
| --- | --- |
| Yes (59) | the chosen quit, restart or replay goes ahead |
| No (60) | the box closes and the pause menu stays open |
| Back | the same as No |

## Platform differences

| What | PS2 (platform 1) | Platform 2 |
| --- | --- | --- |
| Back button in usage line text 25 | triangle (`<T>`) | circle (`<O>`) |
| Back button in usage line text 31 | triangle (`<T>`) | circle (`<O>`) |

## Entry points

| Address | Job | Outcomes |
| --- | --- | --- |
| `0x001dbee0` | Open the pause menu | Menu layout |
| `0x001de6c0` | Act on the chosen pause menu item | Items |
| `0x001db278` | Answer the pause confirmation box | Confirmation box |

## Acceptance checks

- Pausing a story level shows Objectives, Stats and Options on the first row and Controls, Restart, Resume and Quit
  on the second.
- Pausing a rumble shows Rules, Options and Controls on the first row and Replay, Resume and Quit on the second.
- The pause menu of story level 95 has no Restart item.
- Restart offers "Restart Level" and "Restart Last Check Point".
- Replay asks "Are you sure you want to replay? All progress made on this level will be lost." with Yes and No.
- In a two-player story game, Quit offers Player 2 ("Continue game in 1 player mode") and All ("Exit game and go
  back to title screen").
- In a rumble started from the front end, Quit offers To Rumble Mode and To Main Menu.
- Answering No, or pressing back, on the confirmation box leaves the pause menu open.
- Resume closes the menu and play continues.

## Open questions

- Which of texts 231 and 232 heads the objectives panel at a given moment.
- The menu header is likely the level's name in a story level and the rumble mode's title in a rumble; unconfirmed.
- The picture behind the menu is likely tinted and the radar hidden while the menu is open; unconfirmed.
- Where texts 259 and 260 are shown, and whether the restart choices go through a confirmation box.
- What the stats screen shows, and whether Stats does anything when no stats are available.
- What decides that a story level offers a return to the hangout from the Quit choices.
- Where each Yes outcome leads in detail (title screen, hangout, rumble menu, level restart).
- Which level story level 95 is.
- The Xbox pause menu was not checked.
