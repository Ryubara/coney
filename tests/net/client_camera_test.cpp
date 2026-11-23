// SPDX-License-Identifier: GPL-3.0-or-later
// The client's own camera (docs/guides/client-world.md#client-camera): Coney's follow camera run on the human a
// client's player slot follows, from the humans the client's world shows, with no display and no real time: what it
// reads of a replicated human, where it stands behind him, how it trails him, the client's own right stick, a cut to
// another human, the frames between two steps, and the whole of it over the loopback through a bad link.
#include "net/client_camera.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <numbers>
#include <optional>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "camera/camera_view.h"
#include "camera/follow_camera.h"
#include "human/locomotion.h"
#include "net/client_world.h"
#include "net/link_conditioner.h"
#include "net/loopback_transport.h"
#include "net/net_client.h"
#include "net/net_id.h"
#include "net/net_server.h"
#include "net/snapshot.h"
#include "support/optional_value.h"

using Catch::Approx;
using coney::anim::Vec3;
using namespace coney::net;

namespace {

// The id of the `slot`th human, generation 1.
NetId idOf(std::uint16_t slot) { return NetId{.slot = slot, .generation = 1}; }

// A human standing at `position`, facing `heading`.
HumanState humanAt(Vec3 position, float heading = 0.0F) {
    HumanState human;
    human.position = position;
    human.heading = heading;
    human.health = 150;
    human.healthMax = 150;
    human.layers[0] = HumanAnimLayer{.clip = 388, .playhead = 0.0F, .gait = 0.0F, .fade = 1.0F};
    human.grounded = true;
    return human;
}

// A character's gait speeds, as the locomotion clips give a player's (the order of human::Speeds).
const coney::human::Speeds kSpeeds{.base = 1.0F, .sneak = 0.8F, .walk = 1.6F, .jog = 3.0F, .run = 4.6F, .sprint = 7.0F};

// The follow camera's time step, and the metres between the camera and a human along the ground.
constexpr float kStep = 1.0F / 30.0F;
float groundDistance(Vec3 a, Vec3 b) { return std::hypot(a.x - b.x, a.y - b.y); }

// A server's snapshots reaching a client's world with no network between: each step's humans sent, decoded and
// acknowledged at once, then the client's step (the buffer's clock on one step, its playback some steps behind).
struct Feed {
    SnapshotSender sender;
    SnapshotReceiver receiver;
    ClientWorld world;
    std::uint32_t step = 0;

    // The server's step with `humans`, then the client's.
    void run(const std::map<NetId, HumanState>& humans) {
        std::vector<SnapshotHuman> sent;
        sent.reserve(humans.size());
        for (const auto& [id, human] : humans) {
            sent.push_back(SnapshotHuman{.id = id, .words = humanWords(human), .priority = 1});
        }
        const Snapshot snapshot = sender.build(step++, sent);
        REQUIRE(receiver.apply(snapshot) == SnapshotApplied::Applied);
        REQUIRE(sender.acknowledge(snapshot.sequence).has_value());
        world.receive(receiver);
        world.step();
    }
};

// The input of a client step with the pad at rest.
ClientCameraInput restInput() {
    return ClientCameraInput{.speeds = kSpeeds,
                             .rawRightX = kClientStickRest,
                             .rawRightY = kClientStickRest,
                             .mesh = nullptr,
                             .seconds = kStep};
}

// The camera's view direction on the ground, as an angle (radians, anticlockwise from +x seen from above).
float yawOf(const coney::camera::CameraView& view) {
    const Vec3 forward = coney::camera::viewForward(view);
    return std::atan2(forward.y, forward.x);
}

} // namespace

// Expected: unverified, #1333 (Coney's own: the stand-in of docs/guides/client-world.md#client-camera, the host's
// player 1 being the human with the lowest network id)
TEST_CASE("client camera: every player slot follows the host's player 1, the lowest network id, until the snapshots "
          "name the human of each",
          "[net][client_camera]") {
    ClientWorld::Humans humans;
    CHECK_FALSE(followedHuman(humans, 1).has_value());
    humans.emplace(idOf(7), humanAt(Vec3{1.0F, 0.0F, 0.0F}));
    humans.emplace(idOf(2), humanAt(Vec3{2.0F, 0.0F, 0.0F}));
    humans.emplace(idOf(4), humanAt(Vec3{3.0F, 0.0F, 0.0F}));
    for (const std::uint8_t slot : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{3}}) {
        CHECK(followedHuman(humans, slot) == std::optional<NetId>{idOf(2)});
    }
}

