// SPDX-License-Identifier: GPL-3.0-or-later
#include "physics/body_sweep.h"

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "raycast/collision_builder.h"

// The page's synthetic sweeps (docs/research/physics-bodies.md#u3-tests): a fake owner answering a fixed code, a fake
// world handing out one scripted contact per pass. Start at the origin, dt = 1/30, attributes 1 = 82, 7 = k, 8 = e.

using Catch::Approx;
using coney::anim::Vec3;
using coney::physics::Body;
using coney::physics::Box;
using coney::physics::Contact;
using coney::physics::ContactList;

namespace {

constexpr double kTolerance = 1e-5;
constexpr float kDt = 1.0F / 30.0F;

// An owner that records what the sweep does to it.
class FakeOwner final : public coney::physics::BodyOwner {
  public:
    std::uint32_t answer = 1;
    float pushWeight = 1.0F;
    float restitution = 0.0F;
    std::uint32_t mask = 0;
    bool inAir = false;
    Vec3 at;
    Body* own = nullptr;
    std::vector<Vec3> velocities;
    int afterMoves = 0;
    std::vector<Vec3> pushes;

    [[nodiscard]] std::uint32_t typeMask() const override { return mask; }
    [[nodiscard]] bool airborne() const override { return inAir; }
    [[nodiscard]] Vec3 cachedPosition() const override { return Vec3{}; }
    [[nodiscard]] Vec3 position() const override { return at; }
    void setPosition(Vec3 position) override { at = position; }
    void setVelocity(Vec3 velocity) override { velocities.push_back(velocity); }
    [[nodiscard]] float attribute(int which) const override {
        if (which == coney::physics::kAttributePushWeight) {
            return pushWeight;
        }
        return which == coney::physics::kAttributeRestitution ? restitution : 82.0F;
    }
    [[nodiscard]] Body* body() override { return own; }
    [[nodiscard]] std::uint32_t onContact(const Contact& /*contact*/) override { return answer; }
    void afterMove() override { ++afterMoves; }
    void onPushed(const Contact& /*contact*/, Vec3 push, Vec3 /*move*/) override { pushes.push_back(push); }
};

// A world that hands out the scripted contact of each pass (none past the script) and records the passes asked.
class FakeWorld final : public coney::physics::SweepWorld {
  public:
    std::vector<std::vector<Contact>> script;
    std::map<std::uint32_t, std::uint16_t> flags;
    int queries = 0;

    void sweepMesh(const Box& /*box*/, Body& body, Vec3 /*move*/, ContactList& list) override {
        const auto pass = static_cast<std::size_t>(queries++);
        if (pass >= script.size()) {
            return;
        }
        for (const Contact& contact : script[pass]) {
            list.pool.push_back(contact);
            coney::physics::insertSorted(list.pool.back(), list.sorted, body.owner, contact.otherOwner,
                                         contact.otherOwner != nullptr);
        }
    }
    [[nodiscard]] std::uint16_t triangleFlags(std::uint32_t index) const override {
        const auto it = flags.find(index);
        return it == flags.end() ? std::uint16_t{1} : it->second;
    }
};

// A level contact with `normal` at `fraction`, triangle 0.
Contact level(Vec3 normal, float fraction, std::uint32_t flags = 0, Vec3 slide = {}) {
    Contact contact;
    contact.normal = normal;
    contact.fraction = fraction;
    contact.triangle = 0;
    contact.flags = flags;
    contact.slide = slide;
    return contact;
}

// A body for `owner` with the page's default flags (0x400). The caller points `owner.own` at it where the body is
// asked for (a code 4 contact's other owner): the returned copy lives in the caller.
Body bodyFor(FakeOwner& owner, std::uint32_t flags = coney::physics::kBodyWriteVelocity) {
    Body body;
    body.owner = &owner;
    body.flags = flags;
    body.shape.radius = 0.5F;
    return body;
}

// Checks two vectors match to the tolerance.
void checkVec(Vec3 actual, Vec3 expected) {
    CHECK(actual.x == Approx(expected.x).margin(kTolerance));
    CHECK(actual.y == Approx(expected.y).margin(kTolerance));
    CHECK(actual.z == Approx(expected.z).margin(kTolerance));
}

} // namespace

