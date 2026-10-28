// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/cpak_index.h"

namespace coney::cpak {

std::string_view compressionName(Compression) { return "unknown"; }

std::expected<void, Error> checkName(std::string_view) { return fail(ErrorCode::Invalid, "stub"); }

std::expected<Index, Error> Index::parse(std::span<const std::byte>, std::uint64_t) {
    return fail(ErrorCode::Invalid, "stub");
}

std::expected<Index, Error> Index::read(io::Stream&) { return fail(ErrorCode::Invalid, "stub"); }

const Entry* Index::find(std::string_view) const { return nullptr; }

std::expected<void, Error> verifyEntry(const Entry&, std::span<const std::byte>) {
    return fail(ErrorCode::Invalid, "stub");
}

std::expected<std::vector<std::byte>, Error> readEntry(io::Stream&, const Entry&) {
    return fail(ErrorCode::Invalid, "stub");
}

} // namespace coney::cpak
