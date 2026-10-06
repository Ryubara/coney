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

/// One tagging stick game (`0x002741d8`, update `0x002748a8`, `0x00274710`).
class TagGame {
  public:
    /// The most path points, and the most painted cells before the game ends unfinished.
    static constexpr std::size_t kMaxPoints = 300;
    /// The stick moves the cursor past this.
    static constexpr float kDeadZone = 0.2F;
    /// An update's elapsed time is at most this.
    static constexpr std::uint32_t kMaxStepMs = 30;
    /// The cursor's speed ramps up from 0 over this.
    static constexpr std::uint32_t kRampMs = 2000;
    /// A painted cell is at least this far from every other.
    static constexpr int kPaintSpacing = 2;
    /// The cursor is on track within this many cells of a path point ...
    static constexpr int kTrackCells = 6;
    /// ... no more than this many points from the progress.
    static constexpr int kTrackWindow = 4;
    /// Off track for more than this many updates while the stick moves, the cursor snaps back.
    static constexpr int kOffTrackUpdates = 4;
    /// Finished when the progress is within this many points of the path's end.
    static constexpr int kFinishWindow = 4;
    /// Unfinished with less than this share of the current charge left, one more charge is spent.
    static constexpr float kWasteShare = 0.3F;

    /// How an update left the game.
    enum class Result : std::uint8_t {
        Playing,    ///< Still going.
        Finished,   ///< The pattern is traced.
        Unfinished, ///< Out of paint, or 300 cells painted first.
    };

    /// What an update did that the caller acts on.
    struct Events {
        bool slipped = false;     ///< Off track: the cursor snapped back, the pad rumbles, the game pauses (and a
                                  ///< crew member may say 80 `tagcheer`).
        bool chargeSpent = false; ///< A charge ran out and the next was spent (the cursor snapped back, a pause).
    };

    /// Spends one charge of paint (spray paint, inventory item 3, − 1); false when there is none.
    using SpendCharge = std::function<bool()>;

    /// A game on `path` starting `fraction` of the way along it (the tag spot's painted fraction), with `tuning`.
    TagGame(std::vector<TagCell> path, float fraction, TagTuning tuning);

    /// One update with the left stick at (`stickX`, `stickY`), each -1 to 1 (y up), `elapsedMs` since the last.
    /// `spend` is asked when the current charge runs out.
    /// @orig 0x002748a8 TagGame_Update (unknown)
    /// @orig 0x00274710 TagGame_Track (unknown)
    Result update(float stickX, float stickY, std::uint32_t elapsedMs, const SpendCharge& spend);
    /// The game ends unfinished at once (a command outside the game's own).
    void abandon() { m_result = Result::Unfinished; }

    /// Whether, unfinished, the charge in use is spent too (less than 30 % of it left).
    [[nodiscard]] bool wastesCharge() const;

    [[nodiscard]] Result result() const { return m_result; }
    [[nodiscard]] const Events& events() const { return m_events; }
    [[nodiscard]] const std::vector<TagCell>& path() const { return m_path; }
    [[nodiscard]] const std::vector<TagCell>& painted() const { return m_painted; }
    [[nodiscard]] float cursorX() const { return m_cursorX; }
    [[nodiscard]] float cursorY() const { return m_cursorY; }
    /// The path point the player has reached.
    [[nodiscard]] std::size_t progress() const { return m_progress; }
    /// The progress as a share of the path, 0-1: the tag spot's painted fraction.
    [[nodiscard]] float fraction() const;
    /// Milliseconds left of the charge in use, and of the pause.
    [[nodiscard]] std::uint32_t chargeLeftMs() const { return m_chargeLeftMs; }
    [[nodiscard]] std::uint32_t pauseLeftMs() const { return m_pauseLeftMs; }

  private:
    // The cursor back on the path at the progress, and the game paused.
    void snapBack();
    // Paints the cell under the cursor when it is far enough from every painted one.
    void paint();

    std::vector<TagCell> m_path;
    TagTuning m_tuning;
    std::vector<TagCell> m_painted;
    float m_cursorX = 0.0F;
    float m_cursorY = 0.0F;
    std::size_t m_progress = 0;
    std::uint32_t m_playedMs = 0;
    std::uint32_t m_chargeLeftMs = 0;
    std::uint32_t m_pauseLeftMs = 0;
    int m_offTrack = 0;
    Result m_result = Result::Playing;
    Events m_events;
};

} // namespace coney
