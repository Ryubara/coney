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

// The explosion family (docs/research/script-types.md#part-explosion, the molotov's `sub_explode` included). A tick is
// a 60th of a second; a particle's drawn sprite is twice its size wide (kDrawnPerSize).
constexpr float kTick = 1.0F / 60.0F;
constexpr float kDrawnPerSize = 2.0F;
// `sub_explode`: from white at alpha 0xf0, 1 tick to size 0.2, 8 ticks to 1.0 towards alpha 0xbf (setting off the
// `part_explosion` as that stage begins), 40 ticks to 1.5 fading to alpha 0.
constexpr std::array<float, 3> kExplodeTicks{1.0F, 8.0F, 40.0F};
constexpr std::array<float, 3> kExplodeSizes{0.2F, 1.0F, 1.5F};
constexpr std::array<std::uint32_t, 3> kExplodeColours{0xFFFFFFF0U, 0xFFFFFFBFU, 0xFFFFFF00U};
constexpr std::uint32_t kExplodeStart = 0xFFFFFFF0U;
constexpr std::size_t kExplosionStage = 1;
// `part_explosion`: six embers (x and z ±0.05, y −3.5 to 0.05, then turned; 6-8 m/s; 25-40 ticks; a random 0.6-1.5,
// read here as a size factor on kEmberSize), twenty large and eight small chips of debris.
constexpr std::size_t kEmbers = 6;
constexpr float kEmberSize = 0.2F;
constexpr std::size_t kLargeDebris = 20;
constexpr std::size_t kSmallDebris = 8;
// `sub_fireball` from the emitter (scale 2.0): 1.25 × scale m/s outward, size 0.8 × scale, seven stages of the table's
// ticks plus up to as many again, each to 0.8-0.88 × scale, through the table's colours from transparent black.
constexpr float kFireballScale = 2.0F;
constexpr float kFireballSpeed = 1.25F;
constexpr float kFireballSize = 0.8F;
constexpr std::array<float, 7> kFireballTicks{8.0F, 10.0F, 10.0F, 8.0F, 8.0F, 10.0F, 10.0F};
constexpr std::array<std::uint32_t, 7> kFireballColours{0x1010ce24U, 0xfb780c9fU, 0xc336097fU, 0x590e0024U,
                                                        0x33080030U, 0x33080020U, 0x34210010U};

// Whether a system of `behaviour` ends once its sprites are gone (a burst), rather than living until it is killed.
bool endsWhenEmpty(ParticleBehaviour behaviour) {
    switch (behaviour) {
    case ParticleBehaviour::Explode:
    case ParticleBehaviour::Explosion:
    case ParticleBehaviour::Fireball:
    case ParticleBehaviour::Embers:
    case ParticleBehaviour::Debris:
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

// `direction` turned by `rotation`.
anim::Vec3 turned(anim::Quat rotation, anim::Vec3 direction) {
    return anim::transformDirection(anim::matrixFromQuat(anim::normalise(rotation)), direction);
}

// The colour `t` of the way from `from` to `to`, channel by channel (`0xRRGGBBAA`).
std::uint32_t mixColour(std::uint32_t from, std::uint32_t to, float t) {
    std::uint32_t out = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) {
        const auto a = static_cast<float>((from >> shift) & 0xffU);
        const auto b = static_cast<float>((to >> shift) & 0xffU);
        out |= (static_cast<std::uint32_t>(std::lround(a + ((b - a) * t))) & 0xffU) << shift;
    }
    return out;
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
    } else if (system.type->behaviour == ParticleBehaviour::Explosion) {
        // An explosion's parts, or an emitter's fireballs, are made by its init.
        if (system.type->name == "sub_fireball_emitter") {
            spawnFireballs(position, rotation);
        } else {
            spawnExplosionParts(position, rotation);
        }
    }
    return &m_systems[index];
}

bool ParticleSystems::spawnSprite(std::string_view typeName, anim::Vec3 at, anim::Quat rotation,
                                  const Particle& particle) {
    if (m_particles >= kParticleBudget) {
        return false;
    }
    ParticleSystem* system = spawn(typeName, at, rotation);
    if (system == nullptr) {
        return false;
    }
    system->particles.push_back(particle);
    system->started = true;
    ++m_particles;
    return true;
}

