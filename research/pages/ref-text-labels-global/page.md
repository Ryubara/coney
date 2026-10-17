# Text labels: global tables

This reference page lists the keys of the game's shared string tables: `GSTRING`, `LABEL`, `TSTRING` and `RUMBLE`.
The tables of the story levels are on the companion page `ref-text-labels-levels`. Every value was read from the PS2
disc (NTSC-U, `SLUS_212.15`), from entries of `WARRIORS.WAD` located through `WARRIORS.DIR`. All the files named
here are compiled Lua 4.0 chunks. The page gives keys, ids, counts and the files that hold them, not the text itself.
The text values carry inline markup tags (font, size, colour, alignment and button glyphs) around the words.

## Disc sources

39 files hold the four tables: `global.lua`, the five config strings files and the 33 arena scripts. A key count is
the number of distinct keys of that table over all the languages in the file; a dash means the file does not hold
that table. Entry numbers count from 0.

| File | `WARRIORS.DIR` entry | Size (bytes) | Languages | `GSTRING` keys | `LABEL` keys | `TSTRING` keys | `RUMBLE` keys |
| --- | ---: | ---: | --- | ---: | ---: | ---: | ---: |
| `global.lua` | 4,105 | 169,601 | all five | 34 | 9 | - | - |
| `config_strings_de.lua` | 3,774 | 42,607 | German | 8 | - | 27 | - |
| `config_strings_en.lua` | 3,775 | 38,257 | English | 7 | - | 27 | - |
| `config_strings_es.lua` | 3,776 | 43,443 | Spanish | 7 | - | 27 | - |
| `config_strings_fr.lua` | 3,777 | 41,311 | French | 8 | - | 27 | - |
| `config_strings_it.lua` | 3,778 | 41,600 | Italian | 7 | - | 27 | - |
| `level101.lua` | 6,898 | 51,476 | all five | - | - | - | 17 |
| `level102.lua` | 6,917 | 50,785 | all five | - | - | - | 17 |
| `level103.lua` | 6,936 | 44,420 | all five | - | - | - | 17 |
| `level104.lua` | 6,957 | 49,956 | all five | - | - | - | 17 |
| `level105.lua` | 6,983 | 42,163 | all five | - | - | - | 17 |
| `level107.lua` | 7,007 | 45,526 | all five | - | - | - | 17 |
| `level108.lua` | 7,025 | 52,227 | all five | - | - | - | 17 |
| `level109.lua` | 7,050 | 50,042 | all five | - | - | - | 17 |
| `level110.lua` | 7,074 | 45,898 | all five | - | - | - | 17 |
| `level111.lua` | 7,087 | 59,727 | all five | - | - | - | 17 |
| `level112.lua` | 7,109 | 92,468 | all five | - | - | - | 17 |
| `level113.lua` | 7,139 | 47,380 | all five | - | - | - | 17 |
| `level114.lua` | 7,156 | 53,731 | all five | - | - | - | 17 |
| `level115.lua` | 7,181 | 53,942 | all five | - | - | - | 17 |
| `level116.lua` | 7,200 | 42,941 | all five | - | - | - | 17 |
| `level118.lua` | 7,219 | 46,158 | all five | - | - | - | 17 |
| `level119.lua` | 7,233 | 46,641 | all five | - | - | - | 17 |
| `level120.lua` | 7,344 | 53,667 | all five | - | - | - | 17 |
| `level121.lua` | 7,383 | 44,771 | all five | - | - | - | 17 |
| `level122.lua` | 7,398 | 44,060 | all five | - | - | - | 17 |
| `level123.lua` | 7,414 | 41,185 | all five | - | - | - | 17 |
| `level124.lua` | 7,425 | 42,765 | all five | - | - | - | 17 |
| `level126.lua` | 7,444 | 45,500 | all five | - | - | - | 17 |
| `level127.lua` | 7,468 | 50,821 | all five | - | - | - | 17 |
| `level128.lua` | 7,483 | 53,213 | all five | - | - | - | 17 |
| `level129.lua` | 7,499 | 54,862 | all five | - | - | - | 17 |
| `level130.lua` | 7,517 | 51,333 | all five | - | - | - | 17 |
| `level131.lua` | 7,532 | 46,936 | all five | - | - | - | 17 |
| `level132.lua` | 7,547 | 45,635 | all five | - | - | - | 17 |
| `level133.lua` | 7,559 | 54,236 | all five | - | - | - | 17 |
| `level134.lua` | 7,586 | 66,200 | all five | - | - | - | 17 |
| `level136.lua` | 7,623 | 55,290 | all five | - | - | - | 17 |
| `level137.lua` | 7,640 | 46,286 | all five | - | - | - | 17 |

