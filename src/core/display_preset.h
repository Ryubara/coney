// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The display presets (docs/engine/renderer.md#presets): *PS2*, *Xbox*, *Enhanced* and *High*, each a named set of the
// render-side display values, chosen as `Display/Preset` in the debug menus' settings store
// (docs/guides/enhancements.md#where-the-settings-live), and *Custom* once a value differs from every preset's. The
// values are render-side only: no simulation code reads them, so a change needs no step-boundary queue and a run plays
// the same in every preset. Free of SDL and librw, so it can be tested; src/debug/game_tunables.cpp registers the
// values and src/platform/main.cpp settles them between frames.

#include <cstdint>
#include <optional>
#include <string_view>

namespace coney {

/// A display preset, in the order of `Display/Preset`'s values (0 to 4).
enum class DisplayPreset : std::uint8_t {
    Ps2,      ///< today's picture, the PS2's: every option off. What tests draw, and the player's default.
    Xbox,     ///< what the Xbox port draws differently: its lights, shadow map, fog and gamma ramp.
    Enhanced, ///< *Xbox* widened: per-pixel lights, more shadow receivers, environment maps, SMAA.
    High,     ///< *Enhanced* with the caps lifted: clustered lights, cascades, AO, MSAA or TAA.
    Custom,   ///< the player's own set: a value differs from every preset's. Not a preset to choose.
};

/// The number of presets a player can choose (*Custom* is not one).
inline constexpr int kChoosablePresets = 4;

/// The preset `name` names, as `--preset` takes it: `ps2`, `xbox`, `enhanced` or `high`; nothing for anything else
/// (`custom` too: it is not a set of values).
[[nodiscard]] std::optional<DisplayPreset> parseDisplayPresetName(std::string_view name);

/// The lower-case name of `preset`: `ps2`, `xbox`, `enhanced`, `high` or `custom`.
[[nodiscard]] std::string_view displayPresetName(DisplayPreset preset);

/// The preset the `Display/Preset` setting's value names: 0 to 3 a preset, anything else *Custom*.
[[nodiscard]] DisplayPreset presetOfSetting(int value);

/// The render-side display values a preset sets, one per row of the Presets table that differs between presets
/// (docs/engine/renderer.md#presets); each is a `Display/<name>` setting. A level counts up the table's columns: 0 is
/// *PS2*'s, 1 *Xbox*'s, 2 *Enhanced*'s, 3 *High*'s. Only `renderer` is drawn by anything yet; each other value waits
/// for the item of docs/engine/renderer.md#order that draws it, and its setting's help says which.
struct DisplayValues {
    int renderer = 0; ///< `Display/Renderer`: 0 GL3, 1 the SDL3 GPU renderer, read at the next start
    int lights = 0;   ///< `Display/Lights`: 0 per vertex at the PS2's caps, 1 the Xbox's caps, 2 per pixel, 3 clustered
    int shadows = 0;  ///< `Display/Shadows`: 0 no shadow map, 1 the Xbox's, 2 a fitted PCF map, 3 two cascades
    bool blobShadow = true;   ///< `Display/Blob shadow`: the PS2's blob under each human
    bool perPixelFog = false; ///< `Display/Per-pixel fog`: the Xbox's linear fog per pixel, in place of the PS2's
    bool gammaRamp = false;   ///< `Display/Gamma ramp`: the level's Xbox gamma ramp and its brightness default
    int reflections = 0;      ///< `Display/Reflections`: 0 none, 1 environment maps, 2 with GTAO and probes
    int antiAliasing = 0;     ///< `Display/Anti-aliasing`: 0 none, 1 SMAA, 2 MSAA or TAA with SMAA

    friend bool operator==(const DisplayValues&, const DisplayValues&) = default;
};

/// The values `preset` sets (docs/engine/renderer.md#presets). *Custom* sets none: asking for its values is a
/// programmer error (CONEY_ASSERT).
[[nodiscard]] DisplayValues presetValues(DisplayPreset preset);

/// The preset whose values `values` are, or *Custom* when they are no preset's.
[[nodiscard]] DisplayPreset presetMatching(const DisplayValues& values);

/// The live display settings: `Display/Preset` and the values, which the settings store edits through the variables
/// presetSetting() and values() return, and what drawing reads (drawn()).
///
/// **Choosing and Custom.** The store writes its queued changes between two frames; settle() then reconciles them. A
/// new preset writes its values, except those changed in the same batch, which stay as set (an overrides file that
/// saves *Xbox* and one value of its own reads back as that); and the preset then shows the one the values match, so
/// changing a value after choosing a preset shows *Custom*, and changing it back shows the preset again.
///
/// **Test mode.** pin() fixes what drawn() returns to one preset's values whatever the settings say, so a test run
/// draws the same whatever a player saved: *PS2*, or the preset `--preset` asks for (pinnedDisplayPreset() in
/// core/options.h).
/// @coney-only Coney's display options; the original has one picture
class DisplaySettings {
  public:
    /// Settings over `renderer`, the live `Display/Renderer` variable (rendererSetting() for the game's; a test's own
    /// int otherwise), which is read at the start and so lives apart. Everything starts at *PS2*.
    explicit DisplaySettings(int& renderer);

    /// The `Display/Preset` variable the store edits (presetOfSetting() reads it).
    [[nodiscard]] int& presetSetting() { return m_preset; }
    /// The value variables the store edits; `renderer` here is unused (the store edits the one given at construction).
    [[nodiscard]] DisplayValues& values() { return m_values; }

    /// The preset the settings show now: one of the four, or *Custom*.
    [[nodiscard]] DisplayPreset preset() const;
    /// The values the settings hold now, the renderer's included.
    [[nodiscard]] DisplayValues current() const;

    /// Reconciles the preset and the values after the store wrote its changes, as the class comment says. Call it
    /// between frames, after TunableRegistry::applyPending().
    void settle();
    /// Chooses `preset` now: its values exactly, whatever was set before. Choosing *Custom* changes nothing.
    void choose(DisplayPreset preset);
    /// Chooses `preset` at the next settle(), after the store's waiting changes, so it wins over an overrides file
    /// loaded at the start (`--preset` outside test mode).
    void chooseAtNextSettle(DisplayPreset preset) { m_chooseNext = preset; }

    /// Fixes drawn() to `preset`'s values (test mode), or frees it (nothing). *Custom* is not a preset to pin
    /// (CONEY_ASSERT).
    void pin(std::optional<DisplayPreset> preset);
    /// The preset drawn() is fixed to, if any.
    [[nodiscard]] std::optional<DisplayPreset> pinned() const { return m_pinned; }
    /// The values drawing uses: the pinned preset's, else current().
    [[nodiscard]] DisplayValues drawn() const;

  private:
    // Writes `values` into the live variables, the renderer's included.
    void store(const DisplayValues& values);

    int& m_renderer;
    int m_preset = 0;                          // Display/Preset, as the store edits it
    DisplayValues m_values;                    // the other Display values, as the store edits them
    int m_settledPreset = 0;                   // Display/Preset after the last settle()
    DisplayValues m_settled;                   // the values after the last settle()
    std::optional<DisplayPreset> m_chooseNext; // chooseAtNextSettle()
    std::optional<DisplayPreset> m_pinned;     // pin()
};

/// The game's display settings, over rendererSetting(): what the Tunables page edits and drawing reads.
[[nodiscard]] DisplaySettings& displaySettings();

} // namespace coney
