// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/language.h"
#include "graphics/render_device.h"
#include "graphics/sprite_batch.h"
#include "gui/base_widget.h"
#include "gui/global_strings.h"
#include "gui/menu_input.h"
#include "gui/text_widget.h"
#include "gui/widget.h"

namespace coney::gui {

/// The Options screen's strings (docs/research/pause.md#options).
namespace option_strings {
/// The seven page titles, in order.
inline constexpr std::array<std::uint32_t, 7> kTitles{0x117, 0x119, 0x122, 0x123, 0x124, 0x11f, 0x128};
inline constexpr std::uint32_t kInvertP1 = 0x11a;
inline constexpr std::uint32_t kInvertP2 = 0x11b;
inline constexpr std::uint32_t kAutoAdjustP1 = 0x11c;
inline constexpr std::uint32_t kAutoAdjustP2 = 0x11d;
inline constexpr std::uint32_t kMerge = 0x11e;
inline constexpr std::uint32_t kP1Controller = 0x120;
inline constexpr std::uint32_t kP2Controller = 0x121;
inline constexpr std::uint32_t kProLogic = 0x126;
inline constexpr std::uint32_t kMusic = 0x129;
inline constexpr std::uint32_t kSoundFx = 0x12a;
inline constexpr std::uint32_t kOn = 0x12b;
inline constexpr std::uint32_t kOff = 0x12c;
inline constexpr std::uint32_t kNormal = 0x12d;
inline constexpr std::uint32_t kWide = 0x12e;
inline constexpr std::uint32_t kYes = 0x3b;
inline constexpr std::uint32_t kNo = 0x3c;
inline constexpr std::uint32_t kLightingHint = 0x118;
} // namespace option_strings

/// The Options screen's pages, in the title list's order.
enum class OptionPage : std::uint8_t { Lighting, Camera, Audio, Video, Subtitles, Vibration, RestoreDefault };

/// How many pages there are.
inline constexpr std::size_t kOptionPages = 7;

/// The values the Options screen edits, as the profile keeps them (docs/research/save.md#options): the brightness
/// (`W_GameState + 0x57a4`, 0-100), the volumes (`+0x57ac` music, `+0x57a8` SoundFX, 0-1), per pad the camera's
/// inversion and auto-adjust and the vibration, the split-screen merge, the subtitles, Pro Logic II and the video
/// mode (Wide when set).
struct OptionValues {
    int brightness = 40;
    float musicVolume = 0.9F;
    float soundFxVolume = 0.9F;
    std::array<bool, 2> invertCamera{false, false};
    std::array<bool, 2> autoAdjustCamera{true, true};
    std::array<bool, 2> vibration{true, true};
    bool splitScreenMerge = true;
    bool subtitles = false;
    bool proLogic = false;
    bool wideVideo = false;

    /// What Restore Default sets (`OptionMenu_OnRestoreDefaults`): brightness 40, both volumes 0.9, invert off,
    /// auto-adjust on, merge on, vibration on, subtitles off, Pro Logic II off. The video mode is kept.
    /// @orig 0x001d80e0 OptionMenu_OnRestoreDefaults (OptionMenu.cpp)
    [[nodiscard]] static OptionValues defaults(bool wideVideo);

    bool operator==(const OptionValues&) const = default;
};

/// The Options screen's places for one video mode and language (`OptionMenu_Open` rewrites the globals
/// `0x0050ed30`-`0x0050edd0` on every open by the device flags; docs/research/pause.md#options). GUI units.
struct OptionLayout {
    float titleY = 0.30F;             ///< The first title and the pages' heading (`0x0050ed98`, `0x0050edc8`).
    float titleStep = 0.055F;         ///< `0x0050ed9c`.
    float lightingLabelY = 0.29F;     ///< `0x0050ed70`.
    float rowY = 0.36F;               ///< Camera and Vibration's first row (`0x0050ed84`, `0x0050ed88`).
    float rowStep = 0.045F;           ///< `0x0050eda0`.
    float answersOffset = 0.4F;       ///< The answers' x after their label's (`0x0050ed78`).
    float cameraAnswersExtra = 0.0F;  ///< Added for the Camera page in German (`0x0050edd0` = 0.1).
    float underAnswersOffset = 0.06F; ///< Subtitles, Video and Restore Default: answers under the label (`0x0050ed7c`).
    float restoreAnswersOffset = 0.06F; ///< Restore Default's (0.11 in German, `0x0050ed80`).
    std::array<float, 4> volumeRows{0.37F, 0.405F, 0.455F, 0.49F}; ///< Music label, bar, SoundFX label, bar.
    float squaresY = 0.38F;                                        ///< The brightness squares' y (`0x0050ed4c`).
    float squareScale = 1.0F;                                      ///< `0x0050ed44`.

