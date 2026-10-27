// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "characters/character_class.h"
#include "core/language.h"
#include "hud/hud_canvas.h"
#include "hud/hud_layout.h"

// The Armies of the Night parts of the HUD (levels 60-69, `GameState_IsArmiesLevel`): the credits line, the GO sign and
// the players' join messages, which the HUD sets up and steps only in those levels. Of the two `ANHud` player panels
// only whether each is wanted is kept, which decides its join message; their parts (health and power bars, portrait,
// boss picture) are not built (#662): Coney's story panel stands in for them.
// Research: docs/research/armies.md#credits, docs/research/armies.md#go-sign,
// docs/research/armies.md#the-join-messages, docs/research/hud-functions-parts.md#fn-pre-basewidget,
// docs/research/hud-functions-panels.md#fn-anhud

namespace coney::hud {

/// Whether a level number is an Armies of the Night level (60 to 69): the one test, characters::isArmiesLevel(),
/// which the HUD shares.
using characters::isArmiesLevel;

/// The GO sign's modes (`HUDANSetGOSignMode`, byte `0x0060f260`): any other value hides it as Off does.
enum class GoSignMode : std::uint8_t {
    Text = 0,   ///< "GO" and one arrow blinking.
    Arrows = 1, ///< "GO" and three arrows lit one at a time.
    Off = 2,    ///< Hidden (the set-up's mode).
};

/// The Armies of the Night credits line, GO sign and join messages.
class ArmiesHud {
  public:
    /// The global strings they show: the credits label, "GO" and the join message.
    static constexpr std::uint32_t kCreditsString = 0xd5;
    static constexpr std::uint32_t kGoString = 0xd3;
    static constexpr std::uint32_t kJoinString = 0xd4;
    /// The GO sign's frame advances every this many ms (`0x0050cfa8`); a join message blinks every kJoinBlinkMs
    /// (`0x0050f9f0`).
    static constexpr std::uint64_t kGoBlinkMs = 300;
    static constexpr std::uint64_t kJoinBlinkMs = 500;
    /// The credits line's place in the default video mode: "<label> <two digits>" (`HUD_Update`, `0x0050d4bc`,
    /// `0x0050d4c0`: 0.75 then 0.71 when the display flags 2, 4 and 8 are clear; the format `0x00554650`).
    static constexpr GuiPoint kCreditsPlace{0.71F, 0.92F};
    /// How far left the GO sign moves in Spanish and in Italian (`0x0050cf98`), GUI units; 0 in the other languages.
    static constexpr float kSpanishGoShift = 0.4F;
    static constexpr float kItalianGoShift = 0.02F;

    /// The level's set-up (`HUD_InitLevel`), before the level's script, in the game's `language`: the credits 0 (the
    /// HUD's word `+0x190d0`, `0x00619910`, which the Armies stages carry from one stage to the next in their Lua save
    /// data), the GO sign Off and placed for the language, both join messages on (the original allocates its `ANHud`
    /// panels afresh and its slot `+0x80` sets each one's flag; a panel's first attach clears it again).
    /// @orig 0x0019e030 GoSign_Setup (unknown)
    /// @orig 0x004ec650 ANHud_SetPlayer (unknown)
    void levelSetUp(Language language = Language::English);
    /// How far left of its English place the GO sign sits in the set-up's language, in GUI units (`0x0050cf98`).
    [[nodiscard]] float goSignShift() const { return m_goShift; }
    /// Whether the level in play is an Armies level (`HUD +0x225d5`), as the last update() was told: the stopwatch
    /// shows its seconds form then.
    [[nodiscard]] bool active() const { return m_active; }

