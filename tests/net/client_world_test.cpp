// SPDX-License-Identifier: GPL-3.0-or-later
// The client's world (docs/guides/client-world.md): two snapshots' humans blended (positions, headings, playheads
// round their clips, layers pushed down by a new one, jumps), the client's steps through Interpolated and DrawnPoses,
// a human's pose evaluated from its replicated layers as its task stack sampled it, and what a client draws against
// what the server had, over the loopback through a bad link.
#include "net/client_world.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <set>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_clip.h"
#include "animation/anim_math.h"
#include "animation/anim_pose.h"
#include "animation/anim_task.h"
#include "net/jitter_buffer.h"
#include "net/link_conditioner.h"
#include "net/loopback_transport.h"
#include "net/net_client.h"
#include "net/net_id.h"
#include "net/net_server.h"
#include "net/snapshot.h"

using Catch::Approx;
using coney::anim::AnimClip;
using coney::anim::Quat;
using coney::anim::Vec3;
using namespace coney::net;

namespace {

// The id of the `slot`th human, generation 1.
NetId idOf(std::uint16_t slot) { return NetId{.slot = slot, .generation = 1}; }

// A standing human at `x`, playing clip 5 at `playhead` alone.
HumanState standing(float x, float playhead = 0.0F) {
    HumanState human;
    human.position = Vec3{x, 0.0F, 0.0F};
    human.health = 150;
    human.healthMax = 150;
    human.layers[0] = HumanAnimLayer{.clip = 5, .playhead = playhead, .gait = 0.0F, .fade = 1.0F};
    human.grounded = true;
    return human;
}

// A clip of `duration` seconds whose bone 2 turns about z from 0 to `endAngle` radians over its length, and whose
// bone 3 holds `bone3Angle` throughout: enough to tell clips and times apart in a pose.
AnimClip testClip(float duration, float endAngle, float bone3Angle = 0.0F) {
    AnimClip clip;
    clip.duration = duration;
    const auto lastFrame = static_cast<std::uint32_t>(duration * coney::anim::kClipFrameRate);
    const float half = endAngle * 0.5F;
    clip.rotations = {
        {{.frame = 0, .value = Quat{}},
         {.frame = lastFrame, .value = Quat{0.0F, 0.0F, std::sin(half), std::cos(half)}}},
        {{.frame = 0, .value = Quat{0.0F, 0.0F, std::sin(bone3Angle * 0.5F), std::cos(bone3Angle * 0.5F)}}}};
    clip.rotationBones = {2, 3};
    clip.boneMask = (1U << 2U) | (1U << 3U);
    return clip;
}

// The identity bind pose.
const std::array<Quat, coney::anim::kPoseBones> kBind{};

// A clip source over a fixed set of clips by anim id, with one gait set.
class TestClips final : public ClipSource {
  public:
    std::map<std::int32_t, AnimClip> clips;
    std::array<std::int32_t, 5> gaitIds{};

    [[nodiscard]] const AnimClip* clip(std::int32_t animId) const override {
        const auto found = clips.find(animId);
        return found == clips.end() ? nullptr : &found->second;
    }
    [[nodiscard]] std::optional<std::array<coney::anim::GaitClip, 5>> gaitClips(std::int32_t animId) const override {
        if (std::ranges::find(gaitIds, animId) == gaitIds.end()) {
            return std::nullopt;
        }
        std::array<coney::anim::GaitClip, 5> set{};
        for (std::size_t i = 0; i < set.size(); ++i) {
            set.at(i) =
                coney::anim::GaitClip{.clip = clip(gaitIds.at(i)), .animId = static_cast<std::uint32_t>(gaitIds.at(i))};
        }
        return set;
    }
};

// Two poses' rotations of bones 2 and 3 agree.
void checkSamePose(const coney::anim::Pose& got, const coney::anim::Pose& expected) {
    for (const std::size_t bone : {std::size_t{2}, std::size_t{3}}) {
        CHECK(got.rotations.at(bone).x == Approx(expected.rotations.at(bone).x).margin(1e-5));
        CHECK(got.rotations.at(bone).y == Approx(expected.rotations.at(bone).y).margin(1e-5));
        CHECK(got.rotations.at(bone).z == Approx(expected.rotations.at(bone).z).margin(1e-5));
        CHECK(got.rotations.at(bone).w == Approx(expected.rotations.at(bone).w).margin(1e-5));
    }
}

} // namespace

