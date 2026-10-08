// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "core/language.h"
#include "graphics/overlay_camera.h"
#include "graphics/particle_page.h"
#include "graphics/render_device.h"
#include "graphics/sprite_batch.h"
#include "hud/spinner.h"

namespace coney {

/// The pictures a level's loading screen shows, in order, and whether the search fell back to `default_ls_0` (the
/// screen object's `+0x14`).
///
/// Research: docs/research/level-loading.md#level-screen
struct LoadScreenPictures {
    std::vector<std::string> names; ///< One to three resource names.
    bool fellBack = false;          ///< The last name is `default_ls_0`, found by the fallback.
};

/// The highest level number with story pictures and the 23 s timeline: the story levels and `level100`. Above it are
/// the Rumble arenas (one `rumble_<g>` picture, 30 s).
inline constexpr int kLastStoryLoadScreenLevel = 100;

/// The name of picture `n` of `level` (`level99`), in the order the original searches (`0x00163270`): `<level>_ls_<n>`
/// with `_w` for 16:9 and the language's suffix (`_sp`, `_fr`, `_it`, `_ge`); without the suffix; picture 0's
/// name; `default_ls_0`. The first name `exists` says the archive holds wins; `fellBack` is set when none did.
///
/// Research: docs/research/level-loading.md#level-screen
/// @orig 0x00163270 LoadScreen_FormatTextureName (unknown)
[[nodiscard]] std::string loadScreenPictureName(std::string_view level, int n, Language language, bool widescreen,
                                                const std::function<bool(std::string_view)>& exists,
                                                bool* fellBack = nullptr);

/// The Rumble form (`0x001635d0`): `rumble_<gameType>_ls_0` with the 16:9 and language forms as above, else
/// `default_ls_0` without a check.
///
/// Research: docs/research/level-loading.md#level-screen
/// @orig 0x001635d0 LoadScreen_FormatTextureNameEx (unknown)
[[nodiscard]] std::string rumbleLoadScreenPictureName(int gameType, Language language, bool widescreen,
                                                      const std::function<bool(std::string_view)>& exists,
                                                      bool* fellBack = nullptr);

/// The pictures of the level numbered `levelNumber` named `level`: for a story level (number up to 100) picture 0, 1
/// and 2, stopping at the first that repeats the name before it (so a level with one picture gets one); for a Rumble
/// arena one picture from `rumbleGameType` (the Rumble set-up's game type, `0x0063eec2`).
///
/// Research: docs/research/level-loading.md#level-screen
/// @orig 0x00163888 LevelLoadScreen_Start (unknown)
[[nodiscard]] LoadScreenPictures loadScreenPictures(std::string_view level, int levelNumber, int rumbleGameType,
                                                    Language language, bool widescreen,
                                                    const std::function<bool(std::string_view)>& exists);

/// The memory-card screen's two pictures: picture 0 `memory_card_screen` (the 16:9 and language forms as a level's,
/// else the plain name), picture 1 `memory_card_loading` (`_w` with the 16:9 option).
///
/// Research: docs/research/level-loading.md#memory-card-screen
/// @orig 0x001620a0 MemCardLoadScreen_Start (unknown)
[[nodiscard]] LoadScreenPictures memoryCardPictures(Language language, bool widescreen,
                                                    const std::function<bool(std::string_view)>& exists);

/// The memory-card screen's timeline (21 s) and when it changes to its second picture (5 s in).
inline constexpr std::uint64_t kMemoryCardTimelineMilliseconds = 21000;
inline constexpr std::uint64_t kMemoryCardFirstPictureMilliseconds = 5000;

/// The sprite sheet the spinner is drawn from on the memory-card screen.
inline constexpr std::string_view kSpinnerSheet = "part_page0";

/// How long a level's loading timeline lasts, in milliseconds: 23,000 for a level numbered up to 100, 30,000 above.
[[nodiscard]] constexpr std::uint64_t loadScreenTimelineMilliseconds(int levelNumber) {
    return levelNumber <= kLastStoryLoadScreenLevel ? 23000 : 30000;
}

/// The progress bar's colour (alpha aside): red (170, 43, 43), or grey (223, 223, 223) for the levels numbered 11,
/// 20, 82, 83 and 92.
///
/// Research: docs/research/level-loading.md#level-screen
/// @orig 0x00162688 LevelLoadScreen_DrawBar (unknown)
[[nodiscard]] graphics::Rgba loadScreenBarColour(int levelNumber);

/// The progress bar's rectangle for progress `p` (0 to 1) in the interlaced 4:3 mode or with the 16:9 option: its
/// top-left corner is the point (x0, y0) at depth 1 through the overlay camera `camera`, and it is `W` × `p` pixels
/// wide and 8 tall (x0 0.04, y0 −0.328, W 273 in 4:3; 0.225, −0.352, 212 in 16:9). Coney offers neither progressive
/// mode.
///
/// Research: docs/research/level-loading.md#level-screen
[[nodiscard]] graphics::LogicalQuad loadScreenBarQuad(const graphics::OverlayCamera& camera, bool widescreen, float p,
                                                      graphics::Rgba colour);

/// The loading screen's clock: a start and an end in milliseconds, and what a tick draws at `now`. The original reads
/// the real-time clock; Coney passes game time (see LoadingScreen).
///
/// Research: docs/research/level-loading.md#level-screen
struct LoadScreenTimeline {
    /// The fade in and the fade out, each 200 ms.
    static constexpr std::uint64_t kFadeMilliseconds = 200;
    /// The finish ticks until now is this close to the moved end: about 170 ms of fade out.
    static constexpr std::uint64_t kFinishMarginMilliseconds = 30;

