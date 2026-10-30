// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/options.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "core/clip_recording.h"
#include "core/log_record.h"
#include "core/parse_number.h"

namespace coney {

namespace {

// The options of OptionSet::Player (playerOptions()).
constexpr std::array<std::string_view, 22> kPlayerOptions{
    "--install",     "--data",           "--verify",       "--disc",     "--language", "--profiles",
    "--fps-cap",     "--vsync",          "--line-blend",   "--show-fps", "--no-audio", "--skip-movies",
    "--no-activate", "--window-display", "--host",         "--join",     "--net-name", "--help",
    "--headless",    "--frames",         "--input-script", "--renderer"};

// The synopsis of usageText(OptionSet::Player), before its options' lines.
constexpr std::string_view kPlayerSynopsis =
    "Usage: coney --install DISC --data DIR [--verify]\n"
    "       coney --script-patch make|apply ...   (coney --script-patch for its usage)\n"
    "       coney [--disc PATH | --data DIR] [--language CODE] [--profiles DIR]\n"
    "             [--fps-cap N] [--vsync on|off] [--line-blend on|off] [--show-fps]\n"
    "             [--renderer gl3|gpu]\n"
    "             [--no-activate] [--window-display DISPLAY] [--no-audio] [--skip-movies]\n"
    "             [--host [[ADDR:]PORT] | --join ADDR[:PORT]] [--net-name NAME]\n"
    "             [--headless] [--frames N] [--input-script FILE] [--help]\n"
    "\n";

// The text usageText() returns (see its doc comment in options.h).
constexpr std::string_view kUsage =
    "Usage: coney --install DISC --data DIR [--verify]\n"
    "       coney --script-patch make|apply ...   (coney --script-patch for its usage)\n"
    "       coney [--disc PATH | --data DIR] [--load ENTRY]... [--view-txd ENTRY] [--view-sheet SHEET]\n"
    "             [--frames N] [--screenshot PATH] [--draw-hash] [--headless] [--help]\n"
    "             [--input-script FILE] [--view-text FONT TEXT] [--language CODE] [--tunables FILE]\n"
    "             [--profiles DIR]\n"
    "             [--view-world NAME] [--view-character [NAME]] [--anim CLIP]\n"
    "             [--play-level NAME [--spawn NAME | --checkpoint N] [--start X,Y,Z,H[,D,YAW]]\n"
    "             [--give ITEM,COUNT]...\n"
    "             [--trace FILE] [--script-trace FILE] [--scene NAME] [--camera X,Y,Z,QX,QY,QZ,QW[,FOV]]\n"
    "             [--freeze-world]] [--sandbox [NAME]]\n"
    "             [--assets DIR]\n"
    "             [--event-log FILE] [--pad-pipe] [--trace-humans FILE]\n"
    "             [--ai-player] [--ai-seed N]\n"
    "             [--bench FILE [--bench-fighters N] [--bench-allies N] [--bench-god]\n"
    "                           [--bench-warmup N] [--bench-frames FILE] [--bench-real-time]\n"
    "                           [--bench-spawn-step N] [--bench-measure N]\n"
    "                           [--bench-place X,Y,Z,H] [--bench-neutrals N] [--bench-fps]]\n"
    "             [--log-file FILE] [--log-verbose CATEGORIES]\n"
    "             [--dev-overlay N] [--mesh-view SPEC] [--world-edit FILE]\n"
    "             [--render-references DIR [--kind KIND] [--only NAME]... [--names FILE]]\n"
    "             [--fps-cap N] [--vsync on|off] [--line-blend on|off] [--show-fps]\n"
    "             [--renderer gl3|gpu]\n"
    "             [--no-activate] [--window-display DISPLAY]\n"
    "             [--no-audio | --audio-test] [--skip-movies] [--cpu-skinning]\n"
    "             [--record FILE [--record-seconds N] [--record-debug-draw]]\n"
    "             [--host [[ADDR:]PORT] | --join ADDR[:PORT]] [--net-name NAME]\n"
    "\n"
    "  --install DISC     install the PS2 disc (a mounted disc, a folder of its files or an ISO image)\n"
    "                     into the folder --data names: its files copied as they are and checked,\n"
    "                     then exit; run it again to finish an install that was stopped\n"
    "  --data DIR         the install folder: where --install puts the game, or, without --install,\n"
    "                     the installed game to play (in place of --disc). With neither --disc nor\n"
    "                     --data, Coney plays the install it sits in, if it sits in one\n"
    "  --verify           with --install: read every installed file again and copy any that changed\n"
    "  --disc PATH        the game's disc: a mounted disc, a folder of its files or an ISO image\n"
    "  --load ENTRY       load a WAD entry (a name such as level1.lev, or a hash such as 0x7e23a6f2)\n"
    "                     through the chunk system and print a summary; repeatable; needs --disc;\n"
    "                     runs without a window and exits when every entry is loaded\n"
    "  --view-txd ENTRY   show the textures of a WAD entry's texture dictionaries; needs --disc\n"
    "  --view-sheet SHEET show a sprite sheet's rectangles as sprites: a name such as menu_system,\n"
    "                     or a WAD entry; needs --disc\n"
    "  --view-text FONT TEXT\n"
    "                     lay out and draw TEXT (markup allowed; @ID for a UI string, such as @0x1f)\n"
    "                     with the font sheet FONT (such as big_font); needs --disc\n"
    "  --language CODE    the language of the UI strings: en, es, fr, it or de (default en)\n"
    "  --view-world NAME  fly through a level's streamed scenery: level2, level2s, objarena; needs --disc\n"
    "  --view-character [NAME]\n"
    "                     show a character playing a clip: a model name such as warr_re_cv (the default,\n"
    "                     Rembrandt); needs --disc\n"
    "  --anim CLIP        the clip --view-character plays: an anim id or a clip name\n"
    "  --play-level NAME  play a level as Rembrandt with a gamepad and the follow camera: levelN\n"
    "                     (level2, level99: any level with a streamed world on the disc), starting\n"
    "                     where the level's script puts player 1, or sandbox:NAME for a sandbox\n"
    "                     layout (sandbox alone: default); needs --disc\n"
    "  --spawn NAME       with --play-level sandbox:NAME: the layout's spawn point to start at\n"
    "  --checkpoint N     with --play-level levelN: the checkpoint to start at (1 to 99, default 1)\n"
    "  --start X,Y,Z,HEADING[,DISTANCE,YAW]\n"
    "                     with --play-level: start player 1 there (feet in metres, heading in\n"
    "                     degrees) in place of the level's start, and the camera DISTANCE m from\n"
    "                     him with its view facing YAW degrees; a test aid for trace scenarios\n"
    "  --give ITEM,COUNT  with --play-level: player 1 holds COUNT (0 to 9999) of inventory item ITEM\n"
    "                     (0 to 22; 3 is spray paint) from the first step of play; repeatable; a test aid\n"
    "  --trace FILE       with --play-level: write the player's and the camera's state after every\n"
    "                     step to FILE, one CSV line per step\n"
    "  --trace-humans FILE with --play-level or --rumble (--trace in a match): write every human's\n"
    "                     state (position, goals, target, attacks, blocks) after every step to\n"
    "                     FILE, one CSV row per human per step\n"
    "  --ai-player        with --rumble: hand player 1 to the AI when the countdown ends, so every\n"
    "                     fighter is AI-driven (a test aid for comparing gang fights)\n"
    "  --ai-seed N        with --play-level or --rumble: vary the AI's random draws (0 to\n"
    "                     2147483647, default 0); the same N plays the same run\n"
    "  --bench FILE       with --play-level and --frames: measure the run with the profiler and\n"
    "                     write its summary (frame times, zones, counters) to FILE as JSON\n"
    "  --bench-fighters N with --bench: spawn N AI fighters (0 to 1000) in front of the player on\n"
    "                     the first frame of play\n"
    "  --bench-allies N   with --bench: also spawn N AI allies (0 to 1000) behind the player\n"
    "  --bench-god        with --bench: make the player invincible when the fighters spawn\n"
    "  --bench-spawn-step N with --bench: spawn the fighters (and make the player invincible)\n"
    "                     once N steps of play have run; the warm-up counts from there\n"
    "  --bench-warmup N   with --bench: frames left unmeasured after the spawn (default 60)\n"
    "  --bench-measure N  with --bench: end the run once N frames are measured (--frames stays\n"
    "                     the longest it may take)\n"
    "  --bench-frames FILE with --bench: also write every measured frame (its length, zones and\n"
    "                     counters) to FILE as CSV\n"
    "  --bench-real-time  with --bench: pace the run by the real clock as a player's run is (30\n"
    "                     steps a second, frames as fast as they go), not one step a frame\n"
    "  --bench-place X,Y,Z,HEADING with --bench: put the player there (dropped onto the ground)\n"
    "                     just before the fighters spawn\n"
    "  --bench-neutrals N with --bench: also spawn N neutral AI fighters (0 to 1000) beyond the\n"
    "                     enemies\n"
    "  --bench-fps        with --bench: show the debug menus' FPS counter from the start (it reads\n"
    "                     the real clock: with --bench-real-time)\n"
    "  --script-trace FILE with --play-level levelN: write every script binding call, and every call\n"
    "                     into the scripts, with its arguments to FILE, one line each\n"
    "  --event-log FILE   write what a player would notice (binding calls, calls into the scripts,\n"
    "                     hints, sounds, humans in and out) to FILE, one CSV line per event\n"
    "  --log-file FILE    write the run log (every line stamped with its step) to FILE; by default\n"
    "                     coney.log in the user's data folder, none in test mode\n"
    "  --log-verbose CATEGORIES\n"
    "                     keep the detail lines of these run log categories too: player, ai,\n"
    "                     combat, script, world, engine, comma-separated, or all\n"
    "  --pad-pipe         read player 1's pad from standard input, one line per frame, after\n"
    "                     writing what is on screen to standard output; for a driver program\n"
    "  --camera X,Y,Z,QX,QY,QZ,QW[,FOV]\n"
    "                     with --play-level: pin player 1's view for the whole run at the eye X,Y,Z\n"
    "                     (metres, z up) with the camera's orientation quaternion and field of view\n"
    "                     (degrees, default 60); a test aid for matching the original's frames\n"
    "  --freeze-world     with --play-level: after the first step nothing in the world moves\n"
    "                     (people, cars, particles, animation, scripts), so every frame is the same\n"
    "  --scene NAME       with --play-level levelN: play the in-engine scene NAME (such as l99_c1) at\n"
    "                     once, with stand-ins in its roles and the player in his; a test aid\n"
    "  --sandbox [NAME]   fly round a sandbox test world: default (the default), parkour, or a\n"
    "                     .layout file; needs no disc\n"
    "  --assets DIR       the folder of Coney's own assets (sandbox layouts and textures), in place\n"
    "                     of the assets folder beside the executable\n"
    "  --render-references DIR\n"
    "                     write a 256x256 PNG of every character (standing), object and car, and a\n"
    "                     PNG of at most 64x64 of every radar icon and traced particle sprite, into\n"
    "                     DIR/characters, objects, cars, radar and particles, and exit; needs --disc\n"
    "                     and a display (the window stays hidden)\n"
    "  --kind KIND        with --render-references: characters, objects, cars, radar, particles or\n"
    "                     all (the default)\n"
    "  --only NAME        with --render-references: render only this entry (a model, object, car or\n"
    "                     particle type name, icon-N for a radar icon, or a 0x name hash); repeatable\n"
    "  --names FILE       with --render-references: model and object type names, one per line, to\n"
    "                     name the images by\n"
    "  --fps-cap N        draw at most N frames a second (0, the default: no cap); the game runs at\n"
    "                     its fixed 30 steps a second whatever the rate; 30 draws one frame per step\n"
    "  --vsync on|off     wait for the display's vertical blank when presenting (default on)\n"
    "  --line-blend on|off  on: an optional PS2 look, the picture softened as the PS2's video output\n"
    "                     does, each line the mean of two neighbouring lines; off (the default)\n"
    "                     shows the frame as drawn\n"
    "  --show-fps         print the frame and step rates once a second\n"
    "  --renderer gl3|gpu draw with librw's OpenGL 3.3 (gl3, the default) or SDL3's GPU API (gpu:\n"
    "                     Vulkan, Direct3D 12 or Metal; it only clears and presents so far); unset,\n"
    "                     the saved Display/Renderer setting outside test mode\n"
    "  --no-activate      open the window without taking the keyboard focus from the one in use\n"
    "                     (always so in test mode and for --render-references)\n"
    "  --window-display DISPLAY\n"
    "                     open the window centred on a display: its number (1 is the first),\n"
    "                     part of its name, or portrait (the first taller than wide); shrunk to\n"
    "                     fit if need be, never taking the keyboard focus. Default:\n"
    "                     CONEY_WINDOW_DISPLAY, else the system's choice\n"
    "  --no-audio         run with no sound output (no audio device is opened)\n"
    "  --host [[ADDR:]PORT]\n"
    "                     host a network game from the start: listen on UDP PORT (default 27615; 0 for\n"
    "                     any free one) of the local address ADDR (default every interface)\n"
    "  --join ADDR[:PORT] join the network game at ADDR (a host name or IPv4 address), port 27615 unless\n"
    "                     given\n"
    "  --net-name NAME    the player name a network game shows (1 to 32 printable ASCII characters)\n"
    "  --skip-movies      skip every movie at once, as if it had ended (by default they play, in\n"
    "                     test mode too; any pad button skips one, as in the game)\n"
    "  --cpu-skinning     skin the humans on the CPU instead of the GPU, to compare the two\n"
    "  --audio-test       play a synthesised tone sweep, looping, and print what was mixed at the end;\n"
    "                     in test mode no device opens and the sound is mixed offline\n"
    "  --frames N         stop after N frames (1 to 1000000); used by tests and CI. Test mode: with\n"
    "                     --frames, --headless, --input-script or --screenshot each frame is one\n"
    "                     step and one render, with no clock, so a run is the same every time\n"
    "  --screenshot PATH  save the last frame as a PNG; needs --frames and a window\n"
    "  --draw-hash        print the counts and hash of what the last frame draws; needs --frames\n"
    "  --record FILE      record a video clip (H.264 MP4, 30 fps, 720 lines, with the game's sound) of\n"
    "                     Coney's own frames, without the debug menus, into FILE: a plain name in a\n"
    "                     scratch folder's clips/ folder, never in a git checkout; needs a window and\n"
    "                     FFmpeg (CONEY_FFMPEG or PATH). Test mode; with --play-level from the first\n"
    "                     frame of play\n"
    "  --record-seconds N the clip's length, 1 to 10 seconds (default and most: 10)\n"
    "  --record-debug-draw\n"
    "                     keep the Debug draw lines the run switched on (the world edits' collision\n"
    "                     wireframe) in the clip; the menus and the overlay stay out\n"
    "  --render-size WxH  draw frames W x H pixels (such as 640x448, the original's), the logical\n"
    "                     screen filling them at its 4:3 shape; needs a window\n"
    "  --input-script FILE\n"
    "                     play the pad input in FILE instead of the keyboard and gamepads\n"
    "  --tunables FILE    the debug menus' tunable overrides to load and save (default: coney-tunables.ini\n"
    "                     in your config folder)\n"
    "  --profiles DIR     the folder of saved player profiles (default: profiles in your data folder;\n"
    "                     none in test mode, where profiles last the run)\n"
    "  --dev-overlay N    show the developer overlay (F1) for the first N frames, then hide it; a test aid\n"
    "  --mesh-view SPEC   the Debug draw page's mesh views from the first frame of play: items separated by\n"
    "                     commas, wire=selection|humans|all, view=wire|material|draw|lod,\n"
    "                     scope=selection|humans|all, normals, bones, select=nearest|player, frames=N\n"
    "  --world-edit FILE  with --view-world or a level's --play-level: the world edits in FILE (sectors\n"
    "                     spread on a grid, a world hidden, a building cut out and moved, roof collision)\n"
    "  --headless        run with no window and no GPU (nothing is drawn)\n"
    "  --help             show this text and exit\n";

// Shorthand for the one error code every option mistake uses.
std::unexpected<Error> invalidArgument(std::string message) {
    return std::unexpected(Error{ErrorCode::InvalidArgument, std::move(message)});
}

// True for a non-empty string of ASCII digits only; no sign, spaces or locale digits.
bool isAllDigits(std::string_view text) {
    return !text.empty() && std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; });
}

