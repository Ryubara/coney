# Level boxes

Boxes are invisible regions placed in a level. A box notices characters coming in and going out, answers whether
something is inside it, and marks a gang's territory. This page gives where boxes are stored on the disc, the values
each box carries, the three kinds, and what happens in play when a character enters or leaves one. Every value was
read from the PS2 disc (NTSC-U, `SLUS_212.15`): the compiled Lua 4.0 scripts in `WARRIORS.WAD` (located through
`WARRIORS.DIR`) and the executable's data. The full per-level list of boxes is on the ref-boxes page; level names and
titles are on ref-levels.

## Where boxes live

| Fact | Value on the PS2 disc |
| --- | --- |
| Files that place boxes | The level scripts only: 29 story-level scripts `level<N>.lua`, 23 Rumble scripts `level<N>_<mode>_init.lua` and one unnamed script (entry 7,105) |
| Boxes placed | 974 in 53 scripts; from 1 (`level63.lua`, Stage 4) to 81 (`level51.lua`, Desperate Dudes) per script |
| Boxes in other files | None: no `.lev`, pack or other archive entry carries boxes |
| How a box is placed | One script command per box with four values: name, kind, corner, size |
| Turning a box | A second script command gives four turn values; 211 boxes are turned, each once |
| Pool sizes | Each script sets how many boxes of each kind its level may hold before it places any |

The Lua 4.0 chunks on the disc are stripped of source names and debug data; their file names are known only from
the directory hashes. Each placing command assigns its box to a script variable, usually of the same name as the box
(620 of 974); the other 354 go into script lists such as `vbAmbSound[1]`.

## Box values

| Value | Type | Meaning | On the disc |
| --- | --- | --- | --- |
| Name | text | The box's name; the game keeps the first 15 characters | 3 to 26 characters; 117 names are longer than 15 |
| Kind | integer | 0, 2 or 3 (see Box kinds); 1 makes no box | 547 of kind 0, 251 of kind 2, 176 of kind 3 |
| Corner | x, y, z | The corner with the lowest coordinates | At most one decimal |
| Size | x, y, z | Extent from the corner; the far corner is corner plus size | All above 0; by volume from 1.1, 1.1, 3 to 260, 460, 70 |
| Turn | a, b, c, d | 2 x 2 matrix applied to the footprint about its centre; 1, 0, 0, 1 when not given | Close to rotations, at most three decimals |
| Enabled | yes or no | Optional fifth value of the placing command; yes when left out | Never given; every box starts enabled |
| Camera mark | yes or no | Optional sixth value; no when left out | Never given; no box on the disc has it |

A placed box stays in its level until the level ends; the scripts have no command that removes a box. The game
looks after at most 128 boxes of all kinds at once. Every script stays well under that (81 at most).

## Box kinds

| Kind | Who counts as inside | Occupants it keeps | What it reports by itself | Boxes on the disc |
| ---: | --- | --- | --- | ---: |
| 0 | Any character | Up to 60 | Enter, leave and repeat; hit and theft reports | 547 |
| 2 | Player 1 and player 2 only | 2 (one per player) | Enter, leave and repeat | 251 |
| 3 | Nobody | None | Nothing; it only marks gang territory (see Turf boxes) | 176 |
| 1 | No box is made | none | none | 0 |

Each kind has its own pool, sized by the level script's pool list for kinds 0, 1, 2 and 3. The kind-1 entry sizes
nothing. In every script with boxes the kind-0 pool equals its kind-0 box count; the kind-2 and kind-3 pools are
equal or larger.

## Inside test

| Rule | Outcome |
| --- | --- |
| Point tested | A character's or object's position (a single point, not its body) |
| Height | Inside when the point's z lies from corner z to corner z plus size z, both ends included |
| Footprint | Inside when the point's x and y lie strictly within the four footprint corners after the turn |
| Turn | The footprint corners are moved by the turn matrix about the footprint's centre; the z range never turns |
| Matrix values | Used exactly as stored; the 21 boxes of `level92.lua` with 0.99 and 0.967 get a slightly squashed footprint |
| Points on an edge | A point exactly on a footprint edge is outside; one exactly at the top or bottom height is inside |

