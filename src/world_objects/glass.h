// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "animation/anim_math.h"
#include "world_objects/nav_links.h"
#include "world_objects/object_services.h"

// The breakable glass panes: their types (`CfgSetGlassProperties`), the panes level scripts spawn
// (`SpawnBreakableGlass`), what breaks them and the shatter. Research: docs/research/objects.md#glass,
// docs/research/objects.md#pane, docs/research/objects.md#shatter

namespace coney::world_objects {

/// Glass types the table holds. **Coney's choice**: the page gives the table's place, not its size; the scripts
/// configure 0-18.
inline constexpr std::size_t kGlassTypeCount = 32;

/// Glass types with their own behaviour (docs/research/objects.md#pane).
namespace glass_type {
inline constexpr int kCarWindow = 12;    ///< Frees the car stereos within 2 m when it breaks.
inline constexpr int kStained = 14;      ///< Its own sprite batch; the `sub_stained_glass` shatter.
inline constexpr int kInvisible = 15;    ///< Colour 0: never drawn.
inline constexpr int kPreBroken = 17;    ///< Starts broken.
inline constexpr int kPreBrokenGap = 18; ///< Starts broken and opens its path polygon (flag 8).
} // namespace glass_type

/// One entry of the glass type table (`GlassTaskManager` `+0x14 + 16 × type`).
struct GlassType {
    bool windowLink = false;        ///< `+0x00`: the pane ties a navigation link (charged through, opened by breaking).
    bool alarm = false;             ///< `+0x04`: breaking the pane reports a break-in.
    std::uint32_t sprite = 0;       ///< `+0x08`: the whole pane's sprite word; its low 16 bits are the rectangle.
    std::uint32_t brokenSprite = 0; ///< `+0x0c`: the broken pane's sprite word; 0 hides the pane once broken.
};

/// `SpawnBreakableGlass`' arguments.
struct GlassSpawn {
    int type = 0;
    anim::Vec3 corner{};  ///< The first corner.
    anim::Vec3 cornerU{}; ///< The corner along the width from it.
    anim::Vec3 cornerV{}; ///< The corner along the height from it.
    std::array<float, 2> uv0{};
    std::array<float, 2> uv1{};
    int flag = 0;                             ///< Pushed and dropped by the initialiser.
    std::array<std::uint32_t, 2> triangles{}; ///< The two static collision triangles that stand for the pane.
};

/// A pane's colour word at spawn, and type 15's.
inline constexpr std::uint32_t kPaneColour = 0x808080e0U;
/// Type 14's sprite word: its own batch.
inline constexpr std::uint32_t kStainedSprite = 0x00020000U;

/// One pane (the `0x100`-byte `glass_script` task; Coney keeps its behaviour, not its layout).
struct GlassPane {
    double handle = kNoObject;
    int type = 0;
    anim::Vec3 centre{};                ///< `+0x10`: the first corner + half of each edge.
    anim::Vec3 edgeU{};                 ///< `+0x70`: the edge along the width.
    anim::Vec3 normal{};                ///< `+0x80`: the unit normal, width × height.
    anim::Vec3 edgeV{};                 ///< `+0x90`: the edge along the height.
    float width = 0.0F;                 ///< `+0xcc`, metres.
    float height = 0.0F;                ///< `+0xd4`, metres.
    std::uint32_t sizeWord = 0;         ///< `+0xc8`: whole metres of width, height `<< 16`.
    std::uint32_t sprite = 0;           ///< `+0xb0`.
    std::uint32_t colour = kPaneColour; ///< `+0xb4`.
    std::array<float, 2> uv0{};
    std::array<float, 2> uv1{};
    std::array<std::uint32_t, 2> triangles{}; ///< `+0xd8` / `+0xdc`.
    bool hidden = false;                      ///< Flag 4.
    bool broken = false;                      ///< Flag `0x800000`.
    std::uint16_t alarmBits = 0;              ///< `+0xf8`: 3 with the alarm.
    std::optional<std::uint32_t> link{};      ///< The window link's link (with the window link), found at spawn.
    std::optional<std::uint32_t> polygon{};   ///< The path polygon the pane lies in, if any.
};

/// What a shatter makes: its shard count and size and the material whose pair it sounds.
struct ShatterPlan {
    int count = 0;
    float shardSize = 0.0F;
    std::uint8_t soundMaterial = material::kGlass;
};

/// Shards' size in a small shatter, and Coney's stand-in for a large one (the page gives only the small size).
inline constexpr float kSmallShardSize = 0.06F;
/// The shard count's cap: the size word is overwritten with `0x4f` above it.
inline constexpr int kMaxShards = 79;
/// How far a shard lands from the centre, as a fraction of the half-width and half-height.
inline constexpr float kShardSpread = 0.571F;

/// The shatter of a pane with `sizeWord`: 10 × width × height shards (whole metres, at most kMaxShards), the
/// `GLASS` × `GLASS` sound; a count under 2 or a 1 × 1 pane gives 10 small shards and the `GLASS_SMALL` sound.
/// @orig 0x003e4cb8 SubGlass_Update (unknown)
[[nodiscard]] ShatterPlan shatterPlan(std::uint32_t sizeWord);

/// The level's glass type table and panes.
///
/// Research: docs/research/objects.md#glass
class GlassPanes {
  public:
    /// `CfgSetGlassProperties(type, windowLink, alarm, sprite, brokenSprite)`: one entry of the table. A type outside
    /// the table is ignored.
    /// @orig 0x0038fab8 GlassTypes_Set (unknown)
    void setType(int type, const GlassType& entry);
    /// The type table's entry; nothing outside the table.
    [[nodiscard]] const GlassType* type(int type) const;