TEST_CASE("a head-on wall: advance, slide to nothing, stuck", "[physics][body_sweep]") {
    FakeOwner owner;
    Body body = bodyFor(owner);
    FakeWorld world;
    world.script = {{level(Vec3{-1, 0, 0}, 0.5F)}};
    const auto out = coney::physics::sweepBody(kDt, world, body, Vec3{3, 0, 0});
    checkVec(owner.at, Vec3{0.04F, 0, 0});
    REQUIRE(out.has_value());
    checkVec(out.value_or(Vec3{}), Vec3{0.04F, 0, 0});
    CHECK(body.stuck == 1);
    REQUIRE(owner.velocities.size() == 2); // the stuck branch, then the write-back
    checkVec(owner.velocities[0], Vec3{});
    checkVec(owner.velocities[1], Vec3{});
    CHECK(owner.afterMoves == 1);
    CHECK((body.flags & coney::physics::kBodyDirty) != 0);
}

TEST_CASE("a stop answer ends the walk without being stuck; an abort ends everything", "[physics][body_sweep]") {
    FakeOwner owner;
    owner.answer = coney::physics::kAnswerStop | 1;
    Body body = bodyFor(owner);
    body.stuck = 5;
    FakeWorld world;
    world.script = {{level(Vec3{-1, 0, 0}, 0.5F)}};
    REQUIRE(coney::physics::sweepBody(kDt, world, body, Vec3{3, 0, 0}).has_value());
    checkVec(owner.at, Vec3{0.04F, 0, 0});
    CHECK(body.stuck == 0);
    REQUIRE(owner.velocities.size() == 1);

    FakeOwner aborting;
    aborting.answer = coney::physics::kAnswerAbort | 1;
    Body other = bodyFor(aborting);
    other.stuck = 5;
    FakeWorld world2;
    world2.script = {{level(Vec3{-1, 0, 0}, 0.5F)}};
    CHECK_FALSE(coney::physics::sweepBody(kDt, world2, other, Vec3{3, 0, 0}).has_value());
    checkVec(aborting.at, Vec3{0.04F, 0, 0});
    CHECK(aborting.velocities.empty());
    CHECK(other.stuck == 5);
    CHECK(aborting.afterMoves == 0);
}

TEST_CASE("a wall at an angle: advance along the normal, then the slid move in full", "[physics][body_sweep]") {
    for (const std::uint32_t flags : {coney::physics::kBodyWriteVelocity, std::uint32_t{0}}) {
        FakeOwner owner;
        Body body = bodyFor(owner, flags);
        FakeWorld world;
        world.script = {{level(Vec3{-0.6F, 0.8F, 0}, 0.5F)}};
        const auto out = coney::physics::sweepBody(kDt, world, body, Vec3{3, 0, 0});
        checkVec(owner.at, Vec3{0.076F, 0.032F, 0});
        REQUIRE(out.has_value());
        checkVec(out.value_or(Vec3{}), Vec3{0.076F, 0.032F, 0});
        CHECK(body.stuck == 0);
        if (flags != 0) {
            REQUIRE(owner.velocities.size() == 1);
            checkVec(owner.velocities[0], Vec3{1.92F, 1.44F, 0});
        } else {
            CHECK(owner.velocities.empty()); // an AI's velocity is not written back
        }
    }
    // A level triangle with flag 0x100 asks for no write-back.
    FakeOwner owner;
    Body body = bodyFor(owner);
    FakeWorld world;
    world.script = {{level(Vec3{-0.6F, 0.8F, 0}, 0.5F)}};
    world.flags[0] = 0x0101;
    REQUIRE(coney::physics::sweepBody(kDt, world, body, Vec3{3, 0, 0}).has_value());
    checkVec(owner.at, Vec3{0.076F, 0.032F, 0});
    CHECK(owner.velocities.empty());
}

