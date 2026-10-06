// SPDX-License-Identifier: GPL-3.0-or-later
#include "effects/particles.h"

#include "core/assert.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace coney::effects {

namespace {

// Coney's stand-ins for the types' motion (the page traces none of it), by behaviour.
// Gravity on falling sprites: the world objects' (docs/research/physics.md#movers), as nothing else is known.
constexpr float kGravity = 15.68F;
// Flames: one sprite every this many seconds, each living kFlameLife and rising at kFlameRise, stepping through the
// flame sheet's 36 rectangles over its life (docs/research/particles.md#sprite-words).
constexpr float kFlameInterval = 0.08F;
constexpr float kFlameLife = 0.6F;
constexpr float kFlameRise = 1.2F;
constexpr std::uint16_t kFlameFrames = 36;
// Puffs: how many, how long, how fast they rise and how much they grow (times their size, a second).
constexpr std::size_t kPuffCount = 3;
constexpr float kPuffLife = 1.2F;
constexpr float kPuffRise = 0.5F;
constexpr float kPuffGrowth = 0.8F;
// Sprays and sparks: how many, how long, and how fast they leave.
constexpr std::size_t kSprayCount = 12;
constexpr float kSprayLife = 0.6F;
constexpr float kSpraySpeed = 2.5F;
constexpr std::size_t kSparkCount = 8;
constexpr float kSparkLife = 0.35F;
constexpr float kSparkSpeed = 4.0F;
// A flash's life, and a shard's life and fastest spin (radians a second).
constexpr float kFlashLife = 0.1F;
constexpr float kShardLife = 1.5F;
constexpr float kShardSpin = 8.0F;
// Steam (docs/research/particles.md#steam): the game's frames a second; a vent is near within 30 m and 20 m (the tests
// `0x003a5280` and `0x003a51f8`, **Coney's reading**: both of the camera, so within 20 m) and puffs every 60 frames
// when far; the random spreads of a puff's rise, size and growth (0.8-1.2) and of each update's speed (0.75-1.15);
// its rectangle, 42 plus 0-2.
constexpr float kFramesPerSecond = 60.0F;
constexpr float kSteamNear = 20.0F;
constexpr float kSteamFarFrames = 60.0F;
constexpr float kSteamSpreadMin = 0.8F;
constexpr float kSteamSpread = 0.4F;
constexpr float kPuffSpeedMin = 0.75F;
constexpr float kPuffSpeedSpread = 0.4F;
constexpr std::uint16_t kSmokeRect = 42;
// Whether a system of `behaviour` ends once its sprites are gone (a burst), rather than living until it is killed.
bool endsWhenEmpty(ParticleBehaviour behaviour) {
    switch (behaviour) {
    case ParticleBehaviour::Flash:
    case ParticleBehaviour::Puff:
    case ParticleBehaviour::Spray:
    case ParticleBehaviour::Sparks:
    case ParticleBehaviour::Shard:
        return true;
    case ParticleBehaviour::Inert:
    case ParticleBehaviour::Glow:
    case ParticleBehaviour::Flames:
    case ParticleBehaviour::Steam:
        return false;
    }
    return false;
}

// The direction a system faces: its rotation applied to the models' forward, +y.
anim::Vec3 facing(anim::Quat rotation) {
    return anim::transformDirection(anim::matrixFromQuat(anim::normalise(rotation)), anim::Vec3{0.0F, 1.0F, 0.0F});
}

// A rotation that turns +y onto `direction` (unit or not), about the axis between them; none for a zero direction.
anim::Quat rotationTowards(anim::Vec3 direction) {
    const float length = anim::length(direction);
    if (length <= 0.0F) {
        return {};
    }
    const anim::Vec3 to = anim::scale(direction, 1.0F / length);
    const anim::Vec3 from{0.0F, 1.0F, 0.0F};
    const float d = anim::dot(from, to);
    if (d < -0.9999F) {
        return anim::Quat{0.0F, 0.0F, 1.0F, 0.0F}; // half a turn about z
    }
    const anim::Vec3 axis = anim::cross(from, to);
    return anim::normalise(anim::Quat{axis.x, axis.y, axis.z, 1.0F + d});
}

} // namespace

std::uint32_t ParticleSystems::draw(std::uint32_t n) {
    // xorshift32: Coney's own generator, deterministic from its seed.
    m_random ^= m_random << 13U;
    m_random ^= m_random >> 17U;
    m_random ^= m_random << 5U;
    return m_random % (n + 1U);
}

float ParticleSystems::unit() {
    constexpr std::uint32_t kSteps = 1U << 16U;
    return static_cast<float>(draw(kSteps - 1U)) / static_cast<float>(kSteps);
}

