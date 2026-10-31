// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>

#include "animation/anim_math.h"
#include "animation/anim_pose.h"
#include "animation/anim_task.h"
#include "core/interpolation.h"
#include "net/jitter_buffer.h"
#include "net/net_id.h"
#include "net/protocol.h"
#include "net/replicated_fields.h"
#include "world_objects/drawn_poses.h"

// The client's world: the humans a client draws, from the snapshots in its jitter buffer, blended between two of them
// and again between the client's last two steps, and their poses evaluated from the replicated animation layers. A
// client runs no simulation: everything here is presentation. Coney's own; the original has no network play
// (docs/guides/client-world.md).

namespace coney::net {

/// One animation layer of a replicated human (the field table's ClipN, PlayheadN, GaitN and FadeN).
/// @coney-only network play; the original has none
struct HumanAnimLayer {
    std::int32_t clip = 0; ///< the anim id playing; 0 with no such layer
    float playhead = 0.0F; ///< how far through the clip, 0 to 1 (a gait blend's shared phase)
    float gait = 0.0F;     ///< a gait blend's value, 0 to 4; 0 for any other task
    /// How far the layer above this one has faded in over it and the older ones, 0 to 1: the incoming weight
    /// `1 - AnimTaskStack::outgoingWeight()`. 1 for the newest layer, which nothing is over, and with no such layer.
    float fade = 1.0F;

    friend bool operator==(const HumanAnimLayer&, const HumanAnimLayer&) = default;
};

/// A replicated human's state as a client holds it: its field table's values, decoded.
/// @coney-only network play; the original has none
struct HumanState {
    anim::Vec3 position;                                   ///< the feet, metres
    float heading = 0.0F;                                  ///< radians, 0 facing +y
    std::int32_t health = 0;                               ///< health points
    std::int32_t healthMax = 0;                            ///< their maximum
    std::array<HumanAnimLayer, kHumanAnimLayers> layers{}; ///< the newest first
    bool grounded = false;                                 ///< on the ground
    bool knockedOut = false;                               ///< knocked out or wounded

    friend bool operator==(const HumanState&, const HumanState&) = default;
};

/// A human's state from its snapshot words (humanValues() of them, gathered). The words of a decoded snapshot are
/// always valid.
/// @coney-only network play; the original has none
[[nodiscard]] HumanState humanState(const HumanWords& words);

/// The words of a human's state (humanWords() of its values): what the server's game hands a snapshot, and what a
/// client's state compares by to be the server's within quantization. A clip or health out of its field's range is a
/// programmer error, as for writeField().
/// @coney-only network play; the original has none
[[nodiscard]] HumanWords humanWords(const HumanState& human);

/// How many of a human's layers play: the newest, and each older one while the layer above it has not wholly faded in
/// over it (a fade under 1).
/// @coney-only network play; the original has none
[[nodiscard]] std::size_t playingLayers(const HumanState& human);

/// `from` (alpha 0) blended towards `to` (alpha 1), as a client draws a human between two snapshots or two steps: the
/// position lerped and the heading the short way round; each layer of `to` that was already playing in `from` (the same
/// clip, found below the layers `to` added on top) has its playhead blended forward round the clip, its gait value and
/// its fade lerped. The rest is `to`'s, as offline rendering draws the newest step's discrete state: health, the
/// flags, a new clip, a new layer's fade. A move longer than DrawnPoses' kMostBlendedMetres is a jump, drawn as `to`.
/// Returns `to` itself at alpha >= 1 and `from` at alpha <= 0.
/// @coney-only network play; the original has none
[[nodiscard]] HumanState blendHumans(const HumanState& from, const HumanState& to, float alpha);

/// A human's pose and rotation in the world: at its feet, turned by its heading about z.
/// @coney-only network play; the original has none
[[nodiscard]] world_objects::WorldPose humanWorldPose(const HumanState& human);

/// Where a client finds the clips the snapshots name: the same disc clips as the server's.
/// @coney-only network play; the original has none
class ClipSource {
  public:
    ClipSource() = default;
    virtual ~ClipSource() = default;
    ClipSource(const ClipSource&) = delete;
    ClipSource& operator=(const ClipSource&) = delete;
    ClipSource(ClipSource&&) = delete;
    ClipSource& operator=(ClipSource&&) = delete;

