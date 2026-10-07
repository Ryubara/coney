// SPDX-License-Identifier: GPL-3.0-or-later
// The particle manager (docs/research/particles.md): types found by name, spawning and its pool, bursts that end and
// streams that last, attached systems, hiding and killing, a pane's shards, and the same run from the same seed.
#include "effects/particles.h"

#include <cmath>
#include <cstddef>
#include <optional>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "effects/particle_types.h"

using coney::anim::Vec3;
using coney::effects::ParticleBehaviour;
using coney::effects::ParticleSheet;
using coney::effects::ParticleSystem;
using coney::effects::ParticleSystems;

namespace {

constexpr float kStep = 1.0F / 30.0F;

// Steps `systems` for `steps` fixed steps.
void run(ParticleSystems& systems, int steps) {
    for (int i = 0; i < steps; ++i) {
        systems.step(kStep);
    }
}

} // namespace

TEST_CASE("the type table is sorted and finds its names exactly", "[particles]") {
    const auto types = coney::effects::particleTypes();
    REQUIRE_FALSE(types.empty());
    for (std::size_t i = 1; i < types.size(); ++i) {
        CHECK(types[i - 1].name < types[i].name);
    }
    const coney::effects::ParticleType* flash = coney::effects::findParticleType("part_gun_flash");
    REQUIRE(flash != nullptr);
    CHECK(flash->sheet == ParticleSheet::Lighting);
    CHECK(flash->rect == 2);
    CHECK(coney::effects::findParticleType("part_ominoussmoke") == nullptr);
    CHECK(coney::effects::findParticleType("PART_GUN_FLASH") == nullptr);
    CHECK(coney::effects::sheetName(ParticleSheet::PartPage1) == "part_page1");
}

TEST_CASE("a spawned system keeps its handle, place and type; an unknown name is inert", "[particles]") {
    ParticleSystems systems;
    const ParticleSystem* fire = systems.spawn("part_fire", Vec3{1, 2, 3}, {}, 0, 40);
    REQUIRE(fire != nullptr);
    CHECK(fire->type->behaviour == ParticleBehaviour::Flames);
    CHECK(fire->position == Vec3{1, 2, 3});
    const ParticleSystem* unknown = systems.spawn("part_ominoussmoke", Vec3{}, {}, 0, 41);
    REQUIRE(unknown != nullptr);
    CHECK(unknown->type->behaviour == ParticleBehaviour::Inert);
    run(systems, 30);
    REQUIRE(systems.find(40) != nullptr);
    CHECK_FALSE(systems.find(40)->particles.empty());
    // The inert one lives on, drawing nothing.
    REQUIRE(systems.find(41) != nullptr);
    CHECK(systems.find(41)->particles.empty());
    CHECK(systems.find(0) == nullptr);
}

TEST_CASE("a burst ends once its sprites are gone; a glow lasts until killed", "[particles]") {
    ParticleSystems systems;
    REQUIRE(systems.spawnBlood(Vec3{0, 0, 1}, Vec3{1, 0, 0}) != nullptr);
    REQUIRE(systems.spawn("coplights_glow", Vec3{}, {}, 0, 7) != nullptr);
    systems.step(kStep);
    CHECK(systems.systems().size() == 2);
    CHECK(systems.particleCount() > 1);
    // Blood is thrown along its direction and falls.
    const ParticleSystem& blood = systems.systems().front();
    float meanX = 0.0F;
    for (const auto& drop : blood.particles) {
        meanX += drop.velocity.x;
    }
    CHECK(meanX > 0.0F);
    run(systems, 60);
    CHECK(systems.systems().size() == 1);
    CHECK(systems.particleCount() == 1);
    CHECK(systems.kill(7));
    CHECK_FALSE(systems.kill(7));
    CHECK(systems.systems().empty());
    CHECK(systems.particleCount() == 0);
}

TEST_CASE("the gun flash makes a puff of smoke, whose rectangle is 42 or 43", "[particles]") {
    ParticleSystems systems;
    const ParticleSystem* flash = systems.spawn("part_gun_flash", Vec3{}, {}, 0, 5);
    REQUIRE(flash != nullptr);
    CHECK(flash->handle == 5);
    REQUIRE(systems.systems().size() == 2);
    const ParticleSystem& puff = systems.systems().back();
    CHECK(puff.type->name == "sub_shack_puff");
    CHECK((puff.rect == 42 || puff.rect == 43));
}