    /// Each mode's and language's layout, as forMode() picks it (OptionMenu_Open's values; defined in option_menu.cpp).
    static const std::array<OptionLayout, 8> kModes;

    /// The layout for `language` in the default video mode, in 16:9 (`widescreen`, device flag `0x04`), with flag
    /// `0x02` (`flag02`) and with both, from kModes. Flag `0x08` is not modelled.
    [[nodiscard]] static OptionLayout forMode(Language language, bool widescreen, bool flag02) {
        return kModes.at((((static_cast<std::size_t>(flag02) << 1U) | static_cast<std::size_t>(widescreen)) << 1U) |
                         static_cast<std::size_t>(language == Language::German));
    }
};

/// What one Options item is.
enum class OptionItemKind : std::uint8_t {
    Choice,     ///< `YesNoBox`: two answers after (or under) the label.
    VolumeBar,  ///< `StatBarItem`: a 0-1 bar.
    Brightness, ///< `LightingItem`: the brightness bar, two squares and a hint.
};

/// Which value an item edits.
enum class OptionSetting : std::uint8_t {
    Brightness,
    InvertP1,
    InvertP2,
    AutoAdjustP1,
    AutoAdjustP2,
    Merge,
    Music,
    SoundFx,
    ProLogic,
    Video,
    Subtitles,
    VibrationP1,
    VibrationP2,
    RestoreDefault,
};

/// One item of an Options page.
struct OptionItem {
    OptionSetting setting = OptionSetting::Brightness;
    OptionItemKind kind = OptionItemKind::Choice;
    std::uint32_t label = 0;                ///< The label's string.
    std::array<std::uint32_t, 2> answers{}; ///< A choice's two answers.
    bool confirm = false;                   ///< Confirm mode (`+0x4c8`): accept applies, left and right only move.
    std::size_t choice = 0;                 ///< A choice's answer, 0 first.
    float bar = 0.0F;                       ///< A bar's value, 0-1.
    std::size_t savedChoice = 0;            ///< What the page had when opened (slot `+0xa0`).
    float savedBar = 0.0F;
    int savedBrightness = 0;
    TextWidget labelText;
    TextWidget answersText;
    float barX = 0.0F; ///< A bar's left edge and centre.
    float barY = 0.0F;
};

/// The pause menu's Options screen (`OptionMenu`, object `0x0062a1a0`): the list of seven page titles; accepting one
/// replaces the list with that page, whose items are edited in place.
///
/// - **Titles** at (0.26, 0.30 + 0.055 × i), `big_font` (slot 6) at 1.3, the selected one `0x005fd310`, the others
///   `0x005fd320`; up and down move (cue 4, `0xe` at an end), left and right are refused (cue `0xe`); accept opens
///   the page (cue 8) on the item it was left on and remembers its values (Restore Default on No again); back (cue
///   `0xf`) leaves the screen.
/// - **A page**: up and down move between its items (cue 4, `0xe` at an end), left and right go to the item; accept
///   (cue 8) closes the page keeping every value; back (cue `0xf`) closes it after putting back the values it had when
///   opened, except on Restore Default; on Vibration the preview rumble stops first. Camera, Audio and Vibration show
///   the page's name as a heading at (0.26, 0.30), size 1.3.
/// - **Choices** (On `0x12b` / Off `0x12c` and the others): the label (size 1.0, font slot 3, `0x005fd310` when
///   selected, else `0x005fd320`) with the answers 0.4 to its right ("A / B", font slot 4, the chosen one
///   `0x005fd310`); left picks the first answer and right the second (cue 4, `0xe` when already chosen) and the value
///   is set at once (again when already chosen), the first answer meaning on. Video and Restore Default are in confirm
///   mode: left and right only move, accept applies (Normal or Wide; Yes restores the defaults); Restore Default starts
///   on No.
/// - **Volume bars** (Music, SoundFX): 0.54 wide, a white fill over a (21, 21, 21) back, 0.025 high; left and right
///   step 0.05 within 0-1 (cue 4, `0xe` at an end) and set the volume each press, at an end too.
/// - **Brightness**: the bar at brightness / 100; left and right step the brightness by 5 within 0-100; two squares
///   0.09 × 0.065, the first in the brightness's grey, the second black; the hint `0x118` under the label.
///
/// Every change goes to the apply callback at once with all the values (what each setter writes, save.md#options).
///
/// Coney's choices where the page is silent (docs/research/pause.md#options): the bars' frame (sheet 12 rectangle 0)
/// is drawn the bar's size; the bars' back and fill and the squares use sheet 12 rectangle 6 tinted; the page heading
/// and the single-item pages' label sit at the heading's place; Restore Default's Yes opens the menu again at once (the
/// original does it on its next update); the Audio page's music loop and the vibration preview go to hooks the caller
/// may leave empty; Video's Wide only sets the value (Coney has no video mode to switch, so the menus are not
/// reopened); the page's discard box
/// (`OptionPage` `+0x210`), which no code opens, is left out.
///
/// Research: docs/research/pause.md#options, docs/research/pause.md#fn-optionmenu
class OptionMenu {
  public:
    /// The titles' x and size, and their font slot.
    static constexpr float kX = 0.26F;
    static constexpr float kTitleSize = 1.3F;
    static constexpr int kTitleFontSlot = 6;
    /// The items' labels: size, font slot; the answers' font slot.
    static constexpr float kLabelSize = 1.0F;
    static constexpr int kLabelFontSlot = 3;
    static constexpr int kAnswersFontSlot = 4;
    /// Pro Logic II's row (`0x0050edcc`).
    static constexpr float kProLogicY = 0.60F;
    /// The bars' width (GUI), height (overlay), the step, and their back's colour.
    static constexpr float kBarWidth = 0.54F;
    static constexpr float kBarHeight = 0.025F;
    static constexpr float kBarStep = 0.05F;
    static constexpr graphics::Rgba kBarBack{21, 21, 21, 255};
    /// The brightness's step and range.
    static constexpr int kBrightnessStep = 5;
    static constexpr int kBrightnessMax = 100;
    /// The squares' places, size (overlay) and rectangle.
    static constexpr std::array<float, 2> kSquareX{0.68F, 0.74F};
    static constexpr float kSquareWidth = 0.09F;
    static constexpr float kSquareHeight = 0.065F;
    static constexpr std::size_t kSquareRect = 6;
    static constexpr std::size_t kFrameRect = 0;
    /// The vibration preview: strength and length (`0x0050ed50`, `0x0050ed54`).
    static constexpr int kVibrationStrength = 120;
    static constexpr std::uint64_t kVibrationMs = 500;
    /// Cues.
    static constexpr int kMoveCue = 4;
    static constexpr int kAcceptCue = 8;
    static constexpr int kRefusedCue = 0xe;
    static constexpr int kBackCue = 0xf;

