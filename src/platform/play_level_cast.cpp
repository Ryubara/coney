// SPDX-License-Identifier: GPL-3.0-or-later
// The play mode's cast: the humans a level's scripts create (`HuCreate`), made AI humans in the scene once the level
// has loaded, each drawn as the model its type names and fighting as its class, its brain bound to the handle the
// scripts name it by (docs/research/ai.md#coney, docs/research/characters.md#creation).
#include <array>
#include <expected>
#include <format>
#include <memory>
#include <numbers>
#include <string>
#include <utility>

#include "ai/ai_config.h"
#include "ai/scripted_brains.h"
#include "characters/character_types.h"
#include "platform/play_level_mode.h"
#include "scripting/sound_bindings.h"

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
    m_ai->brains().setCollision(&m_scenery->collision());
    // The humans created so far, and the calls held for them, in the scripts' order; later ones as they come.
    const std::size_t held = cast.scripted->held();
    // A human the scripts delete leaves the world (its brain unbound by then).
    cast.scripted->setRemover([this](ai::Brain& brain) { m_ai->remove(brain.human()); });
    // HuSwitchPlayer hands the pad to a team-mate; the human left behind fights as its class's brain. Outside level99
    // a kind-0 gang's human may take over too.
    cast.scripted->setSwitcher(
        [this](ai::Brain& from, ai::Brain& to) {
            m_ai->switchPlayer(to, castBrainType(from.characterClass()));
            m_print(std::format("player: the pad passes from human {} to human {}\n", from.handle(), to.handle()));
        },
        sceneName() != "level99");
    // HuChangePlayerGang hands player 1 to a member of another gang: the player takes its place.
    cast.scripted->setHandOver([this](const ai::Brain& to) { takePlace(to); });
    // GoalPlayDynAnimation's clips are the level's dynamic clips, loaded from the disc when first named.
    cast.scripted->setClipSource([this](std::string_view name) { return m_dynamicClips.find(name); });
    // The AI goals' lines (a boss's taunts and shouts) go to the game's sound, when there is one.
    cast.scripted->setSpeech([this](double handle, int command, bool interrupt) {
        if (m_sound != nullptr) {
            static_cast<void>(m_sound->sayCommand(script::CommandCall{.human = handle,
                                                                      .command = static_cast<std::uint32_t>(command),
                                                                      .interrupt = interrupt},
                                                  {}));
        }
    });
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

void PlayLevelMode::takePlace(const ai::Brain& to) {
    // **Coney's stand-in** for HuChangePlayerGang's hand-over (docs/research/characters.md#level99-handover): the
    // original makes the chosen AI human player 1 and leaves the old one in the world as an AI until its gang is
    // deleted. Coney has one player human, so he takes the chosen human's place instead: put where it stands (spawned
    // there: nothing held, the camera behind him), drawn as its model, named by its handle; the chosen human leaves the
    // world, and the brains name the player by its handle (ai::ScriptedBrains::changePlayerGang()).
    const human::Human& chosen = to.human();
    const human::PlayerStart start{.position = chosen.position(),
                                   .headingDegrees = chosen.heading() * 180.0F / std::numbers::pi_v<float>};
    const double oldHandle = m_ai->playerBrain().handle();
    m_player->teleport(&m_scenery->collision(), start);
    m_player->setStart(start);
    takeHatOf(chosen);
    m_ai->remove(chosen);
    // The scenes know player 1 by the new handle from now on (level99's l99_c2 joins him by it).
    m_playerHandle = to.handle();
    // So does every lookup of player 1 in the scripts' humans (his thefts and pick-ups, his crew, his teleports).
    if (m_cast.humans != nullptr) {
        m_cast.humans->setPlayer(1, to.handle());
    }
    const HumanCreation* made = m_cast.humans != nullptr ? m_cast.humans->find(to.handle()) : nullptr;
    if (made != nullptr && !made->model.empty() && made->model != m_model) {
        if (auto loaded = loadCharacter(m_engine, m_wad, made->model); loaded) {
            swapPlayerModel(std::move(*loaded), made->model);
        } else {
            m_print(std::format("cast: model {} not loaded ({}); player 1 keeps {}\n", made->model,
                                loaded.error().message, m_model));
        }
    }
    if (made != nullptr) {
        m_type = made->type;
    }
    m_print(std::format("player: handed to human {} (was {}) at ({:.2f}, {:.2f}, {:.2f})\n", to.handle(), oldHandle,
                        start.position.x, start.position.y, start.position.z));
}

ai::BrainType PlayLevelMode::castBrainType(int type) const {
    const characters::CharacterType* found = m_types.find(type);
    const int powerClass = found != nullptr && found->powerClass ? *found->powerClass : ai::kFighterPowerClass;
    return m_cast.recorded != nullptr ? ai::aiConfigFrom(*m_cast.recorded, type, powerClass).fighter.brain
                                      : ai::AiConfig{}.fighter.brain;
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
