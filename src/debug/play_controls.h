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

    /// The in-game HUD the mode draws, for the HUD page; null when it has none.
    [[nodiscard]] virtual hud::Hud* hud() { return nullptr; }
};

/// The spot `distance` metres in front of feet `feet` facing `headingDegrees` (0 faces +y, counter-clockwise from
/// above), on the same level.
[[nodiscard]] anim::Vec3 spotAhead(anim::Vec3 feet, float headingDegrees, float distance);

} // namespace coney::debug
