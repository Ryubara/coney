// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

// The tagging stick game: the player traces a spray-paint pattern with the left stick on a 256 × 256 grid, a charge
// of paint at a time, while the tag spot fills in. HuTag starts it for the story's tag spots and Rumble's Tag battle;
// the caller feeds it the stick and acts on how it ends (TagGame::Result).
// Research: docs/research/crimes.md#tagging

namespace coney {

/// One cell of the stick game's 256 × 256 grid.
struct TagCell {
    int x = 0;
    int y = 0;
    friend bool operator==(const TagCell&, const TagCell&) = default;
};

/// A tagging difficulty's tuning: how long a charge of paint lasts, how long the game pauses after a slip, and how
/// fast the cursor moves.
struct TagTuning {
    std::uint32_t chargeMs = 15000;
    std::uint32_t pauseMs = 1000;
    float cellsPerMs = 0.06F;
};

/// The tuning of difficulty 1-3 (the Warrior class's byte `+0x09`; 0 or out of range counts as 1), from the table at
/// `0x00510918`.
[[nodiscard]] TagTuning tagTuning(int difficulty);
/// `HuTagDifficulty`'s defaults (no script sets its own).
[[nodiscard]] TagTuning defaultTagTuning();

/// The path of a pattern: `count` points of `pattern` (flat x, y pairs, as `HuTagPattern` keeps them) sampled along a
/// uniform Catmull-Rom curve, ⌊300 / count⌋ samples a segment at t = k · count / 300, the end points clamped; each
/// sample's x and y truncated and their low byte kept (grid cells), and kept when its squared distance from the last
/// kept (the first from (0, 0)) is over 6. At most 300 points.
/// @orig 0x002741d8 TagGame_Init (unknown)
/// @orig 0x00273ff0 TagGame_CatmullRom (unknown)
[[nodiscard]] std::vector<TagCell> tagPath(std::span<const float> pattern, std::size_t count);

/// Whether a held object of anim set `animSet` leaves the hand as the spray starts: only sets 4
/// (`OVERHEAD_WEAPON_SET`) and 6 (`GHETTO_SET`); a club (set 3) stays in the hand.
/// @orig 0x00278018 Tag_StartSprayClips (unknown)
[[nodiscard]] constexpr bool tagDropsHeld(int animSet) { return animSet == 4 || animSet == 6; }

/// One tagging stick game (`0x002741d8`, update `0x002748a8`, `0x00274710`).
class TagGame {
  public:
    /// The most path points, and the most painted cells: a 301st ends the game unfinished.
    static constexpr std::size_t kMaxPoints = 300;
    /// The stick moves the cursor when its squared length is at least this (a length of 0.2).
    static constexpr float kDeadZoneSquared = 0.04F;
    /// An update's elapsed time for the cursor is at most this.
    static constexpr std::uint32_t kMaxStepMs = 30;
    /// The cursor's speed ramps up from 0 over this, from the first update and again from the end of each new
    /// charge's pause (a slip does not restart it).
    static constexpr std::uint32_t kRampMs = 2000;
    /// A painted cell is at least this far from every other.
    static constexpr int kPaintSpacing = 2;
    /// The cursor is near a path point when its cell's squared distance from it is below this (within 6 cells).
    static constexpr int kTrackDistanceSquared = 36;
    /// Tracking tries this many points behind the progress, and this many from the progress on.
    static constexpr int kTrackWindow = 4;
    /// An off-track count above this slips at the next moving update.
    static constexpr int kOffTrackUpdates = 4;
    /// Finished once the path's count is below the progress plus this: the progress at count − 3.
    static constexpr int kFinishWindow = 4;
    /// Unfinished with less than this share of the current charge left, one more charge is spent.
    static constexpr float kWasteShare = 0.3F;

    /// How an update left the game.
    enum class Result : std::uint8_t {
        Playing,    ///< Still going.
        Finished,   ///< The pattern is traced.
        Unfinished, ///< Out of paint, or 300 cells painted first.
    };

    /// What an update did that the caller acts on. A pause began when either is set.
    struct Events {
        bool slipped = false;     ///< Off track: the cursor snapped back, the pad rumbles, the game pauses (and a
                                  ///< crew member may say 80 `tagcheer`).
        bool chargeSpent = false; ///< A charge ran out and the next began (the cursor snapped back, a pause).
    };

    /// Spends one charge of paint (spray paint, inventory item 3, − 1); false when there is none.
    using SpendCharge = std::function<bool()>;

