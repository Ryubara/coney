// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "core/language.h"

namespace coney {

/// What `--view-text` shows: a text laid out in a font.
struct TextView {
    std::string font; ///< A font's sprite sheet resource, such as `big_font`.
    std::string text; ///< Marked-up text, or `@` and a UI string id (`@31`, `@0x1f`).
};

/// What `--start` asks for: where player 1 starts in place of the level's (or the layout's) start, and optionally
/// where the follow camera starts. Coney's own test aid, so a trace scenario can start where the original's save
/// state stands (docs/guides/research-workflow.md#comparing-with-coney).
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
/// the game would show, so a frame can be matched with one of the original's (docs/guides/building.md#matched-views).
/// The values are the base camera object's (docs/research/camera.md#the-base-camera-object): Coney's own test aid.
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
/// arena is confirmed (docs/guides/building.md#rumble-from-the-command-line). Coney's own developer aid: a fresh
/// profile's menu offers only 1 ON 1 and WAR PARTY.
struct RumbleLaunch {
    std::uint16_t gameType = 0; ///< The mode's `RM_*` number (RumbleSetup::kGameType).
    int arena = 0;              ///< The arena's level number (`101` for `level101`).
    std::uint16_t gangSize = 0; ///< Fighters per side (RumbleSetup::kGangSize).
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

/// Settings taken from the command line.
struct Options {
    /// Stop after this many frames; unset means run until the window closes. Tests and CI use it. A frame here is
    /// one fixed 1/30 s step and one render: `--frames` puts Coney in test mode (isTestMode()).
    std::optional<int> frameLimit;
    /// `--help` was given: print usageText() and exit 0.
    bool showHelp = false;
    /// `--disc`: the player's disc, a folder (a mounted disc) or an ISO 9660 image, exactly as typed.
    std::optional<std::string> discPath;
    /// `--load`, in the order given: WAD entries to load through the chunk system, each a name or a `0x` hash.
    /// Requires discPath.
    std::vector<std::string> loads;
    /// `--headless`: no window and librw's NULL renderer, so nothing needs a display or a GPU. CI runs this way.
    bool headless = false;
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
    /// `--checkpoint`: the checkpoint a level played with `--play-level` starts at, as `SetCheckPoint` sets it before
    /// the level loads; 1 when unset. Requires playLevel to name a level, not a sandbox.
    std::optional<int> checkpoint;
    /// `--scene`: an in-engine scene (`l99_c1`) the play mode plays at once, with stand-ins bound to its roles and
    /// player 1 to his: Coney's test aid. Requires playLevel to name a level.
    std::optional<std::string> scene;
    /// `--start X,Y,Z,HEADING[,DISTANCE,YAW]`: put player 1 (and the camera) there once the level or layout has
    /// started (StartPlace). Requires playLevel.
    std::optional<StartPlace> start;
    /// `--camera X,Y,Z,QX,QY,QZ,QW[,FOV]`: player 1's view pinned there for the whole run (CameraPin). Requires
    /// playLevel.
    std::optional<CameraPin> cameraPin;
    /// `--freeze-world`: after the first step of play nothing in the world advances (pedestrians, cars, particles,
    /// animation, the scripts), so every frame shows the same picture; the scenery still streams round the camera and
    /// the fixed step still runs. Requires playLevel.
    bool freezeWorld = false;
    /// `--trace`: write the player's and the follow camera's state after every step of `--play-level` to this file,
    /// one CSV line per step (human::traceLine()), so feel comparisons can be repeated
    /// (docs/guides/building.md#tracing). Requires playLevel.
    std::optional<std::string> traceFile;
    /// `--script-trace`: write every binding call the level's scripts make, and every call into them by name, to this
    /// file, one line each with its arguments (script::ScriptSystem::traceCalls()), to see where a mission's scripts
    /// wait. Requires playLevel with a level.
    std::optional<std::string> scriptTraceFile;
    /// `--event-log`: write what a player would notice happening (script binding calls, calls into the scripts,
    /// hints shown, sounds started, humans appearing and running out of health) to this file, one CSV line per
    /// event stamped with its step (coney::events), for a differential playthrough against the original
    /// (docs/guides/research-workflow.md#differential-playthroughs). Works on any path through the game.
    std::optional<std::string> eventLogFile;
    /// `--pad-pipe`: take player 1's pad from standard input, one line per frame, after writing what the frame before
    /// left on screen to standard output (docs/guides/building.md#pad-pipe), so a driver program can play the game
    /// by what it sees. Test mode; cannot be combined with `--input-script`.
    bool padPipe = false;
    /// `--assets`: the folder holding Coney's own assets (its `sandbox` folder of layouts and textures), in place of
    /// the `assets` folder beside the executable.
    std::optional<std::string> assetsDir;
    /// `--screenshot`: save the last frame as a PNG at this path. Requires frameLimit and a window (not headless or
    /// `--load`).
    std::optional<std::string> screenshotPath;
    /// `--render-size WxH`: the frame's size (RenderSize) in place of the window's usual one. Needs a window (not
    /// headless, `--load` or `--render-references`).
    std::optional<RenderSize> renderSize;
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
    /// `--dev-overlay`: show the debug menus' developer overlay for this many frames from the start, then hide it; a
    /// test aid for checking that the overlay leaves the frame as it found it (docs/guides/debug-menu.md). Ignored
    /// without a window.
    std::optional<int> devOverlayFrames;
    /// `--language`: the language of the UI strings (`en`, `es`, `fr`, `it`, `de`); English by default.
    Language language = Language::English;
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
    /// `--fps-cap N`: draw at most N frames a second, 0 for no cap (the default; vsync still limits the rate while
    /// it is on). 30 is the original's rhythm: one step and one render per frame, nothing blended
    /// (docs/guides/building.md#frame-rate). Not in test mode.
    std::optional<int> fpsCap;
    /// `--vsync on|off`: whether a present waits for the display's vertical blank; on by default. Needs a window.
    bool vsync = true;
    /// `--line-blend on|off`: whether each shown line is the mean of two neighbouring lines, as the PS2's video output
    /// softens the picture (docs/research/rendering.md#output); on by default. Reference renders never blend.
    bool lineBlend = true;
    /// `--show-fps`: print the frame and step rates once a second, and their totals at the end. Not in test mode.
    bool showFps = false;
    /// `--no-activate`: the window opens without taking the keyboard focus from the one in use, so a run started by
    /// an agent or a tool never interrupts the person typing (activatesWindow()).
    bool noActivate = false;
    /// `--kind`: the lists `--render-references` renders; unset renders all of them. Requires renderReferences.
    std::optional<ReferenceKind> referenceKind;
    /// `--no-audio`: no sound output at all: no mixer, no device (the debug menu's Audio page says so).
    bool noAudio = false;
    /// `--audio-test`: play a synthesised tone sweep, looping, from the start (audio::makeToneSweep()), and print what
    /// was mixed at the end; a check of the sound output. Cannot be combined with `--no-audio`, `--load` or
    /// `--render-references`.
    bool audioTest = false;
    /// `--rumble TYPE [--arena N] [--gang-size N]`: start a Rumble match of that mode as QUICK RUMBLE does, whatever
    /// the menu chose. Requires discPath; cannot be combined with `--load`, the viewers, `--play-level` or `--sandbox`.
    std::optional<RumbleLaunch> rumble;
    /// `--skip-movies`: every movie is skipped at once, as if it had ended
    /// (docs/research/movies.md#coneys-implementation).
    bool skipMovies = false;
};

/// The largest `--fps-cap`.
inline constexpr int kMaxFpsCap = 1000;

/// The largest `--checkpoint`: far beyond any level's (the most is 12, the hub's), but a script reads any number.
inline constexpr int kMaxCheckpoint = 99;

/// Whether `options` ask for test mode: `--headless`, `--load`, `--frames`, `--input-script` or `--screenshot`. Test
/// mode runs the main loop in lockstep, one fixed step and one render per frame, and never reads a real clock, so a run
/// gives the same result every time on any machine (docs/guides/conventions.md#platform-code).
[[nodiscard]] bool isTestMode(const Options& options);

/// Whether the window may take the keyboard focus when it opens: only in a run a person plays, so not with
/// `--no-activate`, in test mode or for `--render-references` (an offscreen job). Runs started by agents and tools
/// are test-mode runs, so they never take the focus from someone typing
/// (docs/guides/research-workflow.md#running-coney).
[[nodiscard]] bool activatesWindow(const Options& options);

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

/// Parses the arguments after the program name. Never throws; a bad argument gives ErrorCode::InvalidArgument with
/// a one-line message naming the argument.
[[nodiscard]] std::expected<Options, Error> parseOptions(std::span<const std::string_view> args);

/// The usage text printed for `--help` and after an argument error. It ends with a newline. Print it by its size
/// (`%.*s` or fwrite), as with any string_view: callers must not rely on data() being null-terminated.
[[nodiscard]] std::string_view usageText();

} // namespace coney
