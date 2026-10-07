// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "animation/anim_math.h"
#include "effects/light_tasks.h"
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
    /// A steam puff's own update (`sub_smoke`, docs/research/particles.md#steam); unset for every other sprite.
    struct SteamPuff {
        anim::Vec3 start;            ///< Its start velocity, metres a second.
        float dragH = 0.0F;          ///< The share of the start velocity lost by the end of the life, across ...
        float dragV = 0.0F;          ///< ... and up.
        float growth = 0.0F;         ///< Size gained per update (× 0.8-1.2 each time).
        float interval = 1.0F;       ///< Seconds between its updates (`puffInterval` frames).
        float due = 0.0F;            ///< Seconds until its next update.
        std::uint32_t age = 0;       ///< Updates done.
        std::uint32_t life = 1;      ///< Updates it lives.
        std::uint8_t startAlpha = 0; ///< The colour's alpha, which fades out over the life.
    };
    std::optional<SteamPuff> steam;
    /// A sprite that steps through a table of stages (`sub_explode`, `sub_fireball`,
    /// docs/research/script-types.md#part-explosion): over each stage its size and colour move from the last stage's
    /// to the stage's own; it ends after the last. Unset for every other sprite.
    struct Stages {
        static constexpr std::size_t kMost = 7;
        std::array<float, kMost> seconds{};         ///< Each stage's length.
        std::array<float, kMost> sizes{};           ///< The size (metres across) each stage ends at.
        std::array<std::uint32_t, kMost> colours{}; ///< The colour `0xRRGGBBAA` each stage ends at.
        std::size_t count = 0;                      ///< Stages in use.
        std::size_t at = 0;                         ///< The stage it is in.
        float inStage = 0.0F;                       ///< Seconds into it.
        float fromSize = 0.0F;                      ///< The size it started the stage at.
        std::uint32_t fromColour = 0;               ///< The colour it started the stage at.
    };
    std::optional<Stages> stages;
    /// A fly (`sub_polar_bugs`, docs/research/script-types.md#flies): it jumps to a new point on its two angles round
    /// its pile at each update. Unset for every other sprite.
    struct Fly {
        float a = 0.0F;           ///< The angle from the pile's up axis, radians.
        float b = 0.0F;           ///< The angle round it, radians.
        float due = 0.0F;         ///< Seconds until its next update.
        float interval = 0.0F;    ///< Seconds between its last update and its next.
        bool grown = false;       ///< Its first update is done: it has faded and grown in.
        std::uint32_t colour = 0; ///< Its full colour, `0xRRGGBBAA`.
        float size = 0.0F;        ///< Its full size, metres across.
    };
    std::optional<Fly> fly;
};

/// A steam vent's configuration: `CfgSteam(object, colour, interval, puffInterval, size, growth, life, speed, rise,
/// dragH, dragV, still)` (docs/references/bindings/config.md#cfgsteam), message `0x27` to a `part_steam` emitter.
struct SteamSettings {
    std::uint32_t colour = 0xFFFFFFFFU; ///< `0xRRGGBBAA`; the alpha is a puff's opacity at birth, fading to 0.
    std::uint32_t interval = 1;         ///< Frames (60 a second) between puffs while the vent is near.
    std::uint32_t puffInterval = 1;     ///< Frames between a puff's updates; never 0.
    float size = 0.0F;                  ///< A puff's size at birth (× 0.8-1.2 at random).
    float growth = 0.0F;                ///< Its growth per puff update (× 0.8-1.2 at random).
    float life = 0.0F;                  ///< Its life, seconds.
    float speed = 0.0F;                 ///< Its speed along the vent's −x axis, metres a second.
    float rise = 0.0F;                  ///< Its rise, metres a second (× 0.8-1.2).
    float dragH = 0.0F;                 ///< Particle::SteamPuff::dragH.
    float dragV = 0.0F;                 ///< Particle::SteamPuff::dragV.
    bool still = false;                 ///< Not blown by the global vector at `0x006f31a0` (inferred: wind).
};

/// A steam vent's settings before any `CfgSteam` (its init's built-in values, docs/research/particles.md#steam), for
/// `part_steam`, `part_steam_large` and `part_steam_huge`; none for another type.
/// @orig 0x003f69b8 PartSteam_Init (unknown)
/// @orig 0x003f7138 PartSteamLarge_Init (unknown)
/// @orig 0x003f6d70 PartSteamHuge_Init (unknown)
[[nodiscard]] std::optional<SteamSettings> defaultSteam(std::string_view typeName);

/// The light a system's light type gives now (an alarm strobe's `strober`, a neon sign's `sub_neon_light`): a point
/// light with no corona at the system, lighting objects and humans, and the world when `lightsWorld`
/// (docs/research/script-types.md#light-type-lights).
struct SystemLight {
    anim::Vec3 position;
    LightColourNow colour; ///< Each channel 0-1.
    float radius = 0.0F;   ///< Metres; the falloff is linear.
    bool lightsWorld = false;
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
    std::optional<SteamSettings> steam; ///< A steam vent's settings: defaultSteam(), then `CfgSteam`'s.
    std::optional<LightTask> light;     ///< Its light type's task: a strobe's `strober`, a neon's light.
    float lightTicks = 0.0F;            ///< Ticks (1/60 s) its light has yet to run.
    std::uint32_t serial = 0;           ///< Its spawn's number, unique while the systems live (for its light).
    std::vector<Particle> particles;
};