TEST_CASE("an attached system follows its parent at the offset it was spawned with", "[particles]") {
    ParticleSystems systems;
    Vec3 parent{10, 0, 0};
    systems.setLocator([&parent](double handle) -> std::optional<Vec3> {
        return handle == 3 ? std::optional<Vec3>(parent) : std::nullopt;
    });
    REQUIRE(systems.spawn("coplights_glow", Vec3{10, 0, 2}, {}, 3, 9) != nullptr);
    parent = Vec3{20, 5, 0};
    systems.step(kStep);
    REQUIRE(systems.find(9) != nullptr);
    CHECK(systems.find(9)->position == Vec3{20, 5, 2});
}

TEST_CASE("a hidden system keeps stepping and can be shown again", "[particles]") {
    ParticleSystems systems;
    REQUIRE(systems.spawn("part_fire", Vec3{}, {}, 0, 2) != nullptr);
    systems.setHidden(2, true);
    systems.setHidden(99, true); // unknown: ignored
    systems.step(kStep);
    REQUIRE(systems.find(2) != nullptr);
    CHECK(systems.find(2)->hidden);
    systems.setHidden(2, false);
    CHECK_FALSE(systems.find(2)->hidden);
}

TEST_CASE("the pool holds 1,400 systems", "[particles]") {
    ParticleSystems systems;
    for (std::size_t i = 0; i < ParticleSystems::kSystemPool; ++i) {
        REQUIRE(systems.spawn("part_train_sound", Vec3{}) != nullptr);
    }
    CHECK(systems.spawn("part_train_sound", Vec3{}) == nullptr);
}

TEST_CASE("a shard is made at once at its size and colour, falls and ends", "[particles]") {
    ParticleSystems systems;
    const ParticleSystem* shard = systems.spawnShard(Vec3{0, 0, 2}, 0.06F, 0x11223344U);
    REQUIRE(shard != nullptr);
    CHECK(shard->type->name == "glasstest");
    CHECK(shard->colour == 0x11223344U);
    REQUIRE(shard->particles.size() == 1);
    CHECK_THAT(shard->particles.front().size, Catch::Matchers::WithinAbs(0.06, 1e-6));
    CHECK(systems.hasRoom(ParticleSystems::kParticleBudget - 1));
    CHECK_FALSE(systems.hasRoom(ParticleSystems::kParticleBudget));
    run(systems, 10);
    REQUIRE(systems.systems().size() == 1);
    CHECK(systems.systems().front().particles.front().position.z < 2.0F);
    run(systems, 60);
    CHECK(systems.systems().empty());
}

TEST_CASE("the same seed gives the same sprites", "[particles]") {
    ParticleSystems a(1234);
    ParticleSystems b(1234);
    for (ParticleSystems* systems : {&a, &b}) {
        REQUIRE(systems->spawnSparks(Vec3{0, 0, 1}, Vec3{0, 1, 0}) != nullptr);
        run(*systems, 3);
    }
    REQUIRE(a.systems().size() == 1);
    REQUIRE(b.systems().size() == 1);
    const auto& pa = a.systems().front().particles;
    const auto& pb = b.systems().front().particles;
    REQUIRE(pa.size() == pb.size());
    for (std::size_t i = 0; i < pa.size(); ++i) {
        CHECK(pa[i].position == pb[i].position);
    }
}

TEST_CASE("a stream switched off makes no sprites until it is switched on again", "[particles]") {
    ParticleSystems systems;
    REQUIRE(systems.spawn("part_fire", Vec3{}, {}, 0, 2) != nullptr);
    CHECK(systems.setEmitting(2, false));
    CHECK_FALSE(systems.setEmitting(99, false));
    for (int i = 0; i < 30; ++i) {
        systems.step(kStep);
    }
    CHECK(systems.find(2)->particles.empty());
    CHECK(systems.setEmitting(2, true));
    for (int i = 0; i < 30; ++i) {
        systems.step(kStep);
    }
    CHECK_FALSE(systems.find(2)->particles.empty());
}

