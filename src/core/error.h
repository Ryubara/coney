// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

namespace coney {

/// What kind of thing went wrong; callers branch on this, people read `message`. One byte is plenty for the codes.
enum class ErrorCode : std::uint8_t {
    InvalidArgument, ///< A caller or the user passed something the function cannot accept (a bad command line).
    PlatformFailure, ///< The operating system or a platform library (SDL3, librw) refused to do its part.
};

/// A recoverable failure, returned through std::expected. The engine never throws.
struct Error {
    ErrorCode code;
    std::string message; ///< One line for people reading logs or a terminal; never parsed by code.
};

} // namespace coney
