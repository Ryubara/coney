// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/profile_management_gui/pm_profile_screens.h"

#include <string>
#include <utility>
#include <vector>

#include "core/pad.h"
#include "gui/profile_management_gui/pm_greet.h"
#include "gui/text_layout.h"

namespace coney::gui {

namespace {

// The profile in the session's chosen slot, or null.
const Profile* chosenProfile(const PmShared& shared) {
    if (shared.profiles == nullptr || !shared.session.slot) {
        return nullptr;
    }
    return shared.profiles->profile(*shared.session.slot);
}

// PM_Load and PM_Continue's way out with a profile: load it, mark it in use and end the menus.
void loadAndFinish(PmShared& shared, std::size_t slot) {
    if (shared.profiles != nullptr) {
        shared.profiles->load(slot);
        shared.profiles->setInUse(true);
    }
    shared.session.done = true;
}

} // namespace

// PM_NumPlayers

void PmNumPlayers::setUp() {
    m_choices.set(
        {{std::string(m_shared.string(kOnePlayerString)), 0}, {std::string(m_shared.string(kTwoPlayersString)), 1}},
        {2});
    m_waiting = false;
    placePmText(m_prompt, m_shared.string(kPromptString), pm_look::kX, kPromptY, pm_look::kItemScale, pm_look::kRed,
                kBigFontSlot);
    m_prompt.setVisible(false);
}

int PmNumPlayers::accept(int code) {
    if (code == 0) {
        if (m_shared.state != nullptr) {
            m_shared.state->twoPlayers = false;
        }
        return 0;
    }
    // Two players: the first accept asks player 2 for START; the accept START sends (poll()) chooses.
    if (!m_waiting) {
        m_waiting = true;
        m_promptShownMs = m_shared.frame.timeMs;
        return kStay;
    }
    if (m_shared.state != nullptr) {
        m_shared.state->twoPlayers = true;
    }
    return 0;
}

int PmNumPlayers::direction(MenuCommand command) {
    const int result = moveIn(m_choices, command);
    if (m_choices.selected() == 0) {
        m_waiting = false;
    }
    return result;
}

int PmNumPlayers::poll() {
    const Pad* second = m_shared.secondPad;
    if (!m_waiting || second == nullptr || faded() || (second->pressed() & pad::kStart) == 0) {
        return kStay;
    }
    return handle(MenuCommand::Accept);
}

void PmNumPlayers::drawExtras() {
    m_prompt.setVisible(m_waiting);
    if (m_waiting) {
        m_prompt.setFade(static_cast<float>(PmGreet::promptAlpha(m_promptShownMs, m_shared.frame.timeMs)) / 255.0F);
    }
    m_prompt.update(m_shared.frame);
    m_prompt.render(m_shared.canvas);
}

// PM_Profile

void PmProfile::setUp() {
    m_title = m_shared.string(kTitleString);
    const ProfileStore* store = m_shared.profiles;
    const std::size_t count = store != nullptr ? store->count() : 0;
    const bool room = store != nullptr && store->hasRoom();
    std::vector<PmChoices::Item> items;
    if (count > 0) {
        items.push_back({std::string(m_shared.string(kUseString)), kUse});
    }
    if (count == 0 || (count != ProfileStore::kSlots && room)) {
        items.push_back({std::string(m_shared.string(kCreateString)), kCreate});
    }
    if (count > 0) {
        items.push_back({std::string(m_shared.string(kDeleteString)), kDelete});
    }
    items.push_back({std::string(m_shared.string(kReloadString)), kReload});
    m_choices.set(std::move(items));
}

int PmProfile::accept(int code) {
    m_shared.session.deleteMode = false;
    switch (code) {
    case kUse:
        return 0;
    case kCreate:
        return 1;
    case kDelete:
        m_shared.session.deleteMode = true;
        return 2;
    default:
        callScript("Menu.reloadProfiles");
        return kStay;
    }
}

// PM_Load

void PmLoad::setUp() {
    m_title = m_shared.string(m_shared.session.deleteMode ? kDeleteTitleString : kTitleString);
    std::vector<PmChoices::Item> items;
    if (m_shared.profiles != nullptr) {
        for (std::size_t slot = 0; slot < ProfileStore::kSlots; ++slot) {
            if (const Profile* profile = m_shared.profiles->profile(slot)) {
                items.push_back({profile->name, static_cast<int>(slot)});
            }
        }
    }
    // Two a row: {n}, {2, n - 2} or {2, 2, n - 4}.
    const std::size_t n = items.size();
    std::vector<std::size_t> rows;
    if (n < 3) {
        rows = {n};
    } else if (n <= 4) {
        rows = {2, n - 2};
    } else {
        rows = {2, 2, n - 4};
    }
    if (n == 0) {
        rows.clear();
    }
    m_choices.set(std::move(items), std::move(rows));
}

int PmLoad::accept(int code) {
    const auto slot = static_cast<std::size_t>(code);
    m_shared.session.slot = slot;
    const Profile* profile = m_shared.profiles != nullptr ? m_shared.profiles->profile(slot) : nullptr;
    if (m_shared.session.deleteMode || (profile != nullptr && profile->damaged)) {
        return kToDelete;
    }
    loadAndFinish(m_shared, slot);
    return kStay;
}

// PM_Continue

void PmContinue::setUp() {
    m_choices.set(
        {{std::string(m_shared.string(kContinueString)), 0}, {std::string(m_shared.string(kDeleteString)), 1}}, {2});
    const Profile* profile = chosenProfile(m_shared);
    placePmText(m_name, profile != nullptr ? std::string_view(profile->name) : std::string_view{}, pm_look::kX, kNameY,
                pm_look::kNameScale, pm_look::kGrey, kBigFontSlot);
}

int PmContinue::accept(int code) {
    if (code == 1) {
        return 1;
    }
    if (m_shared.session.slot) {
        loadAndFinish(m_shared, *m_shared.session.slot);
    }
    return kStay;
}

void PmContinue::drawExtras() {
    m_name.update(m_shared.frame);
    m_name.render(m_shared.canvas);
}

// PM_Delete

void PmDelete::setUp() {
    m_choices.set({{std::string(m_shared.string(kYesString)), 0}, {std::string(m_shared.string(kNoString)), 1}}, {2},
                  1);
    const Profile* profile = chosenProfile(m_shared);
    m_damaged = profile != nullptr && profile->damaged;
    placePmText(m_name, profile != nullptr ? std::string_view(profile->name) : std::string_view{}, pm_look::kX, kNameY,
                pm_look::kNameScale, pm_look::kGrey, kBigFontSlot);
    placePmText(m_question, m_shared.string(m_damaged ? kDamagedString : kSureString), pm_look::kX, kQuestionY,
                pm_look::kMessageScale, pm_look::kRed, kTextFontSlot);
}

int PmDelete::accept(int code) {
    if (code != 0) {
        return kBack;
    }
    if (m_shared.profiles != nullptr && m_shared.session.slot) {
        m_shared.profiles->remove(*m_shared.session.slot);
    }
    callScript(kDeleteFunction);
    return m_damaged ? 2 : 0;
}

void PmDelete::drawExtras() {
    for (TextWidget* text : {&m_name, &m_question}) {
        text->update(m_shared.frame);
        text->render(m_shared.canvas);
    }
}

void PmDelete::closeExtras() {
    m_name.shutdown();
    m_question.shutdown();
}

} // namespace coney::gui
