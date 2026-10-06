// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

// The particle system types Coney draws: what the original's script type table names (docs/research/particles.md)
// and what Coney makes of each. The table's types are compiled code whose motion is not on the page, so every
// behaviour here is Coney's stand-in; the sprite (sheet and rectangle) is the research's where it is traced.

namespace coney::effects {

/// The sprite sheets particle systems draw from, by resource name (docs/research/particles.md#sprite-words).
enum class ParticleSheet : std::uint8_t {
    None,      ///< No texture: a flat-coloured quad (a glass shard, whose sprite is not traced).
    PartPage0, ///< `part_page0`, sheet record 0.
    PartPage1, ///< `part_page1`, sheet record 1.
    PartFire,  ///< `part_fire`, sheet record 7: the flames' 36 rectangles.
    Lighting,  ///< `lighting`, sheet record 8: the glows.
};

/// The resource name of `sheet` (empty for ParticleSheet::None).
[[nodiscard]] std::string_view sheetName(ParticleSheet sheet);

/// How a system of a type behaves. **Coney's stand-in** for each type's update code, which the research has not
/// traced: one behaviour per family of names.
enum class ParticleBehaviour : std::uint8_t {
    Inert,  ///< Exists (its handle answers) but draws nothing: a type whose sprite is not traced, or a sound emitter.
    Glow,   ///< One sprite at the system, for as long as it lives (the `lighting` glows).
    Flash,  ///< One sprite for a moment, then the system ends (a gun's muzzle flash).
    Flames, ///< A steady stream of rising sprites stepping through the flame sheet's rectangles (the fire types).
    Puff,   ///< A few rising, growing, fading sprites, then the system ends (dust and smoke puffs).
    Spray,  ///< A burst of drops thrown along the system's facing and pulled down, then the system ends (blood).
    Sparks, ///< A burst of quick, bright sprites thrown along the system's facing and pulled down (sparks).
    Shard,  ///< One falling, spinning shard in the creator's colour (a glass shard, `glasstest`).
};

/// One particle system type Coney knows.
struct ParticleType {
    std::string_view name; ///< The script type table's name.
    ParticleSheet sheet = ParticleSheet::None;
    std::uint16_t rect = 0; ///< The first rectangle (docs/references/particles.md).
    ParticleBehaviour behaviour = ParticleBehaviour::Inert;
    std::uint32_t colour = 0xFFFFFFFFU; ///< `0xRRGGBBAA`; Coney's stand-in tint.
    float size = 0.25F; ///< Metres across. **Coney's stand-in**: the page gives only the sprite's pixels.
};

/// Every type Coney knows, sorted by name. A name that is not here makes an Inert system (inertType()).
[[nodiscard]] std::span<const ParticleType> particleTypes();

/// The type named `name` (exact spelling, as the original's `strcmp`); null when Coney does not know it.
/// @orig 0x003c55e8 ScriptType_Find (unknown)
[[nodiscard]] const ParticleType* findParticleType(std::string_view name);

/// What a name Coney does not know makes: an Inert system. **Coney's choice**: what the original does with a name
/// its table lacks (`part_ominoussmoke`) is not traced (docs/research/particles.md#spawning).
[[nodiscard]] const ParticleType& inertType();

} // namespace coney::effects
