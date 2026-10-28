// SPDX-License-Identifier: GPL-3.0-or-later
// The Entity inspector's Model edit over a play mode (docs/guides/debug-menu.md#entity-inspector): player 1, an AI
// human or a sandbox target is drawn as another model where it stands. Only what is drawn changes: the mesh, its
// texture and the skeleton it is skinned with. Its handle, gang, brain and goals, sides and enemies, meters, place,
// heading and the animation it plays stay, as they do when the original fills in a model that loaded late
// (CharacterInstance_SetModel, 0x00174af8). The original never replaces a living human's model
// (docs/research/human-model-change.md), so this is Coney's own debug tooling, in the debug tools' target
// (docs/guides/building.md#debug-tools).
#include <cstddef>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ai/ai_humans.h"
#include "ai/brain.h"
#include "human/human.h"
#include "human/player.h"
#include "platform/play_level_controls.h"
#include "platform/play_level_mode.h"

namespace coney::platform {

namespace {

// The inspector's ids (PlayLevelControls::kInspectPlayerId and the kinds' bases).
constexpr debug::EntityId kPlayerId = PlayLevelControls::kInspectPlayerId;
constexpr debug::EntityId kTargetBase = PlayLevelControls::kInspectTargetBase;
constexpr debug::EntityId kAiBase = PlayLevelControls::kInspectAiBase;

// The index `id` names in the kind whose ids start at `base`.
std::optional<std::size_t> indexIn(debug::EntityId id, debug::EntityId base) {
    return PlayLevelControls::inspectIndexIn(id, base);
}

} // namespace

std::vector<std::string> PlayLevelControls::models() const {
    return m_mode != nullptr ? m_mode->fighterModels() : std::vector<std::string>{};
}

std::string PlayLevelControls::modelOf(const PlayLevelMode& mode, debug::EntityId id) {
    if (id == kPlayerId) {
        return mode.m_model;
    }
    if (const auto index = indexIn(id, kTargetBase); index && *index < mode.m_targets.size()) {
        const std::string& model = mode.m_targets[*index].model;
        return model.empty() ? mode.m_sceneModel : model;
    }
    if (const auto index = indexIn(id, kAiBase); index && *index < mode.m_fighterMeshes.size()) {
        // The model its mesh was given by name (spawned or swapped as), else the one the mode loaded for its
        // character, else the scene's character, which is named in a sandbox (a level's cast falls back to it only
        // when its own did not load, and then says so).
        const PlayLevelMode::FighterMesh& mesh = mode.m_fighterMeshes[*index];
        if (!mesh.model.empty()) {
            return mesh.model;
        }
        const human::PlayerCharacter* character = mesh.character;
        for (const auto& [name, loaded] : mode.m_castCharacters) {
            if (loaded.character.get() == character) {
                return name;
            }
        }
        return character == mode.m_character.get() && mode.m_levelNumber == 0 ? mode.m_sceneModel : std::string{};
    }
    return {};
}

std::expected<void, Error> PlayLevelControls::swapModel(debug::EntityId id, const std::string& model) {
    PlayLevelMode& mode = *m_mode;
    // The character `model` is drawn and skinned as: the scene's own for its model, else one loaded once and kept by
    // the mode (castCharacter(), as the scripts' humans and the AI fighters page load theirs).
    const auto lookFor = [&mode](const std::string& name) -> std::expected<PlayLevelMode::CastLook, Error> {
        if (name == mode.m_sceneModel) {
            return PlayLevelMode::CastLook{.character = mode.m_character.get(), .texture = mode.m_texture};
        }
        const PlayLevelMode::CastLook look = mode.castCharacter(name);
        const auto found = mode.m_castCharacters.find(name);
        if (found == mode.m_castCharacters.end() || found->second.character == nullptr) {
            return fail(ErrorCode::NotFound, std::format("model {} did not load", name));
        }
        return look;
    };
    // The scripts' record of the human with `handle` names the model it is drawn as from now on, so a hand-over to it
    // (takePlace()) draws player 1 as it is seen; its type, and so its class and brain, stay.
    const auto recordModel = [&mode, &model](double handle) {
        if (mode.m_cast.humans == nullptr || handle == 0.0) {
            return;
        }
        if (HumanCreation* made = mode.m_cast.humans->find(handle); made != nullptr) {
            made->model = model;
        }
    };

    // Player 1: his own copy of the character's files, as a hand-over loads them (swapPlayerModel() keeps the old ones
    // while his animation still reads them), and his skeleton from the new model.
    if (id == kPlayerId) {
        auto loaded = PlayLevelMode::loadCharacter(mode.m_engine, mode.m_wad, model);
        if (!loaded) {
            return std::unexpected(std::move(loaded.error()));
        }
        mode.swapPlayerModel(std::move(*loaded), model);
        mode.m_player->human().setSkeleton(&mode.playerCharacter().skeleton());
        recordModel(mode.m_playerHandle);
        return {};
    }

    // A sandbox target: its mesh, and the character it is skinned as.
    if (const auto index = indexIn(id, kTargetBase); index && *index < mode.m_targets.size()) {
        auto look = lookFor(model);
        if (!look) {
            return std::unexpected(std::move(look.error()));
        }
        PlayLevelMode::Target& target = mode.m_targets[*index];
        target.mesh = std::make_unique<CharacterMesh>(look->character->assets().model, look->texture, Skinning::Gpu);
        target.character = look->character == mode.m_character.get() ? nullptr : look->character;
        target.model = model;
        return {};
    }

    // An AI human: its mesh, the character it is skinned as and its skeleton; a script's human's record too.
    if (const auto index = aiIndexOf(id); index && *index < mode.m_fighterMeshes.size()) {
        PlayLevelMode::FighterMesh& mesh = mode.m_fighterMeshes[*index];
        if (mesh.hidden) {
            return fail(ErrorCode::NotFound, "that entity is gone");
        }
        auto look = lookFor(model);
        if (!look) {
            return std::unexpected(std::move(look.error()));
        }
        human::Human& human = *mode.m_ai->humans()[*index].human;
        mesh.mesh = std::make_unique<CharacterMesh>(look->character->assets().model, look->texture, Skinning::Gpu);
        mesh.character = look->character;
        mesh.model = model;
        human.setSkeleton(&look->character->skeleton());
        if (const ai::Brain* brain = mode.m_ai->brainOf(human); brain != nullptr) {
            recordModel(brain->handle());
        }
        return {};
    }
    return fail(ErrorCode::InvalidArgument, "only a human takes another model");
}

} // namespace coney::platform
