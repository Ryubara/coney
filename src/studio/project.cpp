// SPDX-License-Identifier: GPL-3.0-or-later
// Stub for the tests-first commit: the tests compile against it and fail.
#include "studio/project.h"

namespace coney::studio {

std::expected<std::string, Error> normaliseProjectPath(std::string_view) {
    return fail(ErrorCode::Io, "not written yet");
}

std::expected<Project, Error> Project::create(const std::filesystem::path&, std::string_view, std::string_view,
                                              std::string_view) {
    return fail(ErrorCode::Io, "not written yet");
}

std::expected<Project, Error> Project::open(const std::filesystem::path&) {
    return fail(ErrorCode::Io, "not written yet");
}

std::expected<std::filesystem::path, Error> Project::resolve(std::string_view) const {
    return fail(ErrorCode::Io, "not written yet");
}

std::expected<std::string, Error> Project::projectPathOf(const std::filesystem::path&) const {
    return fail(ErrorCode::Io, "not written yet");
}

std::expected<std::vector<ProjectEntry>, Error> Project::list(std::string_view) const {
    return fail(ErrorCode::Io, "not written yet");
}

} // namespace coney::studio
