// SPDX-License-Identifier: GPL-3.0-or-later
#include "fileio/qcow2_image.h"

namespace coney::io {

// Stub until the reader lands: nothing is a qcow2 image.
bool Qcow2Image::looksLikeQcow2(Stream& /*stream*/) { return false; }

std::expected<Qcow2Image, Error> Qcow2Image::open(Stream& /*file*/) { return fail(ErrorCode::Invalid, "not read yet"); }

std::expected<void, Error> Qcow2Image::read(std::span<std::byte> /*destination*/) {
    return fail(ErrorCode::Invalid, "not read yet");
}

std::expected<void, Error> Qcow2Image::seek(std::uint64_t /*position*/) {
    return fail(ErrorCode::Invalid, "not read yet");
}

} // namespace coney::io