Particle ParticleSystems::makeFireball(anim::Vec3 at, anim::Vec3 direction) {
    Particle ball;
    ball.position = at;
    ball.velocity = anim::scale(direction, kFireballScale * kFireballSpeed);
    ball.size = kFireballSize * kFireballScale * kDrawnPerSize;
    ball.colour = 0;
    ball.rect = findParticleType("sub_fireball")->rect;
    ball.angle = unit() * 2.0F * std::numbers::pi_v<float>;
    ball.fades = false;
    ball.life = 0.0F; // it ends after its last stage
    Particle::Stages stages;
    stages.count = kFireballTicks.size();
    for (std::size_t i = 0; i < stages.count; ++i) {
        const auto base = static_cast<std::uint32_t>(kFireballTicks.at(i));
        stages.seconds.at(i) = static_cast<float>(base + draw(base)) * kTick;
        stages.sizes.at(i) = (0.8F + (0.08F * unit())) * kFireballScale * kDrawnPerSize;
        stages.colours.at(i) = kFireballColours.at(i);
    }
    stages.fromSize = ball.size;
    stages.fromColour = ball.colour;
    ball.stages = stages;
    return ball;
}

void ParticleSystems::spawnFireballs(anim::Vec3 at, anim::Quat rotation) {
    constexpr std::array<anim::Vec3, 6> kDirections{{{1.0F, 0.0F, 0.0F},
                                                     {-1.0F, 0.0F, 0.0F},
                                                     {0.0F, 1.0F, 0.0F},
                                                     {0.0F, -1.0F, 0.0F},
                                                     {0.0F, 0.0F, 1.0F},
                                                     {0.0F, 0.0F, -1.0F}}};
    for (const anim::Vec3& direction : kDirections) {
        spawnSprite("sub_fireball", at, rotation, makeFireball(at, turned(rotation, direction)));
    }
}

void ParticleSystems::spawnExplosionParts(anim::Vec3 at, anim::Quat rotation) {
    // The embers: each thrown along a random vector turned by the explosion's rotation, falling.
    for (std::size_t i = 0; i < kEmbers; ++i) {
        const anim::Vec3 vector{(unit() - 0.5F) * 0.1F, -3.5F + (3.55F * unit()), (unit() - 0.5F) * 0.1F};
        Particle ember;
        ember.position = at;
        ember.velocity = anim::scale(anim::normalise(turned(rotation, vector)), 6.0F + (2.0F * unit()));
        ember.gravity = kGravity;
        ember.life = static_cast<float>(25U + draw(15)) * kTick;
        ember.size = kEmberSize * (0.6F + (0.9F * unit()));
        ember.rect = findParticleType("sub_explosion_embers")->rect;
        spawnSprite("sub_explosion_embers", at, rotation, ember);
    }
    spawnFireballs(at, rotation);
    // The debris: twenty chips half a metre up, then eight smaller ones a little higher, while the budget allows.
    const auto chip = [this](anim::Vec3 from, float spread, float rise, float riseSpread, float life, float size,
                             float sizeSpread) {
        const anim::Vec3 direction{(unit() - 0.5F) * 2.0F * spread, (unit() - 0.5F) * 2.0F * spread,
                                   rise + (riseSpread * unit())};
        Particle debris;
        debris.position = from;
        debris.velocity = anim::scale(anim::normalise(direction), 5.0F + (4.0F * unit()));
        debris.gravity = kGravity;
        debris.life = life * kTick;
        debris.size = (size + (sizeSpread * unit())) * kDrawnPerSize;
        debris.rect = static_cast<std::uint16_t>(findParticleType("sub_debris")->rect + draw(3));
        debris.angle = unit() * 2.0F * std::numbers::pi_v<float>;
        spawnSprite("sub_debris", from, {}, debris);
    };
    for (std::size_t i = 0; i < kLargeDebris; ++i) {
        chip(anim::add(at, anim::Vec3{0.0F, 0.0F, 0.5F}), 0.5F, 0.25F, 0.75F, 30.0F, 0.25F, 0.1F);
    }
    for (std::size_t i = 0; i < kSmallDebris; ++i) {
        chip(anim::add(at, anim::Vec3{0.0F, 0.0F, 0.55F}), 0.25F, 0.0F, 0.75F, 60.0F, 0.05F, 0.05F);
    }
}