/// Parses the value after `--frames` (or `option`, named in the error): a whole number from 1 to kMaxFrameLimit,
/// written with decimal digits only.
std::expected<int, Error> parseFrameLimit(std::string_view text, std::string_view option = "--frames") {
    auto badValue = [text, option] {
        return invalidArgument(
            std::format("{} needs a whole number from 1 to {}, got \"{}\"", option, kMaxFrameLimit, text));
    };
    // Check the characters first: on its own, from_chars accepts a leading '-' and stops quietly at the first
    // non-digit, so "-3" and "3x" would both look like numbers to it.
    if (!isAllDigits(text)) {
        return badValue();
    }
    int value = 0;
    auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    // A value too large for int (std::errc::result_out_of_range) is the same mistake as one above kMaxFrameLimit.
    if (parsed.ec != std::errc{} || value < 1 || value > kMaxFrameLimit) {
        return badValue();
    }
    return value;
}

// Stores the value after a single-valued option (`--view-txd`, `--screenshot`) in `slot`, moving `i` past it. Refuses
// a repeat, as --frames does, and a missing or empty value, which `needs` describes.
std::expected<void, Error> takeValue(std::span<const std::string_view> args, std::size_t& i,
                                     std::optional<std::string>& slot, std::string_view option,
                                     std::string_view needs) {
    if (slot.has_value()) {
        return invalidArgument(std::format("{} given twice", option));
    }
    if (i + 1 == args.size() || args[i + 1].empty()) {
        return invalidArgument(std::format("{} needs {}", option, needs));
    }
    slot = std::string(args[++i]);
    return {};
}

// Refuses the character viewer's options in combinations that cannot work: part of checkCombinations().
std::expected<void, Error> checkCharacterViewer(const Options& options) {
    if (options.animClip.has_value() && !options.viewCharacter.has_value()) {
        return invalidArgument("--anim needs --view-character: it names the clip the character plays");
    }
    if (!options.viewCharacter.has_value()) {
        return {};
    }
    if (!options.discPath.has_value()) {
        return invalidArgument("--view-character needs --disc to say where the game's files are");
    }
    if (!options.loads.empty() || options.viewTxd.has_value() || options.viewSheet.has_value() ||
        options.viewText.has_value() || options.viewWorld.has_value()) {
        return invalidArgument(
            "--view-character cannot be combined with --load, --view-txd, --view-sheet, --view-text or --view-world");
    }
    return {};
}

