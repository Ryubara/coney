# The debug menus

Coney has a debug menu of its own, in the spirit of the trainer menus players know from the GTA games: one place to
pause and step the game, change tuned values while it runs, call any script binding with arguments you pick, run Lua in
the game's script state and watch the pad. It is for testing and for messing around, from a gamepad in hand.

The original has no debug menu, no free camera and no debug pad handler
([Debug features](../research/debug.md#not-present)), so nothing here reimplements the original: none of this code
carries an `@orig` tag, and nothing in it changes how the game plays while the menu is closed.

## One model, rendered by front ends {#one-model}

Every feature of the menus is defined once, in `src/debug/`, as data: the menu tree (pages of items), the tunables
registry, the native signature table and the actions with their callbacks. A front end is a generic renderer of that
model: it has one way to draw each kind of item and no feature logic of its own, so a feature added to the model
appears in every front end with no front-end code.

| Part | Where | What it is |
| --- | --- | --- |
| The model | `src/debug/menu_model.h` | pages of items of eight kinds, pins, plotted channels |
| The session | `src/debug/debug_session.h` | the pages, the state they share, the input gate |
| The pad menu's state | `src/debug/menu_navigator.h`, `pad_menu_input.h` | breadcrumb, cursor, typing, hold-to-repeat |
| The pad menu's renderer | `src/gui/debug_menu_view.h`, `debug_text_painter.h` | the in-game list over the game's screen |
| The platform glue | `src/platform/debug_menus.h` | draws the pad menu at the end of every frame |
| The developer overlay | `src/platform/imgui_model_view.h`, `imgui_overlay.h` | the same model in Dear ImGui windows |

The item kinds are: **action** (run a callback), **toggle**, **number** (a slider with a range and a step, whole or
real), **choice** (one of a list), **text** (typed text or a number, with an optional history), **submenu**,
**watch** (a live read-only value, optionally with a plotted channel) and **log** (read-only lines).

All of it is built apart from the game, in the `coney_debug` and `coney_devtools` libraries, and is in every build
with the debug tools (`dev`, `playtest`, `ci`); the release build a player gets leaves it out
([The debug tools and the game](building.md#debug-tools)).

There are two front ends. The **pad menu** is a trainer-style list drawn in the game's own font, driven by the pad
alone, in every run including headless ones; it is the one to use with a gamepad in hand, on a television, and in
scripted tests. The **developer overlay** shows the same pages as Dear ImGui windows for the mouse and keyboard, in a
windowed run only; it is the one to use at a desk, for many values at once, searching the bindings, plots and typing
Lua. Both act on the same session at the same time: a value changed in one shows at once in the other, and a pin made in
one is in the other's Favourites.

## Opening it and the controls {#controls}

Press **L3 and R3 together** (both sticks in) to open and close the menu; on a keyboard, **F4** (or F and H together).
No control of the retail game uses that chord, and the cheat codes are single buttons, so it never collides with play.
The game never sees the chord, and while the menu is open the game sees port 1 with nothing pressed: menu input never
reaches gameplay. When the menu closes, the buttons still held (the circle that closed it) stay hidden from the game
until you let go of them. Port 2 always reaches the game.

The menu is driven entirely by the pad:

| Pad | Does |
| --- | --- |
| d-pad or left stick up / down | the previous / next item; held, it repeats and speeds up |
| d-pad or left stick left / right | lower / raise the value; held, the change grows (1, 2, 5, then 10 steps) |
| L2 held / R2 held | fine steps (a tenth) / coarse steps (ten) while changing a value |
| cross | choose: run an action, flip a toggle, open a submenu, start typing a text or a number |
| circle | back a page; on the first page, close the menu |
| square | pin the item to Favourites (or unpin it) |
| triangle | reset the value to its default |
| L1 / R1 | a page of items up / down |

The left stick counts as a direction past half its travel, after the pad's own dead zone, so a firm push of about three
quarters moves the cursor. Hold-to-repeat counts simulation steps, not real time: a direction fires at once, again after
12 steps (0.4 s), then every 4 steps, every 2 after 1.5 s and every step after 3 s.

**Typing with the pad.** Cross on a text item (or on a number, to type it exactly) starts typing. Up and down turn the
character under the cursor through the letters, digits and punctuation; left and right move the cursor, and right past
the end adds a character (a copy of the last, so runs of digits are quick); square deletes; L1 and R1 recall older and
newer lines of the item's history (the Lua console keeps one); cross enters the text and circle cancels.

The breadcrumb on the title bar shows where you are, the counter how far down the page; every page remembers its
cursor for the next visit. The footer shows the item's help, the last message, a plot of a watched channel and the
page's log lines.

## The developer overlay {#the-developer-overlay}

**F1** shows and hides the overlay (a windowed run only; it starts hidden). A menu bar lists the pages; each opens as a
window, and the windows that were open come back in the next run, with their places, from `coney-imgui.ini` in the
config folder next to the tunables file. Every window has a filter box over its items: a submenu whose name matches
shows everything under it, and one whose items match opens to show them, so typing `Ped` in the Natives window shows
the matching bindings in every category. *Refresh* makes the page again for lists that changed.

| Item | Widget |
| --- | --- |
| action | a button |
| toggle | a checkbox |
| number | a drag box over its range, with its units; Ctrl+click types a value |
| choice | a drop-down list |
| text | a text box: Enter commits it; Up and Down walk its history (the Lua console's box empties after each line) |
| submenu | a tree node, labelled with its tag (a category's count, a binding's status) |
| watch | the live value; *Plot* draws its channel's last 300 steps |
| log | a scrolling box that follows new lines |

Hovering an item shows its help; right-clicking it pins it to Favourites (or unpins it) and resets a number to its
default. While a text box has the keyboard, or the mouse is over a window, those events stay with the overlay: the
keyboard does not play on port 1 and Escape does not quit. The gamepads always go to the game and the pad menu.

Hidden, the overlay draws nothing and takes no input. Shown, it draws last in the frame, through Dear ImGui's OpenGL 3
backend, which saves the OpenGL state it changes and restores it afterwards, so librw's own idea of the state stays
right. `--dev-overlay N` shows it for the first N frames and then hides it, to check that: the last frame of
`--frames 150 --dev-overlay 100 --screenshot a.png` is byte-identical to the same run without `--dev-overlay`.

Dear ImGui (MIT licence) is pinned in `cmake/deps.cmake` and used only in `src/platform/`
([LEGAL.md](repo:LEGAL.md#licences)).

### Size at any resolution {#ui-scale}

The overlay is drawn at the same physical size on a 1080p, a 1440p and a 4K display. Its *scale* is the window's
height as Dear ImGui sees it (`DisplaySize.y`: the pixel height on Windows, where Coney is DPI-aware, and the height in
points on macOS, where the system's scaling is already taken out) over 1080, rounded to a step of an eighth and kept
between 1x and 4x: 1x at 1080 and below, 2x at 2160, so a 4K window has the overlay at twice the size of a 1080p one and
not a quarter of it. The scale follows the window as it is resized or moved to another display, from the next frame.

A scale sets every size of Dear ImGui's style (padding, spacing, scrollbars, rounding) and the font size, and makes the
font again at that size, so the text stays sharp and is not a stretched picture: at 1x the overlay uses Dear ImGui's
pixel-clean 13 px font, above that its scalable font at 13 px times the scale. The windows that are open are resized
and moved by the same ratio, so the layout keeps its shape. The settings file (`coney-imgui.ini`) keeps the scale its
windows were saved at, so a window saved at 1x opens at the right size and place at 2x.

The Display page's *Debug UI scale* overrides the choice: *Auto* (the rule above), or a fixed 1x, 1.25x, 1.5x, 2x,
2.5x, 3x or 4x, kept in the settings file; *Debug UI scale now* shows the scale in use. The pad menu is not affected
(it is drawn in the game's own screen space). The rule is `src/debug/ui_scale.h`, the Dear ImGui side
`src/platform/imgui_ui_scale.h`; both have tests that need no window.

## The pages {#pages}

| Page | What it has |
| --- | --- |
| Favourites | every pinned item, working as itself |
| Time | pause, step one fixed step, the time scale (0.05x to 4x, presets, reset to 1x), the frame and step counts with a plot, the real frame time, write a bug report log |
| Tunables | one page per category of the [tunables](#tunables), reset all, save and load the overrides file |
| Natives | the [script bindings](#natives) by category with their Coney status, an argument editor and a call |
| Lua console | a Lua line to run in the script state, a file to run, and the output |
| Cheats | the 27 retail cheat codes, each sent to the script's cheat callback |
| Levels | the levels to play (or the level table's, in the front end), a name to type, and the sandbox layouts |
| Missions | the story's 18 missions in order (number, title, level, checkpoints); choose a mission, then a checkpoint, to start it there |
| Movies | the disc's [movies](#movies) with what plays each; choose one to watch it full screen with its captions, then carry on where you were |
| Player | where the player is and how it moves (plotted), his character type and a change of it, freezing him, the [noclip](#noclip) and its hotkey, teleports |
| Camera | the follow camera's eye, target and distance (plotted), a reset behind the player, the free camera, its values |
| Spawner | objects put in front of the player in a sandbox or a level: a crate, a fence, a wall, a ramp, stairs, a pillar, a ball |
| AI fighters | AI humans that fight the player: a model picker (every human model, or Random) and an amount, spawn them in front of him, the engaging toggle, their state, clear them |
| Animation player | every clip the player's anim sets can play, by set and category with its length, played on him once or looping at a speed, then his own animation back ([below](#animation-player)) |
| HUD | the [in-game HUD](../research/hud.md)'s parts on and off, player 1's rage, score, money and items set (-1 gives the value back), an objective, a hint and an announcement fired |
| Debug draw | lines in the scene: collision near the player (plain or by wall class), the player's heading and velocity, the camera, places, every human's collision shapes, strike shapes and contacts |
| Entity inspector | pick a human or object (nearest, next, looked at, from a list), mark it in the scene and watch everything Coney knows of it |
| Display | the frame-stats line, the FPS counter, the frame cap and vsync (live), the player's coordinates, the GUI safe area, the logical screen, the [overlay's size](#ui-scale) |
| Audio | the output device, the master and bus volumes, a looping test tone, the voices playing (plotted) |
| Network | [network play](multiplayer.md): status, player name, port, address, Host, Join, Disconnect, the ping (plotted), loss, bytes and packets, the players, a Simulate network submenu (latency, jitter, loss, duplicates, reordering), the log |
| Input | port 1 live: buttons held, both sticks (plotted), the raw stick bytes, the triggers' pressure |
| Performance | the profiler on and off, the frame time (plotted) and every zone's and counter's mean over the last 60 frames ([Performance](performance.md#overlay)) |

**Time.** The game always advances by whole fixed 1/30 s steps, never a variable step. Paused, no step runs; *Step
one* runs exactly one. *Time scale* (0.05x to 4x, in steps of 0.05, L2 for hundredths) speeds the game up or slows it
down by running more or fewer of those steps a real second; *Reset to 1x* (or triangle on the scale) gives the game's
own speed back, and *Preset 0.1x*, *0.25x*, *0.5x*, *2x* and *4x* are one press away. A change shows as a notice, and
the frame-stats line names any scale other than 1x. *Write bug report log* writes the last 30 s of the
[run log](run-log.md#bug-reports), every level, into the log file for a bug report, and a notice says how many events
and where.

- **Slower than 1x.** Steps still fall due 30 times a real second, and only the scale's share of them runs: at 0.25x
  one in four (counted in hundredths, so a scale gives exactly its steps over a run, and the first due step after a
  change runs at once). A held step still reads the pad, so the menus keep their full speed, and the game's last step
  stays on screen, drawn as it is (not blended towards a step that has not run).
- **Faster than 1x.** The frame clock makes the scale's multiple of steps due (`FrameClock::setSpeed()`): 60 a second
  at 2x, 120 at 4x, each a whole 1/30 s step. A frame runs at most 4 steps per 1x of the scale, rounded up (16 at 4x),
  so the game keeps the scaled speed down to 7.5 frames a second; a machine too slow for that slows down instead of
  piling up steps (the cap is the guard against the "spiral of death",
  [Update and render](conventions.md#update-and-render)). With a frame cap of 30 (lockstep) a frame runs the scale's
  whole steps (1.5x: 1, 2, 1, 2).
- **Test mode** (`--headless`, `--frames`, `--input-script`, `--screenshot`) stays one due step a frame, with no
  clock: a scale above 1x has no effect there (the item's help says so), and one below 1x holds steps as above, so a
  scripted run is the same every time.
- **Sound and movies do not follow the scale.** Every sound plays at its own speed and pitch: resampling the mix
  would change the pitch, keeping the pitch would need time-stretching, and neither is worth the risk to the sound
  path for a debug tool. Sounds start when the steps that trigger them run (at 4x four times as often), and the sound
  engine's timers count the steps run, but music, speech and a movie's sound play in real time. A movie's pictures
  advance one frame per step, so at another scale they drift from its sound: watch movies at 1x.

In a window the page also shows the real time each frame took, as the frame pacer measures it (a headless run has no
real clock and leaves it out); it is only shown and plotted, never used by a step. The scale is Coney's own; the
original has none ([Debug features](../research/debug.md#not-present)).

**Cheats.** The retail cheat checker (six single buttons matched against a table, [Debug
features](../research/debug.md#cheats)) is not in Coney yet. The page does what the checker does on a match: it calls
the script's cheat callback, `DbgEnterCheat`, with the code's index. `global.lua` defines that callback when a level
loads; at the front end or in the sandbox the page reports that it is not set.

**Levels.** In the game (the story from the front end, or a `--play-level` run, which is the same game session), the
level table holds the game's levels (filled by `CfgLevelName`); choosing one jumps there at checkpoint 1 at the start
of the next frame, as the Missions page does (below). In a sandbox and the other modes run with a disc, the page lists
instead every level with a streamed world on the disc (`level0` to `level199`,
79 of them); choosing one, or typing its name, starts the game there as `--play-level` does, at the start of the next
frame, in place of the
play mode or sandbox viewer on top. *Sandbox layouts* lists the layouts in the sandbox folder
([Sandbox](sandbox.md)); choosing one plays it at the start of the next frame, in place of the play mode or sandbox
viewer on top: with Rembrandt when there is a disc for his character, else with the free camera.

**Missions.** The page lists the story's missions in order, each tagged with its level and its checkpoint count (`1. New
Blood`, `level99, 3 cp`); a mission opens a page with one action per checkpoint, which starts the level there as
`--play-level LEVEL --checkpoint N` does. In the game (the story, or a `--play-level` run) the jump waits for the start
of the next frame, then sets the
checkpoint (`SetCheckPoint`), asks the level flow for the level as `runNextMission` does, and closes everything above
the level flow: the front end's menus or a movie, or the level in play and its pause. The level flow starts the level on
its next step, from the front end or from inside a level (`StartUpFlow::jumpToLevel`). A level the level table does not
have yet (before the preloads at the legal screen fill it) is refused and the page says so. The list is
`src/debug/story_missions.cpp`, which a Python test checks against `research/missions.yaml` and the [levels
list](../references/levels.md). The hub (`level95`) between the missions is not listed: the Levels page loads it.

### Scene test {#scene-test}

Open **Levels > Scene test**, enter the level and the scene name, then choose **Load**. For example, use
`level99` and `l99_c1`. This reloads the level at checkpoint 1 through its own scene-test dispatcher: Coney runs
`global.lua`, sets `SCENETEST` to the scene-name string, then runs the level script and its start callback. The level
supplies its own cast and scene-test loop, as described in [Scene test](../research/scenes.md#scene-test).

Use a scene supported by that level's dispatcher; this entry does not make every scene in the global scene list
playable in every level. It differs from `--scene`, which starts a scene directly. Typing names changes only the
editor; **Load** requests the reload at the next frame boundary. The name is passed as a string, never evaluated as
Lua source. A later ordinary level load resets the debug flag through `global.lua` as usual.

### Movies {#movies}

The Movies page plays any of the disc's Bink movies over whatever the game is doing, and then gives the game back
exactly as it was. It is **Coney's own path**, debug tooling and not a mechanic: the original plays a movie only from
its own callers (start-up, a level's intro and outro, the front end's attract movie and EXTRAS,
[Movies](../research/movies.md#callers)), never on request. Nothing changes while you do not use it.

- **The list** is the disc's own `PSS` folder: every `PSS/<NAME>.BIK`, as `coney-tools movies list` reads it, so
  Coney keeps no copy of it (`discMovies()`, `src/gamemodes/movie_player.h`). The start-up movies come first in the
  order the game plays them, then the others by name (`TRAILER`), then the levels' by level number. Each is tagged
  with what plays it in the game, read from its name: `start-up`, `level9 intro` (`L9_IN`), `level31 outro`
  (`L31_OUT`). Without a disc, or before the game session runs (a sandbox or a viewer), the page says *No movies*.
- **Captions always**, on by default, shows a movie's ordinary captions whatever the game's subtitle option says
  (an English game has it off, so its movies show none). Off, the option decides, as for the game's own movies.
- **Choosing a movie** closes the menu and plays the movie at the start of the next frame, full screen on the movie
  player the game uses (`movies::MovieMode`): its frames, its sound at 80 %, its captions. Any button skips it, as
  any button skips the game's own movies; `LOGO`, which the game plays unskippable, plays to its end. The button that
  chose it is held back from the movie until you let go of it, as every press that closes the menu is.
- **While it plays the game is held**: the movie player sits on top of the mode stack, so the mode beneath (play, the
  pause menu, the front end) runs no step, its scripts, timers and scenes stand still (the game clock stops, as under
  any movie) and the HUD is hidden as under the pause menu. Unlike the game's own movies, which stop the music and
  every sound first, a debug movie **holds** the game's sound where it is, as the pause menu does, and gives it back
  when it ends; if something holds it already (the pause menu under the movie), it is left held.
  `--skip-movies` skips only the game's own movies: one chosen here plays anyway.
- **When it ends or is skipped** the movie player leaves the stack and the mode beneath resumes, as after the pause
  menu: the same mode, the same level state and checkpoint, the player and camera where they were, the pad going to
  the game as before. **The menu stays closed** (Coney's choice): it had to close for the movie's skip to reach the
  movie, and you come back to the game as after any movie. L3 + R3 opens it again on the Movies page with the cursor
  on the movie you watched, for the next one.

A movie cannot be chosen while another plays. A disc test (`tests/platform/disc_debug_movies_test.cpp`) drives the
page through the pad in `level99`, plays `PLOGO` to its end and skips `L99_IN`, and checks that the mode stack, the
player, the camera, the level, the checkpoint, the HUD and the sound engine's pause are the same before and after.

**Player, Camera, Spawner and AI fighters** act on the mode the player plays in (`--play-level`, or a sandbox from
Levels), through `debug::PlayControls` (`src/debug/play_controls.h`), which `platform::PlayLevelControls`
(`src/platform/play_level_controls.h`, in the debug tools) implements over the play mode's own capabilities; elsewhere
they say there is no player. A teleport drops the player onto the ground below the spot, stands him idle as a walk there
would leave him (his health and meters kept) and places the camera behind him again. Frozen, the player ignores the
pad. *Reset behind player* puts the follow camera behind the player as the scripts' `CameraReset` does, keeping what
the scripts set on it; the Follow camera tunables for the leash, pitch and look-at height apply to the next follow
camera made (a level start). The free camera flies with pad 1 as the world viewer's
does, while the player stands still and the scenery streams round the free camera. *AI fighters* spawns fighters
([Sandbox](sandbox.md#ai-fighters)) the chosen distance in front of the player, facing him, in a sandbox or a level;
*Engaging* off keeps idle fighters from taking him on.

*Spawn fighters* makes *Amount* of them (1 to 50) in one click, in rows of five centred in front of the player, each
row 1.2 m further out. *Model* picks what they look and move as: any human model the scene's character types name (the
list *Change character* below draws on, sorted, with its distinct models) or *Random*, which draws a model for each
fighter from the whole list (a generator of the menu's own with a fixed seed, so it never moves the game's random
numbers and a run with the same clicks spawns the same models). A model is loaded from the disc the first time it is
used and kept, so a first click with many models stalls briefly. Without a list (a sandbox without a disc) only
*Random* is offered and every fighter is the scene's own character. A fighter keeps its model when *Change character*
makes the fighters again.

*Clear fighters* removes only the fighters *Spawn fighters* made, through the game's own removal of a human (a level
script's `HuDelete`): never the player, the human the pad drives after a hand-over, his crew, a level script's human or
a sandbox layout's fighter. The page's *Fighters* count is the AI humans still in the world.

**Spawner.** The page adds sandbox primitives (Coney's own sizes) the chosen distance in front of the player, facing his
way, in a sandbox or a level; *Clear spawned* removes them all. The crate and the wall carry flag bit 7 (climbable by
anyone) and the fence material 30 (`LOW_FENCE`), as the parkour course's blocks and fences do, so the climbs take them
([Collision](../research/collision.md#triangles)); the ramp and the stairs are walked up. In a sandbox the page rebuilds
the sandbox with its collision and lighting. In a level the objects' solid triangles (the same ones a sandbox makes) are
added to the level's collision mesh after the disc's own, listed in its grid cells after what each cell held, so the
player, the AI, the climbs, the jumps and the cameras meet them through the queries they use for the level; they are
drawn among the scenery with the sandbox's baked light and tints. The disc's triangles, their order and their indices
are untouched, and *Clear spawned* puts the mesh back exactly as it was loaded. A level loaded again (a jump, a
mission's start) starts with nothing spawned. A script that switches the collision in a box (a door, a barrier) switches
spawned triangles inside that box too. An AI human only climbs on its route's climb legs, which come from the level's
path data, so it walks into a spawned obstacle rather than over it, as it does in a sandbox.

**Change character.** *Character type* lists every type the scripts' `CfgChar` calls configure (449 on the disc) when
the scripts set up the scene (a level's own, the story's, or level99's for a sandbox), each with the model a player of
it is drawn as: a plain alias takes its class's model, and levels 60-64 the `_a` one ([From a type to a
model](../research/characters.md#type-to-model)). *Change character* rebuilds the player where he stands, at full
health, with the camera behind him, through the play mode's own player creation: the model, its animation set and moves
(their Anim Range List), the speeds its clips give, and his class, taken as the level's starting player takes his. That
is the class's damage table scaled by his Warrior class's percentage
([Damage](../research/combat/index.md#damage-table)), and his power class: his Warrior's (58-66) for a Warriors type,
else the type's own ([Power classes](../research/characters.md#power-classes)), which sets his power meter, stamina,
hurt threshold, stun and down times; his health, the type's times his Warrior class's percentage (Cleon's 1800 × 85 % =
1530); and his Warrior class's rage ([Stats](../research/characters.md#stats)). The Player page's *Character* line shows
both classes. The AI fighters are made again where they stand, at full health, since their brains hold the player they
fought; they and the targets keep the scene's character. In a level whose scripts hold the player (every story level,
the hub included) the brains and the scripts hold him by his handle, so he is not made again but becomes the type in
place (`human::Player::changeCharacter()`): the same files, moves, speeds, class damage, power class and full health as
a player made as that type, standing where he was with the camera where it was. The scripts' record of him and his brain
take the new type too, so `HuGetCharType`, his voice and his sounds' character type follow it; the cast is left as it
is. A disc test (`tests/platform/disc_level99_change_character_test.cpp`) checks the result against a player made as the
type. The new character loads from the disc, so that frame stalls briefly.

Not taken from the configuration yet, because Coney does not model them for a player: the Warrior class's cuffing,
theft and mugging stats, the gym upgrades, the body scale (Rembrandt's 0.97), the speed class (a fallback the
original does not use in play, [Speed classes](../research/characters.md#speed-classes)), the attack table, attack
delays and brain type (an AI's, not a pad's), the voice, hat and weapon, and the movement styles that change the anim
slots.

**Display.** *FPS counter* shows, in the top-right corner under the frame stats, the frames a second, the average
frame time and the steps a second over each half second, as the frame pacer measures them (`coney::FrameRateMeter`,
whose one-second readings `--show-fps` prints); it is only shown, never read by a step. *FPS cap* and *Vsync* change
the pacing live, as `--fps-cap` and `--vsync` set it at start: a cap of 30 runs one step a frame, any other (0 shows
as *uncapped*) blends between steps ([Update and render](conventions.md#update-and-render)). A test-mode run has no
real clock, so the counter says so and there is no cap to change. *Coordinates* shows player 1's feet (x, y, z in
metres, z up) and heading (degrees, 0 faces +y) under the other corner lines, read from the play mode; with no player
it says so.

### Debug draw {#debug-draw}

**Debug draw** switches lines the play mode draws into the scene, tested against depth: the collision triangles within
a radius of the player as a wireframe (at most 6,000), a cross at his feet with his heading and velocity, the ground's
normal, the follow camera's wanted position and look-at point, and the scene's places with their headings. They work
in a level and in a sandbox alike.

The collision switches draw what the physics uses, from the engine's own sizes and rules (`src/debug/human_shapes.h`),
never a copy of them, so a change to a radius or a rule shows at once:

| Switch | Draws |
| --- | --- |
| Wall classes | the collision wireframe (in place of the plain one, same radius and budget) coloured by how the walking sweep classes each triangle (`human::wallClassOf()`, through the physics sweep's own wall rules: [Unit 2](../research/physics-bodies.md#unit-2)): **red** a wall, **yellow** a steep face too low or thin to stop a walk (a kerb, a stair's riser), **green** a floor, **blue** a ceiling, **grey** a disabled triangle, **magenta** one with no usable normal |
| Shapes | on each human: grounded, the walking sphere the wall sweep moves (**cyan**; the player's 0.4704 m, an AI human's 0.35 m, × the scale, its centre that plus 0.05 m above the feet); airborne, the push-out sphere (**magenta**, 0.5 m × the scale); the 2 m upright capsule other humans' sweeps meet (**grey**); the ground snap's ray (**green**); during a climb, the face found, its normal and the top (**orange**) |
| Shapes on all humans | on (the default): every human's shapes; off: the player's and the Entity inspector's selection's only |
| Strike shapes | the strike shapes switched on this step (**red** swept spheres) and the spine and head they are tested against (**yellow**) |
| Contacts | each normal the last walking sweep slid along (a wall, another human's capsule, an object's body): a cross where the walking sphere meets it and an arrow along the normal (**orange-red**) |

The shapes and the Entity inspector's highlight are drawn where the models are, `alpha` of the way from the step before
to the newest one (the same blend the meshes use), so at an uncapped frame rate they stay on the model: each human's
shapes are built where the engine has it, then moved by the distance from its newest feet to its drawn ones (the pose of
a strike shape is the newest step's, not blended). Sandbox targets get no shapes, on purpose: a target has no body in
Coney's physics and no strike shapes of its own, so nothing is hit by geometry. The player's attacks reach it by the
attack's hit update and its reach (a **Coney stand-in**, [Combat](../research/combat/index.md#hit-codes)), which is a
timing and a distance, not a shape that could be drawn. The Entity inspector's highlight and its watch lines are how a
target is looked at.

### Entity inspector {#entity-inspector}

The **Entity inspector** page picks one entity of the scene and shows everything Coney knows about it, live. It needs
the play mode (`--play-level`, the story, or a sandbox from Levels); elsewhere it says there are no entities. The
entities are player 1, the sandbox targets, the AI humans (fighters and the scripts' cast; deleted ones are left
out), the doors and barriers, the glass panes and the world objects whose object exists now.

| Item | Does |
| --- | --- |
| Selected | the selection's name and its distance from the player |
| Entities | how many humans and objects there are |
| Nearest to player | selects the entity nearest the player, not counting him |
| Next, Previous | steps through the list (player 1, targets, AI humans, doors, panes, objects), wrapping round |
| Looked at | selects the entity nearest the middle of the camera's view (within 12°, aimed 1 m above its feet) |
| Choose | the whole list; choosing one selects it |
| Clear | drops the selection |
| Highlight | marks the selection in the scene (a white post and diamond over it and a ring at its feet; a door's or a pane's own collision triangles in white) and names it under the corner lines, `Inspecting: AI 2 gang (3.4 m)` |
| Freeze selection | holds the selected human still: an AI human or a sandbox target is not animated, moved or acted for, its brain still runs and the others still hit it (it does not react until let go); for the player it is the Player page's *Frozen* (he ignores the pad). An object cannot be frozen, and says so in the log. The Locomotion page's *Frozen* line shows it |
| Teleport player to it | puts the player 1.5 m from it on his side, facing it |
| Log everything | prints every value of the selection to the log, `Group / name: value` a line |
| Identity ... Object | one page per group, a live watch per value |

A human's groups (`src/debug/human_watch.h`): **Identity** (role, model, character type, what drives it, brain
handle, class and gang), **Transform** (feet, heading, velocity, speed, scale, lean), **Locomotion** (gait, ground or
air, traversal, the stick and command the pad or brain wrote, the brain's move, the ground's normal and material,
landings), **Animation** (clip, animation state, the task's time, the held flags, the idle clip), **State flags** (the
human flag word decoded by name, the scripts' switches, the target state), **Meters** (health, stamina, power, rage),
**AI** (with a brain: type, goal stack top first, reaction goal, actions, target, enemies, attack slots, melee range,
thinks), **Physics** (the walking sphere, capsule and air sphere sizes, the contacts, blocked and airborne updates, the
climb) and **Combat** (mode, the attack playing, blocking, the pair, holding or grabbed, hits). A sandbox target shows
Identity, Transform, Animation, Meters and Combat. An object (`src/debug/object_watch.h`) shows Identity, Transform
and **Object**: a door's class, state, angle, hitpoints and triangles; a pane's size, broken and alarm; a world
object's spawn-record state, flags, zone, tint and money.

The inspector only reads, but for *Freeze selection*: choosing, highlighting and watching change nothing in the game,
and the selection is a number in the Debug draw options, so a test-mode run plays the same with it on.

### Noclip {#noclip}

The Player page's *Noclip* lets the player fly through walls and floors, to reach any spot of a level quickly or look
at a place no path leads to. It is Coney's own debug tool (the original has none) and changes nothing while it is off.
While it is on, pad 1 flies him and does nothing else in the game: no attacks, jumps, climbs, pick-ups or throws.

| Pad | Keyboard | Does |
| --- | --- | --- |
| left stick | W A S D | fly across the ground, relative to the camera as a walk is, at up to 6 m/s; he faces where he flies |
| R2 | 3 | rise, by how far the trigger is pulled (up to 6 m/s) |
| L2 | 1 | descend, by how far the trigger is pulled (up to 6 m/s) |
| R1 held | E | fly four times as fast (24 m/s) |
| right stick | none | turn the follow camera round him, as in play |

He has no collision, no gravity and no falling or landing, so no fall hurts him; let go of everything and he hovers.
Switched off, he is dropped from where he stands with the normal physics: on the ground if it is under his feet, else
he falls to whatever is below. A short notice, *Noclip on* or *Noclip off*, shows under the corner lines for 90 frames.

*Noclip hotkey (R3 + D-pad left)*, on by default (the page's option switches it off), makes **R3 and d-pad left
together** (keyboard: **H and the left arrow**) switch the noclip in play, whichever is pressed first, once per press.
The input gate (`src/debug/input_gate.h`) catches the chord before the game: from the moment both are held neither
reaches the game until it is let go, so the d-pad press neither enters a cheat code nor acts in a fight. (An R3 held
first reaches the game until the left completes the chord, as a lone R3 always does.) With the option off the chord is
ordinary input. It does not fire while the menu is open; `tests/platform/disc_level99_noclip_test.cpp` presses it
through pad 1 in level99. The flight is `human::Human::setFlight()` with the speeds of
`human::noclipFlight()`, stepped at the fixed 1/30 s like everything else, so a scripted run (`--input-script`) flies
the same path every time.

### Animation player {#animation-player}

The Animation player page plays any clip on the player, to look at the game's animations one at a time. It is Coney's
own debug tool (the original has none) and changes nothing until a clip is chosen. It works wherever the Player page
does: in a level (`--play-level`, the story) and in a sandbox.

| Item | Does |
| --- | --- |
| Playing | the clip playing for the page: its anim id, name, how far into it he is and its length, `loop` when it loops |
| Loop | the next clip chosen plays over and over until *Stop* (off: once) |
| Speed | multiplies the clip's own rate, 0.1 to 4 (default 1) |
| In place | the next clip's root motion neither moves nor turns him |
| Stop | ends the clip with his idle |
| one item per anim set | the set's clips by category, each category tagged with its count |

The sets are, in order: **Player** (his character's clips, with the generic ones his character leaves to them: what he
plays in the game), **Generic** (the generic character data alone, the `gen_*` clips) and then **every model a player
can be drawn as** (the models *Change character* offers), each with its own clips only. Every character on the disc has
the same skeleton ([Characters](../research/characters.md)), so every set can play on him; a clip that animated a bone
past the 34 pose entries would be left out. A model's set loads from the disc the first time it is opened, which stalls
that frame briefly; a set that fails to load says why.

A set's page lists its clips by **category**: the first word of each anim id's `ANIM_*` name from `royal.lua`
([Anim ids](../references/anim-ids.md)), in title case, with a word's forms together (MOUNTING and MOUNTED under
*Mount*, ARRESTING and ARRESTED under *Arrest*). The research defines no categories, so this grouping is Coney's choice.
Each clip is one item, `<id> <clip name> <length> s`, its length the clip's own (frames / 30); its help gives the
`ANIM_*` name, how long it plays at the set's rate (`CfgAnimSpeeds` by the id's Anim Range List flags,
[Playback rate](../research/formats/animation.md#playback-rate)) and whether it comes from the generic set.

Choosing a clip plays it on the player at once, from its first frame and with no fade, at the set's rate times
*Speed*, its clip events firing as the game's would (its sounds, for one). Once, it plays to its end, then his idle
takes over and the game chooses his animation again as usual; looping, it plays until *Stop*, or until something else
he plays replaces it (a hit, a fall). While it plays, the pad does not move him, as if he were frozen, and a lock onto
a target (the fight yard's) waits for it to end before his combat walk comes back. The clip is
played by `human::Human::playDebugClip()` (`HumanAnimator::playDebugClip()`, state `AnimState::Debug`), Coney's own
entry point, which nothing of the original's calls. Disc test: `tests/platform/disc_anim_player_test.cpp` plays a clip
in `level99` through the page and checks it ran its full length and that his idle came back, and in the sandbox lists
every set and plays a loop and clips of the generic set and of another character.

## Tunables {#tunables}

A tunable is a named, typed value a subsystem lets the menus edit while the game runs. The subsystem keeps the variable;
the registry (`src/debug/tunables.h`) keeps a pointer to it with its range, step, units, default (the variable's value
when registered) and a description. The Tunables page builds one page per category from the registry by itself.

The first tunables are the player's and the camera's researched values (`src/debug/game_tunables.cpp`):

| Category | Tunables (default) |
| --- | --- |
| Movement | stick dead zone (0.12), run threshold (0.95), acceleration (24 m/s², 0.8 m/s an update), turn limit walking, jogging, running and sprinting (20°, 18°, 18°, 16° an update), in a stance (24°), turn ease (2.0 rad) and carry (0.8), lean factor and lean factor walking (0.4, 8) |
| Body | radius (0.35 × scale), player factor (1.4286: the player's sphere is 0.35 × scale × this × scale, 0.4704 m), foot gap (0.05 m), step height (0.25 m: lower wall faces do not stop a walk), air radius (0.5 × scale) |
| Sprint | drain (20/s); the stamina maximum and refill are each human's power class's ([Stats](../research/characters.md#stats)) |
| Jump | minimum speed (3.3 m/s), up speed (5.5 m/s), climbable check (5.5 m), air turn (4° an update, Coney's choice) |
| Climb | low and high probe (0.69 m, 1.7 m), reach and running reach (1.5 m, 4.5 m), probe behind (0.4 m), fence top limit (0.25 m), fence ceiling (2.5 m), wall window (1.7-2.91 m), short wall window (0.7-1.7 m) |
| Follow camera | position lag (0.22), collision margin (0.2 m), closest after collision (0.5 m), auto-centre (on; off is the default rule), one player camera `0x0050b19c` (on), leash near and far (4.8 m, 5.3 m), pitch (13°), look-at height (1.4 m) |
| Combat | history hold (7 samples), snaps need a target (`CfgSnap`, on), `S1`'s timing (hit 2, window 6 to 15, recovery 17, end 20 updates; the other attacks keep their measured timing), grab search scale (1.25), power endurance (0.25), grab strike cost (0.2, halved for the player), power drain (15/s), rage points cap (25) and factors (1.0, 0.1), rage hold (5000 ms) (the rage's drain and decay are the player's Warrior class's), the button mash, the mugging and the stereo theft ([Combat](../research/combat/index.md#coneys-implementation), `src/debug/combat_tunables.cpp`) |
| Display | cutscene letterbox (on: the black bars over cutscenes, as the original draws them; off plays cutscenes full screen) |

The leash, pitch and look-at height apply when the camera is next placed (a level start); the others at the next step.
The game registers the Combat category at start-up beside the others. Display values are render-side and apply on
the next frame ([Enhancements: Where the settings live](enhancements.md#where-the-settings-live)).

**Determinism.** A change never lands in the middle of a step: the registry queues it, and the input gate applies the
queue between two steps (`TunableRegistry::applyPending()`). A run with the same overrides file and the same input is
the same run.

**The overrides file.** *Save overrides* writes the values that differ from their defaults, one `category/name = value`
line each (`#` starts a comment; a bool is `on` or `off`):

```text
# Coney tunable overrides (docs/guides/debug-menu.md#tunables): category/name = value
Movement/Run threshold = 0.9
Follow camera/Leash far = 4
```

Coney loads the file at start-up: the one `--tunables FILE` names, or `coney-tunables.ini` in your config folder in a
windowed run (`%APPDATA%\Coney\Coney\` on Windows, `~/.local/share/Coney/Coney/` on Linux,
`~/Library/Application Support/Coney/Coney/` on macOS). A headless run without `--tunables` reads and writes no file.
A file with a bad line stops Coney at start-up with the line's number. An override for a tunable that has not
registered yet waits for it. The file is yours: it never goes in the repository.

## Natives {#natives}

The Natives page lists every one of the game's 956 script bindings ([Script bindings](../references/bindings/index.md))
under its category, each with its Coney status:

| Status | Meaning |
| --- | --- |
| implemented | real in Coney's binding table: it does its job |
| partial | routed to a stand-in (music, movies) that logs it |
| stub | registered; returns its documented default and does nothing else |
| missing | not registered: a script's call of it is skipped |

A binding's page shows its status and the call as a script would write it, then one editor per argument, built from its
signature: a number, a whole number, a handle (a number naming a human, gang or camera), a toggle for a boolean, text
for a string, numbers separated by commas for a table, nothing for userdata. Each starts at its default. *Arguments
passed* leaves the last ones off, so their defaults apply. *Call it* calls the binding through the script VM exactly as
a script's call does (the global's function, through `LuaVm::call`), then logs the call, its results and what it wrote
into table arguments; *Recorded calls* counts the calls a recording stub kept.

Calls go to the game's script state when a game runs (with `--disc`, the front end's), otherwise to the menus' own
**sandbox state**: Coney's bindings over a fresh game state, whose menus, music and movies only log. A call there acts
on the sandbox, never on a game.

The signatures come from the masterlist: `coney-tools natives cpp` generates `src/debug/native_signatures.cpp` from
`research/bindings/*.yaml` ([coney-tools](coney-tools-references.md#natives)), and CI checks it is current.

## The Lua console {#lua-console}

*Run line* runs a line of Lua in the same script state the Natives page calls into. A scene-test flag takes a scene
name, such as `SCENETEST = 'l99_c1'` ([Scene test](../research/scenes.md#scene-test)). Coney's VM runs the game's compiled
bytecode and has no Lua compiler, so the console understands the part of Lua a console needs, evaluated
directly on the VM:

- statements separated by `;`: an assignment (`SCENETEST = 'l99_c1'`, `t.x = 2`, `t[1] = 3`) or an expression list, whose
  values are printed; `=expr` prints, as Lua's own console does;
- numbers, strings, `nil`, table constructors, globals, fields, calls `f(...)`, method calls `obj:m(...)`, unary minus
  and `not`, `+ - * / ..`, comparisons, `and`, `or` and parentheses.

Not: `if`, loops, `local` or `function` definitions. *Run file* runs a text file of such lines, or a compiled Lua 4.0
chunk, which can hold anything. The lines run are kept in a history (L1 and R1 recall them while typing).

## Fonts {#fonts}

With a disc, the pad menu draws its text in the game's own text font (`part_page0`) through a sprite batch and the 2D
pass, as the game draws its own text, so it looks at home over the game. Without a disc (the disc-free idle screen, the
sandbox) it draws in Coney's built-in 5 × 7 bitmap font (`src/graphics/bitmap_font.cpp`), whose glyphs were drawn for
Coney: no game data and no third-party font. The panels are flat translucent quads in both cases.

## Adding a feature {#adding-a-feature}

**The rule:** every debug feature (a page, an action, a tunable, a native call, a time control, a level load, and later
teleport and spawn) is defined once, in `src/debug/`, as data: pages of items, tunables, signatures, actions with
callbacks. The front ends only render that model and hold no feature logic. A feature added there appears in every
front end with no front-end code. If a feature seems to need its own front-end code, add the missing item kind or a
generic capability to the model instead, so every front end gets it. Tests check that every item kind has a renderer
in each front end and that the overlay draws every item of the session's pages (`tests/gui/debug_menu_view_test.cpp`,
`tests/platform/imgui_model_view_test.cpp`); the overlay's switch over the kinds has no default case, so a new kind
without a widget does not compile. The overlay's only extras are generic ones: the filter box, plotting any watch with
a channel, and a text box for any text item.

A tunable is one line where the subsystem has its variable:

```cpp
registry.add("Movement", "Run speed", &m_runSpeed).range(0, 20, 0.1).units("m/s").describe("Top speed on foot");
```

A page is one call with a function that fills it from the items' factories (`src/debug/menu_model.h`):

```cpp
session.model().addPage("Player", [&player](debug::MenuPage& page) {
    page.add(debug::toggleItem("God mode", [&] { return player.god(); }, [&](bool on) { player.setGod(on); }));
    page.add(debug::actionItem("Teleport to start", [&] { player.teleport(start); }));
}, "The player: position, health, model.");
```

The standard pages are added in `DebugSession`'s constructor (`src/debug/debug_session.cpp`); give a new one an
`add...Page()` function in `debug_pages.h`. A value worth plotting gets a channel
(`model.addChannel("Player/Speed", [&] { return player.speed(); })`) and a watch item naming it. Callbacks run between
simulation steps (the pad menu acts in the input gate, before the step), so they may change game state freely. Add the
page to the table above and a test that drives it through the model (`tests/debug/debug_session_test.cpp`).

## Testing it without a person {#testing}

The menu counts steps, never time, so a scripted pad drives it exactly like a player. `tests/support/debug_menu.txt`
opens it with the chord, walks to Cheats and sends a code; ctest runs it headless (`coney.debug_menu_by_pad`). For a
picture, run the same script in a window with `--screenshot` (keep screenshots outside the repository):

```sh
build/dev/src/platform/coney --frames 30 --input-script tests/support/debug_menu.txt --screenshot ../../scratch/menu.png
```

The overlay is checked the same way: its renderer runs over a bare Dear ImGui context in the unit tests, and
`--dev-overlay` with `--screenshot` checks that it leaves the frame as it found it ([above](#the-developer-overlay)).

## Still to come {#still-to-come}

- Spawning characters, once other characters move in a level; god mode, once the player has health.
- Spawning objects in a level, and path overlays from the level's path data.
- The retail cheat checker itself, after which the Cheats page gains on/off states.
