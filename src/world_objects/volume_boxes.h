// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace coney::world_objects {

/// One volume box (`AddVolumeBox`): an axis-aligned box, turned about the vertical axis by a 2 × 2 matrix
/// (`RotateVolumeBox`), that a level script uses as a trigger. Coney keeps the behaviour of the original's kind-0
/// record, not its layout (docs/research/scripting.md#triggers).
struct VolumeBox {
    double handle = 0;                     ///< From the world objects' handle space.
    std::string name;                      ///< For the scripts' own reference (`vMark01`).
    int kind = 0;                          ///< 0 a trigger box; 2 and 3 other kinds (not traced: no trigger update).
    std::array<float, 3> low{};            ///< The minimum corner (`+0x18` holds its z).
    std::array<float, 3> high{};           ///< The opposite corner (`+0x28` holds its z).
    std::array<float, 4> turn{1, 0, 0, 1}; ///< `+0x48`-`+0x54`: m00, m01, m10, m11.
    bool enabled = true;                   ///< `+0x68`.
    std::vector<double> occupants;         ///< `+0x70`: the humans inside, at most kMaxOccupants.
    std::uint64_t nextStayMs = 0;          ///< `+0x1e0`: when message 5 may be sent again.
};

/// A human the boxes test: its handle, where its feet are, and whether it is alive (a dead human is skipped).
struct BoxSubject {
    double handle = 0;
    std::array<float, 3> position{};
    bool alive = true;
};

/// The level's volume boxes and their trigger update: a kind-0 box that is enabled sends itself message 3 when a human
/// comes inside, 5 while one stays (at most once per repeat period) and 4 when one leaves or dies inside.
///
/// Research: docs/research/scripting.md#triggers, docs/references/bindings/world.md#addvolumebox,
/// docs/references/bindings/world.md#rotatevolumebox
class VolumeBoxes {
  public:
    /// The most humans a box keeps inside it (`+0x70`, 60 handles).
    static constexpr std::size_t kMaxOccupants = 60;
    /// The repeat period of message 5, ms: the handler component's default (`+0x74`, 1000, `0x00384a10`).
    static constexpr std::uint64_t kStayPeriodMs = 1000;
    /// The messages a box sends itself.
    static constexpr int kEntered = 3;
    static constexpr int kLeft = 4;
    static constexpr int kInside = 5;

    /// What a box's message goes to: (the box, the message, the human).
    using Send = std::function<void(double box, int message, double human)>;

    /// `AddVolumeBox(name, kind, corner, size, enable)`: a box with `handle` from `corner` to `corner + size`.
    /// @orig 0x004125b8 VolumeBox_Add (unknown)
    const VolumeBox& add(double handle, std::string_view name, int kind, const std::array<float, 3>& corner,
                         const std::array<float, 3>& size, bool enabled);
    /// `RotateVolumeBox(box, m00, m01, m10, m11)`: turns the box about the vertical axis; an unknown box is ignored.
    /// @orig 0x00412c40 VolumeBox_SetRotation (unknown)
    void rotate(double handle, const std::array<float, 4>& turn);
    /// The box with `handle`; null when none has it.
    [[nodiscard]] VolumeBox* find(double handle);
    [[nodiscard]] const VolumeBox* find(double handle) const;
    /// Every box, oldest first.
    [[nodiscard]] const std::vector<VolumeBox>& all() const { return m_boxes; }
    /// Forgets every box: the level is unloaded.
    void clear() { m_boxes.clear(); }

    /// Whether `point` is inside `box`: between its lowest and highest z, and inside its four corners turned about
    /// its centre by its matrix (the bounding-sphere test that comes first is implied by this one).
    /// @orig 0x00412a18 VolumeBox_IsInside (unknown)
    [[nodiscard]] static bool inside(const VolumeBox& box, const std::array<float, 3>& point);

    /// One trigger update of every enabled kind-0 box at game time `nowMs` over `subjects`: a living human inside and
    /// new is kept (while there is room) and gets 3; one inside and kept gets 5 when the period is due (the box's next
    /// 5 then a period later); a kept one no longer inside, dead or gone gets 4 and is dropped. The messages go to
    /// `send` after the box's update.
    /// @orig 0x00415378 VolumeBox_Update (unknown)
    void update(std::span<const BoxSubject> subjects, std::uint64_t nowMs, const Send& send);

  private:
    std::vector<VolumeBox> m_boxes;
};

} // namespace coney::world_objects