// Refuses the play mode's option in combinations that cannot work: part of checkCombinations().
std::expected<void, Error> checkPlayLevel(const Options& options) {
    if (!options.playLevel.has_value()) {
        return {};
    }
    if (!options.discPath.has_value()) {
        return invalidArgument("--play-level needs --disc to say where the game's files are");
    }
    if (!options.loads.empty() || options.viewTxd.has_value() || options.viewSheet.has_value() ||
        options.viewText.has_value() || options.viewWorld.has_value() || options.viewCharacter.has_value()) {
        return invalidArgument("--play-level cannot be combined with --load or the viewers");
    }
    return {};
}

// Refuses --rumble where it cannot work: part of checkCombinations().
std::expected<void, Error> checkRumble(const Options& options) {
    if (!options.rumble.has_value()) {
        return {};
    }
    if (!options.discPath.has_value()) {
        return invalidArgument("--rumble needs --disc to say where the game's files are");
    }
    if (!options.loads.empty() || options.viewTxd.has_value() || options.viewSheet.has_value() ||
        options.viewText.has_value() || options.viewWorld.has_value() || options.viewCharacter.has_value() ||
        options.playLevel.has_value() || options.sandbox.has_value()) {
        return invalidArgument("--rumble cannot be combined with --load, the viewers, --play-level or --sandbox");
    }
    return {};
}

// Refuses `--bench` and its options where they cannot work: part of checkSandbox(), with the other play options.
std::expected<void, Error> checkBench(const Options& options) {
    if (!options.benchFile.has_value()) {
        if (options.benchFighters.has_value() || options.benchWarmup.has_value() || options.benchAllies.has_value() ||
            options.benchGod || options.benchFramesFile.has_value() || options.benchRealTime ||
            options.benchSpawnStep.has_value() || options.benchMeasure.has_value() || options.benchPlace.has_value() ||
            options.benchNeutrals.has_value() || options.benchFps) {
            return invalidArgument("--bench-fighters, --bench-allies, --bench-neutrals, --bench-god, --bench-warmup, "
                                   "--bench-frames, --bench-real-time, --bench-spawn-step, --bench-measure, "
                                   "--bench-place and --bench-fps need --bench: they set up the measured run");
        }
        return {};
    }
    if (!options.playLevel.has_value() || !options.frameLimit.has_value()) {
        return invalidArgument("--bench needs --play-level and --frames: it measures a level played for N frames");
    }
    // Each frame runs at least one step in lockstep, so the spawn's step and the warm-up must come before the end.
    if (options.benchSpawnStep.value_or(0) + options.benchWarmup.value_or(kDefaultBenchWarmup) >= *options.frameLimit) {
        return invalidArgument("--bench needs more --frames than its spawn step and warm-up (--bench-spawn-step, "
                               "--bench-warmup, default 60)");
    }
    return {};
}

// Refuses the sandbox's options in combinations that cannot work: part of checkCombinations().
std::expected<void, Error> checkSandbox(const Options& options) {
    if (options.spawn.has_value() && !(options.playLevel && sandboxOfPlayLevel(*options.playLevel))) {
        return invalidArgument("--spawn needs --play-level sandbox:NAME: it names a sandbox layout's spawn point");
    }
    if (options.scene.has_value() && !(options.playLevel && !sandboxOfPlayLevel(*options.playLevel))) {
        return invalidArgument("--scene needs --play-level with a level: the scene plays in it");
    }
    if (options.checkpoint.has_value() && !(options.playLevel && !sandboxOfPlayLevel(*options.playLevel))) {
        return invalidArgument("--checkpoint needs --play-level with a level: it names the level's checkpoint");
    }
    if (options.traceFile.has_value() && !options.playLevel.has_value()) {
        return invalidArgument("--trace needs --play-level: it traces the player");
    }
    if (auto bench = checkBench(options); !bench) {
        return bench;
    }
    if (options.aiSeed.has_value() && !options.playLevel.has_value() && !options.rumble.has_value()) {
        return invalidArgument("--ai-seed needs --play-level or --rumble: it varies the AI of a played level");
    }
    if (options.humansTraceFile.has_value() && !options.playLevel.has_value() && !options.rumble.has_value()) {
        return invalidArgument("--trace-humans needs --play-level or --rumble: it traces the humans of a played level");
    }
    if (options.scriptTraceFile.has_value() && !(options.playLevel && !sandboxOfPlayLevel(*options.playLevel))) {
        return invalidArgument("--script-trace needs --play-level with a level: it traces the level's scripts");
    }
    if (options.padPipe && options.inputScript.has_value()) {
        return invalidArgument("--pad-pipe cannot be combined with --input-script: both give the pad");
    }
    if (options.start.has_value() && !options.playLevel.has_value()) {
        return invalidArgument("--start needs --play-level: it places the player");
    }
    if (!options.gives.empty() && !options.playLevel.has_value()) {
        return invalidArgument("--give needs --play-level: it fills player 1's inventory");
    }
    if (options.cameraPin.has_value() && !options.playLevel.has_value()) {
        return invalidArgument("--camera needs --play-level: it pins player 1's view");
    }
    if (options.freezeWorld && !options.playLevel.has_value()) {
        return invalidArgument("--freeze-world needs --play-level: it freezes the level's world");
    }
    if (!options.sandbox.has_value()) {
        return {};
    }
    if (!options.loads.empty() || options.viewTxd.has_value() || options.viewSheet.has_value() ||
        options.viewText.has_value() || options.viewWorld.has_value() || options.viewCharacter.has_value() ||
        options.playLevel.has_value()) {
        return invalidArgument("--sandbox cannot be combined with --load, the viewers or --play-level (to play a "
                               "sandbox, use --play-level sandbox:NAME)");
    }
    return {};
}

// Refuses the reference renderer's options in combinations that cannot work: part of checkCombinations().
std::expected<void, Error> checkReferenceRenderer(const Options& options) {
    if (!options.renderReferences.has_value()) {
        if (!options.only.empty() || options.namesFile.has_value() || options.referenceKind.has_value()) {
            return invalidArgument("--kind, --only and --names need --render-references");
        }
        return {};
    }
    if (!options.discPath.has_value()) {
        return invalidArgument("--render-references needs --disc to say where the game's files are");
    }
    if (options.headless) {
        return invalidArgument("--render-references needs OpenGL, so it cannot be combined with --headless");
    }
    if (!options.loads.empty() || options.viewTxd.has_value() || options.viewSheet.has_value() ||
        options.viewText.has_value() || options.viewWorld.has_value() || options.viewCharacter.has_value() ||
        options.playLevel.has_value() || options.sandbox.has_value()) {
        return invalidArgument("--render-references cannot be combined with --load or the viewers");
    }
    if (options.frameLimit.has_value() || options.screenshotPath.has_value() || options.inputScript.has_value()) {
        return invalidArgument("--render-references cannot be combined with --frames, --screenshot or --input-script");
    }
    return {};
}

// Refuses `--renderer gpu` where it cannot work: part of checkCombinations(). It opens a window of its own and draws
// nothing yet, so there is no frame to read back.
std::expected<void, Error> checkRenderer(const Options& options) {
    if (options.renderer != RendererKind::Gpu) {
        return {};
    }
    if (options.headless || !options.loads.empty() || options.screenshotPath.has_value() ||
        options.recordPath.has_value() || options.renderReferences.has_value()) {
        return invalidArgument("--renderer gpu opens a window and reads no frame back yet, so it cannot be combined "
                               "with --headless, --load, --screenshot, --record or --render-references");
    }
    return {};
}

// Refuses the frame-pacing options where they cannot work: part of checkCombinations().
std::expected<void, Error> checkPacing(const Options& options) {
    if ((options.fpsCap.has_value() || options.showFps) && isTestMode(options)) {
        return invalidArgument("--fps-cap and --show-fps pace a real-time run, so they cannot be combined with "
                               "--headless, --load, --frames, --input-script or --screenshot (test mode)");
    }
    if (!options.vsync && (options.headless || !options.loads.empty())) {
        return invalidArgument("--vsync needs a window, so it cannot be combined with --headless or --load");
    }
    if ((options.fpsCap.has_value() || options.showFps || !options.vsync) && options.renderReferences.has_value()) {
        return invalidArgument("--fps-cap, --vsync and --show-fps cannot be combined with --render-references");
    }
    return {};
}

// Refuses `--record` where it cannot work: part of checkCombinations().
std::expected<void, Error> checkRecord(const Options& options) {
    if (options.recordSeconds.has_value() && !options.recordPath.has_value()) {
        return invalidArgument("--record-seconds needs --record: it sets the clip's length");
    }
    if (options.recordDebugDraw && !options.recordPath.has_value()) {
        return invalidArgument("--record-debug-draw needs --record: it keeps the debug lines in the clip");
    }
    if (!options.recordPath.has_value()) {
        return {};
    }
    if (options.headless || !options.loads.empty() || options.renderReferences.has_value()) {
        return invalidArgument("--record reads back the window's frames, so it cannot be combined with --headless, "
                               "--load or --render-references");
    }
    return clips::checkClipPath(*options.recordPath);
}