// Expected: unverified, #1333 (Coney's own: what the follow camera reads of a human, Human::gait() and
// followTargetOf() in src/human/player.cpp, from the replicated fields)
TEST_CASE("client camera: a replicated human becomes the follow camera's target with his velocity from the move "
          "between two steps",
          "[net][client_camera]") {
    const HumanState before = humanAt(Vec3{0.0F, 10.0F, 0.0F}, 0.5F);
    HumanState now = humanAt(Vec3{0.0F, 10.0F + (4.6F * kStep), 0.0F}, 0.5F);

    // A run: the speed the move shows, the gait it is nearest, his feet and facing as they are.
    const coney::camera::FollowTarget running = followTargetOf(now, &before, kSpeeds, kStep);
    CHECK(running.velocity.y == Approx(4.6F).margin(1e-3));
    CHECK(running.gait == static_cast<std::uint8_t>(coney::human::Gait::Run));
    CHECK(running.feet.y == Approx(now.position.y));
    CHECK(running.heading == Approx(0.5F));
    CHECK(running.walkSpeed == Approx(kSpeeds.walk));
    CHECK(running.runSpeed == Approx(kSpeeds.run));
    CHECK(running.sprintSpeed == Approx(kSpeeds.sprint));
    CHECK_FALSE(running.airborne);
    CHECK(running.counts);
    CHECK_FALSE(running.down);
    CHECK_FALSE(running.nearestEnemy.has_value());
    CHECK_FALSE(running.lockOn);

    // A human new in the step has no velocity: he stands.
    const coney::camera::FollowTarget fresh = followTargetOf(now, nullptr, kSpeeds, kStep);
    CHECK(coney::anim::length(fresh.velocity) == Approx(0.0F));
    CHECK(fresh.gait == static_cast<std::uint8_t>(coney::human::Gait::Standing));

    // A jump (a respawn, a teleport) is not a speed.
    now.position = Vec3{0.0F, 200.0F, 0.0F};
    const coney::camera::FollowTarget jumped = followTargetOf(now, &before, kSpeeds, kStep);
    CHECK(coney::anim::length(jumped.velocity) == Approx(0.0F));

    // In the air, and knocked out or out of health: not on the ground, and he does not count for the cameras.
    HumanState airborne = humanAt(Vec3{}, 0.0F);
    airborne.grounded = false;
    CHECK(followTargetOf(airborne, nullptr, kSpeeds, kStep).airborne);
    HumanState out = humanAt(Vec3{}, 0.0F);
    out.knockedOut = true;
    const coney::camera::FollowTarget knockedOut = followTargetOf(out, nullptr, kSpeeds, kStep);
    CHECK_FALSE(knockedOut.counts);
    CHECK(knockedOut.down);
    HumanState dead = humanAt(Vec3{}, 0.0F);
    dead.health = 0;
    CHECK_FALSE(followTargetOf(dead, nullptr, kSpeeds, kStep).counts);
}