    /// The clip of anim id `animId`, or null when it is not loaded (yet).
    [[nodiscard]] virtual const anim::AnimClip* clip(std::int32_t animId) const = 0;
    /// The five clips of the gait blend that clip `animId` is one of (the style's walk, jog, run, sprint and sprint
    /// slots), or nothing when it is in none.
    [[nodiscard]] virtual std::optional<std::array<anim::GaitClip, 5>> gaitClips(std::int32_t animId) const = 0;
};

/// A human's pose from its layers, as its AnimTaskStack sampled it on the server: each playing layer sampled at its
/// playhead (a gait blend rebuilt from its clips, value and phase when the layer's clip is one of the pair the value
/// plays), then from the oldest up each newer layer over what is under it by its fade. A layer whose clip `clips` does
/// not have is left out; with none, the rest pose of `defaultRotations`. Root motion is the server's, so the task flags
/// that only change it are not carried.
/// @coney-only network play; the original has none
[[nodiscard]] anim::Pose humanPose(const HumanState& human, const ClipSource& clips,
                                   std::span<const anim::Quat, anim::kPoseBones> defaultRotations);

/// What a client draws of one human in a frame: where (through DrawnPoses) and its state, blended.
/// @coney-only network play; the original has none
struct DrawnHuman {
    world_objects::WorldPose pose; ///< at its feet, turned by its heading; DrawnPoses' blend of the last two steps
    HumanState state;              ///< blendHumans() of the last two steps
};

/// The client's world: what a client that runs no simulation draws of the server's humans. Fed every snapshot its
/// NetClient decodes, it keeps them in a JitterBuffer; each client step it samples the buffer at the playback time
/// (blendHumans() of the two snapshots either side) and commits that as the step's state, through Interpolated<T> and
/// DrawnPoses as an offline mode keeps its last two steps (docs/guides/conventions.md#update-and-render); drawn()
/// blends the last two steps with the frame's alpha. Presentation-side and the client's own.
/// @coney-only network play; the original has none
class ClientWorld {
  public:
    /// The humans a client step drew, by id, walked in id order.
    using Humans = std::map<NetId, HumanState>;

    /// A world whose buffer plays as `config` says.
    explicit ClientWorld(JitterConfig config = {});

    /// Takes the newest snapshot `snapshots` has decoded into the buffer, when it is one not taken before. Call after
    /// each NetClient::receive().
    void receive(const SnapshotReceiver& snapshots);
    /// One client step: the buffer's clock advances, and the humans it shows become this step's.
    void step();
    /// The humans as this frame draws them, `alpha` of the way from the step before to the last one: a human new in
    /// the last step, or one that jumped, as the last step has it.
    [[nodiscard]] std::map<NetId, DrawnHuman> drawn(float alpha) const;

    /// The humans the last step showed.
    [[nodiscard]] const Humans& humans() const { return m_humans.current(); }
    /// The humans the step before showed.
    [[nodiscard]] const Humans& previousHumans() const { return m_humans.previous(); }
    /// The server's time the last step showed, kTimeUnitsPerStep to a step; nothing before the buffer's clock starts.
    [[nodiscard]] std::optional<std::uint64_t> shownTime() const { return m_shownTime; }
    /// The jitter buffer.
    [[nodiscard]] const JitterBuffer& buffer() const { return m_buffer; }

  private:
    JitterBuffer m_buffer;
    std::uint32_t m_taken = 0; // the newest sequence received
    Interpolated<Humans> m_humans{Humans{}};
    world_objects::DrawnPoses<NetId> m_poses;
    std::optional<std::uint64_t> m_shownTime;
};

} // namespace coney::net
