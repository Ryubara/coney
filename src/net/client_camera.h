// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "animation/anim_math.h"
#include "camera/camera_view.h"
#include "camera/follow_camera.h"
#include "human/locomotion.h"
#include "net/client_world.h"
#include "net/net_id.h"

// The client's own camera: Coney's follow camera (camera::FollowCamera, the original's Cam_Follow) run on the human the
// client's player slot follows, from what the snapshots replicate of him. Coney's own; the original has no network
// play (docs/guides/client-world.md#client-camera).

namespace coney::raycast {
class CollisionMesh;
} // namespace coney::raycast

namespace coney::net {

/// The right stick's raw byte at rest.
/// @coney-only network play; the original has none
inline constexpr std::uint8_t kClientStickRest = 128;

/// The human the client of player slot `slot` follows, of the humans a step shows: **stand-in** until the snapshots
/// name the human each player slot drives (#1654). The host's game has one player so far (the guest's pad plays port 2
/// but drives no human of its own, docs/guides/multiplayer.md#player-two), so every slot follows the host's player 1,
/// the human with the lowest network id (his brain comes first in the host's capture). None with no human.
/// @coney-only network play; the original has none
[[nodiscard]] std::optional<NetId> followedHuman(const ClientWorld::Humans& humans, std::uint8_t slot);

/// What the follow camera reads of a human from his replicated state, `now` at this step and `before` at the step
/// before (null when he is new: no velocity), `seconds` apart, with `speeds` his gaits' speeds: his feet, heading,
/// velocity (the move between the two steps), gait (the speed's nearest, as human::Human::gait() reads it), whether he
/// is in the air (not grounded), and whether he counts for the cameras (not knocked out, with health left). What the
/// snapshots do not carry takes the value of a human with nothing near him: no enemy, no lock-on, no grab, nothing
/// carrying the aim point (docs/guides/client-world.md#client-camera).
/// @coney-only network play; the original has none
[[nodiscard]] camera::FollowTarget followTargetOf(const HumanState& now, const HumanState* before,
                                                  const human::Speeds& speeds, float seconds);

/// What a client step gives its camera besides the world.
/// @coney-only network play; the original has none
struct ClientCameraInput {
    human::Speeds speeds;                         ///< the followed human's gait speeds (his character's)
    std::uint8_t rawRightX = kClientStickRest;    ///< this client's own pad's right stick, raw bytes (128 at rest)
    std::uint8_t rawRightY = kClientStickRest;    ///< (turns its own view: the host's camera never reads it)
    const raycast::CollisionMesh* mesh = nullptr; ///< the level's collision for the pull-in; null for none
    float seconds = 1.0F / 30.0F;                 ///< the client step's length
};

/// The camera of a client's own view: a camera::FollowCamera on the human it follows, stepped with the client's steps
/// from the humans the client's world shows (so it follows the jitter buffer's playback, not the newest snapshot), with
/// the client's own right stick. It keeps its last two views for the frames between (view(alpha)), as the play mode
/// keeps its player's. Presentation only: the host simulates every player's own camera (the spawners and the split
/// decision read them), and a client's is never sent back, so nothing it does reaches the host's game.
///
/// A human to follow made afresh puts the camera behind him at once (a cut, not a blend). **Stand-ins**: the level's
/// own camera set-up (CfgFollowCamera and the scripts' camera calls) is not replicated, so the camera runs on the
/// follow camera's defaults; scripted, scene and fixed cameras are the host's; the target's left stick, enemies,
/// lock-on and grabs are not carried either, so those rules do not run.
/// @coney-only network play; the original has none
class ClientCamera {
  public:
    /// A camera with no human yet, made with `settings`.
    explicit ClientCamera(const camera::FollowSettings& settings = camera::followDefaults());

    /// One client step after the world's: follows `followed` (a human of world.humans(), or none), the camera updated
    /// by `input`. Following a human it had not followed puts the camera behind him; none, or a human no longer in the
    /// world, leaves the camera without a view until he is.
    void step(const ClientWorld& world, std::optional<NetId> followed, const ClientCameraInput& input);

    /// The human followed this step; none before the first.
    [[nodiscard]] std::optional<NetId> followed() const { return m_followed; }
    /// The view `alpha` of the way from the step before to the last one (the eye and the point it looks at lerped, the
    /// lens of the last step); none while no human is followed. A cut (a new human followed) draws the last step's.
    [[nodiscard]] std::optional<camera::CameraView> view(float alpha) const;
    /// How many times the camera was put behind a new human.
    [[nodiscard]] std::uint32_t cuts() const { return m_cuts; }
    /// The follow camera itself; null while no human is followed.
    [[nodiscard]] const camera::FollowCamera* follow() const { return m_camera ? &*m_camera : nullptr; }

  private:
    // What drawing a step needs of the camera: where it stands, what it looks at, its lens.
    struct Placed {
        anim::Vec3 eye;
        anim::Vec3 target;
        float fieldOfView = 65.0F;
        float nearClip = 0.1F;
        float farClip = 115.0F;
    };
    // How `follow` stands now.
    [[nodiscard]] static Placed placedBy(const camera::FollowCamera& follow);

    camera::FollowSettings m_settings;
    std::optional<camera::FollowCamera> m_camera; // none while no human is followed
    std::optional<NetId> m_followed;
    Placed m_previous;
    Placed m_current;
    std::uint32_t m_cuts = 0;
};

} // namespace coney::net
