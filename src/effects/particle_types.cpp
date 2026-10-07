// SPDX-License-Identifier: GPL-3.0-or-later
#include "effects/particle_types.h"

#include <algorithm>
#include <array>

namespace coney::effects {

namespace {

using enum ParticleSheet;
using enum ParticleBehaviour;

// Coney's stand-in tints, `0xRRGGBBAA`: the page gives no type's colour.
constexpr std::uint32_t kWhite = 0xFFFFFFFFU;
constexpr std::uint32_t kBlood = 0x800808E0U;
constexpr std::uint32_t kSpark = 0xFFE8A0FFU;
constexpr std::uint32_t kSmoke = 0xA0A0A0A0U;
constexpr std::uint32_t kWarmGlow = 0xFFD890C0U;
constexpr std::uint32_t kPoliceGlow = 0xFF3020C0U;
constexpr std::uint32_t kGlass = 0xC8E0F0B0U;
constexpr std::uint32_t kStrobeRed = 0xFF0000FFU;
// The neon signs' light colours (docs/research/script-types.md#neon-signs).
constexpr std::uint32_t kNeonOrange = 0xffe3aeffU;
constexpr std::uint32_t kNeonPink = 0xffafcfffU;
// A fly's grey and its drawn size, twice its `+0xc0` of 0.02 (docs/research/script-types.md#flies).
constexpr std::uint32_t kFlyGrey = 0x808080ffU;
constexpr float kFlySize = 0.04F;

// The types Coney knows, sorted by name (findParticleType() searches it in halves). Sheets and rectangles are the
// research's traced sprites (docs/references/particles.md); behaviours, sizes and colours are Coney's stand-ins, but
// for the explosion family (`part_explosion`, `sub_explode`, `sub_fireball`, `sub_debris`, `sub_explosion_embers`),
// whose recipe is traced (docs/research/script-types.md#part-explosion; their motion is in particles.cpp). Types whose
// sprite is traced to a sheet Coney does not load yet (the unnamed flame sheets, `part_tv`), to the doubtful rectangle
// 54, or that draw on the HUD, are left out and so make Inert systems. `part_steam` draws its `sub_smoke` puffs,
// `part_page1` rectangles 42-44 (docs/research/particles.md#steam); **Coney's stand-in**: the large and huge vents
// draw theirs the same way (how their puffs differ is not read).
constexpr std::array kTypes{
    ParticleType{"blo_splat", PartPage1, 5, Spray, kBlood, 0.12F},
    ParticleType{"blood_drop", PartPage1, 52, Spray, kBlood, 0.05F},
    ParticleType{"blood_spray", PartPage1, 52, Spray, kBlood, 0.06F},
    ParticleType{"bloosh", PartPage1, 46, Puff, kBlood, 0.25F},
    ParticleType{"coplights_glow", Lighting, 2, Glow, kPoliceGlow, 1.2F},
    ParticleType{"coplights_lens_flare", Lighting, 2, Glow, kPoliceGlow, 1.6F},
    ParticleType{"glasstest", None, 0, Shard, kGlass, 0.06F},
    ParticleType{"part_copcar_lights", Lighting, 2, Glow, kPoliceGlow, 1.2F},
    ParticleType{"part_explosion", PartPage1, 6, Explosion, kWhite, 0.0F},
    ParticleType{"part_fire", PartFire, 0, Flames, kWhite, 0.6F},
    ParticleType{"part_fire_large", PartFire, 0, Flames, kWhite, 1.2F},
    ParticleType{"part_fire_large_ns", PartFire, 0, Flames, kWhite, 1.2F},
    ParticleType{"part_fire_ns", PartFire, 0, Flames, kWhite, 0.6F},
    ParticleType{"part_fire_tiki", PartFire, 0, Flames, kWhite, 0.35F},
    ParticleType{"part_firebarrel", PartFire, 0, Flames, kWhite, 0.7F},
    ParticleType{"part_firebarrel_ns", PartFire, 0, Flames, kWhite, 0.7F},
    ParticleType{"part_firetruck_lights", Lighting, 2, Glow, kPoliceGlow, 1.2F},
    ParticleType{"part_garbage_flies", PartPage1, 19, Flies, kFlyGrey, kFlySize},
    ParticleType{"part_garbage_flies_ns", PartPage1, 19, Flies, kFlyGrey, kFlySize},
    ParticleType{"part_gun_flash", Lighting, 2, Flash, kWarmGlow, 0.8F},
    ParticleType{"part_light_bugs", PartPage1, 19, Flies, kFlyGrey, kFlySize},
    ParticleType{"part_orange_neon", None, 0, Neon, kNeonOrange, 0.0F},
    ParticleType{"part_pink_neon", None, 0, Neon, kNeonPink, 0.0F},
    ParticleType{"part_s_fire", PartFire, 0, Flames, kWhite, 0.6F},
    ParticleType{"part_steam", PartPage1, 42, Steam, kSmoke, 0.25F},
    ParticleType{"part_steam_huge", PartPage1, 42, Steam, kSmoke, 2.5F},
    ParticleType{"part_steam_large", PartPage1, 42, Steam, kSmoke, 0.75F},
    ParticleType{"part_strobe_red", None, 0, Strobe, kStrobeRed, 0.0F},
    ParticleType{"part_torch_flame", PartFire, 0, Flames, kWhite, 0.3F},
    ParticleType{"part_torch_flame_ns", PartFire, 0, Flames, kWhite, 0.3F},
    ParticleType{"spark", PartPage1, 41, Sparks, kSpark, 0.08F},
    ParticleType{"sub_anim_spark", PartPage1, 50, Sparks, kSpark, 0.06F},
    ParticleType{"sub_barlamp_glow", Lighting, 3, Glow, kWarmGlow, 1.0F},
    ParticleType{"sub_blight_glow", Lighting, 3, Glow, kWarmGlow, 1.0F},
    ParticleType{"sub_blood_gout", PartPage1, 2, Spray, kBlood, 0.1F},
    ParticleType{"sub_blood_spray", PartPage1, 6, Spray, kBlood, 0.15F},
    ParticleType{"sub_car_sparks", PartPage1, 45, Sparks, kSpark, 0.1F},
    ParticleType{"sub_debris", PartPage1, 8, Debris, kWhite, 0.6F},
    ParticleType{"sub_embers", PartPage1, 20, Sparks, kSpark, 0.1F},
    ParticleType{"sub_explode", PartPage1, 17, Explode, kWhite, 0.4F},
    ParticleType{"sub_explosion_embers", PartFire, 0, Embers, kWhite, 0.2F},
    ParticleType{"sub_fade_flame", PartFire, 0, Flames, kWhite, 0.4F},
    ParticleType{"sub_fire_smoke", PartPage1, 42, Puff, kSmoke, 0.8F},
    ParticleType{"sub_fireball", PartPage1, 42, Fireball, kWhite, 3.2F},
    ParticleType{"sub_fireball_emitter", PartPage1, 42, Explosion, kWhite, 0.0F},
    ParticleType{"sub_glint", PartPage1, 41, Flash, kWhite, 0.3F},
    ParticleType{"sub_muzzle_flash", PartPage1, 35, Flash, kWarmGlow, 0.3F},
    ParticleType{"sub_objective_glow", Lighting, 3, Glow, kWarmGlow, 1.0F},
    ParticleType{"sub_shack_puff", PartPage1, 42, Puff, kSmoke, 0.6F},
    ParticleType{"sub_shack_puff_aligned", PartPage1, 42, Puff, kSmoke, 0.6F},
    ParticleType{"sub_thrown_dust_puff", PartPage1, 42, Puff, kSmoke, 0.5F},
};

static_assert(std::ranges::is_sorted(kTypes, {}, &ParticleType::name), "kTypes must be sorted by name");

// The type every unknown name makes.
constexpr ParticleType kInert{"", None, 0, Inert, kWhite, 0.0F};

} // namespace

std::string_view sheetName(ParticleSheet sheet) {
    switch (sheet) {
    case None:
        return {};
    case PartPage0:
        return "part_page0";
    case PartPage1:
        return "part_page1";
    case PartFire:
        return "part_fire";
    case Lighting:
        return "lighting";
    case PartFog00:
        return "part_fog_00";
    case PartFog01:
        return "part_fog_01";
    }
    return {};
}

std::span<const ParticleType> particleTypes() { return kTypes; }

const ParticleType* findParticleType(std::string_view name) {
    const auto found = std::ranges::lower_bound(kTypes, name, {}, &ParticleType::name);
    return found != kTypes.end() && found->name == name ? &*found : nullptr;
}

const ParticleType& inertType() { return kInert; }

} // namespace coney::effects