TEST_CASE("a corner: two passes stop, the third has no move", "[physics][body_sweep]") {
    FakeOwner owner;
    Body body = bodyFor(owner);
    FakeWorld world;
    world.script = {{level(Vec3{-1, 0, 0}, 0.5F)}, {level(Vec3{0, -1, 0}, 0.5F)}};
    const auto out = coney::physics::sweepBody(kDt, world, body, Vec3{3, 3, 0});
    checkVec(owner.at, Vec3{0.04F, 0.04F, 0});
    CHECK(body.stuck == 1);
    REQUIRE(out.has_value());
    checkVec(out.value_or(Vec3{}), Vec3{0.04F, 0.04F, 0});
    REQUIRE_FALSE(owner.velocities.empty());
    checkVec(owner.velocities.front(), Vec3{});
}

TEST_CASE("three stopping passes leave the body at the advances, the slid move never made", "[physics][body_sweep]") {
    FakeOwner owner;
    Body body = bodyFor(owner);
    FakeWorld world;
    const Contact wall = level(Vec3{-0.6F, 0.8F, 0}, 0.5F);
    world.script = {{wall}, {wall}, {wall}};
    const auto out = coney::physics::sweepBody(kDt, world, body, Vec3{3, 0, 0});
    checkVec(owner.at, Vec3{0.012F, -0.016F, 0});
    CHECK(body.stuck == 1);
    REQUIRE(out.has_value());
    checkVec(out.value_or(Vec3{}), Vec3{0.012F, -0.016F, 0});
    REQUIRE_FALSE(owner.velocities.empty());
    checkVec(owner.velocities.front(), Vec3{});
}

TEST_CASE("code 0 with 0x20000 does nothing; the push-out moves once", "[physics][body_sweep]") {
    FakeOwner owner;
    owner.answer = coney::physics::kAnswerScale;
    Body body = bodyFor(owner);
    FakeWorld world;
    world.script = {{level(Vec3{-1, 0, 0}, 0.5F)}};
    REQUIRE(coney::physics::sweepBody(kDt, world, body, Vec3{3, 0, 0}).has_value());
    checkVec(owner.at, Vec3{0.1F, 0, 0});
    CHECK(owner.velocities.empty());
    CHECK(body.stuck == 0);

    // The push-out alone: over dt, over the push weight unless the body says not to; cleared afterwards.
    struct Case {
        float k;
        std::uint32_t flags;
        float x;
    };
    for (const Case c :
         {Case{1.0F, coney::physics::kBodyWriteVelocity, 0.02F}, Case{2.0F, coney::physics::kBodyWriteVelocity, 0.01F},
          Case{2.0F, coney::physics::kBodyWriteVelocity | coney::physics::kBodyPushUndivided, 0.02F}}) {
        FakeOwner pushed;
        pushed.pushWeight = c.k;
        Body pushedBody = bodyFor(pushed, c.flags);
        pushedBody.pushOut = Vec3{0.02F, 0, 0};
        FakeWorld empty;
        REQUIRE(coney::physics::sweepBody(kDt, empty, pushedBody, Vec3{}).has_value());
        checkVec(pushed.at, Vec3{c.x, 0, 0});
        checkVec(pushedBody.pushOut, Vec3{});
        CHECK(pushed.velocities.empty());
    }
}

TEST_CASE("too slow to move does nothing; slow but above the cut moves freely", "[physics][body_sweep]") {
    FakeOwner owner;
    Body body = bodyFor(owner, 0);
    body.pushOut = Vec3{0.00001F, 0, 0};
    FakeWorld world;
    CHECK_FALSE(coney::physics::sweepBody(kDt, world, body, Vec3{0.0005F, 0, 0}).has_value());
    checkVec(owner.at, Vec3{});
    checkVec(body.pushOut, Vec3{});
    CHECK((body.flags & coney::physics::kBodyDirty) == 0);
    CHECK(owner.afterMoves == 0);

    FakeOwner slow;
    Body slowBody = bodyFor(slow);
    slowBody.stuck = 3;
    FakeWorld scripted;
    scripted.script = {{level(Vec3{-1, 0, 0}, 0.5F)}};
    REQUIRE(coney::physics::sweepBody(kDt, scripted, slowBody, Vec3{0.02F, 0, 0}).has_value());
    CHECK(scripted.queries == 0);
    checkVec(slow.at, Vec3{0.02F / 30.0F, 0, 0});
    CHECK(slowBody.stuck == 0);
}