- The 34 `GSTRING` keys of `global.lua` are its 33 single-string keys (see "GSTRING keys") and the list `HUD`. Its
  English copy holds all 34; each other copy holds 33, without `HIREBUM31`.
- The `GSTRING` keys of each config strings file are `ACTIONBUTTON` and the six lists `ANNOUNCE`, `COMMAND`, `CRIME`,
  `HUD`, `LEVELNAME` and `MISSIONNAME`. The German and French files also hold `BASECRIME1`.
- The 27 `TSTRING` keys are the ids 0 to 26. The 17 `RUMBLE` keys are those under "RUMBLE keys".
- The words of each config strings file are in the language of its suffix.

## Language copies

A file that carries all five languages holds five copies of each table, numbered 0 to 4. The numbering was identified
from the text of each copy and is the same in every such file on this page and on the levels page.

| Copy number | Language | Suffix of the one-language files |
| ---: | --- | --- |
| 0 | English | `_en` |
| 1 | Spanish | `_es` |
| 2 | French | `_fr` |
| 3 | Italian | `_it` |
| 4 | German | `_de` |

## Numbered and name-keyed lists

These keys hold a list of strings rather than a single string. Ids are the list's keys; Count is the number of
distinct ids over all languages. `TSTRING` is a list in its own right, not a key of another table.

| List | Ids | Count | Languages | Files |
| --- | --- | ---: | --- | --- |
| `GSTRING.ANNOUNCE` | 0-4 | 5 | 5 | the five `config_strings_<lang>.lua` |
| `GSTRING.COMMAND` | 0-7 | 8 | 5 | the five `config_strings_<lang>.lua` |
| `GSTRING.CRIME` | 0-14 | 15 | 5 | the five `config_strings_<lang>.lua` |
| `GSTRING.HUD` | 0-371, 373-388 | 388 | 5 | the five `config_strings_<lang>.lua`; ids 8, 10, 13 and 18 also in `global.lua` |
| `GSTRING.LEVELNAME` | 67 level names | 67 | 5 | the five `config_strings_<lang>.lua` |
| `GSTRING.MISSIONNAME` | 67 level names | 67 | 5 | the five `config_strings_<lang>.lua` |
| `TSTRING` | 0-26 | 27 | 5 | the five `config_strings_<lang>.lua` |
| `RUMBLE.RULES` | 1-3, 9-12, 14, 19, 23-24 | 11 | 5 | the 33 arena scripts |

## Ids by language

Every list above has the same ids in each language, except `GSTRING.HUD`.

| File | `GSTRING.HUD` ids | Count |
| --- | --- | ---: |
| `config_strings_en.lua` | 0-371, 373-388 | 388 |
| `config_strings_de.lua` | 0-371, 373-388 | 388 |
| `config_strings_es.lua` | 0-371, 373-388 | 388 |
| `config_strings_fr.lua` | 0-83, 86-371, 373-388 | 386 |
| `config_strings_it.lua` | 0-221, 223-371, 373-388 | 387 |
| `global.lua` (each language) | 8, 10, 13, 18 | 4 |

- Id 372 is in no language. French lacks ids 84 and 85; Italian lacks id 222.
- For ids 8, 10, 13 and 18 the words in `global.lua` match the config strings file of the same language. The colour
  part differs: `global.lua` gives the colour as a literal value, the config strings files as a named colour.

## Level-name keys

`GSTRING.LEVELNAME` and `GSTRING.MISSIONNAME` use the same 67 keys, level names in the form `level<N>`. The English
values agree with the titles and location labels on `ref-levels`.

