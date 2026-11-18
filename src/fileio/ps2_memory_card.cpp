// SPDX-License-Identifier: GPL-3.0-or-later
#include "fileio/ps2_memory_card.h"

#include <algorithm>

namespace coney::io {

// Stub for the test commit: reads nothing yet.

const Ps2SaveFile* Ps2SaveFolder::file(std::string_view name) const {
    const auto found = std::ranges::find(files, name, &Ps2SaveFile::name);
    return found != files.end() ? &*found : nullptr;
}

bool Ps2MemoryCard::looksLikeCard(std::span<const std::byte> /*image*/) { return false; }

std::expected<Ps2MemoryCard, Error> Ps2MemoryCard::parse(std::span<const std::byte> /*image*/) {
    return fail(ErrorCode::Invalid, "not read yet");
}

std::expected<std::vector<std::byte>, Error> Ps2MemoryCard::readCluster(std::uint32_t /*cluster*/) const {
    return fail(ErrorCode::Invalid, "not read yet");
}

std::expected<std::uint32_t, Error> Ps2MemoryCard::fatEntry(std::uint32_t /*cluster*/) const {
    return fail(ErrorCode::Invalid, "not read yet");
}

std::expected<std::vector<std::byte>, Error> Ps2MemoryCard::readChain(std::uint32_t /*first*/,
                                                                      std::size_t /*length*/) const {
    return fail(ErrorCode::Invalid, "not read yet");
}

std::expected<std::vector<Ps2MemoryCard::Entry>, Error> Ps2MemoryCard::readDirectory(std::uint32_t /*first*/,
                                                                                     std::uint32_t /*count*/) const {
    return fail(ErrorCode::Invalid, "not read yet");
}

std::expected<std::vector<Ps2MemoryCard::Entry>, Error> Ps2MemoryCard::rootEntries() const {
    return fail(ErrorCode::Invalid, "not read yet");
}

std::expected<std::vector<std::string>, Error> Ps2MemoryCard::folders() const {
    return fail(ErrorCode::Invalid, "not read yet");
}

std::expected<Ps2SaveFolder, Error> Ps2MemoryCard::folder(std::string_view /*name*/) const {
    return fail(ErrorCode::Invalid, "not read yet");
}

bool looksLikePsu(std::span<const std::byte> /*bytes*/) { return false; }

std::expected<Ps2SaveFolder, Error> readPsuExport(std::span<const std::byte> /*bytes*/) {
    return fail(ErrorCode::Invalid, "not read yet");
}

} // namespace coney::io
