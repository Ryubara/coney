// SPDX-License-Identifier: GPL-3.0-or-later
#include "fileio/fatx_volume.h"

namespace coney::io {

// Stub until the reader lands: nothing is a FATX volume.
bool FatxVolume::looksLikeFatx(Stream& /*stream*/, std::uint64_t /*offset*/) { return false; }

std::expected<FatxVolume, Error> FatxVolume::open(Stream& /*stream*/, std::uint64_t /*offset*/,
                                                  std::uint64_t /*size*/) {
    return fail(ErrorCode::Invalid, "not read yet");
}

std::expected<std::vector<FatxEntry>, Error> FatxVolume::root() const {
    return fail(ErrorCode::Invalid, "not read yet");
}

std::expected<std::vector<FatxEntry>, Error> FatxVolume::list(const FatxEntry& /*folder*/) const {
    return fail(ErrorCode::Invalid, "not read yet");
}

std::expected<std::vector<std::byte>, Error> FatxVolume::read(const FatxEntry& /*file*/,
                                                              std::uint32_t /*limit*/) const {
    return fail(ErrorCode::Invalid, "not read yet");
}

} // namespace coney::io