/// The light `system`'s light type gives now: an alarm strobe's from its switching on (message `0x12`) until the
/// update after it is switched off, a neon sign's always (docs/research/script-types.md#strober, #neon-signs); none
/// for another type.
[[nodiscard]] std::optional<SystemLight> systemLight(const ParticleSystem& system);

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
    /// Ends every system that follows the object `parent` (a car's lights when the car goes).
    void killFollowing(double parent);
    /// Hides or shows the system `handle` names (messages 0x29 and 0x2a); an unknown handle is ignored.
    void setHidden(double handle, bool hidden);

    /// `StartParticle` / `EndParticle` (messages `0x12` and `0x13`): the system `handle` names starts or stops making
    /// sprites; those in flight live out their life (**Coney choice**: each type's own answer is not traced). False
    /// when no system has the handle.
    /// @orig 0x003975c0 Particle_Start (unknown)
    /// @orig 0x00397610 Particle_End (unknown)
    bool setEmitting(double handle, bool on);

    /// The system nearest `at` whose type name contains `part` (a substring test, as the alarm's `strstr`) within
    /// `within` metres; null for none.
    [[nodiscard]] ParticleSystem* nearestNamed(std::string_view part, anim::Vec3 at, float within);
    /// Message `0x12` or `0x13` to `system`: on or off (a stream's sprites). An alarm strobe switched on makes a new
    /// `strober` (`PartStrobeRed_OnMessage`); switched off, its `strober`'s on flag is cleared and its next update ends
    /// it.
    static void setEmitting(ParticleSystem& system, bool on);

    /// `CfgSteam`: the steam vent `handle` names is configured (a `puffInterval` of 0 is taken as 1: the original
    /// divides by it). False when no system has the handle; **Coney choice**: the original does not check the type,
    /// and a system of another type keeps the settings unused.
    /// @orig 0x0039be28 Steam_Configure (unknown)
    /// @orig 0x003f6b40 Steam_HandleMessage (unknown)
    bool configureSteam(double handle, const SteamSettings& settings);
    /// Where the camera is, for the steam vents' near test (none: no vent puffs).
    void setViewer(std::optional<anim::Vec3> viewer) { m_viewer = viewer; }
    /// Whether a point is no more than a margin (metres) outside a player view's frustum (`Cameras_IsPointVisibleAny`).
    using ViewTest = std::function<bool(anim::Vec3 point, float margin)>;
    /// The view test the fly piles use (empty: no view, so no flies).
    void setViewTest(ViewTest test) { m_viewTest = std::move(test); }

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
    // A steam vent's puffs for the step: one every SteamSettings::interval frames while it is on and near, every 60
    // frames while it is far.
    void stepSteam(ParticleSystem& system, float seconds);
    // One update of a steam puff (`0x003f6460`): its velocity dragged, its size grown, its alpha faded.
    void updatePuff(Particle& puff);
    // A fly pile's update: every 60 ticks while a view is within 50 m it keeps three flies, otherwise every 120 it
    // has none.
    // @orig 0x003d7f10 PartGarbageFlies_Update (unknown)
    void stepFlyPile(ParticleSystem& system, float seconds);
    // A new fly of `system` on random angles, faded and shrunk to nothing until its first update.
    // @orig 0x003e07a0 SubPolarBugs_Init (unknown)
    [[nodiscard]] Particle makeFly(const ParticleSystem& system);
    // A fly's update: on round its angles to its next point, the next update in 20 ticks or (2 in 11) 10.
    // @orig 0x003e0980 SubPolarBugs_Update (unknown)
    void updateFly(Particle& fly, anim::Vec3 centre);
    // A fly's step: its updates as they come round, and its fade and growth until the first.
    void stepFly(Particle& fly, anim::Vec3 centre, float seconds);

    // A system of `typeName` at `at` holding `particle` alone, made at once; false when the pool or the budget is full.
    bool spawnSprite(std::string_view typeName, anim::Vec3 at, anim::Quat rotation, const Particle& particle);
    // `part_explosion`'s init: its embers, its fireball emitter's six fireballs and its debris, round `at`.
    // @orig 0x003c3448 PartExplosion_Init (unknown)
    void spawnExplosionParts(anim::Vec3 at, anim::Quat rotation);
    // `sub_fireball_emitter`'s init: six fireballs from `at`, one along each of ±x, ±y, ±z of `rotation`.
    // @orig 0x003c4580 SubFireballEmitter_Init (unknown)
    void spawnFireballs(anim::Vec3 at, anim::Quat rotation);
    // One `sub_fireball` from `at` along the unit `direction`, at the emitter's scale.
    // @orig 0x003c4a58 SubFireball_Init (unknown)
    [[nodiscard]] Particle makeFireball(anim::Vec3 at, anim::Vec3 direction);
    // Moves a staged sprite through its stages by `seconds`; false once it is past its last.
    // @orig 0x003c54b8 SubExplode_Update (unknown)
    // @orig 0x003c4c38 SubFireball_Update (unknown)
    static bool stepStages(Particle& particle, float seconds);

    std::uint32_t m_random;
    std::uint32_t m_serial = 0; // the last spawn's number
    Locator m_locator;
    std::optional<anim::Vec3> m_viewer;
    ViewTest m_viewTest;
    // Systems a step asked for (a `sub_explode`'s `part_explosion`), spawned once the step is over.
    std::vector<std::pair<anim::Vec3, anim::Quat>> m_pendingExplosions;
};

} // namespace coney::effects
