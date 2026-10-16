# In-game HUD

The in-game HUD of the PS2 release (NTSC-U disc `SLUS-21215`): the hint box, objective messages, the player panel, the
HUD colour slots and the sprite sheets the HUD draws from. The radar has its own page. Values come from the PS2
archive `WARRIORS.WAD` (entries named by `WARRIORS.DIR`), the compiled Lua scripts `config_preload2.lua`, `global.lua`
and `level99.lua` inside it, the executable's data, and ten PCSX2 savestates of the first mission (`level99`, on foot).
Screen positions are in pixels of the 640 x 480 savestate screenshots and are read by eye.

## Screen layout in level 99

| Element | Where on screen | What it shows |
| --- | --- | --- |
| Hint box | Lower left; a dark see-through box from about x 33 to x 500, top edge about y 388 | Two or three lines of light grey text, with pad button symbols inside the text drawn in the button's own colour |
| Objective message | Lower left, at the hint box's place (about x 35, y 385 to 420) | A yellow round `W` badge, the heading "New Objective" in yellow, and the objective text in light grey below it |
| Radar | Lower right | See the radar page |
| Objective marker | In the world, over the place the current objective points at | The same yellow round `W` marker the radar shows |

No player panel is on screen in any of the ten savestates.

## HUD colour slots

The HUD reads ten colour slots, set by `config_preload2.lua` at start-up (`global.lua` holds the same table). Values
are red, green, blue, alpha. The slot 9 value is stored in the script as the text "0xA5A98AFF" rather than as a
number.

| Slot | RGBA | Seen as |
| --- | --- | --- |
| 0 | B2 B2 B2 FF | Light grey; the hint text colour read from the savestates |
| 1 | FF FF FF FF | White |
| 2 | 49 B7 49 FF | Green |
| 3 | 86 1A 1A FF | Dark red |
| 4 | 80 64 00 FF | Main objective heading (yellow on screen) |
| 5 | 50 50 50 FF | Dark grey |
| 6 | 6A 41 83 FF | Bonus objective heading (purple) |
| 7 | 23 53 BC FF | Blue |
| 8 | 23 53 BC FF | Blue; the same value as slot 7 on this disc |
| 9 | A5 A9 8A FF | Pale khaki |

The same script defines a selected colour of 178, 178, 178, 255 (equal to slot 0) and an enabled and a disabled
colour of 80, 80, 80, 255 (equal to slot 5).

## HUD message texts

The HUD's fixed message texts (such as the "New Objective" heading) come from the HUD group of the string table in
the language script for the chosen language.

| Language | Script | Archive entry | Size in bytes |
| --- | --- | --- | --- |
| English | `config_strings_en.lua` | 3,775 | 38,257 |
| French | `config_strings_fr.lua` | 3,777 | 41,311 |
| German | `config_strings_de.lua` | 3,774 | 42,607 |
| Spanish | `config_strings_es.lua` | 3,776 | 43,443 |
| Italian | `config_strings_it.lua` | 3,778 | 41,600 |

## Hints

| Property | Value |
| --- | --- |
| Priority order | A lower number is more urgent |
| A more urgent hint arrives while one shows | The new hint shows at once; the one it displaced goes back into the queue |
| Priority given when a level script names none | 1 |
| Priority value a clear request uses when the script names none | 4, which covers hints of every priority |
| Text wrap width | 0.74 of the screen width; 0.52 in split screen |
| Text colour | HUD colour slot 0 (B2 B2 B2 FF) |
| Backdrop | Dark, see-through; its alpha rises to at most 125 |
| Sound | Interface sound cue 0x15 plays when a new hint appears |

## Objectives

Level scripts set objectives in two slots: slot 0 is the main objective, slot 1 the bonus objective. Each request
gives a slot, a text, a mode, a silent flag and a message time in milliseconds (8,000 when the script gives none).

| Mode | Outcome |
| --- | --- |
| 0 | The objective is added; an objective message shows unless the request is silent |
| 1 | The objective is removed |
| 2 | The objective is ticked as done; a pending message for it is dropped; ticking the bonus objective shows a "bonus complete" announcement |
| 3 | The objective is added already ticked |

| Property | Value |
| --- | --- |
| Main objective heading colour | HUD colour slot 4 |
| Bonus objective heading colour | HUD colour slot 6 |
| Message text size | 0.05 screen units |
| Sound | Interface sound cue 0x11 plays with each objective message |
| A message equal to the one showing | Dropped |
| A new, different message | Cuts the showing message short |
| First bonus objective of a level | Queues tutorial hint 21 once, while tutorial texts are switched on |

## Player panel