TEST_CASE("a world object bounces on a floor and keeps 0.98 of its speed", "[physics][body_sweep]") {
    FakeOwner owner;
    owner.answer = coney::physics::kAnswerScale | coney::physics::kCodeBounce;
    owner.restitution = 0.1F;
    Body body = bodyFor(owner, 0x80000500U);
    FakeWorld world;
    world.script = {{level(Vec3{0, 0, 1}, 0.4F)}};
    REQUIRE(coney::physics::sweepBody(kDt, world, body, Vec3{1, 0, -5}).has_value());
    checkVec(owner.at, Vec3{1.0F / 30.0F, 0, -0.04F});
    REQUIRE(owner.velocities.size() == 1);
    checkVec(owner.velocities[0], Vec3{0.98F, 0, 0.49F});
}

TEST_CASE("the bounce damps an outgoing normal speed too", "[physics][body_sweep]") {
    Vec3 out{0, 0, 2};
    coney::physics::bounce(out, Vec3{0, 0, 1}, 0.1F);
    checkVec(out, Vec3{0, 0, 1.8F});
    // Going in, `v −= n d (1 + e)`: out at e of the speed in, as the page's floor bounce (test 12, −5 to 0.5). The
    // page's test 13 gives 2.2 here, which its own formula does not (a question on the page).
    Vec3 in{0, 0, -2};
    coney::physics::bounce(in, Vec3{0, 0, 1}, 0.1F);
    checkVec(in, Vec3{0, 0, 0.2F});
    // The slide removes only a part going in.
    Vec3 away{1, 0, 0};
    coney::physics::removeNormalPart(away, Vec3{1, 0, 0});
    checkVec(away, Vec3{1, 0, 0});
    // The stand-alone advance.
    Vec3 point{};
    coney::physics::backOffPlane(point, Vec3{0.1F, 0, 0}, Vec3{-1, 0, 0}, 0.5F);
    checkVec(point, Vec3{0.04F, 0, 0});
}

TEST_CASE("the contact scale: the largest met, v.z only when rising, not the position", "[physics][body_sweep]") {
    struct Case {
        Vec3 velocity;
        coney::physics::ContactScale kind;
        bool airborne;
        Vec3 written;
    };
    for (const Case c :
         {Case{Vec3{3, 0, -1}, coney::physics::ContactScale::Generic, false, Vec3{1.8816F, 1.4112F, -1}},
          Case{Vec3{3, 0, 1}, coney::physics::ContactScale::Generic, false, Vec3{1.8816F, 1.4112F, 0.98F}},
          Case{Vec3{3, 0, -1}, coney::physics::ContactScale::Human, true, Vec3{0.48F, 0.36F, -1}},
          Case{Vec3{3, 0, -1}, coney::physics::ContactScale::Human, false, Vec3{1.92F, 1.44F, -1}}}) {
        FakeOwner owner;
        owner.answer = coney::physics::kAnswerScale | coney::physics::kCodeSlide;
        owner.inAir = c.airborne;
        Body body = bodyFor(owner);
        body.scaleKind = c.kind;
        FakeWorld world;
        world.script = {{level(Vec3{-0.6F, 0.8F, 0}, 0.5F)}};
        REQUIRE(coney::physics::sweepBody(kDt, world, body, c.velocity).has_value());
        REQUIRE(owner.velocities.size() == 1);
        checkVec(owner.velocities[0], c.written);
    }
    // A contact with a body scales 1.0, the level 0.98: the larger wins.
    FakeOwner owner;
    owner.answer = coney::physics::kAnswerScale | coney::physics::kCodeSlide;
    Body body = bodyFor(owner);
    FakeOwner other;
    Body otherBody = bodyFor(other);
    other.own = &otherBody;
    Contact withBody = level(Vec3{0, 1, 0}, 0.5F);
    withBody.triangle.reset();
    withBody.otherOwner = &other;
    FakeWorld world;
    world.script = {{level(Vec3{-0.6F, 0.8F, 0}, 0.5F)}, {withBody}};
    REQUIRE(coney::physics::sweepBody(kDt, world, body, Vec3{3, 0, 0}).has_value());
    REQUIRE_FALSE(owner.velocities.empty());
    CHECK(owner.velocities.back().x == Approx(1.92F).margin(kTolerance));
}