    std::uint64_t start = 0; ///< The screen's start (`+0x18`).
    std::uint64_t end = 0;   ///< Its end (`+0x1c`): start + the timeline, then now + 200 at the finish.

    /// The picture shown at `now` of `count`: floor((now − start) / (end − start) × count), at most count − 1. A cut,
    /// no cross-fade.
    [[nodiscard]] int picture(std::uint64_t now, int count) const;
    /// The alpha at `now`: rising over the first 200 ms, falling over the last 200 ms before the end, else 255
    /// (unsigned differences: past the end it stays 255).
    [[nodiscard]] std::uint8_t alpha(std::uint64_t now) const;
    /// The bar's progress at `now`: (now − start) / (end − start), at most 1. A clock, not a measure of the load.
    [[nodiscard]] float progress(std::uint64_t now) const;
};

/// Where the loading screen's sounds go: the platform hands them to the sound engine (audio::SoundEngine's
/// startLoadScreen and endLoadScreen, which pick the bank `load_NN` and play its two halves; core does not link the
/// audio library). Either function may be empty.
///
/// Research: docs/research/sound.md#banks
struct LoadScreenSounds {
    std::function<void()> start; ///< `AudioManager_StartLoadScreen` (`0x00111178`): the next bank and its two sounds.
    std::function<void()> stop;  ///< `AudioManager_StopLoadScreen` (`0x00111428`): stops both.
};

/// What picks the pictures besides the level: the language (`W_GameState + 0x120`) and the 16:9 option.
struct LoadScreenSettings {
    Language language = Language::English;
    bool widescreen = false;
};

/// The level loading screen (`0x005e6da8`): pictures, a timed bar, fades, the load-screen sounds. Not a game mode: the
/// mode that loads (GameplayMode) begins it, draws it each frame while it loads, finishes it and ends it, as
/// `InitLevel` does around its blocking reads (docs/research/level-loading.md#loading-screen). Nothing reads the pads.
///
/// The clock is the caller's: Coney passes game time on the fixed 1/30 s step where the original reads real time, so a
/// test run draws the same frames.
///
/// Research: docs/research/level-loading.md#level-screen
class LoadingScreen {
  public:
    /// Loads a sprite sheet by resource name (the platform reads it from the disc).
    using SheetLoader = std::function<std::expected<graphics::SpriteSheet, Error>(std::string_view resourceName)>;
    /// Whether the archive holds the resource named so (its file is the decimal CRC-32 of the name).
    using ResourceExists = std::function<bool(std::string_view resourceName)>;

    /// Draws through `device`, reads pictures with `loadSheet` after asking `exists`, starts and stops the sounds
    /// through `sounds`. `log` gets a line per begin and per failed picture. `device` must outlive the screen.
    LoadingScreen(graphics::RenderDevice& device, SheetLoader loadSheet, ResourceExists exists,
                  LoadScreenSettings settings, LoadScreenSounds sounds, std::function<void(std::string_view)> log);