// Refuses the install's options in combinations that cannot work: part of checkCombinations(). An install runs on its
// own and exits, so nothing that starts the game goes with it.
std::expected<void, Error> checkInstall(const Options& options) {
    if (options.verifyInstall && !options.installFrom.has_value()) {
        return invalidArgument("--verify needs --install: it checks the files an install copied");
    }
    if (!options.installFrom.has_value()) {
        return {};
    }
    if (!options.dataPath.has_value()) {
        return invalidArgument("--install needs --data DIR: the folder to install into");
    }
    const bool other = options.discPath || !options.loads.empty() || options.viewTxd || options.viewSheet ||
                       options.viewText || options.viewWorld || options.viewCharacter || options.playLevel ||
                       options.sandbox || options.renderReferences || options.rumble || options.frameLimit ||
                       options.inputScript || options.screenshotPath || options.recordPath || options.padPipe ||
                       options.headless || options.audioTest || options.netHost || options.netJoin || options.netName;
    if (other) {
        return invalidArgument("--install runs on its own and exits: only --data and --verify go with it");
    }
    return {};
}

// Points discPath at an install folder's copy of the disc: the one `--data` names, else, with neither `--disc` nor
// `--data`, the install beside the executable when there is one. An install's `game/ps2/` is the disc's files as they
// are, so the same disc code reads it (docs/guides/asset-and-script-architecture.md#install-folder).
std::expected<void, Error> resolveDataFolder(Options& options, std::optional<std::string_view> besideExecutable) {
    if (options.installFrom.has_value()) {
        return {}; // with --install, --data is where the install goes
    }
    if (options.dataPath.has_value() && options.discPath.has_value()) {
        return invalidArgument("--data and --disc cannot be combined: both say where the game's files are");
    }
    if (!options.dataPath.has_value() && !options.discPath.has_value() && besideExecutable.has_value()) {
        options.dataPath = std::string(*besideExecutable);
    }
    if (options.dataPath.has_value()) {
        options.discPath = installGameFolder(*options.dataPath);
    }
    return {};
}

// Refuses `--world-edit` where there is no streamed world to edit: part of checkCombinations().
std::expected<void, Error> checkWorldEdit(const Options& options) {
    if (!options.worldEditFile.has_value()) {
        return {};
    }
    const bool level = options.playLevel.has_value() && !sandboxOfPlayLevel(*options.playLevel).has_value();
    if (!options.viewWorld.has_value() && !level) {
        return invalidArgument("--world-edit needs --view-world or a level's --play-level: it edits a streamed world");
    }
    return {};
}

// Refuses options that cannot work together, once the whole command line is read.
std::expected<void, Error> checkCombinations(const Options& options) {
    if (auto installing = checkInstall(options); !installing) {
        return installing;
    }
    if (auto edits = checkWorldEdit(options); !edits) {
        return edits;
    }
    if (auto renderer = checkRenderer(options); !renderer) {
        return renderer;
    }
    if (auto record = checkRecord(options); !record) {
        return record;
    }
    if (auto pacing = checkPacing(options); !pacing) {
        return pacing;
    }
    if (options.netHost.has_value() && options.netJoin.has_value()) {
        return invalidArgument("--host and --join cannot be combined: a game either hosts or joins");
    }
    if (options.audioTest && (options.noAudio || !options.loads.empty() || options.renderReferences.has_value())) {
        return invalidArgument("--audio-test plays through the sound output, so it cannot be combined with "
                               "--no-audio, --load or --render-references");
    }
    if (auto character = checkCharacterViewer(options); !character) {
        return character;
    }
    if (auto references = checkReferenceRenderer(options); !references) {
        return references;
    }
    if (auto play = checkPlayLevel(options); !play) {
        return play;
    }
    if (auto rumble = checkRumble(options); !rumble) {
        return rumble;
    }
    if (auto sandbox = checkSandbox(options); !sandbox) {
        return sandbox;
    }
    if (!options.loads.empty() && !options.discPath.has_value()) {
        return invalidArgument("--load needs --disc to say where the game's files are");
    }
    if (options.viewTxd.has_value() && !options.discPath.has_value()) {
        return invalidArgument("--view-txd needs --disc to say where the game's files are");
    }
    if (options.viewTxd.has_value() && !options.loads.empty()) {
        return invalidArgument("--view-txd and --load cannot be combined: --load runs without a window");
    }
    if (options.viewSheet.has_value() && !options.discPath.has_value()) {
        return invalidArgument("--view-sheet needs --disc to say where the game's files are");
    }
    if (options.viewSheet.has_value() && (!options.loads.empty() || options.viewTxd.has_value())) {
        return invalidArgument("--view-sheet cannot be combined with --load or --view-txd");
    }
    if (options.viewText.has_value() && !options.discPath.has_value()) {
        return invalidArgument("--view-text needs --disc to say where the game's files are");
    }
    if (options.viewText.has_value() &&
        (!options.loads.empty() || options.viewTxd.has_value() || options.viewSheet.has_value())) {
        return invalidArgument("--view-text cannot be combined with --load, --view-txd or --view-sheet");
    }
    if (options.viewWorld.has_value() && !options.discPath.has_value()) {
        return invalidArgument("--view-world needs --disc to say where the game's files are");
    }
    if (options.viewWorld.has_value() && (!options.loads.empty() || options.viewTxd.has_value() ||
                                          options.viewSheet.has_value() || options.viewText.has_value())) {
        return invalidArgument("--view-world cannot be combined with --load, --view-txd, --view-sheet or --view-text");
    }
    if (options.renderSize.has_value() &&
        (options.headless || !options.loads.empty() || options.renderReferences.has_value())) {
        return invalidArgument("--render-size sizes the window's frames, so it cannot be combined with --headless, "
                               "--load or --render-references");
    }
    if (options.drawHash &&
        (!options.frameLimit.has_value() || !options.loads.empty() || options.renderReferences.has_value())) {
        return invalidArgument("--draw-hash needs --frames N (the hash is of the last frame) and a run that draws "
                               "frames, so not --load or --render-references");
    }
    if (options.screenshotPath.has_value()) {
        if (!options.frameLimit.has_value()) {
            return invalidArgument("--screenshot needs --frames N: the screenshot is of the last frame");
        }
        if (options.headless || !options.loads.empty()) {
            return invalidArgument("--screenshot needs a window, so it cannot be combined with --headless or --load");
        }
    }
    return {};
}

// Parses the value after `--record-seconds`: a whole number from 1 to clips::kMaxClipSeconds.
std::expected<int, Error> parseRecordSeconds(std::string_view text) {
    int value = 0;
    if (isAllDigits(text)) {
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (parsed.ec == std::errc{} && value >= 1 && value <= clips::kMaxClipSeconds) {
            return value;
        }
    }
    return invalidArgument(
        std::format("--record-seconds needs a whole number from 1 to {}, got \"{}\"", clips::kMaxClipSeconds, text));
}

// Parses the value after `--fps-cap`: a whole number from 0 to kMaxFpsCap, written with decimal digits only.
std::expected<int, Error> parseFpsCap(std::string_view text) {
    int value = 0;
    if (isAllDigits(text)) {
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (parsed.ec == std::errc{} && value <= kMaxFpsCap) {
            return value;
        }
    }
    return invalidArgument(
        std::format("--fps-cap needs a whole number from 0 (no cap) to {}, got \"{}\"", kMaxFpsCap, text));
}

// The modes --rumble names, with the arena and gang size each plays in by default (the disc tests' choices).
constexpr std::array<RumbleModeName, 6> kRumbleModes{{
    {"brawl", 12, 102, 1},
    {"warparty", 14, 102, 5},
    {"kinghill", 2, 101, 3},
    {"royal", 3, 131, 3},
    {"survival", 9, 134, 1},
    {"wchair", 24, 104, 1},
}};

// The arenas' level numbers (`level101` to `level137`) and the most fighters a side the set-up holds.
constexpr int kFirstRumbleArena = 101;
constexpr int kLastRumbleArena = 137;
constexpr int kMaxGangSize = 9;
// The largest `--ai-seed`: the most an int holds, so it parses as the arena and the gang size do.
constexpr int kMaxAiSeed = 2147483647;
// The arena and gang size a numeric game type no name lists plays in: the Fight Pen, one a side.
constexpr int kFallbackArena = 102;

// The valid mode names, for an error message.
std::string rumbleNameList() {
    std::string list;
    for (const RumbleModeName& mode : kRumbleModes) {
        list += list.empty() ? "" : ", ";
        list += mode.name;
    }
    return list;
}

// Parses a value of `option` (`--arena`, `--gang-size`): a whole number from `low` to `high`, decimal digits only.
std::expected<int, Error> parseRange(std::string_view option, std::string_view text, int low, int high) {
    int value = 0;
    if (isAllDigits(text)) {
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (parsed.ec == std::errc{} && value >= low && value <= high) {
            return value;
        }
    }
    return invalidArgument(std::format("{} needs a whole number from {} to {}, got \"{}\"", option, low, high, text));
}

