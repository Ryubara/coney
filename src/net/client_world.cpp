// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/client_world.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <utility>
#include <variant>

#include "animation/anim_clip.h"
#include "animation/anim_math.h"
#include "animation/anim_pose.h"
#include "animation/anim_task.h"
#include "core/interpolation.h"
#include "net/jitter_buffer.h"
#include "net/net_id.h"
#include "net/replicated_fields.h"
#include "net/snapshot.h"
#include "world_objects/drawn_poses.h"

namespace coney::net {

namespace {

// Field `f`'s index into a human's values.
constexpr std::size_t at(HumanField f) { return static_cast<std::size_t>(f); }

// The fields of layer `layer` from 1 up are four apart, from Clip1 on: clip, playhead, gait value, fade.
constexpr std::size_t kLayerStride = at(HumanField::Clip2) - at(HumanField::Clip1);
static_assert(at(HumanField::Clip3) - at(HumanField::Clip2) == kLayerStride);
static_assert(at(HumanField::Fade1) - at(HumanField::Clip1) == 3);

// The index of layer `layer`'s clip field; its playhead, gait value and fade follow it.
constexpr std::size_t clipField(std::size_t layer) {
    return layer == 0 ? at(HumanField::Clip) : at(HumanField::Clip1) + ((layer - 1) * kLayerStride);
}

// The farthest a human is blended across between two snapshots or two steps; farther is a jump. DrawnPoses' rule, so
// a human's state and its pose jump together.
constexpr float kMostBlendedMetres = world_objects::DrawnPoses<NetId>::kMostBlendedMetres;

// The pose of one layer at its playhead: a gait blend rebuilt when the layer's clip is one of the pair its gait value
// plays (the lower clip, or the upper one while the value is between whole numbers), else the clip alone. Nothing
// when the clip is not loaded.
std::optional<anim::Pose> layerPose(const HumanAnimLayer& layer, const ClipSource& clips,
                                    std::span<const anim::Quat, anim::kPoseBones> defaultRotations) {
    const anim::AnimClip* clip = clips.clip(layer.clip);
    if (clip == nullptr) {
        return std::nullopt;
    }
    if (const auto gait = clips.gaitClips(layer.clip); gait.has_value()) {
        const bool loaded = std::ranges::all_of(*gait, [](const anim::GaitClip& slot) { return slot.clip != nullptr; });
        if (loaded) {
            const anim::GaitBlendTask blend(*gait, layer.gait, 0.0F, 1.0F, anim::kGaitBlendFlags, layer.playhead);
            const std::size_t lower = blend.lowerIndex();
            const auto id = static_cast<std::uint32_t>(layer.clip);
            const bool between = layer.gait != static_cast<float>(lower);
            if (gait->at(lower).animId == id || (between && gait->at(lower + 1).animId == id)) {
                return blend.sample(defaultRotations);
            }
        }
    }
    return anim::samplePose(*clip, layer.playhead * clip->duration, defaultRotations);
}

} // namespace

HumanState humanState(const HumanWords& words) {
    const auto values = humanValues(words);
    const auto real = [&](HumanField f) { return std::get<float>(values.at(at(f))); };
    const auto whole = [&](std::size_t f) { return std::get<std::int32_t>(values.at(f)); };
    HumanState human;
    human.position = anim::Vec3{real(HumanField::PositionX), real(HumanField::PositionY), real(HumanField::PositionZ)};
    human.heading = real(HumanField::Heading);
    human.health = whole(at(HumanField::Health));
    human.healthMax = whole(at(HumanField::HealthMax));
    for (std::size_t layer = 0; layer < kHumanAnimLayers; ++layer) {
        const std::size_t clip = clipField(layer);
        human.layers.at(layer).clip = whole(clip);
        human.layers.at(layer).playhead = std::get<float>(values.at(clip + 1));
        human.layers.at(layer).gait = std::get<float>(values.at(clip + 2));
        human.layers.at(layer).fade = layer == 0 ? 1.0F : std::get<float>(values.at(clip + 3));
    }
    human.grounded = std::get<bool>(values.at(at(HumanField::Grounded)));
    human.knockedOut = std::get<bool>(values.at(at(HumanField::KnockedOut)));
    return human;
}

HumanWords humanWords(const HumanState& human) {
    std::array<FieldValue, kHumanFieldCount> values{};
    values.at(at(HumanField::PositionX)) = human.position.x;
    values.at(at(HumanField::PositionY)) = human.position.y;
    values.at(at(HumanField::PositionZ)) = human.position.z;
    values.at(at(HumanField::Heading)) = human.heading;
    values.at(at(HumanField::Health)) = human.health;
    values.at(at(HumanField::HealthMax)) = human.healthMax;
    for (std::size_t layer = 0; layer < kHumanAnimLayers; ++layer) {
        const std::size_t clip = clipField(layer);
        values.at(clip) = human.layers.at(layer).clip;
        values.at(clip + 1) = human.layers.at(layer).playhead;
        values.at(clip + 2) = human.layers.at(layer).gait;
        if (layer != 0) {
            values.at(clip + 3) = human.layers.at(layer).fade;
        }
    }
    values.at(at(HumanField::Grounded)) = human.grounded;
    values.at(at(HumanField::KnockedOut)) = human.knockedOut;
    return humanWords(std::span<const FieldValue, kHumanFieldCount>(values));
}

std::size_t playingLayers(const HumanState& human) {
    std::size_t playing = 1;
    while (playing < kHumanAnimLayers && human.layers.at(playing).fade < 1.0F) {
        ++playing;
    }
    return playing;
}

HumanState blendHumans(const HumanState& from, const HumanState& to, float alpha) {
    if (alpha >= 1.0F) {
        return to;
    }
    if (alpha <= 0.0F) {
        return from;
    }
    const float dx = to.position.x - from.position.x;
    const float dy = to.position.y - from.position.y;
    const float dz = to.position.z - from.position.z;
    if ((dx * dx) + (dy * dy) + (dz * dz) > kMostBlendedMetres * kMostBlendedMetres) {
        return to;
    }
    HumanState blended = to;
    blended.position =
        anim::Vec3{lerp(from.position.x, to.position.x, alpha), lerp(from.position.y, to.position.y, alpha),
                   lerp(from.position.z, to.position.z, alpha)};
    blended.heading = lerpAngle(from.heading, to.heading, alpha);

    // The layers `to` added on top push `from`'s down: find where `from`'s newest layer went, then blend each layer
    // from there down with the one it was, while it plays the same clip.
    const std::size_t playing = playingLayers(to);
    std::size_t shift = 0;
    while (shift < playing && to.layers.at(shift).clip != from.layers.at(0).clip) {
        ++shift;
    }
    if (shift == playing) {
        return blended; // nothing of `from`'s newest layer is left: a cut, drawn as `to`
    }
    for (std::size_t layer = shift; layer < kHumanAnimLayers; ++layer) {
        const HumanAnimLayer& before = from.layers.at(layer - shift);
        const HumanAnimLayer& after = to.layers.at(layer);
        if (before.clip != after.clip) {
            continue;
        }
        HumanAnimLayer& drawn = blended.layers.at(layer);
        drawn.playhead = lerpLooping(before.playhead, after.playhead, alpha, 1.0F);
        drawn.gait = lerp(before.gait, after.gait, alpha);
        // The fade over the layer `from`'s newest became is a new layer's fading in: `to`'s.
        if (layer > shift) {
            drawn.fade = lerp(before.fade, after.fade, alpha);
        }
    }
    return blended;
}

world_objects::WorldPose humanWorldPose(const HumanState& human) {
    const float half = human.heading * 0.5F;
    return world_objects::WorldPose{.position = human.position,
                                    .rotation = anim::Quat{0.0F, 0.0F, std::sin(half), std::cos(half)}};
}

anim::Pose humanPose(const HumanState& human, const ClipSource& clips,
                     std::span<const anim::Quat, anim::kPoseBones> defaultRotations) {
    // From the oldest playing layer up, each newer one over what is under it, as AnimTaskStack::sample() does: the
    // older weighing the outgoing weight, which is 1 less the fade the newer one came in by.
    std::optional<anim::Pose> pose;
    for (std::size_t layer = playingLayers(human); layer-- > 0;) {
        auto sampled = layerPose(human.layers.at(layer), clips, defaultRotations);
        if (!sampled.has_value()) {
            continue;
        }
        if (!pose.has_value()) {
            pose = *sampled;
            continue;
        }
        pose = anim::blendPoses(*sampled, *pose, 1.0F - human.layers.at(layer + 1).fade);
    }
    if (pose.has_value()) {
        return *pose;
    }
    anim::Pose rest;
    std::ranges::copy(defaultRotations, rest.rotations.begin());
    rest.defaulted.fill(true);
    return rest;
}

ClientWorld::ClientWorld(JitterConfig config) : m_buffer(config) {}

void ClientWorld::receive(const SnapshotReceiver& snapshots) {
    if (snapshots.newest() == 0 || snapshots.newest() == m_taken) {
        return;
    }
    m_taken = snapshots.newest();
    (void)m_buffer.push(snapshots.newestStep(), snapshots.humans());
}

void ClientWorld::step() {
    m_humans.commit();
    m_poses.commit();
    m_buffer.advance();
    const auto sample = m_buffer.sample();
    if (!sample.has_value()) {
        return;
    }
    // The buffer's time: each human of the newer snapshot, blended from the older where it was there too.
    Humans now;
    for (const auto& [id, words] : *sample->to) {
        HumanState human = humanState(words);
        if (const auto before = sample->from->find(id); before != sample->from->end() && sample->from != sample->to) {
            human = blendHumans(humanState(before->second), human, sample->alpha);
        }
        m_poses.record(id, humanWorldPose(human));
        now.emplace(id, human);
    }
    m_humans.current() = std::move(now);
    m_shownTime = m_buffer.time();
}

std::map<NetId, DrawnHuman> ClientWorld::drawn(float alpha) const {
    std::map<NetId, DrawnHuman> drawn;
    for (const auto& [id, human] : m_humans.current()) {
        const auto before = m_humans.previous().find(id);
        const HumanState state =
            before == m_humans.previous().end() ? human : blendHumans(before->second, human, alpha);
        drawn.emplace(id, DrawnHuman{.pose = m_poses.drawn(id, humanWorldPose(human), alpha), .state = state});
    }
    return drawn;
}

} // namespace coney::net