A turn of 0, -1, 1, 0 turns the footprint a quarter turn about its centre, so a box of size 6, 30 then covers 30 by
6 around the same centre.

## Timing

| Fact | Value |
| --- | --- |
| How often a box looks for characters | Once every 5 frames |
| Delay before an enter or leave is noticed | Up to 5 frames, about a sixth of a second at 30 frames a second |
| Repeat period while inside | 1,000 ms of game clock per box |
| Disabled boxes | Do no work at all until enabled again |

## Enter and leave

These are the messages a box of kind 0 or 2 delivers to the handler the level script set for it. The handler gets
the box and the character.

| Situation | Outcome |
| --- | --- |
| A counted character comes inside an enabled box | Message 3 (enter), once |
| The character stays inside | Message 5 (repeat) once every 1,000 ms; every occupant gets it on the same pass |
| The character goes outside | Message 4 (leave), once |
| A character inside enters the down-or-dead state the box watches | Message 4 (leave); while in that state it never counts as inside |
| A kind-2 box and a character who is not a player | Nothing; only the two players are ever counted |
| More than 60 characters near a kind-0 box | Only 60 are tracked |
| A kind-0 box is disabled | Its occupant list empties at once and no leave messages are sent |
| A kind-0 box is enabled again with characters inside | Each of them gets a new enter message |
| A kind-2 box is disabled and enabled again | It keeps its two player slots: a player still inside gets repeat messages, not a new enter; a player who left meanwhile gets a leave |

The level scripts on the disc set enter handlers on 303 boxes, leave handlers on 51 and repeat handlers on 9 (all
kind 0). Many boxes have no handler and are only asked about by the inside query.

## Hit and theft reports

| Situation | Outcome |
| --- | --- |
| A character hits an object | Every enabled kind-0 box that has a handler for message 6 and holds the attacking character's position gets message 6, with the character, the box and the object |
| A character takes an item that belongs to someone | Every enabled kind-0 box that has a handler for message 7 and holds the item's position gets message 7 |
| After either report | Characters within 30 of the place take notice, whether or not a box received the report |
| Kind-2 and kind-3 boxes | Never get these reports |

On the disc, 15 boxes have hit handlers (the vandal boxes such as `vVandal` in `level55.lua` and `vMVandal` in
`level20.lua`) and one has a theft handler (`vStealTrigger` in `level80.lua`, Real Live Bunch).

## Script commands

| Command | Uses in the scripts | Outcome |
| --- | ---: | --- |
| Place a box | 974 | Gives back the new box; kind 1 gives back the empty handle |
| Turn a box | 211 | Replaces the box's turn values |
| Enable or disable a box | 173 | Turns the box on or off; given without a flag, it turns the box off |
| Ask whether something is inside | 297 | Yes or no for the object's position at that moment; works on any object, any kind of box, enabled or not |
| Give a gang a turf box | 87 | See Turf boxes |
| Take a turf box from a gang | 23 | Takes the box out of the gang's turf |

## Turf boxes

| Fact | Outcome |
| --- | --- |
| Boxes a gang can hold as turf | Up to 8; a ninth is ignored |
| Giving a gang a box of kind 0 or 2 | Ignored; only kind-3 boxes become turf |
| A gang with no turf boxes | Every place counts as its turf |
| A gang with turf boxes | A place is its turf when any one of its turf boxes holds it |

Turf owners change during play: chapter scripts give and take turf boxes as the mission moves on. For example the
Blackout riot script gives `vTier2Turf` to two gangs and later takes it from both.

## Boxes in play

What a box does beyond its messages is up to the level script. The common uses on the disc, with what the player
sees:

| Boxes | Kind | Count | Outcome in play |
| --- | ---: | ---: | --- |
| Sound boxes `vbAmbSound_*` | 2 | 73 in 13 levels | Entering an interior switches the ambient bed to `vags/ambient/city/inside_room1_loop`; leaving, with no player still in another sound box, brings back the street bed (`vags/ambient/city/distant_traffic_loop` in `level3`, `level14` and `level93`). In `level3` and `level14` outdoor mode also goes off inside and back on outside |
| Subway sound box `vPlaySubwaySound` (`level20.lua`) | 0 | 1 | A player inside hears `vags/ambient/city/ambient_substation_loop` with effect preset 5 at 0.2; once no player is inside, the street bed returns with preset 1 at 0.1 |
| Store interior boxes `vLiquorStore`, `vPharmacyStore` (`level52.lua`) | 0 | 2 | A player inside gets the interior follow-camera setting (view value 80), outdoor mode off and the ambient bed at half volume; leaving restores the default camera, outdoor mode and full volume |
| Fixed camera boxes `vFixCam_*` (`level3.lua`) | 0 | 8 | During the rooftop chase a player entering one switches to a fixed camera, some with a short slow motion; leaving returns to the rail camera |
| Kill box `vChaseKill` (`level3.lua`) | 0 | 1 | A player falling into it sees a fade and the mission fails half a second later; pursuers falling in are killed and other Warriors are moved to warp flags |
| Fall box `vDeathGallery` (`level3.lua`) | 0 | 1 | A player entering fails the mission |
| King-of-the-hill box `vTopTier` | 0 | 4 | Each gang's count of members on top goes up on enter and down on leave |
| Finish boxes `vFinishLine`, `vFinish` | 0 | 9 | A player entering ends the Rumble round that uses it, and the scores decide the winner |
| Vandal boxes | 0 | 15 | Hits on objects inside drive the mission, for example its vandalism objective |

The sound-box setup of each level also sets effect preset 1 at 0.1. What outdoor mode changes in play is not
established.

## Entry points

| Address | Job | Outcomes |
| --- | --- | --- |
| `0x0037ac70` | Place a box from a level script | Box values |
| `0x00412a18` | Test whether a point lies inside a box | Inside test |
| `0x00412ca0` | Run the per-frame box pass | Timing |
| `0x00415378` | Track the characters in an any-character box | Enter and leave |
| `0x004134f8` | Track the players in a player box | Enter and leave |
| `0x00413018` | Report a hit made inside a box | Hit and theft reports |

## Acceptance checks

- The PS2 disc's level scripts place 974 boxes: 547 of kind 0, 251 of kind 2, 176 of kind 3 and none of kind 1.
- `level51.lua` places 81 boxes and `level63.lua` one, `VOrphTrig_11`.
- Walking a player into an interior with a sound box in Writer's Block, Set Up or Destroyed switches the street
  traffic loop to the indoor room loop; walking out brings the traffic loop back.
- In Scout's Honor, standing in the subway entrance box `vPlaySubwaySound` plays the substation loop with a
  different sound effect; stepping out brings back the street traffic loop and the normal effect.
- Entering or leaving a box is noticed within about a sixth of a second.
- A player standing still inside a box with a repeat handler triggers it about once a second.
- Non-player characters walking through a player box (kind 2) trigger nothing.
- In the Writer's Block rooftop chase, falling into the gap below the roofs fails the mission half a second after a
  fade.
- A turned box with turn values 0, -1, 1, 0 covers its footprint rotated a quarter turn about its centre.
- A box name longer than 15 characters is kept as its first 15 characters.

## Open questions

- Which of the down-or-dead states makes a character stop counting as inside a box (it is one of the states that
  also make the character count as not alive).
- What outdoor mode changes in play (reverb, ambient emitters or something else).
- What the camera mark does in play; no box on the disc sets it.
- Whether messages 3, 4 and 5 are sent at all to a box with no handler for them.
- How turf boxes shape what a gang's members do in play (where they wander, chase or gather) belongs to the gang
  behaviour pages and was not checked here.
- Which kinds of hit (blows, thrown objects, vehicles) produce the hit report.
- What happens when a script places more boxes of a kind than its pool holds; no script on the disc does.
- The Xbox disc's scripts and executable were not compared.
