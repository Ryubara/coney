// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/profile_management_gui/pm_import.h"

namespace coney::gui {

// Stub for the test commit: offers nothing yet.

bool PmImport::offered(const PmShared& /*shared*/) { return false; }

void PmImport::setUp() { m_title = std::string(kImportText); }

int PmImport::accept(int /*code*/) { return kStay; }

int PmImport::direction(MenuCommand command) { return moveIn(m_choices, command); }

void PmImport::describeSelected() {}

void PmImport::showLine(std::string text, bool /*problem*/) { m_lineText = std::move(text); }

void PmImport::drawExtras() {}

} // namespace coney::gui
