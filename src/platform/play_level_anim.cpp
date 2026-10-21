// SPDX-License-Identifier: GPL-3.0-or-later
// The debug menus' Animation player over a play mode (docs/guides/debug-menu.md#animation-player): the anim sets the
// player can play from and a clip of one played on him (human::Human::playDebugClip()). Coney's own tooling, in the
// debug tools' target (docs/guides/building.md#debug-tools): the original has nothing like it, and nothing here runs
// unless a person uses the page. The sets it loads are kept by the mode, whose player's tasks may hold their clips.
#include "platform/play_level_controls.h"

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

std::vector<std::string> PlayLevelControls::animSets() const {
    const PlayLevelMode& mode = *m_mode;
    std::vector<std::string> sets{std::format("Player: {}", mode.m_model), "Generic"};
    // Every model a player can be drawn as, once each, sorted: the models Change character offers. Every character
    // shares the player's skeleton (docs/research/characters.md, 32 bones), so each can play on him.
    std::set<std::string, std::less<>> models;
    for (const characters::CharacterChoice& choice : mode.characterChoices()) {
        models.insert(choice.model);
    }
    sets.insert(sets.end(), models.begin(), models.end());
    return sets;
}

std::expected<const characters::AnimSet*, Error> PlayLevelControls::animSetAt(std::size_t set) {
    PlayLevelMode& mode = *m_mode;
    if (set == kPlayerSet) {
        return &mode.playerCharacter().anims();
    }
    if (set == kGenericSet) {
        if (!mode.m_genericAnims) {
            mode.m_genericAnims = std::make_unique<characters::AnimSet>(mode.playerCharacter().generic(), nullptr);
        }
        return mode.m_genericAnims.get();
    }
    const std::vector<std::string> sets = animSets();
    if (set >= sets.size()) {
        return fail(ErrorCode::InvalidArgument, std::format("no anim set {}", set));
    }
    const std::string& model = sets[set];
    auto found = mode.m_animSetCharacters.find(model);
    if (found == mode.m_animSetCharacters.end()) {
        // The model's files, as a player of it would load them (no textures: nothing draws it).
        chunk::ChunkHandlerTable table = chunk::ChunkHandlerTable::withDefaults();
        characters::addCharacterDataHandlers(table);
        auto character = human::PlayerCharacter::load(mode.m_wad, table, model);
        if (!character) {
            return std::unexpected(std::move(character.error()));
        }
        auto own = std::make_unique<characters::AnimSet>((*character)->assets().data, nullptr);
        found = mode.m_animSetCharacters
                    .emplace(model,
                             PlayLevelMode::AnimSetCharacter{.character = std::move(*character), .own = std::move(own)})
                    .first;
        mode.m_print(std::format("anim: {} loaded for the Animation player\n", model));
    }
    return found->second.own.get();
}

std::expected<std::vector<debug::AnimClipChoice>, Error> PlayLevelControls::animClips(std::size_t set) {
    auto anims = animSetAt(set);
    if (!anims) {
        return std::unexpected(std::move(anims.error()));
    }
    return debug::clipChoicesOf(**anims);
}

std::expected<void, Error> PlayLevelControls::playAnim(std::size_t set, std::uint32_t id,
                                                       const debug::AnimPlayOptions& options) {
    PlayLevelMode& mode = *m_mode;
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
    mode.m_player->human().playDebugClip(*clip, id, (*anims)->rate(id) * options.speed, options.loop, options.inPlace);
    mode.m_animSetName = animSets()[set];
    mode.m_animClipName = clip->name;
    return {};
}

debug::AnimPlayback PlayLevelControls::animPlayback() const {
    const PlayLevelMode& mode = *m_mode;
    const human::HumanAnimator& animator = mode.m_player->human().animator();
    const anim::AnimTask* top = animator.tasks().top();
    if (!animator.debugClipPlaying() || top == nullptr) {
        return {};
    }
    return debug::AnimPlayback{.playing = true,
                               .id = animator.animId(),
                               .clip = mode.m_animClipName,
                               .set = mode.m_animSetName,
                               .time = top->time(),
                               .duration = top->duration(),
                               .loop = top->type() == anim::AnimTaskType::Loop};
}

} // namespace coney::platform
