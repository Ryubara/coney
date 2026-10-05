// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace coney::world_objects {

/// Where a world object stands: its position (metres, game axes with z up) and its heading in degrees about z.
struct Placement {
    std::array<float, 3> position{};
    float headingDegrees = 0.0F;
};

/// Finds the live world object behind a handle and says where it is; nothing when the handle names no live object.
/// The flags ask it for a flag's parent (docs/research/flags.md#position).
using ObjectLocator = std::function<std::optional<Placement>(double handle)>;

/// One world flag: a named point with a facing that a level script places (`AddFlag`). Coney keeps the behaviour of
/// the original's 0xf0-byte record, not its layout (docs/research/flags.md#record).
struct WorldFlag {
    double handle = 0;               ///< `+0x30`: the flag's handle, from the world objects' handle space.
    std::string name;                ///< `+0x34`: at most kNameLength characters of the name AddFlag was given.
    std::array<float, 3> position{}; ///< `+0x10`: x, y, z as AddFlag was given them.
    float headingDegrees = 0.0F;     ///< `+0x44`: the heading in degrees, as given.
    double parent = 0;               ///< `+0x48`: the object the flag follows; the nil handle from AddFlag.
    double user = 0;                 ///< `+0xdc`: who is using the flag; the nil handle at first.
    bool enabled = true;             ///< `+0xd4`: 1 from AddFlag (`FlagEnable` writes it).
    int kind = 0;                    ///< `+0xd0`: AddFlag's fourth argument, 16 bits; meaning not traced.
    int kind2 = 0;                   ///< `+0xd8`: AddFlag's fifth argument, read as 16 bits and sign-extended.
};

/// The level's world flags: the pool `CfgSetDatabaseSizes` makes, the flags `AddFlag` and `InitLevel` add to it, the
/// name search and the position and heading that follow a parent. Every flag lasts until the level unloads (clear()).
///
/// Coney's choices (docs/research/flags.md#pool): the pool is a capacity, not a fixed array, so a flag past it is
/// still made and counted in overflows() (the original constructs it on a null pointer); and a second
/// `CfgSetDatabaseSizes` starts a new pool, as the original allocates a new one: the name search then sees only the new
/// pool's flags, while every flag made in the level still resolves by its handle (the handle table keeps them).
///
/// Research: docs/research/flags.md
class WorldFlags {
  public:
    /// Characters of a name the flag keeps: the record has 16 bytes, the last one the terminator.
    static constexpr std::size_t kNameLength = 15;
    /// Records the pool holds beyond what `CfgSetDatabaseSizes` asks for: it passes `flags + 2`, the pool adds 2.
    static constexpr std::size_t kPoolExtra = 4;

    /// `CfgSetDatabaseSizes`' flag count: makes a new pool of `flags + 4` records. Flags made so far stay findable
    /// by handle but not by name.
    /// @orig 0x004158f8 FlagPool_Create (flags.cpp)
    void createPool(std::size_t flags);

    /// Makes a flag in the pool's next slot with `handle` (the caller's, from the world objects' handle space) and
    /// returns it: the name cut to kNameLength characters, enabled, no parent, no user. A slot past the pool's capacity
    /// is still made and counted (overflows()).
    /// @orig 0x00415c18 Flag_Add (flags.cpp)
    /// @orig 0x00415b68 Flag_New (flags.cpp)
    /// @orig 0x00415af0 FlagPool_Take (flags.cpp)
    /// @orig 0x00415e70 Flag_Construct (flags.cpp)
    const WorldFlag& add(double handle, std::string_view name, const std::array<float, 3>& position,
                         float headingDegrees, int kind = 0, int kind2 = 0);

    /// The flag with `handle`; null when no flag of this level has it.
    [[nodiscard]] const WorldFlag* find(double handle) const;
    /// The same, for changing it (a parent, the enabled flag).
    [[nodiscard]] WorldFlag* find(double handle);

    /// The handle of the first flag of the current pool, in creation order, whose stored name equals `name` exactly
    /// (case-sensitive): a name longer than kNameLength never matches. Nothing when none does.
    /// @orig 0x00415c48 Flag_FindByName (flags.cpp)
    [[nodiscard]] std::optional<double> findByName(std::string_view name) const;

    /// Where `flag` is: its parent's position when the parent resolves through `locate` to a live object, else its
    /// own. A null `locate` resolves nothing.
    /// @orig 0x004161e8 Flag_Position (flags.cpp)
    [[nodiscard]] static std::array<float, 3> position(const WorldFlag& flag, const ObjectLocator& locate);
    /// Which way `flag` faces, in degrees: its parent's heading when the parent is live, else its own.
    /// @orig 0x00416258 Flag_Heading (flags.cpp)
    [[nodiscard]] static float headingDegrees(const WorldFlag& flag, const ObjectLocator& locate);

    /// Forgets every flag and the pool: the level's unload.
    /// @orig 0x00415a38 FlagPool_Destroy (flags.cpp)
    void clear();

    /// Every flag made in the level, oldest first.
    [[nodiscard]] const std::vector<WorldFlag>& all() const { return m_flags; }
    /// Records the current pool holds (0 before `CfgSetDatabaseSizes`).
    [[nodiscard]] std::size_t capacity() const { return m_capacity; }
    /// Flags made in the current pool.
    [[nodiscard]] std::size_t poolCount() const { return m_flags.size() - m_poolStart; }
    /// Flags made past the current pool's capacity.
    [[nodiscard]] std::size_t overflows() const { return m_overflows; }

  private:
    std::vector<WorldFlag> m_flags; // every flag of the level, in creation order
    std::size_t m_poolStart = 0;    // the first flag of the current pool in m_flags
    std::size_t m_capacity = 0;
    std::size_t m_overflows = 0;
};

} // namespace coney::world_objects
