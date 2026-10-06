// SPDX-License-Identifier: GPL-3.0-or-later
#include "warriors/crime_reports.h"

#include <iterator>

namespace coney {

namespace {

// The gang kinds a report ignores as offenders: the police (1) and the kind that counts as police (0x17).
constexpr int kPoliceKind = 1;
constexpr int kPoliceLikeKind = 0x17;
// The HUD messages the report sends.
constexpr int kHudNewCrime = 0;
constexpr int kHudWantedOver = 0xb;

// Whether `type` names a crime type.
bool validType(int type) { return type >= 0 && type < crime::kTypes; }

// Types whose offender's gang only becomes a target: the police turn hostile, but not the gang to them.
bool oneWayHostility(int type) { return type == crime::kGangCurfew || type == crime::kTrespassing; }

// The responders' spawn kind for `type`: 1 for most, 3 for a precinct attack; nothing for the types that send none.
std::optional<int> responderKind(int type) {
    switch (type) {
    case crime::kAssault:
    case crime::kBreakAndEnter: // Coney's reading (the header)
    case crime::kCopAssault:
    case crime::kCopKill:
    case crime::kCustom: // Coney's reading (the header)
    case crime::kDisorderlyConduct:
    case crime::kMugging:
    case crime::kTheft:
    case crime::kVandalism:
        return 1;
    case crime::kPrecinctAttack:
        return 3;
    default:
        return std::nullopt;
    }
}

// Types that note the offender's gang's time (gang `+0x5f4`).
bool notesAssault(int type) {
    return type == crime::kAssault || type == crime::kCopAssault || type == crime::kCopKill || type == crime::kMugging;
}

// Types that score a player offender's statistic against the victim.
bool scoresVictim(int type) { return type == crime::kAssault || type == crime::kCopAssault || type == crime::kMugging; }

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

    // 4. Responders, when the report asks for them.
    if (sendResponders) {
        if (const std::optional<int> kind = responderKind(type)) {
            const int responding = type == crime::kCustom ? count : responders(type);
            const double delay = type == crime::kBreakAndEnter ? m_breakInDelay : 0.0;
            services.queueResponders(type, *kind, responding, at, delay);
        }
    }
    if (gang.has_value() && notesAssault(type)) {
        m_lastAssault[gang->id] = nowMs;
    }

    // 5. A break-in robs the store; an assault-like crime scores the player offender, once per victim.
    if (type == crime::kBreakAndEnter) {
        services.markStoreRobbed(at, gang.has_value() ? gang->id : -1);
    }
    if (scoresVictim(type) && victim != 0.0 && offender != 0.0 && services.isPlayer(offender) &&
        m_scoredVictims.insert(victim).second) {
        services.scoreAssault(offender, victim);
    }

    // 6. Player 1's gang: the last crime type (unless it holds 7 or 12) and the HUD.
    if (gang.has_value() && gang->id == services.playerOneGang()) {
        if (m_lastCrime != crime::kGangCurfew && m_lastCrime != crime::kTrespassing) {
            m_lastCrime = type;
        }
        services.notifyHud(kHudNewCrime);
    }
}

void CrimeReports::update(CrimeServices& services, std::uint64_t nowMs) {
    for (auto entry = m_wantedUntil.begin(); entry != m_wantedUntil.end();) {
        // Forced: the gang is held wanted at 10 s from now.
        if (m_forced) {
            entry->second = nowMs + kWantedMs;
            ++entry;
            continue;
        }
        if (nowMs < entry->second) {
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
}

float CrimeReports::wantedFraction(int gang, std::uint64_t nowMs) const {
    const auto found = m_wantedUntil.find(gang);
    if (found == m_wantedUntil.end() || nowMs >= found->second) {
        return 0.0F;
    }
    return static_cast<float>(found->second - nowMs) / static_cast<float>(kWantedMs);
}

std::optional<std::uint64_t> CrimeReports::lastAssault(int gang) const {
    const auto found = m_lastAssault.find(gang);
    return found != m_lastAssault.end() ? std::optional<std::uint64_t>(found->second) : std::nullopt;
}

void CrimeReports::clearLevel() {
    m_wantedUntil.clear();
    m_lastAssault.clear();
    m_scoredVictims.clear();
    m_scene.reset();
    m_lastCrime = crime::kNoCrime;
}

} // namespace coney
