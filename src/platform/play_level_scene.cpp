// SPDX-License-Identifier: GPL-3.0-or-later
// The play mode's scenes: the stage a playing scene draws through, the scenes stepped with the mode, player 1 handed
// over to a scene and back, and `--scene`, Coney's test aid that plays one at once (docs/research/scenes.md,
// docs/guides/building.md#playing-a-level).
#include <array>
#include <format>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>

#include "platform/play_level_mode.h"
#include "raycast/collision_mesh.h"
#include "scenes/scene_disc.h"
#include "scenes/scene_player.h"

namespace coney::platform {

namespace {

// The handle the test aid gives player 1 when no script has, and the first it gives its stand-ins (role i is
// kStandInHandle + i).
constexpr double kTestPlayerHandle = 1.0;
constexpr double kStandInHandle = 1000.0;
// The role the test aid binds player 1 to: Rembrandt's cutscene model in level99's scenes.
constexpr std::string_view kPlayerRole = "warrrecv";

// **Coney stand-in** for `--scene`: the model a role's name stands for in level99's scenes (a role names its
// cutscene model, not a Character List entry; in the game the bound humans bring their own). Unknown names are drawn
// as the player.
std::string standInModelOf(std::string_view role) {
    constexpr std::array<std::pair<std::string_view, std::string_view>, 12> kModels{{{"warrcl", "warr_cl"},
                                                                                     {"warrve", "warr_ve"},
                                                                                     {"warrrecv", "warr_re_cv"},
                                                                                     {"warrash", "warr_ty"},
                                                                                     {"warrashcv", "warr_ty_cv"},
                                                                                     {"warrlynx", "warr_ly"},
                                                                                     {"warrjones", "warr_jn"},
                                                                                     {"warrmal", "warr_ma"},
                                                                                     {"warrso01", "warr_so_va1"},
                                                                                     {"warrso02", "warr_so_va2"},
                                                                                     {"warrso03", "warr_so_va3"},
                                                                                     {"warrso04", "warr_so_vb1"}}};
    for (const auto& [name, model] : kModels) {
        if (name == role) {
            return std::string(model);
        }
    }
    return {};
}

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

bool PlayLevelMode::sceneHoldsPlayer() const { return m_scenes != nullptr && m_stage->holds(m_playerHandle); }

double PlayLevelMode::castHandleOf(const ai::AiHuman& fighter) {
    const ai::Brain* brain = m_cast.scripted != nullptr ? m_ai->brainOf(*fighter.human) : nullptr;
    return brain != nullptr ? brain->handle() : 0.0;
}

const ai::AiHuman* PlayLevelMode::castHumanOf(double handle) {
    if (handle == 0.0 || m_cast.scripted == nullptr) {
        return nullptr;
    }
    for (const ai::AiHuman& fighter : m_ai->humans()) {
        if (castHandleOf(fighter) == handle) {
            return &fighter;
        }
    }
    return nullptr;
}

void PlayLevelMode::stepScenes(std::uint64_t nowMs, std::uint16_t buttons) {
    if (m_scenes == nullptr) {
        return;
    }
    m_stage->beginStep(nowMs);
    m_scenes->update(nowMs, buttons);
    m_stage->setCinematic(m_scenes->cinematicActive());
    // Player 1 and the cast, let go, stand where the scene left them (their end marks after a skip), on the ground.
    // **Coney's choice** for a cast human: placed as a spawn places it (Human_Init), its stamina full again.
    for (const SceneStage::Release& release : m_stage->takeReleases()) {
        const float headingDegrees = release.heading * 180.0F / std::numbers::pi_v<float>;
        if (release.human == m_playerHandle) {
            m_player->teleport(&m_scenery->collision(),
                               human::PlayerStart{.position = release.feet, .headingDegrees = headingDegrees});
        } else if (const ai::AiHuman* fighter = castHumanOf(release.human); fighter != nullptr) {
            fighter->human->spawn(&m_scenery->collision(), release.feet, headingDegrees);
        }
    }
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
    for (std::size_t role = 0; role < roleNames.size(); ++role) {
        const double human =
            roleNames[role] == kPlayerRole ? m_playerHandle : kStandInHandle + static_cast<double>(role);
        system.joinHuman(human, id, role, 0);
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