    /// What the screen asks of the game; each may be empty.
    struct Hooks {
        /// Plays a front-end cue.
        std::function<void(int cue)> playCue;
        /// Every change, with all the values.
        std::function<void(const OptionValues& values)> apply;
        /// The Audio page's music loop (`vags/music/wonderwheel_mono`): on with the music volume while the page is
        /// open (called again when the volume changes), off when it closes.
        std::function<void(bool on, float volume)> previewMusic;
        /// A pad's vibration turned on: rumble `pad` at `strength` for `ms`.
        std::function<void(int pad, int strength, std::uint64_t ms)> vibrate;
        /// The vibration preview stopped early (back on the Vibration page).
        std::function<void()> stopVibration;
    };

    /// The screen, closed, with the bars' and squares' widgets shown (drawn only while a page with a bar is open).
    /// @orig 0x001d84a0 OptionMenu_Construct (OptionMenu.cpp)
    OptionMenu();

    /// Opens the screen on its title list, the first selected, every page on its first item, with `values` and
    /// `layout`; `level` is the level's number (level 102 has no Split Screen Merge). `strings` and `input` must
    /// outlive the menu's use. Restore Default opens it again with the new values.
    /// @orig 0x001d86a8 OptionMenu_Open (OptionMenu.cpp)
    void open(const GlobalStrings* strings, MenuInput& input, Hooks hooks, const OptionValues& values,
              const OptionLayout& layout, int level, std::uint64_t nowMs);
    /// Closes the screen (the music loop stops).
    /// @orig 0x001da348 OptionMenu_Close (OptionMenu.cpp)
    void close();