    /// A game on `path` starting `fraction` of the way along it (the tag spot's painted fraction), with `tuning`: the
    /// progress and the cursor at point ⌊fraction × count⌋, and the path's points before it already painted.
    TagGame(std::vector<TagCell> path, float fraction, TagTuning tuning);

    /// One update with the left stick at (`stickX`, `stickY`), each -1 to 1 (y up), `elapsedMs` of game clock since
    /// the last. The paint runs first, paused or not: once the charge time has passed a new charge starts (the
    /// cursor back on the progress, the off-track count cleared, a pause, the ramp from the pause's end) and `spend`
    /// is asked for a can. Unpaused, the cursor moves and paints, a count of five off-track moving updates slips, and
    /// tracking moves the progress forward.
    /// @orig 0x002748a8 TagGame_Update (unknown)
    /// @orig 0x00274710 TagGame_SpendPaint (unknown)
    Result update(float stickX, float stickY, std::uint32_t elapsedMs, const SpendCharge& spend);
    /// The game ends unfinished at once (a command outside the game's own, or no cans left).
    void abandon() { m_result = Result::Unfinished; }

    /// Whether, unfinished, the charge in use is spent too (less than 30 % of it left).
    [[nodiscard]] bool wastesCharge() const;

    [[nodiscard]] Result result() const { return m_result; }
    [[nodiscard]] const Events& events() const { return m_events; }
    [[nodiscard]] const std::vector<TagCell>& path() const { return m_path; }
    [[nodiscard]] const std::vector<TagCell>& painted() const { return m_painted; }
    [[nodiscard]] float cursorX() const { return m_cursorX; }
    [[nodiscard]] float cursorY() const { return m_cursorY; }
    /// The path point the player has reached: it only moves forward.
    [[nodiscard]] std::size_t progress() const { return m_progress; }
    /// The progress as a share of the path, 0-1, the tag spot's painted fraction: progress ÷ count, and 1.0 once
    /// finished.
    /// @orig 0x00274e38 Tag_GetProgress (unknown)
    [[nodiscard]] float fraction() const;
    /// Whether the cursor was near the path at the last tracking (`+0x500`); a pause keeps the last value.
    /// @orig 0x002748a0 TagGame_IsOnTrack (unknown)
    [[nodiscard]] bool onTrack() const { return m_onTrack; }
    /// Moving updates off track since the last slip or new charge (`+0x510`).
    [[nodiscard]] int offTrackCount() const { return m_offTrack; }
    /// Milliseconds left of the charge in use (it drains through pauses too), and of the pause.
    [[nodiscard]] std::uint32_t chargeLeftMs() const;
    [[nodiscard]] std::uint32_t pauseLeftMs() const;
    /// The share of the charge in use left, 0-1: the HUD's charge bar.
    /// @orig 0x00274638 Tag_GetChargeLeft (unknown)
    [[nodiscard]] float chargeLeft() const;
    /// The difficulty's tuning.
    [[nodiscard]] const TagTuning& tuning() const { return m_tuning; }

  private:
    // The cursor back on the path at the progress, the off-track count cleared and the game paused.
    void snapBack();
    // Paints the cell under the cursor when it is far enough from every painted one; false when it would be the
    // 301st.
    bool paint();
    // The paint's part of every update: the first stamps the charge and the ramp, and once the charge time has passed
    // a new charge starts.
    void spendPaint(const SpendCharge& spend);
    // Tracking: on track near one of the 4 points behind (the progress kept) or at the first near one of the progress
    // and the 3 after it (the progress moved there); else off track, counted when the stick moved.
    void track(bool moving);

    std::vector<TagCell> m_path;
    TagTuning m_tuning;
    std::vector<TagCell> m_painted;
    float m_cursorX = 0.0F;
    float m_cursorY = 0.0F;
    std::size_t m_progress = 0;
    std::uint64_t m_clockMs = 0;       // the game clock: the sum of the updates' elapsed times
    std::uint64_t m_lastPlayedMs = 0;  // the clock at the last unpaused update
    std::uint64_t m_chargeStartMs = 0; // +0x518
    std::uint64_t m_pauseUntilMs = 0;  // +0x514
    std::uint64_t m_rampStartMs = 0;   // +0x51c
    bool m_started = false;            // the first update has stamped the charge and the ramp
    bool m_onTrack = true;             // +0x500
    int m_offTrack = 0;                // +0x510
    Result m_result = Result::Playing;
    Events m_events;
};

} // namespace coney
