// SPDX-License-Identifier: GPL-3.0-or-later
// The particle manager (docs/research/particles.md): types found by name, spawning and its pool, bursts that end and
// streams that last, attached systems, hiding and killing, a pane's shards, and the same run from the same seed.
#include "effects/particles.h"

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
