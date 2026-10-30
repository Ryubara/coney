// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace coney::world_objects {

/// One volume box (`AddVolumeBox`): an axis-aligned box, turned about the vertical axis by a 2 × 2 matrix
/// (`RotateVolumeBox`), that a level script uses as a trigger. Coney keeps the behaviour of the original's kind-0
/// record, not its layout (docs/research/scripting.md#triggers).
struct VolumeBox {
    double handle = 0;                     ///< From the world objects' handle space.
    std::string name;                      ///< For the scripts' own reference (`vMark01`).
    int kind = 0;                          ///< 0 a trigger box; 2 a players' trigger box; 3 turf (no trigger update).
    std::array<float, 3> low{};            ///< The minimum corner (`+0x18` holds its z).
    std::array<float, 3> high{};           ///< The opposite corner (`+0x28` holds its z).
    std::array<float, 4> turn{1, 0, 0, 1}; ///< `+0x48`-`+0x54`: m00, m01, m10, m11.
    bool enabled = true;                   ///< `+0x68`.
    std::vector<double> occupants;         ///< `+0x70`: the humans inside, at most kMaxOccupants.
    std::uint64_t nextStayMs = 0;          ///< `+0x1e0`: when message 5 may be sent again.
};

/// A human the boxes test: its handle, where its feet are, whether it is alive (a dead human is skipped) and whether
/// it is a player's (the only ones a kind-2 box tests).
struct BoxSubject {
    double handle = 0;
    std::array<float, 3> position{};
    bool alive = true;
    bool player = false;
};

/// The level's volume boxes and their trigger update: a kind-0 box that is enabled sends itself message 3 when a human
/// comes inside, 5 while one stays (at most once per repeat period) and 4 when one leaves or dies inside.
///
/// Research: docs/research/scripting.md#triggers, docs/research/trigger-updates.md,
/// docs/references/bindings/world.md#addvolumebox,
/// docs/references/bindings/world.md#rotatevolumebox
class VolumeBoxes {
  public:
    /// The most humans a box keeps inside it (`+0x70`, 60 handles).
    static constexpr std::size_t kMaxOccupants = 60;
    /// The repeat period of message 5, ms, when setStayPeriod() gave none: the handler component's default (`+0x74`,
    /// 1000, `0x00384a10`).
    static constexpr std::uint64_t kStayPeriodMs = 1000;
    /// The messages a box sends itself.
    static constexpr int kEntered = 3;
    static constexpr int kLeft = 4;
    static constexpr int kInside = 5;
    /// Damage done by a human standing inside (docs/research/scripting.md#triggers).
    static constexpr int kDamaged = 6;
    /// An owned object taken from inside (Item_TakeOwned, docs/research/crimes.md#owned-items).
    static constexpr int kStolen = 7;

    /// What a box's message goes to: (the box, the message, the human).
    using Send = std::function<void(double box, int message, double human)>;
    /// Whether a handle still names an object (`0x00390268`); none set: every handle does.
    using Resolves = std::function<bool(double handle)>;

    /// A box's message-5 period, ms: its handler component's repeat period.
    using Period = std::function<std::uint64_t(double box)>;

    /// Sets the test update() asks of an occupant missing from its subjects.
    void setResolves(Resolves resolves) { m_resolves = std::move(resolves); }
    /// Sets where update() reads each box's message-5 period; none set: kStayPeriodMs for every box.
    void setStayPeriod(Period period) { m_stayPeriod = std::move(period); }

    /// Whether `AddVolumeBox` makes a box of `kind`: 0, 2 and 3 have pools, 1 is refused and any other has none
    /// (docs/research/trigger-updates.md#add).
    [[nodiscard]] static constexpr bool makesKind(int kind) { return kind == 0 || kind == 2 || kind == 3; }

