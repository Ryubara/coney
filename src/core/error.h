// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <utility>

namespace coney {

/// What kind of thing went wrong; callers branch on this, people read `message`. One byte is plenty for the codes.
enum class ErrorCode : std::uint8_t {
    InvalidArgument, ///< A caller or the user passed something the function cannot accept (a bad command line).
    PlatformFailure, ///< The operating system or a platform library (SDL3, librw) refused to do its part.
    NotFound,        ///< A file, disc entry or WAD entry does not exist.
    Truncated,       ///< Data ended before its own header said it would.
    Invalid,         ///< Data is present but breaks its format's rules (a bad count, an unknown chunk type).
    Io,              ///< The operating system reported a read failure.
};

/// A recoverable failure, returned through std::expected. The engine never throws.
struct Error {
    ErrorCode code;
    std::string message; ///< One line for people reading logs or a terminal; never parsed by code.
};

/// Shorthand for the failure branch of a std::expected: `return fail(ErrorCode::Truncated, "...")`.
[[nodiscard]] inline std::unexpected<Error> fail(ErrorCode code, std::string message) {
    return std::unexpected(Error{code, std::move(message)});
}

} // namespace coney
