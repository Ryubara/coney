// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "animation/anim_math.h"
#include "core/error.h"
#include "debug/anim_catalog.h"
#include "debug/entity_inspect.h"
#include "sandbox/sandbox_layout.h"

namespace coney::hud {
class Hud;
}

namespace coney::debug {

/// A named place the player can be put: the feet, in the game's axes (z up, metres), and the heading in degrees (0
/// faces +y), as a level's player start and a sandbox spawn point are given.
struct Place {
    std::string name;
    anim::Vec3 feet;
    float headingDegrees = 0.0F;
};

/// What the Debug draw page switches: lines a play mode draws into the 3D scene, over the scenery.
struct DebugDrawOptions {
    bool collision = false;       ///< The collision triangles near the player, as a wireframe.
    float collisionRadius = 8.0F; ///< How far from the player's feet, in metres, the wireframe reaches.
    bool player = false;          ///< The player's feet (a cross), heading (a line) and velocity.
    bool groundNormal = false;    ///< The normal of the ground under the player.
    bool camera = false;          ///< The follow camera's wanted position and its look-at point.
    bool places = false;          ///< The places of PlayControls::places(), each a marker with its heading.
    // The collision shapes and the Entity inspector's highlight (src/debug/pages_entity.cpp, src/debug/human_shapes.h).
    bool wallClasses = false;    ///< The collision wireframe coloured by how the walking sweep classes each triangle.
    bool shapes = false;         ///< The humans' bodies: walking or air sphere, capsule, ground probe, climb.
    bool shapesAllHumans = true; ///< The shapes on every human; off: on the player and the inspected entity only.
    bool strikes = false;        ///< The strike shapes switched on, and the spine and head they are tested against.
    bool contacts = false;       ///< The contacts the last walking sweep slid along: a point and its normal.
    std::uint64_t inspected = 0; ///< The Entity inspector's selection (an EntityId; 0 for none).
    bool highlight = true;       ///< Mark the inspected entity in the scene.
};

/// One object the Spawner page can put in front of the player: a sandbox primitive and the name the page shows. The
/// primitive's base is replaced by the spot in front of the player and its yaw by the player's heading.
struct Spawnable {
    std::string_view name;
    sandbox::Primitive primitive;
};

/// The objects the Spawner page offers: Coney's own choices of sizes, in the sandbox's primitives
/// (docs/guides/sandbox.md).
[[nodiscard]] const std::vector<Spawnable>& spawnables();

/// One character type the Player page can rebuild the player as: its `CfgChar` type and the model a player of it is
/// drawn as (characters::CharacterTypes::modelFor()).
struct CharacterChoice {
    int type = 0;
    std::string model;
};

/// How the Animation player plays a clip on the player (docs/guides/debug-menu.md#animation-player).
struct AnimPlayOptions {
    bool loop = false;    ///< Loop until stopped, rather than once.
    float speed = 1.0F;   ///< Multiplies the clip's own rate.
    bool inPlace = false; ///< The clip's root motion neither moves nor turns him.
};

/// What the Animation player is playing on the player.
struct AnimPlayback {
    bool playing = false;  ///< A clip the Animation player started still plays.
    std::uint32_t id = 0;  ///< Its anim id.
    std::string clip;      ///< Its name.
    std::string set;       ///< The anim set it came from.
    float time = 0.0F;     ///< Seconds into the clip (clip time).
    float duration = 0.0F; ///< The clip's length, seconds.
    bool loop = false;     ///< Whether it loops.
};

/// What the debug menus can reach in a mode where the player plays (the play mode, `--play-level`): the player, the
/// follow camera and the scenery. The mode implements it; the Player, Camera and Spawner pages
/// (src/debug/pages_play.cpp) are defined over it, so they are the same in every front end and need no platform code
/// of their own.
///
/// Everything is in the game's axes (z up, metres; headings in degrees, 0 facing +y). The pages call it between
/// simulation steps, so a change lands before the next step.
class PlayControls {
  public:
    virtual ~PlayControls() = default;

    /// What is being played: the level's or the sandbox layout's name.
    [[nodiscard]] virtual std::string sceneName() const = 0;

