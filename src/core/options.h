// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

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
    /// `--screenshot`: save the last frame as a PNG at this path. Requires frameLimit and a window (not headless or
    /// `--load`).
    std::optional<std::string> screenshotPath;
    /// `--input-script`: play the pad input in this file (src/core/input_script.h) instead of reading the keyboard
    /// and gamepads: the scripted input of test mode.
    std::optional<std::string> inputScript;
    /// `--view-text FONT TEXT`: lay out and draw a text. Requires discPath; cannot be combined with `--load`,
    /// `--view-txd` or `--view-sheet`.
    std::optional<TextView> viewText;
    /// `--language`: the language of the UI strings (`en`, `es`, `fr`, `it`, `de`); English by default.
    Language language = Language::English;
    /// `--render-references`: the folder to write a reference image of every character into, then exit. Requires
    /// discPath and a window (not headless); cannot be combined with `--load`, the viewers, `--frames`,
    /// `--screenshot` or `--input-script`.
    std::optional<std::string> renderReferences;
    /// `--only`, in the order given: the characters `--render-references` renders, each a model name or a `0x` name
    /// hash; empty renders every one. Requires renderReferences.
    std::vector<std::string> only;
    /// `--names`: a text file of model names, one per line, that `--render-references` files images under. Requires
    /// renderReferences.
    std::optional<std::string> namesFile;
    /// `--fps-cap N`: draw at most N frames a second, 0 for no cap (the default; vsync still limits the rate while
    /// it is on). 30 is the original's rhythm: one step and one render per frame, nothing blended
    /// (docs/guides/building.md#frame-rate). Not in test mode.
    std::optional<int> fpsCap;
    /// `--vsync on|off`: whether a present waits for the display's vertical blank; on by default. Needs a window.
    bool vsync = true;
    /// `--show-fps`: print the frame and step rates once a second, and their totals at the end. Not in test mode.
    bool showFps = false;
};

/// The largest `--fps-cap`.
inline constexpr int kMaxFpsCap = 1000;

/// Whether `options` ask for test mode: `--headless`, `--load`, `--frames`, `--input-script` or `--screenshot`. Test
/// mode runs the main loop in lockstep, one fixed step and one render per frame, and never reads a real clock, so a run
/// gives the same result every time on any machine (docs/guides/conventions.md#platform-code).
[[nodiscard]] bool isTestMode(const Options& options);

/// The character `--view-character` shows without a name: Rembrandt, the player of level99 (warr_re_cv).
inline constexpr std::string_view kDefaultViewCharacter = "warr_re_cv";

/// Largest accepted `--frames` value: about 4.6 hours at 60 Hz, far beyond any test, and well inside `int`.
inline constexpr int kMaxFrameLimit = 1'000'000;

/// Parses the arguments after the program name. Never throws; a bad argument gives ErrorCode::InvalidArgument with
/// a one-line message naming the argument.
[[nodiscard]] std::expected<Options, Error> parseOptions(std::span<const std::string_view> args);

/// The usage text printed for `--help` and after an argument error. It ends with a newline. Print it by its size
/// (`%.*s` or fwrite), as with any string_view: callers must not rely on data() being null-terminated.
[[nodiscard]] std::string_view usageText();

} // namespace coney