bool ParticleSystems::stepStages(Particle& particle, float seconds) {
    CONEY_ASSERT(particle.stages.has_value()); // only staged sprites step through stages
    Particle::Stages& stages = *particle.stages;
    stages.inStage += seconds;
    while (stages.at < stages.count && stages.inStage >= stages.seconds.at(stages.at)) {
        stages.inStage -= stages.seconds.at(stages.at);
        stages.fromSize = stages.sizes.at(stages.at);
        stages.fromColour = stages.colours.at(stages.at);
        ++stages.at;
    }
    if (stages.at >= stages.count) {
        return false;
    }
    const float length = stages.seconds.at(stages.at);
    const float t = length > 0.0F ? stages.inStage / length : 1.0F;
    particle.size = stages.fromSize + ((stages.sizes.at(stages.at) - stages.fromSize) * t);
    particle.colour = mixColour(stages.fromColour, stages.colours.at(stages.at), t);
    return true;
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
    m_pendingExplosions.clear();
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
    case ParticleBehaviour::Explosion:
        break;
    case ParticleBehaviour::Explode: {
        // The flash: a random roll, then its three stages.
        particle.angle = unit() * 2.0F * std::numbers::pi_v<float>;
        particle.fades = false;
        particle.life = 0.0F;
        particle.size = 0.0F;
        particle.colour = kExplodeStart;
        Particle::Stages stages;
        stages.count = kExplodeTicks.size();
        for (std::size_t i = 0; i < stages.count; ++i) {
            stages.seconds.at(i) = kExplodeTicks.at(i) * kTick;
            stages.sizes.at(i) = kExplodeSizes.at(i) * kDrawnPerSize;
            stages.colours.at(i) = kExplodeColours.at(i);
        }
        stages.fromColour = kExplodeStart;
        particle.stages = stages;
        break;
    }
    case ParticleBehaviour::Fireball:
        particle = makeFireball(system.position, ahead);
        break;
    case ParticleBehaviour::Embers:
        // One ember alone, as the explosion throws them (spawnExplosionParts()).
        particle.velocity = anim::scale(ahead, 6.0F + (2.0F * unit()));
        particle.gravity = kGravity;
        particle.life = static_cast<float>(25U + draw(15)) * kTick;
        break;
    case ParticleBehaviour::Debris:
        // One large chip alone, as the explosion throws them (spawnExplosionParts()).
        particle.velocity =
            anim::scale(anim::normalise(anim::add(ahead, anim::scale(jitter, 1.0F))), 5.0F + (4.0F * unit()));
        particle.gravity = kGravity;
        particle.life = 30.0F * kTick;
        particle.rect = static_cast<std::uint16_t>(system.rect + draw(3));
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
        case ParticleBehaviour::Explode:
        case ParticleBehaviour::Fireball:
        case ParticleBehaviour::Embers:
        case ParticleBehaviour::Debris:
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
        case ParticleBehaviour::Explosion:
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
        // A staged sprite moves through its stages; the flash sets off its explosion as the second stage begins.
        if (particle.stages) {
            const std::size_t was = particle.stages->at;
            static_cast<void>(stepStages(particle, seconds));
            if (behaviour == ParticleBehaviour::Explode && was < kExplosionStage &&
                particle.stages->at >= kExplosionStage) {
                m_pendingExplosions.emplace_back(particle.position, system.rotation);
            }
        }
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
        return (p.life > 0.0F && p.age >= p.life) || (p.steam && p.steam->age >= p.steam->life) ||
               (p.stages && p.stages->at >= p.stages->count);
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
    // The explosions the flashes set off this step, made now that the systems are not being walked.
    std::vector<std::pair<anim::Vec3, anim::Quat>> pending;
    pending.swap(m_pendingExplosions);
    for (const auto& [at, rotation] : pending) {
        spawn("part_explosion", at, rotation);
    }
}

} // namespace coney::effects
