// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/parse_number.h"

#include <charconv>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <string>
#include <system_error>

namespace coney {

std::optional<double> parseDecimal(std::string_view text) {
    if (text.empty()) {
        return std::nullopt;
    }
    double value = 0.0;
#if defined(__cpp_lib_to_chars)
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
#else
    // strtod reads more than from_chars does (leading spaces or `+`, hexadecimal), so those are refused first; the
    // copy gives it the terminating null it needs.
    const char first = text.front();
    if (first == '+' || first == ' ' || first == '\t' || first == '\n' || first == '\r' || first == '\v' ||
        first == '\f' || text.find_first_of("xX") != std::string_view::npos) {
        return std::nullopt;
    }
    const std::string copy(text);
    char* end = nullptr;
    errno = 0;
    value = std::strtod(copy.c_str(), &end);
    if (errno == ERANGE || end != copy.c_str() + copy.size()) {
        return std::nullopt;
    }
#endif
    if (!std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

} // namespace coney
