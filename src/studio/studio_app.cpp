// SPDX-License-Identifier: GPL-3.0-or-later
// Stub for the tests-first commit: the tests compile against it and fail.
#include "studio/studio_app.h"

#include <utility>

#include "platform/imgui_model_view.h"

namespace coney::studio {

StudioApp::StudioApp(StudioHost& host, StudioSettings settings)
    : m_host(host), m_settings(std::move(settings)), m_camera(FlyCamera({}, 0.0F, 0.0F)) {}

StudioApp::~StudioApp() = default;

void StudioApp::setFonts(ImFont*, ImFont*, ImFont*) {}

std::expected<void, Error> StudioApp::newProject(const std::filesystem::path&, std::string_view, std::string_view) {
    return fail(ErrorCode::Io, "not written yet");
}

std::expected<void, Error> StudioApp::openProject(const std::filesystem::path&) {
    return fail(ErrorCode::Io, "not written yet");
}

std::expected<void, Error> StudioApp::openLevel(std::string_view) { return fail(ErrorCode::Io, "not written yet"); }

void StudioApp::select(std::optional<EntityId>) {}

void StudioApp::frameSelection() {}

void StudioApp::startFraming() {}

bool StudioApp::goToView(std::string_view) { return false; }

std::expected<void, Error> StudioApp::runInConey() { return fail(ErrorCode::Io, "not written yet"); }

void StudioApp::log(ConsoleLine::Level, std::string) {}

std::string StudioApp::windowTitle() const { return {}; }

void StudioApp::refreshInspector() {}

void StudioApp::report(std::string_view, const Error&) {}

ViewPose StudioApp::blendedPose(float) const { return {}; }

void StudioApp::frame(std::uint32_t, float, float) {}

} // namespace coney::studio
