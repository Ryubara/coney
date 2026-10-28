// SPDX-License-Identifier: GPL-3.0-or-later
// The Entity inspector's edits over a play mode (docs/guides/debug-menu.md#entity-inspector): the edits the debug
// session's input gate applies between two steps to player 1, the AI humans (a level's cast included) and the sandbox
// targets. Coney's
// own debug tooling, in the debug tools' target (docs/guides/building.md#debug-tools): nothing here runs until an edit
// is made.
#include <cmath>
#include <cstddef>
#include <format>
#include <numbers>
#include <optional>
#include <string>

#include "ai/scripted_brains.h"
#include "human/human.h"
#include "human/human_flags.h"
#include "human/player.h"
#include "platform/play_level_controls.h"
#include "raycast/collision_mesh.h"

namespace coney::platform {

namespace {

// How far above a requested spot the ground is looked for, and how far down it is searched, in metres: a teleport finds
// the floor under a point in mid-air as the debug menus' teleport does.
constexpr float kDropAbove = 2.0F;
constexpr float kDropSearch = 100.0F;

// The unit is degrees on the page and radians in the human.
constexpr float kRadiansToDegrees = 180.0F / std::numbers::pi_v<float>;

// A 0/1 flag as a value the log and the page show.
double flagValue(bool on) { return on ? 1.0 : 0.0; }

// `feet` dropped onto the ground below it in `mesh`; nothing when there is none within the search.
std::optional<anim::Vec3> groundBelow(const raycast::CollisionMesh& mesh, anim::Vec3 feet) {
    raycast::Vec3 point{feet.x, feet.y, feet.z + kDropAbove};
    if (!raycast::dropToGround(mesh, kDropSearch, point)) {
        return std::nullopt;
    }
    return anim::Vec3{point.x, point.y, point.z};
}

// Places `human` at `feet` facing `degrees` with Human::place() (the ground, if given, lowers it by a short ray), or by
// Player::place() when `player` owns it (player 1).
void placeHuman(human::Human& human, human::Player* player, const raycast::CollisionMesh* ground, anim::Vec3 feet,
                float degrees) {
    if (player != nullptr) {
        player->place(ground, human::PlayerStart{.position = feet, .headingDegrees = degrees});
    } else {
        human.place(ground, feet, degrees);
    }
}

} // namespace

std::expected<void, Error> PlayLevelControls::deleteAiHuman(std::size_t index) {
    PlayLevelMode& mode = *m_mode;
    const human::Human& human = *mode.m_ai->humans()[index].human;
    // The human the pad drives is player 1 for the game: the "player gone" path (camera, HUD, the scripts'
    // HuIsAPlayer) is not ported, so it is refused rather than half done (#723).
    if (&human == &mode.m_ai->playerBrain().human()) {
        return fail(ErrorCode::InvalidArgument, "the human the pad drives cannot be deleted yet");
    }
    // A level script's human goes the way HuDelete takes it: unbound, out of its gang and formation, then out of the
    // world at the end of the next step; the rest (a debug fighter, a layout's) through the world's removal alone.
    const ai::Brain* brain = mode.m_ai->brainOf(human);
    if (mode.m_cast.scripted != nullptr && brain != nullptr && brain->handle() != 0.0) {
        mode.m_cast.scripted->humanDelete(brain->handle());
    } else {
        mode.m_ai->remove(human);
    }
    return {};
}

std::expected<void, Error> PlayLevelControls::applyEdit(const debug::EntityCommand& command) {
    if (m_mode == nullptr) {
        return fail(ErrorCode::InvalidArgument, "nothing is playing");
    }
    PlayLevelMode& mode = *m_mode;
    const std::vector<debug::EntityRef> entities = entitiesOf(mode);
    const debug::EntityRef* entity = debug::findEntity(entities, command.entity);
    if (entity == nullptr) {
        return fail(ErrorCode::NotFound, "that entity is gone");
    }
    const std::string_view opName = debug::editOpName(command.op);
    // A model swap reaches every human, the sandbox targets too (play_level_model.cpp).
    if (command.op == debug::EditOp::Model) {
        const std::optional<std::string> model = debug::modelWithId(models(), command.values[0]);
        if (!model) {
            return fail(ErrorCode::NotFound, "no model here has that id");
        }
        return swapModel(command.entity, *model);
    }
    human::Human* human = humanOf(command.entity);
    if (human == nullptr) {
        // A sandbox target takes the freeze (the inspector's); an object nothing yet.
        if (entity->kind == debug::EntityKind::Human && command.op == debug::EditOp::Frozen) {
            setEntityFrozen(command.entity, command.values[0] != 0.0);
            return {};
        }
        const char* kind = entity->kind == debug::EntityKind::Object ? "an object" : "a sandbox target";
        return fail(ErrorCode::InvalidArgument, std::format("{} cannot take {} yet", kind, opName));
    }
    const bool player = human == &mode.m_player->human();
    human::Player* const owner = player ? mode.m_player.get() : nullptr;
    const double value = command.values[0];
    const float number = static_cast<float>(value);
    switch (command.op) {
    case debug::EditOp::Teleport: {
        // Dropped onto the ground below the spot, as a start is; left where it is over nothing.
        const anim::Vec3 asked{static_cast<float>(command.values[0]), static_cast<float>(command.values[1]),
                               static_cast<float>(command.values[2])};
        const anim::Vec3 feet = groundBelow(mode.m_scenery->collision(), asked).value_or(asked);
        const float heading = human->heading() * kRadiansToDegrees;
        if (player) {
            mode.teleport(human::Place{.name = "entity editor", .feet = feet, .headingDegrees = heading});
        } else {
            placeHuman(*human, nullptr, &mode.m_scenery->collision(), feet, heading);
            human->resetMotion();
            mode.brainPlaced(*human);
        }
        return {};
    }
    case debug::EditOp::Heading:
        placeHuman(*human, owner, nullptr, human->position(), number);
        mode.brainPlaced(*human);
        return {};
    case debug::EditOp::DropToGround: {
        const std::optional<anim::Vec3> ground = groundBelow(mode.m_scenery->collision(), human->position());
        if (!ground) {
            return fail(ErrorCode::NotFound, "no ground below");
        }
        placeHuman(*human, owner, nullptr, *ground, human->heading() * kRadiansToDegrees);
        mode.brainPlaced(*human);
        return {};
    }
    case debug::EditOp::Health:
        human->setHealthShare(number);
        return {};
    case debug::EditOp::Stamina:
        human->setStaminaShare(number);
        return {};
    case debug::EditOp::Power:
        human->setPowerShare(number);
        return {};
    case debug::EditOp::Rage:
        human->setRageShare(number);
        return {};
    case debug::EditOp::Invincible:
        human->setFlag(human::flag::kGod, value != 0.0);
        return {};
    case debug::EditOp::Kill:
        human->kill();
        return {};
    case debug::EditOp::Scale:
        if (!(value > 0.0) || human->configuredScale() <= 0.0F) {
            return fail(ErrorCode::InvalidArgument, "the scale must be above 0");
        }
        // The body, its collision (unless kept) and the drawn mesh together; the mesh relative to the configured size,
        // so a human at its configured scale is drawn as it always was.
        human->setScale(number);
        human->setMeshScale(number / human->configuredScale());
        return {};
    case debug::EditOp::ScaleCollision:
        human->setCollisionFollowsMesh(value != 0.0);
        return {};
    case debug::EditOp::AnimRate:
        human->setAnimRate(number);
        return {};
    case debug::EditOp::SpeedScale:
        human->setSpeedScale(number);
        return {};
    case debug::EditOp::Frozen:
        setEntityFrozen(command.entity, value != 0.0);
        return {};
    case debug::EditOp::Model:
        // Taken above, before the human is looked up.
        break;
    case debug::EditOp::Delete: {
        if (player) {
            return fail(ErrorCode::InvalidArgument,
                        "player 1 cannot be deleted yet: the player-gone path is not ported");
        }
        if (const std::optional<std::size_t> index = aiIndexOf(command.entity)) {
            return deleteAiHuman(*index);
        }
        return fail(ErrorCode::NotFound, "that entity is gone");
    }
    }
    return fail(ErrorCode::InvalidArgument, std::format("unknown edit {}", opName));
}

std::optional<std::array<double, 3>> PlayLevelControls::configuredValues(debug::EntityId entity,
                                                                         debug::EditOp op) const {
    const human::Human* human = humanOf(entity);
    if (human == nullptr) {
        return std::nullopt;
    }
    switch (op) {
    case debug::EditOp::Health:
    case debug::EditOp::Stamina:
    case debug::EditOp::Power:
        return std::array<double, 3>{100.0, 0.0, 0.0};
    case debug::EditOp::Rage:
    case debug::EditOp::Invincible:
    case debug::EditOp::Frozen:
        return std::array<double, 3>{0.0, 0.0, 0.0};
    case debug::EditOp::Scale:
        return std::array<double, 3>{static_cast<double>(human->configuredScale()), 0.0, 0.0};
    case debug::EditOp::ScaleCollision:
    case debug::EditOp::AnimRate:
    case debug::EditOp::SpeedScale:
        return std::array<double, 3>{1.0, 0.0, 0.0};
    default:
        // A position, a heading, a kill or a delete has no configured value to go back to.
        return std::nullopt;
    }
}

std::optional<double> PlayLevelControls::currentValue(debug::EntityId entity, debug::EditOp op) const {
    // The model is known for a sandbox target too, which has no human::Human.
    if (op == debug::EditOp::Model) {
        const std::string model = m_mode != nullptr ? modelOf(*m_mode, entity) : std::string{};
        return model.empty() ? std::nullopt : std::optional<double>(debug::modelId(model));
    }
    const human::Human* human = humanOf(entity);
    if (human == nullptr) {
        return std::nullopt;
    }
    // A meter as a percentage of its maximum.
    const auto share = [](double value, double maximum) { return maximum > 0.0 ? 100.0 * value / maximum : 0.0; };
    switch (op) {
    case debug::EditOp::Heading: {
        double degrees = std::fmod(static_cast<double>(human->heading() * kRadiansToDegrees), 360.0);
        return degrees < 0.0 ? degrees + 360.0 : degrees;
    }
    case debug::EditOp::Health:
        return share(human->health().value(), human->health().maximum());
    case debug::EditOp::Stamina:
        return share(human->stamina().value(), human->stamina().maximum());
    case debug::EditOp::Power: {
        const combat::PowerMeter& power = human->fighter().combat().power();
        return share(power.value(), power.maximum());
    }
    case debug::EditOp::Rage: {
        const combat::RageMeter& rage = human->fighter().combat().rage();
        return share(rage.value(), rage.maximum());
    }
    case debug::EditOp::Invincible:
        return flagValue(human->hasFlag(human::flag::kGod));
    case debug::EditOp::Scale:
        return static_cast<double>(human->scale());
    case debug::EditOp::ScaleCollision:
        return flagValue(human->collisionFollowsMesh());
    case debug::EditOp::AnimRate:
        return static_cast<double>(human->animRate());
    case debug::EditOp::SpeedScale:
        return static_cast<double>(human->speedScale());
    case debug::EditOp::Frozen:
        return flagValue(entityFrozen(entity));
    default:
        return std::nullopt;
    }
}

} // namespace coney::platform
