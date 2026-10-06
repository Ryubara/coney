// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

#include "world_objects/volume_boxes.h"

namespace coney::world_objects {

/// One trigger sphere (`TriggerSphereCfg`): a radius round an object (a flag, a human or a prop) that sends the object
/// message 3 when a human comes inside, 5 while one stays and 4 when one leaves or dies. Coney keeps the behaviour of
/// the original's 0x184-byte record, not its layout (docs/research/scripting.md#triggers).
struct TriggerSphere {
    double object = 0;                 ///< `+0x170`: the object the sphere is round and whose handler hears it.
    float radius = 0.0F;               ///< `+0x174`, metres.
    int mode = 0;                      ///< `+0x17c`: 0 distance only, 1 and 2 also a clear line (see TriggerSpheres).
    bool armed = false;                ///< `+0x180`.
    std::uint64_t stayPeriodMs = 1000; ///< `+0x164`: the period of message 5 (1000 from `0x00414480`).
    std::uint32_t handlerPeriodMs = 1000; ///< `TriggerSphereCfg`'s last argument, the handler component's `+0x74`.
    std::vector<double> occupants;        ///< The humans inside, at most VolumeBoxes::kMaxOccupants.
    std::uint64_t nextStayMs = 0;         ///< When message 5 may be sent again.
};

/// The level's trigger spheres: a pool of 100, one per object, updated round-robin so each sphere is checked every
/// fifth frame, with the volume boxes' enter / stay / leave rules (VolumeBoxes::update).
///
/// **Coney's choices** where the page is silent: the sphere's own object is never one of its occupants (a human with a
/// sphere would otherwise enter it at once); the clear-line tests are Coney's (the original's `0x0024dee8` and
/// `0x0024df40` are not traced): mode 1 a line from the sphere's centre to a point 1 m above the human's feet, mode 2
/// the same line with its start also raised 1 m; with no line test given every line is clear.
///
/// Research: docs/research/scripting.md#triggers, docs/references/bindings/world.md#triggerspherecfg
class TriggerSpheres {
  public:
    /// Spheres the pool holds (`0x006f3f50`).
    static constexpr std::size_t kCapacity = 100;
    /// Frames between two checks of one sphere.
    static constexpr std::uint32_t kFramesPerCheck = 5;
    /// The height above the feet (and, in mode 2, above the centre) of the clear-line tests' ends: Coney's choice.
    static constexpr float kLineRaise = 1.0F;

    /// Where an object is: its position, or nothing when the handle names no live object.
    using Locate = std::function<std::optional<std::array<float, 3>>(double object)>;
    /// Whether the line between two points is clear of the level's walls.
    using ClearLine = std::function<bool(const std::array<float, 3>& from, const std::array<float, 3>& to)>;

    /// `TriggerSphereCfg(object, enable, radius, mode, interval)`: the object's sphere, made when it has none (refused,
    /// false, when the pool is full), then configured. Occupants are kept across a reconfiguration.
    /// @orig 0x00414bc0 TriggerSphere_Configure (unknown)
    bool configure(double object, bool armed, float radius, int mode, std::uint32_t intervalMs);
    /// The sphere round `object`; null when it has none.
    [[nodiscard]] TriggerSphere* find(double object);
    [[nodiscard]] const TriggerSphere* find(double object) const;
    /// Every sphere, oldest first.
    [[nodiscard]] const std::vector<TriggerSphere>& all() const { return m_spheres; }
    /// Forgets every sphere: the level is unloaded.
    void clear() {
        m_spheres.clear();
        m_frame = 0;
    }

    /// Sets how objects are found (null finds none, so no sphere sends anything) and the clear-line test (null: every
    /// line is clear).
    void setLocate(Locate locate) { m_locate = std::move(locate); }
    void setClearLine(ClearLine clearLine) { m_clearLine = std::move(clearLine); }

    /// One frame of the pool at game time `nowMs` over `subjects`: the spheres whose turn it is (index modulo
    /// kFramesPerCheck against the frame count) are checked; their messages go to `send` (object, message, human)
    /// once each sphere's check is done. An unarmed sphere, or one whose object is gone, is skipped.
    /// @orig 0x00414398 TriggerSpheres_Update (unknown)
    /// @orig 0x004146e0 TriggerSphere_Update (unknown)
    void update(std::span<const BoxSubject> subjects, std::uint64_t nowMs, const VolumeBoxes::Send& send);

    /// Whether `point` counts as inside `sphere` centred on `centre`: within the radius (in 3D) and, in modes 1 and 2,
    /// with a clear line to it.
    [[nodiscard]] bool accepts(const TriggerSphere& sphere, const std::array<float, 3>& centre,
                               const std::array<float, 3>& point) const;

  private:
    std::vector<TriggerSphere> m_spheres;
    Locate m_locate;
    ClearLine m_clearLine;
    std::uint32_t m_frame = 0;
};

} // namespace coney::world_objects