| Group | Keys | Count |
| --- | --- | ---: |
| Story levels of orders 1 to 23 | `level99`, `level80`, `level87`, `level34`, `level2`, `level3`, `level5`, `level81`, `level86`, `level93`, `level31`, `level14`, `level9`, `level51`, `level52`, `level54`, `level55`, `level84`, `level82`, `level92`, `level83`, `level20`, `level11` | 23 |
| Stages of order 25 | `level60` to `level64` | 5 |
| Order 24 | `level95` | 1 |
| Rumble arenas of order 26 | `level101` to `level137` | 37 |
| No level record | `level7` | 1 |

- The mission name of `level95` is an empty string in all five languages; its level name is not empty.
- `level7` has no record in the level list; both of its entries are present in all five languages.

## GSTRING keys

`GSTRING` keys that hold a single string rather than a list: 34 keys over the six files, 33 of them in
`global.lua`. Languages is the number of languages that have
a value for the key, over all six files. Yes means the file gives the key a value in at least one language.

| Key | Languages | `global.lua` | `config_strings_de.lua` | `config_strings_en.lua` | `config_strings_es.lua` | `config_strings_fr.lua` | `config_strings_it.lua` |
| --- | ---: | --- | --- | --- | --- | --- | --- |
| `ACTIONBUTTON` | 5 | Yes | Yes | Yes | Yes | Yes | Yes |
| `ACTIONFORMAT` | 5 | Yes | No | No | No | No | No |
| `AMBUSH` | 5 | Yes | No | No | No | No | No |
| `BASECRIME1` | 2 | No | Yes | No | No | Yes | No |
| `BICO` | 5 | Yes | No | No | No | No | No |
| `CHEAT` | 5 | Yes | No | No | No | No | No |
| `DANCE` | 5 | Yes | No | No | No | No | No |
| `DRINK` | 5 | Yes | No | No | No | No | No |
| `ELEVATOR` | 5 | Yes | No | No | No | No | No |
| `GOODTOGO` | 5 | Yes | No | No | No | No | No |
| `GTGPOLICE` | 5 | Yes | No | No | No | No | No |
| `HIREBUM31` | 1 | Yes | No | No | No | No | No |
| `KISS` | 5 | Yes | No | No | No | No | No |
| `LOSTTAG` | 5 | Yes | No | No | No | No | No |
| `NULL` | 5 | Yes | No | No | No | No | No |
| `PRESSANDHOLDFORBASE` | 5 | Yes | No | No | No | No | No |
| `PRESSANDHOLDFORDETAIL` | 5 | Yes | No | No | No | No | No |
| `PRESSANDHOLDFORFILL` | 5 | Yes | No | No | No | No | No |
| `PRESSANDHOLDFOROUTLINE` | 5 | Yes | No | No | No | No | No |
| `PRESSTOCLOSE` | 5 | Yes | No | No | No | No | No |
| `PRESSTOCUTLOCK` | 5 | Yes | No | No | No | No | No |
| `PRESSTOGETIN` | 5 | Yes | No | No | No | No | No |
| `PRESSTOHITTHESWITCH` | 5 | Yes | No | No | No | No | No |
| `PRESSTOOPEN` | 5 | Yes | No | No | No | No | No |
| `PRESSTOPICKUP` | 5 | Yes | No | No | No | No | No |
| `PRESSTOPLANTLOOT` | 5 | Yes | No | No | No | No | No |
| `PRESSTOTALK` | 5 | Yes | No | No | No | No | No |
| `PRESSTOUSE` | 5 | Yes | No | No | No | No | No |
| `QUIT` | 5 | Yes | No | No | No | No | No |
| `RICO` | 5 | Yes | No | No | No | No | No |
| `SIT` | 5 | Yes | No | No | No | No | No |
| `SMOKE` | 5 | Yes | No | No | No | No | No |
| `YEL_TIMER` | 5 | Yes | No | No | No | No | No |
| `YICO` | 5 | Yes | No | No | No | No | No |