// Parses the value of `--host` or `--join`: `PORT` (host only), `ADDR` or `ADDR:PORT`, the port a whole number from
// `lowestPort` to 65535. IPv4 and host names only: ENet 1.3 speaks IPv4, so an address has no colon of its own.
std::expected<NetEndpoint, Error> parseEndpoint(std::string_view option, std::string_view text, bool portAlone,
                                                int lowestPort) {
    NetEndpoint endpoint;
    std::string_view portText;
    if (portAlone && isAllDigits(text)) {
        portText = text;
    } else if (const auto colon = text.find(':'); colon != std::string_view::npos) {
        endpoint.address = std::string(text.substr(0, colon));
        portText = text.substr(colon + 1);
    } else {
        endpoint.address = std::string(text);
    }
    if (endpoint.address.empty() && !portAlone) {
        return invalidArgument(std::format("{} needs an address, got \"{}\"", option, text));
    }
    if (!portText.empty() || text.ends_with(':')) {
        auto port = parseRange(option, portText, lowestPort, 65535);
        if (!port) {
            return std::unexpected(std::move(port.error()));
        }
        endpoint.port = static_cast<std::uint16_t>(*port);
    }
    return endpoint;
}

// Parses the value after `--checkpoint`: a whole number from 1 to kMaxCheckpoint, written with decimal digits only.
std::expected<int, Error> parseCheckpoint(std::string_view text) {
    int value = 0;
    if (isAllDigits(text)) {
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (parsed.ec == std::errc{} && value >= 1 && value <= kMaxCheckpoint) {
            return value;
        }
    }
    return invalidArgument(
        std::format("--checkpoint needs a whole number from 1 to {}, got \"{}\"", kMaxCheckpoint, text));
}

// Parses the value after `--start`: four or six decimal numbers separated by commas, X,Y,Z,HEADING and optionally the
// camera's DISTANCE (above 0) and YAW.
std::expected<StartPlace, Error> parseStart(std::string_view text) {
    std::vector<float> values;
    std::size_t from = 0;
    while (from <= text.size()) {
        const std::size_t comma = text.find(',', from);
        const std::string_view part = text.substr(from, comma == std::string_view::npos ? text.npos : comma - from);
        const std::optional<double> value = parseDecimal(part);
        if (!value) {
            values.clear();
            break;
        }
        values.push_back(static_cast<float>(*value));
        if (comma == std::string_view::npos) {
            break;
        }
        from = comma + 1;
    }
    if (values.size() != 4 && !(values.size() == 6 && values[4] > 0.0F)) {
        return invalidArgument(
            std::format("--start needs X,Y,Z,HEADING or X,Y,Z,HEADING,DISTANCE,YAW (decimal numbers, "
                        "the distance above 0), got \"{}\"",
                        text));
    }
    const bool camera = values.size() == 6;
    return StartPlace{.x = values[0],
                      .y = values[1],
                      .z = values[2],
                      .headingDegrees = values[3],
                      .cameraDistance = camera ? std::optional<float>(values[4]) : std::nullopt,
                      .cameraYawDegrees = camera ? std::optional<float>(values[5]) : std::nullopt};
}

// Parses the value after `--give`: ITEM,COUNT, an inventory item id from 0 to 22 and a count from 0 to kMaxGiftCount,
// whole numbers in decimal digits.
std::expected<ItemGift, Error> parseGift(std::string_view text) {
    // One part: a whole number from 0 to `high`, or nothing.
    const auto whole = [](std::string_view part, int high) -> std::optional<int> {
        int value = 0;
        if (!isAllDigits(part)) {
            return std::nullopt;
        }
        const auto parsed = std::from_chars(part.data(), part.data() + part.size(), value);
        if (parsed.ec != std::errc{} || value > high) {
            return std::nullopt;
        }
        return value;
    };
    const std::size_t comma = text.find(',');
    const std::optional<int> item = comma == std::string_view::npos ? std::nullopt : whole(text.substr(0, comma), 22);
    const std::optional<int> count =
        comma == std::string_view::npos ? std::nullopt : whole(text.substr(comma + 1), kMaxGiftCount);
    if (!item || !count) {
        return invalidArgument(
            std::format("--give needs ITEM,COUNT: an item id from 0 to 22 and a count from 0 to {}, got \"{}\"",
                        kMaxGiftCount, text));
    }
    return ItemGift{.item = *item, .count = *count};
}

// Splits `text` at its commas into decimal numbers; empty when any part is not one.
std::vector<float> parseDecimalList(std::string_view text) {
    std::vector<float> values;
    std::size_t from = 0;
    while (from <= text.size()) {
        const std::size_t comma = text.find(',', from);
        const std::string_view part = text.substr(from, comma == std::string_view::npos ? text.npos : comma - from);
        const std::optional<double> value = parseDecimal(part);
        if (!value) {
            return {};
        }
        values.push_back(static_cast<float>(*value));
        if (comma == std::string_view::npos) {
            break;
        }
        from = comma + 1;
    }
    return values;
}

// Parses the value after `--camera`: X,Y,Z,QX,QY,QZ,QW and optionally FOV, decimal numbers. The quaternion must not
// be zero (it is normalised where it is used) and the field of view must lie between 0 and 180 degrees.
std::expected<CameraPin, Error> parseCameraPin(std::string_view text) {
    const std::vector<float> v = parseDecimalList(text);
    const bool sized = v.size() == 7 || v.size() == 8;
    const bool quaternion = sized && (v[3] * v[3] + v[4] * v[4] + v[5] * v[5] + v[6] * v[6]) > 1e-12F;
    const bool lens = v.size() != 8 || (v[7] > 0.0F && v[7] < 180.0F);
    if (!sized || !quaternion || !lens) {
        return invalidArgument(std::format("--camera needs X,Y,Z,QX,QY,QZ,QW or X,Y,Z,QX,QY,QZ,QW,FOV (decimal "
                                           "numbers, a quaternion that is not zero, FOV between 0 and 180), got \"{}\"",
                                           text));
    }
    CameraPin pin{
        .x = v[0], .y = v[1], .z = v[2], .qx = v[3], .qy = v[4], .qz = v[5], .qw = v[6], .fieldOfView = 60.0F};
    if (v.size() == 8) {
        pin.fieldOfView = v[7];
    }
    return pin;
}

// One side of `--render-size`: a whole number from kMinRenderSide to kMaxRenderSide, decimal digits only.
std::optional<int> parseRenderSide(std::string_view text) {
    int value = 0;
    if (!isAllDigits(text)) {
        return std::nullopt;
    }
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || value < kMinRenderSide || value > kMaxRenderSide) {
        return std::nullopt;
    }
    return value;
}

// Parses the value after `--render-size`: WxH (or WXH), each side as parseRenderSide() takes it.
std::expected<RenderSize, Error> parseRenderSize(std::string_view text) {
    const std::size_t x = text.find_first_of("xX");
    const std::optional<int> width = x == std::string_view::npos ? std::nullopt : parseRenderSide(text.substr(0, x));
    const std::optional<int> height = x == std::string_view::npos ? std::nullopt : parseRenderSide(text.substr(x + 1));
    if (!width || !height) {
        return invalidArgument(std::format("--render-size needs WxH, each a whole number from {} to {}, got \"{}\"",
                                           kMinRenderSide, kMaxRenderSide, text));
    }
    return RenderSize{.width = *width, .height = *height};
}

} // namespace

bool isTestMode(const Options& options) {
    return options.headless || !options.loads.empty() || options.frameLimit.has_value() ||
           options.inputScript.has_value() || options.padPipe || options.screenshotPath.has_value() ||
           options.recordPath.has_value();
}

bool activatesWindow(const Options& options) {
    return !options.noActivate && !isTestMode(options) && !options.renderReferences.has_value();
}

bool takesSavedRenderer(const Options& options) {
    return !isTestMode(options) && !options.renderReferences.has_value();
}

