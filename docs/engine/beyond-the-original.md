# Beyond the original

Every deliberate way Coney differs from the PS2 build of *The Warriors*, in one place: the limits it lifts, the picture
and sound it can do past both builds, what it takes from the Xbox build as options, the multiplayer, mods and tools the
original never had, and the option or preset that switches each. The rule behind it is [Faithful first](repo:AGENTS.md):
gameplay matches the PS2 exactly, and anything beyond it is an option, off in test mode, or a lift past the original's
range ([The original's fixed limits](../guides/conventions.md#original-limits)). This page is the list of those
exceptions, so that none is forgotten and a reader can see what a run with everything off would be.

**What is not here.** A difference the original's behaviour needs fixed (a stand-in that should become the original's
rule, a move Coney plays differently) is a gap in fidelity, not a way of going beyond it, and stays on its worklist:
[the combat differences](../research/combat-diffs.md), the stand-ins each page of [Combat](combat.md) and the research
pages name, and the issues of the `P-` and `A-` labels. Nothing on this page changes what the original does inside its
range with the option off.

## How to read the tables {#reading}

Every section is one table with the same columns:

| Column | What it holds |
| --- | --- |
| Difference | What differs, in a few words |
| The original | What the PS2 build does, with its number where it has one |
| Coney | What Coney does, with its number beside the original's |
| Switch and test mode | The option, setting or preset that switches it, and whether test mode keeps the original's behaviour |
| Status | `on main` when the code has it, or `planned` with the issue that adds it |
| Details | The page that says how it works |

The lifted limits table has one more column, **Marker**: the `path:Symbol` of each `@coney-only limit:` marker in the
sources that the entry stands for. The check below reads it. A `planned` entry names no marker, since the code has none,
and an entry `on main` names every one it has.

- **Test mode** is `--headless`, `--load`, `--frames`, `--input-script`, `--screenshot`, `--record`,
  `--render-references` and the disc tests ([Test mode](../guides/testing.md)). It keeps the original's picture (the
  *PS2* preset, [Presets](renderer.md#presets)), its sound, and its limits where gameplay inside them depends on one.
  Where an entry says "off in test mode" that is a rule, not a default: nothing beyond the
  original may change what a test sees.
- **Planned** entries marked "no issue yet" come from a maintainer decision of the limits sweep (decisions 1 to 16, the
  maintainer's answers, tracked by [#1608](issue:1608)) or from a design page; the issue is named here when it is
  filed. An engineer who claims one updates its row in the same commit as the work.

## Keeping the page {#keeping}

`coney-tools docs check` ([the check](../guides/coney-tools-build.md#beyond)) fails when a `@coney-only limit:` marker in
`src/` has no entry in the **Marker** column of the lifted limits table, and when an entry names a marker that no longer
exists. It also checks that every section below has its table with the six columns and that each status starts with
`on main` or `planned`. A change that lifts a limit or adds an improvement updates this page in the same commit
([the rule](../guides/conventions.md#original-limits)); a change that removes one takes its row out. A new option or
feature has no marker for the check to read, so it is the reviewer's to ask for its row.

## Lifted limits {#lifted-limits}

The original's sizes were fixed for the PS2's memory and processor. Coney's rule: inside the original's range the same
state as the PS2, past it Coney's own rule, marked `@coney-only limit:` beside the original's number
([the marker](../guides/conventions.md#original-limits)). The *engine limits* preset
([Engine limits](../guides/running-options.md#engine-limits)) pins every lift back at the original's number for a run
that must compare with the PS2 one to one: test mode and the disc tests play under it unless they name Coney's rule
(`--engine-limits coney`), and a run a person plays and a `--bench` lift the limits.

| Difference | The original | Coney | Switch and test mode | Status | Details | Marker |
| --- | --- | --- | --- | --- | --- | --- |
| Enemy scans an update | 5 tokens an update across all brains | 5 up to 60 scanning brains; past 60, 5 per 60 brains rounded up, at most 20 | `--engine-limits`; test mode plays under the engine limits preset, which pins 5 | on main | [Crowd rules: the enemy scans](crowd-rules.md#scans), [the enemy scan](../research/ai-fights.md#enemy-scan) | `src/core/crowd_limits.h:scanTokens` |
| Lists of humans round a walker, a help call and a trigger | 60 handles, one for each human slot | Every human in reach past 60 humans; 60 up to them | `--engine-limits`; test mode plays under the engine limits preset, which pins 60 | on main | [Crowd rules: the lists of humans](crowd-rules.md#lists), [who is found](../research/trigger-updates.md#found), [help calls](../research/ai-brain-types.md#help-calls) | `src/core/crowd_limits.h:humanListCap` |
| Gangs | 32 records, relation masks of 32 bits | Lifted by count; a crowd past a gang's size gets more gangs of at most 16 (sweep decision 1) | Inside the range as the original; the engine limits preset pins 32 | planned, no issue yet (sweep decision 1, [#1608](issue:1608)) | [Gangs](../research/ai-gangs.md#gang-record) | - |
| Humans a level's scripts create | 60 live humans; `HuCreate` returns nil past them | The same inside the range, lifted above it by count, for the story's scripts and mods alike | Inside the range as the original; the engine limits preset pins 60 | planned, no issue yet (sweep decision 2, [#1608](issue:1608)) | [HuCreate](../references/bindings/character.md#hucreate), [Creating a human](../research/characters.md#creation) | - |
| Routes | A pool of 32 routes | 32 up to 60 humans, then 32 per 60 humans | The engine limits preset pins 32 | planned, no issue yet (Coney's own crowd rule, [#1608](issue:1608)) | [Crowd rules: planned lifts](crowd-rules.md#planned), [Building a route](../research/ai-moving.md#path-planning) | - |
| Animation callbacks and the dynamic animation list | 16 callbacks; a list of 64 dynamic animations | Lifted above the original's range by count, for mods | Inside the range as the original | planned, no issue yet (sweep decision 2, [#1608](issue:1608)) | [AddAllAnimCallback](../references/bindings/character.md#addallanimcallback), [SetDynamicAnimation](../references/bindings/character.md#setdynamicanimation) | - |
| Names the scripts pass | A human's name cut at 15 characters; a callback or goal name at 31 | Kept for the game's own scripts, lifted for mods (sweep decision 2) | Inside the range as the original | planned, no issue yet (sweep decision 2, [#1608](issue:1608)) | [Human_SetName](../research/characters.md#original-structure), [the game state's callbacks](../research/crimes.md#warriors-functions), [GoalPlayDynAnimation](../research/ai-scripting.md#dyn-animation) | - |
| Mod missions | A level table of 128 records, 111 of them filled by the disc | Today a stand-in: at most 17 mod missions, the 17 free records. Whether the table grows past 128 for mods or the design's count is cut is open | Inside the range as the original | planned, no issue yet (an open question of story progress) | [The level table](../research/frontend.md#the-level-table), [Open questions](../research/story-progress.md#open-questions) | - |
| Profiles | 6 profile slots | More than 6; the save layout grows, the 8-character name stays (sweep decision 5) | Test mode keeps 6 | planned, no issue yet (sweep decision 5, [#1608](issue:1608)) | [Saving](../research/save.md) | - |
| Boss pictures on an Armies panel | 6 pairs; a seventh boss is dropped (`TargetPanel_SetBossTexture`, `0x0020fc98`) | Every boss: past 6 a new pair, a freed pair still taken first | `--engine-limits`; test mode plays under the engine limits preset, which pins 6 | on main | [Armies of the Night: In Coney](../research/armies.md#coney) | `src/hud/armies_hud.h:bossPictureCap` |
| Model and clip formats | 32 bones (34 pose entries) and 722 animation ids on the disc | Coney's own model and clip formats go past them; the disc's content keeps its numbers (sweep decision 10) | Disc characters and clips are unchanged in every mode | planned, no issue yet (sweep decision 10, [#1608](issue:1608)) | [Characters](../research/characters.md#files), [Animation](../research/formats/animation.md) | - |
| World objects around the players | Spawned when the nearest camera is within 70 m, and stored beyond the first camera's draw distance plus 10 m (the draw distance is inferred, not traced) | Coney's port reads player 1's camera only and the window's draw distance, a stand-in; it becomes every player slot's camera, and the simulation stops reading the window's draw distance (sweep decision 11) | One player keeps the original's numbers in every mode | planned, no issue yet (sweep decision 11, [#1608](issue:1608)) | [World objects](../research/objects.md) | - |
| Glass shards | Made only when an active camera is within 15 m of the pane and the pane is in a player's view; the tests are about cameras, never player 1's position; the shards draw random numbers | Coney's port tests one view's camera, player 1's, and always makes the shards when there is no view, as in a headless run, a stand-in; it gates on every player's camera the same way headless and windowed, so a headless run and a window agree (sweep decision 12); the hashes move once | Test mode uses the one gate | planned, no issue yet (sweep decision 12, [#1608](issue:1608)) | [Objects](../research/objects.md) | - |
| Small pools that overflow silently | Light records 512, trigger spheres 100, cars 18, flag net nodes 128, settle slots 64, hat slots 48 | Lifted after a count of each level's peak shows what a level needs (sweep decision 13); the glass type table holds 32, Coney's own choice, for the 19 types the scripts set | Inside the range as the original | planned, no issue yet (sweep decision 13, [#1608](issue:1608)) | [Lights](../research/lighting.md#manager), [trigger spheres](../research/scripting.md#triggers), [cars](../research/tasks.md#classes), [flag net](../references/bindings/world.md#flagnetaddlink), [settle slots](../research/physics-world.md#u15-start), [hats](../research/characters.md#hats), [glass types](../research/objects.md#glass) | - |
| Human slot order and the corpse cull | The lowest free slot below 60; with none free, one cull of corpses and a second search | The original's order inside the range, so the hashes move once toward the PS2; above 60 the crowd rules (sweep decision 9) | Inside the range as the original | planned, no issue yet (sweep decision 9, [#1608](issue:1608)) | [Task classes and pools](../research/tasks.md#classes) | - |
| The detail level | Scans four times as slowly for a human 30 m or more from the nearest camera | Left out of the simulation, as Coney's own rule written down and marked (sweep decision 15): a detail level may change only what is drawn | The same in every mode | planned, no issue yet (sweep decision 15, [#1608](issue:1608)) | [The enemy scan](../research/ai-fights.md#enemy-scan) | - |

Decision 16 of the limits sweep (polling stand-ins replaced by the original's events) is fidelity work and has no row:
it moves Coney toward the PS2, so it stays on the worklists above. So is the half of decision 9 inside the range, the
original's slot order, which its row names only to say where the crowd rules start.

## Picture and sound past both builds {#picture-and-sound}

Presentation beyond what the PS2 and the Xbox did. Every one is render-side or mixer-side only, an option with the
*PS2* look always selectable, and forced off in test mode ([Renderer](renderer.md),
[Enhancements plan](../guides/enhancements.md)).

| Difference | The original | Coney | Switch and test mode | Status | Details |
| --- | --- | --- | --- | --- | --- |
| Frame rate above 30 | 30 frames a second, one flip every second vertical blank | The simulation stays at 30 Hz; drawing blends the last two steps at any rate | `--fps-cap`, `--vsync`. Test mode and `--fps-cap 30` are lockstep, one render at alpha 1 | on main | [Frame rate](renderer.md#frame-rate), [the original's frame rate](../research/graphics.md#frame-rate) |
| VRR frame pacing | Presents 30 frames a second, one flip every second vertical blank | Vsync off and a cap just under the display's refresh rate | `--fps-cap vrr` or the saved `Display/VRR`. Test mode is lockstep | on main | [Frame rate](renderer.md#frame-rate-gaps), [the original's frame rate](../research/graphics.md#frame-rate) |
| Any window size | 640 x 448, interlaced | The window's own size, the 4:3 picture fitted inside it | The window; the screenshot and record runs keep their fixed size | on main | [Screen and timing](../guides/enhancements.md#screen-and-timing), [video mode](../research/graphics.md#video-mode) |
| The SDL3 GPU renderer | The PS2's GS | Coney's own renderer on SDL3's GPU API (Vulkan, Direct3D 12, Metal); librw kept for reading data | `--renderer gpu`, or `Display/Renderer`. Test mode takes GL3 | on main | [GPU renderer](gpu-renderer.md) |
| Render scale and internal resolution | One fixed size, 640 x 448 | 0.5 to 2 times, and a *PS2* 640 x 448 mode | `Display/Preset`; test mode draws at 1 | planned, [#1214](issue:1214) | [Screen and timing](../guides/enhancements.md#screen-and-timing), [video mode](../research/graphics.md#video-mode) |
| Any aspect | 4:3, and the original's own 16:9 mode (faithful) | 21:9, 32:9 and any other aspect, with the HUD placed by anchor | A display setting. Test mode keeps 4:3 | planned, no issue yet (design) | [Screen and timing](../guides/enhancements.md#screen-and-timing), [video mode](../research/graphics.md#video-mode) |
| Anti-aliasing | Only the video output's blend of two adjacent lines against flicker | MSAA, alpha-to-coverage, SMAA and TAA on the GPU renderer | `Anti-aliasing` of the presets. Test mode takes none | planned, [#1218](issue:1218), [#1219](issue:1219), [#1224](issue:1224) | [Anti-aliasing, scaling and HDR](renderer-scaling.md), [the PS2 video output](../research/ps2-render.md#video-output) |
| Upscaling and frame generation | None | FSR 1, FSR 3.1 as the default, optional DLSS, FSR 4 and frame generation | `Display/Preset`; test mode draws at 1 | planned, [#1225](issue:1225), [#1226](issue:1226), [#1227](issue:1227), [#1228](issue:1228), [#1236](issue:1236) | [Upscalers](renderer-scaling.md#upscalers) |
| HDR output | Standard range | HDR10 or scRGB with a paper-white setting | A live toggle. Test mode is standard range | planned, [#1215](issue:1215) | [HDR](renderer-scaling.md#hdr) |
| Lights per pixel, with the caps lifted | Per vertex; the world up to 8 lights, objects and humans 3 to 6 by distance | Per pixel at the Xbox's caps (*Enhanced*); only *High* lifts the caps, with clustered lights (sweep decision 3) | `Lights` of the presets. Test mode takes the *PS2* lights | planned, [#1221](issue:1221) | [Lights per surface](../research/lighting.md#select), [Presets](renderer.md#presets) |
| Shadows past the Xbox's | A faint blob under each human | Fitted shadow maps with soft edges (*Enhanced*), cascades and point-light shadows for fires and lamps (*High*) | `Shadows` of the presets. Test mode takes the blob | planned, [#1222](issue:1222), [#1223](issue:1223) | [Realtime lighting](../guides/realtime-lighting.md), [Lighting: humans](../research/lighting.md#humans) |
| Reflections, ambient occlusion and ray-traced shadows | None | Environment-map reflections, GTAO, probes; ray-traced shadows as a later native path | `Reflections` of the presets. Test mode takes none | planned, [#137](issue:137) for the reflections, GTAO and probes; the ray-traced shadows and AO are [#1238](issue:1238), a concept on hold | [Realtime lighting](../guides/realtime-lighting.md) |
| The draw distance and the fog | The world to 115 m for the player camera, humans to 70 m, fog from half the distance | A render-side far clip and human draw distance past them, the fog scaling with the distance (sweep decision 3) | The *Coney* preset, on by default outside test mode; *PS2* is kept and is test mode's | planned, no issue yet (sweep decision 3, [#1608](issue:1608)) | [Distance and level of detail](../research/graphics.md#lod), [World: a frame](../research/world.md#a-frame), [Assets and distance](../guides/enhancements.md#assets-and-distance) |
| Mipmaps and anisotropic filtering | 741 of 42,211 textures have mips | Mips built when textures are converted to Coney's formats, with anisotropy on the GPU renderer (sweep decision 3) | The *Coney* preset. Test mode keeps the PS2's levels | planned, no issue yet (sweep decision 3, [#1608](issue:1608)) | [Graphics: texture formats](../research/graphics.md#texture-formats), [Anti-aliasing and filtering](../guides/enhancements.md#anti-aliasing-and-filtering) |
| Texture packs | The disc's textures | A loader for packs players make, keyed by texture name; Coney ships none | The mods folder. Test mode loads none | planned, no issue yet (design) | [Texture packs](../guides/enhancements.md#packs) |
| Post effects | The game's own screen effects | Bloom from what glows, colour grading, depth of field in cutscenes, chromatic aberration | Display settings. Test mode takes none | planned, no issue yet (design) | [Lighting and post-processing](../guides/enhancements.md#lighting-and-post-processing), [Screen effects](../research/graphics.md#screen-effects) |
| The HUD and menus between steps | Drawn at each 30 Hz step | Drawn between steps with the logic unchanged (sweep decision 4) | A display setting. Test mode keeps one draw a step | planned, no issue yet (sweep decision 4, [#1608](issue:1608)) | [Update and render](../guides/conventions.md#update-and-render), [Presenting and the frame rate](../research/graphics.md#frame-rate) |
| Extra sound voices | 35 sampled voices and 5 stream channels (3 more for a few loops); admission refused past 100 and 240 live tasks | Extra mixer voices under the game's own allocation, and the thresholds scaled by the crowd above 60 humans (sweep decision 6) | On by default outside test mode; off in test mode | planned, no issue yet (sweep decision 6, [#1608](issue:1608)) | [Starting a sound](../research/sound.md#play), [Coney's sound](../research/sound.md#coneys-implementation) |

## Xbox-native features offered as options {#xbox-native}

Presentation the Xbox port does and the PS2 does not, ported from its executable (`@orig xbox`,
[Xbox tags](../guides/conventions.md#xbox-tags)). Anything either build does natively is faithful; each is an option,
off in the *PS2* preset and in test mode, never changing what the simulation computes. The *Xbox* preset is the set of
them ([Presets](renderer.md#presets)); its settings are stored and chosen, and each draws once its issue lands.

| Difference | The original | Coney | Switch and test mode | Status | Details |
| --- | --- | --- | --- | --- | --- |
| The Xbox preset's settings | The PS2 has no such set | `Display/Preset` 1 and a value for each Xbox part, stored, shown on the Tunables page and read through one function | `--preset xbox`, or the Tunables page. Test mode pins *PS2* | on main | [Built](renderer.md#presets-built) |
| The character shadow map | A faint blob; the Xbox draws a shadow map from the level's shadow light | The Xbox's map from the level's shadow light, over the world, its objects and the humans' held props | `Shadows` of the *Xbox* preset. Test mode takes the blob | planned, [#1220](issue:1220) | [Xbox lighting and shadows](../research/xbox-lighting.md) |
| The gamma ramp and brightness default | None | The level's ramp in the last pass, and the Xbox's brightness default | `Gamma ramp` of the *Xbox* preset. Test mode takes none | planned, [#1216](issue:1216) | [Xbox lighting and shadows](../research/xbox-lighting.md) |
| Per-pixel fog and the Xbox's light caps | Per vertex fog; the world up to 8 lights, objects and humans 3 to 6 by distance | Per-pixel fog; the Xbox's caps: a world 1 directional and up to 7 point lights, an object or human 2 and 3, taken in list order | `Per-pixel fog` and `Lights` of the *Xbox* preset. Test mode takes the PS2's | planned, [#1217](issue:1217) | [Xbox lighting: lights per surface](../research/xbox-lighting.md#light-slots) |
| Xbox assets from the player's second disc | The PS2 disc's assets | `coney-tools extract --xbox-disc` takes the larger textures and 720p movies where the two discs correspond | The installer's choice; the game runs the same with or without the Xbox disc | on main | [Xbox assets](../research/xbox-assets.md) |
| Xbox textures in the renderer | The PS2 texture | The Xbox texture where it is larger, the PS2's elsewhere | A display setting. Test mode takes the PS2's | planned, no issue yet (design) | [Assets and distance](../guides/enhancements.md#assets-and-distance) |
| Xbox HD movies | The PS2's movies | The 1280 x 720 movie when the Xbox disc has one | A display setting. Test mode plays the PS2's | planned, no issue yet (design) | [Assets and distance](../guides/enhancements.md#assets-and-distance) |

## Multiplayer {#multiplayer}

The original has two-player co-op on one console, which is faithful and so is no row here. Network play, and everything
past two players, is Coney's own, and no code of it carries an `@orig` tag ([Network multiplayer](../guides/multiplayer.md)).
With no session open no network code runs, and a test proves it.

| Difference | The original | Coney | Switch and test mode | Status | Details |
| --- | --- | --- | --- | --- | --- |
| Hosting and joining a session | One console | A listen server in the host's game, ENet, `coney_net`; the guest's pad is the host's pad port 2 | `--host`, `--join`, the debug menus' Network page. Test mode opens none unless a test asks | on main | [Network multiplayer](../guides/multiplayer.md#in-the-game) |
| Snapshots, events and the loading barrier | None | The server's humans in delta-compressed snapshots, the game's events and a level change's barrier sent to the guest | A session only | on main | [Snapshots](../guides/snapshots.md) |
| A guest that draws the host's game | None | A client game mode that loads the host's level and draws the snapshots through the renderer, from the guest's own follow camera on the host's player 1 (its right stick turns the view; the snapshots carry no camera) | A session only (`--join ADDR --net-view`; no test-mode difference: a run without it plays its own game) | on main | [The client game](../guides/client-world.md#client-game) |
| Joining by address and LAN discovery | None | A host on the network is found, one across the internet by its address | A session only | planned, [#562](issue:562) | [Network architecture](../guides/network-architecture.md#phases) |
| Prediction and an encrypted session | None | The joining player's own Warrior answers at once; the session is encrypted | A session only | planned, [#559](issue:559) for the prediction, [#561](issue:561) for the encryption | [Prediction](../guides/network-architecture.md#prediction) |
| A dedicated server | None | `coney-server`, a headless program that hosts a session | A session only | planned, [#564](issue:564) | [Network architecture](../guides/network-architecture.md#phases) |
| Scripted servers and clients | None | Server and client resources, with events and state between them, as a sandboxed script | A session only | planned, [#566](issue:566) | [Network architecture](../guides/network-architecture.md#scripting) |
| Three and four players | Two players | Coney's own rule set for more than two, written as a design first (sweep decision 8) | A session marked with Coney's rule set; test mode and one player never turn it on | planned, [#565](issue:565) | [Network architecture](../guides/network-architecture.md#phases), [Players](../research/characters.md#players) |
| Large sessions | None | Dozens of players, and several worlds on one server (ConeyMP) | A session only | planned, [#567](issue:567) | [Network architecture](../guides/network-architecture.md#phases) |

## Mods and Studio {#mods-and-studio}

What the original never had: ways for players to make content that runs on Coney. Coney ships only the loaders and the
documentation, never a mod, and the game without mods is the original's ([Script mods](../roadmap.md#script-mods)).

| Difference | The original | Coney | Switch and test mode | Status | Details |
| --- | --- | --- | --- | --- | --- |
| The UI widget model for mods | The game's fixed screens | A tree of screens and widgets drawn with the game's own widget classes, behind a frame label that keeps a mod's screen from passing for Coney's | Used only with a mod installed. Test mode runs none | on main | [The UI widget model](../guides/mod-ui.md) |
| Script patches | The disc's scripts | A `.cpatch` file holds a modder's differences from a script, and each player's Coney applies it to the script from their own disc | The mods folder. Test mode applies none | on main | [Script patches](../guides/script-patches.md) |
| The Lua 4.0 compiler | Scripts shipped as bytecode | A compiler from Lua 4.0 source, so a mod's scripts are written in source | `coney-tools` and Studio. Test mode runs the disc's scripts | on main | [Lua tools](../guides/studio/lua-tools.md#compiler), [Lua 4.0 bytecode](../research/formats/lua-bytecode.md) |
| The Lua 5.4 project runtime | One Lua 4.0 state | A sandboxed Lua 5.4 runtime for a project's own scripts | A project only. Test mode runs none | planned, [#1463](issue:1463) | [The Lua SDK](../guides/studio/lua-sdk.md), [Life of the Lua state](../research/scripting.md#life-of-the-lua-state) |
| The script mods loader | None | Lua files from a mods folder, loaded at documented points and calling the bindings the game's scripts call | The mods folder. The game runs the same with it empty | planned, no issue yet (design) | [The Lua SDK](../guides/studio/lua-sdk.md#manifest) |
| Mod missions | The disc's story | Missions a mod adds, opened from hub chapters and listed in the mission select | A mod only. Test mode lists none | planned, [#1469](issue:1469) | [Mission mods](../guides/studio/mission-mods.md) |
| Coney Studio | None | An editor for levels, characters, paths and scripts, with Play into the game; a prototype is built | `coney-studio`, in the builds with the debug tools | planned, [#1054](issue:1054) | [Coney Studio](../guides/studio/index.md), [the prototype](../guides/studio/prototype.md) |
| Custom content | The disc's levels and characters | Maps built in Blender (glTF) and characters from other games the player owns, converted on the player's machine and never in the repository | A project only | planned, no issue yet (design) | [Custom content](../roadmap.md#custom-content) |
| Coney Director | None | A recorder for 60-second clips and a video editor with free and tracking cameras, exporting an MP4 | A setting of the game; test mode records none | planned, [#1184](issue:1184) | [Coney Director](../guides/director/index.md) |

## Debug and player tools {#debug-and-player-tools}

Tools the original had none of, for testing and for players. The release build holds no debug tools; the player console
is its one console ([Debug tools](../guides/building.md#debug-tools)), and nothing here changes how the game plays while
it is closed.

| Difference | The original | Coney | Switch and test mode | Status | Details |
| --- | --- | --- | --- | --- | --- |
| Test mode | None | A fixed step, seeded randomness and scripted input, so a run is the same on every machine | `--headless`, `--frames`, `--input-script` | on main | [Testing](../guides/testing.md) |
| The debug menus | No debug menu or debug pad handler; the cheat codes only | A trainer-style menu from a gamepad: time controls, tunables, any binding with an argument editor, a Lua console and the cheat codes | The debug tools' builds, F1 for the overlay. Closed, nothing changes | on main | [The debug menus](../guides/debug-menu.md), [Debug features](../research/debug.md) |
| The tunables and the overrides file | Values fixed in the code | A registry of tuned values, saved in an overrides file | The Tunables page, `--tunables`. Test mode pins what drawing reads to the *PS2* values | on main | [The debug menus](../guides/debug-menu.md) |
| The sandbox | None | Test worlds of simple shapes for trying movement, flown through or played in | `--sandbox`, `--play-level sandbox:NAME`; debug tools' builds only | on main | [The sandbox](../guides/sandbox.md) |
| The player console | None | The F8 terminal with the commands every build offers | F8, in the release build too | on main | [The Coney console](../guides/console.md) |
| The console's developer commands | None | `lua <line>` runs a line of Lua in the level's script state, as the Lua console page does, so a clip's input script can call a binding | Typed in the F8 console; debug tools' builds only, refused by a console that takes player commands only | on main | [The Coney console](../guides/console.md) |
| The benchmark and the profiler | None | `--bench` measures a run and writes its summary; the profiler only measures | `--bench`, `--bench-fighters`; debug tools' builds only | on main | [Performance](../guides/performance.md) |
| The input recording | None | A run with a person's pads writes its inputs beside the run log as an input script, so a bug report carries a run that `--input-script` replays to the same state; the release build writes it too, and the log names it by file name only | `--record-input FILE` for any run; a window run writes `coney.input.txt` by default, in the release build too | on main | [The input recording](../guides/run-log.md#input-recording) |
| Clip recording | None | `--record` writes a short H.264 clip of a run, from the frames Coney draws | `--record`, which also puts Coney in test mode | on main | [Recording a clip](../guides/running-options.md#record) |
| The engine limits preset | None | A test-mode switch that pins every lifted limit at the original's number, so a run compares with the PS2's one to one | `--engine-limits original\|coney`; on in test mode and the disc tests, off for a `--bench` | on main | [Engine limits](../guides/running-options.md#engine-limits), [The original's fixed limits](../guides/conventions.md#original-limits) |
| Loading holds | The disc's own load times | The loading screens hold 3 s a level and 7 s at boot to stand for the disc; none by default outside test mode, and the legal screen stays (sweep decision 14) | A setting; test mode keeps its steps | planned, no issue yet (sweep decision 14, [#1608](issue:1608)) | [Level loading](../research/level-loading.md) |
| The installer | None | An install that sorts the disc into Coney's own archives, and loading the game from them | The install; a maintainer approves each change | planned, [#1108](issue:1108) for the installer, [#1109](issue:1109) for loading from its archives | [The .cpak format](../guides/cpak-format.md) |
| The launcher | None | A program to install, update and start Coney without a terminal | The launcher; a maintainer approves each change | planned, no issue yet | [The launcher](../guides/launcher.md) |