// Expected: unverified, #1333 (Coney's own; the placing behind is camera::FollowCamera's, docs/research/camera.md)
TEST_CASE("client camera: with no human it has no view, and with one it stands behind him looking at him",
          "[net][client_camera]") {
    Feed feed;
    ClientCamera camera;
    // Before the buffer's clock runs the world shows nobody.
    camera.step(feed.world, followedHuman(feed.world.humans(), 1), restInput());
    CHECK_FALSE(camera.followed().has_value());
    CHECK_FALSE(camera.view(1.0F).has_value());
    CHECK(camera.cuts() == 0);

    // A human standing at (10, 20, 0) facing +y.
    const Vec3 feet{10.0F, 20.0F, 0.0F};
    for (int i = 0; i < 20; ++i) {
        feed.run({{idOf(3), humanAt(feet)}});
        camera.step(feed.world, followedHuman(feed.world.humans(), 1), restInput());
    }
    REQUIRE(camera.followed() == std::optional<NetId>{idOf(3)});
    CHECK(camera.cuts() == 1);
    const std::optional<coney::camera::CameraView> view = camera.view(1.0F);
    REQUIRE(view.has_value());
    const coney::camera::CameraView seen = coney::test::got(view);
    const coney::camera::FollowCamera* follow = camera.follow();
    REQUIRE(follow != nullptr);
    // Behind him (he faces +y, so the eye has a lower y), above his feet, at the leash band's distance, and looking at
    // him along the ground.
    CHECK(seen.position.y < feet.y);
    CHECK(seen.position.x == Approx(feet.x).margin(0.05));
    CHECK(seen.position.z > feet.z);
    const float away = groundDistance(seen.position, feet);
    CHECK(away >= follow->bandNear() * 0.8F);
    CHECK(away <= follow->bandFar() + 0.5F);
    CHECK(yawOf(seen) == Approx(std::numbers::pi_v<float> / 2.0F).margin(0.05));
}

// Expected: unverified, #1333 (Coney's own; the leash band is Cam_Follow's, docs/research/camera.md#leash)
TEST_CASE("client camera: a human running away is trailed within the leash band and the camera keeps him in view",
          "[net][client_camera]") {
    Feed feed;
    ClientCamera camera;
    Vec3 feet{0.0F, 0.0F, 0.0F};
    float farthest = 0.0F;
    float nearest = 100.0F;
    int checked = 0;
    for (int i = 0; i < 240; ++i) {
        // A run along +y at the run speed, after 30 steps of standing.
        if (i >= 30) {
            feet.y += 4.6F * kStep;
        }
        feed.run({{idOf(0), humanAt(feet)}});
        camera.step(feed.world, followedHuman(feed.world.humans(), 1), restInput());
        // The human as the world shows him this step: the camera's own target.
        const auto shown = feed.world.humans().find(idOf(0));
        if (i < 60 || shown == feed.world.humans().end()) {
            continue;
        }
        const coney::camera::CameraView view = coney::test::got(camera.view(1.0F));
        const float away = groundDistance(view.position, shown->second.position);
        farthest = std::max(farthest, away);
        nearest = std::min(nearest, away);
        // He is ahead of the camera, within a small angle of the middle of the view: the camera's yaw is on +y.
        CHECK(std::abs(yawOf(view) - (std::numbers::pi_v<float> / 2.0F)) < 0.2F);
        CHECK(shown->second.position.y > view.position.y);
        ++checked;
    }
    REQUIRE(checked > 100);
    const coney::camera::FollowCamera* follow = camera.follow();
    REQUIRE(follow != nullptr);
    // Over the run the camera moved with him (nearly all of his 30 m), never nearer than the band's near edge less
    // what the lag lets it close in by, never farther than the far edge and the lag of a sprint.
    CHECK(coney::test::got(camera.view(1.0F)).position.y > 20.0F);
    CHECK(nearest >= follow->bandNear() - 1.0F);
    CHECK(farthest <= follow->bandFar() + 2.0F);
}

// Expected: unverified, #1333 (Coney's own; the right stick's turn is Cam_Follow's, camera.md#right-stick, 150 degrees
// a second at the stick's end, positive turning the view anticlockwise)
TEST_CASE("client camera: the client's own right stick turns its own view and the human's state is untouched",
          "[net][client_camera]") {
    const auto yawAfter = [](std::uint8_t rawRightX) {
        Feed feed;
        ClientCamera camera;
        ClientCameraInput input = restInput();
        for (int i = 0; i < 90; ++i) {
            // The stick is held for the last fifteen steps: half a second.
            input.rawRightX = i >= 75 ? rawRightX : kClientStickRest;
            feed.run({{idOf(0), humanAt(Vec3{0.0F, 0.0F, 0.0F})}});
            camera.step(feed.world, followedHuman(feed.world.humans(), 1), input);
        }
        const float yaw = yawOf(coney::test::got(camera.view(1.0F)));
        // He stays where the world has him, wherever the camera looks.
        CHECK(feed.world.humans().at(idOf(0)).position.y == Approx(0.0F).margin(0.01));
        return yaw;
    };
    const float rest = yawAfter(kClientStickRest);
    const float left = yawAfter(0);
    const float right = yawAfter(255);
    CHECK(rest == Approx(std::numbers::pi_v<float> / 2.0F).margin(0.05));
    // Half a second of the stick turns the view 0.94 radians (54 degrees, measured: the camera covers a share of its
    // wanted move each update, so less than the 75 degrees of 150 a second) to the left, and as far the other way.
    CHECK(std::remainder(left - rest, 2.0F * std::numbers::pi_v<float>) > 0.8F);
    CHECK(std::remainder(right - rest, 2.0F * std::numbers::pi_v<float>) < -0.8F);
}