std::expected<Options, Error> parseOptions(std::span<const std::string_view> args, OptionSet set,
                                           std::optional<std::string_view> installBesideExecutable) {
    Options options;
    std::optional<std::string> languageArg;      // as typed, so a repeat is refused like any other option
    std::optional<std::string> fpsCapArg;        // as typed, likewise
    std::optional<std::string> recordSecondsArg; // as typed, likewise
    std::optional<std::string> kindArg;          // as typed, likewise
    std::optional<std::string> vsyncArg;         // as typed, likewise
    std::optional<std::string> lineBlendArg;     // as typed, likewise
    std::optional<std::string> rendererArg;      // as typed, likewise
    std::optional<std::string> checkpointArg;    // as typed, likewise
    std::optional<std::string> startArg;         // as typed, likewise
    std::optional<std::string> rumbleArg;        // as typed, likewise
    std::optional<std::string> arenaArg;         // as typed, likewise
    std::optional<std::string> gangSizeArg;      // as typed, likewise
    std::optional<std::string> aiSeedArg;        // as typed, likewise
    std::optional<std::string> benchFightersArg; // as typed, likewise
    std::optional<std::string> benchWarmupArg;   // as typed, likewise
    std::optional<std::string> benchAlliesArg;   // as typed, likewise
    std::optional<std::string> benchSpawnArg;    // as typed, likewise
    std::optional<std::string> benchMeasureArg;  // as typed, likewise
    std::optional<std::string> benchPlaceArg;    // as typed, likewise
    std::optional<std::string> benchNeutralsArg; // as typed, likewise
    bool aiPlayer = false;                       // `--ai-player`, which goes into the Rumble launch
    std::optional<std::string> cameraArg;        // as typed, likewise
    std::optional<std::string> renderSizeArg;    // as typed, likewise
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        // The release build's command line: a developer's option is refused by name, before its value is read.
        if (set == OptionSet::Player && arg.starts_with("--") && !isPlayerOption(arg)) {
            return invalidArgument(std::format(
                "{} is a developer's option: it needs a build with the debug tools (docs/guides/building.md)", arg));
        }
        if (arg == "--help") {
            options.showHelp = true;
        } else if (arg == "--frames") {
            // Refuse a repeat rather than letting the last one win: two limits on one command line is a mistake in
            // a script, and silently picking one would hide it.
            if (options.frameLimit.has_value()) {
                return invalidArgument("--frames given twice");
            }
            if (i + 1 == args.size()) {
                return invalidArgument(std::format("--frames needs a whole number from 1 to {}", kMaxFrameLimit));
            }
            ++i; // The value is consumed here, so the loop does not read it again as an argument.
            auto limit = parseFrameLimit(args[i]);
            if (!limit) {
                return std::unexpected(std::move(limit.error()));
            }
            options.frameLimit = *limit;
        } else if (arg == "--dev-overlay") {
            if (options.devOverlayFrames.has_value()) {
                return invalidArgument("--dev-overlay given twice");
            }
            if (i + 1 == args.size()) {
                return invalidArgument(std::format("--dev-overlay needs a whole number from 1 to {}", kMaxFrameLimit));
            }
            ++i;
            auto frames = parseFrameLimit(args[i], "--dev-overlay");
            if (!frames) {
                return std::unexpected(std::move(frames.error()));
            }
            options.devOverlayFrames = *frames;
        } else if (arg == "--disc") {
            if (options.discPath.has_value()) {
                return invalidArgument("--disc given twice");
            }
            if (i + 1 == args.size() || args[i + 1].empty()) {
                return invalidArgument("--disc needs the path of a disc folder or an ISO image");
            }
            options.discPath = std::string(args[++i]);
        } else if (arg == "--install") {
            if (auto value = takeValue(args, i, options.installFrom, "--install",
                                       "the path of the PS2 disc: a mounted disc, a folder or an ISO image");
                !value) {
                return std::unexpected(std::move(value.error()));
            }
        } else if (arg == "--data") {
            if (auto value = takeValue(args, i, options.dataPath, "--data", "the install folder's path"); !value) {
                return std::unexpected(std::move(value.error()));
            }
        } else if (arg == "--verify") {
            options.verifyInstall = true;
        } else if (arg == "--load") {
            if (i + 1 == args.size() || args[i + 1].empty()) {
                return invalidArgument("--load needs an entry name or a 0x hash");
            }
            options.loads.emplace_back(args[++i]);
        } else if (arg == "--headless") {
            options.headless = true;
        } else if (arg == "--view-txd") {
            if (auto value = takeValue(args, i, options.viewTxd, "--view-txd", "an entry name or a 0x hash"); !value) {
                return std::unexpected(std::move(value.error()));
            }
        } else if (arg == "--view-sheet") {
            if (auto value = takeValue(args, i, options.viewSheet, "--view-sheet", "a sheet name or a WAD entry");
                !value) {
                return std::unexpected(std::move(value.error()));
            }
        } else if (arg == "--view-world") {
            if (auto value = takeValue(args, i, options.viewWorld, "--view-world", "a level or world name"); !value) {
                return std::unexpected(std::move(value.error()));
            }
        } else if (arg == "--play-level") {
            if (auto value = takeValue(args, i, options.playLevel, "--play-level", "a level name"); !value) {
                return std::unexpected(std::move(value.error()));
            }
        } else if (arg == "--trace") {
            if (auto value = takeValue(args, i, options.traceFile, "--trace", "the path of a CSV file"); !value) {
                return std::unexpected(std::move(value.error()));
            }
        } else if (arg == "--trace-humans") {
            if (auto value = takeValue(args, i, options.humansTraceFile, "--trace-humans", "the path of a CSV file");
                !value) {
                return std::unexpected(std::move(value.error()));
            }
        } else if (arg == "--script-trace") {
            if (auto value = takeValue(args, i, options.scriptTraceFile, "--script-trace", "the path of a text file");
                !value) {
                return std::unexpected(std::move(value.error()));
            }
        } else if (arg == "--event-log") {
            if (auto value = takeValue(args, i, options.eventLogFile, "--event-log", "the path of a CSV file");
                !value) {
                return std::unexpected(std::move(value.error()));
            }
        } else if (arg == "--log-file") {
            if (auto value = takeValue(args, i, options.logFile, "--log-file", "the path of a text file"); !value) {
                return std::unexpected(std::move(value.error()));
            }
        } else if (arg == "--log-verbose") {
            if (!options.logVerbose.empty()) {
                return invalidArgument("--log-verbose given twice: list the categories once, such as ai,combat");
            }
            if (i + 1 == args.size()) {
                return invalidArgument("--log-verbose needs log categories, such as ai,combat");
            }
            auto categories = logging::parseCategoryList(args[++i]);
            if (!categories) {
                return invalidArgument(std::format("--log-verbose: {}", categories.error().message));
            }
            options.logVerbose = std::move(*categories);
        } else if (arg == "--pad-pipe") {
            if (options.padPipe) {
                return invalidArgument("--pad-pipe given twice");
            }
            options.padPipe = true;
        } else if (arg == "--scene") {
            if (auto value = takeValue(args, i, options.scene, "--scene", "a scene's name"); !value) {
                return std::unexpected(std::move(value.error()));
            }
        } else if (arg == "--spawn") {
            if (auto value = takeValue(args, i, options.spawn, "--spawn", "a spawn point's name"); !value) {
                return std::unexpected(std::move(value.error()));
            }
        } else if (arg == "--assets") {
            if (auto value = takeValue(args, i, options.assetsDir, "--assets", "a folder"); !value) {
                return std::unexpected(std::move(value.error()));
            }
        } else if (arg == "--sandbox") {
            // The name is optional: without one (the end, or another option next) the default layout.
            if (options.sandbox.has_value()) {
                return invalidArgument("--sandbox given twice");
            }
            const bool named = i + 1 < args.size() && !args[i + 1].empty() && !args[i + 1].starts_with("--");
            options.sandbox = named ? std::string(args[++i]) : std::string(kDefaultSandbox);
        } else if (arg == "--view-character") {
            // The name is optional: without one (the end, or another option next) the viewer shows Rembrandt.
            if (options.viewCharacter.has_value()) {
                return invalidArgument("--view-character given twice");
            }
            const bool named = i + 1 < args.size() && !args[i + 1].empty() && !args[i + 1].starts_with("--");
            options.viewCharacter = named ? std::string(args[++i]) : std::string(kDefaultViewCharacter);
        } else if (arg == "--anim") {
            if (auto value = takeValue(args, i, options.animClip, "--anim", "an anim id or a clip name"); !value) {
                return std::unexpected(std::move(value.error()));
            }
        } else if (arg == "--render-references") {
            if (auto value = takeValue(args, i, options.renderReferences, "--render-references",
                                       "the folder to write the images into");
                !value) {
                return std::unexpected(std::move(value.error()));
            }
        } else if (arg == "--kind") {
            if (auto value =
                    takeValue(args, i, kindArg, "--kind", "characters, objects, cars, radar, particles or all");
                !value) {
                return std::unexpected(std::move(value.error()));
            }
            // takeValue filled kindArg; value_or keeps the access checked.
            const std::string kind = kindArg.value_or(std::string{});
            if (kind == "all") {
                options.referenceKind = ReferenceKind::All;
            } else if (kind == "characters") {
                options.referenceKind = ReferenceKind::Characters;
            } else if (kind == "objects") {
                options.referenceKind = ReferenceKind::Objects;
            } else if (kind == "cars") {
                options.referenceKind = ReferenceKind::Cars;
            } else if (kind == "radar") {
                options.referenceKind = ReferenceKind::Radar;
            } else if (kind == "particles") {
                options.referenceKind = ReferenceKind::Particles;
            } else {
                return invalidArgument(
                    std::format("--kind needs characters, objects, cars, radar, particles or all, got \"{}\"", kind));
            }
        } else if (arg == "--only") {
            if (i + 1 == args.size() || args[i + 1].empty()) {
                return invalidArgument("--only needs a model or object type name or a 0x name hash");
            }
            options.only.emplace_back(args[++i]);
        } else if (arg == "--names") {
            if (auto value = takeValue(args, i, options.namesFile, "--names", "the path of a name list"); !value) {
                return std::unexpected(std::move(value.error()));
            }
        } else if (arg == "--input-script") {
            if (auto value = takeValue(args, i, options.inputScript, "--input-script", "the path of an input script");
                !value) {
                return std::unexpected(std::move(value.error()));
            }
        } else if (arg == "--tunables") {
            if (auto value = takeValue(args, i, options.tunablesFile, "--tunables", "the path of a tunables file");
                !value) {
                return std::unexpected(std::move(value.error()));
            }
        } else if (arg == "--world-edit") {
            if (auto value = takeValue(args, i, options.worldEditFile, "--world-edit", "the path of a world edit file");
                !value) {
                return std::unexpected(std::move(value.error()));
            }
        } else if (arg == "--record-debug-draw") {
            if (options.recordDebugDraw) {
                return invalidArgument("--record-debug-draw given twice");
            }
            options.recordDebugDraw = true;
        } else if (arg == "--mesh-view") {
            if (auto value = takeValue(args, i, options.meshView, "--mesh-view", "a list of views"); !value) {
                return std::unexpected(std::move(value.error()));
            }
        } else if (arg == "--profiles") {
            if (auto value = takeValue(args, i, options.profilesDir, "--profiles", "a folder"); !value) {
                return std::unexpected(std::move(value.error()));
            }
        } else if (arg == "--view-text") {
            // Two values: the font, then the text.
            if (options.viewText.has_value()) {
                return invalidArgument("--view-text given twice");
            }
            if (i + 2 >= args.size() || args[i + 1].empty() || args[i + 2].empty()) {
                return invalidArgument("--view-text needs a font sheet and a text");
            }
            options.viewText = TextView{std::string(args[i + 1]), std::string(args[i + 2])};
            i += 2;
        } else if (arg == "--language") {
            if (auto value = takeValue(args, i, languageArg, "--language", "en, es, fr, it or de"); !value) {
                return std::unexpected(std::move(value.error()));
            }
            // takeValue filled languageArg; value_or keeps the access checked.
            const std::string code = languageArg.value_or(std::string{});
            const std::optional<Language> language = languageFromCode(code);
            if (!language) {
                return invalidArgument(std::format("--language needs en, es, fr, it or de, got \"{}\"", code));
            }
            options.language = *language;
        } else if (arg == "--fps-cap") {
            if (auto value = takeValue(args, i, fpsCapArg, "--fps-cap", "a number of frames a second (0: no cap)");
                !value) {
                return std::unexpected(std::move(value.error()));
            }
            auto cap = parseFpsCap(fpsCapArg.value_or(std::string{}));
            if (!cap) {
                return std::unexpected(std::move(cap.error()));
            }
            options.fpsCap = *cap;
        } else if (arg == "--vsync") {
            if (auto value = takeValue(args, i, vsyncArg, "--vsync", "on or off"); !value) {
                return std::unexpected(std::move(value.error()));
            }
            const std::string setting = vsyncArg.value_or(std::string{});
            if (setting != "on" && setting != "off") {
                return invalidArgument(std::format("--vsync needs on or off, got \"{}\"", setting));
            }
            options.vsync = setting == "on";
        } else if (arg == "--renderer") {
            if (auto value = takeValue(args, i, rendererArg, "--renderer", "gl3 or gpu"); !value) {
                return std::unexpected(std::move(value.error()));
            }
            const std::string name = rendererArg.value_or(std::string{});
            const std::optional<RendererKind> renderer = parseRendererName(name);
            if (!renderer) {
                return invalidArgument(std::format("--renderer needs gl3 or gpu, got \"{}\"", name));
            }
            options.renderer = renderer;
        } else if (arg == "--line-blend") {
            if (auto value = takeValue(args, i, lineBlendArg, "--line-blend", "on or off"); !value) {
                return std::unexpected(std::move(value.error()));
            }
            const std::string setting = lineBlendArg.value_or(std::string{});
            if (setting != "on" && setting != "off") {
                return invalidArgument(std::format("--line-blend needs on or off, got \"{}\"", setting));
            }
            options.lineBlend = setting == "on";
        } else if (arg == "--host") {
            // The value is optional: without one (the end, or another option next) the default port on every interface.
            if (options.netHost.has_value()) {
                return invalidArgument("--host given twice");
            }
            options.netHost = NetEndpoint{};
            if (i + 1 < args.size() && !args[i + 1].empty() && !args[i + 1].starts_with("--")) {
                auto endpoint = parseEndpoint("--host", args[++i], true, 0);
                if (!endpoint) {
                    return std::unexpected(std::move(endpoint.error()));
                }
                options.netHost = std::move(*endpoint);
            }
        } else if (arg == "--join") {
            if (options.netJoin.has_value()) {
                return invalidArgument("--join given twice");
            }
            if (i + 1 == args.size() || args[i + 1].empty()) {
                return invalidArgument("--join needs a server address, ADDR or ADDR:PORT");
            }
            auto endpoint = parseEndpoint("--join", args[++i], false, 1);
            if (!endpoint) {
                return std::unexpected(std::move(endpoint.error()));
            }
            options.netJoin = std::move(*endpoint);
        } else if (arg == "--net-name") {
            if (auto value = takeValue(args, i, options.netName, "--net-name", "a player name"); !value) {
                return std::unexpected(std::move(value.error()));
            }
        } else if (arg == "--no-audio") {
            options.noAudio = true;
        } else if (arg == "--skip-movies") {
            options.skipMovies = true;
        } else if (arg == "--cpu-skinning") {
            options.cpuSkinning = true;
        } else if (arg == "--audio-test") {
            options.audioTest = true;
        } else if (arg == "--no-activate") {
            options.noActivate = true;
        } else if (arg == "--window-display") {
            if (auto value = takeValue(args, i, options.windowDisplay, "--window-display",
                                       "a display: its number, part of its name, or portrait");
                !value) {
                return std::unexpected(std::move(value.error()));
            }
        } else if (arg == "--show-fps") {
            options.showFps = true;
        } else if (arg == "--checkpoint") {
            if (auto value = takeValue(args, i, checkpointArg, "--checkpoint", "a checkpoint number"); !value) {
                return std::unexpected(std::move(value.error()));
            }
            auto checkpoint = parseCheckpoint(checkpointArg.value_or(std::string{}));
            if (!checkpoint) {
                return std::unexpected(std::move(checkpoint.error()));
            }
            options.checkpoint = *checkpoint;
        } else if (arg == "--rumble") {
            if (auto value = takeValue(args, i, rumbleArg, "--rumble",
                                       "a mode: brawl, warparty, kinghill, royal, survival, wchair or a number");
                !value) {
                return std::unexpected(std::move(value.error()));
            }
        } else if (arg == "--arena") {
            if (auto value = takeValue(args, i, arenaArg, "--arena", "an arena's level number"); !value) {
                return std::unexpected(std::move(value.error()));
            }
        } else if (arg == "--ai-player") {
            if (aiPlayer) {
                return invalidArgument("--ai-player given twice");
            }
            aiPlayer = true;
        } else if (arg == "--ai-seed") {
            if (auto value = takeValue(args, i, aiSeedArg, "--ai-seed", "a whole number from 0"); !value) {
                return std::unexpected(std::move(value.error()));
            }
            auto seed = parseRange("--ai-seed", aiSeedArg.value_or(std::string{}), 0, kMaxAiSeed);
            if (!seed) {
                return std::unexpected(std::move(seed.error()));
            }
            options.aiSeed = static_cast<std::uint32_t>(*seed);
        } else if (arg == "--bench") {
            if (auto value = takeValue(args, i, options.benchFile, "--bench", "the path of a JSON file"); !value) {
                return std::unexpected(std::move(value.error()));
            }
        } else if (arg == "--bench-fighters") {
            if (auto value = takeValue(args, i, benchFightersArg, "--bench-fighters", "a number of fighters"); !value) {
                return std::unexpected(std::move(value.error()));
            }
            auto fighters =
                parseRange("--bench-fighters", benchFightersArg.value_or(std::string{}), 0, kMaxBenchFighters);
            if (!fighters) {
                return std::unexpected(std::move(fighters.error()));
            }
            options.benchFighters = *fighters;
        } else if (arg == "--bench-warmup") {
            if (auto value = takeValue(args, i, benchWarmupArg, "--bench-warmup", "a number of frames"); !value) {
                return std::unexpected(std::move(value.error()));
            }
            auto warmup = parseRange("--bench-warmup", benchWarmupArg.value_or(std::string{}), 0, kMaxFrameLimit);
            if (!warmup) {
                return std::unexpected(std::move(warmup.error()));
            }
            options.benchWarmup = *warmup;
        } else if (arg == "--bench-allies") {
            if (auto value = takeValue(args, i, benchAlliesArg, "--bench-allies", "a number of allies"); !value) {
                return std::unexpected(std::move(value.error()));
            }
            auto allies = parseRange("--bench-allies", benchAlliesArg.value_or(std::string{}), 0, kMaxBenchFighters);
            if (!allies) {
                return std::unexpected(std::move(allies.error()));
            }
            options.benchAllies = *allies;
        } else if (arg == "--bench-god") {
            options.benchGod = true;
        } else if (arg == "--bench-spawn-step") {
            if (auto value = takeValue(args, i, benchSpawnArg, "--bench-spawn-step", "a number of steps"); !value) {
                return std::unexpected(std::move(value.error()));
            }
            auto step = parseRange("--bench-spawn-step", benchSpawnArg.value_or(std::string{}), 0, kMaxFrameLimit);
            if (!step) {
                return std::unexpected(std::move(step.error()));
            }
            options.benchSpawnStep = *step;
        } else if (arg == "--bench-measure") {
            if (auto value = takeValue(args, i, benchMeasureArg, "--bench-measure", "a number of frames"); !value) {
                return std::unexpected(std::move(value.error()));
            }
            auto measure = parseRange("--bench-measure", benchMeasureArg.value_or(std::string{}), 1, kMaxFrameLimit);
            if (!measure) {
                return std::unexpected(std::move(measure.error()));
            }
            options.benchMeasure = *measure;
        } else if (arg == "--bench-place") {
            if (auto value = takeValue(args, i, benchPlaceArg, "--bench-place", "X,Y,Z,HEADING"); !value) {
                return std::unexpected(std::move(value.error()));
            }
            // As --start's spot, without its camera.
            auto place = parseStart(benchPlaceArg.value_or(std::string{}));
            if (!place || place->cameraDistance.has_value()) {
                return invalidArgument(std::format("--bench-place needs X,Y,Z,HEADING (decimal numbers), got \"{}\"",
                                                   benchPlaceArg.value_or(std::string{})));
            }
            options.benchPlace = *place;
        } else if (arg == "--bench-fps") {
            options.benchFps = true;
        } else if (arg == "--bench-neutrals") {
            if (auto value = takeValue(args, i, benchNeutralsArg, "--bench-neutrals", "a number of neutrals"); !value) {
                return std::unexpected(std::move(value.error()));
            }
            auto neutrals =
                parseRange("--bench-neutrals", benchNeutralsArg.value_or(std::string{}), 0, kMaxBenchFighters);
            if (!neutrals) {
                return std::unexpected(std::move(neutrals.error()));
            }
            options.benchNeutrals = *neutrals;
        } else if (arg == "--bench-real-time") {
            options.benchRealTime = true;
        } else if (arg == "--bench-frames") {
            if (auto value = takeValue(args, i, options.benchFramesFile, "--bench-frames", "the path of a CSV file");
                !value) {
                return std::unexpected(std::move(value.error()));
            }
        } else if (arg == "--gang-size") {
            if (auto value = takeValue(args, i, gangSizeArg, "--gang-size", "the fighters a side"); !value) {
                return std::unexpected(std::move(value.error()));
            }
        } else if (arg == "--start") {
            if (auto value = takeValue(args, i, startArg, "--start", "X,Y,Z,HEADING[,DISTANCE,YAW]"); !value) {
                return std::unexpected(std::move(value.error()));
            }
            auto start = parseStart(startArg.value_or(std::string{}));
            if (!start) {
                return std::unexpected(std::move(start.error()));
            }
            options.start = *start;
        } else if (arg == "--give") {
            if (i + 1 == args.size() || args[i + 1].empty()) {
                return invalidArgument("--give needs ITEM,COUNT");
            }
            auto gift = parseGift(args[++i]);
            if (!gift) {
                return std::unexpected(std::move(gift.error()));
            }
            options.gives.push_back(*gift);
        } else if (arg == "--camera") {
            if (auto value = takeValue(args, i, cameraArg, "--camera", "X,Y,Z,QX,QY,QZ,QW[,FOV]"); !value) {
                return std::unexpected(std::move(value.error()));
            }
            auto pin = parseCameraPin(cameraArg.value_or(std::string{}));
            if (!pin) {
                return std::unexpected(std::move(pin.error()));
            }
            options.cameraPin = *pin;
        } else if (arg == "--render-size") {
            if (auto value = takeValue(args, i, renderSizeArg, "--render-size", "WxH, such as 640x448"); !value) {
                return std::unexpected(std::move(value.error()));
            }
            auto size = parseRenderSize(renderSizeArg.value_or(std::string{}));
            if (!size) {
                return std::unexpected(std::move(size.error()));
            }
            options.renderSize = *size;
        } else if (arg == "--draw-hash") {
            if (options.drawHash) {
                return invalidArgument("--draw-hash given twice");
            }
            options.drawHash = true;
        } else if (arg == "--freeze-world") {
            if (options.freezeWorld) {
                return invalidArgument("--freeze-world given twice");
            }
            options.freezeWorld = true;
        } else if (arg == "--record") {
            if (auto value = takeValue(args, i, options.recordPath, "--record", "the path of an MP4 file"); !value) {
                return std::unexpected(std::move(value.error()));
            }
        } else if (arg == "--record-seconds") {
            if (auto value = takeValue(args, i, recordSecondsArg, "--record-seconds", "a number of seconds"); !value) {
                return std::unexpected(std::move(value.error()));
            }
            auto seconds = parseRecordSeconds(recordSecondsArg.value_or(std::string{}));
            if (!seconds) {
                return std::unexpected(std::move(seconds.error()));
            }
            options.recordSeconds = *seconds;
        } else if (arg == "--screenshot") {

            if (auto value = takeValue(args, i, options.screenshotPath, "--screenshot", "the path of a PNG file");
                !value) {
                return std::unexpected(std::move(value.error()));
            }
        } else {
            // Unknown options and stray positionals alike: a typo ignored silently would run something else.
            return invalidArgument(std::format("unknown argument \"{}\"", arg));
        }
    }
    if (options.showHelp) {
        return options; // --help wins over every other mistake: the user is asking how to get it right
    }
    if (!rumbleArg && (arenaArg || gangSizeArg)) {
        return invalidArgument("--arena and --gang-size need --rumble: they set the match it starts");
    }
    if (!rumbleArg && aiPlayer) {
        return invalidArgument("--ai-player needs --rumble: it hands player 1 of a match to the AI");
    }
    if (rumbleArg) {
        std::optional<int> arena;
        std::optional<int> gangSize;
        if (arenaArg) {
            auto parsed = parseRange("--arena", *arenaArg, kFirstRumbleArena, kLastRumbleArena);
            if (!parsed) {
                return std::unexpected(std::move(parsed.error()));
            }
            arena = *parsed;
        }
        if (gangSizeArg) {
            auto parsed = parseRange("--gang-size", *gangSizeArg, 1, kMaxGangSize);
            if (!parsed) {
                return std::unexpected(std::move(parsed.error()));
            }
            gangSize = *parsed;
        }
        auto launch = resolveRumbleLaunch(*rumbleArg, arena, gangSize);
        if (!launch) {
            return std::unexpected(std::move(launch.error()));
        }
        launch->aiPlayer = aiPlayer;
        options.rumble = *launch;
        // `--trace FILE` in a Rumble match traces every human: there is no single player level to trace the player of.
        if (options.traceFile.has_value() && !options.humansTraceFile.has_value()) {
            options.humansTraceFile = std::move(options.traceFile);
            options.traceFile.reset();
        }
    }
    if (auto data = resolveDataFolder(options, installBesideExecutable); !data) {
        return std::unexpected(std::move(data.error()));
    }
    if (auto combined = checkCombinations(options); !combined) {
        return std::unexpected(std::move(combined.error()));
    }
    return options;
}

