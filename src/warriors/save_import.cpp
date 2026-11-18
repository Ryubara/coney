// SPDX-License-Identifier: GPL-3.0-or-later
#include "warriors/save_import.h"

namespace coney {

// Stub for the test commit: imports nothing yet.

std::string_view describe(SaveSourceKind /*kind*/) { return "save"; }

std::expected<std::vector<ImportCandidate>, Error> readPs2DataFile(std::span<const std::byte> /*bytes*/,
                                                                   std::vector<std::string>& /*skipped*/) {
    return fail(ErrorCode::Invalid, "not read yet");
}

std::expected<OriginalSave, Error> readOriginalSave(std::span<const std::byte> /*bytes*/, std::string /*source*/) {
    return fail(ErrorCode::Invalid, "not read yet");
}

std::expected<OriginalSave, Error> readOriginalSave(const std::filesystem::path& /*path*/) {
    return fail(ErrorCode::Invalid, "not read yet");
}

std::expected<std::size_t, Error> importProfile(const ProfileRecord& /*record*/, ProfileStore& /*store*/) {
    return fail(ErrorCode::Invalid, "not imported yet");
}

std::size_t ImportScan::profileCount() const { return 0; }

ImportScan scanImportFolder(const std::filesystem::path& /*folder*/) { return {}; }

} // namespace coney
