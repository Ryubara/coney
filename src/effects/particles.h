// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

#include "animation/anim_math.h"
#include "effects/particle_types.h"

namespace coney::effects {

/// One sprite of a particle system, in the game's axes (z up).
struct Particle {
    anim::Vec3 position;
    anim::Vec3 velocity;                ///< Metres a second.
    float age = 0.0F;                   ///< Seconds since it was made.
    float life = 1.0F;                  ///< Seconds it lives.
    float size = 0.25F;                 ///< Metres across now.
    float grow = 0.0F;                  ///< Metres across gained a second.
    float angle = 0.0F;                 ///< Its turn on the screen, radians.
    float spin = 0.0F;                  ///< Radians a second.
    float gravity = 0.0F;               ///< Metres a second², pulling down.
    std::uint32_t colour = 0xFFFFFFFFU; ///< `0xRRGGBBAA` at birth; the alpha fades to 0 over the life when `fades`.
    std::uint16_t rect = 0;             ///< The rectangle of the system's sheet it shows.
    bool fades = true;
};

/// One live particle system: the original's particle task (docs/research/particles.md#task-fields), with the sprites
/// it has made.
struct ParticleSystem {
    double handle = 0;                  ///< Its script handle; 0 for one the engine made (an impact's).
    const ParticleType* type = nullptr; ///< Never null.
    anim::Vec3 position;                ///< `+0x10`.
    anim::Quat rotation;                ///< `+0x20`.
    double parent = 0;                  ///< The object it follows; 0 for none.
    anim::Vec3 offset;                  ///< From the parent, kept at the spawn.
    std::uint32_t colour = 0xFFFFFFFFU; ///< `+0xb0`, `0xRRGGBBAA`.
    std::uint16_t rect = 0;             ///< The low half of the sprite word `+0xc4`.
    bool hidden = false;                ///< `+0x54` bit 0x04: drawn not.
    bool emitting = true;               ///< On (`StartParticle`) or off (`EndParticle`): a stream makes sprites.
    float age = 0.0F;                   ///< Seconds since its spawn.
    float emitDue = 0.0F;               ///< Seconds until a stream type makes its next sprite.
    bool started = false;               ///< Whether its first step (a burst's emission) has run.
    std::vector<Particle> particles;
};

/// The particle manager: the pool of particle systems scripts and the engine spawn by type name, each stepped on the
/// fixed step and drawn as sprites by the platform (src/platform/particle_renderer.h).
///
/// Spawning follows `Particle_Spawn` (docs/research/particles.md#spawning): the position (w = 1), rotation and parent
/// a system starts with, a type found by name, its handle. **Coney's stand-ins** where the page is silent: every
/// type's motion (ParticleBehaviour), the sprites' world sizes and tints, the pool of sprites (kParticleBudget), what
/// an unknown name makes (an Inert system), and an attached system keeping its offset from its parent. Random numbers
/// come from the manager's own generator (xorshift32 from a fixed seed), so a run is the same every time.
///
/// Research: docs/research/particles.md
class ParticleSystems {
  public:
    /// Particle tasks in play (docs/research/tasks.md#classes).
    static constexpr std::size_t kSystemPool = 1400;
    /// **Coney's stand-in** for the particle budget (`0x003a5a50`, not traced): sprites alive at once.
    static constexpr std::size_t kParticleBudget = 4096;
    /// The generator's seed unless one is given.
    static constexpr std::uint32_t kDefaultSeed = 0x2545F491U;

    /// Where a parent object is now (game axes), or nothing when it is gone.
    using Locator = std::function<std::optional<anim::Vec3>(double handle)>;

    explicit ParticleSystems(std::uint32_t seed = kDefaultSeed) : m_random(seed != 0 ? seed : kDefaultSeed) {}

    /// Finds attached systems' parents through `locator` (empty: attached systems stay where they were spawned).
    void setLocator(Locator locator) { m_locator = std::move(locator); }

    /// `Particle_Spawn`: a system of type `typeName` at `position` turned by `rotation`, following `parent` (0 for
    /// none), named by `handle` (0 for an engine effect). `colour` and `rect` are what a creator passes the types
    /// that take them (a shard's colour, a spark's rectangle); unset keeps the type's. Returns the system (valid until
    /// the next spawn or step), or null when the pool is full.
    /// @orig 0x0039bfb0 Particle_Spawn (unknown)
    ParticleSystem* spawn(std::string_view typeName, anim::Vec3 position, anim::Quat rotation = {}, double parent = 0,
                          double handle = 0, std::optional<std::uint32_t> colour = std::nullopt,
                          std::optional<std::uint16_t> rect = std::nullopt);

    /// Blood thrown from a hit at `at` along `direction` (game axes): a `blood_spray` system. For combat; which hits
    /// bleed is the caller's choice (docs/research/combat.md does not say).
    ParticleSystem* spawnBlood(anim::Vec3 at, anim::Vec3 direction);
    /// Sparks from a hit at `at` along `direction`: a `spark` system.
    ParticleSystem* spawnSparks(anim::Vec3 at, anim::Vec3 direction);

    /// One `glasstest` shard of a breaking pane at `at`, `size` metres across, in the pane's colour word (the shatter
    /// itself, its count and its tries, is the pane's: docs/research/objects.md#shatter).
    ParticleSystem* spawnShard(anim::Vec3 at, float size, std::uint32_t colour);
    /// Whether `count` more sprites fit in the budget (kParticleBudget), as the shatter asks (`0x003a5a50`).
    [[nodiscard]] bool hasRoom(std::size_t count) const { return m_particles + count <= kParticleBudget; }

    /// Ends the system `handle` names; false when none does.
    bool kill(double handle);
    /// Hides or shows the system `handle` names (messages 0x29 and 0x2a); an unknown handle is ignored.
    void setHidden(double handle, bool hidden);

    /// `StartParticle` / `EndParticle` (messages `0x12` and `0x13`): the system `handle` names starts or stops making
    /// sprites; those in flight live out their life (**Coney choice**: each type's own answer is not traced). False
    /// when no system has the handle.
    /// @orig 0x003975c0 Particle_Start (unknown)
    /// @orig 0x00397610 Particle_End (unknown)
    bool setEmitting(double handle, bool on);

    /// One step of `seconds`: each system follows its parent, makes and moves its sprites, and ends when its type's
    /// behaviour says so.
    void step(float seconds);

    /// The system `handle` names; null when none does (or it ended).
    [[nodiscard]] const ParticleSystem* find(double handle) const;
    /// The live systems, oldest first.
    [[nodiscard]] const std::vector<ParticleSystem>& systems() const { return m_systems; }
    /// Sprites alive in every system.
    [[nodiscard]] std::size_t particleCount() const { return m_particles; }
    /// Ends every system: the level is unloaded.
    void clear();

    /// A whole number in [0, n] from the generator.
    [[nodiscard]] std::uint32_t draw(std::uint32_t n);
    /// A number in [0, 1).
    [[nodiscard]] float unit();

  private:
    // Makes `count` sprites of `system`'s type, from its behaviour.
    void emit(ParticleSystem& system, std::size_t count);
    // One sprite of `system` at its position, its other fields from the behaviour.
    [[nodiscard]] Particle makeParticle(ParticleSystem& system);
    // The step of one system; false when it has ended.
    bool stepSystem(ParticleSystem& system, float seconds);

    std::vector<ParticleSystem> m_systems;
    std::size_t m_particles = 0;
    std::uint32_t m_random;
    Locator m_locator;
};

} // namespace coney::effects
