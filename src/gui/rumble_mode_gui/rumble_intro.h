// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/game_random.h"
#include "gui/global_strings.h"
#include "gui/menu_input.h"
#include "gui/text_widget.h"
#include "gui/widget.h"

namespace coney::gui {

/// What the intro asks of the sound: announcer lines whose end it waits for, and plain sounds. Either may be empty.
struct RumbleIntroSounds {
    /// Plays the announcer line `name` and returns how long it lasts in milliseconds, or nothing when the line cannot
    /// play (the gang line then falls back to a general one). Empty: every line plays for kStandInVoiceMs.
    std::function<std::optional<std::uint64_t>(std::string_view name)> playVoice;
    /// Plays the sound `name` (the synth stings).
    std::function<void(std::string_view name)> playSound;
};

/// Where the intro is (`+0xe4`).
enum class RumbleIntroPhase : std::uint8_t {
    Closed,    ///< Not showing.
    Names,     ///< 1: the names one at a time with the announcer.
    Prompt,    ///< 2: the prompt, waiting for accept.
    Ready,     ///< 3: the ready line, then the countdown.
    Countdown, ///< 4: 3, 2, 1, the last word.
};

/// The Rumble match's intro and countdown: the HUD's **RM_Intro** screen (`0x600840 + 0xe530`) that
/// `ShowRumbleModeIntro(onDone, names)` opens.
///
/// 1. **Names**, one at a time: the announcer names the gang (`dj_gang_<pack + 1>`, the pack from set-up value 3 for
///    the first name, 4 for the second; when that line cannot play, a random `dj_genintro_01`-`30`, a different one for
///    the second name) with a synth sting (`rumblesynth_01` / `_02`); the name fades up over kNameFadeMs. When the
///    voice ends, the separator shows with a random `dj_vs_01`-`15`; when that ends, the next name.
/// 2. **Prompt** (global string 0x25) fades in over kPromptFadeMs; once nearly opaque the screen takes the pad and
///    hides the names. Accept plays a random `dj_ready_01`-`05`.
/// 3. When that line ends, the countdown starts.
/// 4. **Countdown**: "3", "2", "1" and global string 0x3f, a second each, each fading from opaque to clear; the "3"
///    picks a random `dj_start_01`-`10`, which plays with the last word. kCountdownMs after its start the screen
///    calls `onDone` and closes.
///
/// Coney's stand-ins (docs/research/rumble.md#open-questions): the layout (the names centred on big_font at y 0.35 and
/// 0.55, the separator "VS" at 0.45, the prompt at 0.7, the countdown at 0.45, all white) and the separator's text are
/// not on the page; the gang line is named with the plain decimal pack number; "nearly opaque" is kPromptTakeLevel;
/// the screen effects 0 the intro queues and the countdown's first sound are left out; with no voice lengths a line
/// lasts kStandInVoiceMs.
///
/// Research: docs/research/rumble.md#intro
/// @orig 0x001f9418 RM_Intro_Open (unknown)
/// @orig 0x001f9558 RM_Intro_Build (unknown)
class RumbleIntro : public Widget {
  public:
    /// The most names kept.
    static constexpr std::size_t kMaxNames = 10;
    /// How long a name fades up (`0x0050f47c`) and the prompt fades in (`0x0050f484`).
    static constexpr std::uint64_t kNameFadeMs = 500;
    static constexpr std::uint64_t kPromptFadeMs = 1000;
    /// The countdown: a word a second, and onDone after this long.
    static constexpr std::uint64_t kCountdownStepMs = 1000;
    static constexpr std::uint64_t kCountdownMs = 4000;
    /// Coney's stand-ins: how opaque the prompt is when it takes the pad, and how long a voice line lasts without
    /// lengths.
    static constexpr float kPromptTakeLevel = 0.95F;
    static constexpr std::uint64_t kStandInVoiceMs = 1500;
    /// The strings: the prompt and the countdown's last word.
    static constexpr std::uint32_t kPromptString = 0x25;
    static constexpr std::uint32_t kGoString = 0x3f;
    /// Coney's stand-in layout.
    static constexpr float kFirstNameY = 0.35F;
    static constexpr float kSeparatorY = 0.45F;
    static constexpr float kSecondNameY = 0.55F;
    static constexpr float kPromptY = 0.7F;
    static constexpr float kCountdownY = 0.45F;
    static constexpr std::string_view kSeparatorText = "VS";

