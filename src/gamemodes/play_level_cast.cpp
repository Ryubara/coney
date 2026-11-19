// SPDX-License-Identifier: GPL-3.0-or-later
// The level simulation's cast: the humans a level's scripts create (`HuCreate`), made AI humans in the scene once the
// level has loaded, each playing the character its type's model names (the platform draws him as it) and fighting as
// its class, its brain bound to the handle the scripts name it by (docs/research/ai/index.md#in-coney,
// docs/research/characters.md#creation); and the fighters a sandbox layout or the debug menus spawn.
#include <array>
#include <expected>
#include <format>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ai/ai_config.h"
#include "ai/ai_humans.h"
#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/route_planner.h"
#include "ai/scripted_brains.h"
#include "characters/character_choice.h"
#include "characters/character_types.h"
#include "gamemodes/play_level_simulation.h"
#include "human/human.h"
#include "human/player.h"
#include "human/tag_colour.h"
#include "raycast/collision_mesh.h"
#include "scripting/sound_bindings.h"

namespace coney {

namespace {

// How far above a fighter's spot the drop onto the ground starts, and half how far it reaches, in metres (as a
// teleport's).
constexpr float kFighterDrop = 2.0F;

} // namespace

void PlayLevelSimulation::makePlanner() {
    if (const world::PathMap* paths = m_scenery->pathMap(); paths != nullptr) {
        m_planner = std::make_unique<ai::RoutePlanner>(*paths);
        m_planner->setGroundProbe(ai::groundProbe(m_scenery->collision()));
    }
}

void PlayLevelSimulation::makeLayoutFighters(const ai::AiConfig& fighters) {
    m_ai = std::make_unique<ai::AiHumans>(*m_player, *m_character, fighters);
    m_ai->brains().setPlanner(m_planner.get());
    m_ai->brains().setCollision(&m_scenery->collision());
    for (const human::Place& point : m_scenery->fighters()) {
        addFighter(point.feet, point.headingDegrees);
    }
    if (m_ai->count() > 0) {
        m_print(std::format("fighters: {} from the layout ({} configuration calls read)\n", m_ai->count(),
                            m_ai->config().callsRead));
    }
}

void PlayLevelSimulation::makeCast(const ScriptedCast& cast, const ai::AiConfig& fighters) {
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
    // A script's placement of a human drops it onto the level's collision, unless a scene holds it.
    cast.scripted->setPlaceGround([this](double human) { return placeGround(human); });
    // GoalPlayDynAnimation's clips are the level's dynamic clips, loaded from the disc when first named.
    cast.scripted->setClipSource([this](std::string_view name) { return m_dynamicClips.find(name); });
    // The AI goals' lines (a boss's taunts and shouts, the dealer's lines) go to the game's sound, when there is one.
    cast.scripted->setSpeech([this](double handle, int command, bool interrupt, double target) {
        if (m_sound != nullptr) {
            static_cast<void>(m_sound->sayCommand(script::CommandCall{.human = handle,
                                                                      .command = static_cast<std::uint32_t>(command),
                                                                      .interrupt = interrupt,
                                                                      .target = target},
                                                  {}));
        }
    });
    // The AI's gestures (the waiting attackers' taunts) take the game's sound's gesture slots, when there is one.
    cast.scripted->setGestureTest([this](const ai::Brain& brain) {
        if (m_sound == nullptr) {
            return true;
        }
        const anim::Vec3 feet = brain.human().position();
        script::HumanSoundCall who;
        who.human = brain.handle();
        who.characterType = brain.characterClass();
        who.position = {feet.x, feet.y, feet.z};
        who.gestureKind = ai::reactionKind(brain);
        who.gestureSlots = ai::helpLevel(brain);
        return m_sound->mayGesture(who);
    });
    cast.scripted->release([this](const HumanCreation& human) { return castHuman(human); });
    m_print(std::format("cast: {} humans from the level's scripts, {} AI ({} models); {} calls held for them run\n",
                        cast.humans != nullptr ? cast.humans->all().size() : 0, m_ai->count(), m_castCharacters.size(),
                        held));
}

ai::Brain* PlayLevelSimulation::castHuman(const HumanCreation& human) {
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
    ai::AiConfig config =
        m_cast.recorded != nullptr ? ai::aiConfigFrom(*m_cast.recorded, human.type, powerClass) : ai::AiConfig{};
    // HuCreate's flag makes him a gang member unless his class makes Warriors (docs/research/human-init.md#brain-type).
    config.fighter.brain = ai::createdBrainType(config.fighter.brain, human.gangFlag);
    const human::PlayerCharacter& character = castCharacter(human.model);
    // Snapped to the ground as HuCreate snaps (Human::spawn: a 2.5 m ray from 1 m above).
    const std::array<float, 3>& p = *human.position;
    ai::Brain& brain =
        m_ai->spawn(character, config, &m_scenery->collision(), anim::Vec3{p[0], p[1], p[2]}, human.headingDegrees);
    // Human_Init's tag colour: a civilian woman's is ff ef ef ef, everyone else keeps the default; the brain type is
    // the one after the `+0x19d` remap (docs/research/human-init.md#brain-type).
    if (config.fighter.brain == ai::BrainType::Civilian && type != nullptr && type->female.value_or(0) == 1) {
        brain.human().script().tagColour = human::kFemaleCivilianTagColour;
    }
    // The character he plays, and what draws him.
    m_played[&brain.human()] = Played{.character = &character, .model = human.model};
    humanMade(brain.human(), human.model);
    return &brain;
}

void PlayLevelSimulation::takePlace(const ai::Brain& to) {
    // **Coney's stand-in** for HuChangePlayerGang's hand-over (docs/research/characters.md#level99-handover): the
    // original makes the chosen AI human player 1 and leaves the old one in the world as an AI until its gang is
    // deleted. Coney has one player human, so he takes the chosen human's place instead: put where it stands (placed
    // there, the camera following him there by its own rules), standing afresh as the chosen human does (nothing of the
    // old body's fight carried over) with its meters as Human_MakePlayer leaves them (its own health scaled by its
    // Warrior class, full power and stamina, human::makePlayerOf()), drawn as its model, named by its handle; the
    // chosen human leaves the world, and the brains name the player by its handle
    // (ai::ScriptedBrains::changePlayerGang()).
    const human::Human& chosen = to.human();
    const human::PlayerStart start{.position = chosen.position(),
                                   .headingDegrees = chosen.heading() * 180.0F / std::numbers::pi_v<float>};
    const double oldHandle = m_ai->playerBrain().handle();
    m_player->teleport(&m_scenery->collision(), start);
    m_player->setStart(start);
    const HumanCreation* made = m_cast.humans != nullptr ? m_cast.humans->find(to.handle()) : nullptr;
    const std::optional<characters::PlayerTraits> traits =
        made != nullptr ? m_types.playerTraitsOf(made->type) : std::nullopt;
    human::makePlayerOf(m_player->human(), chosen, traits ? traits->healthPercent : 0);
    takeHatOf(chosen);
    m_ai->remove(chosen);
    // The scenes know player 1 by the new handle from now on (level99's l99_c2 joins him by it).
    m_playerHandle = to.handle();
    // So does every lookup of player 1 in the scripts' humans (his thefts and pick-ups, his crew, his teleports).
    if (m_cast.humans != nullptr) {
        m_cast.humans->setPlayer(1, to.handle());
    }
    if (made != nullptr && !made->model.empty() && made->model != m_model) {
        changePlayerModel(made->model);
    }
    if (made != nullptr) {
        m_type = made->type;
    }
    m_print(std::format("player: handed to human {} (was {}) at ({:.2f}, {:.2f}, {:.2f}), health {} of {}\n",
                        to.handle(), oldHandle, start.position.x, start.position.y, start.position.z,
                        m_player->human().health().value(), m_player->human().health().maximum()));
}

bool PlayLevelSimulation::releasePlayerToAi() {
    if (!m_ai) {
        return false;
    }
    // The brain his class gives (Human_Init's choice), as for the human HuSwitchPlayer leaves behind.
    ai::Brain& player = m_ai->playerBrain();
    if (!m_ai->releasePlayer(castBrainType(player.characterClass()))) {
        return false;
    }
    m_print(std::format("player: player 1 (human {}) handed to the AI as brain type {}\n", player.handle(),
                        static_cast<int>(player.type())));
    return true;
}

ai::BrainType PlayLevelSimulation::castBrainType(int type) const {
    const characters::CharacterType* found = m_types.find(type);
    const int powerClass = found != nullptr && found->powerClass ? *found->powerClass : ai::kFighterPowerClass;
    return m_cast.recorded != nullptr ? ai::aiConfigFrom(*m_cast.recorded, type, powerClass).fighter.brain
                                      : ai::AiConfig{}.fighter.brain;
}

const human::PlayerCharacter& PlayLevelSimulation::castCharacter(const std::string& model) {
    auto found = m_castCharacters.find(model);
    if (found == m_castCharacters.end()) {
        // **Coney choice**: a type with no model, or one that fails to load, looks and moves as the scene's character
        // (kept as an empty entry, so the failure is printed once).
        std::expected<std::unique_ptr<human::PlayerCharacter>, Error> loaded =
            model.empty()
                ? std::expected<std::unique_ptr<human::PlayerCharacter>, Error>(fail(ErrorCode::NotFound, "no model"))
                : loadCastCharacter(model);
        if (!loaded) {
            m_print(std::format("cast: model {} not loaded ({}); drawn as {}\n", model.empty() ? "(none)" : model,
                                loaded.error().message, m_model));
        }
        found = m_castCharacters.emplace(model, loaded ? std::move(*loaded) : nullptr).first;
    }
    return found->second != nullptr ? *found->second : *m_character;
}

const human::PlayerCharacter& PlayLevelSimulation::characterOf(const human::Human& human) const {
    const auto found = m_played.find(&human);
    return found != m_played.end() && found->second.character != nullptr ? *found->second.character : *m_character;
}

void PlayLevelSimulation::bindDebugFighter(human::Human& made, const human::PlayerCharacter& character,
                                           const std::string& model) {
    ai::Brain* brain = m_ai->brains().find(made);
    if (brain == nullptr) {
        return;
    }
    // The type it is drawn as: the player's for the scene's own character (a model that did not load falls back to
    // it), else the first configured type a player of which is drawn as `model`. Its voice set follows from it.
    int type = m_type;
    if (&character != m_character.get()) {
        for (const characters::CharacterChoice& choice : characterChoices()) {
            if (choice.model == model) {
                type = choice.type;
                break;
            }
        }
    }
    brain->setCharacterClass(type);
    // In a level with scripts, a handle from the counter `HuCreate` takes its own from, bound as `HuCreate`'s humans
    // are (in no gang): gameplay hands the bound humans' sounds to the game's sound (Human_OnAnimSoundEvent,
    // 0x0021f700, plays every human's clip sounds, whoever made him), and the lines find their speaker by it.
    if (m_cast.scripted != nullptr && m_cast.objectHandles) {
        const double handle = m_cast.objectHandles();
        if (handle != 0.0) {
            m_cast.scripted->bind(handle, *brain);
        }
    }
}

void PlayLevelSimulation::unbindDebugFighters() {
    if (m_cast.scripted == nullptr) {
        return;
    }
    // The ones removeDebugSpawned() takes: marked, still in the world, not the human the pad drives.
    const human::Human* driven = &m_ai->playerBrain().human();
    for (const ai::AiHuman& entry : m_ai->humans()) {
        if (!entry.debugSpawned || entry.removed || entry.human.get() == driven) {
            continue;
        }
        if (const ai::Brain* brain = m_ai->brains().find(*entry.human); brain != nullptr && brain->handle() != 0.0) {
            m_cast.scripted->unbind(brain->handle());
        }
    }
}

void PlayLevelSimulation::addFighter(anim::Vec3 spot, float headingDegrees, const std::string& model, bool debugSpawned,
                                     ai::FighterSide side) {
    // Dropped onto the ground below its spot, as a teleport is.
    raycast::Vec3 feet{spot.x, spot.y, spot.z + kFighterDrop};
    if (!raycast::dropToGround(m_scenery->collision(), kFighterDrop * 2.0F, feet)) {
        feet.z = spot.z;
    }
    // The scene's character, or the one `model` names (loaded once and kept, as the scripts' humans' are; a model that
    // does not load falls back to the scene's, with a line saying so).
    const human::PlayerCharacter& character = model.empty() ? *m_character : castCharacter(model);
    human::Human& made = m_ai->spawnFighter(character, &m_scenery->collision(), anim::Vec3{feet.x, feet.y, feet.z},
                                            headingDegrees, side);
    if (debugSpawned) {
        m_ai->markDebugSpawned(made);
        bindDebugFighter(made, character, model);
    }
    // With every gang slot in use the fighter has no gang, so it is nobody's friend or enemy: say so.
    if (const ai::Brain* brain = m_ai->brainOf(made); brain != nullptr && brain->gang() == nullptr) {
        m_print("fighters: no free gang slot, so a fighter was made with no gang\n");
    }
    m_played[&made] = Played{.character = &character, .model = model};
    humanMade(made, model);
}

double PlayLevelSimulation::castHandleOf(const ai::AiHuman& fighter) {
    const ai::Brain* brain = m_cast.scripted != nullptr ? m_ai->brainOf(*fighter.human) : nullptr;
    return brain != nullptr ? brain->handle() : 0.0;
}

const ai::AiHuman* PlayLevelSimulation::castHumanOf(double handle) {
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

std::vector<characters::CharacterChoice> PlayLevelSimulation::characterChoices() const {
    // Every configured type a player can be drawn as: those whose record (or class record) names a model.
    std::vector<characters::CharacterChoice> choices;
    for (const characters::CharacterType& type : m_types.all()) {
        if (std::optional<std::string> model = m_types.modelFor(type.type, 1, m_levelNumber); model) {
            choices.push_back(characters::CharacterChoice{.type = type.type, .model = std::move(*model)});
        }
    }
    return choices;
}

} // namespace coney
