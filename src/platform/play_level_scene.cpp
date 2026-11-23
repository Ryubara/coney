// SPDX-License-Identifier: GPL-3.0-or-later
// The play mode's scenes: the stage a playing scene draws through, the scenes stepped with the mode, player 1 handed
// over to a scene and back, and `--scene`, Coney's test aid that plays one at once (docs/research/scenes.md,
// docs/guides/running-play-level.md#playing-a-level).
#include <algorithm>
#include <cstdint>
#include <format>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/game_events.h"
#include "gamemodes/level_pickups.h"
#include "human/human.h"
#include "human/human_sounds.h"
#include "human/humans.h"
#include "platform/play_level_mode.h"
#include "raycast/collision_mesh.h"
#include "scenes/scene_casts.h"
#include "scenes/scene_disc.h"
#include "scenes/scene_player.h"
#include "world_objects/object_types.h"
#include "world_objects/spawn_records.h"

namespace coney::platform {

namespace {

// The handle the test aid gives player 1 when no script has, and the first it gives its stand-ins (role i is
// kStandInHandle + i).
constexpr double kTestPlayerHandle = 1.0;
constexpr double kStandInHandle = 1000.0;
// The role the test aid binds player 1 to: Rembrandt's cutscene model in level99's scenes.
constexpr std::string_view kPlayerRole = "warrrecv";
// The ground under a scene's human: a ray from 1 m above his feet, 1.5 m down, as each update's snap
// (docs/research/sound-events.md#ground-material).
constexpr float kSceneGroundAbove = 1.0F;
constexpr float kSceneGroundLength = 1.5F;

// **Coney stand-in**: a bound handle that is no human of the level, and not one of the preview's stand-ins (which
// SceneStage::drawAs() casts), is drawn as the model its role's name stands for.
using scenes::standInModelOf;

} // namespace

void PlayLevelMode::makeStage() {
    m_stage = std::make_unique<SceneStage>(
        [this](std::string_view model) -> std::expected<StageCharacter, Error> {
            auto loaded = loadCharacter(m_engine, m_wad, model);
            if (!loaded) {
                return std::unexpected(std::move(loaded.error()));
            }
            return StageCharacter{.character = std::move(loaded->character),
                                  .dictionaries = std::move(loaded->dictionaries)};
        },
        [this](double human, std::size_t /*role*/, std::string_view roleName) -> std::string {
            if (human == m_playerHandle || castHumanOf(human) != nullptr) {
                return {}; // the mode draws its own player and the level's cast
            }
            std::string model = standInModelOf(roleName);
            return model.empty() ? m_model : model;
        },
        m_print);
    // Player 1 or a cast human in a grab or a mount is left out of the scene's start (scenes.md#humans).
    m_stage->setFreeTest([this](double human) {
        if (human == m_playerHandle) {
            return !m_player->human().fighter().inPair();
        }
        const ai::AiHuman* fighter = castHumanOf(human);
        return fighter == nullptr || !fighter->human->fighter().inPair();
    });
    // A human a script binds to a role is reset first (scenes.md#join-reset): his carried prop goes, his pair breaks
    // and he is back in his normal mode, so the free test above takes in a human the player was holding (l99_c7's
    // Rudy, held in Rembrandt's power move). A handle that is no human here (a stand-in) has nothing to reset, but
    // every bind, also a stand-in's, ends every raging human's rage.
    m_stage->setJoinReset([this](double handle) {
        if (handle == m_playerHandle) {
            m_player->human().resetForJoin();
            dropReleasedProp(m_player->human());
        } else if (const ai::AiHuman* fighter = castHumanOf(handle); fighter != nullptr) {
            fighter->human->resetForJoin();
            dropReleasedProp(*fighter->human);
        }
        human::endAllRage(m_player->humans().humans());
    });
    // Placed at the release, before the scene's end function can move them on.
    m_stage->setReleaseHandler([this](const SceneStage::Release& release) { placeReleased(release); });
    // Player 1's brain is off from his join until the scene lets him go, which takes the pad away and gives it back
    // (BrDead(brain, 1) at the join, BrDead(brain, 0) in the join goal's destroy, scenes.md#humans). So a script that
    // switched his brain off before a scene (level5's boss stages) has the pad back after it.
    // @orig 0x002e5300 Goal_JoinCinematic (unknown)
    // Research: docs/research/scenes.md#humans
    m_stage->setJoinHandler([this](double human, bool joined) {
        if (human == m_playerHandle && m_ai != nullptr) {
            m_ai->playerBrain().setDead(joined);
        }
    });
    // A bound human's role clip sounds (event 11) as his gameplay clips do, at the scene's place for him and on the
    // ground there (**Coney's reading**: the scene moves the human himself in the original, so his snap finds that
    // ground; Coney poses him without moving his body). A stand-in the stage draws has no body and stays silent.
    // Research: docs/research/sound.md#anim-sounds
    m_stage->setAnimSoundHandler([this](double handle, std::uint32_t id, std::optional<anim::Vec3> feet) {
        human::Human* body = nullptr;
        if (handle == m_playerHandle) {
            body = &m_player->human();
        } else if (const ai::AiHuman* fighter = castHumanOf(handle); fighter != nullptr) {
            body = fighter->human.get();
        }
        if (body == nullptr) {
            return;
        }
        human::HumanSound sound{
            .kind = human::HumanSound::Kind::Anim, .animSound = id, .at = {}, .sceneFeet = std::nullopt};
        if (feet) {
            sound.sceneFeet = feet;
            sound.sceneGround = body->groundMaterial();
            const raycast::Ray ray{.origin = raycast::Vec3{feet->x, feet->y, feet->z + kSceneGroundAbove},
                                   .direction = raycast::kDown,
                                   .length = kSceneGroundLength};
            if (const std::optional<raycast::RayHit> hit = m_scenery->collision().rayCast(ray, {}, 0); hit) {
                sound.sceneGround = hit->material;
            }
        }
        body->reportSound(sound);
    });
    // A bound object stays where the scene leaves it (scenes.md#ending): its record keeps the place.
    m_stage->setObjectMover([this](double object, anim::Vec3 position, anim::Quat rotation) {
        if (m_pickups != nullptr) {
            m_pickups->placeObject(object, position, rotation);
        }
    });
}

void PlayLevelMode::attachScenes(scenes::SceneSystem* scenes, double playerHandle) {
    if (m_scenes != nullptr) {
        m_scenes->setHost(nullptr);
    }
    m_scenes = scenes;
    m_playerHandle = playerHandle;
    if (m_scenes != nullptr) {
        m_scenes->setHost(m_stage.get());
    }
}

bool PlayLevelMode::sceneHolds(double human) const { return m_scenes != nullptr && m_stage->holds(human); }

bool PlayLevelMode::letterboxShown(std::uint64_t nowMs) const { return m_stage->barHeight(nowMs) > 0.0F; }

void PlayLevelMode::stepScenes(std::uint64_t nowMs, std::uint16_t buttons) {
    if (m_scenes == nullptr) {
        return;
    }
    m_stage->beginStep(nowMs);
    m_stage->setWidescreen(m_engine.widescreen());
    m_scenes->update(nowMs, buttons);
    m_stage->setCinematic(m_scenes->cinematicActive());
    // Network play: the scenes that started or ended in this update.
    raiseSceneChanges(gameEvents(), m_scenesPlaying, m_scenes->playingScenes());
}

void PlayLevelMode::placeReleased(const SceneStage::Release& release) {
    // Player 1 and the cast, let go, stand where the scene left them, placed as a teleport places (Human::place()), so
    // they keep their health, meters and clip, and a pair is broken, the partner playing its reaction. After a skip the
    // original places each at its end pose through Human_SetTransform once it is out of the scene, so the drop onto the
    // ground applies (docs/research/scenes.md#ending). **Coney's choice** after a clip played to its end: the original
    // places no one (its clip moved the human there), but Coney's scene drives a frame, not the human, so it is placed
    // where the clip left it, with no drop. The motion reset is the one the original makes at the scene's start
    // (scenes.md#humans); Coney's held human has not moved since. The brain's front move is popped only after a skip,
    // where the original's Human_SetTransform pops it: after a clip played to its end the original places no one, so
    // nothing pops.
    const float headingDegrees = release.heading * 180.0F / std::numbers::pi_v<float>;
    const raycast::CollisionMesh* ground = release.skipped ? &m_scenery->collision() : nullptr;
    if (release.human == m_playerHandle) {
        m_player->teleport(ground, human::PlayerStart{.position = release.feet, .headingDegrees = headingDegrees});
        if (release.skipped) {
            brainPlaced(m_player->human());
        }
    } else if (const ai::AiHuman* fighter = castHumanOf(release.human); fighter != nullptr) {
        ai::Brain* brain = m_ai != nullptr ? m_ai->brainOf(*fighter->human) : nullptr;
        ai::placeAfterScene(*fighter->human, brain, ground, release.feet, headingDegrees, release.skipped);
        // His scene clip over, he goes back to his idle with its fade, whose held flag keeps him out of the help calls
        // of the first five updates (a Destroyer at level80's scene end, docs/research/scenes.md#ending).
        // stand-in: the player is left as he was; whether his own idle fade gates his stick there is not checked.
        fighter->human->settleToIdle();
    }
}

std::optional<double> PlayLevelMode::makeSceneProp(std::string_view type) {
    // An object only of a type the level's scripts configured (CfgObj), with a handle from the world objects' counter.
    if (m_records == nullptr || m_objectTypes == nullptr || !m_cast.objectHandles ||
        m_objectTypes->find(type) == nullptr) {
        m_print(std::format("scene: no object of type {} for the preview\n", type));
        return std::nullopt;
    }
    world_objects::SpawnRecord record;
    record.handle = m_cast.objectHandles();
    record.typeName = std::string(type);
    record.live = true;
    record.pinned = true;
    const double handle = record.handle;
    if (m_records->add(std::move(record)) == nullptr) {
        m_print(std::format("scene: no room for an object of type {}\n", type));
        return std::nullopt;
    }
    return handle;
}

std::expected<void, Error> PlayLevelMode::playScene(std::string_view name) {
    // The level's scenes when its scripts have them, else the test aid's own system over the disc's scene list.
    if (m_scenes == nullptr) {
        auto list = scenes::loadSceneList(m_wad);
        if (!list) {
            return std::unexpected(std::move(list.error()));
        }
        m_ownSceneList = std::make_unique<scenes::SceneList>(std::move(*list));
        m_ownScenes = std::make_unique<scenes::SceneSystem>(*m_ownSceneList, scenes::wadSceneSource(m_wad),
                                                            scenes::SceneSystem::ScriptCall{});
        attachScenes(m_ownScenes.get(), m_playerHandle != 0.0 ? m_playerHandle : kTestPlayerHandle);
    }
    scenes::SceneSystem& system = *m_scenes;
    // Loaded now, then bound as gPlayCutScene binds a scene table's humans, and played as a level99 cinematic.
    const std::uint32_t id = system.preload(name, "");
    auto slot = system.cache().loadNow(id, 0);
    if (!slot) {
        return std::unexpected(std::move(slot.error()));
    }
    const scenes::SceneHeader& header = *(*slot)->header;
    std::vector<std::string> roleNames;
    roleNames.reserve(header.roles.size());
    for (const scenes::SceneRole& role : header.roles) {
        roleNames.push_back(role.name);
    }
    m_stage->setRoleNames(roleNames);
    // The cast the scene's script binds, as the scenes list records it (scene_casts.h,
    // scenes.md#coneys-implementation): player 1 in the role the script gives the player, else the one named
    // kPlayerRole; a stand-in drawn as the character type the script binds in each other bound role; nobody in a role
    // the script leaves unbound.
    std::vector<scenes::CastModel> cast;
    cast.reserve(roleNames.size());
    for (std::size_t role = 0; role < roleNames.size(); ++role) {
        cast.push_back(scenes::castModelOf(header.name, role, roleNames[role]));
    }
    auto playerRole = std::ranges::find(cast, scenes::CastSource::Player, &scenes::CastModel::source) - cast.begin();
    if (static_cast<std::size_t>(playerRole) == cast.size()) {
        playerRole = std::ranges::find(roleNames, kPlayerRole) - roleNames.begin();
    }
    for (std::size_t role = 0; role < roleNames.size(); ++role) {
        if (cast[role].source == scenes::CastSource::Unbound) {
            continue;
        }
        double human = m_playerHandle;
        if (role != static_cast<std::size_t>(playerRole)) {
            human = kStandInHandle + static_cast<double>(role);
            m_stage->drawAs(human, cast[role].model.empty() ? m_model : cast[role].model);
        }
        system.joinHuman(human, id, role, 0);
    }
    // Each object slot gets a new object of the type the script binds there, its record pinned as Scene_BindObject
    // (0x00354280) pins a bound object's, so the streaming never stores it (scenes.md#cast).
    for (std::size_t objectSlot = 0; objectSlot < header.objects.size(); ++objectSlot) {
        if (const std::optional<std::string> type = scenes::propTypeOf(header.name, objectSlot); type) {
            if (const std::optional<double> object = makeSceneProp(*type); object) {
                system.addObject(id, *object, objectSlot);
            }
        }
    }
    if (!system.play(
            id, scenes::PlayRequest{
                    .kind = scenes::PlayKind::Cinematic, .cinematic = true, .skippable = true, .blendCam = -1.0F})) {
        return fail(ErrorCode::Invalid, std::format("scene {} did not start", name));
    }
    m_print(std::format("scene: {} (id {}): {} roles, {} objects, {} frames{}\n", header.name, id, header.roles.size(),
                        header.objects.size(), header.frames, header.hasSegments() ? ", in segments" : ""));
    return {};
}

} // namespace coney::platform