TEST_CASE("a steam vent puffs with its built-in values or as CfgSteam configures it, faster near the camera",
          "[particles]") {
    ParticleSystems systems;
    ParticleSystem* vent = systems.spawn("part_steam", Vec3{10, 0, 0}, {}, 0, 5);
    REQUIRE(vent != nullptr);
    CHECK(vent->type->behaviour == ParticleBehaviour::Steam);
    systems.setViewer(Vec3{0, 0, 0});
    // Before CfgSteam it has its init's built-in values: on, white at alpha 64, a puff every 17 frames.
    REQUIRE(vent->steam.has_value());
    CHECK(vent->steam.value_or(coney::effects::SteamSettings{}).colour == 0xffffff40U);
    CHECK(vent->steam.value_or(coney::effects::SteamSettings{}).interval == 17);
    CHECK(coney::effects::defaultSteam("part_steam_huge").value_or(coney::effects::SteamSettings{}).puffInterval == 30);
    CHECK_FALSE(coney::effects::defaultSteam("part_fire").has_value());
    coney::effects::SteamSettings steam{.colour = 0x808080FFU,
                                        .interval = 10,
                                        .puffInterval = 0,
                                        .size = 0.25F,
                                        .growth = 0.125F,
                                        .life = 1.0F,
                                        .speed = 2.0F,
                                        .rise = 0.5F,
                                        .dragH = 1.0F};
    CHECK(systems.configureSteam(5, steam));
    CHECK_FALSE(systems.configureSteam(6, steam));
    const std::optional<coney::effects::SteamSettings>& configured = systems.find(5)->steam;
    REQUIRE(configured.has_value());
    CHECK(configured.value_or(steam).puffInterval == 1); // never 0: the original divides by it
    // Near (within 20 m): one puff every 10 frames; each updates every 6 frames, round(1 s × 60) / 6 = 10 times,
    // moving along the vent's -x at 2 m/s and rising.
    steam.puffInterval = 6;
    REQUIRE(systems.configureSteam(5, steam));
    run(systems, 15); // half a second
    CHECK(systems.particleCount() == 3);
    const coney::effects::Particle& first = systems.find(5)->particles.front();
    REQUIRE(first.steam.has_value());
    CHECK(first.steam.value_or(coney::effects::Particle::SteamPuff{}).life == 10);
    CHECK(first.position.x < 10.0F); // along -x
    CHECK(first.position.z > 0.0F);  // rising
    CHECK(first.rect >= 42);
    CHECK(first.rect <= 44);
    CHECK((first.colour & 0xffU) < 0xffU); // fading out over its life
    CHECK(first.size > 0.25F * 0.8F);      // growing each update
    // A puff ends with its last update.
    run(systems, 30);
    for (const coney::effects::Particle& puff : systems.find(5)->particles) {
        CHECK(puff.steam.value_or(coney::effects::Particle::SteamPuff{}).age < 10);
    }
    // Far (more than 20 m): one puff a second.
    ParticleSystems far;
    REQUIRE(far.spawn("part_steam", Vec3{50, 0, 0}, {}, 0, 7) != nullptr);
    far.setViewer(Vec3{0, 0, 0});
    REQUIRE(far.configureSteam(7, steam));
    run(far, 15);
    CHECK(far.particleCount() == 1);
    // Switched off it makes none.
    REQUIRE(far.setEmitting(7, false));
    run(far, 90);
    CHECK(far.particleCount() == 0);
}

TEST_CASE("a sub_explode flash sets off a part_explosion: embers, six fireballs and debris", "[effects][particles]") {
    ParticleSystems systems;
    REQUIRE(systems.spawn("sub_explode", Vec3{0.0F, 0.0F, 1.0F}) != nullptr);
    // The first step makes the flash and runs its first stage (one tick): the explosion is made as the second begins.
    systems.step(1.0F / 30.0F);
    std::size_t fireballs = 0;
    std::size_t embers = 0;
    std::size_t debris = 0;
    for (const ParticleSystem& system : systems.systems()) {
        fireballs += system.type->name == "sub_fireball" ? 1 : 0;
        embers += system.type->name == "sub_explosion_embers" ? 1 : 0;
        debris += system.type->name == "sub_debris" ? 1 : 0;
    }
    CHECK(fireballs == 6);
    CHECK(embers == 6);
    CHECK(debris == 28);
    // The flash's three stages last 49 ticks; the fireballs at most 128; then nothing is left.
    for (int step = 0; step < 90; ++step) {
        systems.step(1.0F / 30.0F);
    }
    CHECK(systems.systems().empty());
    CHECK(systems.particleCount() == 0);
}

TEST_CASE("a fireball grows from 3.2 m across through its stages from transparent black", "[effects][particles]") {
    ParticleSystems systems;
    REQUIRE(systems.spawn("sub_fireball", Vec3{}) != nullptr);
    systems.step(1.0F / 60.0F);
    REQUIRE(systems.systems().size() == 1);
    const auto& ball = systems.systems()[0].particles.at(0);
    CHECK(ball.size >= 3.2F);
    CHECK(ball.size <= 3.52F);
    CHECK((ball.colour & 0xffU) < 0x24U);
}