    /// Acts on one command. Returns true when back leaves the screen (from the title list).
    /// @orig 0x001da3f8 OptionMenu_OnCommand (OptionMenu.cpp)
    [[nodiscard]] bool handle(MenuCommand command);

    /// Advances the texts.
    /// @orig 0x001da658 OptionMenu_Update (OptionMenu.cpp)
    void update(const GuiFrame& frame);
    /// Adds the titles, or the open page.
    /// @orig 0x001da880 OptionMenu_Render (OptionMenu.cpp)
    /// @orig 0x001d62d8 OptionPage_Render (OptionMenu.cpp)
    void render(const GuiCanvas& canvas) const;
    /// The menu's alpha, 0 to 1.
    /// @coney-only the pause menu's fade handed to Coney's widgets; the original passes the alpha to each render
    void setFade(float fade);
    /// Draws the bars and squares into `batch` (sheet 12's); null draws none.
    /// @coney-only Coney's sprite batches; the original's widgets hold their own sprites
    void setSpriteBatch(graphics::SpriteBatch* batch);

    /// The values now.
    [[nodiscard]] const OptionValues& values() const { return m_values; }
    /// The selected title.
    [[nodiscard]] std::size_t selectedTitle() const { return m_title; }
    /// Whether a page is open.
    [[nodiscard]] bool pageOpen() const { return m_pageOpen; }
    /// The selected item of the selected title's page (each page keeps its own while the screen is open).
    [[nodiscard]] std::size_t selectedItem() const { return m_selected.at(m_title); }
    /// Page `page`'s items.
    [[nodiscard]] const std::vector<std::unique_ptr<OptionItem>>& items(OptionPage page) const {
        return m_pages.at(static_cast<std::size_t>(page));
    }
    /// Title `index`'s widget.
    [[nodiscard]] const TextWidget& title(std::size_t index) const { return m_titles.at(index); }
    /// The page heading's widget.
    [[nodiscard]] const TextWidget& heading() const { return m_heading; }
    /// The brightness hint's widget.
    [[nodiscard]] const TextWidget& hint() const { return m_hint; }
    /// The texts shown now, in drawing order (for tests and the log).
    /// @coney-only a test and log aid
    [[nodiscard]] std::vector<std::string> lines() const;
    /// The layout the screen was opened with.
    [[nodiscard]] const OptionLayout& layout() const { return m_layout; }

