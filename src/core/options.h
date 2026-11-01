// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/display_preset.h"
#include "core/error.h"
#include "core/language.h"
#include "core/log_record.h"
#include "core/renderer_choice.h"

namespace coney {

/// What `--view-text` shows: a text laid out in a font.
struct TextView {
    std::string font; ///< A font's sprite sheet resource, such as `big_font`.
    std::string text; ///< Marked-up text, or `@` and a UI string id (`@31`, `@0x1f`).
};

/// What `--start` asks for: where player 1 starts in place of the level's (or the layout's) start, and optionally
/// where the follow camera starts. Coney's own test aid, so a trace scenario can start where the original's save
/// state stands (docs/guides/research-traces.md#comparing-with-coney).
struct StartPlace {
    float x = 0.0F; ///< The feet, game axes (metres, z up); dropped onto the ground below as a start is.
    float y = 0.0F;
    float z = 0.0F;
    float headingDegrees = 0.0F; ///< 0 faces +y, anticlockwise.
    /// The camera's distance from its look-at point (metres) and the heading its view faces (degrees); unset: behind
    /// the player at the leash band's near edge, as at any start.
    std::optional<float> cameraDistance;
    std::optional<float> cameraYawDegrees;
};

/// What `--camera` asks for: player 1's view pinned to one place and orientation for the whole run, whatever camera
/// the game would show, so a frame can be matched with one of the original's
/// (docs/guides/running-play-level.md#matched-views). The values are the base camera object's
/// (docs/research/camera.md#the-base-camera-object): Coney's own test aid.
struct CameraPin {
    float x = 0.0F; ///< The eye, game axes (metres, z up): the camera's +0x10.
    float y = 0.0F;
    float z = 0.0F;
    float qx = 0.0F; ///< The orientation quaternion as the camera stores it at +0x20 (x, y, z, w).
    float qy = 0.0F;
    float qz = 0.0F;
    float qw = 1.0F;
    float fieldOfView = 60.0F; ///< Degrees, the field of view used (+0x48); 60, the base camera's, when not given.
};

/// What one `--give ITEM,COUNT` asks for: player 1 holds COUNT of inventory item ITEM (docs/references/inventory.md)
/// from the first step of play, so a capture can show what needs an item (the spray needs paint). Coney's own test aid.
struct ItemGift {
    int item = 0;  ///< The inventory item id, 0 to 22 (3 is spray paint).
    int count = 0; ///< How many, 0 to kMaxGiftCount.
};

/// The most of one item `--give` hands over.
inline constexpr int kMaxGiftCount = 9999;

/// What `--render-size` asks for: the frame's size in pixels, which then holds the logical screen exactly, shown at
/// its 4:3 display shape, as the original's 640 x 448 frame buffer does.
struct RenderSize {
    int width = 0;
    int height = 0;
};

/// The smallest `--render-size` side.
inline constexpr int kMinRenderSide = 16;
/// The largest `--render-size` side.
inline constexpr int kMaxRenderSide = 8192;

/// What `--rumble` asks for: the Rumble set-up the match starts with, written over what the Rumble menu chose when its
/// arena is confirmed (docs/guides/running-viewers.md#rumble-from-the-command-line). Coney's own developer aid: a fresh
/// profile's menu offers only 1 ON 1 and WAR PARTY.
struct RumbleLaunch {
    std::uint16_t gameType = 0; ///< The mode's `RM_*` number (RumbleSetup::kGameType).
    int arena = 0;              ///< The arena's level number (`101` for `level101`).
    std::uint16_t gangSize = 0; ///< Fighters per side (RumbleSetup::kGangSize).
    /// `--ai-player`: player 1 is handed to the AI when the countdown ends (ai::AiHumans::releasePlayer()), as the
    /// original's `Human_ReleasePlayer` (`0x0022a2a8`) does to the human it takes the pad from, so every fighter is
    /// AI-driven: the matched gang-fight scenario of docs/guides/research-traces.md#comparing-with-coney.
    bool aiPlayer = false;
};

/// One mode `--rumble` knows: its name on the command line, its `RM_*` number and the arena and gang size it plays in
/// unless `--arena` and `--gang-size` say otherwise (the ones the disc tests use).
struct RumbleModeName {
    std::string_view name;
    std::uint16_t gameType;
    int arena;
    std::uint16_t gangSize;
};

/// Every mode `--rumble` accepts, in the order the usage text lists them.
[[nodiscard]] std::span<const RumbleModeName> rumbleModeNames();

/// Resolves `--rumble`'s `type` (a name from rumbleModeNames() or a numeric game type), `arena` and `gangSize` (unset:
/// the mode's own; a number that is no listed mode's takes arena 102 and gang size 1) into a RumbleLaunch. A bad type
/// is ErrorCode::InvalidArgument whose message lists the valid names; so is an arena outside 101 to 137 or a gang size
/// outside 1 to 9.
[[nodiscard]] std::expected<RumbleLaunch, Error> resolveRumbleLaunch(std::string_view type, std::optional<int> arena,
                                                                     std::optional<int> gangSize);

/// Which lists `--render-references` renders (`--kind`).
enum class ReferenceKind : std::uint8_t {
    All,        ///< Every kind below (the default).
    Characters, ///< Only the Character List's records.
    Objects,    ///< Only the Object List's records.
    Cars,       ///< Only the six car types.
    Radar,      ///< Only the radar icons.
    Particles,  ///< Only the particle effects whose sprite is traced.
};

/// The UDP port network play uses unless told otherwise (net::kDefaultPort; options are below the network library).
inline constexpr std::uint16_t kDefaultNetPort = 27615;

/// Where `--host` listens or `--join` connects (docs/guides/multiplayer.md).
struct NetEndpoint {
    /// `--host`: the local address to listen on, empty for every interface. `--join`: the server's host name or
    /// numeric IPv4 address, never empty.
    std::string address;
    /// The UDP port. `--host` may give 0: any free port, which the run prints.
    std::uint16_t port = kDefaultNetPort;
};

/// Settings taken from the command line.
struct Options {
    // Members are grouped by alignment (8-byte, then 4-byte, then 1-byte) so the struct packs without padding
    // (clang-tidy's clang-analyzer-optin.performance.Padding): a new member goes to the group of its alignment,
    // after the others there, not beside the option it belongs to.
    // 8-byte-aligned: strings, vectors and structs that hold them.
    /// `--disc`: the player's disc, a folder (a mounted disc) or an ISO 9660 image, exactly as typed.
    std::optional<std::string> discPath;
    /// `--install DISC`: install the PS2 disc (a mounted disc, a folder of its files or an ISO image) into the folder
    /// dataPath names, then exit (docs/guides/running.md#install). Only dataPath and verifyInstall go with it.
    std::optional<std::string> installFrom;
    /// `--data DIR`: the install folder (docs/guides/asset-and-script-architecture.md#install-folder): where
    /// installFrom goes, or, without it, the install to play from: discPath is then its `game/ps2/`. Set to the install
    /// beside the executable when neither `--disc` nor `--data` is given and there is one. Cannot be combined with
    /// discPath on the command line.
    std::optional<std::string> dataPath;
    /// `--load`, in the order given: WAD entries to load through the chunk system, each a name or a `0x` hash.
    /// Requires discPath.
    std::vector<std::string> loads;
    /// `--view-txd`: a WAD entry (a name or a `0x` hash) whose texture dictionaries the viewer shows. Requires
    /// discPath; cannot be combined with `--load`.
    std::optional<std::string> viewTxd;
    /// `--view-sheet`: a sprite sheet to show, by resource name (`menu_system`) or as a WAD entry (a name or a `0x`
    /// hash). Requires discPath; cannot be combined with `--load` or `--view-txd`.
    std::optional<std::string> viewSheet;
    /// `--view-world`: a level or streamed world whose scenery the world viewer streams and shows (`level2`, `level2s`,
    /// `objarena`). Requires discPath; cannot be combined with `--load`, `--view-txd`, `--view-sheet` or `--view-text`.
    std::optional<std::string> viewWorld;
    /// `--view-character`: a character (a model name of the Character List, such as `warr_re_cv`) the character
    /// viewer shows; kDefaultViewCharacter when the option is given without a name. Requires discPath; cannot be
    /// combined with `--load` or the other viewers.
    std::optional<std::string> viewCharacter;
    /// `--anim`: the clip the character viewer plays, an anim id or a clip name. Requires viewCharacter.
    std::optional<std::string> animClip;
    /// `--play-level`: a level the player plays (`level99`). Requires discPath; cannot be combined with `--load` or
    /// the viewers.
    std::optional<std::string> playLevel;
    /// `--sandbox`: a sandbox layout to fly round with the free camera, by name (`default`, `parkour`) or as a path to
    /// a `.layout` file; kDefaultSandbox when given without one. Needs no disc; cannot be combined with `--load`, the
    /// viewers or `--play-level`.
    std::optional<std::string> sandbox;
    /// `--spawn`: the spawn point of a sandbox layout the player starts at (`--play-level sandbox:NAME`); the layout's
    /// first when unset. Requires playLevel to name a sandbox.
    std::optional<std::string> spawn;
    /// `--scene`: an in-engine scene (`l99_c1`) the play mode plays at once, with stand-ins bound to its roles and
    /// player 1 to his: Coney's test aid. Requires playLevel to name a level.
    std::optional<std::string> scene;
    /// `--give ITEM,COUNT`, repeatable: the counts player 1 holds from the first step of play (ItemGift). Requires
    /// playLevel.
    std::vector<ItemGift> gives;
    /// `--trace`: write the player's and the follow camera's state after every step of `--play-level` to this file,
    /// one CSV line per step (human::traceLine()), so feel comparisons can be repeated
    /// (docs/guides/running-play-level.md#tracing). Requires playLevel.
    std::optional<std::string> traceFile;
    /// `--trace-humans`: write one CSV row per human per step (ai::FightTracer) of a played level or a Rumble match to
    /// this file, to compare a gang fight with the original's (docs/guides/running-play-level.md#tracing). Requires
    /// playLevel or rumble.
    std::optional<std::string> humansTraceFile;
    /// `--script-trace`: write every binding call the level's scripts make, and every call into them by name, to this
    /// file, one line each with its arguments (script::ScriptSystem::traceCalls()), to see where a mission's scripts
    /// wait. Requires playLevel with a level.
    std::optional<std::string> scriptTraceFile;
    /// `--event-log`: write what a player would notice happening (script binding calls, calls into the scripts,
    /// hints shown, sounds started, humans appearing and running out of health) to this file, one CSV line per
    /// event stamped with its step (coney::events), for a differential playthrough against the original
    /// (docs/guides/research-traces.md#differential-playthroughs). Works on any path through the game.
    std::optional<std::string> eventLogFile;
    /// `--log-file`: write the run log (core/log.h) to this file, in place of `coney.log` in the user's data folder
    /// (none in test mode without it) (docs/guides/run-log.md).
    std::optional<std::string> logFile;
    /// `--log-verbose`: the run log categories whose detail lines the log file and the console keep too
    /// (`ai,combat`, or `all`); empty: info and up only.
    std::vector<logging::Category> logVerbose;
    /// `--assets`: the folder holding Coney's own assets (its `sandbox` folder of layouts and textures), in place of
    /// the `assets` folder beside the executable.
    std::optional<std::string> assetsDir;
    /// `--screenshot`: save the last frame as a PNG at this path. Requires frameLimit and a window (not headless or
    /// `--load`).
    std::optional<std::string> screenshotPath;
    /// `--draw-hash`: record what the last frame draws through the draw layer and print its counts and hash
    /// (docs/engine/renderer.md#comparing). Requires frameLimit; works with a window and headless.
    bool drawHash = false;
    /// `--record`: write a video clip of Coney's own frames (and its sound) to this MP4 file, for issues and pull
    /// requests (src/core/clip_recording.h, docs/guides/testing.md#clips). Coney's own debug tooling. Puts Coney in
    /// test mode; needs a window (not headless, `--load` or `--render-references`); the path must pass
    /// clips::checkClipPath().
    std::optional<std::string> recordPath;
    /// `--bench`: measure a `--play-level` run with the profiler (core/perf.h) and write its summary as JSON to this
    /// file at the end (docs/guides/performance.md). Coney's own debug tooling. Requires playLevel and frameLimit.
    std::optional<std::string> benchFile;
    /// `--bench-frames FILE`: also write every measured frame (its length, each zone and counter) to this file as CSV,
    /// to study the worst frames rather than the spread. Requires benchFile.
    std::optional<std::string> benchFramesFile;
    /// `--input-script`: play the pad input in this file (src/core/input_script.h) instead of reading the keyboard
    /// and gamepads: the scripted input of test mode.
    std::optional<std::string> inputScript;
    /// `--view-text FONT TEXT`: lay out and draw a text. Requires discPath; cannot be combined with `--load`,
    /// `--view-txd` or `--view-sheet`.
    std::optional<TextView> viewText;
    /// `--tunables`: the debug menus' overrides file (docs/guides/debug-menu.md#tunables), loaded at start-up and
    /// written by the Tunables page; unset: `coney-tunables.ini` in the user's config folder.
    std::optional<std::string> tunablesFile;
    /// `--profiles`: the folder the player profiles are kept in, one file per profile (docs/research/save.md#coney);
    /// unset: `profiles` in the user's data folder for a normal run, and none (profiles last the run) in test mode.
    std::optional<std::string> profilesDir;
    /// `--mesh-view`: the Debug draw page's mesh views to switch on from the first frame of play, as the debug tools
    /// parse it (debug::parseMeshViewSpec(), docs/guides/debug-menu.md#mesh-views); a test aid and the way to take a
    /// screenshot of a view.
    std::optional<std::string> meshView;
    /// `--world-edit FILE`: the debug tools' world edits (debug::parseWorldEdits(),
    /// docs/guides/running-viewers.md#world-edits) applied to the world viewer's worlds or the command line's level.
    /// Requires viewWorld, or a playLevel that is not a sandbox.
    std::optional<std::string> worldEditFile;
    /// `--render-references`: the folder to write a reference image of every character, object, car, radar icon and
    /// traced particle effect into (in its `characters`, `objects`, `cars`, `radar` and `particles` folders), then
    /// exit. Requires discPath and a window (not headless); cannot be
    /// combined with `--load`, the viewers, `--frames`, `--screenshot` or `--input-script`.
    std::optional<std::string> renderReferences;
    /// `--only`, in the order given: the characters and objects `--render-references` renders, each a model or
    /// object type name or a `0x` name hash; empty renders every one. Requires renderReferences.
    std::vector<std::string> only;
    /// `--names`: a text file of model and object type names, one per line, that `--render-references` files images
    /// under. Requires renderReferences.
    std::optional<std::string> namesFile;
    /// `--window-display VALUE`: the display the window opens on, centred (core/window_display.h): a 1-based index in
    /// the window system's list, part of a display's name, or `portrait`. Unset: the `CONEY_WINDOW_DISPLAY`
    /// environment variable, else the window system's own placement. Either one set also keeps the window from taking
    /// the keyboard focus (as --no-activate does). A debug aid that keeps agents' runs off the
    /// display a person works on (docs/guides/running-options.md#window-display).
    std::optional<std::string> windowDisplay;
    /// `--host [[ADDR:]PORT]`: host a network game from the start, listening on PORT (default kDefaultNetPort) of
    /// ADDR (default every interface). Cannot be combined with `--join`.
    std::optional<NetEndpoint> netHost;
    /// `--join ADDR[:PORT]`: join the network game at ADDR from the start. Cannot be combined with `--host`.
    std::optional<NetEndpoint> netJoin;
    /// `--net-name NAME`: the player name sent when joining or hosting (the Network page's Player name); main checks
    /// it is a valid one (1 to 32 printable ASCII characters).
    std::optional<std::string> netName;
    // 4-byte-aligned: ints and structs of floats or ints.
    /// Stop after this many frames; unset means run until the window closes. Tests and CI use it. A frame here is
    /// one fixed 1/30 s step and one render: `--frames` puts Coney in test mode (isTestMode()).
    std::optional<int> frameLimit;
    /// `--checkpoint`: the checkpoint a level played with `--play-level` starts at, as `SetCheckPoint` sets it before
    /// the level loads; 1 when unset. Requires playLevel to name a level, not a sandbox.
    std::optional<int> checkpoint;
    /// `--start X,Y,Z,HEADING[,DISTANCE,YAW]`: put player 1 (and the camera) there once the level or layout has
    /// started (StartPlace). Requires playLevel.
    std::optional<StartPlace> start;
    /// `--camera X,Y,Z,QX,QY,QZ,QW[,FOV]`: player 1's view pinned there for the whole run (CameraPin). Requires
    /// playLevel.
    std::optional<CameraPin> cameraPin;
    /// `--ai-seed N`: vary the AI's random draws by N (0 to 2,147,483,647): the game's random stream starts at index N
    /// (GameRandom::startAt()) and every brain's and the gangs' generator is seeded through ai::variedSeed(). Unset is
    /// 0, the usual run; the same N plays the same run. Requires playLevel or rumble.
    std::optional<std::uint32_t> aiSeed;
    /// `--record-seconds`: the clip's length, 1 to clips::kMaxClipSeconds; unset: the cap. Requires recordPath.
    std::optional<int> recordSeconds;
    /// `--record-debug-draw`: the clip keeps the Debug draw page's lines in the 3D scene that the run switched on (the
    /// world edits' collision wireframe); the menus and the developer overlay stay out of it. Requires recordPath.
    bool recordDebugDraw = false;
    /// `--render-size WxH`: the frame's size (RenderSize) in place of the window's usual one. Needs a window (not
    /// headless, `--load` or `--render-references`).
    std::optional<RenderSize> renderSize;
    /// `--bench-fighters N`: spawn N AI fighters in front of the player on the level's first frame of play, as the
    /// debug menus' AI fighters page does, to measure the cost of many (0 to kMaxBenchFighters). Requires benchFile.
    std::optional<int> benchFighters;
    /// `--bench-warmup N`: the frames after the first frame of play that the bench does not measure (the fighters
    /// settling, the caches filling); unset: kDefaultBenchWarmup. Must leave frames to measure. Requires benchFile.
    std::optional<int> benchWarmup;
    /// `--bench-allies N`: also spawn N AI allies behind the player on the first frame of play, as the AI fighters
    /// page's Ally side does, so a crowd fights itself and not only the player (0 to kMaxBenchFighters). Requires
    /// benchFile.
    std::optional<int> benchAllies;
    /// `--bench-spawn-step N`: spawn the fighters and make the player invincible once N fixed steps of play have run
    /// (an input script's frames count the same steps), not on the first frame of play, so a run can first take the
    /// player somewhere (out of the hub's clubhouse into the streets). The warm-up counts from that frame. Requires
    /// benchFile.
    std::optional<int> benchSpawnStep;
    /// `--bench-measure N`: end the run once N frames after the warm-up are measured (1 or more), so a real-time
    /// bench, whose frames before the spawn are many and vary, measures as many as asked; `--frames` stays the
    /// longest the run may take. Unset: measure until `--frames`. Requires benchFile.
    std::optional<int> benchMeasure;
    /// `--bench-place X,Y,Z,HEADING`: put player 1 there (the debug menus' teleport, dropped onto the ground below)
    /// just before the fighters spawn, so the crowd fights at a chosen spot of a level whose script makes the player
    /// (where `--start` is overridden). Unset: where he stands. Requires benchFile.
    std::optional<StartPlace> benchPlace;
    /// `--bench-neutrals N`: also spawn N neutral AI fighters (the AI fighters page's Neutral side: nobody's friend or
    /// enemy until one is hit) in front of the player, beyond the enemies (0 to kMaxBenchFighters). Requires
    /// benchFile.
    std::optional<int> benchNeutrals;
    /// `--dev-overlay`: show the debug menus' developer overlay for this many frames from the start, then hide it; a
    /// test aid for checking that the overlay leaves the frame as it found it (docs/guides/debug-menu.md). Ignored
    /// without a window.
    std::optional<int> devOverlayFrames;
    /// `--fps-cap N`: draw at most N frames a second, 0 for no cap (the default; vsync still limits the rate while
    /// it is on). 30 is the original's rhythm: one step and one render per frame, nothing blended
    /// (docs/guides/running-options.md#frame-rate). Not in test mode.
    std::optional<int> fpsCap;
    /// `--rumble TYPE [--arena N] [--gang-size N]`: start a Rumble match of that mode as QUICK RUMBLE does, whatever
    /// the menu chose. Requires discPath; cannot be combined with `--load`, the viewers, `--play-level` or `--sandbox`.
    std::optional<RumbleLaunch> rumble;
    // 1-byte-aligned: enums and flags.
    /// `--bench-god`: make player 1 invincible when the fighters spawn (the Entity inspector's Invincible), so a long
    /// bench among enemies does not end in his death. Requires benchFile.
    bool benchGod = false;
    /// `--bench-real-time`: pace the bench by the real clock as a player's run is (fixed steps at 30 a second, frames
    /// as fast as they go, rendered between steps), not in test mode's lockstep of one step a frame, so the frames that
    /// run a step and those that only render both show. The run still ends after `--frames` frames. Requires
    /// benchFile.
    bool benchRealTime = false;
    /// `--bench-fps`: turn the debug menus' FPS counter (the Display page's) on from the start, so a screenshot of the
    /// run shows its frame rate; it reads the real clock, so it shows rates in a `--bench-real-time` run. Requires
    /// benchFile.
    bool benchFps = false;
    /// `--help` was given: print usageText() and exit 0.
    bool showHelp = false;
    /// `--verify`: with installFrom, re-read every file already installed and copy again any that changed.
    bool verifyInstall = false;
    /// `--headless`: no window and librw's NULL renderer, so nothing needs a display or a GPU. CI runs this way.
    bool headless = false;
    /// `--freeze-world`: after the first step of play nothing in the world advances (pedestrians, cars, particles,
    /// animation, the scripts), so every frame shows the same picture; the scenery still streams round the camera and
    /// the fixed step still runs. Requires playLevel.
    bool freezeWorld = false;
    /// `--pad-pipe`: take player 1's pad from standard input, one line per frame, after writing what the frame before
    /// left on screen to standard output (docs/guides/running-play-level.md#pad-pipe), so a driver program can play the
    /// game by what it sees. Test mode; cannot be combined with `--input-script`.
    bool padPipe = false;
    /// `--language`: the language of the UI strings (`en`, `es`, `fr`, `it`, `de`); English by default.
    Language language = Language::English;
    /// `--vsync on|off`: whether a present waits for the display's vertical blank; on by default. Needs a window.
    bool vsync = true;
    /// `--line-blend on|off`: whether each shown line is the mean of two neighbouring lines, as the PS2's video output
    /// softens the picture (docs/research/rendering.md#output); off by default, an optional PS2 look: on a
    /// full-resolution monitor there is no interlace flicker to hide, and the one-line shift reads as a smear.
    /// Reference renders never blend.
    bool lineBlend = false;
    /// `--show-fps`: print the frame and step rates once a second, and their totals at the end. Not in test mode.
    bool showFps = false;
    /// `--no-activate`: the window opens without taking the keyboard focus from the one in use, so a run started by
    /// an agent or a tool never interrupts the person typing (activatesWindow()).
    bool noActivate = false;
    /// `--kind`: the lists `--render-references` renders; unset renders all of them. Requires renderReferences.
    std::optional<ReferenceKind> referenceKind;
    /// `--renderer gl3|gpu`: the renderer a windowed run draws with (docs/engine/renderer.md#migration); unset takes
    /// the saved `Display/Renderer` setting outside test mode, else GL3 (chooseRenderer()); a saved GPU renderer starts
    /// GL3 while it draws no menus (kGpuRendererDrawsMenus, docs/engine/gpu-renderer.md#way-back). `gpu` needs a window
    /// and reads no frame back, so it cannot be combined with `--headless`, `--load`, `--screenshot`, `--record` or
    /// `--render-references`.
    std::optional<RendererKind> renderer;
    /// `--preset ps2|xbox|enhanced|high`: the display preset (docs/engine/renderer.md#presets). Outside test mode it is
    /// chosen at the start, over the saved settings, and the player may change it after; in test mode it is what
    /// drawing is pinned to in place of *PS2* (pinnedDisplayPreset()). Unset: the saved settings, or *PS2* in test
    /// mode.
    std::optional<DisplayPreset> displayPreset;
    /// `--no-audio`: no sound output at all: no mixer, no device (the debug menu's Audio page says so).
    bool noAudio = false;
    /// `--audio-test`: play a synthesised tone sweep, looping, from the start (audio::makeToneSweep()), and print what
    /// was mixed at the end; a check of the sound output. Cannot be combined with `--no-audio`, `--load` or
    /// `--render-references`.
    bool audioTest = false;
    /// `--skip-movies`: every movie is skipped at once, as if it had ended
    /// (docs/research/movies.md#coneys-implementation).
    bool skipMovies = false;
    /// `--cpu-skinning`: skin every human by CharacterMesh's CPU path, to compare it with the GPU's
    /// (docs/guides/performance.md#gpu-skinning). Coney's own debug tooling.
    bool cpuSkinning = false;
};

/// The largest `--fps-cap`.
inline constexpr int kMaxFpsCap = 1000;

/// The largest `--checkpoint`: far beyond any level's (the most is 12, the hub's), but a script reads any number.
inline constexpr int kMaxCheckpoint = 99;
/// The most fighters `--bench-fighters` spawns.
inline constexpr int kMaxBenchFighters = 1000;
/// The frames a bench leaves unmeasured after the first frame of play unless `--bench-warmup` says otherwise.
inline constexpr int kDefaultBenchWarmup = 60;

/// Whether `options` ask for test mode: `--headless`, `--load`, `--frames`, `--input-script`, `--screenshot` or
/// `--record`. Test mode runs the main loop in lockstep, one fixed step and one render per frame, and never reads a
/// real clock, so a run gives the same result every time on any machine (docs/guides/conventions.md#platform-code).
[[nodiscard]] bool isTestMode(const Options& options);

/// Whether the window may take the keyboard focus when it opens: only in a run a person plays, so not with
/// `--no-activate`, in test mode or for `--render-references` (an offscreen job). Runs started by agents and tools
/// are test-mode runs, so they never take the focus from someone typing
/// (docs/guides/research-tools.md#running-coney).
[[nodiscard]] bool activatesWindow(const Options& options);

/// Whether the run may start the renderer the overrides file saves (`Display/Renderer`, chooseRenderer()): only in a
/// run a person plays, so not in test mode, and not for `--render-references`, whose offscreen drawing needs GL3
/// (docs/engine/gpu-renderer.md#choice).
[[nodiscard]] bool takesSavedRenderer(const Options& options);

/// The display preset drawing is pinned to (DisplaySettings::pin()), whatever the saved settings say: in test mode
/// and for `--render-references`, the preset `--preset` names, else *PS2*, so a test run draws the PS2 picture
/// whatever a player saved (docs/engine/renderer.md#ps2-preset); nothing in a run a person plays.
[[nodiscard]] std::optional<DisplayPreset> pinnedDisplayPreset(const Options& options);

/// The character `--view-character` shows without a name: Rembrandt, the player of level99 (warr_re_cv).
inline constexpr std::string_view kDefaultViewCharacter = "warr_re_cv";

/// The sandbox layout `--sandbox` flies round without a name.
inline constexpr std::string_view kDefaultSandbox = "default";

/// What `--play-level` takes to play a sandbox layout instead of a level: `sandbox` alone (kDefaultSandbox), or
/// `sandbox:NAME` for another layout.
inline constexpr std::string_view kSandboxLevelPrefix = "sandbox";

/// The sandbox layout a `--play-level` name stands for (`sandbox` gives kDefaultSandbox, `sandbox:parkour` gives
/// `parkour`), or nothing for a level name.
[[nodiscard]] std::optional<std::string> sandboxOfPlayLevel(std::string_view name);

/// Largest accepted `--frames` value: about 4.6 hours at 60 Hz, far beyond any test, and well inside `int`.
inline constexpr int kMaxFrameLimit = 1'000'000;

/// Which options a build takes (docs/guides/building.md#debug-tools): every one in a build with the debug tools, a
/// player's alone in the release build.
enum class OptionSet : std::uint8_t {
    All,    ///< Every option: the developers' and agents' tools as well as a player's.
    Player, ///< A player's options and the three of test mode the release build's smoke tests use.
};

/// The options the release build takes, as typed (`--disc`): a player's (the install, the disc, the language, the
/// profiles, the frame rate and vsync, the PS2 look, the sound, the FPS counter, skipping movies, the window, network
/// play) and test mode's `--headless`, `--frames` and `--input-script`, which its smoke tests need.
[[nodiscard]] std::span<const std::string_view> playerOptions();

/// Whether `option` (as typed, `--disc`) is one of playerOptions().
[[nodiscard]] bool isPlayerOption(std::string_view option);

/// Parses the arguments after the program name, taking the options of `set`. `installBesideExecutable` is the install
/// folder the executable sits in, when it sits in one (platform::installBesideExecutable()): with neither `--disc` nor
/// `--data`, the game is played from it. Never throws; a bad argument gives ErrorCode::InvalidArgument with a one-line
/// message naming the argument, and an option outside `set` says it needs a build with the debug tools.
[[nodiscard]] std::expected<Options, Error> parseOptions(std::span<const std::string_view> args,
                                                         OptionSet set = OptionSet::All,
                                                         std::optional<std::string_view> installBesideExecutable = {});

/// The folder of an install that holds the PS2 disc's files: `<install>/game/ps2`.
[[nodiscard]] std::string installGameFolder(std::string_view install);

/// The usage text printed for `--help` and after an argument error. It ends with a newline. Print it by its size
/// (`%.*s` or fwrite), as with any string_view: callers must not rely on data() being null-terminated.
[[nodiscard]] std::string_view usageText();

/// The usage text of the options of `set`: usageText() for OptionSet::All; for OptionSet::Player its synopsis of a
/// player's options and their lines alone. It ends with a newline.
[[nodiscard]] std::string usageText(OptionSet set);

} // namespace coney
