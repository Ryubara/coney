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
    "             [--input-script FILE] [--view-text FONT TEXT] [--language CODE]\n"
    "             [--view-world NAME] [--view-character [NAME]] [--anim CLIP]\n"
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
    "  --frames N         stop after N frames (1 to 1000000); used by tests and CI\n"
    "  --screenshot PATH  save the last frame as a PNG; needs --frames and a window\n"
    "  --input-script FILE\n"
    "                     play the pad input in FILE instead of the keyboard and gamepads\n"
    "  --headless         run with no window and no GPU (nothing is drawn)\n"
    "  --help             show this text and exit\n";

// Shorthand for the one error code every option mistake uses.
std::unexpected<Error> invalidArgument(std::string message) {
    return std::unexpected(Error{ErrorCode::InvalidArgument, std::move(message)});
}

// True for a non-empty string of ASCII digits only; no sign, spaces or locale digits.
bool isAllDigits(std::string_view text) {
    return !text.empty() && std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; });
}

/// Parses the value after `--frames`: a whole number from 1 to kMaxFrameLimit, written with decimal digits only.
std::expected<int, Error> parseFrameLimit(std::string_view text) {
    auto badValue = [text] {
        return invalidArgument(
            std::format("--frames needs a whole number from 1 to {}, got \"{}\"", kMaxFrameLimit, text));
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

// Refuses options that cannot work together, once the whole command line is read.
std::expected<void, Error> checkCombinations(const Options& options) {
    if (auto character = checkCharacterViewer(options); !character) {
        return character;
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

} // namespace

std::expected<Options, Error> parseOptions(std::span<const std::string_view> args) {
    Options options;
    std::optional<std::string> languageArg; // as typed, so a repeat is refused like any other option
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
        } else if (arg == "--input-script") {
            if (auto value = takeValue(args, i, options.inputScript, "--input-script", "the path of an input script");
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

} // namespace coney
