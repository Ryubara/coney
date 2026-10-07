// SPDX-License-Identifier: GPL-3.0-or-later
// The gameplay mode's tagging (docs/research/crimes.md#tagging): HuTag's start, the tag spots' update and player 1's
// stick game on pad 1, and its end.
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <vector>

#include "ai/attack_kinds.h"
#include "ai/brain.h"
#include "ai/gangs.h"
#include "ai/scripted_story.h"
#include "animation/anim_math.h"
#include "characters/character_class.h"
#include "gamemodes/gameplay_mode.h"
#include "scripting/object_bindings.h"
#include "scripting/sound_bindings.h"
#include "scripting/story_bindings.h"
#include "warriors/tag_game.h"

namespace coney {

namespace {

// The speech commands of the tag (docs/research/crimes.md#tagging): no paint, a crew mate's cheer, and the tag done.
constexpr std::uint32_t kNoPaintCommand = 37;
constexpr std::uint32_t kTagCheerCommand = 80;
constexpr std::uint32_t kTagDoneCommand = 83;
// The Warrior command a crew mate's tag lines need: defend.
constexpr int kDefendCommand = 2;
// A later candidate replaces the pick when a roll of 0-99 is at least this.
constexpr int kReplaceRoll = 50;
// The event the tagger gets at the end: the tag's handle and whether it was finished (`GangTagComplete`).
constexpr int kTagEndEvent = 0xe;
// The tag spots update every second 60 Hz tick.
constexpr double kTicksPerSecond = 60.0;
constexpr double kTicksPerSpotUpdate = 2.0;
// The stick game's elapsed time is whole milliseconds.
constexpr double kMillisecondsPerSecond = 1000.0;

} // namespace

void GameplayMode::startTag(double human, double tag, double flag) {
    const HumanCreation* player = m_humans.player(1);
    if (player == nullptr || player->handle != human) {
        // Another human: the spot takes him as its tagger and fades in, and his spray starts (Coney's stand-in: no
        // walk to the flag and no spray animation first).
        m_log(std::format("tag: human {:.0f} sprays tag {:.0f} from flag {:.0f}\n", human, tag, flag));
        m_tagSpots.setTagger(tag, human);
        callTagStart(human, tag, flag);
        return;
    }
    if (m_tagSession || m_tagIntro) {
        return;
    }
    // A player with no paint says so, and he himself gets event 14 with the tag, not finished.
    if (!TagSession::hasPaint(m_state.player.inventory, 0)) {
        m_log(std::format("tag: no paint for tag {:.0f}\n", tag));
        if (m_context.sound != nullptr) {
            static_cast<void>(m_context.sound->sayCommand(
                script::CommandCall{.human = human, .command = kNoPaintCommand, .interrupt = true}, {}));
        }
        if (m_context.messages != nullptr) {
            m_context.messages->deliver(m_scripts, human, kTagEndEvent, 0.0, tag, 0.0);
        }
        return;
    }
    // The player stays where he pressed triangle, held still by the game, and his spray clips start: 334, turning to
    // face the tag, then the loop 335. The stick game goes live when the intro ends (beginTagSpray()).
    m_scripted->storyHost().lockMovement(human, true);
    m_tagIntro = TagIntro{.human = human, .tag = tag, .flag = flag};
    auto* scripted = dynamic_cast<ScriptedPlayer*>(m_level.get());
    const std::optional<anim::Vec3> at = promptObjectPosition(tag);
    const bool clips =
        scripted != nullptr && at.has_value() && scripted->startTagSpray(std::array<float, 3>{at->x, at->y, at->z});
    m_log(std::format("tag: player starts spraying tag {:.0f}{}\n", tag, clips ? "" : " (no spray clips)"));
    if (!clips) {
        beginTagSpray();
    }
}

void GameplayMode::beginTagSpray() {
    if (!m_tagIntro) {
        return;
    }
    const TagIntro intro = *m_tagIntro;
    m_tagIntro.reset();
    const HumanCreation* player = m_humans.player(1);
    if (player == nullptr || player->handle != intro.human) {
        return;
    }
    // The spray (0x0022e610): the stick game, the tag told its tagger (message 0, which starts its own fade in), and
    // last the start callback.
    const std::span<const float> pattern(m_state.story.tagPattern);
    const int difficulty = script::tagDifficulty(&m_recorded, characters::warriorClassOf(player->type));
    m_tagSession.emplace(m_tagSpots, m_state.player.inventory, 0, intro.human, intro.tag,
                         tagPath(pattern, m_state.story.tagPatternCount), tagTuning(difficulty));
    m_log(std::format("tag: player sprays tag {:.0f}, {} path points, difficulty {}\n", intro.tag,
                      m_tagSession->game().path().size(), difficulty));
    m_tagSpots.setTagger(intro.tag, intro.human);
    callTagStart(intro.human, intro.tag, intro.flag);
}

void GameplayMode::callTagStart(double human, double tag, double flag) {
    if (m_state.story.tagStartCallback.empty()) {
        return;
    }
    // Three handles and no result asked: whatever the function returns is dropped.
    static_cast<void>(
        m_scripts.call(m_state.story.tagStartCallback,
                       std::vector<script::Value>{script::Value(human), script::Value(tag), script::Value(flag)}));
}

void GameplayMode::updateTagging(const Pads& pads, double seconds) {
    // The spots' fades, every second tick.
    m_tagTicks += seconds * kTicksPerSecond;
    while (m_tagTicks >= kTicksPerSpotUpdate) {
        m_tagTicks -= kTicksPerSpotUpdate;
        m_tagSpots.update();
    }
    // The spray's intro: the stick game goes live when its loop starts; a body something else took ends it unsprayed.
    if (m_tagIntro) {
        const auto* scripted = dynamic_cast<const ScriptedPlayer*>(m_level.get());
        if (scripted == nullptr || scripted->tagSprayLooping()) {
            beginTagSpray();
        } else if (!scripted->tagSprayPlaying()) {
            m_log(std::format("tag: the spray of tag {:.0f} was cut short\n", m_tagIntro->tag));
            m_scripted->storyHost().lockMovement(m_tagIntro->human, false);
            m_tagIntro.reset();
        }
    }
    if (!m_tagSession) {
        return;
    }
    // Player 1's stick game on pad 1's left stick.
    const Pad& pad = pads.port(0);
    const auto elapsedMs = static_cast<std::uint32_t>(std::lround(seconds * kMillisecondsPerSecond));
    if (!m_tagSession->ended()) {
        m_tagSession->update(pad.leftX(), pad.leftY(), elapsedMs);
        if (m_tagSession->game().events().slipped) {
            m_log("tag: slipped off the pattern\n");
            const HumanCreation* tagger = m_humans.find(m_tagSession->human());
            if (tagger != nullptr && tagger->tagCheer) {
                sayTagLine(m_tagSession->human(), kTagCheerCommand);
            }
        }
    }
    if (!m_tagSession->ended()) {
        return;
    }
    // The end (Tag_End): the pad freed, the tag-done line on a finish, the tagger's event.
    const double human = m_tagSession->human();
    const double tag = m_tagSession->tag();
    const bool finished = m_tagSession->end().finished;
    m_log(std::format("tag: tag {:.0f} {}\n", tag, finished ? "finished" : "left unfinished"));
    m_tagSession.reset();
    m_scripted->storyHost().lockMovement(human, false);
    if (auto* scripted = dynamic_cast<ScriptedPlayer*>(m_level.get())) {
        scripted->endTagSpray();
    }
    if (finished) {
        sayTagLine(human, kTagDoneCommand);
    }
    if (m_context.messages != nullptr) {
        m_context.messages->deliver(m_scripts, human, kTagEndEvent, 0.0, tag, finished ? 1.0 : 0.0);
    }
}

void GameplayMode::sayTagLine(double tagger, std::uint32_t command) {
    // Only a war chief (Coney's reading: player 1) whose player last ordered defend.
    const HumanCreation* player = m_humans.player(1);
    if (!m_scripted || m_context.sound == nullptr || player == nullptr || player->handle != tagger ||
        m_state.characters.lastWarriorCommand.at(0) != kDefendCommand) {
        return;
    }
    const ai::Brain* chief = m_scripted->brain(tagger);
    ai::Gang* gang = chief != nullptr ? chief->gang() : nullptr;
    if (gang == nullptr) {
        return;
    }
    // The candidates in slot order: not the leader, not a player, free to act, no actions queued, and within the
    // tagger's far melee range; the first is picked and each later one replaces it on a roll of 50 or more.
    // **Coney's reading**: the busy test (`Human_IsBusy`'s state flags, not kept) is the actions-blocked test's.
    const script::StoryBindingHost* story = m_scripted->story();
    const ai::Brain* leader = gang->leader();
    const anim::Vec3 at = chief->human().position();
    const float range = chief->meleeFar();
    const ai::Brain* picked = nullptr;
    for (const ai::Brain* member : gang->members()) {
        if (member == nullptr || member == leader || member->type() == ai::BrainType::Player ||
            member->actionCount() > 0 || (story != nullptr && story->actionsBlocked(member->handle()))) {
            continue;
        }
        const anim::Vec3 there = member->human().position();
        if (std::hypot(there.x - at.x, there.y - at.y, there.z - at.z) > range) {
            continue;
        }
        if (picked == nullptr || ai::rollRange(gang->owner().random(), 0, 99) >= kReplaceRoll) {
            picked = member;
        }
    }
    if (picked == nullptr) {
        return;
    }
    // Only the pick is asked whether he may comment; nobody else is tried. **Coney's stand-in**: the gesture slots
    // (`Ambient_MayGesture`) are not kept, so any pick may gesture.
    const HumanCreation* made = m_humans.find(picked->handle());
    if (made != nullptr && !made->tagDoneSpeech) {
        return;
    }
    m_log(std::format("tag: human {:.0f} says {}\n", picked->handle(), command));
    static_cast<void>(m_context.sound->sayCommand(
        script::CommandCall{.human = picked->handle(), .command = command, .interrupt = false}, {}));
}

} // namespace coney