// Expected: unverified, #555 (Coney's own; the blends are coney::lerp, lerpAngle and lerpLooping)
TEST_CASE("client world: two snapshots' humans blend the position, the heading the short way and the playhead round "
          "its clip",
          "[net][client_world]") {
    HumanState from = standing(0.0F, 0.9F);
    from.heading = 3.0F;
    HumanState to = standing(1.0F, 0.1F);
    to.position.y = 2.0F;
    to.heading = -3.0F;
    to.health = 120;

    const HumanState half = blendHumans(from, to, 0.5F);
    CHECK(half.position.x == Approx(0.5F));
    CHECK(half.position.y == Approx(1.0F));
    // 3.0 to -3.0 the short way goes through pi.
    CHECK(std::abs(std::remainder(half.heading - std::numbers::pi_v<float>, 2.0F * std::numbers::pi_v<float>)) < 1e-4F);
    // 0.9 to 0.1 on the same clip wrapped round its end: halfway is its start.
    CHECK(std::min(half.layers[0].playhead, 1.0F - half.layers[0].playhead) < 1e-4F);
    CHECK(half.layers[0].clip == 5);
    CHECK(half.health == 120); // discrete: the newer snapshot's

    // The ends are exact.
    CHECK(blendHumans(from, to, 1.0F) == to);
    CHECK(blendHumans(from, to, 0.0F) == from);
}

// Expected: unverified, #555 (Coney's own; a jump is DrawnPoses' kMostBlendedMetres, 10 m)
TEST_CASE("client world: a jump, a new clip and a new layer's fade are drawn as the newer snapshot has them, and the "
          "layers under a new one blend on",
          "[net][client_world]") {
    // A teleport of 20 m is drawn where it lands, playhead and all.
    const HumanState from = standing(0.0F, 0.2F);
    HumanState far = standing(20.0F, 0.6F);
    CHECK(blendHumans(from, far, 0.5F) == far);

    // A new clip on the only layer, with no fade (a cut): its playhead is the newer one's.
    HumanState cut = standing(0.0F, 0.7F);
    cut.layers[0].clip = 9;
    CHECK(blendHumans(from, cut, 0.5F).layers[0].playhead == 0.7F);

    // A new clip fading in over clip 5: clip 5 is pushed down to layer 1 and still plays on, its playhead blended;
    // the new layer and its fade are the newer snapshot's.
    HumanState fading = standing(0.0F, 0.0F);
    fading.layers[0] = HumanAnimLayer{.clip = 9, .playhead = 0.1F, .gait = 0.0F, .fade = 1.0F};
    fading.layers[1] = HumanAnimLayer{.clip = 5, .playhead = 0.3F, .gait = 0.0F, .fade = 0.2F};
    HumanState blended = blendHumans(from, fading, 0.5F);
    CHECK(blended.layers[0].clip == 9);
    CHECK(blended.layers[0].playhead == 0.1F);
    CHECK(blended.layers[1].clip == 5);
    CHECK(blended.layers[1].playhead == Approx(0.25F));
    CHECK(blended.layers[1].fade == 0.2F);
    CHECK(playingLayers(fading) == 2);

    // As the fade runs on, both layers' playheads and the fade blend.
    HumanState further = fading;
    further.layers[0].playhead = 0.2F;
    further.layers[1].playhead = 0.4F;
    further.layers[1].fade = 0.6F;
    blended = blendHumans(fading, further, 0.5F);
    CHECK(blended.layers[0].playhead == Approx(0.15F));
    CHECK(blended.layers[1].playhead == Approx(0.35F));
    CHECK(blended.layers[1].fade == Approx(0.4F));

    // The words round-trip within the fields' steps.
    const HumanState decoded = humanState(humanWords(further));
    CHECK(humanWords(decoded) == humanWords(further));
    CHECK(decoded.layers[1].fade == Approx(0.6F).margin(1e-3));
    CHECK(decoded.layers[1].clip == 5);
}

