// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/cpak_archive.h"

namespace coney::cpak {

std::expected<Archive, Error> Archive::open(std::span<const std::byte>) { return fail(ErrorCode::Invalid, "stub"); }

std::expected<std::span<const std::byte>, Error> Archive::view(const Entry&) const {
    return fail(ErrorCode::Invalid, "stub");
}

std::expected<std::span<const std::byte>, Error> Archive::verified(const Entry&) const {
    return fail(ErrorCode::Invalid, "stub");
}

std::expected<std::span<const std::byte>, Error> Archive::read(std::string_view) const {
    return fail(ErrorCode::Invalid, "stub");
}

} // namespace coney::cpak