  private:
    // String `id`, or empty without strings.
    // @coney-only Coney's string table lookup (the original calls GlobalString_Get)
    [[nodiscard]] std::string_view string(std::uint32_t id) const;
    // Plays `cue`, if there is a sink.
    // @coney-only Coney's cue hook (the original calls the sound manager)
    void playCue(int cue) const;
    // The titles' and items' colours by the selection: the selected one `0x005fd310`, the others `0x005fd320`.
    // @coney-only Coney sets them when the selection changes; the original picks them as it draws (OptionMenu_Render,
    // OptionPage_Render), whose compared pairs point here
    void colour();
    // Rebuilds a choice's answers text: the chosen answer `0x005fd310`, the other `0x005fd320`, a "/" between.
    // @orig 0x001d6b00 YesNoBox_RenderColoured (OptionMenu.cpp)
    void refreshAnswers(OptionItem& item) const;
    // The open page's items.
    [[nodiscard]] std::vector<std::unique_ptr<OptionItem>>& page() { return m_pages.at(m_title); }
    [[nodiscard]] const std::vector<std::unique_ptr<OptionItem>>& page() const { return m_pages.at(m_title); }
    // One command to the open page; true when it closes.
    // @orig 0x001d5dc8 OptionPage_OnCommand (OptionMenu.cpp)
    bool pageCommand(MenuCommand command);
    // Left or right on `item`: an item at its end refuses (cue 0xe) but still sets its value and calls its setter.
    // @orig 0x001d7490 StatBarItem_OnCommand (OptionMenu.cpp)
    // @orig 0x001d7700 LightingItem_OnCommand (OptionMenu.cpp)
    // @orig 0x001d8440 OptionMenu_AddBrightness (OptionMenu.cpp)
    void itemCommand(OptionItem& item, bool right);
    // Applies a choice's answer (a setter; `confirmed` for confirm mode's accept).
    // @orig 0x001d7f90 OptionMenu_OnInvertChoice (OptionMenu.cpp)
    // @orig 0x001d7ff0 OptionMenu_OnAutoAdjustChoice (OptionMenu.cpp)
    // @orig 0x001d8050 OptionMenu_OnMergeChoice (OptionMenu.cpp)
    // @orig 0x001d80a8 OptionMenu_OnSubtitlesChoice (OptionMenu.cpp)
    // @orig 0x001d8208 OptionMenu_OnProLogicChoice (OptionMenu.cpp)
    // @orig 0x001d7ef0 OptionMenu_OnVibrationChoice (OptionMenu.cpp)
    // @orig 0x001d8258 OptionMenu_OnVideoChoice (OptionMenu.cpp)
    void applyChoice(OptionItem& item, bool confirmed);
    // Applies a bar's value.
    // @orig 0x001d83e0 OptionMenu_SetSoundFxVolume (OptionMenu.cpp)
    // @orig 0x001d8410 OptionMenu_SetMusicVolume (OptionMenu.cpp)
    void applyBar(const OptionItem& item);
    // Remembers the open page's values (slot `+0xa0` of each item).
    // @orig 0x001d60e8 OptionPage_BeginEdit (OptionMenu.cpp)
    void beginEdit();
    // Puts the open page's values back and applies them (slot `+0xa8`; OptionMenu_RevertPage).
    // @orig 0x001d6160 OptionPage_Revert (OptionMenu.cpp)
    void revert();
    // Sends the values to the apply hook (and the music loop its volume).
    // @coney-only Coney hands every change to its caller; the original's setters write the game state themselves
    void changed();

    const GlobalStrings* m_strings = nullptr;
    // The input and time open() was given, for Restore Default's reopening.
    MenuInput* m_input = nullptr;
    std::uint64_t m_openedMs = 0;
    Hooks m_hooks;
    OptionValues m_values;
    OptionLayout m_layout;
    int m_level = 0;
    std::array<TextWidget, kOptionPages> m_titles;
    // Each page's items; pointers, a widget cannot move.
    std::array<std::vector<std::unique_ptr<OptionItem>>, kOptionPages> m_pages;
    TextWidget m_heading;
    TextWidget m_hint;
    // One frame, back and fill drawn for each bar in turn: placed in render().
    mutable BaseWidget m_barFrame{nullptr, kFrameRect};
    mutable BaseWidget m_barBack{nullptr, kSquareRect};
    mutable BaseWidget m_barFill{nullptr, kSquareRect};
    mutable std::array<BaseWidget, 2> m_squares{BaseWidget{nullptr, kSquareRect}, BaseWidget{nullptr, kSquareRect}};
    std::size_t m_title = 0;
    // Each page's selected item (OptionPage +4): set to 0 by open() only, so a page reopens where it was left.
    std::array<std::size_t, kOptionPages> m_selected{};
    bool m_pageOpen = false;
    bool m_open = false;
    float m_fade = 1.0F;
};

} // namespace coney::gui