    /// `HUDANSetCredit` / `HUDANGetCredit`: the credits left (the HUD's `+0x190d0`, `0x00619910`).
    /// @orig 0x001b46e0 HUD_ANSetCredits (unknown)
    void setCredits(std::uint32_t credits) { m_credits = credits; }
    /// @orig 0x001b4700 HUD_ANGetCredits (unknown)
    [[nodiscard]] std::uint32_t credits() const { return m_credits; }
    /// `HUDANSetGOSignMode(mode)`: the byte the sign's update reads.
    /// @orig 0x001b4690 HUD_ANSetGOSignMode (unknown)
    void setGoSignMode(std::uint8_t mode) { m_goMode = mode; }
    /// The GO sign's mode byte.
    [[nodiscard]] std::uint8_t goSignMode() const { return m_goMode; }
    /// The GO sign's frame (`+0x3d1`): which arrow is lit.
    [[nodiscard]] std::uint8_t goSignFrame() const { return m_goFrame; }
    /// `HUDANEnableJoinMsg(player, on)`: panel `player`'s join message on or off (`+0x4614`).
    /// @orig 0x001b46a0 HUD_ANEnableJoinMsg (unknown)
    void setJoinMessage(std::size_t player, bool on);
    /// Panel `player`'s first attach to a player (`HUD_AttachPlayer` -> `ANHud_Init`): its join message off.
    /// @orig 0x002107e8 ANHud_Init (unknown)
    void panelAttached(std::size_t player);
    /// Panel `player` enabled (interface slot `+0x20`, `ANHud_Enable`): wanted again, as a continue makes it.
    /// @orig 0x0020fd40 ANHud_Enable (unknown)
    void panelEnabled(std::size_t player);
    /// Whether panel `player`'s join message is on.
    [[nodiscard]] bool joinMessage(std::size_t player) const { return m_join.at(player); }
    /// Whether panel `player`'s join message shows now: on, the panel not wanted (its player not in, or knocked out
    /// since the panel was last enabled), and on the blink's lit half.
    /// @orig 0x00211938 ANHud_Draw (unknown)
    [[nodiscard]] bool joinShowing(std::size_t player) const;
    /// Whether panel `player` is wanted (`+0x40f8`): its player in and not knocked out since the panel was enabled.
    /// @coney-only a query for the join message and the tests; the original reads `+0x40f8` in place
    [[nodiscard]] bool panelWanted(std::size_t player) const;

    /// One HUD step at game time `nowMs` in level number `level`, with which players are in the game (their panel
    /// attached) and which are knocked out (`Human_IsKnockedOut`); nothing outside an Armies level. The GO sign's blink
    /// and the join messages' blink, which share one timer. **Coney's choice**: both are timed by the game time, where
    /// the original reads its real-time clock, so a test run blinks the same every time. A knocked-out player's panel
    /// stops being wanted from the next step, as `ANHud_Draw` drops it on the frame it draws.
    /// @orig 0x0019e660 GoSign_StepBlink (unknown)
    /// @orig 0x0019e758 GoSign_Update (unknown)
    void update(std::uint64_t nowMs, int level, std::array<bool, kPlayers> playersIn,
                std::array<bool, kPlayers> knockedOut = {});

    /// Adds the credits line, the GO sign and the join messages, with the global strings `strings` gives.
    /// @orig 0x0019ed28 GoSign_Render (unknown)
    void render(const HudCanvas& canvas, const std::function<std::string(std::uint32_t)>& strings) const;

  private:
    bool m_active = false;
    std::uint32_t m_credits = 0;
    std::uint8_t m_goMode = static_cast<std::uint8_t>(GoSignMode::Off);
    std::uint8_t m_goFrame = 0;
    float m_goShift = 0.0F; // `0x0050cf98`
    bool m_goTimed = false; // the blink's clock read once (`0x0060003c`)
    std::uint64_t m_goLastMs = 0;
    std::array<bool, kPlayers> m_join{};
    std::array<bool, kPlayers> m_joinLit{};
    std::array<bool, kPlayers> m_in{};
    // A panel dropped by its player's knock-out (`+0x40f8` cleared), and one that drops at the next step.
    std::array<bool, kPlayers> m_dropped{};
    std::array<bool, kPlayers> m_dropping{};
    bool m_joinTimed = false; // `0x0063f1f4`
    std::uint64_t m_joinLastMs = 0;
};

} // namespace coney::hud
