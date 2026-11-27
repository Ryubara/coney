// SPDX-License-Identifier: GPL-3.0-or-later
#include "warriors/crime_reports.h"

#include <iterator>

namespace coney {

namespace {

// The gang kinds a report ignores as offenders: the police (1) and the kind that counts as police (0x17).
constexpr int kPoliceKind = 1;
constexpr int kPoliceLikeKind = 0x17;
// The HUD messages the report sends: 7 (show; a constant, not the type) and 0xb (clear).
constexpr int kHudNewCrime = 7;
constexpr int kHudWantedOver = 0xb;

// Whether `type` names a crime type.
bool validType(int type) { return type >= 0 && type < crime::kTypes; }

// Types whose offender's gang only becomes a target: the police turn hostile, but not the gang to them.
bool oneWayHostility(int type) { return type == crime::kGangCurfew || type == crime::kTrespassing; }

// The brain kinds whose humans score as victims: a cop (1, crime event 3), a civilian (4) or a dealer (5) (event 0).
constexpr int kCopBrain = 1;
constexpr int kCivilianBrain = 4;
constexpr int kDealerBrain = 5;
constexpr int kCopVictimEvent = 3;
constexpr int kVictimEvent = 0;
// The crime event a store's new gang scores for its leader.
constexpr int kStoreClaimEvent = 1;
// The responders' spawn kinds: 1, or 3 for a precinct attack.
constexpr int kResponderKind = 1;
constexpr int kPrecinctResponderKind = 3;

} // namespace

void CrimeReports::setResponders(int type, int count) {
    if (validType(type)) {
        m_responders.at(static_cast<std::size_t>(type)) = count;
    }
}

int CrimeReports::responders(int type) const {
    return validType(type) ? m_responders.at(static_cast<std::size_t>(type)) : 0;
}

void CrimeReports::setEnabled(int type, bool on) {
    if (validType(type)) {
        m_enabled.at(static_cast<std::size_t>(type)) = on;
    }
}

bool CrimeReports::enabled(int type) const { return validType(type) && m_enabled.at(static_cast<std::size_t>(type)); }

void CrimeReports::report(CrimeServices& services, int type, const CrimePosition& at, double offender, double victim,
                          bool sendResponders, int count, std::uint64_t nowMs) {
    // 1. Nothing while reporting is off, or for a police offender.
    if (!m_reporting || !validType(type)) {
        return;
    }
    const std::optional<CrimeGang> gang = offender != 0.0 ? services.gangOf(offender) : std::nullopt;
    if (gang.has_value() && (gang->kind == kPoliceKind || gang->kind == kPoliceLikeKind)) {
        return;
    }

    ++m_reports;
    m_lastPosition = at;

    // 2. With an offender's gang: the police turn on it, it is wanted for 10 s, the callback runs.
    if (gang.has_value()) {
        services.makePoliceHostile(gang->id, !oneWayHostility(type));
        m_wantedUntil[gang->id] = nowMs + kWantedMs;
        if (!m_callback.empty()) {
            services.callCrimeCallback(m_callback, gang->id, type);
        }
    }

    // 3. The CrimeScene flag follows the crime when its position changed.
    if (!m_scene.has_value() || *m_scene != at) {
        m_scene = at;
        services.moveCrimeScene(at);
    }

    // 4. By type: the responders when the report asks for them (mode 1), the assault time, the victim's score and the
    // break-in's store.
    switch (type) {
    case crime::kAssault:
    case crime::kCopAssault:
    case crime::kMugging:
        if (sendResponders) {
            services.queueResponders(type, kResponderKind, responders(type), at, 0.0);
            if (gang.has_value()) {
                m_lastAssault[gang->id] = nowMs;
            }
        }
        scoreVictim(services, offender, victim);
        break;
    case crime::kBreakAndEnter:
        breakIn(services, at, offender, gang, sendResponders);
        break;
    case crime::kCopKill:
        if (sendResponders) {
            services.queueResponders(type, kResponderKind, responders(type), at, 0.0);
            if (gang.has_value()) {
                m_lastAssault[gang->id] = nowMs;
            }
        }
        break;
    case crime::kCustom:
        if (sendResponders && count != 0) {
            services.queueResponders(type, kResponderKind, count, at, 0.0);
        }
        break;
    case crime::kDisorderlyConduct:
    case crime::kTheft:
    case crime::kVandalism:
        if (sendResponders) {
            services.queueResponders(type, kResponderKind, responders(type), at, 0.0);
        }
        break;
    case crime::kPrecinctAttack:
        if (sendResponders) {
            services.queueResponders(type, kPrecinctResponderKind, responders(type), at, 0.0);
        }
        break;
    default: // a gang fight, a curfew, tagging, trespassing and no crime send no one
        break;
    }

    // 5. Player 1's gang: the last crime type (unless it holds 7 or 12) and the HUD.
    if (gang.has_value() && gang->id == services.playerOneGang()) {
        if (m_lastCrime != crime::kGangCurfew && m_lastCrime != crime::kTrespassing) {
            m_lastCrime = type;
        }
        services.notifyHud(kHudNewCrime);
    }
}

void CrimeReports::breakIn(CrimeServices& services, const CrimePosition& at, double offender,
                           const std::optional<CrimeGang>& gang, bool sendResponders) {
    // No store front within 10 m: nothing.
    const std::optional<CrimeStore> store = services.nearestStore(at);
    if (!store.has_value()) {
        return;
    }
    // Not yet robbed: the responders after the break-in delay, with no offender, then the store robbed and its alarm.
    // **Not ported (#1008)**: the store's buyers and browsers switched off (`Flags_DisableGangActivity`) and game
    // state `+0x2b4` cleared.
    if (!store->robbed) {
        if (sendResponders) {
            services.queueResponders(crime::kBreakAndEnter, kResponderKind, responders(crime::kBreakAndEnter), at,
                                     m_breakInDelay);
        }
        services.robStore(at);
        return;
    }
    // Robbed already, by no gang yet: the offender's gang takes it, and its leader scores when he is a player.
    if (store->gang == CrimeStore::kNoGang && gang.has_value()) {
        services.claimStore(at, gang->id);
        if (const std::optional<double> leader = services.gangLeader(offender);
            leader.has_value() && services.playerIndex(*leader) >= 0) {
            services.scoreCrime(*leader, kStoreClaimEvent);
        }
    }
}

void CrimeReports::scoreVictim(CrimeServices& services, double offender, double victim) {
    // **Coney's choice**: with no offender the original reads a player index from address 0x1b0; Coney scores none.
    if (offender == 0.0 || victim == 0.0) {
        return;
    }
    const int player = services.playerIndex(offender);
    if (player < 0) {
        return;
    }
    const int brain = services.brainType(victim);
    if (brain != kCopBrain && brain != kCivilianBrain && brain != kDealerBrain) {
        return;
    }
    // Once per victim and player (a bit per player on the victim's brain).
    if (m_scoredVictims.emplace(victim, player).second) {
        services.scoreCrime(offender, brain == kCopBrain ? kCopVictimEvent : kVictimEvent);
    }
}

void CrimeReports::update(CrimeServices& services, std::uint64_t nowMs) {
    // **Not ported (#1008)**: the third timer (gang `+0x5ec`, the IssueWarning goal's) cleared once past.
    std::set<int> held; // wanted gangs held by ForceCrimeLevel: their second timers are not checked
    for (auto entry = m_wantedUntil.begin(); entry != m_wantedUntil.end();) {
        // Forced: the gang is held wanted at 10 s from now, and nothing else of it is checked.
        if (m_forced) {
            entry->second = nowMs + kWantedMs;
            held.insert(entry->first);
            ++entry;
            continue;
        }
        if (nowMs <= entry->second) {
            ++entry;
            continue;
        }
        const int gang = entry->first;
        entry = m_wantedUntil.erase(entry);
        services.clearPoliceHostility(gang);
        if (gang == services.playerOneGang()) {
            services.notifyHud(kHudWantedOver);
            m_lastCrime = crime::kNoCrime;
        }
    }
    // The second timers run out once past. **Not ported (#1008)**: the all-clear callback (`SetAllClearCallBack`,
    // game state `+0x2bc`) at the end of player 1's gang's.
    std::erase_if(m_secondUntil,
                  [nowMs, &held](const auto& entry) { return !held.contains(entry.first) && nowMs > entry.second; });
}

float CrimeReports::wantedFraction(int gang, std::uint64_t nowMs) const {
    const auto found = m_wantedUntil.find(gang);
    if (found == m_wantedUntil.end() || nowMs >= found->second) {
        return 0.0F;
    }
    return static_cast<float>(found->second - nowMs) / static_cast<float>(kWantedMs);
}

float CrimeReports::secondWantedFraction(int gang, std::uint64_t nowMs) const {
    const auto found = m_secondUntil.find(gang);
    if (found == m_secondUntil.end() || nowMs >= found->second) {
        return 0.0F;
    }
    return static_cast<float>(found->second - nowMs) / static_cast<float>(kSecondWantedMs);
}

std::optional<std::uint64_t> CrimeReports::lastAssault(int gang) const {
    const auto found = m_lastAssault.find(gang);
    return found != m_lastAssault.end() ? std::optional<std::uint64_t>(found->second) : std::nullopt;
}

void CrimeReports::clearLevel() {
    m_wantedUntil.clear();
    m_secondUntil.clear();
    m_lastAssault.clear();
    m_scoredVictims.clear();
    m_scene.reset();
    m_lastCrime = crime::kNoCrime;
}

} // namespace coney