// Expected: unverified, #1333 (Coney's own: a camera made behind each human it follows, the frames between two steps
// lerped as the play mode lerps its player's snapshots)
TEST_CASE("client camera: a human followed after another cuts to behind him, and a frame between two steps is "
          "between their views",
          "[net][client_camera]") {
    Feed feed;
    ClientCamera camera;
    // Two humans far apart; the lowest id is followed, and then leaves the world.
    const std::map<NetId, HumanState> both{{idOf(1), humanAt(Vec3{0.0F, 0.0F, 0.0F})},
                                           {idOf(2), humanAt(Vec3{100.0F, 100.0F, 0.0F}, 1.0F)}};
    for (int i = 0; i < 20; ++i) {
        feed.run(both);
        camera.step(feed.world, followedHuman(feed.world.humans(), 1), restInput());
    }
    REQUIRE(camera.followed() == std::optional<NetId>{idOf(1)});
    CHECK(camera.cuts() == 1);
    const Vec3 first = coney::test::got(camera.view(1.0F)).position;
    CHECK(groundDistance(first, Vec3{0.0F, 0.0F, 0.0F}) < 7.0F);

    // He leaves: the world, in its playback's time, shows the other; the camera is put behind him in one step, and the
    // frame before it is the same view (no swing across the level).
    for (int i = 0; i < 20; ++i) {
        feed.run({{idOf(2), humanAt(Vec3{100.0F, 100.0F, 0.0F}, 1.0F)}});
        camera.step(feed.world, followedHuman(feed.world.humans(), 1), restInput());
    }
    REQUIRE(camera.followed() == std::optional<NetId>{idOf(2)});
    CHECK(camera.cuts() == 2);
    const coney::camera::CameraView cutTo = coney::test::got(camera.view(1.0F));
    CHECK(groundDistance(cutTo.position, Vec3{100.0F, 100.0F, 0.0F}) < 7.0F);

    // A frame between two steps lies between the two views the last step has; at the ends it is exactly either.
    ClientCamera moving;
    Feed other;
    Vec3 feet{0.0F, 0.0F, 0.0F};
    for (int i = 0; i < 60; ++i) {
        feet.y += 4.6F * kStep;
        other.run({{idOf(0), humanAt(feet)}});
        moving.step(other.world, followedHuman(other.world.humans(), 1), restInput());
    }
    const Vec3 older = coney::test::got(moving.view(0.0F)).position;
    const Vec3 newer = coney::test::got(moving.view(1.0F)).position;
    const Vec3 middle = coney::test::got(moving.view(0.5F)).position;
    CHECK(newer.y > older.y);
    CHECK(middle.y == Approx((older.y + newer.y) / 2.0F).margin(1e-4));
    CHECK(middle.x == Approx((older.x + newer.x) / 2.0F).margin(1e-4));
}

namespace {

constexpr std::uint64_t kStepNs = 33'333'333; // one 30 Hz step

// A host and one guest on a loopback network, each behind a link conditioner, stepped together as the main loop does.
struct Session {
    LoopbackNetwork network;
    LoopbackTransport hostWire{network, "host"};
    LinkConditioner hostLink{hostWire, 7};
    NetServer server;
    LoopbackTransport guestWire{network, "guest"};
    LinkConditioner guestLink{guestWire, 99};
    NetClient client;

    Session() : server(hostLink, serverConfig()), client(guestLink, ClientConfig{}) {
        REQUIRE(server.start().has_value());
        REQUIRE(client.connect("host", server.port()).has_value());
    }