    /// The player's feet.
    [[nodiscard]] virtual anim::Vec3 playerFeet() const = 0;
    /// The player's heading.
    [[nodiscard]] virtual float playerHeadingDegrees() const = 0;
    /// The player's speed across the ground, metres a second.
    [[nodiscard]] virtual float playerSpeed() const = 0;
    /// One line on the player's movement: gait, clip, on the ground or in the air.
    [[nodiscard]] virtual std::string playerState() const = 0;
    /// Puts the player at `place`, dropped onto the ground below it, with the camera placed behind it again.
    virtual void teleport(const Place& place) = 0;
    /// The scene's named places: the level's start or the sandbox's spawn points.
    [[nodiscard]] virtual std::vector<Place> places() const = 0;
    /// Whether the player ignores the pad (stands still).
    [[nodiscard]] virtual bool playerFrozen() const = 0;
    /// Makes the player ignore the pad, or listen to it again.
    virtual void setPlayerFrozen(bool frozen) = 0;
    /// Whether the player can noclip here (docs/guides/debug-menu.md#noclip). A mode without it keeps the defaults.
    [[nodiscard]] virtual bool canNoclip() const { return false; }
    /// Whether the noclip is on: pad 1 flies the player through walls and floors with no gravity (left stick across
    /// the ground as for a walk, R2 up, L2 down, R1 fast), and no other button acts in the game.
    [[nodiscard]] virtual bool noclip() const { return false; }
    /// Switches the noclip on, or off, which drops the player from where he is with the normal physics.
    virtual void setNoclip(bool /*on*/) {}

    /// The follow camera's eye.
    [[nodiscard]] virtual anim::Vec3 cameraEye() const = 0;
    /// The point the follow camera looks at.
    [[nodiscard]] virtual anim::Vec3 cameraTarget() const = 0;
    /// Places the follow camera behind the player again, with the current follow settings (the Follow camera
    /// tunables' leash, pitch and look-at height apply from here).
    virtual void resetCamera() = 0;
    /// Whether the free camera is on: it flies by pad 1 (the world viewer's controls) and draws the view, while the
    /// player stands still.
    [[nodiscard]] virtual bool freeCamera() const = 0;
    /// Switches the free camera on (at the follow camera's eye, looking where it looks) or off.
    virtual void setFreeCamera(bool on) = 0;

    /// Whether objects can be spawned here (a sandbox can; a level cannot yet).
    [[nodiscard]] virtual bool canSpawn() const = 0;
    /// Adds `primitive` to the scene, solid and drawn. Fails with ErrorCode::InvalidArgument where nothing can be
    /// spawned, and as the scene's rebuild does.
    virtual std::expected<void, Error> spawn(const sandbox::Primitive& primitive) = 0;
    /// Objects spawned so far.
    [[nodiscard]] virtual std::size_t spawnedCount() const = 0;
    /// Removes every spawned object.
    virtual std::expected<void, Error> clearSpawned() = 0;

    // AI fighters (ai::AiHumans): humans with a brain that fight the player. A mode without them keeps the defaults.

