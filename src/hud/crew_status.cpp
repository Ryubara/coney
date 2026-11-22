// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/crew_status.h"

namespace coney::hud {

// Stubs: the list is not built yet.
CrewState crewState(const CrewMember& /*member*/) { return CrewState::None; }

std::optional<int> crewPortraitKind(int /*type*/) { return std::nullopt; }

std::uint32_t crewPortraitRect(const CrewMember& /*member*/, const CrewFrame& /*frame*/) { return 0; }

std::string crewCaptionName(std::string_view name) { return std::string(name); }

std::string crewCaption(const CrewMember& /*member*/, CrewState /*state*/, const CrewTexts& /*texts*/) { return {}; }

float crewStatusAlpha(std::uint64_t /*nowMs*/) { return 1.0F; }

bool CrewStatusList::add(const CrewMember& /*member*/, const CrewFrame& /*frame*/, const CrewTexts& /*texts*/,
                         std::uint64_t /*nowMs*/) {
    return false;
}

bool CrewStatusList::remove(double /*handle*/) { return false; }

void CrewStatusList::update(const CrewFrame& /*frame*/) {}

void CrewStatusList::render(const HudCanvas& /*canvas*/, std::uint64_t /*nowMs*/) const {}

void CrewStatusList::clear() {}

const CrewStatusList::Entry* CrewStatusList::find(double /*handle*/) const { return nullptr; }

GuiPoint CrewStatusList::entryPlace(std::uint32_t /*order*/, std::size_t /*count*/) { return {}; }

GuiPoint CrewStatusList::captionPlaceFor(std::size_t /*placed*/, std::size_t /*count*/) { return {}; }

void CrewStatusList::updateEntry(Entry& /*entry*/, const CrewMember& /*member*/, const CrewFrame& /*frame*/) {}

} // namespace coney::hud