// Expected: unverified, #555 (Coney's own; a gait blend's clip is GaitBlendTask::animId(), the leading clip of its
// pair, so it changes while the value and phase run on)
TEST_CASE("client world: a gait blend crossing from the walk into the jog blends on, and names the clip its value "
          "plays",
          "[net][client_world]") {
    // A walk speeding into a jog: the lower pair's walk leads at 0.9, the jog pair's jog at 1.1.
    HumanState from = standing(0.0F);
    from.layers[0] = HumanAnimLayer{.clip = 20, .playhead = 0.2F, .gait = 0.9F, .fade = 1.0F};
    HumanState to = standing(0.1F);
    to.layers[0] = HumanAnimLayer{.clip = 21, .playhead = 0.3F, .gait = 1.1F, .fade = 1.0F};

    // A quarter of the way the value is still in the walk's pair: the walk named, the value and phase blended.
    HumanState blended = blendHumans(from, to, 0.25F);
    CHECK(blended.layers[0].gait == Approx(0.95F));
    CHECK(blended.layers[0].playhead == Approx(0.225F));
    CHECK(blended.layers[0].clip == 20);
    // Three quarters of the way it is in the jog's pair: the jog named.
    blended = blendHumans(from, to, 0.75F);
    CHECK(blended.layers[0].gait == Approx(1.05F));
    CHECK(blended.layers[0].playhead == Approx(0.275F));
    CHECK(blended.layers[0].clip == 21);
    // Slowing down is the same the other way.
    blended = blendHumans(to, from, 0.75F);
    CHECK(blended.layers[0].gait == Approx(0.95F));
    CHECK(blended.layers[0].clip == 20);

    // A clip with no gait value (any other task) into a gait blend is a cut, drawn as the newer snapshot has it.
    HumanState other = standing(0.0F, 0.6F);
    other.layers[0].clip = 9;
    blended = blendHumans(other, to, 0.25F);
    CHECK(blended.layers[0].clip == 21);
    CHECK(blended.layers[0].gait == 1.1F);
    CHECK(blended.layers[0].playhead == 0.3F);
}

// Expected: unverified, #555 (Coney's own; docs/guides/conventions.md#update-and-render's two-step blend)
TEST_CASE("client world: each step draws the buffer's time, and a frame between two steps blends them through "
          "DrawnPoses",
          "[net][client_world]") {
    SnapshotSender sender;
    SnapshotReceiver receiver;
    ClientWorld world;
    // A human walking 0.1 m a step, heading 0; a second one from step 5 on.
    const auto send = [&](std::uint32_t step) {
        std::vector<SnapshotHuman> humans{
            {.id = idOf(0), .words = humanWords(standing(0.1F * static_cast<float>(step)))}};
        if (step >= 5) {
            humans.push_back({.id = idOf(1), .words = humanWords(standing(-4.0F))});
        }
        const Snapshot snapshot = sender.build(step, humans);
        REQUIRE(receiver.apply(snapshot) == SnapshotApplied::Applied);
        REQUIRE(sender.acknowledge(snapshot.sequence).has_value());
        world.receive(receiver);
    };

    for (std::uint32_t step = 0; step <= 3; ++step) {
        send(step);
    }
    world.step();
    REQUIRE(world.shownTime() == std::optional<std::uint64_t>{0});
    REQUIRE(world.humans().size() == 1);
    CHECK(world.humans().at(idOf(0)).position.x == Approx(0.0F).margin(0.005F));

    for (std::uint32_t step = 4; step <= 8; ++step) {
        send(step);
        world.step();
        CHECK(world.shownTime() == std::optional<std::uint64_t>{(step - 3) * kTimeUnitsPerStep});
        CHECK(world.humans().at(idOf(0)).position.x == Approx(0.1F * static_cast<float>(step - 3)).margin(0.005F));
    }
    // Shown time 5: the second human is new this step, drawn where it is; the first blends from step 4 to 5.
    const auto drawn = world.drawn(0.5F);
    REQUIRE(drawn.size() == 2);
    const float before = world.previousHumans().at(idOf(0)).position.x;
    const float now = world.humans().at(idOf(0)).position.x;
    CHECK(drawn.at(idOf(0)).pose.position.x == Approx((before + now) * 0.5F));
    CHECK(drawn.at(idOf(0)).state.position.x == Approx((before + now) * 0.5F));
    CHECK(drawn.at(idOf(1)).pose.position.x == world.humans().at(idOf(1)).position.x);
    // Alpha 1 is the last step exactly.
    const auto last = world.drawn(1.0F);
    CHECK(last.at(idOf(0)).state == world.humans().at(idOf(0)));
    CHECK(last.at(idOf(0)).pose.position.x == world.humans().at(idOf(0)).position.x);
}

