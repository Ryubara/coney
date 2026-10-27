// SPDX-License-Identifier: GPL-3.0-or-later
// The gangs of the debug menus' spawned fighters (docs/guides/debug-menu.md#pages). @coney-only debug tooling: nothing
// in the original corresponds, but every relation is made with the original's gang calls.
#include "ai/fighter_sides.h"

#include <cstdint>
#include <string>

namespace coney::ai {

namespace {

// The name of a side's first gang; the next ones add their number.
std::string_view baseName(FighterSide side) {
    switch (side) {
    case FighterSide::Enemy:
        return "Fighters";
    case FighterSide::Ally:
        return "Allies";
    case FighterSide::Neutral:
        return "Neutrals";
    }
    return "Fighters";
}

// How many names of one side are tried before giving up (the gangs have 32 slots, so this is never reached).
constexpr int kMaxNames = 64;

// Whether gang `id` has its bit in `mask`.
bool hasBit(std::uint32_t mask, int id) { return (mask & (1U << static_cast<unsigned>(id))) != 0; }

} // namespace

Gang* FighterSides::alive(const Made& made) const {
    Gang* gang = m_gangs->find(made.id);
    return gang != nullptr && gang->name() == made.name ? gang : nullptr;
}

std::vector<Gang*> FighterSides::gangsOf(FighterSide side) const {
    std::vector<Gang*> found;
    for (const Made& made : m_made[static_cast<std::size_t>(side)]) {
        if (Gang* gang = alive(made); gang != nullptr) {
            found.push_back(gang);
        }
    }
    return found;
}

Gang* FighterSides::gangWithRoom(FighterSide side, const Gang* playersGang) {
    std::vector<Made>& made = m_made[static_cast<std::size_t>(side)];
    // A gang of ours that has room (fighters cleared leave room behind), else a new one.
    Gang* last = nullptr;
    for (const Made& entry : made) {
        if (Gang* gang = alive(entry); gang != nullptr) {
            if (gang->members().size() < gang->capacity()) {
                return gang;
            }
            last = gang;
        }
    }
    int kind = kFighterGangKind;
    if (side == FighterSide::Ally) {
        kind = playersGang != nullptr ? playersGang->kind() : kWarriorsKind;
    } else if (side == FighterSide::Neutral) {
        kind = kNeutralGangKind;
    }
    for (int number = static_cast<int>(made.size()) + 1; number <= kMaxNames; ++number) {
        // The first gang of a side has the plain name; a script's gang under it makes the next try the numbered one.
        const std::string name =
            number == 1 ? std::string(baseName(side)) : std::string(baseName(side)) + std::to_string(number);
        const int id = m_gangs->create(kind, name);
        if (id >= 0) {
            made.push_back(Made{.id = id, .name = name});
            return m_gangs->find(id);
        }
    }
    // Every slot is in use: the last gang of the side takes it, as a full gang does (its first member leaves).
    return last;
}

void FighterSides::relate(FighterSide side, Gang& gang, Gang* playersGang) {
    switch (side) {
    case FighterSide::Enemy:
        // Enemies of the player's camp, and so of his allies.
        if (playersGang != nullptr) {
            m_gangs->makeEnemies(playersGang->id(), gang.id());
        }
        for (const Gang* ally : gangsOf(FighterSide::Ally)) {
            m_gangs->makeEnemies(ally->id(), gang.id());
        }
        break;
    case FighterSide::Ally:
        // The player's camp: his friends and whoever he fights, as his gang has them now.
        if (playersGang != nullptr) {
            m_gangs->makeFriends(gang.id(), playersGang->id());
            for (int id = 0; id < static_cast<int>(kGangSlots); ++id) {
                const Gang* other = m_gangs->find(id);
                if (other == nullptr || other == &gang || other == playersGang) {
                    continue;
                }
                if (hasBit(playersGang->enemyMask(), id)) {
                    m_gangs->makeEnemies(gang.id(), id);
                } else if (hasBit(playersGang->friendMask(), id)) {
                    m_gangs->makeFriends(gang.id(), id);
                }
            }
        }
        for (const Gang* enemy : gangsOf(FighterSide::Enemy)) {
            m_gangs->makeEnemies(gang.id(), enemy->id());
        }
        break;
    case FighterSide::Neutral:
        // A gang of its own: no bit either way.
        break;
    }
}

int FighterSides::join(FighterSide side, Brain& fighter, const Brain& player) {
    Gang* playersGang = player.gang();
    Gang* gang = gangWithRoom(side, playersGang);
    if (gang == nullptr) {
        return -1;
    }
    // Every live gang of the side, the full ones too: the player's gang may have gained enemies since they filled.
    for (Gang* ours : gangsOf(side)) {
        relate(side, *ours, playersGang);
    }
    m_gangs->addMember(gang->id(), fighter);
    return gang->id();
}

std::optional<FighterSide> FighterSides::sideOf(const Gang* gang) const {
    if (gang == nullptr) {
        return std::nullopt;
    }
    for (const FighterSide side : kFighterSides) {
        for (const Made& made : m_made[static_cast<std::size_t>(side)]) {
            if (made.id == gang->id() && made.name == gang->name()) {
                return side;
            }
        }
    }
    return std::nullopt;
}

} // namespace coney::ai