ParticleSystem* ParticleSystems::spawn(std::string_view typeName, anim::Vec3 position, anim::Quat rotation,
                                       double parent, double handle, std::optional<std::uint32_t> colour,
                                       std::optional<std::uint16_t> rect) {
    if (m_systems.size() >= kSystemPool) {
        return nullptr;
    }
    const ParticleType* type = findParticleType(typeName);
    ParticleSystem& system = m_systems.emplace_back();
    system.handle = handle;
    system.type = type != nullptr ? type : &inertType();
    system.position = position;
    system.rotation = rotation;
    system.parent = parent;
    system.colour = colour.value_or(system.type->colour);
    system.rect = rect.value_or(system.type->rect);
    // `sub_shack_puff` takes rectangle 42 plus a random 0-1 (docs/research/particles.md#sprite-words).
    if (!rect && system.type->name.starts_with("sub_shack_puff")) {
        system.rect = static_cast<std::uint16_t>(system.rect + draw(1));
    }
    if (parent != 0 && m_locator) {
        if (const std::optional<anim::Vec3> at = m_locator(parent)) {
            system.offset = anim::subtract(position, *at);
        }
    }
    // The muzzle flash makes a puff of smoke where it fires (its Spawns link, docs/references/particles.md).
    const std::size_t index = m_systems.size() - 1;
    if (system.type->name == "part_gun_flash") {
        spawn("sub_shack_puff", position, rotation);
    }
    return &m_systems[index];
}

ParticleSystem* ParticleSystems::spawnBlood(anim::Vec3 at, anim::Vec3 direction) {
    return spawn("blood_spray", at, rotationTowards(direction));
}

ParticleSystem* ParticleSystems::spawnSparks(anim::Vec3 at, anim::Vec3 direction) {
    return spawn("spark", at, rotationTowards(direction));
}

ParticleSystem* ParticleSystems::spawnShard(anim::Vec3 at, float size, std::uint32_t colour) {
    ParticleSystem* shard = spawn("glasstest", at, {}, 0, 0, colour);
    if (shard == nullptr || m_particles >= kParticleBudget) {
        return shard;
    }
    // The shard is made at once, at the size the pane gives it.
    Particle particle = makeParticle(*shard);
    particle.size = size;
    shard->particles.push_back(particle);
    shard->started = true;
    ++m_particles;
    return shard;
}

bool ParticleSystems::kill(double handle) {
    const auto found = std::ranges::find(m_systems, handle, &ParticleSystem::handle);
    if (handle == 0 || found == m_systems.end()) {
        return false;
    }
    m_particles -= found->particles.size();
    m_systems.erase(found);
    return true;
}

void ParticleSystems::killFollowing(double parent) {
    if (parent == 0) {
        return;
    }
    std::erase_if(m_systems, [this, parent](const ParticleSystem& system) {
        if (system.parent != parent) {
            return false;
        }
        m_particles -= system.particles.size();
        return true;
    });
}

void ParticleSystems::setHidden(double handle, bool hidden) {
    const auto found = std::ranges::find(m_systems, handle, &ParticleSystem::handle);
    if (handle != 0 && found != m_systems.end()) {
        found->hidden = hidden;
    }
}

const ParticleSystem* ParticleSystems::find(double handle) const {
    const auto found = std::ranges::find(m_systems, handle, &ParticleSystem::handle);
    return handle != 0 && found != m_systems.end() ? &*found : nullptr;
}

void ParticleSystems::clear() {
    m_systems.clear();
    m_particles = 0;
}