// Expected: unverified, #555 (Coney's own; NetClient::receive() decodes every snapshot that arrived in a step)
TEST_CASE("client world: every snapshot decoded since the last step goes into the buffer, not only the newest",
          "[net][client_world]") {
    SnapshotSender sender;
    SnapshotReceiver receiver;
    ClientWorld world;
    // The snapshot of a human 0.1 m a step along at `step`, decoded and acknowledged, but not yet taken.
    const auto decode = [&](std::uint32_t step) {
        const Snapshot snapshot =
            sender.build(step, std::vector<SnapshotHuman>{
                                   {.id = idOf(0), .words = humanWords(standing(0.1F * static_cast<float>(step)))}});
        REQUIRE(receiver.apply(snapshot) == SnapshotApplied::Applied);
        REQUIRE(sender.acknowledge(snapshot.sequence).has_value());
    };

    // Four arrive before the client's first step: all four are buffered, so the clock starts the delay behind the
    // newest, at the first.
    for (std::uint32_t step = 0; step <= 3; ++step) {
        decode(step);
    }
    world.receive(receiver);
    CHECK(world.buffer().size() == 4);
    world.step();
    CHECK(world.shownTime() == std::optional<std::uint64_t>{0});

    // Two arrive in one step: the older is drawn at its own step too, not blended across.
    decode(4);
    decode(5);
    world.receive(receiver);
    world.receive(receiver); // taking again adds nothing
    CHECK(world.buffer().newestStep() == 5);
    CHECK(world.buffer().size() == 6);
    for (std::uint32_t shown = 1; shown <= 2; ++shown) {
        world.step();
        REQUIRE(world.shownTime() == std::optional<std::uint64_t>{shown * kTimeUnitsPerStep});
        CHECK(world.buffer().sample()->toStep == shown);
    }
}

// Expected: unverified, #555 (Coney's own; the pose is AnimTaskStack::sample()'s,
// docs/research/formats/animation.md#task-stack)
TEST_CASE("client world: a human's pose is its layers sampled and faded in as its task stack sampled them",
          "[net][client_world]") {
    TestClips clips;
    clips.clips.emplace(1, testClip(1.0F, 1.0F, 0.2F));
    clips.clips.emplace(2, testClip(0.5F, -1.5F, -0.6F));
    for (std::int32_t id = 20; id < 25; ++id) {
        clips.clips.emplace(id, testClip(1.0F, 0.3F * static_cast<float>(id - 19), 0.1F * static_cast<float>(id - 19)));
    }
    clips.gaitIds = {20, 21, 22, 23, 24};

    // Clip 2 fading in over clip 1, a third of the way through a 0.3 s fade.
    coney::anim::AnimTaskStack stack;
    stack.change(std::make_unique<coney::anim::LoopTask>(*clips.clip(1), 1, 1.0F, 0U), 0.0F);
    stack.advance(0.4F);
    stack.change(std::make_unique<coney::anim::LoopTask>(*clips.clip(2), 2, 1.0F, 0U), 0.3F);
    stack.advance(0.1F);
    HumanState human = standing(0.0F);
    human.layers[0] = HumanAnimLayer{.clip = 2, .playhead = 0.1F / 0.5F, .gait = 0.0F, .fade = 1.0F};
    human.layers[1] = HumanAnimLayer{.clip = 1,
                                     .playhead = 0.5F,
                                     .gait = 0.0F,
                                     .fade = 1.0F - coney::anim::AnimTaskStack::outgoingWeight(0.1F, 0.3F)};
    checkSamePose(humanPose(human, clips, kBind), stack.sample(kBind));

    // A walk easing into a jog: the gait blend rebuilt from its clips, value and phase.
    std::array<coney::anim::GaitClip, 5> gait{};
    for (std::size_t i = 0; i < gait.size(); ++i) {
        gait.at(i) = {.clip = clips.clip(20 + static_cast<std::int32_t>(i)),
                      .animId = 20U + static_cast<std::uint32_t>(i)};
    }
    const coney::anim::GaitBlendTask blend(gait, 1.4F, 0.0F, 1.0F, coney::anim::kGaitBlendFlags, 0.25F);
    HumanState walker = standing(0.0F);
    walker.layers[0] = HumanAnimLayer{.clip = static_cast<std::int32_t>(blend.animId()),
                                      .playhead = blend.phase(),
                                      .gait = blend.value(),
                                      .fade = 1.0F};
    checkSamePose(humanPose(walker, clips, kBind), blend.sample(kBind));

    // The jog played as a plain loop (gait value 0, which plays the walk): sampled as itself, not as the walk.
    HumanState jogger = standing(0.0F);
    jogger.layers[0] = HumanAnimLayer{.clip = 21, .playhead = 0.5F, .gait = 0.0F, .fade = 1.0F};
    checkSamePose(humanPose(jogger, clips, kBind), coney::anim::samplePose(*clips.clip(21), 0.5F, kBind));

    // A layer whose clip is not loaded is left out; with none, the rest pose.
    HumanState unknown = human;
    unknown.layers[0].clip = 99;
    checkSamePose(humanPose(unknown, clips, kBind), coney::anim::samplePose(*clips.clip(1), 0.5F, kBind));
    unknown.layers[1].clip = 98;
    const coney::anim::Pose rest = humanPose(unknown, clips, kBind);
    CHECK(rest.defaulted.at(2));
    CHECK(rest.rotations.at(2).w == 1.0F);
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
    void run(int steps, const std::function<void()>& afterReceive = {}) {
        for (int i = 0; i < steps; ++i) {
            server.advance(kStepNs);
            client.advance(kStepNs);
            server.receive();
            client.receive();
            if (afterReceive) {
                afterReceive();
            }
            server.send();
            client.send();
        }
    }
};

