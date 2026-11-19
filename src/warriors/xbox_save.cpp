// SPDX-License-Identifier: GPL-3.0-or-later
#include "warriors/xbox_save.h"

namespace coney {

// Stubs until the Xbox importer lands: no Xbox save is read.
std::expected<ProfileRecord, Error> parseXboxRecord(std::span<const std::byte> /*bytes*/) {
    return fail(ErrorCode::Invalid, "not read yet");
}

std::optional<std::string> xboxSaveName(std::span<const std::byte> /*saveMeta*/) { return std::nullopt; }

std::vector<ImportCandidate> readXboxSaveFolders(const std::vector<XboxSaveFiles>& /*folders*/,
                                                 std::vector<std::string>& /*skipped*/) {
    return {};
}

std::optional<std::expected<OriginalSave, Error>> readXboxFolder(const std::filesystem::path& /*path*/) {
    return std::nullopt;
}

std::optional<std::expected<OriginalSave, Error>> readXboxImage(io::Stream& /*stream*/, std::string /*source*/) {
    return std::nullopt;
}

} // namespace coney
