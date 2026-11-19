// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/mission_select.h"

namespace coney::gui {

std::size_t mapColumn(bool /*flag02*/, bool /*widescreen*/, bool /*flag20*/) { return 0; }
std::optional<std::string_view> missionAreaName(const LevelRecord& /*record*/) { return std::nullopt; }
std::string missionLabel(const LevelRecord& /*record*/) { return {}; }
int labelNumber(std::string_view /*label*/) { return 0; }
MapPoint missionMapPoint(const LevelRecord& /*record*/, std::size_t /*column*/) { return {}; }
MapPoint missionLogoPoint(const LevelRecord& /*record*/, std::size_t /*column*/) { return {}; }
graphics::Rgba areaHaloColour(double /*area*/) { return {}; }
bool isMissionSelectListed(const LevelRecord& /*record*/, const UnlockRecords& /*unlocks*/,
                           const SavedProgress& /*progress*/) {
    return false;
}
std::vector<MissionSelectArea> buildMissionSelectAreas(const LevelTable& /*levels*/, const UnlockRecords& /*unlocks*/,
                                                       const SavedProgress& /*progress*/, std::size_t /*column*/) {
    return {};
}
MissionSelectLayout MissionSelectLayout::forFlags(bool /*flag02*/, bool /*widescreen*/, bool /*flag20*/) { return {}; }
void MissionSelectScreen::open(std::vector<MissionSelectArea> areas, std::uint64_t /*nowMs*/) {
    m_areas = std::move(areas);
}
void MissionSelectScreen::close() {}
void MissionSelectScreen::handle(MenuCommand /*command*/, std::uint64_t /*nowMs*/) {}
void MissionSelectScreen::update(const GuiFrame& /*frame*/) {}
void MissionSelectScreen::render(const GuiCanvas& /*canvas*/) const {}
std::size_t MissionSelectScreen::selectedMission() const { return 0; }
void MissionSelectScreen::onMenuCommand(MenuCommand /*command*/, std::uint64_t /*nowMs*/) {}
bool MissionSelectScreen::onListCommand(MenuCommand /*command*/, std::uint64_t /*nowMs*/) { return false; }
void MissionSelectScreen::nextArea() {}
void MissionSelectScreen::prevArea() {}
void MissionSelectScreen::showArea() {}
void MissionSelectScreen::fillArea() {}
void MissionSelectScreen::onAccept() {}
MissionSelectMission& MissionSelectScreen::current() { return m_areas.at(m_cursor).missions.at(0); }
void MissionSelectScreen::playCue(int /*cue*/) const {}

} // namespace coney::gui
