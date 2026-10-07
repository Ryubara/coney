# Asset and script architecture

The design for moving Coney off the original's file formats and onto its own asset store and script runner. The
owner's rule stands: **the move happens after the game plays 1:1**. Nothing here is built yet. This page draws the
boundaries *now*, so that the faithful work going on today does not add new ties to the PS2 formats, and so that each
later step changes one layer.

It covers four things: the asset layers and how they fit the installer, the split of the script system, what each
modernisation really buys and what it needs first, and what to do now versus later. The texture resolver and the
render backend are on [Enhancements plan](enhancements.md#foundations) (F4, F5); the Xbox side is on
[Xbox assets](../research/xbox-assets.md#coneys-implementation). This page links to them and does not repeat them.

## Where Coney stands {#where-coney-stands}

Counted on `main` at `7300c566` (2026-10-07). The `repo check` rule below ([N4](#now)) keeps the first four rows
current once it exists.

| What | Count | Where |
| --- | --- | --- |
| Legacy file layer | 7 units, 1,183 lines | `src/fileio/`: `disc`, `wad`, `wad_index`, `executable`, `file_stream`, `stream`, `reader` |
| Engine files that include a `fileio/` header | 57 (`platform` 21, `characters` 10, `world` 5, `audio` 3, `graphics` 3, `scripting` 3, 12 others) | plus 58 test files |
| Public functions and constructors that take `const io::Wad&` | 48 in 32 headers (`platform` 17 headers, `characters` 5, `audio` 2, 8 others) | the loaders' signatures |
| Direct archive or disc calls (`lookup`, `openEntry`, `index().find`/`findHash`/`entries`, `disc()`, `Disc::open`, `Wad::open`, `readExecutableWords`) | 44 in 21 engine files (`main.cpp` 9, `scene_disc.cpp` 4); 123 in 45 test files | |
| Direct reads of a disc file outside the WAD | 3: `IOP/BFW.SND` and `IOP/MUSIC.SND` (`audio/sound_stream.cpp`), `PSS/*.BIK` (`platform/ffmpeg_movie_decoder.cpp`), `SLUS_212.15` for the random table (`platform/main.cpp`) | |
| Modes that need `--disc` | 8 (`--load`, `--render-references`, `--view-txd`, `--view-sheet`, `--view-text`, `--view-world`, `--view-character`, `--play-level`) and the front end | `src/core/options.h` |
| Seams that already hide the source | 5 function objects: `ScriptSource`, `SceneRecordSource`, `CaptionSource`, `SoundFiles`, `MovieOpener` | made from the WAD in 9 places (`main.cpp` 7, `audio_output.cpp`, `play_level_scene.cpp`) |
| Format parsers that take bytes or a stream and return a Coney struct | 37 in core directories (Lua chunks, animation clips, characters, scenes, sound tables, worlds, level objects, path maps, captions, particle pages, ...) | they stay as they are |
| Files using librw (`rw::`) | 49, all in `src/platform/`; texture dictionaries, sprite sheets, world atomics and level objects come back as librw objects | |
| Script natives registered | 466 `registerFunction` calls in 31 files, 26 of them `*_bindings.cpp` | `src/scripting/` |
| Code outside `src/scripting/` that touches the VM | `debug/` 7 files, `gamemodes/` 3; at least 27 calls of `ScriptSystem` (`call`, `schedule`, `runFile`, `enterLevel`) | |
| Process-wide mutable state | 8 tuning singletons (`followTuning`, `followDefaults`, `combatTuning`, `bodyTuning`, `climbTuning`, `jumpTuning`, `locomotionTuning`, `staminaTuning`) and the tunable registry | |
| Threads | audio only (`mixer`, `pcm_stream`, `spsc_queue`, `sdl_audio_device`) and `sdl_input` | |
| Simulation inside `src/platform/` | `PlayLevelMode::update` steps the player, the humans, pick-ups, objects, scenes, streaming and lights (`play_level_*.cpp`, 2,105 lines) | |
| Per-user writes (SDL pref path) | `profiles/` (`profile_folder.cpp`), and also `coney-tunables.ini` and `coney-imgui.ini` (`debug_menus.cpp`) | |

What this means: the parsers are already clean. Bytes go in and Coney structs come out, with no WAD in sight. What is
tied to the PS2 is *how a loader finds its bytes*. 48 signatures pass the WAD itself around, and 44 calls look things
up in it by WAD name. That is the layer to cut.

## Assets {#assets}

### Layers {#asset-layers}

```text
4  Game code         gamemodes, world, characters, audio, scenes, script bindings
                     asks for engine-native data by id; never sees a file, a WAD or a disc
3  AssetStore        resolves an id against the sources in priority order; one per session
2  Typed loaders     bytes -> engine-native struct: the 37 parsers of today, unchanged
1  Sources           Overrides | Xbox | PS2 install | Disc (--disc) | Memory (tests)
0  fileio            Stream, Reader, Disc, Wad, WadIndex, Executable: only layer 1 includes
                     disc/wad/wad_index/executable; Reader and Stream stay shared byte tools
```

Rules that keep the layers apart:

- **Only sources open files.** A loader takes an `io::Stream&` or a byte span and returns a struct. It never opens a
  file by name.
- **Game code asks by id, not by path.** The id is the original's own key, so nothing has to be renamed and every
  research page still applies. For a WAD entry the key is the name hash ([Name hash](../research/name-hash.md)), for
  a disc file its path, and for a resource inside a pack its resource hash and chunk type. The Xbox resolver needs that
  last one ([Xbox assets](../research/xbox-assets.md#coneys-implementation)).
- **Each asset kind is a simulation kind or a presentation kind.** Presentation kinds (textures, movies, sounds,
  music, later models' looks) can be replaced by a mod. Simulation kinds can only be replaced in a session marked as
  modded: scripts, levels, object lists, collision, worlds' streaming data, animation clips (their timing drives
  combat), scenes, character data and executable tables. This extends the Xbox policy (assets only, never
  behaviour) to every source. The store reports a **simulation digest** over the simulation kinds it served, which
  test mode and multiplayer compare.
- **Test mode uses the PS2 data only.** `--headless`, `--input-script` and the disc tests build a store with the PS2
  source alone, so reference comparisons never see an override or an Xbox texture.
- **librw stays a renderer detail.** Presentation loaders return CPU data (an image with its palette and mip
  levels, a mesh). `src/platform/` uploads that data. Today librw both parses and uploads RenderWare streams; the
  boundary moves in two steps ([Benefits](#benefits)).

### Interface sketch {#asset-interface}

A new directory `src/assets/` (core, no OS or librw code). The names are proposals.

```cpp
namespace coney::assets {

/// A WAD entry, by the hash of `./ee_files/<name>` (docs/research/name-hash.md).
struct FileId {
    std::uint32_t nameHash = 0;
    [[nodiscard]] static FileId of(std::string_view wadName); // "level99.lua" -> its hash
};
/// A file of the disc outside the WAD: "IOP/BFW.SND", "PSS/INTRO.BIK", "SLUS_212.15".
struct DiscFileId { std::string path; };
/// A resource inside a pack, as the chunk system and the Xbox resolver key it.
struct ResourceId { std::uint32_t resourceHash = 0; std::uint32_t chunkType = 0; };

/// Where a served asset came from; logged, and part of the simulation digest.
enum class Origin : std::uint8_t { Override, Xbox, Ps2, Memory };

/// One source of game files. NotFound means "ask the next source".
class AssetSource {
  public:
    virtual ~AssetSource() = default;
    [[nodiscard]] virtual Origin origin() const = 0;
    [[nodiscard]] virtual std::expected<std::unique_ptr<io::Stream>, Error> open(const FileId& id) const = 0;
    [[nodiscard]] virtual std::expected<std::unique_ptr<io::Stream>, Error> open(const DiscFileId& id) const = 0;
    /// Every WAD entry this source holds (what playableLevelNames() and the viewers list).
    [[nodiscard]] virtual std::vector<FileId> files() const = 0;
};

/// The session's sources, in priority order, and the policy above.
class AssetStore {
  public:
    AssetStore(std::vector<std::unique_ptr<AssetSource>> sources, StorePolicy policy);
    [[nodiscard]] std::expected<Opened, Error> open(const FileId& id, AssetKind kind) const; // Opened: stream + origin
    [[nodiscard]] std::expected<std::vector<std::byte>, Error> read(const FileId& id, AssetKind kind) const;
    [[nodiscard]] std::expected<Opened, Error> open(const DiscFileId& id, AssetKind kind) const;
    [[nodiscard]] bool has(const FileId& id) const;
    [[nodiscard]] std::uint64_t simulationDigest() const;
};

} // namespace coney::assets
```

Loaders keep their parsers and change only their first parameter. For example,
`loadSoundBank(const io::Wad&, name, ...)` becomes `loadSoundBank(const assets::AssetStore&, name, ...)`. The five
existing seams get store-based factories (`storeScriptSource(store)` and so on) in place of `wadScriptSource(wad)`.
Resource-level replacement (an Xbox texture for a PS2 dictionary) is a second interface, `ResourceResolver`, that
the texture loader asks after it has parsed the PS2 dictionary, as F4 describes. Keeping it out of `AssetSource` means
whole-file sources stay simple.

### Sources {#sources}

| Source | Reads | Built from | When |
| --- | --- | --- | --- |
| `DiscSource` | a mounted disc, a copy of its files, or an ISO image | today's `io::Disc` + `io::Wad`, unchanged | `--disc` (development, CI disc tests) |
| `Ps2InstallSource` | `<install>/game/ps2/`: the disc's files copied as they are | the same `Disc` + `Wad` over a folder; `Disc` already reads "a copy of its files" | the installed game |
| `XboxSource` | `<install>/game/xbox/`: `XBoxWad.idx`, the volumes and the movies, copied | a C++ port of `coney-tools xbox` (XDVDFS reader not needed after install) | after 1:1, presentation kinds only |
| `OverrideSource` | `<install>/overrides/`, loose files by the extract tool's names | open-format loaders (PNG, WAV, BIK) | after 1:1 |
| `MemorySource` | byte buffers | tests | now, with the store |

Because the PS2 install keeps the original files, it is the same code as `--disc`. The engine can play from an
installed folder as soon as the store exists, with no new parser.

### The install folder {#install-folder}

The owner's decision (2026-10-07): **portable**. Everything lives in the folder the player picks in the installer.
Only save data goes to the per-user location (SDL's pref path). No disc or ISO is needed after install. The Xbox ISO
is preferred for assets, and the PS2 disc fills the gaps and is always the behaviour reference.

```text
<install>/                      picked by the player; movable as a whole
  coney(.exe), licences
  game/ps2/                     the PS2 disc's files as they are (WARRIORS.DIR, WARRIORS.WAD, IOP/, PSS/, SLUS_212.15)
  game/xbox/                    optional: the Xbox disc's archive and movies as they are
  overrides/                    mods and texture packs
    packs/<pack>/               a texture pack as `coney-tools` exports it: manifest + images, keyed by texture hash
    textures/<dictionary>/<texture>.png   loose files: the same names as `coney-tools extract` writes
    audio/sounds/<name>.wav     audio/music/<track>.wav
    movies/<name>.bik
    files/<wad name>            power users: a whole WAD entry replaced (simulation kinds mark the session modded)
  open/                         optional modding kit: `coney-tools extract` output (PNG, WAV, Lua chunks, index)
  index/
    install.json                sources, disc SHA-1s, Coney version, per-type counts and digests
    xbox-resources.bin          cache of the Xbox resource index (resource hash, chunk type -> location)
  config/                       proposed: tunables, overlay layout (see Maintainer decisions)
<SDL pref path>/profiles/       save data only
```

- **The installer** is a front end over one library call per step: copy the disc's files and check them against
  the known SHA-1s, write `index/install.json`, and optionally build the Xbox index and the modding kit. The first
  version can be a `coney-tools install` command next to `extract`. The installer study track owns the launcher
  ([coney-tools: extract](coney-tools.md#extract) owns the names).
- **Name and lookup per kind**:

| Kind | Order | Note |
| --- | --- | --- |
| Textures | overrides (packs by the original texture's hash; an entry matching no texture on the disc is ignored), Xbox (by resource hash, equal counts only), PS2 | F4; [Texture packs](enhancements.md#packs) |
| Movies | overrides, Xbox `_hd`, PS2 | |
| Sounds, music | overrides, PS2 | Xbox mapping is open ([Xbox assets](../research/xbox-assets.md#coneys-implementation)) |
| Fonts, HUD and particle sheets | overrides, PS2 | presentation |
| Scripts, levels, object lists, worlds, collision, animations, scenes, characters, executable tables, game text | PS2 (overrides only in a modded session) | simulation kinds |

- **Command line.** `--data <install>` opens the install folder, and `--disc` stays for development and CI. With
  neither, Coney looks for `game/ps2/` next to its executable. All three build the same store.

### The legacy loaders, one by one {#legacy-loaders}

What changes for each original format. *Seam* means a function object already hides the source; only its factory
moves. *Signature* means the loader takes the WAD and its first parameter changes.

| Format | Loader (file) | Parser kept | Today | Change |
| --- | --- | --- | --- | --- |
| WAD index, archive, disc | `fileio/wad`, `wad_index`, `disc` | `WadIndex::parse`, `readIsoRoot` | used everywhere | used by `DiscSource` and `Ps2InstallSource` only |
| Executable tables | `fileio/executable` (`readExecutableWords`) | yes | `main.cpp` reads the random table from the disc | `DiscFileId{"SLUS_212.15"}` through the store |
| Lua chunks, `_objs.txt`, global strings | `scripting/config_strings` (`wadScriptSource`, `loadGlobalStrings`) | `parseLuaChunk` | seam | `storeScriptSource(store)` |
| Scene list and records (`.scn`) | `scenes/scene_disc` (`loadSceneList`, `wadSceneSource`) | `SceneList::parse`, `parseSceneHeader`, `parseSceneSegment` | seam + signature | store |
| Captions | `movies/disc_captions` | `parseSubtitles` | seam | store |
| Movies (Bink) | `platform/ffmpeg_movie_decoder` (`discMovieOpener`) | FFmpeg | seam, opens `PSS/` on the disc | opener over `store.open(DiscFileId)` |
| Sound tables, banks, streams | `audio/sound_data`, `sound_bank`, `sound_stream` (`DiscSoundFiles`) | `parseSoundList`, `parseIndex`, ADPCM | 2 signatures + seam | store |
| Characters | `characters/character_list`, `character_assets`, `character_data`, `character_model`, `dynamic_clips`; `human/player` | `CharacterList::parse`, `decodeCharacterModel`, `readCharacterClump` | 6 signatures | store |
| Animations | `characters/dynamic_clips` (`loadAnimResource`) | `parseAnimClip` | signature | store |
| Generic chunk containers | `gamemodes/load_entry_mode` (`loadWadEntry`), `core/chunk_system` | `loadContainer`, `loadGroupedContainer` | signature | store |
| Level objects | `world_objects/object_list`, `placed_objects_file`, `world/level_object`, `path_map` | yes | 1 signature | store |
| Worlds and level files | `platform/world_set`, `level_file`, `world_viewer_mode`, `play_scenery` | `readWorldManifest`, `readSectorPluginData`, ... | 8 signatures, librw objects | store, then [Benefits](#benefits) |
| Textures, sprite sheets | `platform/texture_dictionary`, `sprite_sheets`, `object_models`, `texture_lookup` | librw's RenderWare reader | 6 signatures, librw objects | one texture function (F4), then CPU images |
| Particles, HUD sheets, smoke | `platform/particle_renderer`, `hud_layer`, `room_smoke_overlay` | `parseParticlePage`, `parseSpriteSheetTable` | 4 signatures | store |
| Viewers, modes | `platform/*_viewer_mode`, `play_level_mode`, `front_end_scene`, `reference_renderer`, `audio_output` | | pass the WAD on | pass the store on |
| Profiles | `warriors/disk_profile_store` | `ProfileRecord::parse` | not a disc format | unchanged: save data stays in the pref path |

## Scripts {#scripts}

Today `ScriptSystem` (`src/scripting/script_system.h`) does three jobs in one object. It reads chunks through
`ScriptSource`. It owns the one Lua 4.0 state, whose bindings `installBindings` registers straight on `LuaVm`. And it
runs the schedule of delayed calls and the update function. Message handlers, animation callbacks and trigger
boxes call into the same state. The split below keeps that behaviour exactly ([Scripting: life of the Lua
state](../research/scripting.md#life-of-the-lua-state)). Only the seams move.

### Three parts {#script-parts}

```text
Script source   which program a name means: original chunk | Coney's level script | (later) a mod
     |
Runtime         Coney's Lua 4.0 VM (lua_vm, lua_chunk, lua_value, lua_libraries); one state per level, as now
     |
Native API      the 956 original natives + Coney's own, as data (name, signature, status, callback)
     |
Scheduler       when scripts run: level entry, update function, schedule, message handlers, anim callbacks, triggers
```

**Script source.** A `ScriptCatalog` maps a script's name (`level99.lua`) to a provider:

- *original*: the compiled chunk from the store (today's only path);
- *Coney*: a clean-room rewrite of that level's script, written from the level walks on
  [Scripting](../research/scripting.md#level99). Recommended form: **Lua 4.0 source text** that Coney compiles with
  its own compiler into the same `LuaProto` the VM runs. Then there is one runtime, one binding table and one
  scheduler, and a rewrite can be compared call for call with the original chunk;
- *mod*: a file from `overrides/scripts/` (this marks the session modded).

The level flow asks the catalog, never the store, so a level can switch from the original chunk to Coney's rewrite
with a one-line change in the catalog. That switch is allowed only when the parity test passes: same input script,
same natives called in the same order with the same arguments, compared on a trace.

A compiler from Lua 4.0 source to `LuaProto` replaces the console's evaluator (`debug/lua_console`, expressions and
assignments only). It also gives the console `if`, loops, `local` and `function`. A **modern Lua (5.4) runtime for
mods** would be a second runtime behind the same native API, with its own value bridge. Defer it. One runtime is
enough until mods need features that 4.0 lacks.

**Native API.** Today each `*_bindings.cpp` captures a `BindingContext` and calls `vm.registerFunction(name,
lambda)`. Bindings reach the game only through host interfaces (`BindingHost`, `AiBindingHost`, `SoundHost`,
`CrimeServices`, ...), and that is the boundary to keep. The proposed change is small:

```cpp
namespace coney::script {
/// One native, independent of any VM: what the Natives page, the masterlist and the VM all read.
struct Native {
    std::string_view name;          // "HuCreate"; Coney's own live in a `Coney` table, never as bare globals
    NativeOrigin origin;            // Original | Coney
    BindingKind status;             // implemented | partial | stub (today's BindingKind)
    NativeFunction call;            // std::span<const Value> -> std::expected<std::vector<Value>, Error>
};
/// Built once per state from the BindingContext; the runtime installs it, the debug menus list it.
[[nodiscard]] std::vector<Native> buildNatives(const BindingContext& context);
}
```

The binding bodies do not change. Only the registration moves from the VM to a list. A second runtime or a call
tracer can then wrap every native in one place. Coney's own natives go in a `Coney` table, so they can never clash
with one of the 956 original names.

**Scheduler.** Scripts run only on the simulation thread, inside a game mode's `update()`, at the points the
research fixes. Everything that starts a script (level entry, update function, due scheduled calls, message
handlers, animation callbacks, trigger events) goes through one `ScriptScheduler::run(entry)` that owns the order
and the instruction budget. Coroutines later become one more entry kind ("resume coroutine *n*"). Lua 4.0 has none,
and the research shows the original's scripts waiting only through the schedule and callbacks, never inside a call.

### Debug console needs {#console-needs}

What the console ([Debug menus: Lua console](debug-menu.md#lua-console)) needs from these parts:

- **Pick the state**: the game's state or the sandbox state, as the Natives page already does.
- **Full statements**: the source compiler above (`if`, loops, `local`, `function`); `print` captured to the console.
- **Errors with lines**: the compiler keeps line information for Coney's chunks. The original chunks have none, and
  `parseLuaChunk` drops it.
- **Introspection**: globals, natives with their status (the native list), the schedule queue, the update function.
- **A call trace**: the natives a script called this step, with their arguments. The parity tests need the same
  trace.
- **Reload**: re-run a Coney script file in the running state, outside test mode only.
- **Safety**: every console run has its own instruction budget (`LuaVmOptions::maxInstructions`), so a typed loop
  cannot hang the game.

## Benefits and prerequisites {#benefits}

An honest list: what each modernisation buys, what has to exist first, and what in today's code stands in the way.

| Goal | What it buys | Needs first | In the way today | Verdict |
| --- | --- | --- | --- | --- |
| **Multithreading** | Load and decode on worker threads (world parts, textures, sound banks): fewer hitches when streaming | The store; loaders that are pure (bytes in, struct out); decode split from GPU upload; completed loads applied at a step boundary, in request order (test mode: synchronous) | 49 librw files decode and upload in one go on the main thread; 8 tuning singletons and the tunable registry are process-wide (harmless while only the simulation thread reads them) | Worth it for **loading only**. The simulation stays on one thread, as the original's does: that keeps it deterministic and easy to compare |
| **Modern render pipeline** | F2-F5 on [Enhancements plan](enhancements.md#foundations): a post chain, SDL3 GPU (Vulkan, D3D12, Metal) | Presentation loaders that return CPU data; the simulation moved out of `src/platform/` | `PlayLevelMode::update` (platform) steps the simulation; texture dictionaries, sprite sheets, atomics and level objects come back as `rw::` objects | Do it in two steps: first a platform-side upload interface with librw still parsing; much later, Coney's own RenderWare readers. Only then can librw go |
| **Memory hardcodes** | Bigger levels, more humans for mods and multiplayer | Each limit marked as faithful (it changes behaviour) or as Coney's own; faithful limits gathered in one `Limits` struct with the original's values, forced in test mode | `kSectorPoolSize` (17,217,536: what streams in when), `kGangSlots` 32, `kGangMemberSlots` 16, `kFormationPool` 42, `kRoutePoolSize` 32, `kMaxEnemies` 16, `kLightCapacity` 512, `kMaxShards` 79 and others are `constexpr` in their headers | Raising any of them changes play ([Memory: what to keep](../research/memory.md#what-a-reimplementation-must-keep)). Gather them now as documentation; turn them into options only for mods and multiplayer |
| **Async scripting** | Level scripts written as straight lines ("wait until the fence is climbed") for rewrites and mods | The scheduler as the only way in; a VM that can suspend (today's `execute` recurses through C++ for every call); a fixed resume order | Nothing for the original's scripts, which never wait | **Defer**. Faithful scripts do not need it, and rewrites must match the original's timing anyway |
| **Multiplayer** | [Roadmap: online multiplayer](../roadmap.md#online-multiplayer) | Fixed step (done), seeded randomness (`GameRandom`, done), input scripts (done), one input path for pads and brains; simulation and presentation separate; state snapshots; matching simulation data (the store's simulation digest) | Simulation inside `src/platform/`; no snapshot of simulation state; pools sized for one player | After the whole game. The store's digest and the platform split are the parts worth preparing |
| **Open formats (PNG, WAV, glTF)** | Modders edit what they understand; overrides load without PS2 encoders | Typed loaders chosen by asset kind, not by file format; one naming scheme (the extract tool's) | Textures are 4- or 8-bit palettised, so a PNG round trip must be lossless (indexed PNG, or exact RGBA, compared on the texel hash); 9.5 GB for a full extract, mostly WAV | Overrides in open formats: yes. The engine's own data stays in the original formats: they are smaller, already parsed, and they are what the behaviour was researched on |

### Legal {#legal}

- **Converting a file does not change who owns it.** A game texture saved as PNG, upscaled, renamed or packed is
  still the game's texture. The same goes for a sound decoded to WAV or a model exported to glTF. Everything in
  `game/`, `open/`, `index/` and any override made from game assets is the player's local copy. It never goes into
  the repository, a release, a CI cache, an issue or a test fixture ([LEGAL.md](repo:LEGAL.md#no-game-data)).
- **What keeps Coney clean** is not the format. It is that Coney **ships no game data** and **extracts it from the
  player's own disc at install**, on the player's machine. Tests use synthetic fixtures (`MemorySource`), and disc
  checks print counts and hashes only. The repository may hold expected counts and SHA-1s, as `extract.py` already
  does.
- **Nothing the project hosts or links holds game pixels.** The installer and `coney-tools` download Coney and,
  for upscaling, model weights (owner, 2026-10-07), never game data. Texture packs are shared as original art or
  as recipes that each player rebuilds from their own disc; players may import a pre-made upscale, but the project
  never hosts or links one ([Texture packs](enhancements.md#packs)).

### Maintainer decisions {#maintainer-decisions}

These are open. This page assumes the first option of each, and the owner decides. Sharing texture packs is
decided ([Enhancements plan: Decided](enhancements.md#decided)).

1. **Clean-room level scripts in the repository.** Coney's Lua rewrites are written from research walks, not copied.
   May they ship under GPL-3.0-or-later, and how closely may their structure follow the original's (function
   names, order of calls)? [LEGAL.md](repo:LEGAL.md#no-game-data) forbids the game's script source, and a rewrite
   is not that source, but where the line falls is a legal call.
2. **Game text** (dialogue, subtitles, menu strings): extracted at install and never shipped (current
   recommendation; pending).
3. **Settings location.** The owner said only save data goes to the pref path, but `coney-tunables.ini` and
   `coney-imgui.ini` are written there today. Proposal: move them to `<install>/config/`, and keep the pref path
   for `profiles/` only.
4. **The modding kit** (`open/`, about 9.5 GB): optional at install, or left to `coney-tools extract`.

## Timing {#timing}

### Now {#now}

Each item is one implementer, one feature commit, and engine behaviour does not change: the same bytes reach the
same parsers. Order: N1, then N2-N5 in any order, then N6-N7. Coordinate platform files with "main", since other
implementers are working in `src/platform/` now.

| Id | Item | Files | Size |
| --- | --- | --- | --- |
| N1 | `src/assets/`: `FileId`, `DiscFileId`, `ResourceId`, `AssetSource`, `AssetStore`, `DiscSource` (over `Disc` + `Wad`), `MemorySource`; Catch2 tests with synthetic WADs; `main.cpp` builds the store after opening `--disc` and passes nothing new yet | new `src/assets/*`, `platform/main.cpp`, `tests/assets/*` | S |
| N2 | Store-based factories for the 5 seams (`ScriptSource`, `SceneRecordSource`, `CaptionSource`, `SoundFiles`, `MovieOpener`), used at their 9 construction sites; the random table read as `DiscFileId{"SLUS_212.15"}` | `scripting/config_strings`, `scenes/scene_disc`, `movies/disc_captions`, `audio/sound_stream`, `platform/ffmpeg_movie_decoder`, `platform/main.cpp`, `audio_output.cpp`, `play_level_scene.cpp` | S |
| N3 | Core loaders from `const io::Wad&` to `const assets::AssetStore&`, one directory per commit: `characters` + `human/player` (6), `audio` (2), `scenes`, `world_objects`, `gamemodes/load_entry_mode` (4) | 15 headers in core directories, their tests | 3 x S |
| N4 | `coney-tools repo check` rule: outside `src/fileio/`, `src/assets/` and a shrinking allowlist, no `#include` of `fileio/disc.h`, `wad.h`, `wad_index.h` or `executable.h` (`reader.h` and `stream.h` stay free). The allowlist starts with today's files and only ever shrinks, so the 1:1 work adds no new direct use | `python/src/coney_tools/repo_checks.py`, its test | S |
| N5 | One function for every texture load (Enhancements choice 5), called by `texture_dictionary`, `sprite_sheets` and `object_models`; F4 attaches there later | `src/platform/` texture files | S |
| N6 | Platform loaders and modes to the store, after N3 and when the platform owners agree: 17 headers, about 30 signatures (`world_set`, `level_file`, `play_level_*`, viewers, `hud_layer`, `particle_renderer`, ...) | `src/platform/` | 2 x M |
| N7 | `--data <install>` opening `<install>/game/ps2/` through the same `Disc` + `Wad` (a folder copy already works), and the default lookup next to the executable; `docs/guides/building.md` updated | `core/options`, `platform/main.cpp` | S |

Not now, but **rules for all work from today on**: no new `const io::Wad&` parameter (N4 enforces this once it
lands); a new loader takes a stream or bytes; new script entry points go through `ScriptSystem`, not `LuaVm`; new
pool limits get a `// faithful limit` or `// Coney limit` comment; nothing new goes in the pref path except save
data.

### Deferred, in order {#later}

After the game plays 1:1 ([The whole game](../roadmap.md#the-whole-game) for the core; earlier where the owner
says):

1. **Installer**: `coney-tools install` (copy, verify, `index/install.json`), then the launcher.
2. **Overrides** for presentation kinds, with PNG and WAV loaders; test mode stays PS2-only.
3. **Xbox source**: textures via the `ResourceResolver` (F4) and HD movies ([Xbox assets](../research/xbox-assets.md)).
4. **Native list** (`buildNatives`), the `ScriptScheduler` as the one entry, and the call tracer.
5. **Lua 4.0 source compiler**, then the console on it, then clean-room level scripts, one level at a time, each
   behind its parity test.
6. **Simulation out of `src/platform/`** (`PlayLevelMode::update` into a core gameplay mode; platform keeps the
   drawing), and simulation snapshots.
7. **Async loading**: decode on workers, upload on the main thread, apply at step boundaries.
8. **`Limits`** struct; the render backend (F5); Coney's own RenderWare readers if librw is to go.
9. **Coroutines** in the VM, the `Coney` natives table for mods, a modern Lua runtime if mods need it.
10. **Multiplayer**.

### Risks {#risks}

- **Merge conflicts** with the implementers active today. Change the core directories first and `src/platform/`
  last, one directory per commit, and agree each platform step with "main".
- **Behaviour drift from a source.** An override or an Xbox file reaching the simulation would change play. The
  split into simulation and presentation kinds, PS2-only test mode and the simulation digest guard against it.
- **Two naming schemes.** Overrides must use the extract tool's names, or modders will face two sets of names. The
  extract tool owns them; the engine reads them.
- **Xbox mismatches.** 65 texture resources have different counts on the two discs. They stay PS2 until mapped
  ([Xbox assets: open questions](../research/xbox-assets.md#open-questions)).
- **Rewritten scripts drifting from the original.** No level switches to a rewrite without a passing parity trace
  under the same input script.
- **The compiler's size.** Lua 4.0's grammar is small, but a compiler with correct code generation is still an
  M-to-L item. It is the gate for every clean-room script, so schedule it first among the script items.
