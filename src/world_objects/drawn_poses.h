// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <map>
#include <utility>

#include "animation/anim_math.h"
#include "world_objects/object_tasks.h"

namespace coney::world_objects {

/// `from` (alpha 0) blended towards `to` (alpha 1): the position lerped, the rotation slerped the short way round.
/// Returns `to` itself at alpha >= 1 and `from` at alpha <= 0, so lockstep (alpha 1) draws the newest pose bit for bit.
[[nodiscard]] WorldPose blendPose(const WorldPose& from, const WorldPose& to, float alpha);

/// The poses a set of drawn things (world objects, door leaves, icons) had at the last two simulation steps, so a
/// render between the steps draws each one between them (docs/guides/conventions.md#update-and-render).
///
/// The rule, as for coney::Interpolated: each update calls commit() first, then record() for every thing drawn that
/// step; render() asks drawn() for each, with the frame's alpha. A thing with no pose the step before (just spawned,
/// just let go of, back in the world) is drawn as it is now, and so is a jump: a different `generation` on the two
/// steps (a script placing the object, a drop) or a move longer than kMostBlendedMetres.
///
/// **Coney's choice**: the original draws once per step and blends nothing; this only smooths a display that runs
/// faster than the 1/30 s step. Gameplay never reads it.
///
/// Research: docs/research/rendering.md#interpolation
template <typename Key> class DrawnPoses {
  public:
    /// The farthest a thing is blended across in one step, metres: farther is a jump. **Coney's choice**: nothing in
    /// the game travels 10 m in 1/30 s (300 m/s); a thrown object goes about 1 m.
    static constexpr float kMostBlendedMetres = 10.0F;

    /// Starts a step: the poses recorded in the step before become the previous ones.
    void commit() {
        m_previous = std::move(m_current);
        m_current.clear();
    }

    /// Records thing `key` at `pose` in this step; `generation` changes when it jumps (a new placement).
    void record(const Key& key, const WorldPose& pose, std::uint32_t generation = 0) {
        m_current.insert_or_assign(key, Entry{pose, generation});
    }

    /// Where thing `key`, at `newest` in the newest step, is drawn `alpha` of the way from the step before: blended
    /// from its previous pose when it had one in the same generation and moved no farther than kMostBlendedMetres,
    /// else `newest` as given.
    [[nodiscard]] WorldPose drawn(const Key& key, const WorldPose& newest, float alpha) const {
        const auto before = m_previous.find(key);
        const auto now = m_current.find(key);
        if (before == m_previous.end() || now == m_current.end() ||
            before->second.generation != now->second.generation) {
            return newest;
        }
        const anim::Vec3 from = before->second.pose.position;
        const float dx = newest.position.x - from.x;
        const float dy = newest.position.y - from.y;
        const float dz = newest.position.z - from.z;
        if (dx * dx + dy * dy + dz * dz > kMostBlendedMetres * kMostBlendedMetres) {
            return newest;
        }
        return blendPose(before->second.pose, newest, alpha);
    }

  private:
    // One thing's pose in a step.
    struct Entry {
        WorldPose pose;
        std::uint32_t generation = 0;
    };

    std::map<Key, Entry> m_previous;
    std::map<Key, Entry> m_current;
};

} // namespace coney::world_objects
