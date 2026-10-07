# Cut and unused content

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`), the PS2 disc's
`WARRIORS.DIR` / `.WAD` and the NTSC-U Xbox disc ([Xbox assets](xbox-assets.md)). No runtime claims: everything
here is read from the discs' data and the executable's strings.

## Purpose

What the discs hold that the retail game never uses, and what one disc has that the other lacks. Coney ignores all
of it (it is not part of the retail behaviour); the page exists so that modders and the curious know what is there.
Names below are the discs' own file and record names; descriptions are ours.

## Graveyard Shift: the cut level 7 {#graveyard-shift}

What survives of a story mission set in a Prospect Park graveyard, with the Jones Street Boys and the Saracens.

| Piece | Where | Evidence |
| --- | --- | --- |
| **Title.** `GSTRING.MISSIONNAME` in `config_strings_en.lua` has an entry for `level7` titled *Graveyard Shift*, placed between the Armies of the Night stages and the Rumble arenas; the German, French, Spanish and Italian string files translate it. So the name is the developers', not a fan name. | both discs | inferred (string tables) |
| **No level record.** No `levelNames` record (`config_preload3.lua`, [Levels](../references/levels.md)) is named `level7`, and neither disc has a `level7.lua`, `level7.lev` or `level7s` / `level7d` world, so the retail game cannot load it. `SLUS_212.15` has no string naming it. | both discs | inferred |
| **Three loading screens.** Texture dictionaries `level7_ls_0`, `_1` and `_2` show a wooden gate, a statue by an iron fence under a moon, and a shack. Their caption reads *Prospect Park* and *Graveyard Shift* with the mission bullet **17**, where the retail screens carry their story mission number (Set Up's reads 12). Unlike every retail story level they have no widescreen (`_w`) variant, and the word for loading is baked into the picture. | PS2 WAD | inferred (images) |
| **Radar map.** A standalone dictionary named `level7` (resource hash `ee9be190`, PS2 WAD entry 2795) has the shape of every story level's radar sheet, which `Radar_Setup` (`0x001c3de0`) finds by the world's name ([HUD](hud.md)). No level record or pack uses it. | both discs | inferred |
| **Cutscenes, Xbox only.** 40 scene records `l7_*` (headers `l7_c1`, `l7_c2`, `l7_c3`, `l7_c6`-`l7_c10`, `l7_check_gate`, `l7_make_out`, `l7_scare_att`, `l7_t2_008` and their segments). Roles: Warriors (Fox, Cowboy, Snow, Vermin, Swan, Ajax, Rembrandt, Cleon), Jones Street Boys (their boss, a lieutenant, soldiers, `knox`) and Saracens (`edge`, a lieutenant, soldiers). | Xbox WAD | inferred (records parsed with `coney_tools.scenes`) |
| **No speech.** Neither disc has a `l7` speech folder (the `l11_l7_*` lines belong to `level11`), so the cutscenes are silent. | both discs | inferred |

### What the cutscenes show {#l7-scenes}

Positions are scene coordinates (x, y, z up), read from each header's role starts.

- `l7_c2`: Cleon, Rembrandt, Cowboy, Fox and two soldiers at about (-175, 90, -194.4). That is the Warriors'
  clubhouse briefing spot that `l5_c1` and `l14_c6_a` (Set Up's briefing) also use, so `l7_c2` is the mission's
  briefing. `l7_c1` (Fox and Cowboy, at z -158) is in no other scene's area. **Evidence:** inferred.
- `l7_c3`: Cowboy, Fox and Vermin at a pair of big gates (`door_big_gate`); `l7_check_gate` and `l7_t2_008` are Swan
  (with Ajax) scouting; `l7_make_out` is Swan with a girl (`hook`).
- `l7_scare_att` (label `l7_t2_004`): Jones Street Boys with shovels and beer bottles, the boss among them, and Swan
  watching: a digging party in the graveyard.
- `l7_c6`, `l7_c8`, `l7_c9`: a Jones Street lieutenant with a brown bag, then with soldiers.
- `l7_c7` and `l7_c10`: Fox, Cowboy, Snow and Vermin meet Edge and his Saracens over the brown bag; in `l7_c10` the
  Jones Street Boys and Knox join.
- Fox's presence puts the mission before *Boys In Blue*, where he dies.

### The map is The Graveyard's world {#l7-map}

`level119` (*The Graveyard*) and `level120` (*The Bridge*) are two Rumble arenas on **one world**: their `level119s`
/ `level120s` and `level119d` / `level120d` streams decode to byte-identical geometry (x -167.6 to 46.7, y -15.4 to
194.1). Every `l7_*` position at ground level (all but `l7_c1` and `l7_c2`) falls inside that world, and the `level7`
radar sheet matches its layout: the twin mausoleums at the top, the sunken channel crossing from the left to the lower
right, and the square plot on the right with a rotated block inside. So the cut level 7 was played on the world that
retail keeps only as two small arenas (and Mausoleum Hill, `level130`, is a separate small hill world, not part of
it). **Evidence:** inferred (top-down render of the extracted world against the radar sheet).

### Set Up (mission 12) {#set-up}

Graveyard Shift became *Set Up* (`level14`) according to the fan wiki. Both have Cleon's clubhouse briefing, the
Warriors working with Edge's Saracens against the Jones Street Boys, and a bag of goods; Set Up sends Cowboy and
Cochise instead of Fox's party. A speech-to-text pass over all 67 `l14` lines (scenes `l14_c1`-`l14_c6` and the
chapter scripts' `l14_t*` / `l14_s1_*`) and the Jones Street voice sets (16, 18, 19, 21, 23, 83; their spot, search,
alert and engage commands) found **no mention of a graveyard**. `l14_t4_004` is in the sound list but no script plays
it; it is a Jones Street line from the bum scare in `l14_c4`. **Evidence:** inferred (machine transcription, not
checked by ear).

## Other cut levels and scenes {#other}

- **`level88`**: no title, script or world, but 11 PS2 scene records `l88_*`: Swan, Cowboy, Vermin, Cochise and Ajax
  with the Riffs, a car (`vood_*` parts) and a subway train (`train_a`, `door_subway31`...). **Evidence:** inferred.
- **World-only levels are movie-capture sets.** `level70`-`level74` and `level90`-`level98` have worlds but no
  `.lev` or script on the PS2. Their scripts survive on the Xbox only, and each just builds a cast with dummy players
  and plays a table of scenes with a capture flag (`bCapture`, `KillCapture`) and no gameplay: `level73` plays the
  `l51_c6_*` scenes, `level74` plays `l9_c4`, `level71` and `level72` stage scenes for `level52` and `level54`, and
  `level70` plays its 31 `l70*` records: a subway ride (map, tunnel, station, doors), the Wonder Wheel, gang line-ups
  (Boppers, Elimination, Huns, Moonrunners, Punks, Saracens) and Warriors pairs. Their levels line up with
  the intro movies `L1_IN`, `L51_IN`, `L52_IN`, `L54_IN` and `L9_*` ([Movies](movies.md)), so these levels were
  most likely where those cutscenes were recorded to Bink, and the PS2 kept the worlds and scenes but not the scripts.
  `level90`, `91`, `94`, `96`, `97` and `98` play scenes labelled `e2_*`-`e6_*` (the Rogues, Luther and Cropsie with a
  cash roll, a phone and a gun; Cyrus and Masai with the Riffs; Cleon and Swan at the clubhouse), for a movie not
  yet identified. **Evidence:** inferred (Xbox scripts' strings), speculative for the movie mapping.
- **`l4_*`**: 18 records of ghost-train props (wall panels, a skull, a spider, a mannequin, a mine car) on a level
  number with no level; *Spookarama* (`level125`, Xbox-only `.lev`) is the likely user. **Evidence:** speculative.
- **Orphan radar sheets** like `level7`'s: `level4`, `level23` (worlds of the test levels `combatslow` and
  `test_jump`) and `level106` (*The Shanties*, which has no `.lev` on the PS2). **Evidence:** inferred.
- **Scenes no script names.** 924 of the PS2's 1,236 scene headers are not named by any Lua string, but most are
  reached by built names (talk lines `lN_p1_000`..., bark sets) or by the executable, so this is no measure of cut
  content. Besides those above, `l64_intro`, `rumble_102` / `105` / `106` / `107`, the 18 `l95_intro_*` and the six
  `load_screen*` records are named by neither the scripts, the level data nor `SLUS_212.15`. **Evidence:** inferred.

## Xbox-only content {#xbox-only}

Compared entry by entry (SHA1) with the PS2 WAD. **Evidence:** inferred.

- **31 extra `.lev` records**: `level106`, `level117`, `level125`, `level135` (Rumble arenas the PS2 lists but
  cannot load), `level70`-`74`, `level90`, `91`, `94`, `96`-`98`, and the test levels `combatselect`, `objarena`,
  `test4special`, `testclimb`, `test_car`, `test_combat2`-`5`, `test_combat_ben`, `test_combat_cop`,
  `test_combat_pit`, `test_jump`, `test_rumble`, `test_sound` and `test_variable` (95 against the PS2's 64).
- **36 extra Lua scripts**: the four extra Rumble arenas' scripts (the full Rumble rules script every arena has,
  plus flag and camera files; `level106` also a strings file), the capture levels' scripts above, and test scripts:
  `combatselect` (pick a Warrior and a fight: a bar with the Orphans, a rooftop with the Hurricanes, a court with the
  Furies), `test4special` and `test_car` (car damage with a bat), `testclimb`, `test_jump` (jumps at each speed),
  `test_rumble`, a combat test against Hi-Hats, a cops test with 28 cuff pick-ups, a test of hookers, pimps and
  thugs, a sound-command browser over every hat model, a weapon-throw test, two AI strategy tests, a fidget
  animation list and a list of every throwable object and weapon.
- **49 extra scene records**: the 40 `l7_*`, five more segments of `l55_c5` (`l55_c5an`-`ar`) and four
  `test_ls_bar_*` tests.

## What others found {#web}

| Source | Claim | Here |
| --- | --- | --- |
| The Warriors fan wiki, *Graveyard Shift (Unused Level)* | A lost level reworked into Set Up; Warriors sent by Cleon to a Prospect Park graveyard against the Jones Street Boys, with the Saracens; Mausoleum Hill, The Graveyard and The Bridge are what is left | Confirmed: title, place, gangs, briefing; The Graveyard and The Bridge share the level's world. Mausoleum Hill is a separate world. |
| Same page | In Set Up, Jones Street war parties mention their graveyard plans being foiled | Not found in any `l14` line or Jones Street voice set (see [Set Up](#set-up)) |
| TCRF, *The Warriors/Unused Textures* | Three Graveyard Shift loading screens without widescreen variants; its radar map; radar maps of `level4` and `level23` | Confirmed |
| TCRF, *The Warriors* | `level119` *The Graveyard* is an unused map; `level106` *The Shanties* a stripped Heavy Muscle area | Confirmed for the names and world; arena use not checked |
| atlas (YouTube), *Graveyard Shift cutscenes* | The cutscenes exist only on Xbox and PSP | Confirmed for Xbox vs PS2; PSP not checked |
| atlas (YouTube), *Warriors Exploring the Graveyard* | The full map is larger than the two Rumble areas | Agrees with the shared world above |

## Open questions

- Where `l7_c1` (z -158) takes place.
- Which movie, if any, the `e2`-`e6` capture scenes of levels 90-98 became.
- Whether the PSP disc holds `level7` script or speech.