TEST_CASE("an alarm strobe's strober lights from its switching on until the update after its switching off",
          "[particles]") {
    ParticleSystems systems;
    ParticleSystem* strobe = systems.spawn("part_strobe_red", Vec3{1, 2, 3}, {}, 0, 5.0);
    REQUIRE(strobe != nullptr);
    CHECK_FALSE(strobe->emitting);
    CHECK_FALSE(coney::effects::systemLight(*strobe).has_value());
    CHECK(systems.nearestNamed("strobe", Vec3{1, 2, 0}, 6.0F) == strobe);
    CHECK(systems.nearestNamed("strobe", Vec3{1, 20, 0}, 6.0F) == nullptr);
    CHECK(systems.nearestNamed("steam", Vec3{1, 2, 0}, 6.0F) == nullptr);
    ParticleSystems::setEmitting(*strobe, true);
    const std::optional<coney::effects::SystemLight> lit = coney::effects::systemLight(*strobe);
    REQUIRE(lit.has_value());
    CHECK(lit.value_or(coney::effects::SystemLight{}).radius == 10.0F);
    CHECK(lit.value_or(coney::effects::SystemLight{}).colour.r == 1.0F);
    CHECK(lit.value_or(coney::effects::SystemLight{}).lightsWorld);
    run(systems, 20); // 40 ticks: faded to black
    REQUIRE(coney::effects::systemLight(systems.systems()[0]).has_value());
    CHECK(coney::effects::systemLight(systems.systems()[0]).value_or(coney::effects::SystemLight{}).colour.r == 0.0F);
    // Switched off, it ends at its next update (tick 50).
    CHECK(systems.setEmitting(5.0, false));
    run(systems, 4);
    CHECK(coney::effects::systemLight(systems.systems()[0]).has_value());
    run(systems, 1);
    CHECK_FALSE(coney::effects::systemLight(systems.systems()[0]).has_value());
}

TEST_CASE("a neon sign gives its light in its colour from the start", "[particles]") {
    ParticleSystems systems;
    const ParticleSystem* sign = systems.spawn("part_pink_neon", Vec3{1, 2, 3});
    REQUIRE(sign != nullptr);
    const std::optional<coney::effects::SystemLight> light = coney::effects::systemLight(*sign);
    REQUIRE(light.has_value());
    CHECK(light.value_or(coney::effects::SystemLight{}).radius == 4.0F);
    CHECK(light.value_or(coney::effects::SystemLight{}).colour.g == 175.0F / 255.0F);
    run(systems, 600); // 20 s: the sign stays
    REQUIRE(systems.systems().size() == 1);
    CHECK(coney::effects::systemLight(systems.systems()[0]).has_value());
}

TEST_CASE("a garbage pile keeps three flies jumping round it while a view is near, none otherwise", "[particles]") {
    ParticleSystems systems;
    REQUIRE(systems.spawn("part_garbage_flies", Vec3{10, 20, 1}) != nullptr);
    bool seen = true;
    systems.setViewTest([&seen](Vec3 /*point*/, float margin) { return seen && margin == 50.0F; });
    run(systems, 29); // 58 ticks: the pile's first update is at 60
    CHECK(systems.systems()[0].particles.empty());
    run(systems, 1);
    const auto& flies = systems.systems()[0].particles;
    REQUIRE(flies.size() == 3);
    for (const coney::effects::Particle& fly : flies) {
        // On the 1.5 m surface round the pile, from part_page1 rectangle 19, faded and grown to nothing at first.
        const Vec3 d = coney::anim::subtract(fly.position, Vec3{10, 20, 1});
        CHECK(std::sqrt(coney::anim::dot(d, d)) <= 1.5F + 1e-4F);
        CHECK(std::abs(d.z) <= 0.75F + 1e-4F);
        CHECK(fly.rect == 19);
        CHECK(fly.size == 0.0F);
        CHECK((fly.colour & 0xffU) == 0);
    }
    // Over the first update's 20 ticks each grows in; afterwards it is grey at 0.04 and jumps on.
    const Vec3 before = flies[0].position;
    run(systems, 5);
    CHECK(systems.systems()[0].particles[0].size > 0.0F);
    CHECK(systems.systems()[0].particles[0].size < 0.04F);
    run(systems, 6);
    CHECK(systems.systems()[0].particles[0].size == 0.04F);
    CHECK(systems.systems()[0].particles[0].colour == 0x808080ffU);
    const Vec3 after = systems.systems()[0].particles[0].position;
    CHECK((after.x != before.x || after.y != before.y || after.z != before.z));
    // No view near at the next pile update (tick 120): the flies go.
    seen = false;
    run(systems, 20);
    CHECK(systems.systems()[0].particles.empty());
    CHECK(systems.particleCount() == 0);
}