    // The tests' server: any free port.
    static ServerConfig serverConfig() {
        ServerConfig config;
        config.port = 0;
        return config;
    }

    // Runs `steps` fixed steps; `afterReceive` runs between each step's receiving and its sending.
    void run(int steps, const std::function<void()>& afterReceive) {
        for (int i = 0; i < steps; ++i) {
            server.advance(kStepNs);
            client.advance(kStepNs);
            server.receive();
            client.receive();
            afterReceive();
            server.send();
            client.send();
        }
    }
};

} // namespace

// Expected: unverified, #1333 (Coney's own: the camera over the client's jitter buffer, #555, through a bad link)
TEST_CASE("client camera: over a bad link the client's own camera follows its player's replicated path within the "
          "follow camera's rules",
          "[net][client_camera][sessions]") {
    Session session;
    session.run(3, [] {});
    REQUIRE(session.client.state() == ClientState::Connected);
    const LinkConditions bad{.latencyMs = 20,
                             .jitterMs = 15,
                             .lossFraction = 0.05F,
                             .duplicateFraction = 0.05F,
                             .reorderFraction = 0.1F,
                             .reorderDelayMs = 30};
    session.hostLink.setConditions(bad);
    session.guestLink.setConditions(bad);

    ClientWorld world;
    ClientCamera camera;
    std::uint32_t step = 0;
    int checked = 0;
    float farthest = 0.0F;
    float nearest = 100.0F;
    float worstOff = 0.0F;
    const auto stepGame = [&] {
        // The client's step: what arrived goes into the buffer, the buffer's time is drawn, and the camera follows the
        // human of its slot as the world shows him.
        world.receive(session.client.snapshots());
        world.step();
        camera.step(world, followedHuman(world.humans(), 1), restInput());
        const std::optional<NetId> followed = camera.followed();
        if (step >= 90 && followed) {
            const HumanState& human = world.humans().at(*followed);
            const coney::camera::CameraView view = coney::test::got(camera.view(1.0F));
            const float away = groundDistance(view.position, human.position);
            farthest = std::max(farthest, away);
            nearest = std::min(nearest, away);
            // How far off the middle of the view the human's chest is.
            const Vec3 chest{human.position.x, human.position.y, human.position.z + 1.0F};
            const Vec3 toChest = coney::anim::normalise(coney::anim::subtract(chest, view.position));
            const float cosine = coney::anim::dot(toChest, coney::camera::viewForward(view));
            worstOff = std::max(worstOff, std::acos(std::min(cosine, 1.0F)));
            ++checked;
        }
        // The server's step: the player runs round a circle of 15 m, turning as he goes, and a second human walks.
        const auto t = static_cast<float>(step);
        const float angle = 0.02F * t;
        const std::vector<SnapshotHuman> humans{
            {.id = idOf(0),
             .words = humanWords(humanAt(Vec3{15.0F * std::cos(angle), 15.0F * std::sin(angle), 0.0F},
                                         angle + (std::numbers::pi_v<float> / 2.0F))),
             .priority = 1},
            {.id = idOf(1), .words = humanWords(humanAt(Vec3{40.0F + (0.05F * t), 0.0F, 0.0F})), .priority = 1}};
        session.server.sendSnapshots(step, humans);
        ++step;
    };
    session.run(400, stepGame);
    REQUIRE(session.client.state() == ClientState::Connected);

    // It followed the first human (the lowest id) all the way, from the first step the world showed him, and was made
    // behind him once.
    CHECK(camera.followed() == std::optional<NetId>{idOf(0)});
    CHECK(camera.cuts() == 1);
    REQUIRE(checked > 250);
    const coney::camera::FollowCamera* follow = camera.follow();
    REQUIRE(follow != nullptr);
    // The camera never came nearer than the leash's near edge less its lag, nor beyond the far edge with it, and kept
    // the human within 40 degrees of the middle of the view round the whole circle.
    CHECK(nearest >= follow->bandNear() - 1.5F);
    CHECK(farthest <= follow->bandFar() + 2.5F);
    CHECK(worstOff < 0.7F);
}