    /// `AddVolumeBox(name, kind, corner, size, enable)`: a box with `handle` from `corner` to `corner + size`; null,
    /// and no box, for a kind makesKind() refuses.
    /// @orig 0x004125b8 VolumeBox_Add (unknown)
    const VolumeBox* add(double handle, std::string_view name, int kind, const std::array<float, 3>& corner,
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

    /// A kind-2 box keeps at most this many occupants: the two players (`+0xf0`).
    static constexpr std::size_t kMaxPlayerOccupants = 2;

    /// One trigger update of every enabled kind-0 box (updateBox()) and kind-2 box (updatePlayerBox()) at game time
    /// `nowMs` over `subjects`, the messages going to `send` as they happen.
    /// @orig 0x00415378 VolumeBox_Update (unknown)
    /// @orig 0x004134f8 PlayerVolumeBox_Update (unknown)
    void update(std::span<const BoxSubject> subjects, std::uint64_t nowMs, const Send& send);

    /// Damage done by a human whose feet are at `human`: every enabled kind-0 box that `human` is inside goes to
    /// `send` with message kDamaged, in the order the boxes were added. Whether the box has a handler for it is the
    /// delivery's test. The 30 m alert that follows in the original (`0x00293768`) is not here.
    /// @orig 0x00413018 VolumeBoxes_SendDamageMessage (unknown)
    void sendDamage(const std::array<float, 3>& human, const std::function<void(double box)>& send) const;

    /// An owned object taken at `spot`: every enabled kind-0 box that `spot` is inside goes to `send` with message
    /// kStolen, in the order the boxes were added (Item_TakeOwned tells the boxes round the spot through the inside
    /// test `0x00412a18`). **Coney's reading**: the page names the inside test but not which boxes are asked, so they
    /// are those message 6 asks (enabled, kind 0), and the spot is the object's, not the taker's; the witnesses'
    /// alert, the crime score and the lines that follow in the original are not here.
    /// @orig 0x00412dc8 Item_TakeOwned (unknown)
    void sendTheft(const std::array<float, 3>& spot, const std::function<void(double box)>& send) const;

  private:
    // One update of the kind-0 box at `index`: the stay timer (stayDue()); each living human inside enters (3) or, on
    // a due update, stays (5), and a listed one no longer inside leaves (4); then each occupant dead or no longer
    // inside leaves (4), one whose handle no longer resolves being skipped and kept. Each message goes at once, and the
    // update ends when a handler disables the box (docs/research/trigger-updates.md#box).
    // @orig 0x00415378 VolumeBox_Update (unknown)
    void updateBox(std::size_t index, std::span<const BoxSubject> subjects, std::uint64_t nowMs, const Send& send);
    // One update of the kind-2 box at `index` over the two players' humans, with the kind-0 box's messages and no pass
    // over the occupants (docs/research/trigger-updates.md#player-box).
    // @orig 0x004134f8 PlayerVolumeBox_Update (unknown)
    void updatePlayerBox(std::size_t index, std::span<const BoxSubject> subjects, std::uint64_t nowMs,
                         const Send& send);
    // Whether the box at `index` is still the enabled one with `handle`.
    // @orig 0x00415378 VolumeBox_Update (unknown)
    // @orig 0x004134f8 PlayerVolumeBox_Update (unknown)
    [[nodiscard]] bool stillEnabled(std::size_t index, double handle) const;
    // Whether `box`'s stay timer is due at `nowMs` (strictly past it), re-arming it to now + the box's period when it
    // is (docs/research/trigger-updates.md#stay-timer).
    // @orig 0x00415378 VolumeBox_Update (unknown)
    // @orig 0x004134f8 PlayerVolumeBox_Update (unknown)
    bool stayDue(VolumeBox& box, std::uint64_t nowMs) const;
    // Lists `human` in `box`: a free place among `room`, else the first whose handle no longer resolves, else nowhere.
    // @orig 0x00415378 VolumeBox_Update (unknown)
    // @orig 0x004134f8 PlayerVolumeBox_Update (unknown)
    void addOccupant(VolumeBox& box, double human, std::size_t room) const;

    std::vector<VolumeBox> m_boxes;
    Resolves m_resolves;
    Period m_stayPeriod;
};

} // namespace coney::world_objects