std::string_view usageText() { return kUsage; }

std::span<const std::string_view> playerOptions() { return kPlayerOptions; }

bool isPlayerOption(std::string_view option) {
    return std::ranges::find(kPlayerOptions, option) != kPlayerOptions.end();
}

std::string usageText(OptionSet set) {
    if (set == OptionSet::All) {
        return std::string(kUsage);
    }
    // The options' lines of the full text, each option with its continuation lines, kept for a player's options.
    std::string text(kPlayerSynopsis);
    const std::string_view usage = kUsage.substr(kUsage.find("\n\n") + 2);
    bool keep = false;
    std::size_t at = 0;
    while (at < usage.size()) {
        const std::size_t end = usage.find('\n', at);
        const std::size_t next = end == std::string_view::npos ? usage.size() : end + 1;
        const std::string_view line = usage.substr(at, next - at);
        if (line.starts_with("  --")) {
            const std::string_view name = line.substr(2, line.find_first_of(" \n", 2) - 2);
            keep = isPlayerOption(name);
        } else if (!line.starts_with("   ")) {
            keep = false; // a line that is neither an option nor its continuation ends the option before it
        }
        if (keep) {
            text += line;
        }
        at = next;
    }
    return text;
}

std::string installGameFolder(std::string_view install) {
    std::string folder(install);
    if (!folder.empty() && folder.back() != '/' && folder.back() != '\\') {
        folder.push_back('/');
    }
    return folder + "game/ps2";
}

