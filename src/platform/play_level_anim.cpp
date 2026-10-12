// SPDX-License-Identifier: GPL-3.0-or-later
// The play mode's side of the debug menus' Animation player (docs/guides/debug-menu.md#animation-player): the anim
// sets the player can play from and a clip of one played on him (human::Human::playDebugClip()). Coney's own tooling:
// the original has nothing like it, and nothing here runs unless a person uses the page.
#include "platform/play_level_mode.h"

#include <format>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "characters/anim_set.h"
#include "characters/character_data.h"
#include "core/chunk_system.h"

namespace coney::platform {

namespace {

// The sets before the other characters': the player's own, then the generic set.
constexpr std::size_t kPlayerSet = 0;
constexpr std::size_t kGenericSet = 1;

} // namespace

std::vector<std::string> PlayLevelMode::animSets() const {
    std::vector<std::string> sets{std::format("Player: {}", m_model), "Generic"};
    // Every model a player can be drawn as, once each, sorted: the models Change character offers. Every character
    // shares the player's skeleton (docs/research/characters.md, 32 bones), so each can play on him.
    std::set<std::string, std::less<>> models;
    for (const debug::CharacterChoice& choice : characterChoices()) {
        models.insert(choice.model);
    }
    sets.insert(sets.end(), models.begin(), models.end());
    return sets;
}

std::expected<const characters::AnimSet*, Error> PlayLevelMode::animSetAt(std::size_t set) {
    if (set == kPlayerSet) {
        return &playerCharacter().anims();
    }
    if (set == kGenericSet) {
        if (!m_genericAnims) {
            m_genericAnims = std::make_unique<characters::AnimSet>(playerCharacter().generic(), nullptr);
        }
        return m_genericAnims.get();
    }
    const std::vector<std::string> sets = animSets();
    if (set >= sets.size()) {
        return fail(ErrorCode::InvalidArgument, std::format("no anim set {}", set));
    }
    const std::string& model = sets[set];
    auto found = m_animSetCharacters.find(model);
    if (found == m_animSetCharacters.end()) {
        // The model's files, as a player of it would load them (no textures: nothing draws it).
        chunk::ChunkHandlerTable table = chunk::ChunkHandlerTable::withDefaults();
        characters::addCharacterDataHandlers(table);
        auto character = human::PlayerCharacter::load(m_wad, table, model);
        if (!character) {
            return std::unexpected(std::move(character.error()));
        }
        auto own = std::make_unique<characters::AnimSet>((*character)->assets().data, nullptr);
        found = m_animSetCharacters
                    .emplace(model, AnimSetCharacter{.character = std::move(*character), .own = std::move(own)})
                    .first;
        m_print(std::format("anim: {} loaded for the Animation player\n", model));
    }
    return found->second.own.get();
}

std::expected<std::vector<debug::AnimClipChoice>, Error> PlayLevelMode::animClips(std::size_t set) {
    auto anims = animSetAt(set);
    if (!anims) {
        return std::unexpected(std::move(anims.error()));
    }
    return debug::clipChoicesOf(**anims);
}

std::expected<void, Error> PlayLevelMode::playAnim(std::size_t set, std::uint32_t id,
                                                   const debug::AnimPlayOptions& options) {
    if (!(options.speed > 0.0F)) {
        return fail(ErrorCode::InvalidArgument, "the speed must be above 0");
    }
    auto anims = animSetAt(set);
    if (!anims) {
        return std::unexpected(std::move(anims.error()));
    }
    const anim::AnimClip* clip = (*anims)->clip(id);
    if (clip == nullptr || !debug::playableOnPlayer(*clip)) {
        return fail(ErrorCode::NotFound, std::format("anim set {} has no clip for anim {}", set, id));
    }
    // At the set's own rate for the id, times the speed asked for.
    m_player->human().playDebugClip(*clip, id, (*anims)->rate(id) * options.speed, options.loop, options.inPlace);
    m_animSetName = animSets()[set];
    m_animClipName = clip->name;
    return {};
}

debug::AnimPlayback PlayLevelMode::animPlayback() const {
    const human::HumanAnimator& animator = m_player->human().animator();
    const anim::AnimTask* top = animator.tasks().top();
    if (!animator.debugClipPlaying() || top == nullptr) {
        return {};
    }
    return debug::AnimPlayback{.playing = true,
                               .id = animator.animId(),
                               .clip = m_animClipName,
                               .set = m_animSetName,
                               .time = top->time(),
                               .duration = top->duration(),
                               .loop = top->type() == anim::AnimTaskType::Loop};
}

} // namespace coney::platform