// The server's humans at `step`, every replicated field moving: six walking, turning, losing health, changing clips,
// a gait blend, fading layers, landing and getting up; at step 150 the sixth leaves and another comes.
std::map<NetId, HumanState> sceneAt(std::uint32_t step) {
    std::map<NetId, HumanState> humans;
    const auto t = static_cast<float>(step);
    for (std::uint16_t i = 0; i < 7; ++i) {
        if ((i == 5 && step >= 150) || (i == 6 && step < 150)) {
            continue;
        }
        const auto fi = static_cast<float>(i);
        HumanState human;
        human.position = Vec3{(2.0F * fi) + (0.05F * t), 0.02F * t * fi, 0.0F};
        human.heading = std::remainder(0.03F * t * (fi + 1.0F), 2.0F * std::numbers::pi_v<float>);
        human.health = 150 - static_cast<std::int32_t>((step / 40) % 50);
        human.healthMax = 150;
        human.layers[0] = HumanAnimLayer{.clip = 10 + static_cast<std::int32_t>((step / 50) % 3),
                                         .playhead = static_cast<float>(step % 45) / 45.0F,
                                         .gait = i == 0 ? 1.5F : 0.0F,
                                         .fade = 1.0F};
        if (i % 2 == 1) {
            human.layers[1] = HumanAnimLayer{.clip = 7,
                                             .playhead = static_cast<float>(step % 30) / 30.0F,
                                             .gait = 0.0F,
                                             .fade = static_cast<float>((step % 20) + 1) / 21.0F};
        }
        human.grounded = (step / 25) % 2 == 0;
        human.knockedOut = i == 3 && step > 200;
        humans.emplace(idOf(i), human);
    }
    return humans;
}

} // namespace