std::span<const RumbleModeName> rumbleModeNames() { return kRumbleModes; }

std::expected<RumbleLaunch, Error> resolveRumbleLaunch(std::string_view type, std::optional<int> arena,
                                                       std::optional<int> gangSize) {
    RumbleLaunch launch;
    const auto named = std::ranges::find(kRumbleModes, type, &RumbleModeName::name);
    if (named != kRumbleModes.end()) {
        launch = {.gameType = named->gameType, .arena = named->arena, .gangSize = named->gangSize};
    } else {
        // A number: any `RM_*` game type, which the arena's own script may or may not support.
        int number = 0;
        const auto parsed = std::from_chars(type.data(), type.data() + type.size(), number);
        if (!isAllDigits(type) || parsed.ec != std::errc{} || number < 1 || number > 99) {
            return invalidArgument(std::format("--rumble needs a mode, one of {} or a game type's number, got \"{}\"",
                                               rumbleNameList(), type));
        }
        const auto listed = std::ranges::find(
            kRumbleModes, number, [](const RumbleModeName& mode) { return static_cast<int>(mode.gameType); });
        launch =
            listed != kRumbleModes.end()
                ? RumbleLaunch{.gameType = listed->gameType, .arena = listed->arena, .gangSize = listed->gangSize}
                : RumbleLaunch{.gameType = static_cast<std::uint16_t>(number), .arena = kFallbackArena, .gangSize = 1};
    }
    if (arena) {
        launch.arena = *arena;
    }
    if (gangSize) {
        launch.gangSize = static_cast<std::uint16_t>(*gangSize);
    }
    return launch;
}

std::optional<std::string> sandboxOfPlayLevel(std::string_view name) {
    if (name == kSandboxLevelPrefix) {
        return std::string(kDefaultSandbox);
    }
    if (name.starts_with(kSandboxLevelPrefix) && name.size() > kSandboxLevelPrefix.size() + 1 &&
        name[kSandboxLevelPrefix.size()] == ':') {
        return std::string(name.substr(kSandboxLevelPrefix.size() + 1));
    }
    return std::nullopt;
}

} // namespace coney
