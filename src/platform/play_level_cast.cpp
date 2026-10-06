// SPDX-License-Identifier: GPL-3.0-or-later
// The play mode's cast: the humans a level's scripts create (`HuCreate`), made AI humans in the scene once the level
// has loaded, each drawn as the model its type names and fighting as its class, its brain bound to the handle the
// scripts name it by (docs/research/ai.md#coney, docs/research/characters.md#creation).
#include <array>
#include <expected>
#include <format>
#include <memory>
#include <string>
#include <utility>

#include "ai/ai_config.h"
#include "ai/scripted_brains.h"
#include "characters/character_types.h"
#include "platform/play_level_mode.h"

namespace coney::platform {

void PlayLevelMode::makeCast(const ScriptedCast& cast, const ai::AiConfig& fighters) {
    m_cast = cast;
    // Player 1's cameras, which the level script set up: the player steps them and draws through the current one.
    if (cast.cameras != nullptr) {
        m_player->setCameras(cast.cameras);
    }
    // The AI on the level's brains, whose gangs the scripts made, planning on the level's routes.
    m_ai = std::make_unique<ai::AiHumans>(*m_player, *m_character, fighters, *cast.brains);
    m_ai->brains().setPlanner(m_planner.get());
    // The humans created so far, and the calls held for them, in the scripts' order; later ones as they come.
    const std::size_t held = cast.scripted->held();
    cast.scripted->release([this](const HumanCreation& human) { return castHuman(human); });
    m_print(std::format("cast: {} humans from the level's scripts, {} AI ({} models); {} calls held for them run\n",
                        cast.humans != nullptr ? cast.humans->all().size() : 0, m_ai->count(), m_castCharacters.size(),
                        held));
}

ai::Brain* PlayLevelMode::castHuman(const HumanCreation& human) {
    // Player 1's first creation is the player the mode made at its start.
    if (human.playerIndex == 1 && !m_castPlayerBound) {
        m_castPlayerBound = true;
        m_cast.scripted->setPlayer(&m_ai->playerBrain());
        return &m_ai->playerBrain();
    }
    if (!human.position) {
        m_print(std::format("cast: {} (type {}) has no position; not made\n", human.name, human.type));
        return nullptr;
    }
    // Its class: the type's CfgChar record and the power class it names (the fighters' when it names none).
    const characters::CharacterType* type = m_types.find(human.type);
    const int powerClass = type != nullptr && type->powerClass ? *type->powerClass : ai::kFighterPowerClass;
    const ai::AiConfig config =
        m_cast.recorded != nullptr ? ai::aiConfigFrom(*m_cast.recorded, human.type, powerClass) : ai::AiConfig{};
    const CastLook look = castCharacter(human.model);
    // Snapped to the ground as HuCreate snaps (Human::spawn: a 2.5 m ray from 1 m above).
    const std::array<float, 3>& p = *human.position;
    ai::Brain& brain = m_ai->spawn(*look.character, config, &m_scenery->collision(), anim::Vec3{p[0], p[1], p[2]},
                                   human.headingDegrees);
    FighterMesh mesh;
    mesh.character = look.character;
    mesh.mesh = std::make_unique<CharacterMesh>(look.character->assets().model, look.texture);
    mesh.positions.resize(look.character->assets().model.vertices.size());
    mesh.normals.resize(mesh.positions.size());
    m_fighterMeshes.push_back(std::move(mesh));
    return &brain;
}

PlayLevelMode::CastLook PlayLevelMode::castCharacter(const std::string& model) {
    auto found = m_castCharacters.find(model);
    if (found == m_castCharacters.end()) {
        // **Coney choice**: a type with no model, or one that fails to load, looks and moves as the scene's character
        // (kept as an empty entry, so the failure is printed once).
        std::expected<LoadedCharacter, Error> loaded =
            model.empty() ? std::expected<LoadedCharacter, Error>(fail(ErrorCode::NotFound, "no model"))
                          : loadCharacter(m_engine, m_wad, model);
        if (!loaded) {
            m_print(std::format("cast: model {} not loaded ({}); drawn as {}\n", model.empty() ? "(none)" : model,
                                loaded.error().message, m_model));
        }
        found = m_castCharacters.emplace(model, loaded ? std::move(*loaded) : LoadedCharacter{}).first;
    }
    const LoadedCharacter& entry = found->second;
    if (entry.character == nullptr) {
        return CastLook{.character = m_character.get(), .texture = m_texture};
    }
    return CastLook{.character = entry.character.get(), .texture = textureOf(entry.dictionaries)};
}

} // namespace coney::platform
