// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/options.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <format>
#include <string>
#include <system_error>
#include <utility>

namespace coney {

namespace {

// The text usageText() returns (see its doc comment in options.h).
constexpr std::string_view kUsage =
    "Usage: coney [--disc PATH] [--load ENTRY]... [--frames N] [--help]\n"
    "\n"
    "  --disc PATH    the game's disc: a mounted disc, a folder of its files or an ISO image\n"
    "  --load ENTRY   load a WAD entry (a name such as level1.lev, or a hash such as 0x7e23a6f2)\n"
    "                 through the chunk system and print a summary; repeatable; needs --disc;\n"
    "                 runs without a window and exits when every entry is loaded\n"
    "  --frames N     stop after N frames (1 to 1000000); used by tests and CI\n"
    "  --help         show this text and exit\n";

std::unexpected<Error> invalidArgument(std::string message) {
    return std::unexpected(Error{ErrorCode::InvalidArgument, std::move(message)});
}

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

} // namespace

std::expected<Options, Error> parseOptions(std::span<const std::string_view> args) {
    Options options;
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
        } else {
            // Unknown options and stray positionals alike: a typo ignored silently would run something else.
            return invalidArgument(std::format("unknown argument \"{}\"", arg));
        }
    }
    if (!options.loads.empty() && !options.discPath.has_value() && !options.showHelp) {
        return invalidArgument("--load needs --disc to say where the game's files are");
    }
    return options;
}

std::string_view usageText() { return kUsage; }

} // namespace coney