TEST_CASE("code 4 pushes the other body by half the move into it over the weights", "[physics][body_sweep]") {
    for (const float k : {1.0F, 1e9F}) {
        FakeOwner owner;
        owner.answer = coney::physics::kCodePush;
        Body body = bodyFor(owner);
        FakeOwner other;
        other.pushWeight = k;
        Body otherBody = bodyFor(other, 0);
        other.own = &otherBody;
        Contact contact = level(Vec3{-1, 0, 0}, 0.5F);
        contact.triangle.reset();
        contact.otherOwner = &other;
        FakeWorld world;
        world.script = {{contact}};
        REQUIRE(coney::physics::sweepBody(kDt, world, body, Vec3{3, 0, 0}).has_value());
        CHECK(otherBody.pushOut.x == Approx(0.05F / k).margin(1e-9));
        REQUIRE(other.pushes.size() == 1);
        checkVec(owner.at, Vec3{0.04F, 0, 0});
    }
    // An unpushable body only slides the mover.
    FakeOwner owner;
    owner.answer = coney::physics::kCodePush;
    Body body = bodyFor(owner);
    FakeOwner other;
    Body otherBody = bodyFor(other, coney::physics::kBodyNotPushable);
    other.own = &otherBody;
    Contact contact = level(Vec3{-1, 0, 0}, 0.5F);
    contact.triangle.reset();
    contact.otherOwner = &other;
    FakeWorld world;
    world.script = {{contact}};
    REQUIRE(coney::physics::sweepBody(kDt, world, body, Vec3{3, 0, 0}).has_value());
    checkVec(otherBody.pushOut, Vec3{});
    CHECK(other.pushes.empty());
    checkVec(owner.at, Vec3{0.04F, 0, 0});
}

TEST_CASE("the corner slide joins the move only; the stored slide waits for the next move", "[physics][body_sweep]") {
    for (const Vec3 slide : {Vec3{0, 0.5F, 0}, Vec3{-0.5F, 0.5F, 0}}) {
        FakeOwner owner;
        owner.answer = coney::physics::kAnswerScale | coney::physics::kCodeSlide;
        Body body = bodyFor(owner);
        FakeWorld world;
        world.script = {{level(Vec3{-1, 0, 0}, 0.5F, coney::physics::kContactAddSlide, slide)}};
        REQUIRE(coney::physics::sweepBody(kDt, world, body, Vec3{3, 0, 0}).has_value());
        checkVec(owner.at, Vec3{0.04F, 0.5F / 30.0F, 0});
        CHECK(body.stuck == 0);
        REQUIRE(owner.velocities.size() == 1);
        checkVec(owner.velocities[0], Vec3{});
    }
    // A body that takes no corner slide is stuck as head-on.
    FakeOwner owner;
    Body body = bodyFor(owner, coney::physics::kBodyWriteVelocity | coney::physics::kBodyNoCornerSlide);
    FakeWorld world;
    world.script = {{level(Vec3{-1, 0, 0}, 0.5F, coney::physics::kContactAddSlide, Vec3{0, 0.5F, 0})}};
    REQUIRE(coney::physics::sweepBody(kDt, world, body, Vec3{3, 0, 0}).has_value());
    CHECK(body.stuck == 1);

    // The stored slide: the first of the move is kept as the next push-out.
    FakeOwner storing;
    Body storingBody = bodyFor(storing);
    FakeWorld stored;
    stored.script = {{level(Vec3{-0.6F, 0.8F, 0}, 0.5F, coney::physics::kContactStoreSlide, Vec3{0, 0.05F, 0})},
                     {level(Vec3{0, -1, 0}, 0.5F, coney::physics::kContactStoreSlide, Vec3{0.05F, 0, 0})}};
    REQUIRE(coney::physics::sweepBody(kDt, stored, storingBody, Vec3{3, 0, 0}).has_value());
    checkVec(storingBody.pushOut, Vec3{0, 0.05F, 0});
}

