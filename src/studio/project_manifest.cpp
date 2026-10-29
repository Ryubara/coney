// SPDX-License-Identifier: GPL-3.0-or-later
// Stub for the tests-first commit: the tests compile against it and fail.
#include "studio/project_manifest.h"

namespace coney::studio {

bool validProjectName(std::string_view) { return false; }

std::expected<ProjectManifest, Error> parseProjectManifest(std::string_view) {
    return fail(ErrorCode::Io, "not written yet");
}

std::string formatProjectManifest(const ProjectManifest&) { return {}; }

} // namespace coney::studio
