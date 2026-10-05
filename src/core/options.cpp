// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/options.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace coney {

namespace {

// The text usageText() returns (see its doc comment in options.h).
constexpr std::string_view kUsage =
    "Usage: coney [--disc PATH] [--load ENTRY]... [--view-txd ENTRY] [--view-sheet SHEET] [--frames N]\n"
    "             [--screenshot PATH] [--headless] [--help]\n"
    "             [--input-script FILE] [--view-text FONT TEXT] [--language CODE] [--tunables FILE]\n"
    "             [--view-world NAME] [--view-character [NAME]] [--anim CLIP]\n"
    "             [--play-level NAME [--spawn NAME]] [--sandbox [NAME]] [--assets DIR] [--dev-overlay N]\n"
    "             [--render-references DIR [--only NAME]... [--names FILE]]\n"
    "             [--fps-cap N] [--vsync on|off] [--show-fps]\n"
    "\n"
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
    "                     (level2, level99: any level with a streamed world on the disc),\n"
    "                     or sandbox:NAME for a sandbox layout (sandbox alone: default); needs --disc\n"
    "  --spawn NAME       with --play-level sandbox:NAME: the layout's spawn point to start at\n"
    "  --sandbox [NAME]   fly round a sandbox test world: default (the default), parkour, or a\n"
    "                     .layout file; needs no disc\n"
    "  --assets DIR       the folder of Coney's own assets (sandbox layouts and textures), in place\n"
    "                     of the assets folder beside the executable\n"
    "  --render-references DIR\n"
    "                     write a 256x256 PNG of every character, standing, into DIR and exit;\n"
    "                     needs --disc and a display (the window stays hidden)\n"
    "  --only NAME        with --render-references: render only this character (a model name or a\n"
    "                     0x name hash); repeatable\n"
    "  --names FILE       with --render-references: model names, one per line, to name the images by\n"
    "  --fps-cap N        draw at most N frames a second (0, the default: no cap); the game runs at\n"
    "                     its fixed 30 steps a second whatever the rate; 30 draws one frame per step\n"
    "  --vsync on|off     wait for the display's vertical blank when presenting (default on)\n"
    "  --show-fps         print the frame and step rates once a second\n"
    "  --frames N         stop after N frames (1 to 1000000); used by tests and CI. Test mode: with\n"
    "                     --frames, --headless, --input-script or --screenshot each frame is one\n"
    "                     step and one render, with no clock, so a run is the same every time\n"
    "  --screenshot PATH  save the last frame as a PNG; needs --frames and a window\n"
    "  --input-script FILE\n"
    "                     play the pad input in FILE instead of the keyboard and gamepads\n"
    "  --tunables FILE    the debug menus' tunable overrides to load and save (default: coney-tunables.ini\n"
    "                     in your config folder)\n"
    "  --dev-overlay N    show the developer overlay (F1) for the first N frames, then hide it; a test aid\n"
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

// Refuses the sandbox's options in combinations that cannot work: part of checkCombinations().
std::expected<void, Error> checkSandbox(const Options& options) {
    if (options.spawn.has_value() && !(options.playLevel && sandboxOfPlayLevel(*options.playLevel))) {
        return invalidArgument("--spawn needs --play-level sandbox:NAME: it names a sandbox layout's spawn point");
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
        if (!options.only.empty() || options.namesFile.has_value()) {
            return invalidArgument("--only and --names need --render-references");
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

// Refuses options that cannot work together, once the whole command line is read.
std::expected<void, Error> checkCombinations(const Options& options) {
    if (auto pacing = checkPacing(options); !pacing) {
        return pacing;
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

} // namespace

bool isTestMode(const Options& options) {
    return options.headless || !options.loads.empty() || options.frameLimit.has_value() ||
           options.inputScript.has_value() || options.screenshotPath.has_value();
}

std::expected<Options, Error> parseOptions(std::span<const std::string_view> args) {
    Options options;
    std::optional<std::string> languageArg; // as typed, so a repeat is refused like any other option
    std::optional<std::string> fpsCapArg;   // as typed, likewise
    std::optional<std::string> vsyncArg;    // as typed, likewise
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view arg = args[i];
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
        } else if (arg == "--only") {
            if (i + 1 == args.size() || args[i + 1].empty()) {
                return invalidArgument("--only needs a model name or a 0x name hash");
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
        } else if (arg == "--show-fps") {
            options.showFps = true;
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
    if (auto combined = checkCombinations(options); !combined) {
        return std::unexpected(std::move(combined.error()));
    }
    return options;
}

std::string_view usageText() { return kUsage; }

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