Particle ParticleSystems::makeParticle(ParticleSystem& system) {
    const ParticleType& type = *system.type;
    Particle particle;
    particle.position = system.position;
    particle.size = type.size;
    particle.colour = system.colour;
    particle.rect = system.rect;
    // A random direction round the system's facing, for the bursts.
    const anim::Vec3 ahead = facing(system.rotation);
    const anim::Vec3 jitter{unit() - 0.5F, unit() - 0.5F, unit() - 0.5F};
    switch (type.behaviour) {
    case ParticleBehaviour::Inert:
        break;
    case ParticleBehaviour::Steam:
        if (system.steam) {
            // A `sub_smoke` puff (`0x003f61d8`): the vent's −x axis × speed with z the rise (× 0.8-1.2), its alpha 0
            // until its first update, living round(life × 60) / puffInterval updates.
            const SteamSettings& steam = *system.steam;
            const anim::Vec3 back = anim::transformDirection(anim::matrixFromQuat(anim::normalise(system.rotation)),
                                                             anim::Vec3{-1.0F, 0.0F, 0.0F});
            Particle::SteamPuff puff;
            puff.start = anim::Vec3{back.x * steam.speed, back.y * steam.speed,
                                    steam.rise * (kSteamSpreadMin + (kSteamSpread * unit()))};
            puff.dragH = steam.dragH;
            puff.dragV = steam.dragV;
            puff.growth = steam.growth;
            puff.interval = static_cast<float>(steam.puffInterval) / kFramesPerSecond;
            puff.due = puff.interval;
            puff.life = std::max<std::uint32_t>(
                1, static_cast<std::uint32_t>(std::round(steam.life * kFramesPerSecond)) / steam.puffInterval);
            puff.startAlpha = static_cast<std::uint8_t>(steam.colour & 0xffU);
            particle.colour = steam.colour & 0xffffff00U;
            particle.fades = false;
            particle.life = 0.0F; // it ends at its last update
            particle.size = steam.size * (kSteamSpreadMin + (kSteamSpread * unit()));
            particle.velocity = puff.start;
            particle.rect = static_cast<std::uint16_t>(kSmokeRect + draw(2));
            particle.steam = puff;
        }
        break;
    case ParticleBehaviour::Glow:
        particle.life = 0.0F; // lives with the system
        particle.fades = false;
        break;
    case ParticleBehaviour::Flash:
        particle.life = kFlashLife;
        break;
    case ParticleBehaviour::Flames:
        particle.life = kFlameLife;
        particle.velocity = anim::Vec3{jitter.x * 0.4F, jitter.y * 0.4F, kFlameRise};
        particle.grow = -type.size * 0.5F;
        particle.rect = static_cast<std::uint16_t>(system.rect);
        break;
    case ParticleBehaviour::Puff:
        particle.life = kPuffLife;
        particle.velocity = anim::add(anim::scale(jitter, 0.6F), anim::Vec3{0.0F, 0.0F, kPuffRise});
        particle.grow = type.size * kPuffGrowth;
        particle.angle = unit() * 2.0F * std::numbers::pi_v<float>;
        break;
    case ParticleBehaviour::Spray:
        particle.life = kSprayLife * (0.6F + 0.4F * unit());
        particle.velocity =
            anim::scale(anim::normalise(anim::add(ahead, anim::scale(jitter, 1.0F))), kSpraySpeed * (0.6F + unit()));
        particle.gravity = kGravity;
        break;
    case ParticleBehaviour::Sparks:
        particle.life = kSparkLife * (0.5F + 0.5F * unit());
        particle.velocity =
            anim::scale(anim::normalise(anim::add(ahead, anim::scale(jitter, 1.2F))), kSparkSpeed * (0.5F + unit()));
        particle.gravity = kGravity;
        break;
    case ParticleBehaviour::Shard:
        particle.life = kShardLife;
        particle.velocity = anim::scale(jitter, 1.0F);
        particle.gravity = kGravity;
        particle.angle = unit() * 2.0F * std::numbers::pi_v<float>;
        particle.spin = (unit() * 2.0F - 1.0F) * kShardSpin;
        break;
    }
    return particle;
}

void ParticleSystems::emit(ParticleSystem& system, std::size_t count) {
    for (std::size_t i = 0; i < count && m_particles < kParticleBudget; ++i) {
        system.particles.push_back(makeParticle(system));
        ++m_particles;
    }
}

bool ParticleSystems::setEmitting(double handle, bool on) {
    const auto found = std::ranges::find(m_systems, handle, &ParticleSystem::handle);
    if (handle == 0 || found == m_systems.end()) {
        return false;
    }
    found->emitting = on;
    return true;
}

bool ParticleSystems::configureSteam(double handle, const SteamSettings& settings) {
    const auto found = std::ranges::find(m_systems, handle, &ParticleSystem::handle);
    if (handle == 0 || found == m_systems.end()) {
        return false;
    }
    SteamSettings steam = settings;
    steam.puffInterval = std::max<std::uint32_t>(steam.puffInterval, 1);
    found->steam = steam;
    return true;
}

void ParticleSystems::stepSteam(ParticleSystem& system, float seconds) {
    if (!system.steam || !system.emitting || !m_viewer) {
        return;
    }
    // A run every `interval` frames while near, every 60 while far; each run makes one puff.
    system.emitDue -= seconds;
    if (system.emitDue > 0.0F) {
        return;
    }
    emit(system, 1);
    const anim::Vec3 away = anim::subtract(system.position, *m_viewer);
    const bool near = anim::dot(away, away) <= kSteamNear * kSteamNear;
    const float frames =
        near ? static_cast<float>(std::max<std::uint32_t>(system.steam->interval, 1)) : kSteamFarFrames;
    system.emitDue = std::max(system.emitDue + (frames / kFramesPerSecond), 0.0F);
}

