// SPDX-License-Identifier: GPL-3.0-or-later
// Stubs for the session record's tests: they compile and fail until the record is written.
#include "net/session_record.h"

namespace coney::net {

std::string formatPadLine(std::uint32_t /*step*/, const PadSample& /*pad*/) { return {}; }

std::string formatEventLine(std::uint32_t /*step*/, std::string_view /*text*/) { return {}; }

std::expected<RecordLine, Error> parseRecordLine(std::string_view /*text*/) {
    return std::unexpected(Error{ErrorCode::Invalid, "not written yet"});
}

std::expected<SessionReplay, Error> SessionReplay::parse(std::string_view /*text*/) { return SessionReplay{}; }

std::optional<PadSample> SessionReplay::pad(std::uint32_t /*step*/) const { return std::nullopt; }

} // namespace coney::net