    /// Whether AI fighters can be spawned here.
    [[nodiscard]] virtual bool canSpawnFighter() const { return false; }
    /// Spawns an AI fighter with its feet at `feet` (dropped onto the ground) facing `headingDegrees`. Fails with
    /// ErrorCode::InvalidArgument where none can be spawned.
    virtual std::expected<void, Error> spawnFighter(anim::Vec3 /*feet*/, float /*headingDegrees*/) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "no AI fighters here"});
    }
    /// AI fighters in the scene.
    [[nodiscard]] virtual std::size_t fighterCount() const { return 0; }
    /// Removes every AI fighter.
    virtual void clearFighters() {}
    /// Whether an idle fighter takes the player on when he comes within its melee range (ai::AiHumans::engaging()).
    [[nodiscard]] virtual bool fightersEngage() const { return false; }
    virtual void setFightersEngage(bool /*on*/) {}
    /// One line on the fighters: each one's health, top goal and front action, and the player's health.
    [[nodiscard]] virtual std::string fightersState() const { return "-"; }

    // The player's character type (the Player page's Change character). A mode without the configuration's types
    // keeps the defaults.

    /// The types the player can be rebuilt as, in rising order; empty when the configuration is not known here.
    [[nodiscard]] virtual std::vector<CharacterChoice> characterChoices() const { return {}; }
    /// The type the player is now; 0 when not known.
    [[nodiscard]] virtual int playerType() const { return 0; }
    /// One line on what the player is: the type, the model and the health.
    [[nodiscard]] virtual std::string characterState() const { return "-"; }
    /// Rebuilds the player in place as character type `type`, through the mode's own player creation: the model, its
    /// animation set and moves, and what the mode takes from the type's configuration, at full health, keeping the
    /// feet and the heading, with the camera placed behind him again. Fails, leaving the player as he was, with
    /// ErrorCode::InvalidArgument where it cannot be done, ErrorCode::NotFound for a type without a model, and as
    /// loading the character does.
    virtual std::expected<void, Error> changeCharacter(int /*type*/) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "no character types here"});
    }

    // The Animation player (docs/guides/debug-menu.md#animation-player): **Coney's own**, the original has nothing like
    // it. A mode without anim sets keeps the defaults.

    /// The anim sets clips can be played from, by name: the player's own first (his character's clips with the
    /// generic ones it leaves to them), then the others his skeleton can play. Empty when there are none here.
    [[nodiscard]] virtual std::vector<std::string> animSets() const { return {}; }
    /// The clips of anim set `set` (an index into animSets()), in id order; a set not loaded yet is loaded from the
    /// disc. Fails with ErrorCode::InvalidArgument for a set that does not exist, and as loading it does.
    virtual std::expected<std::vector<AnimClipChoice>, Error> animClips(std::size_t /*set*/) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "no anim sets here"});
    }
    /// Plays anim `id` of set `set` on the player from its first frame, as `options` say, then gives him his own
    /// animation back (HumanAnimator::playDebugClip()); the pad does not move him while it plays. Fails with
    /// ErrorCode::InvalidArgument where nothing can play, ErrorCode::NotFound for an id the set has no clip for, and as
    /// animClips() does.
    virtual std::expected<void, Error> playAnim(std::size_t /*set*/, std::uint32_t /*id*/,
                                                const AnimPlayOptions& /*options*/) {
        return std::unexpected(Error{ErrorCode::InvalidArgument, "no anim sets here"});
    }
    /// Stops the clip playAnim() plays, with the idle; nothing when none plays.
    virtual void stopAnim() {}
    /// What playAnim() plays now.
    [[nodiscard]] virtual AnimPlayback animPlayback() const { return {}; }

    /// The in-game HUD the mode draws, for the HUD page; null when it has none.
    [[nodiscard]] virtual hud::Hud* hud() { return nullptr; }

    /// The scene's humans and objects for the Entity inspector page (src/debug/entity_inspect.h); null when the mode
    /// cannot list them.
    [[nodiscard]] virtual const EntityInspect* inspect() const { return nullptr; }
    /// Whether the Entity inspector can freeze entity `id`: the player and the other humans, not an object. False by
    /// default.
    [[nodiscard]] virtual bool canFreezeEntity(EntityId /*id*/) const { return false; }
    /// Whether entity `id` is frozen: it neither moves, animates nor acts until it is let go (the player ignores the
    /// pad, as the Player page's freeze makes him). False for an entity that cannot be frozen.
    [[nodiscard]] virtual bool entityFrozen(EntityId /*id*/) const { return false; }
    /// Freezes entity `id`, or lets it go; nothing for an entity that cannot be frozen.
    virtual void setEntityFrozen(EntityId /*id*/, bool /*frozen*/) {}
};

/// The spot `distance` metres in front of feet `feet` facing `headingDegrees` (0 faces +y, counter-clockwise from
/// above), on the same level.
[[nodiscard]] anim::Vec3 spotAhead(anim::Vec3 feet, float headingDegrees, float distance);

} // namespace coney::debug