void ParticleSystems::updatePuff(Particle& puff) {
    CONEY_ASSERT(puff.steam.has_value()); // only steam puffs are updated as puffs
    Particle::SteamPuff& steam = *puff.steam;
    ++steam.age;
    const float f = static_cast<float>(steam.age) / static_cast<float>(steam.life);
    // The start velocity less its dragged share (the wind, `0x006f31a0`, is not traced: none), × 0.75-1.15.
    const anim::Vec3 dragged{steam.start.x * (1.0F - (f * steam.dragH)), steam.start.y * (1.0F - (f * steam.dragH)),
                             steam.start.z * (1.0F - (f * steam.dragV))};
    puff.velocity = anim::scale(dragged, kPuffSpeedMin + (kPuffSpeedSpread * unit()));
    puff.size += steam.growth * (kSteamSpreadMin + (kSteamSpread * unit()));
    const std::uint32_t left = steam.age >= steam.life ? 0 : steam.life - steam.age;
    const auto alpha = static_cast<std::uint32_t>(steam.startAlpha) * left / steam.life;
    puff.colour = (puff.colour & 0xffffff00U) | (alpha & 0xffU);
}

bool ParticleSystems::stepSystem(ParticleSystem& system, float seconds) {
    // Follow the parent, while it is there.
    if (system.parent != 0 && m_locator) {
        if (const std::optional<anim::Vec3> at = m_locator(system.parent)) {
            system.position = anim::add(*at, system.offset);
        }
    }
    const ParticleBehaviour behaviour = system.type->behaviour;
    // The first step makes a burst's sprites, or a glow's one.
    if (!system.started) {
        system.started = true;
        switch (behaviour) {
        case ParticleBehaviour::Glow:
        case ParticleBehaviour::Flash:
        case ParticleBehaviour::Shard:
            emit(system, 1);
            break;
        case ParticleBehaviour::Puff:
            emit(system, kPuffCount);
            break;
        case ParticleBehaviour::Spray:
            emit(system, kSprayCount);
            break;
        case ParticleBehaviour::Sparks:
            emit(system, kSparkCount);
            break;
        case ParticleBehaviour::Inert:
        case ParticleBehaviour::Flames:
        case ParticleBehaviour::Steam:
            break;
        }
    }
    if (behaviour == ParticleBehaviour::Steam) {
        stepSteam(system, seconds);
    }
    // A stream makes its sprites as its interval comes round.
    if (behaviour == ParticleBehaviour::Flames && system.emitting) {
        system.emitDue -= seconds;
        while (system.emitDue <= 0.0F) {
            emit(system, 1);
            system.emitDue += kFlameInterval;
        }
    }
    // Move and age the sprites; drop the ones whose life is over.
    for (Particle& particle : system.particles) {
        particle.age += seconds;
        particle.velocity.z -= particle.gravity * seconds;
        particle.position = anim::add(particle.position, anim::scale(particle.velocity, seconds));
        // A steam puff updates every puffInterval frames.
        if (particle.steam) {
            particle.steam->due -= seconds;
            while (particle.steam->due <= 0.0F && particle.steam->age < particle.steam->life) {
                updatePuff(particle);
                particle.steam->due += particle.steam->interval;
            }
        }
        particle.size = std::max(0.0F, particle.size + particle.grow * seconds);
        particle.angle += particle.spin * seconds;
        if (behaviour == ParticleBehaviour::Glow) {
            particle.position = system.position;
        }
        if (behaviour == ParticleBehaviour::Flames && particle.life > 0.0F) {
            const auto frame = static_cast<std::uint16_t>(std::min<float>(
                kFlameFrames - 1, std::floor(particle.age / particle.life * static_cast<float>(kFlameFrames))));
            particle.rect = static_cast<std::uint16_t>(system.rect + frame);
        }
    }
    const std::size_t before = system.particles.size();
    std::erase_if(system.particles, [](const Particle& p) {
        return (p.life > 0.0F && p.age >= p.life) || (p.steam && p.steam->age >= p.steam->life);
    });
    m_particles -= before - system.particles.size();
    system.age += seconds;
    return !(endsWhenEmpty(behaviour) && system.particles.empty());
}

void ParticleSystems::step(float seconds) {
    std::erase_if(m_systems, [this, seconds](ParticleSystem& system) {
        const bool lives = stepSystem(system, seconds);
        if (!lives) {
            m_particles -= system.particles.size();
        }
        return !lives;
    });
}

} // namespace coney::effects