- `HIREBUM31` has an English value only. `BASECRIME1` has a French and a German value, one in each of those files.
- The value of `NULL` is an empty string in all five languages.
- Each config strings file also names `ACTIONFORMAT`, `BASECOMMAND` and `BASECRIME`, but none of the three holds a
  value in the table the file leaves. `BASECOMMAND` and `BASECRIME` do not appear in `global.lua`.

## LABEL keys

Single strings, all in `global.lua`, in all five languages. 9 keys.

| Key | Languages |
| --- | --- |
| `BONG` | 5 |
| `BTAG` | 5 |
| `HAT` | 5 |
| `LOOT` | 5 |
| `MEAT` | 5 |
| `PLUMBING` | 5 |
| `STEREOS` | 5 |
| `TAG` | 5 |
| `VOODOO` | 5 |

## RUMBLE keys

Single strings, in all five languages, in each of the 33 arena scripts. Each language's value of each key, and each
id of `RUMBLE.RULES`, is the same in all 33 files. 17 keys: the 16 below and the list `RULES`.

| Key | Languages |
| --- | --- |
| `DRAW` | 5 |
| `G` | 5 |
| `KILLS` | 5 |
| `LOSE` | 5 |
| `P1` | 5 |
| `P2` | 5 |
| `PB` | 5 |
| `REVIVESPAWN` | 5 |
| `TIME` | 5 |
| `TURFWAR_ATTACK` | 5 |
| `TURFWAR_DEFEND` | 5 |
| `TURFWAR_STOREDAMAGE` | 5 |
| `WHEELCHAIR_ONE` | 5 |
| `WHEELCHAIR_TWO` | 5 |
| `WINS` | 5 |
| `WINS_WHEELCHAIR` | 5 |

## Acceptance checks

- Entry 4,105 (zero-based) of the PS2 disc's `WARRIORS.DIR` is `global.lua`, 169,601 bytes, beginning with the Lua
  4.0 signature; entries 3,774 to 3,778 are `config_strings_de.lua`, `_en`, `_es`, `_fr` and `_it`, in that order.
- `GSTRING.HUD` in `config_strings_en.lua` holds 388 ids, 0 to 388 without 372.
- `GSTRING.HUD` holds 386 ids in `config_strings_fr.lua` (no 84, 85) and 387 in `config_strings_it.lua` (no 222).
- `GSTRING.ANNOUNCE`, `COMMAND` and `CRIME` hold 5, 8 and 15 ids from 0, and `TSTRING` 27 ids (0 to 26), in each of
  the five config strings files.
- `GSTRING.LEVELNAME` and `GSTRING.MISSIONNAME` hold the same 67 level names in each language, `level7` among them.
- The English mission names stored under `level99` and `level34` are the titles of those records on `ref-levels`;
  the one under `level95` is empty.
- `global.lua` holds the 9 `LABEL` keys and 33 single-string `GSTRING` keys; all have five languages except
  `HIREBUM31`, which has an English value only.
- `GSTRING.BASECRIME1` has a value in `config_strings_fr.lua` and `config_strings_de.lua` only.
- The 33 arena scripts `level101.lua` to `level137.lua` (no `level106`, `level117`, `level125`, `level135`) hold the
  same 16 `RUMBLE` keys and the 11 `RUMBLE.RULES` ids in five languages.
- In the five-language files, copy 0 of `LABEL.MEAT` is English, copy 2 French and copy 4 German.

## Open questions

- The sweep listed `RUMBLE.WIN_STRING` with one language. No string table on the disc holds that key, though the
  arena scripts carry the name; the text shown for a Rumble win was not checked in game.
- What each numbered list shows in game (`ANNOUNCE`, `COMMAND`, `CRIME`, `HUD`, `TSTRING`, `RUMBLE.RULES`) was not
  confirmed by play.
- What the game shows in French for `GSTRING.HUD` 84 and 85, in Italian for 222, and in any language for 372.
- What the game shows for `GSTRING.HIREBUM31` outside English and for `GSTRING.BASECRIME1` outside French and German.
- Why the `RUMBLE.RULES` ids skip 4 to 8, 13, 15 to 18 and 20 to 22.
- The meaning of each markup tag in the text values is not given here.
- The Xbox disc's copy of these scripts was not compared.
