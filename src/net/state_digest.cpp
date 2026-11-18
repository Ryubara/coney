// SPDX-License-Identifier: GPL-3.0-or-later
// Stubs for the state digest's tests: they compile and fail until the digest is written.
#include "net/state_digest.h"

namespace coney::net {

void DigestHasher::add(std::uint64_t /*value*/, std::size_t /*bytes*/) {}

std::uint64_t humansDigest(const SnapshotHumans& /*humans*/) { return 0; }

std::uint64_t padsDigest(const PortSamples& /*pads*/) { return 0; }

std::optional<HumanDifference> firstDifference(const SnapshotHumans& /*first*/, const SnapshotHumans& /*second*/) {
    return std::nullopt;
}

std::string describeDifference(const HumanDifference& /*difference*/, const SnapshotHumans& /*first*/,
                               const SnapshotHumans& /*second*/) {
    return {};
}

std::string formatDigestLine(const DigestLine& /*line*/) { return {}; }

std::expected<DigestLine, Error> parseDigestLine(std::string_view /*text*/) {
    return std::unexpected(Error{ErrorCode::Invalid, "not written yet"});
}

DigestMatch matchDigests(std::span<const DigestLine> /*host*/, std::span<const DigestLine> /*guest*/) { return {}; }

} // namespace coney::net
