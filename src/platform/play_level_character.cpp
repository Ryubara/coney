// SPDX-License-Identifier: GPL-3.0-or-later
// The play mode's player character: loading one (the mode's start and a change of character share it) and the debug
// menus' Change character, which rebuilds the player in place as another type (docs/guides/debug-menu.md#pages).
#include "platform/play_level_mode.h"

#include <cstddef>
#include <format>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <rw.h>

#include "characters/character_data.h"

namespace coney::platform {

namespace {

// Radians to degrees, for the headings the player and the fighters are made with.
float degreesOf(float radians) { return radians * 180.0F / std::numbers::pi_v<float>; }

} // namespace

std::expected<PlayLevelMode::LoadedCharacter, Error>
PlayLevelMode::loadCharacter(RenderEngine& engine, const io::Wad& wad, std::string_view model) {
    chunk::ChunkHandlerTable table = chunk::ChunkHandlerTable::withDefaults();
    characters::addCharacterDataHandlers(table);
    addTextureDictionaryHandlers(table);
    auto character = human::PlayerCharacter::load(wad, table, model);
    if (!character) {
        return std::unexpected(std::move(character.error()));
    }
    auto dictionaries = loadTextureDictionaries(wad, *(*character)->assets().textures, table);
    if (!dictionaries) {
        return std::unexpected(std::move(dictionaries.error()));
    }
    if (engine.drawsPixels()) {
        for (TextureDictionary& dictionary : *dictionaries) {
            if (auto converted = dictionary.convertForDrawing(); !converted) {
                return std::unexpected(std::move(converted.error()));
            }
        }
    }
    return LoadedCharacter{.character = std::move(*character), .dictionaries = std::move(*dictionaries)};
}

rw::Texture* PlayLevelMode::textureOf(const std::vector<TextureDictionary>& dictionaries) {
    if (dictionaries.empty()) {
        return nullptr;
    }
    const std::vector<rw::Texture*> textures = dictionaries.front().textures();
    return textures.empty() ? nullptr : textures.front();
}

std::vector<debug::CharacterChoice> PlayLevelMode::characterChoices() const {
    // Every configured type a player can be drawn as: those whose record (or class record) names a model.
    std::vector<debug::CharacterChoice> choices;
    for (const characters::CharacterType& type : m_types.all()) {
        if (std::optional<std::string> model = m_types.modelFor(type.type, 1, m_levelNumber); model) {
            choices.push_back(debug::CharacterChoice{.type = type.type, .model = std::move(*model)});
        }
    }
    return choices;
}

std::string PlayLevelMode::characterState() const {
    const combat::Health& health = m_player->human().health();
    std::string state = std::format("type {} {}, health {}/{}", m_type, m_model, health.value(), health.maximum());
    if (const std::optional<characters::PlayerTraits> traits = m_types.playerTraitsOf(m_type)) {
        state += std::format(", power class {}{}, Warrior class {} ({}% damage)", traits->powerClassId,
                             traits->powerClass ? "" : " (not configured)", traits->warriorClass,
                             traits->damagePercent == 0 ? 100 : traits->damagePercent);
    }
    return state;
}

std::expected<void, Error> PlayLevelMode::changeCharacter(int type) {
    // **Coney choice**: not in a level whose scripts drive the cast, whose brains hold the player by his handle.
    if (m_cast.brains != nullptr) {
        return fail(ErrorCode::InvalidArgument, "the level's scripts hold the player; change him in a sandbox");
    }
    // The type must be configured and name a model a player of it is drawn as.
    if (m_types.find(type) == nullptr) {
        return fail(ErrorCode::NotFound, std::format("type {} is not in the configuration", type));
    }
    const std::optional<std::string> model = m_types.modelFor(type, 1, m_levelNumber);
    if (!model) {
        return fail(ErrorCode::NotFound, std::format("type {} names no model", type));
    }
    // Its files first, so a failure leaves the player as he was.
    auto loaded = loadCharacter(m_engine, m_wad, *model);
    if (!loaded) {
        return std::unexpected(std::move(loaded.error()));
    }

    // Where the player and the fighters stand. The fighters' brains hold the player they fight, so they go with him
    // and are made again round the new one.
    const raycast::CollisionMesh* mesh = &m_scenery->collision();
    const human::Human& before = m_player->human();
    const human::PlayerStart here{.position = before.position(), .headingDegrees = degreesOf(before.heading())};
    const human::PlayerStart start = m_player->start();
    std::vector<human::PlayerStart> fighters;
    for (const ai::AiHuman& fighter : m_ai->humans()) {
        fighters.push_back(human::PlayerStart{.position = fighter.human->position(),
                                              .headingDegrees = degreesOf(fighter.human->heading())});
    }
    const ai::AiConfig aiConfig = m_ai->config();
    const bool engaging = m_ai->engaging();
    m_ai.reset();
    m_fighterMeshes.clear();
    // A target the old player held in a grab is let go: nothing places it any more.
    for (Target& target : m_targets) {
        target.human->setAttached(false);
    }

    // The new player through the same creation as the mode's start, keeping where a fall out of the world puts him
    // back, then moved to where the old one stood with the camera behind him.
    m_player = std::make_unique<human::Player>(*loaded->character, mesh, start, human::playerClassOf(m_types, type));
    m_player->teleport(mesh, here);
    m_mesh = std::make_unique<CharacterMesh>(loaded->character->assets().model, textureOf(loaded->dictionaries));
    m_positions.assign(loaded->character->assets().model.vertices.size(), anim::Vec3{});
    m_normals.assign(m_positions.size(), anim::Vec3{});
    if (m_playerCharacter) {
        m_retired.push_back(std::move(m_playerCharacter));
    }
    m_playerCharacter = std::move(loaded->character);
    m_playerDictionaries = std::move(loaded->dictionaries); // the old ones' mesh went above

    // The fighters again, as the scene's character.
    m_ai = std::make_unique<ai::AiHumans>(*m_player, *m_character, aiConfig);
    m_ai->setEngaging(engaging);
    // The new brains plan on the level's routes like the old ones did.
    if (m_planner) {
        m_ai->brains().setPlanner(m_planner.get());
    }
    for (const human::PlayerStart& fighter : fighters) {
        addFighter(fighter.position, fighter.headingDegrees);
    }
    m_type = type;
    m_model = *model;
    m_lastAnimId = 0;
    const human::Speeds speeds = human::speedsOf(playerCharacter().anims(), human::AnimSlots::player());
    // The menu prints the new state (characterState()); this adds what only the rebuild knows.
    m_print(std::format("player: type {} speeds walk {:.3f}, jog {:.3f}, run {:.3f}, sprint {:.3f} m/s; fighters made "
                        "again: {}\n",
                        type, speeds.walk, speeds.jog, speeds.run, speeds.sprint, fighters.size()));
    return {};
}

} // namespace coney::platform
