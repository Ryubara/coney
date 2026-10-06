// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/voice_table.h"

#include <format>

#include "core/name_hash.h"

namespace coney::audio {

namespace {

// The speech commands by id, space-separated: the names of the game's table at 0x0050aaa8 (docs/references/speech.md,
// from research/references/speech.yaml).
constexpr std::string_view kCommandNameText =
    "nothing attack follow defend hold steal vandal grunt_x swear scream throw block pain near mount "
    "point cheer1 cheer2 range mug statement response spot search give_up arrested slag safe resist "
    "resist2 item no_item avoid rat agony search2 alert nopaint throw2 mugcop fall copradio cop1011 "
    "cop1064v cop1034m cop1034i cop1034f cop1039 cop1034p cop1034c copresist cop1034kp cop1034kc "
    "cop1082b cop1082rb cop1011co cop1034cco cop1034fco cop1034ico cop1034kpco cop1034mco cop1034pco "
    "cop1039co cop1064vco copresistco lecturer listener unarrest_thank unarrest_reasure hide holdhide "
    "engage scared beg holdline revive_thank revive_reasure statement2 response2 mugcheer tagcheer "
    "garbage warm_alone tagdone phone_cop phone_ped follow_hide kingohill fuck_you riot cop1011t "
    "cop1011tco range2 mugcopcheer greet offer cash nocash leave_buy leave_nobuy fight_near limit chase "
    "dirty_resist dirty_item ripoff hide_response onfire cb_wear cb_whine hat_wear rules shoot nada pt "
    "headcrack offer2 fight2 narc walkaway drain roll trips letsgo fight mumble unarrest_help catch_rat "
    "stealitem hurryup defend_resp follow_resp attack_resp hold_resp nosearch shadow shadow_spot "
    "mug_grunt chase_cop travel vandal_resp steal_resp vandalitem whoop cheer3 bum_beg give_me "
    "bum_beg_resp kicked tired energy heavylift lightlift leader_charge leader_regroup leader_rush "
    "leader_objpile leader_getobj scatter scatter_resp rage clow craps_w craps_l win lose push roar "
    "hide_stealth surprise bum_beg_deal meet_male meet_female beckon beckon_player hostile kiyap boo "
    "catcall question answer bye bum_hire ex_alert give_up_ne store_greet store_chat phone_gang cower "
    "dead_meat shake_bum shake_bum_resp shake_ho shake_ho_resp ex_kill turf_defend fall_in collect "
    "collect_resp busy dance workout pinball sprayface club help_girl hide_response";

// Splits kCommandNameText at its spaces, at compile time.
consteval std::array<std::string_view, kSpeechCommands> splitCommandNames() {
    std::array<std::string_view, kSpeechCommands> names{};
    std::string_view rest = kCommandNameText;
    for (std::string_view& name : names) {
        const std::size_t space = rest.find(' ');
        name = rest.substr(0, space);
        rest = space == std::string_view::npos ? std::string_view{} : rest.substr(space + 1);
    }
    return names;
}

constexpr std::array<std::string_view, kSpeechCommands> kCommandNames = splitCommandNames();

// The chance that means "always": no roll.
constexpr std::uint8_t kAlways = 100;

} // namespace

std::string_view speechCommandName(std::uint32_t command) {
    return command < kCommandNames.size() ? kCommandNames.at(command) : std::string_view{};
}

std::string voiceLineName(int voiceSet, std::uint32_t command, int line) {
    return std::format("vags/character/voices/{}/{}_{:02}", voiceSet, speechCommandName(command), line);
}

void VoiceTable::build(int sets, const std::function<bool(std::uint32_t hash)>& exists) {
    m_sets = sets > 0 ? sets : 0;
    m_entries.assign(static_cast<std::size_t>(m_sets) * kSpeechCommands, Entry{});
    for (int set = 0; set < m_sets; ++set) {
        for (std::uint32_t command = 0; command < kSpeechCommands; ++command) {
            Entry& counted = *entry(set, command);
            int count = 0;
            while (count < kMaxLines && exists(crc32(voiceLineName(set, command, count + 1)))) {
                ++count;
            }
            counted.count = static_cast<std::uint8_t>(count);
        }
    }
}

void VoiceTable::setPercent(int voiceSet, std::uint32_t command, std::uint8_t percent) {
    if (command >= kSpeechCommands) {
        return;
    }
    if (voiceSet == -1) {
        for (int set = 0; set < m_sets; ++set) {
            entry(set, command)->percent = percent;
        }
        return;
    }
    if (Entry* found = entry(voiceSet, command); found != nullptr) {
        found->percent = percent;
    }
}

std::optional<std::uint32_t> VoiceTable::nextLine(int voiceSet, std::uint32_t command, const RandomRange& random) {
    Entry* found = entry(voiceSet, command);
    if (found == nullptr) {
        return std::nullopt;
    }
    // The chance first: a roll above it says nothing.
    if (found->percent < kAlways && random(0, kAlways - 1) > found->percent) {
        return std::nullopt;
    }
    if (found->count == 0) {
        return std::nullopt;
    }
    const int line = found->next + 1;
    found->next = static_cast<std::uint8_t>(found->next + 1 >= found->count ? 0 : found->next + 1);
    return crc32(voiceLineName(voiceSet, command, line));
}

int VoiceTable::lines(int voiceSet, std::uint32_t command) const {
    const Entry* found = entry(voiceSet, command);
    return found != nullptr ? found->count : 0;
}

int VoiceTable::percent(int voiceSet, std::uint32_t command) const {
    const Entry* found = entry(voiceSet, command);
    return found != nullptr ? found->percent : 0;
}

// The entry of `voiceSet` for `command`, or null when either is out of range.
const VoiceTable::Entry* VoiceTable::entry(int voiceSet, std::uint32_t command) const {
    if (voiceSet < 0 || voiceSet >= m_sets || command >= kSpeechCommands) {
        return nullptr;
    }
    return &m_entries.at((static_cast<std::size_t>(voiceSet) * kSpeechCommands) + command);
}

VoiceTable::Entry* VoiceTable::entry(int voiceSet, std::uint32_t command) {
    if (voiceSet < 0 || voiceSet >= m_sets || command >= kSpeechCommands) {
        return nullptr;
    }
    return &m_entries.at((static_cast<std::size_t>(voiceSet) * kSpeechCommands) + command);
}

} // namespace coney::audio