| Property | Value |
| --- | --- |
| Hold time | Fully shown for 2,000 ms after it is brought up |
| Fade time | Fades out over the following 1,000 ms |
| Forced show | The panel stays up without fading while the game asks for it to be kept on screen |
| Score counter | Not drawn in Rumble levels (level 100 and up) |
| Levels 60 to 69 | Use a different kind of panel |

## Visibility rules

| Situation | What the HUD shows |
| --- | --- |
| Letterbox (cut-scene bars) on | Nothing; both radars are off |
| Objective message showing | The hint box is not drawn |
| War-command display or an announcement showing | The objective message, the hint box and the action prompts are not drawn |
| Split screen | A second radar shows, and a divider made of 19 sprites splits the screen |
| Start of a level | The HUD is hidden after the level is set up; `level99.lua` hides it and later brings it back |

## Sprite sheets

The HUD's sprites come from sprite sheets: a resource holding a texture dictionary (chunk 0x2A) and a sprite page
(chunk 0x4C). Both sheets below sit inside `global.pak` (archive entry 4,106) and also as stand-alone entries.

| Sheet | Stand-alone entry | Offset in `global.pak` | Texture | Chunk 0x2A size | Chunk 0x4C size | Rectangles |
| --- | --- | --- | --- | --- | --- | --- |
| `part_page0` | 528 | 155,440 | 512 x 256, 8-bit | 132,496 | 5,968 | 371 |
| `hud_minigames` | 2,570 | 361,088 | 256 x 128, 8-bit | 34,208 | 240 | 13 |

### Sprite page layout

| Offset | Size | Meaning |
| --- | --- | --- |
| 0x00 | 4 | Word 0x00421798 in every sheet read (`part_page0`, `hud_minigames` and the radar map sheets) |
| 0x04 | 4 | Rectangle count (371 in `part_page0`) |
| 0x08 | 4 | 94 in `part_page0`; 0xFFFFFFFF in `hud_minigames` and the radar map sheets |
| 0x0C | 4 | Zero on disc |
| 0x10 | 4 | Zero on disc |
| 0x14 | 16 per rectangle | Four 32-bit floats u0, v0, u1, v1 as fractions of the texture's width and height |

Each rectangle is inset by a quarter of a texel on every side: rectangle 22 of `part_page0` spans texels 476.25 to
500.75 across and 125.125 to 151.875 down, a 25 x 27 sprite. `part_page0` ends with 12 zero bytes after its 371
rectangles (20 + 371 x 16 + 12 = 5,968); `hud_minigames` does the same after its 13 (20 + 13 x 16 + 12 = 240).

## Entry points

| Address | Job | Outcomes |
| --- | --- | --- |
| `0x001ad588` | Set up the HUD for a level | Visibility rules |
| `0x001b1688` | Draw the HUD each frame | Visibility rules |
| `0x001ce3c0` | Queue a hint | Hints |
| `0x001dad88` | Set, remove or tick an objective | Objectives |
| `0x001c8b08` | Queue an objective message | Objectives |
| `0x00213290` | Draw the player panel | Player panel |

## Acceptance checks

| Outcome |
| --- |
| The PS2 archive entry 528 holds a sprite sheet named `part_page0` with a 512 x 256 8-bit texture and 371 rectangles |
| The sprite page of `part_page0` is 5,968 bytes long |
| Rectangle 22 of `part_page0` is a 25 x 27 sprite once the quarter-texel inset is removed |
| `config_preload2.lua` gives HUD colour slot 0 the value B2 B2 B2 FF and slots 7 and 8 the same blue, 23 53 BC FF |
| In the first mission, hints show as light grey text in a dark see-through box at the lower left of the screen |
| While an objective message shows, the hint box is not on screen |
| A new main objective shows a yellow "New Objective" heading with the objective text below it |
| A newly brought-up player panel stays fully visible for about two seconds and fades out over about one more |
| No HUD element is drawn while the cut-scene letterbox bars are on screen |
| In split screen each player has a radar |

## Open questions

- Where each HUD element sits and how large it is in screen units is still to be read from the executable; the
  pixel positions above are estimates from screenshots.
- What HUD colour slots 1, 2, 3, 5, 7, 8 and 9 colour on screen (cash, gang and bonus counters are likely but not
  confirmed), and whether the slot 8 value equal to slot 7 is intended.
- The hint backdrop's alpha scale (out of 128 or 255) and how fast it fades in and out.
- The hint layout table in the executable's data holds 4,000 and 200 next to the wrap widths; their meaning (perhaps
  a hold time and a fade time in milliseconds) is unconfirmed.
- Which levels count as the level kind that skips the first-bonus tutorial hint.
- What the player panel shows in each panel kind and where it sits; it is never on screen in the savestates.
- What the `hud_minigames` sheet's rectangles hold, and the meaning of the sprite page word at 0x08.
- Differences between the PS2 and Xbox HUD have not been looked at.