    /// Shows strings from `strings` (null: empty), plays through `sounds`, draws its random choices from `random`
    /// (null: the first of each list), and calls `onDone(name)` when the countdown ends; each must outlive the intro.
    void setStrings(const GlobalStrings* strings) { m_strings = strings; }
    void setSounds(RumbleIntroSounds sounds) { m_sounds = std::move(sounds); }
    void setRandom(GameRandom* random) { m_random = random; }
    void setDoneSink(std::function<void(std::string_view onDone)> done) { m_done = std::move(done); }

    /// `ShowRumbleModeIntro(onDone, names)`: keeps `onDone` and the first kMaxNames non-empty names, with `packs`
    /// (set-up values 3 and 4) naming the gang lines, and starts the first name at `nowMs`. With no names it goes
    /// straight to the prompt.
    /// @orig 0x001b5f88 ShowRumbleModeIntro (unknown)
    void open(std::string_view onDone, std::span<const std::string> names, std::array<int, 2> packs,
              std::uint64_t nowMs);

    /// Closes the intro without calling `onDone`.
    void close() {
        m_phase = RumbleIntroPhase::Closed;
        m_holdsPad = false;
    }

    /// One frame of the phase; the prompt reads `frame`'s pad.
    /// @orig 0x001fad40 RM_Intro_Update (unknown)
    void update(const GuiFrame& frame) override;
    /// Draws the phase's texts.
    void render(const GuiCanvas& canvas) const override;

    /// The phase.
    [[nodiscard]] RumbleIntroPhase phase() const { return m_phase; }
    /// Whether it is showing.
    [[nodiscard]] bool isOpen() const { return m_phase != RumbleIntroPhase::Closed; }
    /// Whether the screen holds the pad (from the prompt's take to the close).
    [[nodiscard]] bool holdsPad() const { return m_holdsPad; }
    /// The names kept, and how many have been shown.
    [[nodiscard]] const std::vector<std::string>& names() const { return m_names; }
    [[nodiscard]] std::size_t shown() const { return m_shown; }
    /// The announcer lines played so far, oldest first (for the log and the tests).
    [[nodiscard]] const std::vector<std::string>& voices() const { return m_voices; }
    /// The countdown's word now ("3", "2", "1" or the last word); empty outside the countdown.
    [[nodiscard]] std::string countdownWord(std::uint64_t nowMs) const;

  private:
    // A whole number in [low, high] from the game's random numbers (low without them).
    [[nodiscard]] int draw(int low, int high);
    // Plays the announcer line `name` and notes when it ends; false when it cannot play.
    bool speak(std::string_view name, std::uint64_t nowMs);
    // Starts name `index` at `nowMs`: its gang line or a general one, and its sting.
    void startName(std::size_t index, std::uint64_t nowMs);
    // Moves to the prompt at `nowMs`.
    void startPrompt(std::uint64_t nowMs);
    // String `id`, or empty without strings.
    [[nodiscard]] std::string_view string(std::uint32_t id) const;
    // Sets `text` up as one of the intro's centred white big_font lines at `y`, showing `value`.
    static void setupLine(TextWidget& text, float y, std::string_view value);

    const GlobalStrings* m_strings = nullptr;
    RumbleIntroSounds m_sounds;
    GameRandom* m_random = nullptr;
    std::function<void(std::string_view)> m_done;
    MenuInput m_input;
    std::string m_onDone;
    std::vector<std::string> m_names;
    std::array<int, 2> m_packs{};
    std::vector<std::string> m_voices;
    RumbleIntroPhase m_phase = RumbleIntroPhase::Closed;
    std::size_t m_shown = 0;         // names started so far
    bool m_separator = false;        // the separator's line is playing (after name m_shown)
    int m_firstGeneral = 0;          // the general line the first name took (0: none)
    std::uint64_t m_phaseMs = 0;     // when the phase (or the current name) started
    std::uint64_t m_voiceEndsMs = 0; // when the current announcer line ends
    std::uint64_t m_nameStartMs = 0; // when the newest name started
    std::uint64_t m_lastMs = 0;      // the last update's time
    std::string m_startLine;         // the dj_start line the "3" picked
    bool m_startPlayed = false;
    bool m_holdsPad = false;
    bool m_namesHidden = false;
    std::vector<std::unique_ptr<TextWidget>> m_nameTexts;      // one per name
    std::vector<std::unique_ptr<TextWidget>> m_separatorTexts; // one between each two names
    TextWidget m_prompt;
    TextWidget m_countdown;
};

} // namespace coney::gui
