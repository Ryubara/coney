// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <vector>

#include "animation/anim_math.h"
#include "core/game_random.h"
#include "effects/particles.h"

// The small blinking stars on objects a player can take or pick: a `sub_triglint` at a pickup lying in the world or at
// a lock-pickable door, and the three `sub_glint` sprites it owns.
// Research: docs/research/particles.md#glints

namespace coney::effects {

/// What owns a triglint: the object it marks, and where the triglint stands (game axes, z up).
struct GlintOwner {
    double object = 0;
    anim::Vec3 position;
};

/// One `sub_triglint` and its three `sub_glint`s. Stepped in 60 Hz ticks.
///
/// Research: docs/research/particles.md#glints
class Triglint {
  public:
    /// The triglint's update interval, ticks.
    static constexpr int kInterval = 60;
    /// An unattached glint's first update, ticks after its spawn.
    static constexpr int kGlintFirstUpdate = 30;
    /// An unattached glint's next interval: kGlintIntervalMin + `Random_Int(kGlintIntervalRandom)`, 20 to 30 ticks.
    static constexpr int kGlintIntervalMin = 20;
    static constexpr std::uint32_t kGlintIntervalRandom = 10;
    /// The three glints' offsets from the triglint (metres, no rotation) and their sizes (before × kDrawnSize).
    static constexpr std::array<anim::Vec3, 3> kOffsets{
        anim::Vec3{0.0F, 0.043F, 0.067F}, anim::Vec3{-0.071F, -0.025F, -0.027F}, anim::Vec3{0.061F, -0.032F, -0.040F}};
    static constexpr std::array<float, 3> kSizes{1.3F, 0.75F, 0.65F};
    /// `sub_glint` draws its sprite at the given size × this.
    static constexpr float kDrawnSize = 0.15F;
    /// An "on" glint's colour word `0xRRGGBBAA`: white, half transparent.
    static constexpr std::uint32_t kOnColour = 0xFFFFFF80U;
    /// The sprite: `part_page1` rectangle 41 (sprite word `0x40029`).
    static constexpr ParticleSheet kSheet = ParticleSheet::PartPage1;
    static constexpr std::uint16_t kRect = 41;

    /// `Random_Int(n)`: draws a whole number from 0 to n, inclusive.
    using Random = std::function<std::uint32_t(std::uint32_t n)>;
    /// Whether a point (game axes) is seen by a camera within `distance` metres.
    using Visible = std::function<bool(anim::Vec3 point, float distance)>;

    /// A triglint at `position`: hidden itself, its update due in kInterval ticks, its three glints spawned.
    /// @orig 0x003eaf98 SubTriglint_Init (unknown)
    explicit Triglint(anim::Vec3 position);

    /// One 60 Hz tick: each glint's blink, and every kInterval ticks the visibility check, which removes the glints
    /// when the triglint's position is not seen by a camera within 30 m and spawns the missing ones when it is.
    /// @orig 0x003eb0d0 SubTriglint_Update (unknown)
    /// @orig 0x003e96a0 SubGlint_Update (unknown)
    void tick(const Visible& visible, const Random& random);

    /// Moves the triglint (its owner moved); its glints follow.
    void moveTo(anim::Vec3 position) { m_position = position; }
    /// Where it stands.
    [[nodiscard]] anim::Vec3 position() const { return m_position; }
    /// How many of its glints exist now (0 to 3).
    [[nodiscard]] std::size_t glints() const;
    /// Adds the sprites of its glints that are on to `out`: at the triglint's position + offset, white, half
    /// transparent, sized kSizes × kDrawnSize. A glint that is off draws nothing (alpha 0, size 0).
    void addSprites(std::vector<Particle>& out) const;

  private:
    // One `sub_glint`, unattached.
    struct Glint {
        bool exists = false;
        bool on = false;
        int countdown = kGlintFirstUpdate; // ticks to its next update
    };

    // Spawns the glints that do not exist.
    // @orig 0x003eade8 SubTriglint_SpawnGlints (unknown)
    void spawnGlints();
    // Removes all three.
    // @orig 0x003eaf38 SubTriglint_RemoveGlints (unknown)
    void removeGlints();

    anim::Vec3 m_position;
    int m_countdown = kInterval;
    std::array<Glint, 3> m_glints{};
};

/// The level's triglints, one per owner: sync() makes one for each new owner and removes (message `0x15`) those whose
/// owner is gone, tick() steps them, sprites() gives their glints' sprites to draw.
///
/// The blink draws `Random_Int` (`0x003353b8`: the next raw number mod (n + 1)) from the game's fixed table on its own
/// index, the particles' stream (`0x006eb8b8`); without the table (setTable() not called: no disc, a test) the
/// stand-in generator of GameRandom draws.
///
/// **Coney's stand-ins** where the page is open: a new glint starts off (its first update turns it on); the stream's
/// index starts at 0 when the level starts and only the glints draw from it; and a `pickup_item`'s own test (within
/// 30 m on screen) is left to the triglint's, so every lying pickup that may glint keeps one.
class Triglints {
  public:
    /// Draws from the game's random table (GameRandom::kTableSize numbers) from now on.
    void setTable(std::span<const std::uint32_t> table) { m_random.setTable(table); }

    /// The owners this step: a triglint is made for each owner that has none (`PickupItem_ShowGlint`, a door made
    /// pickable), moved with its owner, and removed with its glints for each owner no longer listed.
    /// @orig 0x003f16e8 PickupItem_ShowGlint (unknown)
    /// @orig 0x003eb060 SubTriglint_OnMessage (unknown)
    void sync(std::span<const GlintOwner> owners);
    /// `ticks` 60 Hz ticks of every triglint.
    void tick(int ticks, const Triglint::Visible& visible);
    /// The sprites of every glint that is on now.
    [[nodiscard]] std::vector<Particle> sprites() const;
    /// The triglint of `object`; null when it has none.
    [[nodiscard]] const Triglint* find(double object) const;
    /// How many triglints there are.
    [[nodiscard]] std::size_t count() const { return m_triglints.size(); }
    /// Removes every triglint: the level is unloaded.
    void clear() { m_triglints.clear(); }

  private:
    // `Random_Int(n)`: a draw of 0 to n from the stream.
    std::uint32_t draw(std::uint32_t n);

    std::map<double, Triglint> m_triglints;
    GameRandom m_random; // the particles' stream
};

} // namespace coney::effects
