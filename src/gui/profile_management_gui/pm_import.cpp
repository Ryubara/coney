// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/profile_management_gui/pm_import.h"

#include <filesystem>
#include <format>
#include <optional>
#include <utility>

#include "gui/text_layout.h"

namespace coney::gui {

bool PmImport::offered(const PmShared& shared) {
    const ProfileStore* store = shared.profiles;
    if (store == nullptr) {
        return false;
    }
    const std::optional<std::filesystem::path> folder = store->importFolder();
    if (!folder || store->count() == ProfileStore::kSlots || !store->hasRoom()) {
        return false;
    }
    const ImportScan scan = scanImportFolder(*folder);
    return !scan.saves.empty() || !scan.problems.empty();
}

void PmImport::setUp() {
    m_title = std::string(kImportText);
    m_scan = {};
    m_listed.clear();
    m_sources.clear();
    if (m_shared.profiles != nullptr) {
        if (const std::optional<std::filesystem::path> folder = m_shared.profiles->importFolder()) {
            m_scan = scanImportFolder(*folder);
        }
    }
    // Every profile of every save, in the folder's order, up to the grid's six.
    std::vector<PmChoices::Item> items;
    for (const OriginalSave& save : m_scan.saves) {
        for (const ImportCandidate& candidate : save.profiles) {
            if (m_listed.size() == kMaxListed) {
                break;
            }
            items.push_back({candidate.record.name, static_cast<int>(m_listed.size())});
            m_listed.push_back(candidate);
            m_sources.push_back(save.source);
        }
    }
    // Two a row, as PM_Load lays out the profiles.
    const std::size_t n = items.size();
    std::vector<std::size_t> rows;
    if (n > 0 && n < 3) {
        rows = {n};
    } else if (n >= 3 && n <= 4) {
        rows = {2, n - 2};
    } else if (n > 4) {
        rows = {2, 2, n - 4};
    }
    m_choices.set(std::move(items), std::move(rows));
    if (!m_listed.empty()) {
        describeSelected();
    } else if (!m_scan.problems.empty()) {
        showLine(m_scan.problems.front(), true);
    } else {
        showLine("No PS2 save to import was found", true);
    }
}

int PmImport::accept(int code) {
    const auto index = static_cast<std::size_t>(code);
    if (index >= m_listed.size() || m_shared.profiles == nullptr) {
        return kStay;
    }
    const auto slot = importProfile(m_listed[index].record, *m_shared.profiles);
    if (!slot) {
        playSound(pm_cue::kRefused);
        showLine(slot.error().message, true);
        return kStay;
    }
    return kToProfile;
}

int PmImport::direction(MenuCommand command) {
    const int result = moveIn(m_choices, command);
    if (!m_listed.empty()) {
        describeSelected();
    }
    return result;
}

void PmImport::describeSelected() {
    const std::size_t index = m_choices.selected();
    if (index < m_listed.size()) {
        showLine(std::format("From {}, card slot {}", m_sources[index], m_listed[index].slot + 1), false);
    }
}

void PmImport::showLine(std::string text, bool problem) {
    m_lineText = std::move(text);
    placePmText(m_line, m_lineText, pm_look::kX, kLineY, pm_look::kMessageScale,
                problem ? pm_look::kRed : pm_look::kGrey, kTextFontSlot);
}

void PmImport::drawExtras() {
    m_line.update(m_shared.frame);
    m_line.render(m_shared.canvas);
}

} // namespace coney::gui
