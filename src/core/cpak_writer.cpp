// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/cpak_writer.h"

namespace coney::cpak {

std::expected<void, Error> Writer::add(std::string, std::span<const std::byte>) { return {}; }

std::expected<void, Error> Writer::add(std::string, std::uint64_t, const Sha256Digest&, Fetch) { return {}; }

std::expected<void, Error> Writer::write(const Sink&) const { return fail(ErrorCode::Invalid, "stub"); }

std::expected<std::vector<std::byte>, Error> Writer::toBytes() const { return fail(ErrorCode::Invalid, "stub"); }

} // namespace coney::cpak