// Expected: unverified, #555 (the architecture's "What a client sees" test,
// docs/guides/network-architecture.md#testing)
TEST_CASE("client world: once the jitter buffer settles, every field the client draws is the server's at the drawn "
          "step, within quantization, over a bad link",
          "[net][client_world][sessions]") {
    Session session;
    session.run(3);
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
    std::map<std::uint32_t, std::map<NetId, HumanState>> sent; // what the server's game had at each step
    std::map<std::uint32_t, std::uint32_t> sentStep;           // the server's step of each snapshot sequence sent
    std::set<std::uint32_t> decoded;                           // the steps whose snapshot the client decoded
    std::set<std::uint32_t> sampled;                           // the steps the buffer's samples blended to or from
    std::uint32_t seen = 0;                                    // the newest sequence looked at
    std::uint32_t step = 0;
    std::optional<std::uint64_t> firstShown;
    int compared = 0;
    int missed = 0;  // shown on a decoded snapshot's step, but not drawn from that snapshot
    int between = 0; // shown between two whole steps (while easing)
    int lost = 0;    // shown on a step whose snapshot the client never decoded
    int settled = 0;
    int counted = 0;
    std::uint64_t leastDelay = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t mostDelay = 0;
    const auto stepGame = [&] {
        // What the client decoded this step, every snapshot of it, and not only the newest.
        const SnapshotReceiver& snapshots = session.client.snapshots();
        for (std::uint32_t sequence = seen + 1; sequence <= snapshots.newest(); ++sequence) {
            if (snapshots.holding(sequence) != nullptr) {
                decoded.insert(sentStep.at(sequence));
            }
        }
        seen = snapshots.newest();
        // The client's step: what arrived goes into the buffer, and the buffer's time is drawn.
        world.receive(snapshots);
        world.step();
        if (step >= 90) { // three seconds to settle
            ++counted;
            settled += world.buffer().settled() ? 1 : 0;
            leastDelay = std::min(leastDelay, world.buffer().delay());
            mostDelay = std::max(mostDelay, world.buffer().delay());
            const auto sample = world.buffer().sample();
            const auto shown = world.shownTime();
            REQUIRE(sample.has_value());
            REQUIRE(shown.has_value());
            firstShown = firstShown.value_or(*shown);
            sampled.insert(sample->fromStep);
            sampled.insert(sample->toStep);
            const bool whole = *shown % kTimeUnitsPerStep == 0;
            if (whole && decoded.contains(static_cast<std::uint32_t>(*shown / kTimeUnitsPerStep)) &&
                sample->toStep * kTimeUnitsPerStep != *shown) {
                ++missed; // the client had this step's snapshot, but blended across it
            }
            if (!whole) {
                ++between;
            } else if (!decoded.contains(static_cast<std::uint32_t>(*shown / kTimeUnitsPerStep))) {
                ++lost;
            }
            if (sample->toStep * kTimeUnitsPerStep == *shown) {
                // On a step a snapshot shows: the very humans the server had then, each field its word.
                const auto& expected = sent.at(sample->toStep);
                REQUIRE(world.humans().size() == expected.size());
                for (const auto& [id, human] : expected) {
                    REQUIRE(world.humans().contains(id));
                    CHECK(humanWords(world.humans().at(id)) == humanWords(human));
                }
                ++compared;
            }
        }
        // The server's step: its humans, and their snapshot.
        const auto humans = sceneAt(step);
        std::vector<SnapshotHuman> snapshot;
        for (const auto& [id, human] : humans) {
            snapshot.push_back({.id = id, .words = humanWords(human)});
        }
        session.server.sendSnapshots(step, snapshot);
        REQUIRE(session.server.clients().size() == 1);
        sentStep.emplace(session.server.clients().front().snapshots.sequence(), step);
        sent.emplace(step, humans);
        ++step;
    };
    session.run(400, stepGame);
    REQUIRE(session.client.state() == ClientState::Connected);
    REQUIRE(firstShown.has_value());

    // Every snapshot the client decoded reached the buffer: each step one shows, from the first drawn to the last, was
    // blended to or from, and drawn exactly when the clock was on it.
    int unbuffered = 0;
    const auto lastShown = static_cast<std::uint32_t>(*world.shownTime() / kTimeUnitsPerStep);
    for (const std::uint32_t decodedStep : decoded) {
        if (decodedStep * kTimeUnitsPerStep >= *firstShown && decodedStep <= lastShown &&
            !sampled.contains(decodedStep)) {
            ++unbuffered;
        }
    }
    CHECK(unbuffered == 0);
    CHECK(missed == 0);
    // Every step drawn is compared but those whose snapshot the client never decoded: lost on the link, or overtaken
    // by a newer one while reordered (so stale). On this seeded link that is 23 of the 310 (measured, the same each
    // run); no step fell between whole steps, since the clock never had to ease.
    CHECK(compared + lost == counted);
    CHECK(between == 0);
    CHECK(compared >= 287);
    CHECK(settled * 10 >= counted * 9);
    CHECK(world.buffer().snaps() == 0);
    // It draws the past, about the delay behind the newest snapshot, at every step (1 to 3 steps, measured).
    CHECK(leastDelay >= 1 * kTimeUnitsPerStep);
    CHECK(mostDelay <= 5 * kTimeUnitsPerStep);
}
