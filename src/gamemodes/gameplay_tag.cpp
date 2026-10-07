// SPDX-License-Identifier: GPL-3.0-or-later
// The gameplay mode's tagging (docs/research/crimes.md#tagging): HuTag's start, the tag spots' update and player 1's
// stick game on pad 1, and its end.
#include <cmath>
#include <cstdint>
#include <format>
#include <span>
#include <vector>

#include "ai/scripted_story.h"
#include "characters/character_class.h"
#include "gamemodes/gameplay_mode.h"
#include "scripting/object_bindings.h"
#include "scripting/sound_bindings.h"
#include "warriors/tag_game.h"

namespace coney {

namespace {

// The speech commands of the tag (docs/research/crimes.md#tagging): no paint, and the tag done.
constexpr std::uint32_t kNoPaintCommand = 37;
constexpr std::uint32_t kTagDoneCommand = 83;
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
    if (m_tagSession) {
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
    // The player sprays where he stands (Coney's stand-in for occupying the flag and the spray animation), held still
    // by the game; the stick game goes live at once, and with it the start callback.
    const std::span<const float> pattern(m_state.story.tagPattern);
    const int difficulty = script::tagDifficulty(&m_recorded, characters::warriorClassOf(player->type));
    m_tagSession.emplace(m_tagSpots, m_state.player.inventory, 0, human, tag,
                         tagPath(pattern, m_state.story.tagPatternCount), tagTuning(difficulty));
    m_log(std::format("tag: player sprays tag {:.0f}, {} path points, difficulty {}\n", tag,
                      m_tagSession->game().path().size(), difficulty));
    m_scripted->storyHost().lockMovement(human, true);
    callTagStart(human, tag, flag);
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
    if (finished && m_context.sound != nullptr) {
        // Coney's stand-in: the tagger says it (the original has a crew member say it).
        static_cast<void>(m_context.sound->sayCommand(
            script::CommandCall{.human = human, .command = kTagDoneCommand, .interrupt = true}, {}));
    }
    if (m_context.messages != nullptr) {
        m_context.messages->deliver(m_scripts, human, kTagEndEvent, 0.0, tag, finished ? 1.0 : 0.0);
    }
}

} // namespace coney