TEST_CASE("the landing segment's fraction is measured beyond 1 m from its top", "[physics][body_sweep]") {
    const auto ground = coney::raycast::buildCollisionMesh(
        std::vector<coney::raycast::BuildTriangle>{
            {.corners = {coney::raycast::Vec3{-5, -5, 0}, coney::raycast::Vec3{5, -5, 0},
                         coney::raycast::Vec3{5, 5, 0}}},
            {.corners = {coney::raycast::Vec3{-5, -5, 0}, coney::raycast::Vec3{5, 5, 0},
                         coney::raycast::Vec3{-5, 5, 0}}}},
        2.0F);
    REQUIRE(ground.has_value());
    const auto hit = coney::physics::landingContact(**ground, Vec3{0, 0, 0.3F}, Vec3{0, 0, 1.18F}, Vec3{0, 0, -0.5F});
    REQUIRE(hit.has_value());
    // value_or after the REQUIRE: clang-tidy cannot see that REQUIRE ends the test on an empty optional.
    const coney::physics::Contact found = hit.value_or(coney::physics::Contact{});
    CHECK(found.fraction == Approx(0.18F / 0.38F).margin(1e-4));
    checkVec(found.normal, Vec3{0, 0, 1});
    const auto near = coney::physics::landingContact(**ground, Vec3{0, 0, 0.05F}, Vec3{0, 0, 0.93F}, Vec3{0, 0, -0.2F});
    REQUIRE(near.has_value());
    coney::physics::Contact missing;
    missing.fraction = -1.0F;
    CHECK(near.value_or(missing).fraction == 0.0F);
}

TEST_CASE("the sweep's box helpers", "[physics][body_sweep]") {
    Box box{Vec3{0, 0, 0}, Vec3{2, 2, 2}};
    coney::physics::scaleAboutCentre(box, Vec3{1.5F, 1, -1});
    checkVec(box.min, Vec3{-0.5F, 0, 0});
    checkVec(box.max, Vec3{2.5F, 2, 2});
    Box same{Vec3{0, 0, 0}, Vec3{2, 2, 2}};
    coney::physics::scaleAboutCentre(same, Vec3{1, 1, 1});
    checkVec(same.max, Vec3{2, 2, 2});
    Box moved{Vec3{0, 0, 0}, Vec3{1, 1, 1}};
    coney::physics::extendByMove(moved, Vec3{2, -1, 0});
    checkVec(moved.min, Vec3{0, -1, 0});
    checkVec(moved.max, Vec3{3, 1, 1});
}

TEST_CASE("a capsule body moved through the mesh world stops 0.01 m short of a wall it would cross",
          "[physics][body_sweep]") {
    // A 1 m move in one update toward a wall 0.8 m ahead: the walking sphere (0.5 m) touches it after 0.3 m, is
    // advanced to 0.01 m short, its move slid to nothing, and is stuck (docs/research/physics-bodies.md#u3-sweep).
    using Rv = coney::raycast::Vec3;
    const std::vector<coney::raycast::BuildTriangle> wall{
        coney::raycast::BuildTriangle{.corners = {Rv{0.8F, -5, 0}, Rv{0.8F, 5, 3}, Rv{0.8F, 5, 0}}},
        coney::raycast::BuildTriangle{.corners = {Rv{0.8F, -5, 0}, Rv{0.8F, -5, 3}, Rv{0.8F, 5, 3}}}};
    auto mesh = coney::raycast::buildCollisionMesh(wall, 2.0F);
    REQUIRE(mesh.has_value());
    coney::physics::MeshScratch scratch;
    coney::physics::MeshWorld world(**mesh, scratch);
    FakeOwner owner;
    owner.mask = coney::physics::kTypeHuman;
    Body body = bodyFor(owner);
    body.shape.type = coney::physics::ShapeType::Capsule;
    const auto out = coney::physics::sweepBody(kDt, world, body, Vec3{30, 0, 0});
    REQUIRE(out.has_value());
    checkVec(out.value_or(Vec3{}), Vec3{0.29F, 0, 0});
    CHECK(body.stuck == 1);
    REQUIRE(!owner.velocities.empty());
    checkVec(owner.velocities.back(), Vec3{});
    // Moving away from it, nothing is met: the whole move is made.
    owner.at = Vec3{};
    owner.velocities.clear();
    const auto away = coney::physics::sweepBody(kDt, world, body, Vec3{-30, 0, 0});
    REQUIRE(away.has_value());
    checkVec(away.value_or(Vec3{}), Vec3{-1, 0, 0});
    CHECK(body.stuck == 0);
    CHECK(owner.velocities.empty());
}
