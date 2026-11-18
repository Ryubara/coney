// SPDX-License-Identifier: GPL-3.0-or-later
#include "console/command_picker.h"

namespace coney::console {

void CommandPicker::reset() {}

bool CommandPicker::apply(PickerInput /*input*/, PlayerSlot /*slot*/) { return true; }

std::vector<PickerRow> CommandPicker::rows() const { return {}; }

std::string CommandPicker::line() const { return {}; }

std::vector<const Command*> CommandPicker::commands() const { return {}; }

const Command* CommandPicker::chosen() const { return nullptr; }

std::vector<std::string> CommandPicker::choices(std::size_t /*index*/) const { return {}; }

void CommandPicker::stepValue(std::size_t /*index*/, int /*step*/) {}

void CommandPicker::choose(PlayerSlot /*slot*/) {}

} // namespace coney::console