    /// `SpawnBreakableGlass`: makes the pane with `handle` (the caller's, from the world objects' handle space) from
    /// `spawn`: its geometry, its triangles made two-sided with material `GLASS`, its sprite and colour, types 17 and
    /// 18 broken from the start (18 also opens its polygon), the alarm bits and the window link.
    /// @orig 0x0039c0e0 Glass_Spawn (unknown)
    /// @orig 0x0038f8a8 GlassManager_Create (unknown)
    /// @orig 0x003e29e8 GlassScript_Init (unknown)
    const GlassPane& spawn(double handle, const GlassSpawn& spawn, ObjectWorld& world);

    /// Message 1 to the pane `handle`: when it is not broken, the shatter (the broken sprite or hidden, the shards and
    /// the sound), its triangles off, its body removed and the pane broken; a car window frees the stereos within 2 m.
    /// Returns whether it broke now (false for a broken pane or no pane).
    /// @orig 0x003e2d90 GlassScript_Message (unknown)
    bool hit(double handle, ObjectWorld& world);
    /// Message 0: the shatter and the triangles off, without marking it broken.
    void shatterOnly(double handle, ObjectWorld& world);

    /// `Glass_Break(pane, breaker, object)`, after a human's or a thrown object's hit: the alarm (the `CrimeScene` flag
    /// moved to the pane and a break-in reported at it), the window link opened, and the window-look flags within the
    /// pane's larger side switched off.
    /// @orig 0x0038f378 Glass_Break (unknown)
    void breakBy(double handle, double breaker, ObjectWorld& world);

    /// A human's landed hit on the pane: hit(), and when it broke, breakBy() and the pane statistic.
    /// @orig 0x0021b290 Strike_Contact (unknown)
    bool humanHit(double handle, double attacker, ObjectWorld& world);
    /// A thrown object's hit, by `thrower`: as humanHit().
    /// @orig 0x00393538 Thrown_HitObject (unknown)
    bool thrownHit(double handle, double thrower, ObjectWorld& world);

    /// `BreakGlassInRadius(centre, radius)`: hit() on every pane whose centre is within `radius` (no alarm, no window
    /// link), then the `TYPE_GLASS` objects. Returns how many panes broke.
    /// @orig 0x003963b8 World_BreakGlassInRadius (unknown)
    std::size_t breakInRadius(anim::Vec3 centre, float radius, ObjectWorld& world);

    /// The pane with `handle`; null when there is none.
    [[nodiscard]] const GlassPane* find(double handle) const;
    /// The pane one of whose two triangles is `triangle`; null when none.
    [[nodiscard]] const GlassPane* findByTriangle(std::uint32_t triangle) const;
    /// Every pane, oldest first.
    [[nodiscard]] std::span<const GlassPane> panes() const { return m_panes; }
    /// Forgets the panes (the level's unload); the type table stays, as the global configuration does.
    void clearPanes() { m_panes.clear(); }

  private:
    // The pane with `handle`, for changing it; null when there is none.
    GlassPane* findMutable(double handle);
    // The shatter of `pane`: its sprite, the effect, the triangles and the body; `markBroken` for message 1.
    void shatter(GlassPane& pane, bool markBroken, ObjectWorld& world);
    // The `sub_glass` effect: the sound and, when wanted, the shards across the pane.
    void shatterEffect(const GlassPane& pane, ObjectWorld& world);

    std::array<GlassType, kGlassTypeCount> m_types{};
    std::vector<GlassPane> m_panes;
};

} // namespace coney::world_objects