    /// `LoadScreen_Begin` and the screen's start: picks and loads the pictures of `level` (numbered `levelNumber`;
    /// `rumbleGameType` for an arena) and sets the timeline from `nowMs`. The 200 ms fade in is the caller's to wait.
    /// @orig 0x001612b0 LoadScreen_Begin (unknown)
    /// @orig 0x00163888 LevelLoadScreen_Start (unknown)
    void begin(std::string_view level, int levelNumber, int rumbleGameType, std::uint64_t nowMs);
    /// The memory-card screen's start (the front end's load at start-up, while the profile manager's load-screen flag
    /// is set): memoryCardPictures() and the spinner's sheet loaded, a 21 s timeline from `nowMs`.
    /// @orig 0x001620a0 MemCardLoadScreen_Start (unknown)
    void beginMemoryCard(std::uint64_t nowMs);
    /// The HUD's spinner the memory-card screen pulses (it turns it on and leaves its colour on it); null: one of the
    /// screen's own. Must outlive the screen.
    void setSpinner(hud::Spinner* spinner) { m_spinner = spinner; }

    /// Starts the load-screen sounds (step 3 of `InitLevel`).
    void startSounds();
    /// Stops the load-screen sounds (step 11).
    void stopSounds();

    /// The finish: the end moves to `nowMs` + 200 (the bar jumps near full, the picture to the last one), and the
    /// caller ticks until finished().
    /// @orig 0x00162598 LevelLoadScreen_Finish (unknown)
    void finish(std::uint64_t nowMs);
    /// Whether the finish's fade out is over at `nowMs`: now ≥ end − 30.
    [[nodiscard]] bool finished(std::uint64_t nowMs) const;
    /// `LoadScreen_End`'s last part: releases the pictures and zeroes the timeline.
    /// @orig 0x00161378 LoadScreen_End (unknown)
    void end();

    /// A tick's drawing at `nowMs`: black, the current picture with the alpha if it is loaded and the alpha is above
    /// 10, then the bar (alpha 255 when the picture is not loaded); presents.
    /// @orig 0x00162b88 LevelLoadScreen_Tick (unknown)
    void render(std::uint64_t nowMs) const;

    /// Whether the last begin was the memory-card screen's.
    [[nodiscard]] bool memoryCard() const { return m_memoryCard; }

    /// Whether the screen is begun and not ended.
    [[nodiscard]] bool active() const { return m_active; }
    /// The pictures chosen by the last begin.
    [[nodiscard]] const LoadScreenPictures& pictures() const { return m_pictures; }
    /// The clock.
    [[nodiscard]] const LoadScreenTimeline& timeline() const { return m_timeline; }
    /// The level number of the last begin.
    [[nodiscard]] int levelNumber() const { return m_levelNumber; }

  private:
    graphics::RenderDevice& m_device;
    SheetLoader m_loadSheet;
    ResourceExists m_exists;
    LoadScreenSettings m_settings;
    LoadScreenSounds m_sounds;
    std::function<void(std::string_view)> m_log;
    bool m_active = false;
    int m_levelNumber = 0;
    LoadScreenPictures m_pictures;
    std::vector<std::optional<graphics::SpriteSheet>> m_sheets; // one per picture; empty when it failed to load
    LoadScreenTimeline m_timeline;
    bool m_memoryCard = false;
    // The memory-card screen's spinner: the HUD's (setSpinner()) or its own, drawn from part_page0 through `m_parts`.
    hud::Spinner* m_spinner = nullptr;
    mutable hud::Spinner m_ownSpinner;
    mutable std::optional<graphics::SpriteBatch> m_parts;

    // The memory-card screen's tick: picture 0 for 5 s, then picture 1 with the spinner's pulse over it; no bar.
    // @orig 0x001619d0 MemCardLoadScreen_Tick (unknown)
    void renderMemoryCard(std::uint64_t nowMs, const graphics::OverlayCamera& camera) const;
    // Loads `name` into a slot of m_sheets (empty when it fails).
    void loadPicture(const std::string& name);
};

} // namespace coney
